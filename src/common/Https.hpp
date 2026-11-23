#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace perlite::https {

    struct Response {
        int status = 0;
        std::string body;
        std::string location;  // the Location header of a redirect
        long long length = -1; // the Content-Length of a streamed body, -1 when not given
    };

    // Takes a streamed body piece by piece; false stops the download. It is called once with
    // (nullptr, 0) before the first piece, once the status and length are known.
    using Sink = std::function<bool(const char* data, size_t size)>;

    // An HTTPS/1.1 client for one host that keeps its connection open between requests, so many small
    // files can be fetched without a new TLS handshake each. Certificates are checked against the
    // system's trusted roots.
    class Client {
    public:
        explicit Client(std::string host);
        ~Client();
        Client(const Client&) = delete;
        Client& operator=(const Client&) = delete;

        // GET path (starting with '/'). False when the request could not be made at all; an HTTP
        // error status still returns true with the status set.
        bool Get(const std::string& path, Response& out, std::string* error = nullptr);
        // The same, with a 2xx body handed to `sink` instead of kept (no size limit).
        bool Get(const std::string& path, Response& out, const Sink* sink, std::string* error);

        const std::string& Host() const { return host; }

    private:
        struct Impl;
        std::string host;
        std::unique_ptr<Impl> impl;
    };

    // One GET of a full https:// URL, following up to five redirects.
    bool Get(const std::string& url, Response& out, std::string* error = nullptr);
    // One GET whose 2xx body is streamed into `sink` (for downloads too big to keep in memory).
    bool Get(const std::string& url, Response& out, const Sink* sink, std::string* error);
}
