#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace perlite::nbt {

    enum Tag : uint8_t {
        End = 0,
        Byte = 1,
        Short = 2,
        Int = 3,
        Long = 4,
        Float = 5,
        Double = 6,
        ByteArray = 7,
        String = 8,
        List = 9,
        Compound = 10,
        IntArray = 11,
        LongArray = 12
    };

    enum class Encoding { Network, LittleEndian };

    class Writer {
    public:
        explicit Writer(Encoding encoding = Encoding::Network) : encoding(encoding) {}

        void BeginRoot(std::string_view name = {}) {
            out.push_back(Compound);
            Name(name);
        }
        void EndCompound() { out.push_back(End); }

        void BeginCompound(std::string_view name) {
            out.push_back(Compound);
            Name(name);
        }
        void BeginList(std::string_view name, Tag element, int32_t count) {
            out.push_back(List);
            Name(name);
            out.push_back(element);
            Int32(count);
        }

        void PutByte(std::string_view name, int8_t v) {
            out.push_back(Byte);
            Name(name);
            out.push_back(static_cast<uint8_t>(v));
        }
        void PutShort(std::string_view name, int16_t v) {
            out.push_back(Short);
            Name(name);
            Raw16(static_cast<uint16_t>(v));
        }
        void PutInt(std::string_view name, int32_t v) {
            out.push_back(Int);
            Name(name);
            Int32(v);
        }
        void PutLong(std::string_view name, int64_t v) {
            out.push_back(Long);
            Name(name);
            Int64(v);
        }
        void PutFloat(std::string_view name, float v) {
            out.push_back(Float);
            Name(name);
            uint32_t bits;
            std::memcpy(&bits, &v, 4);
            Raw32(bits);
        }
        void PutDouble(std::string_view name, double v) {
            out.push_back(Double);
            Name(name);
            uint64_t bits;
            std::memcpy(&bits, &v, 8);
            Raw64(bits);
        }
        void PutString(std::string_view name, std::string_view v) {
            out.push_back(String);
            Name(name);
            Str(v);
        }

        void ElementInt(int32_t v) { Int32(v); }
        void ElementString(std::string_view v) { Str(v); }
        void ElementFloat(float v) {
            uint32_t bits;
            std::memcpy(&bits, &v, 4);
            Raw32(bits);
        }

        std::vector<uint8_t>& Bytes() { return out; }

    private:
        void Name(std::string_view name) { Str(name); }

        void Str(std::string_view s) {
            if (encoding == Encoding::Network)
                VarU32(static_cast<uint32_t>(s.size()));
            else
                Raw16(static_cast<uint16_t>(s.size()));
            out.insert(out.end(), s.begin(), s.end());
        }

        void Int32(int32_t v) {
            if (encoding == Encoding::Network)
                VarU32((static_cast<uint32_t>(v) << 1) ^ static_cast<uint32_t>(v >> 31));
            else
                Raw32(static_cast<uint32_t>(v));
        }
        void Int64(int64_t v) {
            if (encoding == Encoding::Network) {
                uint64_t z = (static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63);
                while (z >= 0x80) {
                    out.push_back(static_cast<uint8_t>(z | 0x80));
                    z >>= 7;
                }
                out.push_back(static_cast<uint8_t>(z));
            } else {
                Raw64(static_cast<uint64_t>(v));
            }
        }
        void VarU32(uint32_t v) {
            while (v >= 0x80) {
                out.push_back(static_cast<uint8_t>(v | 0x80));
                v >>= 7;
            }
            out.push_back(static_cast<uint8_t>(v));
        }
        void Raw16(uint16_t v) {
            out.push_back(static_cast<uint8_t>(v));
            out.push_back(static_cast<uint8_t>(v >> 8));
        }
        void Raw32(uint32_t v) {
            for (int i = 0; i < 4; i++)
                out.push_back(static_cast<uint8_t>(v >> (8 * i)));
        }
        void Raw64(uint64_t v) {
            for (int i = 0; i < 8; i++)
                out.push_back(static_cast<uint8_t>(v >> (8 * i)));
        }

        Encoding encoding;
        std::vector<uint8_t> out;
    };
}
