#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace perlite {

    struct IpBan {
        std::string ip;
        std::string reason;
        std::string source;
        int64_t created = 0;
    };

    class IpBanList {
    public:
        void Load(const std::string& path);
        bool Save() const;
        bool Contains(const std::string& ip) const;
        const IpBan* Find(const std::string& ip) const;
        bool Add(const std::string& ip, const std::string& reason, const std::string& source);
        bool Remove(const std::string& ip);
        std::vector<IpBan> Entries() const;
        static bool LooksLikeIp(const std::string& text);

    private:
        mutable std::mutex mutex;
        std::string path;
        std::vector<IpBan> bans;
    };
}
