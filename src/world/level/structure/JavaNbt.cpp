#include "world/level/structure/JavaNbt.hpp"

#include <miniz.h>

#include <cstring>

namespace perlite::structure {
    namespace {
        struct BigEndianReader {
            std::span<const uint8_t> data;
            size_t at = 0;
            bool ok = true;

            uint64_t Raw(int n) {
                if (at + static_cast<size_t>(n) > data.size()) {
                    ok = false;
                    return 0;
                }
                uint64_t v = 0;
                for (int i = 0; i < n; i++)
                    v = (v << 8) | data[at++];
                return v;
            }
            std::string Str() {
                const size_t n = static_cast<size_t>(Raw(2));
                if (!ok || at + n > data.size()) {
                    ok = false;
                    return {};
                }
                std::string s(reinterpret_cast<const char*>(data.data() + at), n);
                at += n;
                return s;
            }
            bool Payload(nbt::Tag type, nbt::Value& v, int depth) {
                using namespace nbt;
                if (depth > 64)
                    return ok = false;
                v.type = type;
                switch (type) {
                case Byte:
                    v.integer = static_cast<int8_t>(Raw(1));
                    break;
                case Short:
                    v.integer = static_cast<int16_t>(Raw(2));
                    break;
                case Int:
                    v.integer = static_cast<int32_t>(Raw(4));
                    break;
                case Long:
                    v.integer = static_cast<int64_t>(Raw(8));
                    break;
                case Float: {
                    const uint32_t bits = static_cast<uint32_t>(Raw(4));
                    float f;
                    std::memcpy(&f, &bits, 4);
                    v.real = f;
                    break;
                }
                case Double: {
                    const uint64_t bits = Raw(8);
                    double d;
                    std::memcpy(&d, &bits, 8);
                    v.real = d;
                    break;
                }
                case ByteArray: {
                    const int32_t n = static_cast<int32_t>(Raw(4));
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
                    v.listType = static_cast<Tag>(Raw(1));
                    const int32_t n = static_cast<int32_t>(Raw(4));
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
                        const Tag t = static_cast<Tag>(Raw(1));
                        if (!ok)
                            return false;
                        if (t == End)
                            break;
                        std::string name = Str();
                        Value child;
                        if (!Payload(t, child, depth + 1))
                            return false;
                        v.compound.push_back({std::move(name), std::move(child)});
                    }
                    break;
                case IntArray:
                case LongArray: {
                    const int32_t n = static_cast<int32_t>(Raw(4));
                    if (n < 0 || static_cast<size_t>(n) > data.size())
                        return ok = false;
                    v.numbers.resize(static_cast<size_t>(n));
                    for (int64_t& x : v.numbers)
                        x = type == IntArray ? static_cast<int32_t>(Raw(4)) : static_cast<int64_t>(Raw(8));
                    break;
                }
                default:
                    return ok = false;
                }
                return ok;
            }
        };
    }

    bool Gunzip(std::span<const uint8_t> in, std::string& out) {
        if (in.size() < 18 || in[0] != 0x1f || in[1] != 0x8b || in[2] != 8)
            return false;
        const uint8_t flags = in[3];
        size_t at = 10;
        if (flags & 4) { // extra
            if (at + 2 > in.size())
                return false;
            at += 2 + (in[at] | (in[at + 1] << 8));
        }
        if (flags & 8) // name
            while (at < in.size() && in[at++] != 0) {}
        if (flags & 16) // comment
            while (at < in.size() && in[at++] != 0) {}
        if (flags & 2)
            at += 2;
        if (at >= in.size())
            return false;
        const size_t isize = in[in.size() - 4] | (in[in.size() - 3] << 8) | (in[in.size() - 2] << 16) |
                             (static_cast<size_t>(in[in.size() - 1]) << 24);
        out.resize(isize);
        const size_t got = tinfl_decompress_mem_to_mem(out.data(), out.size(), in.data() + at, in.size() - at - 8, 0);
        if (got == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED || got != isize)
            return false;
        return true;
    }

    bool ReadTemplateNbt(std::span<const uint8_t> file, nbt::Value& root) {
        std::string inflated;
        std::span<const uint8_t> raw = file;
        if (file.size() >= 2 && file[0] == 0x1f && file[1] == 0x8b) {
            if (!Gunzip(file, inflated))
                return false;
            raw = {reinterpret_cast<const uint8_t*>(inflated.data()), inflated.size()};
        }
        BigEndianReader r{raw};
        const nbt::Tag type = static_cast<nbt::Tag>(r.Raw(1));
        if (type != nbt::Compound)
            return false;
        r.Str();
        return r.Payload(type, root, 0);
    }
}
