#include "server/access/BanList.hpp"

#include "common/Json.hpp"
#include "common/Log.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

namespace perlite {
    namespace {
        std::string Escape(const std::string& s) {
            std::string out;
            for (char c : s) {
                if (c == '"' || c == '\\')
                    out += '\\';
                if (static_cast<unsigned char>(c) >= 0x20)
                    out += c;
            }
            return out;
        }
    }

    bool IpBanList::LooksLikeIp(const std::string& text) {
        if (text.empty() || text.size() > 45)
            return false;
        const bool v4 =
            std::all_of(text.begin(), text.end(),
                        [](char c) { return std::isdigit(static_cast<unsigned char>(c)) || c == '.'; }) &&
            std::count(text.begin(), text.end(), '.') == 3;
        const bool v6 =
            text.find(':') != std::string::npos && std::all_of(text.begin(), text.end(), [](char c) {
                return std::isxdigit(static_cast<unsigned char>(c)) || c == ':' || c == '.';
            });
        return v4 || v6;
    }

    void IpBanList::Load(const std::string& file) {
        std::lock_guard lock(mutex);
        path = file;
        bans.clear();
        if (path.empty())
            return;
        std::ifstream in(path);
        if (!in)
            return;
        std::stringstream text;
        text << in.rdbuf();
        json::Value root;
        if (!json::Parse(text.str(), root) || !root.IsArray()) {
            LogWarn(path + " is not a json array; ignoring it");
            return;
        }
        for (const json::Value& e : root.items) {
            IpBan ban;
            ban.ip = e["ip"].String();
            ban.reason = e["reason"].String();
            ban.source = e["source"].String();
            ban.created = e["created"].Int();
            if (!ban.ip.empty())
                bans.push_back(std::move(ban));
        }
    }

    bool IpBanList::Save() const {
        std::lock_guard lock(mutex);
        if (path.empty())
            return false;
        std::ofstream out(path, std::ios::trunc);
        out << "[\n";
        for (size_t i = 0; i < bans.size(); i++) {
            const IpBan& b = bans[i];
            out << "  {\"ip\": \"" << Escape(b.ip) << "\", \"reason\": \"" << Escape(b.reason)
                << "\", \"source\": \"" << Escape(b.source) << "\", \"created\": " << b.created << "}"
                << (i + 1 < bans.size() ? ",\n" : "\n");
        }
        out << "]\n";
        return out.good();
    }

    const IpBan* IpBanList::Find(const std::string& ip) const {
        for (const IpBan& b : bans)
            if (b.ip == ip)
                return &b;
        return nullptr;
    }

    bool IpBanList::Contains(const std::string& ip) const {
        std::lock_guard lock(mutex);
        return !ip.empty() && Find(ip) != nullptr;
    }

    bool IpBanList::Add(const std::string& ip, const std::string& reason, const std::string& source) {
        {
            std::lock_guard lock(mutex);
            if (Find(ip))
                return false;
            IpBan b;
            b.ip = ip;
            b.reason = reason;
            b.source = source;
            b.created = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
            bans.push_back(std::move(b));
        }
        Save();
        return true;
    }

    bool IpBanList::Remove(const std::string& ip) {
        {
            std::lock_guard lock(mutex);
            const auto it =
                std::find_if(bans.begin(), bans.end(), [&](const IpBan& b) { return b.ip == ip; });
            if (it == bans.end())
                return false;
            bans.erase(it);
        }
        Save();
        return true;
    }

    std::vector<IpBan> IpBanList::Entries() const {
        std::lock_guard lock(mutex);
        return bans;
    }
}
