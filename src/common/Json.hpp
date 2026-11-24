#pragma once
#include <cstdint>
#include <algorithm>
#include <map>
#include <utility>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace perlite::json {

    // An object's members, sorted by key in one flat array: a lookup is a few length checks and one compare
    // instead of a walk through tree nodes with a string compare at each (world generation reads its feature
    // definitions this way millions of times per chunk). Iterates in key order, as the std::map it replaces.
    template <class V>
    class MembersOf {
    public:
        using Entry = std::pair<std::string, V>;
        using iterator = typename std::vector<Entry>::iterator;
        using const_iterator = typename std::vector<Entry>::const_iterator;

        iterator begin() { return entries.begin(); }
        iterator end() { return entries.end(); }
        const_iterator begin() const { return entries.begin(); }
        const_iterator end() const { return entries.end(); }
        size_t size() const { return entries.size(); }
        bool empty() const { return entries.empty(); }
        void clear() { entries.clear(); }

        const_iterator find(std::string_view key) const;
        iterator find(std::string_view key);
        bool contains(std::string_view key) const { return find(key) != end(); }
        // The member for key, added (null) when missing.
        V& operator[](std::string_view key);

        // For the parser: add without keeping the order, then Finish once (a repeated key keeps its last value).
        V& Append(std::string key);
        void Finish();

    private:
        std::vector<Entry> entries;
    };

    class Value {
    public:
        enum class Kind : uint8_t { Null, Bool, Number, String, Array, Object };

        Kind kind = Kind::Null;
        bool boolean = false;
        double number = 0;
        std::string text;
        std::vector<Value> items;
        MembersOf<Value> members;

        bool IsNull() const { return kind == Kind::Null; }
        bool IsObject() const { return kind == Kind::Object; }
        bool IsArray() const { return kind == Kind::Array; }
        bool IsString() const { return kind == Kind::String; }

        const Value& operator[](std::string_view key) const;
        const Value& operator[](size_t index) const;

        std::string String(std::string_view fallback = {}) const {
            return kind == Kind::String ? text : std::string(fallback);
        }
        double Number(double fallback = 0) const { return kind == Kind::Number ? number : fallback; }
        int64_t Int(int64_t fallback = 0) const {
            return kind == Kind::Number ? static_cast<int64_t>(number) : fallback;
        }
        bool Bool(bool fallback = false) const { return kind == Kind::Bool ? boolean : fallback; }
        size_t Size() const {
            return kind == Kind::Array ? items.size() : kind == Kind::Object ? members.size() : 0;
        }
    };

    using Members = MembersOf<Value>;

    template <class V>
    typename MembersOf<V>::const_iterator MembersOf<V>::find(std::string_view key) const {
        if (entries.size() <= 8) {
            for (auto it = entries.begin(); it != entries.end(); ++it)
                if (it->first.size() == key.size() && it->first == key)
                    return it;
            return entries.end();
        }
        const auto it = std::lower_bound(entries.begin(), entries.end(), key,
                                         [](const Entry& e, std::string_view k) { return std::string_view(e.first) < k; });
        return it != entries.end() && it->first == key ? it : entries.end();
    }
    template <class V>
    typename MembersOf<V>::iterator MembersOf<V>::find(std::string_view key) {
        const auto it = static_cast<const MembersOf*>(this)->find(key);
        return entries.begin() + (it - entries.cbegin());
    }
    template <class V>
    V& MembersOf<V>::operator[](std::string_view key) {
        const auto it = std::lower_bound(entries.begin(), entries.end(), key,
                                         [](const Entry& e, std::string_view k) { return std::string_view(e.first) < k; });
        if (it != entries.end() && it->first == key)
            return it->second;
        return entries.insert(it, Entry(std::string(key), V{}))->second;
    }
    template <class V>
    V& MembersOf<V>::Append(std::string key) {
        entries.emplace_back(std::move(key), V{});
        return entries.back().second;
    }
    template <class V>
    void MembersOf<V>::Finish() {
        if (entries.size() < 2)
            return;
        std::stable_sort(entries.begin(), entries.end(),
                         [](const Entry& a, const Entry& b) { return a.first < b.first; });
        // A key given twice keeps the value given last, as assigning into a map did.
        size_t out = 0;
        for (size_t i = 0; i < entries.size(); i++) {
            if (i + 1 < entries.size() && entries[i + 1].first == entries[i].first)
                continue;
            if (out != i)
                entries[out] = std::move(entries[i]);
            out++;
        }
        entries.resize(out);
    }

    bool Parse(std::string_view text, Value& out, std::string* error = nullptr);

    bool DecodeJwtPayload(std::string_view jwt, Value& out);
    std::string Base64UrlDecode(std::string_view text);
}
