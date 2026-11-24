#include "common/Https.hpp"

#include <openssl/opensslconf.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

namespace perlite::https {
    namespace {
        constexpr size_t kMaxBody = 64u << 20;

        std::string Lower(std::string s) {
            for (char& c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        // OpenSSL's default paths are empty on Windows; the trusted roots live in the system store.
        [[maybe_unused]] void LoadSystemRoots(SSL_CTX* ctx) {
            SSL_CTX_set_default_verify_paths(ctx);
#ifdef _WIN32
            X509_STORE* store = SSL_CTX_get_cert_store(ctx);
            if (HCERTSTORE sys = CertOpenSystemStoreW(0, L"ROOT")) {
                PCCERT_CONTEXT cert = nullptr;
                while ((cert = CertEnumCertificatesInStore(sys, cert)) != nullptr) {
                    const unsigned char* der = cert->pbCertEncoded;
                    if (X509* x = d2i_X509(nullptr, &der, static_cast<long>(cert->cbCertEncoded))) {
                        X509_STORE_add_cert(store, x);
                        X509_free(x);
                    }
                }
                CertCloseStore(sys, 0);
            }
#endif
        }

        bool SplitUrl(const std::string& url, std::string& host, std::string& path) {
            if (!url.starts_with("https://"))
                return false;
            const size_t slash = url.find('/', 8);
            host = url.substr(8, slash == std::string::npos ? std::string::npos : slash - 8);
            path = slash == std::string::npos ? "/" : url.substr(slash);
            return !host.empty();
        }
    }

    struct Client::Impl {
        SSL_CTX* ctx = nullptr;
        BIO* bio = nullptr;
        std::string buffer;  // bytes read past the end of the last response

        ~Impl() {
            Close();
            if (ctx)
                SSL_CTX_free(ctx);
        }

        void Close() {
            if (bio)
                BIO_free_all(bio);
            bio = nullptr;
            buffer.clear();
        }

        bool Connect(const std::string& host, std::string* error) {
#if defined(OPENSSL_NO_SOCK) || defined(__EMSCRIPTEN__)
            // A build without sockets (the web build's OpenSSL is configured no-sock): there is
            // no way to reach the host, so every request fails cleanly instead.
            (void)host;
            if (error)
                *error = "HTTPS downloads are not available in this build";
            return false;
#else
            if (!ctx) {
                ctx = SSL_CTX_new(TLS_client_method());
                if (!ctx) {
                    if (error)
                        *error = "TLS is not available";
                    return false;
                }
                LoadSystemRoots(ctx);
                SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
            }
            bio = BIO_new_ssl_connect(ctx);
            if (!bio)
                return false;
            SSL* ssl = nullptr;
            BIO_get_ssl(bio, &ssl);
            const std::string target = host + ":443";
            BIO_set_conn_hostname(bio, target.c_str());
            if (ssl) {
                SSL_set_tlsext_host_name(ssl, host.c_str());
                SSL_set1_host(ssl, host.c_str());
            }
            if (BIO_do_connect(bio) != 1 || BIO_do_handshake(bio) != 1) {
                if (error)
                    *error = "could not connect to " + host;
                Close();
                return false;
            }
            return true;
#endif
        }

        // Makes sure the buffer holds at least n bytes; false when the connection ends first.
        bool Fill(size_t n) {
            char chunk[16384];
            while (buffer.size() < n) {
                const int got = BIO_read(bio, chunk, sizeof chunk);
                if (got <= 0)
                    return false;
                buffer.append(chunk, static_cast<size_t>(got));
            }
            return true;
        }

        bool ReadLine(std::string& line) {
            size_t eol;
            while ((eol = buffer.find("\r\n")) == std::string::npos) {
                const size_t before = buffer.size();
                if (!Fill(before + 1))
                    return false;
            }
            line = buffer.substr(0, eol);
            buffer.erase(0, eol + 2);
            return true;
        }

        bool Take(size_t n, std::string& out) {
            if (n > kMaxBody || !Fill(n))
                return false;
            out.append(buffer, 0, n);
            buffer.erase(0, n);
            return true;
        }

        // Hands n body bytes to the sink as they arrive, with no size limit (big downloads).
        bool Stream(size_t n, const Sink& sink) {
            char chunk[65536];
            if (!buffer.empty()) {
                const size_t take = std::min(n, buffer.size());
                if (!sink(buffer.data(), take))
                    return false;
                buffer.erase(0, take);
                n -= take;
            }
            while (n > 0) {
                const int got = BIO_read(bio, chunk, static_cast<int>(std::min(n, sizeof chunk)));
                if (got <= 0)
                    return false;
                if (!sink(chunk, static_cast<size_t>(got)))
                    return false;
                n -= static_cast<size_t>(got);
            }
            return true;
        }
    };

    Client::Client(std::string host) : host(std::move(host)), impl(std::make_unique<Impl>()) {}
    Client::~Client() = default;

    bool Client::Get(const std::string& path, Response& out, std::string* error) {
        return Get(path, out, nullptr, error);
    }

    bool Client::Get(const std::string& path, Response& out, const Sink* sink, std::string* error) {
        out = {};
        for (int attempt = 0; attempt < 2; attempt++) {
            const bool reused = impl->bio != nullptr;
            if (!impl->bio && !impl->Connect(host, error))
                return false;
            const std::string request = "GET " + path + " HTTP/1.1\r\nHost: " + host +
                                        "\r\nUser-Agent: Perlite\r\nAccept: */*\r\nConnection: keep-alive\r\n\r\n";
            std::string status;
            if (BIO_write(impl->bio, request.data(), static_cast<int>(request.size())) !=
                    static_cast<int>(request.size()) ||
                !impl->ReadLine(status)) {
                impl->Close();
                if (reused)
                    continue;  // the server closed the idle connection; try once on a new one
                if (error)
                    *error = "no answer from " + host;
                return false;
            }
            if (!status.starts_with("HTTP/1.") || status.size() < 12) {
                impl->Close();
                if (error)
                    *error = "bad answer from " + host;
                return false;
            }
            out.status = std::atoi(status.c_str() + 9);
            long long length = -1;
            bool chunked = false, close = false;
            std::string line;
            while (impl->ReadLine(line) && !line.empty()) {
                const size_t colon = line.find(':');
                if (colon == std::string::npos)
                    continue;
                const std::string name = Lower(line.substr(0, colon));
                std::string value = line.substr(colon + 1);
                while (!value.empty() && value.front() == ' ')
                    value.erase(0, 1);
                if (name == "content-length")
                    length = std::atoll(value.c_str());
                else if (name == "transfer-encoding")
                    chunked = Lower(value).contains("chunked");
                else if (name == "connection")
                    close = Lower(value).contains("close");
                else if (name == "location")
                    out.location = value;
            }
            bool ok = true;
            // A redirect or an error page is kept in the body; only a 2xx body goes to the sink.
            const Sink* to = out.status >= 200 && out.status < 300 ? sink : nullptr;
            if (to) {
                out.length = length;
                if (!(*to)(nullptr, 0)) { // a zero-length call announces the start
                    impl->Close();
                    return false;
                }
            }
            if (chunked) {
                for (;;) {
                    std::string size;
                    if (!impl->ReadLine(size)) {
                        ok = false;
                        break;
                    }
                    const size_t n = std::strtoul(size.c_str(), nullptr, 16);
                    if (n == 0) {
                        while (impl->ReadLine(line) && !line.empty()) {
                        }
                        break;
                    }
                    if (to ? !impl->Stream(n, *to) || !impl->ReadLine(line)
                           : out.body.size() + n > kMaxBody || !impl->Take(n, out.body) || !impl->ReadLine(line)) {
                        ok = false;
                        break;
                    }
                }
            } else if (length >= 0) {
                ok = to ? impl->Stream(static_cast<size_t>(length), *to) : impl->Take(static_cast<size_t>(length), out.body);
            } else {
                char chunk[16384];
                std::string first = std::move(impl->buffer);
                impl->buffer.clear();
                if (to) {
                    ok = first.empty() || (*to)(first.data(), first.size());
                    int got;
                    while (ok && (got = BIO_read(impl->bio, chunk, sizeof chunk)) > 0)
                        ok = (*to)(chunk, static_cast<size_t>(got));
                } else {
                    out.body = std::move(first);
                    int got;
                    while (out.body.size() < kMaxBody && (got = BIO_read(impl->bio, chunk, sizeof chunk)) > 0)
                        out.body.append(chunk, static_cast<size_t>(got));
                }
                close = true;
            }
            if (!ok || close)
                impl->Close();
            if (!ok && error)
                *error = "the answer from " + host + " was cut off";
            return ok;
        }
        return false;
    }

    bool Get(const std::string& url, Response& out, std::string* error) {
        return Get(url, out, nullptr, error);
    }

    bool Get(const std::string& url, Response& out, const Sink* sink, std::string* error) {
        std::string current = url;
        for (int hop = 0; hop < 6; hop++) {
            std::string host, path;
            if (!SplitUrl(current, host, path)) {
                if (error)
                    *error = "not an https url: " + current;
                return false;
            }
            Client client(host);
            if (!client.Get(path, out, sink, error))
                return false;
            if (out.status < 300 || out.status >= 400 || out.location.empty())
                return true;
            current = out.location.starts_with("/") ? "https://" + host + out.location : out.location;
        }
        if (error)
            *error = "too many redirects";
        return false;
    }
}
