#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace perlite {

    class PacketCipher {
    public:
        PacketCipher();
        ~PacketCipher();
        PacketCipher(const PacketCipher&) = delete;
        PacketCipher& operator=(const PacketCipher&) = delete;

        bool PrepareServerHandshake(const std::string& clientKeyBase64, std::string& token,
                                    std::string& error);
        bool InitWithKey(const uint8_t key[32]);
        void Activate() { active = ready; }
        bool Active() const { return active; }

        std::vector<uint8_t> Encrypt(std::span<const uint8_t> plain);
        bool Decrypt(std::span<const uint8_t> cipher, std::vector<uint8_t>& plain);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
        bool ready = false;
        bool active = false;
    };
}
