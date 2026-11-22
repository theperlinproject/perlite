#pragma once

#include <memory>
#include <string>

namespace perlite::https {

    struct Response {
        int status = 0;
        std::string body;
        std::string location;  // the Location header of a redirect
    };

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

        const std::string& Host() const { return host; }

    private:
        struct Impl;
        std::string host;
        std::unique_ptr<Impl> impl;
    };

    // One GET of a full https:// URL, following up to five redirects.
    bool Get(const std::string& url, Response& out, std::string* error = nullptr);
}
