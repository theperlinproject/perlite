#pragma once
#include "protocol/NbtWriter.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace perlite::nbt {

    struct Value {
        Tag type = End;
        int64_t integer = 0;
        double real = 0;
        std::string text;
        std::vector<uint8_t> bytes;
        std::vector<int64_t> numbers;
        Tag listType = End;
        std::vector<Value> list;
        struct Entry;
        std::vector<Entry> compound;

        Value();
        Value(const Value&);
        Value(Value&&) noexcept;
        Value& operator=(const Value&);
        Value& operator=(Value&&) noexcept;
        ~Value();

        const Value* Find(std::string_view key) const;
        Value* Find(std::string_view key);
        int64_t Int(std::string_view key, int64_t fallback = 0) const;
        double Real(std::string_view key, double fallback = 0) const;
        std::string String(std::string_view key, std::string_view fallback = {}) const;

        static Value Make(Tag type);
        Value& Set(std::string_view key, Value v);
        Value& SetInt(std::string_view key, int32_t v);
        Value& SetLong(std::string_view key, int64_t v);
        Value& SetByte(std::string_view key, int8_t v);
        Value& SetShort(std::string_view key, int16_t v);
        Value& SetFloat(std::string_view key, float v);
        Value& SetString(std::string_view key, std::string_view v);
    };

    struct Value::Entry {
        std::string first;
        Value second;
    };

    inline Value::Value() = default;
    inline Value::Value(const Value&) = default;
    inline Value::Value(Value&&) noexcept = default;
    inline Value& Value::operator=(const Value&) = default;
    inline Value& Value::operator=(Value&&) noexcept = default;
    inline Value::~Value() = default;

    inline Value Value::Make(Tag type) {
        Value v;
        v.type = type;
        return v;
    }

    bool Read(std::span<const uint8_t> data, Encoding encoding, Value& root, std::string* rootName = nullptr,
              size_t* consumed = nullptr);
    void Write(const Value& root, Encoding encoding, std::vector<uint8_t>& out,
               std::string_view rootName = {});
}
