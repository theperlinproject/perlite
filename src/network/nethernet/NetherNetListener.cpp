#include "common/Json.hpp"
#include "common/Log.hpp"
#include "server/Server.hpp"
#include "network/Connection.hpp"

#include "network/nethernet/LanDiscovery.hpp"
#include "network/nethernet/ServerIdentity.hpp"
#include "network/nethernet/SignalingServer.hpp"

#include <rtc/rtc.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace perlite {
    namespace {
        constexpr auto kAnswerTimeout = std::chrono::seconds(4);

        std::string StripIdentityLines(const std::string& sdp) {
            std::istringstream in(sdp);
            std::string line, out;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.starts_with("a=identity"))
                    continue;
                out += line + "\r\n";
            }
            return out;
        }

        std::vector<std::string> SdpValues(const std::string& sdp, const std::string& prefix) {
            std::vector<std::string> out;
            std::istringstream in(sdp);
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.starts_with(prefix))
                    out.push_back(line.substr(prefix.size()));
            }
            return out;
        }

        class Peer final : public Connection, public std::enable_shared_from_this<Peer> {
        public:
            Peer(std::string remote, EventQueue& queue) : remote(std::move(remote)), queue(queue) {}

            bool Send(const uint8_t* data, size_t size, bool wantReliable) override {
                if (closed)
                    return false;
                std::shared_ptr<rtc::DataChannel> dc = wantReliable ? reliable : (unreliable ? unreliable : reliable);
                if (!dc || !dc->isOpen())
                    return false;
                try {
                    // The remote max-message-size decides how big a fragment may be (header byte included).
                    size_t part = kFragmentSize;
                    if (const size_t remoteMax = dc->maxMessageSize(); remoteMax > 1)
                        part = std::min(part, remoteMax - 1);
                    if (dc != reliable && size > part)
                        dc = reliable;
                    const auto parts = SplitFragments(data, size, part);
                    if (parts.empty())
                        return size == 0;
                    for (const auto& p : parts) {
                        rtc::binary msg(reinterpret_cast<const std::byte*>(p.data()),
                                        reinterpret_cast<const std::byte*>(p.data()) + p.size());
                        // send() returns false when the message was queued behind SCTP congestion
                        // control rather than written at once. It is still delivered in order, so
                        // that is not a failure; only an exception (channel closed) is.
                        dc->send(std::move(msg));
                    }
                } catch (const std::exception& e) {
                    LogWarn("NetherNet " + remote + ": send failed: " + e.what());
                    return false;
                }
                return true;
            }

            void Close() override {
                if (closed.exchange(true))
                    return;
                try {
                    if (reliable)
                        reliable->close();
                    if (pc)
                        pc->close();
                } catch (const std::exception& e) {
                    LogWarn(std::string("nethernet: close failed: ") + e.what());
                }
            }

            bool Alive() const override { return !closed && reliable && reliable->isOpen(); }
            std::string Describe() const override { return (lan ? "nethernet-lan " : "nethernet ") + remote; }
            bool SecureTransport() const override { return true; }
            std::string RemoteIp() const override {
                const std::string text = remote;
                const size_t colon = text.rfind(':');
                std::string host = colon == std::string::npos ? text : text.substr(0, colon);
                if (host.size() > 2 && host.front() == '[' && host.back() == ']')
                    host = host.substr(1, host.size() - 2);
                return host;
            }

            void AttachChannel(std::shared_ptr<rtc::DataChannel> dc) {
                const std::string label = dc->label();
                std::weak_ptr<Peer> weak = shared_from_this();
                if (label == "ReliableDataChannel") {
                    reliable = dc;
                    dc->onOpen([weak] {
                        if (auto self = weak.lock(); self && !self->opened.exchange(true)) {
                            NetEvent e;
                            e.type = NetEvent::Type::Open;
                            e.connection = self;
                            self->queue.Push(std::move(e));
                        }
                    });
                    dc->onClosed([weak] {
                        if (auto self = weak.lock())
                            self->Disconnected("data channel closed");
                    });
                    dc->onMessage([weak](rtc::message_variant msg) {
                        auto self = weak.lock();
                        if (!self)
                            return;
                        if (auto* b = std::get_if<rtc::binary>(&msg)) {
                            std::vector<uint8_t> whole;
                            if (self->assembler.Push(reinterpret_cast<const uint8_t*>(b->data()), b->size(),
                                                     whole)) {
                                NetEvent e;
                                e.type = NetEvent::Type::Message;
                                e.connection = self;
                                e.data = std::move(whole);
                                self->queue.Push(std::move(e));
                            }
                        }
                    });
                    if (dc->isOpen() && !opened.exchange(true)) {
                        NetEvent e;
                        e.type = NetEvent::Type::Open;
                        e.connection = shared_from_this();
                        queue.Push(std::move(e));
                    }
                } else if (label == "UnreliableDataChannel") {
                    unreliable = dc;
                    dc->onMessage([weak](rtc::message_variant msg) {
                        auto self = weak.lock();
                        if (!self)
                            return;
                        if (auto* b = std::get_if<rtc::binary>(&msg); b && b->size() > 1) {
                            NetEvent e;
                            e.type = NetEvent::Type::Message;
                            e.connection = self;
                            e.data.assign(reinterpret_cast<const uint8_t*>(b->data()) + 1,
                                          reinterpret_cast<const uint8_t*>(b->data()) + b->size());
                            self->queue.Push(std::move(e));
                        }
                    });
                }
            }

            void Disconnected(const char* why) {
                if (!opened) {
                    closed = true;
                    return;
                }
                if (!closed.exchange(true)) {
                    LogInfo(Describe() + ": connection lost (" + why + ")");
                    NetEvent e;
                    e.type = NetEvent::Type::Close;
                    e.connection = shared_from_this();
                    queue.Push(std::move(e));
                }
            }

            std::string remote;
            EventQueue& queue;
            bool lan = false;
            std::shared_ptr<rtc::PeerConnection> pc;
            std::shared_ptr<rtc::DataChannel> reliable, unreliable;
            FragmentAssembler assembler;
            std::atomic<bool> closed{false};
            std::atomic<bool> opened{false};
            std::chrono::steady_clock::time_point created = std::chrono::steady_clock::now();
            std::mutex answerMutex;
            std::condition_variable answerReady;
            bool gathered = false;
        };

        class NetherNetListener final : public Listener {
        public:
            NetherNetListener(ServerConfig config, std::function<std::string()> infoJson)
                : config(std::move(config)), infoJson(std::move(infoJson)) {}
            ~NetherNetListener() override { Stop(); }

            const char* Name() const override { return "nethernet"; }

            bool Start(EventQueue& q, std::string& error) override {
                queue = &q;
                rtc::InitLogger(std::getenv("PS_RTC_LOG") ? rtc::LogLevel::Debug : rtc::LogLevel::Error);

                if (!config.dataDir.empty()) {
                    std::string note;
                    if (identity.LoadOrCreate(config.dataDir + "/nethernet-identity.pem", note) && !note.empty())
                        LogInfo("NetherNet identity: " + note + " (sha-256 " + identity.Fingerprint() + ")");
                }
                if (!identity.Valid()) {
                    error = "nethernet: could not create the server identity key";
                    return false;
                }
                if (!signaling.Start(config.bindAddress, config.port,
                                     [this](const nethernet::HttpRequest& r) { return Handle(r); }, error))
                    return false;
                running = true;
                janitor = std::thread([this] {
                    SetLogThreadName("NetherNet");
                    while (running) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        Sweep();
                    }
                });
                if (config.lanDiscovery)
                    StartLan();
                return true;
            }

            void Stop() override {
                if (!running.exchange(false))
                    return;
                signaling.Stop();
                if (janitor.joinable())
                    janitor.join();
                {
                    std::unique_lock lock(workersMutex);
                    workersDone.wait_for(lock, std::chrono::seconds(6), [this] { return lanBusy == 0; });
                }
                lanDiscovery.Stop();
                std::vector<std::shared_ptr<Peer>> all;
                {
                    std::lock_guard lock(mutex);
                    for (auto& [id, p] : peers)
                        all.push_back(p);
                    peers.clear();
                }
                for (auto& p : all)
                    p->Close();
                all.clear();
            }

        private:
            std::shared_ptr<rtc::PeerConnection> MakePeerConnection(const std::shared_ptr<Peer>& peer) {
                rtc::Configuration rc;
                for (const std::string& s : config.iceServers)
                    if (s != "none")
                        rc.iceServers.emplace_back(s);
                if (!config.iceBindAddress.empty())
                    rc.bindAddress = config.iceBindAddress;
                if (config.udpPortsSet) {
                    if (config.udpPortBegin != 0) {
                        rc.portRangeBegin = config.udpPortBegin;
                        rc.portRangeEnd = config.udpPortEnd;
                        rc.enableIceUdpMux = config.udpPortBegin == config.udpPortEnd;
                        if (config.iceUdpPort && config.udpPortBegin == config.udpPortEnd) {
                            rc.portRangeBegin = config.iceUdpPort;
                            rc.portRangeEnd = config.iceUdpPort;
                        }
                    }
                } else if (config.iceUdpMux) {
                    rc.enableIceUdpMux = true;
                    rc.portRangeBegin = config.iceUdpPort ? config.iceUdpPort : config.port;
                    rc.portRangeEnd = rc.portRangeBegin;
                } else {
                    rc.portRangeBegin = config.port;
                    rc.portRangeEnd = static_cast<uint16_t>(std::min<int>(65535, config.port + 255));
                }
                auto pc = std::make_shared<rtc::PeerConnection>(rc);
                std::weak_ptr<Peer> weak = peer;
                pc->onGatheringStateChange([weak](rtc::PeerConnection::GatheringState state) {
                    if (state != rtc::PeerConnection::GatheringState::Complete)
                        return;
                    if (auto p = weak.lock()) {
                        std::lock_guard lock(p->answerMutex);
                        p->gathered = true;
                        p->answerReady.notify_all();
                    }
                });
                pc->onStateChange([weak](rtc::PeerConnection::State state) {
                    // Disconnected is transient (missed ICE consent checks) and recovers on its own;
                    // libdatachannel moves on to Failed when it does not.
                    if (state == rtc::PeerConnection::State::Failed)
                        if (auto p = weak.lock())
                            p->Disconnected("ice failed");
                    if (state == rtc::PeerConnection::State::Closed)
                        if (auto p = weak.lock())
                            p->Disconnected("peer connection closed");
                });
                pc->onDataChannel([weak](std::shared_ptr<rtc::DataChannel> dc) {
                    if (auto p = weak.lock())
                        p->AttachChannel(std::move(dc));
                });
                return pc;
            }

            bool RoomForPeer() {
                std::lock_guard lock(mutex);
                return peers.size() < static_cast<size_t>(config.maxPlayers) * 4 + 16;
            }

            std::string WaitForAnswer(const std::shared_ptr<Peer>& peer) {
                {
                    std::unique_lock lock(peer->answerMutex);
                    peer->answerReady.wait_for(lock, kAnswerTimeout, [&] { return peer->gathered; });
                }
                try {
                    if (const auto desc = peer->pc->localDescription())
                        return std::string(*desc);
                } catch (const std::exception& e) {
                    LogWarn(std::string("nethernet: no local description: ") + e.what());
                }
                return {};
            }

            nethernet::HttpResponse HandleOffer(const nethernet::HttpRequest& req, const std::string& netId) {
                nethernet::HttpResponse res;
                if (req.body.size() < 16 || !req.body.starts_with("v=")) {
                    res.status = 400;
                    res.contentType = "text/plain";
                    res.body = "Missing SDP offer in request body";
                    return res;
                }
                if (!RoomForPeer()) {
                    res.status = 503;
                    res.contentType = "text/plain";
                    res.body = "Service unavailable";
                    return res;
                }
                const std::string remote = (req.remoteAddress.find(':') != std::string::npos
                                                ? "[" + req.remoteAddress + "]"
                                                : req.remoteAddress) +
                                           ":" + std::to_string(req.remotePort);
                auto peer = std::make_shared<Peer>(remote, *queue);
                peer->offerSdp = req.body;
                try {
                    peer->pc = MakePeerConnection(peer);
                    peer->pc->setRemoteDescription(
                        rtc::Description(StripIdentityLines(req.body), rtc::Description::Type::Offer));
                } catch (const std::exception& e) {
                    LogWarn("NetherNet: bad offer from " + remote + ": " + e.what());
                    res.status = 400;
                    res.contentType = "text/plain";
                    res.body = "Negotiation failed";
                    return res;
                }
                std::string answer = WaitForAnswer(peer);
                if (answer.empty()) {
                    LogWarn("NetherNet " + remote + ": no answer could be made (ice gathering failed)");
                    peer->Close();
                    res.status = 503;
                    res.contentType = "text/plain";
                    res.body = "Service unavailable";
                    return res;
                }
                {
                    std::lock_guard lock(mutex);
                    peers["http " + netId + " " + remote] = peer;
                }
                answer = nethernet::AddAnswerIdentity(answer, identity);
                // The client reaches us at the address it used for signalling; libjuice leaves
                // loopback out of its host candidates, so add that address when we know the port.
                answer = nethernet::AddHostCandidate(answer, req.localAddress, IcePort());
                res.contentType = "application/sdp";
                res.body = std::move(answer);
                return res;
            }

            uint16_t IcePort() const {
                if (config.udpPortsSet)
                    return config.udpPortBegin == config.udpPortEnd ? config.udpPortBegin : 0;
                if (config.iceUdpMux)
                    return config.iceUdpPort ? config.iceUdpPort : config.port;
                return 0;
            }

            nethernet::HttpResponse Handle(const nethernet::HttpRequest& req) {
                nethernet::HttpResponse res;
                if ((req.method == "GET" || req.method == "HEAD") && req.path == "/v1/join") {
                    // The capability probe also feeds the server list (name, players, version), so
                    // the body is always the status, query string or not.
                    res.contentType = "application/json";
                    res.body = infoJson();
                    return res;
                }
                if (req.method != "POST" || !req.path.starts_with("/v1/join/")) {
                    res.status = 404;
                    res.contentType = "text/plain";
                    res.body = "Expected /v1/join/{networkId}";
                    return res;
                }
                // Mojang documents the network id as opaque; today it is a decimal uint64.
                const std::string id = req.path.substr(9);
                if (id.empty() || id.size() > 128 || id.find_first_of("/ \t") != std::string::npos) {
                    res.status = 400;
                    res.contentType = "text/plain";
                    res.body = "Expected /v1/join/{networkId}";
                    return res;
                }
                return HandleOffer(req, id);
            }

            // ---- LAN ----

            void StartLan() {
                std::random_device rd;
                const uint64_t networkId = (static_cast<uint64_t>(rd()) << 32 | rd()) | 1;
                {
                    std::mt19937_64 rng(networkId ^ static_cast<uint64_t>(
                                                         std::chrono::steady_clock::now().time_since_epoch().count()));
                    static const char* hex = "0123456789abcdef";
                    for (int i = 0; i < 16; i++)
                        lanNonce += hex[rng() & 15];
                }
                nethernet::LanDiscovery::Callbacks cb;
                cb.serverData = [this] { return LanServerData(); };
                cb.signal = [this](uint64_t sender, const std::string& address, const std::string& text) {
                    OnLanSignal(sender, address, text);
                };
                std::string error;
                if (lanDiscovery.Start(nethernet::lan::kPort, networkId, std::move(cb), error))
                    LogInfo("LAN discovery on UDP port " + std::to_string(nethernet::lan::kPort) +
                            " (shows under Friends > LAN Games)");
                else
                    LogWarn(error + "; the server will not show up under LAN Games");
            }

            std::string LanServerData() {
                json::Value v;
                if (!json::Parse(infoJson(), v))
                    return {};
                nethernet::lan::ServerData d;
                d.serverName = v["name"].String();
                d.protocol = static_cast<int32_t>(v["protocol"].Int());
                d.version = v["version"].String();
                d.levelName = v["level"].String();
                d.playerCount = static_cast<int32_t>(v["players"].Int());
                d.maxPlayerCount = static_cast<int32_t>(v["maxPlayers"].Int());
                d.gameType = static_cast<int32_t>(v["gameType"].Int());
                d.acceptsOnlineAuth = true;
                d.acceptsSelfSignedAuth = !config.xboxAuth;
                d.nonce = lanNonce;
                d.connectionType = nethernet::lan::kConnectionTypeLan;
                return nethernet::lan::EncodeServerData(d);
            }

            static std::string LanKey(uint64_t sender, const std::string& connection) {
                return "lan " + std::to_string(sender) + " " + connection;
            }

            void SignalLan(uint64_t sender, const std::string& type, const std::string& connection,
                           const std::string& data) {
                lanDiscovery.Signal(sender, type + " " + connection + " " + data);
            }

            void OnLanSignal(uint64_t sender, const std::string& address, const std::string& text) {
                const size_t a = text.find(' ');
                const size_t b = a == std::string::npos ? std::string::npos : text.find(' ', a + 1);
                if (b == std::string::npos)
                    return;
                const std::string type = text.substr(0, a);
                std::string connection = text.substr(a + 1, b - a - 1);
                connection = connection.substr(0, connection.find_first_not_of("0123456789"));
                const std::string data = text.substr(b + 1);
                if (connection.empty())
                    return;
                const std::string key = LanKey(sender, connection);
                std::shared_ptr<Peer> existing;
                {
                    std::lock_guard lock(mutex);
                    if (const auto it = peers.find(key); it != peers.end())
                        existing = it->second;
                }
                if (type == "CONNECTREQUEST") {
                    if (!existing)
                        AcceptLanOffer(sender, address, connection, key, data);
                } else if (type == "CANDIDATEADD") {
                    if (!existing || !existing->pc)
                        return;
                    try {
                        existing->pc->addRemoteCandidate(rtc::Candidate(data, "0"));
                    } catch (const std::exception& e) {
                        LogWarn("LAN " + address + ": bad candidate: " + e.what());
                    }
                } else if (type == "CONNECTERROR") {
                    if (existing) {
                        LogDebug("LAN join from " + address + ": client gave up (error " + data + ")");
                        existing->Close();
                    }
                }
            }

            void AcceptLanOffer(uint64_t sender, const std::string& address, const std::string& connection,
                                const std::string& key, const std::string& offer) {
                if (!RoomForPeer()) {
                    SignalLan(sender, "CONNECTERROR", connection, "16");
                    return;
                }
                auto peer = std::make_shared<Peer>(address, *queue);
                peer->lan = true;
                // LAN offers carry no a=identity; the login still goes through the normal checks.
                try {
                    peer->pc = MakePeerConnection(peer);
                    peer->pc->setRemoteDescription(
                        rtc::Description(StripIdentityLines(offer), rtc::Description::Type::Offer));
                } catch (const std::exception& e) {
                    LogWarn("LAN: bad offer from " + address + ": " + e.what());
                    SignalLan(sender, "CONNECTERROR", connection, "13");
                    return;
                }
                {
                    std::lock_guard lock(mutex);
                    peers[key] = peer;
                }
                LogDebug("LAN join request from " + address);
                // Gathering takes a moment; keep the discovery socket free for the candidates.
                {
                    std::lock_guard lock(workersMutex);
                    lanBusy++;
                }
                std::thread([this, peer, sender, connection] {
                    SetLogThreadName("LAN");
                    AnswerLan(peer, sender, connection);
                    std::lock_guard lock(workersMutex);
                    lanBusy--;
                    workersDone.notify_all();
                }).detach();
            }

            void AnswerLan(const std::shared_ptr<Peer>& peer, uint64_t sender, const std::string& connection) {
                {
                    std::string answer = WaitForAnswer(peer);
                    if (answer.empty() || !running) {
                        SignalLan(sender, "CONNECTERROR", connection, "11");
                        peer->Close();
                        return;
                    }
                    // LAN answers carry no identity line: LAN offers have none, and the game answers without one.
                    SignalLan(sender, "CONNECTRESPONSE", connection, answer);
                    // Vanilla trickles candidates after the answer; send ours that way too.
                    const auto ufrag = SdpValues(answer, "a=ice-ufrag:");
                    int index = 0;
                    for (const std::string& c : SdpValues(answer, "a=candidate:")) {
                        std::string line = "candidate:" + c + " generation 0";
                        if (!ufrag.empty())
                            line += " ufrag " + ufrag.front();
                        line += " network-id " + std::to_string(index++) + " network-cost 0";
                        SignalLan(sender, "CANDIDATEADD", connection, line);
                    }
                }
            }

            void Sweep() {
                std::vector<std::shared_ptr<Peer>> doomed;
                {
                    std::lock_guard lock(mutex);
                    const auto now = std::chrono::steady_clock::now();
                    for (auto it = peers.begin(); it != peers.end();) {
                        const bool stale =
                            !it->second->opened && now - it->second->created > std::chrono::seconds(30);
                        if (it->second->closed || stale) {
                            doomed.push_back(it->second);
                            it = peers.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }
                for (auto& p : doomed)
                    p->Close();
            }

            ServerConfig config;
            std::function<std::string()> infoJson;
            EventQueue* queue = nullptr;
            nethernet::SignalingServer signaling;
            nethernet::ServerIdentity identity;
            nethernet::LanDiscovery lanDiscovery;
            std::string lanNonce;
            std::mutex workersMutex;
            std::condition_variable workersDone;
            int lanBusy = 0;
            std::thread janitor;
            std::atomic<bool> running{false};
            std::mutex mutex;
            std::unordered_map<std::string, std::shared_ptr<Peer>> peers;
        };
    }

    std::unique_ptr<Listener> CreateNetherNetListener(const ServerConfig& config,
                                                      std::function<std::string()> infoJson) {
        return std::make_unique<NetherNetListener>(config, std::move(infoJson));
    }
}
