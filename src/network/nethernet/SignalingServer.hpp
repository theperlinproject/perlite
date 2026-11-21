#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace perlite::nethernet {

    struct HttpRequest {
        std::string method;
        std::string path;
        std::string query;
        std::map<std::string, std::string> headers;
        std::string body;
        std::string remoteAddress;
        uint16_t remotePort = 0;
        std::string localAddress;

        std::string Header(const std::string& lowerName) const;
        bool HasQuery(const std::string& key) const;
    };

    struct HttpResponse {
        int status = 200;
        std::string contentType = "application/json";
        std::string body;
    };

    class SignalingServer {
    public:
        using Handler = std::function<HttpResponse(const HttpRequest&)>;

        SignalingServer() = default;
        ~SignalingServer();
        SignalingServer(const SignalingServer&) = delete;
        SignalingServer& operator=(const SignalingServer&) = delete;

        bool Start(const std::string& bindAddress, uint16_t port, Handler handler, std::string& error);
        void Stop();
        uint16_t Port() const { return boundPort; }

        static constexpr size_t kMaxHeaderBytes = 16 * 1024;
        static constexpr size_t kMaxBodyBytes = 64 * 1024;
        static constexpr int kMaxClients = 64;

    private:
        void AcceptLoop();
        void Serve(intptr_t client);

        Handler handler;
        intptr_t listenSocket = -1;
        uint16_t boundPort = 0;
        std::thread acceptThread;
        std::atomic<bool> running{false};
        std::mutex clientsMutex;
        std::condition_variable clientsDone;
        int clients = 0;
    };

    std::string BuildHttpResponse(const HttpResponse& response, bool includeBody);
    bool ParseHttpRequest(const std::string& head, HttpRequest& out);
    bool LooksLikeTlsHello(const unsigned char* data, size_t size);
    const std::string& TlsHandshakeFailureAlert();
}
