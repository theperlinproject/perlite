#include "network/nethernet/ServerIdentity.hpp"

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <cstdio>
#include <filesystem>

#include <ctime>
#include <sstream>
#include <vector>

namespace perlite::nethernet {
    namespace {
        constexpr int kTokenLifetimeSeconds = 60;
        constexpr int kCoordinateBytes = 48;

        std::string EscapeJson(const std::string& s) {
            std::string out;
            for (char c : s) {
                if (c == '"' || c == '\\') {
                    out += '\\';
                    out += c;
                } else {
                    out += c;
                }
            }
            return out;
        }

        std::string Coordinate(EVP_PKEY* key, const char* name) {
            BIGNUM* bn = nullptr;
            if (EVP_PKEY_get_bn_param(key, name, &bn) != 1)
                return {};
            std::string raw(kCoordinateBytes, '\0');
            BN_bn2binpad(bn, reinterpret_cast<unsigned char*>(raw.data()), kCoordinateBytes);
            BN_free(bn);
            return raw;
        }
    }

    namespace {
        std::string RawSign(EVP_PKEY* key, const std::string& input) {
            EVP_MD_CTX* ctx = EVP_MD_CTX_new();
            std::string raw;
            size_t sigLen = 0;
            if (ctx && EVP_DigestSignInit(ctx, nullptr, EVP_sha384(), nullptr, key) == 1 &&
                EVP_DigestSign(ctx, nullptr, &sigLen, reinterpret_cast<const unsigned char*>(input.data()),
                               input.size()) == 1) {
                std::vector<unsigned char> der(sigLen);
                if (EVP_DigestSign(ctx, der.data(), &sigLen,
                                   reinterpret_cast<const unsigned char*>(input.data()), input.size()) == 1) {
                    const unsigned char* p = der.data();
                    if (ECDSA_SIG* sig = d2i_ECDSA_SIG(nullptr, &p, static_cast<long>(sigLen))) {
                        const BIGNUM* r = nullptr;
                        const BIGNUM* s = nullptr;
                        ECDSA_SIG_get0(sig, &r, &s);
                        raw.assign(kCoordinateBytes * 2, '\0');
                        BN_bn2binpad(r, reinterpret_cast<unsigned char*>(raw.data()), kCoordinateBytes);
                        BN_bn2binpad(s, reinterpret_cast<unsigned char*>(raw.data()) + kCoordinateBytes,
                                     kCoordinateBytes);
                        ECDSA_SIG_free(sig);
                    }
                }
            }
            EVP_MD_CTX_free(ctx);
            return raw;
        }
    }

    struct ServerIdentity::Impl {
        EVP_PKEY* key = nullptr;
        std::string publicDer;
        std::string x, y;
    };

    std::string ServerIdentity::Base64(const std::string& data, bool url) {
        static const char* std64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        static const char* url64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        const char* alphabet = url ? url64 : std64;
        std::string out;
        size_t i = 0;
        const auto byte = [&](size_t k) { return static_cast<unsigned char>(data[k]); };
        for (; i + 2 < data.size(); i += 3) {
            const unsigned v = (byte(i) << 16) | (byte(i + 1) << 8) | byte(i + 2);
            out += alphabet[(v >> 18) & 63];
            out += alphabet[(v >> 12) & 63];
            out += alphabet[(v >> 6) & 63];
            out += alphabet[v & 63];
        }
        if (data.size() - i == 1) {
            const unsigned v = byte(i) << 16;
            out += alphabet[(v >> 18) & 63];
            out += alphabet[(v >> 12) & 63];
            if (!url)
                out += "==";
        } else if (data.size() - i == 2) {
            const unsigned v = (byte(i) << 16) | (byte(i + 1) << 8);
            out += alphabet[(v >> 18) & 63];
            out += alphabet[(v >> 12) & 63];
            out += alphabet[(v >> 6) & 63];
            if (!url)
                out += '=';
        }
        return out;
    }

    ServerIdentity::ServerIdentity() : impl(std::make_unique<Impl>()) {
        Adopt(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-384"));
    }

    bool ServerIdentity::LoadOrCreate(const std::string& path, std::string& note) {
        if (path.empty())
            return Valid();
        if (FILE* f = std::fopen(path.c_str(), "rb")) {
            EVP_PKEY* loaded = PEM_read_PrivateKey(f, nullptr, nullptr, nullptr);
            std::fclose(f);
            if (loaded && EVP_PKEY_is_a(loaded, "EC") && EVP_PKEY_get_bits(loaded) == 384) {
                Adopt(loaded);
                if (Valid()) {
                    note = "loaded from " + path;
                    return true;
                }
            } else if (loaded) {
                EVP_PKEY_free(loaded);
            }
            note = path + " is not a P-384 key; using a new one";
        }
        if (!Valid())
            Adopt(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-384"));
        if (!Valid())
            return false;
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        if (FILE* f = std::fopen(path.c_str(), "wb")) {
            const bool ok = PEM_write_PrivateKey(f, impl->key, nullptr, nullptr, 0, nullptr, nullptr) == 1;
            std::fclose(f);
#ifndef _WIN32
            std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                         ec);
#endif
            if (ok && note.empty())
                note = "new identity saved to " + path;
        }
        return true;
    }

    std::string ServerIdentity::Fingerprint() const {
        if (!Valid())
            return {};
        unsigned char digest[32];
        unsigned int len = 0;
        EVP_Digest(impl->publicDer.data(), impl->publicDer.size(), digest, &len, EVP_sha256(), nullptr);
        static const char* hex = "0123456789ABCDEF";
        std::string out;
        for (unsigned i = 0; i < len; i++) {
            if (i)
                out += ':';
            out += hex[digest[i] >> 4];
            out += hex[digest[i] & 15];
        }
        return out;
    }

    void ServerIdentity::Adopt(EVP_PKEY* key) {
        if (impl->key)
            EVP_PKEY_free(impl->key);
        impl->key = key;
        impl->publicDer.clear();
        impl->x.clear();
        impl->y.clear();
        if (!impl->key)
            return;
        unsigned char* der = nullptr;
        const int len = i2d_PUBKEY(impl->key, &der);
        if (len > 0) {
            impl->publicDer.assign(reinterpret_cast<char*>(der), static_cast<size_t>(len));
            OPENSSL_free(der);
        }
        impl->x = Coordinate(impl->key, OSSL_PKEY_PARAM_EC_PUB_X);
        impl->y = Coordinate(impl->key, OSSL_PKEY_PARAM_EC_PUB_Y);
    }

    ServerIdentity::~ServerIdentity() {
        if (impl && impl->key)
            EVP_PKEY_free(impl->key);
    }

    bool ServerIdentity::Valid() const {
        return impl->key && !impl->publicDer.empty() && impl->x.size() == kCoordinateBytes;
    }

    std::string ServerIdentity::SignDetached(const std::string& payload) const {
        const std::string header = Base64("{\"alg\":\"ES384\"}", true);
        const std::string input = header + "." + Base64(payload, true);
        const std::string raw = RawSign(impl->key, input);
        if (raw.empty())
            return {};
        return header + ".." + Base64(raw, true);
    }

    std::string ServerIdentity::Token() const {
        if (!Valid())
            return {};
        const std::string header = "{\"alg\":\"ES384\",\"x5u\":\"" + Base64(impl->publicDer, false) + "\"}";
        const long long now = static_cast<long long>(std::time(nullptr));
        const std::string claims = "{\"cpk\":{\"crv\":\"P-384\",\"kty\":\"EC\",\"x\":\"" +
                                   Base64(impl->x, true) + "\",\"y\":\"" + Base64(impl->y, true) +
                                   "\"},\"exp\":" + std::to_string(now + kTokenLifetimeSeconds) +
                                   ",\"iat\":" + std::to_string(now) + "}";
        const std::string input = Base64(header, true) + "." + Base64(claims, true);
        const std::string raw = RawSign(impl->key, input);
        if (raw.empty())
            return {};
        return input + "." + Base64(raw, true);
    }

    std::string ServerIdentity::FingerprintPayload(const std::string& algorithm, const std::string& digest) {
        return "{\"fingerprint\":[{\"algorithm\":\"" + EscapeJson(algorithm) + "\",\"digest\":\"" +
               EscapeJson(digest) + "\"}]}";
    }

    std::string ServerIdentity::AnswerIdentity(const std::string& algorithm,
                                               const std::string& digest) const {
        const std::string fingerprints = SignDetached(FingerprintPayload(algorithm, digest));
        const std::string token = Token();
        if (fingerprints.empty() || token.empty())
            return {};
        const std::string assertion =
            "{\"fingerprints\":\"" + EscapeJson(fingerprints) + "\",\"token\":\"" + EscapeJson(token) + "\"}";
        const std::string envelope = "{\"assertion\":\"" + EscapeJson(assertion) +
                                     "\",\"idp\":{\"domain\":\"self\",\"protocol\":\"default\"}}";
        return Base64(envelope, false);
    }

    namespace {
        std::vector<std::string> Lines(const std::string& sdp) {
            std::vector<std::string> lines;
            std::istringstream in(sdp);
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (!line.empty())
                    lines.push_back(line);
            }
            return lines;
        }

        std::string Join(const std::vector<std::string>& lines) {
            std::string out;
            for (const std::string& l : lines)
                out += l + "\r\n";
            return out;
        }
    }

    std::string AddAnswerIdentity(const std::string& sdp, const ServerIdentity& identity) {
        std::vector<std::string> lines = Lines(sdp);
        std::string algorithm, digest;
        for (const std::string& l : lines)
            if (l.starts_with("a=fingerprint:")) {
                const std::string v = l.substr(14);
                const size_t space = v.find(' ');
                if (space != std::string::npos) {
                    algorithm = v.substr(0, space);
                    digest = v.substr(space + 1);
                }
                break;
            }
        if (algorithm.empty())
            return sdp;
        const std::string value = identity.AnswerIdentity(algorithm, digest);
        if (value.empty())
            return sdp;
        size_t at = 0;
        while (at < lines.size() && !lines[at].starts_with("m="))
            at++;
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), "a=identity:" + value);
        return Join(lines);
    }

    std::string AddHostCandidate(const std::string& sdp, const std::string& address, uint16_t port) {
        if (address.empty() || port == 0 || address == "0.0.0.0" || address == "::")
            return sdp;
        std::vector<std::string> lines = Lines(sdp);
        size_t insertAt = lines.size();
        for (size_t i = 0; i < lines.size(); i++) {
            const std::string& l = lines[i];
            if (l.starts_with("a=candidate:") && l.find(" " + address + " ") != std::string::npos)
                return sdp;
            if (l == "a=end-of-candidates")
                insertAt = i;
        }
        if (insertAt == lines.size()) {
            for (size_t i = 0; i < lines.size(); i++)
                if (lines[i].starts_with("a=candidate:"))
                    insertAt = i + 1;
        }
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertAt),
                     "a=candidate:9 1 UDP 2113937151 " + address + " " + std::to_string(port) + " typ host");
        return Join(lines);
    }
}
