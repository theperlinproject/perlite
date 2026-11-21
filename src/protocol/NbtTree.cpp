#include "protocol/NbtTree.hpp"

#include <cstring>

namespace perlite::nbt {
    namespace {
        constexpr int kMaxDepth = 512;

        class Reader {
        public:
            Reader(std::span<const uint8_t> d, Encoding e) : data(d), encoding(e) {}

            bool ok = true;
            size_t at = 0;

            uint8_t U8() {
                if (at >= data.size()) {
                    ok = false;
                    return 0;
                }
                return data[at++];
            }
            uint64_t RawLe(int n) {
                if (at + static_cast<size_t>(n) > data.size()) {
                    ok = false;
                    at = data.size();
                    return 0;
                }
                uint64_t v = 0;
                for (int i = 0; i < n; i++)
                    v |= static_cast<uint64_t>(data[at + static_cast<size_t>(i)]) << (8 * i);
                at += static_cast<size_t>(n);
                return v;
            }
            uint64_t VarU(int maxBits) {
                uint64_t v = 0;
                for (int shift = 0; shift < maxBits; shift += 7) {
                    const uint8_t b = U8();
                    if (!ok)
                        return 0;
                    v |= static_cast<uint64_t>(b & 0x7F) << shift;
                    if (!(b & 0x80))
                        return v;
                }
                ok = false;
                return 0;
            }
            int32_t I32() {
                if (encoding == Encoding::Network) {
                    const uint32_t z = static_cast<uint32_t>(VarU(35));
                    return static_cast<int32_t>((z >> 1) ^ (~(z & 1) + 1));
                }
                return static_cast<int32_t>(RawLe(4));
            }
            int64_t I64() {
                if (encoding == Encoding::Network) {
                    const uint64_t z = VarU(70);
                    return static_cast<int64_t>((z >> 1) ^ (~(z & 1) + 1));
                }
                return static_cast<int64_t>(RawLe(8));
            }
            std::string Str() {
                const size_t len = encoding == Encoding::Network ? static_cast<size_t>(VarU(35))
                                                                 : static_cast<size_t>(RawLe(2));
                if (!ok || at + len > data.size()) {
                    ok = false;
                    return {};
                }
                std::string s(reinterpret_cast<const char*>(data.data() + at), len);
                at += len;
                return s;
            }

            bool Payload(Tag type, Value& v, int depth) {
                if (depth > kMaxDepth)
                    return ok = false;
                v.type = type;
                switch (type) {
                case Byte:
                    v.integer = static_cast<int8_t>(U8());
                    break;
                case Short:
                    v.integer = static_cast<int16_t>(RawLe(2));
                    break;
                case Int:
                    v.integer = I32();
                    break;
                case Long:
                    v.integer = I64();
                    break;
                case Float: {
                    const uint32_t bits = static_cast<uint32_t>(RawLe(4));
                    float f;
                    std::memcpy(&f, &bits, 4);
                    v.real = f;
                    break;
                }
                case Double: {
                    const uint64_t bits = RawLe(8);
                    double d;
                    std::memcpy(&d, &bits, 8);
                    v.real = d;
                    break;
                }
                case ByteArray: {
                    const int32_t n = I32();
                    if (n < 0 || at + static_cast<size_t>(n) > data.size())
                        return ok = false;
                    v.bytes.assign(data.begin() + static_cast<std::ptrdiff_t>(at),
                                   data.begin() + static_cast<std::ptrdiff_t>(at + static_cast<size_t>(n)));
                    at += static_cast<size_t>(n);
                    break;
                }
                case String:
                    v.text = Str();
                    break;
                case List: {
                    v.listType = static_cast<Tag>(U8());
                    const int32_t n = I32();
                    if (n < 0 || static_cast<size_t>(n) > data.size())
                        return ok = false;
                    v.list.resize(static_cast<size_t>(n));
                    for (Value& item : v.list)
                        if (!Payload(v.listType, item, depth + 1))
                            return false;
                    break;
                }
                case Compound:
                    for (;;) {
                        const Tag t = static_cast<Tag>(U8());
                        if (!ok)
                            return false;
                        if (t == End)
                            break;
                        std::string name = Str();
                        Value child;
                        if (!Payload(t, child, depth + 1))
                            return false;
                        v.compound.emplace_back(std::move(name), std::move(child));
                    }
                    break;
                case IntArray:
                case LongArray: {
                    const int32_t n = I32();
                    if (n < 0 || static_cast<size_t>(n) > data.size())
                        return ok = false;
                    v.numbers.resize(static_cast<size_t>(n));
                    for (int64_t& x : v.numbers)
                        x = type == IntArray ? I32() : I64();
                    break;
                }
                default:
                    return ok = false;
                }
                return ok;
            }

        private:
            std::span<const uint8_t> data;
            Encoding encoding;
        };

        class Out {
        public:
            Out(std::vector<uint8_t>& o, Encoding e) : out(o), encoding(e) {}

            void U8(uint8_t v) { out.push_back(v); }
            void Le(uint64_t v, int n) {
                for (int i = 0; i < n; i++)
                    out.push_back(static_cast<uint8_t>(v >> (8 * i)));
            }
            void VarU(uint64_t v) {
                while (v >= 0x80) {
                    out.push_back(static_cast<uint8_t>(v | 0x80));
                    v >>= 7;
                }
                out.push_back(static_cast<uint8_t>(v));
            }
            void I32(int32_t v) {
                if (encoding == Encoding::Network)
                    VarU((static_cast<uint32_t>(v) << 1) ^ static_cast<uint32_t>(v >> 31));
                else
                    Le(static_cast<uint32_t>(v), 4);
            }
            void I64(int64_t v) {
                if (encoding == Encoding::Network)
                    VarU((static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63));
                else
                    Le(static_cast<uint64_t>(v), 8);
            }
            void Str(std::string_view s) {
                if (encoding == Encoding::Network)
                    VarU(s.size());
                else
                    Le(s.size(), 2);
                out.insert(out.end(), s.begin(), s.end());
            }

            void Payload(const Value& v) {
                switch (v.type) {
                case Byte:
                    U8(static_cast<uint8_t>(v.integer));
                    break;
                case Short:
                    Le(static_cast<uint16_t>(v.integer), 2);
                    break;
                case Int:
                    I32(static_cast<int32_t>(v.integer));
                    break;
                case Long:
                    I64(v.integer);
                    break;
                case Float: {
                    const float f = static_cast<float>(v.real);
                    uint32_t bits;
                    std::memcpy(&bits, &f, 4);
                    Le(bits, 4);
                    break;
                }
                case Double: {
                    uint64_t bits;
                    std::memcpy(&bits, &v.real, 8);
                    Le(bits, 8);
                    break;
                }
                case ByteArray:
                    I32(static_cast<int32_t>(v.bytes.size()));
                    out.insert(out.end(), v.bytes.begin(), v.bytes.end());
                    break;
                case String:
                    Str(v.text);
                    break;
                case List:
                    U8(v.list.empty() && v.listType == End ? End : v.listType);
                    I32(static_cast<int32_t>(v.list.size()));
                    for (const Value& item : v.list)
                        Payload(item);
                    break;
                case Compound:
                    for (const auto& [name, child] : v.compound) {
                        U8(child.type);
                        Str(name);
                        Payload(child);
                    }
                    U8(End);
                    break;
                case IntArray:
                case LongArray:
                    I32(static_cast<int32_t>(v.numbers.size()));
                    for (int64_t x : v.numbers) {
                        if (v.type == IntArray)
                            I32(static_cast<int32_t>(x));
                        else
                            I64(x);
                    }
                    break;
                default:
                    break;
                }
            }

        private:
            std::vector<uint8_t>& out;
            Encoding encoding;
        };
    }

    const Value* Value::Find(std::string_view key) const {
        for (const auto& [k, v] : compound)
            if (k == key)
                return &v;
        return nullptr;
    }

    Value* Value::Find(std::string_view key) {
        for (auto& [k, v] : compound)
            if (k == key)
                return &v;
        return nullptr;
    }

    int64_t Value::Int(std::string_view key, int64_t fallback) const {
        const Value* v = Find(key);
        if (!v)
            return fallback;
        if (v->type == nbt::Float || v->type == nbt::Double)
            return static_cast<int64_t>(v->real);
        return v->type >= nbt::Byte && v->type <= nbt::Long ? v->integer : fallback;
    }

    double Value::Real(std::string_view key, double fallback) const {
        const Value* v = Find(key);
        if (!v)
            return fallback;
        if (v->type == nbt::Float || v->type == nbt::Double)
            return v->real;
        return v->type >= nbt::Byte && v->type <= nbt::Long ? static_cast<double>(v->integer) : fallback;
    }

    std::string Value::String(std::string_view key, std::string_view fallback) const {
        const Value* v = Find(key);
        return v && v->type == nbt::String ? v->text : std::string(fallback);
    }

    Value& Value::Set(std::string_view key, Value v) {
        type = nbt::Compound;
        if (Value* existing = Find(key)) {
            *existing = std::move(v);
            return *existing;
        }
        compound.emplace_back(std::string(key), std::move(v));
        return compound.back().second;
    }

    Value& Value::SetInt(std::string_view key, int32_t v) {
        Value x = Value::Make(nbt::Int);
        x.integer = v;
        return Set(key, std::move(x));
    }
    Value& Value::SetLong(std::string_view key, int64_t v) {
        Value x = Value::Make(nbt::Long);
        x.integer = v;
        return Set(key, std::move(x));
    }
    Value& Value::SetByte(std::string_view key, int8_t v) {
        Value x = Value::Make(nbt::Byte);
        x.integer = v;
        return Set(key, std::move(x));
    }
    Value& Value::SetShort(std::string_view key, int16_t v) {
        Value x = Value::Make(nbt::Short);
        x.integer = v;
        return Set(key, std::move(x));
    }
    Value& Value::SetFloat(std::string_view key, float v) {
        Value x = Value::Make(nbt::Float);
        x.real = v;
        return Set(key, std::move(x));
    }
    Value& Value::SetString(std::string_view key, std::string_view v) {
        Value x = Value::Make(nbt::String);
        x.text = std::string(v);
        return Set(key, std::move(x));
    }

    bool Read(std::span<const uint8_t> data, Encoding encoding, Value& root, std::string* rootName,
              size_t* consumed) {
        Reader r(data, encoding);
        const Tag t = static_cast<Tag>(r.U8());
        if (!r.ok || t != nbt::Compound)
            return false;
        std::string name = r.Str();
        if (rootName)
            *rootName = std::move(name);
        root = Value{};
        const bool ok = r.Payload(nbt::Compound, root, 0) && r.ok;
        if (consumed)
            *consumed = r.at;
        return ok;
    }

    void Write(const Value& root, Encoding encoding, std::vector<uint8_t>& out, std::string_view rootName) {
        Out o(out, encoding);
        o.U8(nbt::Compound);
        o.Str(rootName);
        Value copy;
        const Value& r = root.type == nbt::Compound ? root : (copy = Value::Make(nbt::Compound));
        o.Payload(r);
    }
}
