#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace minihttp {
    struct Response {
        int status = 0;
        std::string contentType;
        std::string body;
    };

    inline std::optional<std::string> Exchange(uint16_t port, const std::string& request,
                                               int timeoutSecs = 15) {
#ifdef _WIN32
        static const bool started = [] {
            WSADATA d;
            return WSAStartup(MAKEWORD(2, 2), &d) == 0;
        }();
        (void)started;
        SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET)
            return std::nullopt;
        DWORD tv = static_cast<DWORD>(timeoutSecs * 1000);
#else
        int s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s < 0)
            return std::nullopt;
        timeval tv{timeoutSecs, 0};
#endif
        ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        std::optional<std::string> out;
        if (::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0 &&
            ::send(s, request.data(), static_cast<int>(request.size()), 0) ==
                static_cast<int>(request.size())) {
            std::string data;
            char buf[4096];
            for (;;) {
                const int n = static_cast<int>(::recv(s, buf, sizeof(buf), 0));
                if (n <= 0)
                    break;
                data.append(buf, static_cast<size_t>(n));
            }
            out = data;
        }
#ifdef _WIN32
        closesocket(s);
#else
        ::close(s);
#endif
        return out;
    }

    inline std::optional<Response> Request(uint16_t port, const std::string& method, const std::string& path,
                                           const std::string& body = {}, const std::string& type = {}) {
        std::string req =
            method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\n";
        if (!type.empty())
            req += "Content-Type: " + type + "\r\n";
        if (!body.empty() || method == "POST")
            req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        req += "Connection: close\r\n\r\n" + body;
        const auto raw = Exchange(port, req);
        if (!raw || !raw->starts_with("HTTP/1.1 "))
            return std::nullopt;
        Response r;
        r.status = std::atoi(raw->c_str() + 9);
        const size_t end = raw->find("\r\n\r\n");
        const std::string head = raw->substr(0, end);
        if (const size_t ct = head.find("Content-Type: "); ct != std::string::npos)
            r.contentType = head.substr(ct + 14, head.find("\r\n", ct) - ct - 14);
        if (end != std::string::npos)
            r.body = raw->substr(end + 4);
        return r;
    }
}
