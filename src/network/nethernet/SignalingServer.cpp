#include "network/nethernet/SignalingServer.hpp"

#include "common/Log.hpp"
#include "common/Strings.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
#define PERLITE_CLOSE_SOCKET closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
#define PERLITE_CLOSE_SOCKET ::close
#endif

namespace perlite::nethernet {
    namespace {
        constexpr int kRequestTimeoutMs = 3000;
        constexpr int kDrainTimeoutMs = 200;

        SocketHandle Handle(intptr_t s) {
            return static_cast<SocketHandle>(s);
        }

        bool Valid(intptr_t s) {
#ifdef _WIN32
            return Handle(s) != INVALID_SOCKET;
#else
            return s >= 0;
#endif
        }

        void EnsureSockets() {
#ifdef _WIN32
            static const bool started = [] {
                WSADATA data;
                return WSAStartup(MAKEWORD(2, 2), &data) == 0;
            }();
            (void)started;
#endif
        }

        bool WaitReadable(intptr_t s, int timeoutMs) {
#ifdef _WIN32
            WSAPOLLFD p{};
            p.fd = Handle(s);
            p.events = POLLRDNORM;
            return WSAPoll(&p, 1, timeoutMs) > 0;
#else
            pollfd p{};
            p.fd = Handle(s);
            p.events = POLLIN;
            return ::poll(&p, 1, timeoutMs) > 0;
#endif
        }

        int ReadSome(intptr_t s, char* buf, size_t cap) {
            return static_cast<int>(::recv(Handle(s), buf, static_cast<int>(cap), 0));
        }

        bool WriteAll(intptr_t s, const std::string& data) {
            size_t sent = 0;
            while (sent < data.size()) {
#ifdef MSG_NOSIGNAL
                const int flags = MSG_NOSIGNAL;
#else
                const int flags = 0;
#endif
                const int n = static_cast<int>(
                    ::send(Handle(s), data.data() + sent, static_cast<int>(data.size() - sent), flags));
                if (n <= 0)
                    return false;
                sent += static_cast<size_t>(n);
            }
            return true;
        }

        std::string AddressText(const sockaddr_storage& addr, uint16_t* port) {
            char text[INET6_ADDRSTRLEN] = {};
            if (addr.ss_family == AF_INET) {
                const auto* a = reinterpret_cast<const sockaddr_in*>(&addr);
                inet_ntop(AF_INET, &a->sin_addr, text, sizeof(text));
                if (port)
                    *port = ntohs(a->sin_port);
            } else if (addr.ss_family == AF_INET6) {
                const auto* a = reinterpret_cast<const sockaddr_in6*>(&addr);
                inet_ntop(AF_INET6, &a->sin6_addr, text, sizeof(text));
                if (port)
                    *port = ntohs(a->sin6_port);
                std::string s = text;
                if (s.starts_with("::ffff:") && s.find('.') != std::string::npos)
                    return s.substr(7);
                return s;
            }
            return text;
        }

        const char* Reason(int status) {
            switch (status) {
            case 200:
                return "OK";
            case 400:
                return "Bad Request";
            case 404:
                return "Not Found";
            case 405:
                return "Method Not Allowed";
            case 413:
                return "Request Entity Too Large";
            case 500:
                return "Internal Server Error";
            case 503:
                return "Service Unavailable";
            default:
                return "Unknown";
            }
        }
    }

    std::string HttpRequest::Header(const std::string& lowerName) const {
        const auto it = headers.find(lowerName);
        return it == headers.end() ? std::string() : it->second;
    }

    bool HttpRequest::HasQuery(const std::string& key) const {
        size_t pos = 0;
        while (pos <= query.size()) {
            const size_t amp = query.find('&', pos);
            const std::string part =
                query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
            const std::string name = part.substr(0, part.find('='));
            if (name == key)
                return true;
            if (amp == std::string::npos)
                break;
            pos = amp + 1;
        }
        return false;
    }

    const std::string& TlsHandshakeFailureAlert() {
        static const std::string alert("\x15\x03\x03\x00\x02\x02\x28", 7);
        return alert;
    }

    bool LooksLikeTlsHello(const unsigned char* data, size_t size) {
        return size >= 1 && data[0] == 0x16;
    }

    std::string BuildHttpResponse(const HttpResponse& response, bool includeBody) {
        std::string out =
            "HTTP/1.1 " + std::to_string(response.status) + " " + Reason(response.status) + "\r\n";
        out += "Content-Type: " + response.contentType + "\r\n";
        out += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
        out += "Connection: close\r\n\r\n";
        if (includeBody)
            out += response.body;
        return out;
    }

    bool ParseHttpRequest(const std::string& head, HttpRequest& out) {
        const size_t lineEnd = head.find("\r\n");
        const std::string requestLine = head.substr(0, lineEnd);
        const size_t a = requestLine.find(' ');
        const size_t b = a == std::string::npos ? std::string::npos : requestLine.find(' ', a + 1);
        if (a == std::string::npos || b == std::string::npos)
            return false;
        out.method = requestLine.substr(0, a);
        std::string target = requestLine.substr(a + 1, b - a - 1);
        if (const size_t q = target.find('?'); q != std::string::npos) {
            out.query = target.substr(q + 1);
            target.resize(q);
        }
        if (target.size() > 1 && target.back() == '/')
            target.pop_back();
        out.path = target;
        size_t pos = lineEnd == std::string::npos ? head.size() : lineEnd + 2;
        while (pos < head.size()) {
            size_t end = head.find("\r\n", pos);
            if (end == std::string::npos)
                end = head.size();
            const std::string line = head.substr(pos, end - pos);
            pos = end + 2;
            if (line.empty())
                break;
            const size_t colon = line.find(':');
            if (colon == std::string::npos)
                continue;
            out.headers[str::Lower(str::Trim(line.substr(0, colon)))] = str::Trim(line.substr(colon + 1));
        }
        return true;
    }

    SignalingServer::~SignalingServer() {
        Stop();
    }

    bool SignalingServer::Start(const std::string& bindAddress, uint16_t port, Handler h,
                                std::string& error) {
        EnsureSockets();
        handler = std::move(h);
        const bool any = bindAddress.empty() || bindAddress == "0.0.0.0" || bindAddress == "::";
        std::string chosen;
        for (int family : {AF_INET6, AF_INET}) {
            if (!any && ((family == AF_INET6) != (bindAddress.find(':') != std::string::npos)))
                continue;
            const SocketHandle s = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
            if (!Valid(static_cast<intptr_t>(s)))
                continue;
            int yes = 1;
            ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
            sockaddr_storage addr{};
            socklen_t len = 0;
            if (family == AF_INET6) {
                int no = 0;
                ::setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&no), sizeof(no));
                auto* a = reinterpret_cast<sockaddr_in6*>(&addr);
                a->sin6_family = AF_INET6;
                a->sin6_port = htons(port);
                a->sin6_addr = in6addr_any;
                if (!any)
                    inet_pton(AF_INET6, bindAddress.c_str(), &a->sin6_addr);
                len = sizeof(sockaddr_in6);
            } else {
                auto* a = reinterpret_cast<sockaddr_in*>(&addr);
                a->sin_family = AF_INET;
                a->sin_port = htons(port);
                a->sin_addr.s_addr = htonl(INADDR_ANY);
                if (!any)
                    inet_pton(AF_INET, bindAddress.c_str(), &a->sin_addr);
                len = sizeof(sockaddr_in);
            }
            if (::bind(s, reinterpret_cast<sockaddr*>(&addr), len) != 0 || ::listen(s, 64) != 0) {
                PERLITE_CLOSE_SOCKET(s);
                continue;
            }
            sockaddr_storage bound{};
            socklen_t boundLen = sizeof(bound);
            ::getsockname(s, reinterpret_cast<sockaddr*>(&bound), &boundLen);
            AddressText(bound, &boundPort);
            listenSocket = static_cast<intptr_t>(s);
            break;
        }
        if (!Valid(listenSocket)) {
            error = "cannot listen on tcp " + (bindAddress.empty() ? std::string("0.0.0.0") : bindAddress) +
                    ":" + std::to_string(port) + " (port in use?)";
            return false;
        }
        running = true;
        acceptThread = std::thread([this] { AcceptLoop(); });
        return true;
    }

    void SignalingServer::Stop() {
        if (!running.exchange(false))
            return;
        if (Valid(listenSocket)) {
#ifdef _WIN32
            ::shutdown(Handle(listenSocket), SD_BOTH);
#else
            ::shutdown(Handle(listenSocket), SHUT_RDWR);
#endif
            PERLITE_CLOSE_SOCKET(Handle(listenSocket));
        }
        if (acceptThread.joinable())
            acceptThread.join();
        listenSocket = -1;
        std::unique_lock lock(clientsMutex);
        clientsDone.wait_for(lock, std::chrono::seconds(10), [this] { return clients == 0; });
    }

    void SignalingServer::AcceptLoop() {
        while (running) {
            if (!WaitReadable(listenSocket, 250))
                continue;
            sockaddr_storage from{};
            socklen_t len = sizeof(from);
            const SocketHandle c = ::accept(Handle(listenSocket), reinterpret_cast<sockaddr*>(&from), &len);
            if (!Valid(static_cast<intptr_t>(c)))
                continue;
            {
                std::lock_guard lock(clientsMutex);
                if (clients >= kMaxClients) {
                    HttpResponse busy;
                    busy.status = 503;
                    busy.contentType = "text/plain";
                    busy.body = "busy";
                    WriteAll(static_cast<intptr_t>(c), BuildHttpResponse(busy, true));
                    PERLITE_CLOSE_SOCKET(c);
                    continue;
                }
                clients++;
            }
            std::thread([this, c] {
                Serve(static_cast<intptr_t>(c));
                std::lock_guard lock(clientsMutex);
                clients--;
                clientsDone.notify_all();
            }).detach();
        }
    }

    void SignalingServer::Serve(intptr_t client) {
        int one = 1;
        ::setsockopt(Handle(client), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one),
                     sizeof(one));
        HttpRequest request;
        {
            sockaddr_storage peer{}, local{};
            socklen_t pl = sizeof(peer), ll = sizeof(local);
            ::getpeername(Handle(client), reinterpret_cast<sockaddr*>(&peer), &pl);
            ::getsockname(Handle(client), reinterpret_cast<sockaddr*>(&local), &ll);
            request.remoteAddress = AddressText(peer, &request.remotePort);
            request.localAddress = AddressText(local, nullptr);
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kRequestTimeoutMs);
        const auto remaining = [&] {
            return static_cast<int>(
                std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(
                                         deadline - std::chrono::steady_clock::now())
                                         .count()));
        };
        std::string data;
        char buf[4096];
        bool tls = false;
        while (data.find("\r\n\r\n") == std::string::npos) {
            if (!WaitReadable(client, remaining()))
                break;
            const int n = ReadSome(client, buf, sizeof(buf));
            if (n <= 0)
                break;
            data.append(buf, static_cast<size_t>(n));
            if (LooksLikeTlsHello(reinterpret_cast<const unsigned char*>(data.data()), data.size())) {
                tls = true;
                break;
            }
            if (data.size() > kMaxHeaderBytes)
                break;
        }
        if (tls) {
            while (WaitReadable(client, kDrainTimeoutMs) && ReadSome(client, buf, sizeof(buf)) > 0) {}
            WriteAll(client, TlsHandshakeFailureAlert());
#ifdef _WIN32
            ::shutdown(Handle(client), SD_SEND);
#else
            ::shutdown(Handle(client), SHUT_WR);
#endif
            while (WaitReadable(client, kDrainTimeoutMs) && ReadSome(client, buf, sizeof(buf)) > 0) {}
            PERLITE_CLOSE_SOCKET(Handle(client));
            return;
        }
        const size_t headEnd = data.find("\r\n\r\n");
        HttpResponse response;
        bool includeBody = true;
        if (headEnd == std::string::npos || !ParseHttpRequest(data.substr(0, headEnd + 2), request)) {
            response.status = 400;
            response.contentType = "text/plain";
            response.body = "Malformed request";
        } else {
            request.body = data.substr(headEnd + 4);
            size_t length = 0;
            if (const std::string cl = request.Header("content-length"); !cl.empty())
                length = static_cast<size_t>(std::strtoull(cl.c_str(), nullptr, 10));
            if (length > kMaxBodyBytes) {
                response.status = 413;
                response.contentType = "text/plain";
                response.body = "Request body is too large";
            } else {
                while (request.body.size() < length && WaitReadable(client, remaining())) {
                    const int n = ReadSome(client, buf, sizeof(buf));
                    if (n <= 0)
                        break;
                    request.body.append(buf, static_cast<size_t>(n));
                }
                if (request.body.size() > length)
                    request.body.resize(length);
                includeBody = request.method != "HEAD";
                try {
                    response = handler(request);
                } catch (const std::exception& e) {
                    LogWarn(std::string("nethernet signalling: ") + e.what());
                    response.status = 500;
                    response.contentType = "text/plain";
                    response.body = "Internal error";
                }
            }
        }
        WriteAll(client, BuildHttpResponse(response, includeBody));
#ifdef _WIN32
        ::shutdown(Handle(client), SD_SEND);
#else
        ::shutdown(Handle(client), SHUT_WR);
#endif
        while (WaitReadable(client, kDrainTimeoutMs) && ReadSome(client, buf, sizeof(buf)) > 0) {}
        PERLITE_CLOSE_SOCKET(Handle(client));
    }
}
