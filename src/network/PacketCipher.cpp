#include "network/PacketCipher.hpp"

#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/x509.h>

#include <cstring>

namespace perlite {
    namespace {
        std::string Base64(const std::string& data, bool url) {
            std::string out(4 * ((data.size() + 2) / 3) + 1, '\0');
            const int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                                          reinterpret_cast<const unsigned char*>(data.data()),
                                          static_cast<int>(data.size()));
            out.resize(static_cast<size_t>(std::max(n, 0)));
            if (url) {
                while (!out.empty() && out.back() == '=')
                    out.pop_back();
                for (char& c : out)
                    c = c == '+' ? '-' : c == '/' ? '_' : c;
            }
            return out;
        }

        std::string Base64Decode(std::string in) {
            for (char& c : in)
                c = c == '-' ? '+' : c == '_' ? '/' : c;
            while (in.size() % 4)
                in += '=';
            std::string out(in.size() / 4 * 3, '\0');
            const int n = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                                          reinterpret_cast<const unsigned char*>(in.data()),
                                          static_cast<int>(in.size()));
            if (n < 0)
                return {};
            size_t len = static_cast<size_t>(n);
            for (size_t i = in.size(); i > 0 && in[i - 1] == '=' && len > 0; i--)
                len--;
            out.resize(len);
            return out;
        }

        std::string SignRaw(EVP_PKEY* key, const std::string& input) {
            EVP_MD_CTX* ctx = EVP_MD_CTX_new();
            std::string raw;
            size_t len = 0;
            if (ctx && EVP_DigestSignInit(ctx, nullptr, EVP_sha384(), nullptr, key) == 1 &&
                EVP_DigestSign(ctx, nullptr, &len, reinterpret_cast<const unsigned char*>(input.data()),
                               input.size()) == 1) {
                std::vector<unsigned char> der(len);
                if (EVP_DigestSign(ctx, der.data(), &len,
                                   reinterpret_cast<const unsigned char*>(input.data()), input.size()) == 1) {
                    const unsigned char* p = der.data();
                    if (ECDSA_SIG* sig = d2i_ECDSA_SIG(nullptr, &p, static_cast<long>(len))) {
                        const BIGNUM* r = nullptr;
                        const BIGNUM* s = nullptr;
                        ECDSA_SIG_get0(sig, &r, &s);
                        raw.assign(96, '\0');
                        BN_bn2binpad(r, reinterpret_cast<unsigned char*>(raw.data()), 48);
                        BN_bn2binpad(s, reinterpret_cast<unsigned char*>(raw.data()) + 48, 48);
                        ECDSA_SIG_free(sig);
                    }
                }
            }
            EVP_MD_CTX_free(ctx);
            return raw;
        }
    }

    struct PacketCipher::Impl {
        EVP_CIPHER_CTX* enc = nullptr;
        EVP_CIPHER_CTX* dec = nullptr;
        uint8_t key[32] = {};
        uint64_t sendCounter = 0;
        uint64_t recvCounter = 0;

        ~Impl() {
            if (enc)
                EVP_CIPHER_CTX_free(enc);
            if (dec)
                EVP_CIPHER_CTX_free(dec);
        }

        void Checksum(uint64_t counter, const uint8_t* data, size_t n, uint8_t out[8]) const {
            uint8_t c[8];
            for (int i = 0; i < 8; i++)
                c[i] = static_cast<uint8_t>(counter >> (8 * i));
            EVP_MD_CTX* ctx = EVP_MD_CTX_new();
            unsigned char digest[32];
            unsigned int len = 0;
            EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
            EVP_DigestUpdate(ctx, c, 8);
            EVP_DigestUpdate(ctx, data, n);
            EVP_DigestUpdate(ctx, key, 32);
            EVP_DigestFinal_ex(ctx, digest, &len);
            EVP_MD_CTX_free(ctx);
            std::memcpy(out, digest, 8);
        }
    };

    PacketCipher::PacketCipher() : impl(std::make_unique<Impl>()) {}
    PacketCipher::~PacketCipher() = default;

    bool PacketCipher::InitWithKey(const uint8_t key[32]) {
        Impl& s = *impl;
        std::memcpy(s.key, key, 32);
        uint8_t iv[16];
        std::memcpy(iv, key, 12);
        iv[12] = 0;
        iv[13] = 0;
        iv[14] = 0;
        iv[15] = 2;
        for (EVP_CIPHER_CTX** ctx : {&s.enc, &s.dec}) {
            if (*ctx)
                EVP_CIPHER_CTX_free(*ctx);
            *ctx = EVP_CIPHER_CTX_new();
            if (!*ctx || EVP_EncryptInit_ex(*ctx, EVP_aes_256_ctr(), nullptr, key, iv) != 1)
                return false;
        }
        s.sendCounter = s.recvCounter = 0;
        ready = true;
        return true;
    }

    bool PacketCipher::PrepareServerHandshake(const std::string& clientKeyBase64, std::string& token,
                                              std::string& error) {
        const std::string clientDer = Base64Decode(clientKeyBase64);
        const unsigned char* p = reinterpret_cast<const unsigned char*>(clientDer.data());
        EVP_PKEY* peer = d2i_PUBKEY(nullptr, &p, static_cast<long>(clientDer.size()));
        if (!peer) {
            error = "the login names no usable client key";
            return false;
        }
        EVP_PKEY* own = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-384");
        EVP_PKEY_CTX* derive = own ? EVP_PKEY_CTX_new(own, nullptr) : nullptr;
        std::string secret;
        size_t secretLen = 0;
        if (derive && EVP_PKEY_derive_init(derive) == 1 && EVP_PKEY_derive_set_peer(derive, peer) == 1 &&
            EVP_PKEY_derive(derive, nullptr, &secretLen) == 1) {
            secret.resize(secretLen);
            if (EVP_PKEY_derive(derive, reinterpret_cast<unsigned char*>(secret.data()), &secretLen) != 1)
                secret.clear();
            else
                secret.resize(secretLen);
        }
        EVP_PKEY_CTX_free(derive);
        EVP_PKEY_free(peer);
        std::string ownDer;
        if (own) {
            unsigned char* der = nullptr;
            const int len = i2d_PUBKEY(own, &der);
            if (len > 0) {
                ownDer.assign(reinterpret_cast<char*>(der), static_cast<size_t>(len));
                OPENSSL_free(der);
            }
        }
        std::string salt(16, '\0');
        RAND_bytes(reinterpret_cast<unsigned char*>(salt.data()), static_cast<int>(salt.size()));
        if (secret.empty() || ownDer.empty()) {
            EVP_PKEY_free(own);
            error = "the key exchange failed";
            return false;
        }
        const std::string header = "{\"alg\":\"ES384\",\"x5u\":\"" + Base64(ownDer, false) + "\"}";
        const std::string payload = "{\"salt\":\"" + Base64(salt, false) + "\"}";
        const std::string input = Base64(header, true) + "." + Base64(payload, true);
        const std::string signature = SignRaw(own, input);
        EVP_PKEY_free(own);
        if (signature.empty()) {
            error = "could not sign the handshake";
            return false;
        }
        token = input + "." + Base64(signature, true);
        std::string material = salt + secret;
        uint8_t key[32];
        unsigned int keyLen = 0;
        EVP_Digest(material.data(), material.size(), key, &keyLen, EVP_sha256(), nullptr);
        if (!InitWithKey(key)) {
            error = "could not start AES";
            return false;
        }
        return true;
    }

    std::vector<uint8_t> PacketCipher::Encrypt(std::span<const uint8_t> plain) {
        Impl& s = *impl;
        std::vector<uint8_t> out(plain.size() + 8);
        if (!plain.empty())
            std::memcpy(out.data(), plain.data(), plain.size());
        s.Checksum(s.sendCounter++, plain.data(), plain.size(), out.data() + plain.size());
        int outLen = 0;
        EVP_EncryptUpdate(s.enc, out.data(), &outLen, out.data(), static_cast<int>(out.size()));
        return out;
    }

    bool PacketCipher::Decrypt(std::span<const uint8_t> cipher, std::vector<uint8_t>& plain) {
        Impl& s = *impl;
        if (cipher.size() < 8)
            return false;
        plain.assign(cipher.size(), 0);
        int outLen = 0;
        EVP_EncryptUpdate(s.dec, plain.data(), &outLen, cipher.data(), static_cast<int>(cipher.size()));
        const size_t n = plain.size() - 8;
        uint8_t sum[8];
        s.Checksum(s.recvCounter++, plain.data(), n, sum);
        const bool ok = std::memcmp(sum, plain.data() + n, 8) == 0;
        plain.resize(n);
        return ok;
    }
}
