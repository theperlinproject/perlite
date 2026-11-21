#pragma once

#include <memory>
#include <mutex>
#include <string>

typedef struct evp_pkey_st EVP_PKEY;

namespace perlite::nethernet {

    class ServerIdentity {
    public:
        ServerIdentity();
        ~ServerIdentity();
        ServerIdentity(const ServerIdentity&) = delete;
        ServerIdentity& operator=(const ServerIdentity&) = delete;

        bool Valid() const;
        // Keeps the key in a PEM file so the trust-on-first-use pin survives restarts.
        bool LoadOrCreate(const std::string& path, std::string& note);
        // SHA-256 of the public key, for publishing out of band.
        std::string Fingerprint() const;
        std::string Token() const;
        std::string SignDetached(const std::string& payload) const;
        std::string AnswerIdentity(const std::string& algorithm, const std::string& digest) const;

        static std::string FingerprintPayload(const std::string& algorithm, const std::string& digest);
        static std::string Base64(const std::string& data, bool url);

    private:
        void Adopt(EVP_PKEY* key);
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    std::string AddAnswerIdentity(const std::string& sdp, const ServerIdentity& identity);
    std::string AddHostCandidate(const std::string& sdp, const std::string& address, uint16_t port);
}
