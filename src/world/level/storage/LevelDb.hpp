#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace perlite {

    class LevelDb {
    public:
        LevelDb();
        ~LevelDb();
        LevelDb(const LevelDb&) = delete;
        LevelDb& operator=(const LevelDb&) = delete;

        static bool Available();

        bool Open(const std::string& path, std::string& error);
        void Close();
        bool IsOpen() const;

        bool Get(std::string_view key, std::string& value) const;
        void Put(std::string_view key, std::string_view value);
        void Delete(std::string_view key);
        bool Commit(std::string* error = nullptr);
        void ForEachPrefix(std::string_view prefix,
                           const std::function<bool(std::string_view key, std::string_view value)>& fn) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
