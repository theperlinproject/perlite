#pragma once

#include <memory>
#include <mutex>
#include <string>

namespace perlite::nethernet {

    class ServerIdentity {
    public:
        ServerIdentity();
        ~ServerIdentity();
        ServerIdentity(const ServerIdentity&) = delete;
        ServerIdentity& operator=(const ServerIdentity&) = delete;

        bool Valid() const;
        std::string Token() const;
        std::string SignDetached(const std::string& payload) const;
        std::string AnswerIdentity(const std::string& algorithm, const std::string& digest) const;

        static std::string FingerprintPayload(const std::string& algorithm, const std::string& digest);
        static std::string Base64(const std::string& data, bool url);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    std::string AddAnswerIdentity(const std::string& sdp, const ServerIdentity& identity);
    std::string AddHostCandidate(const std::string& sdp, const std::string& address, uint16_t port);
}
