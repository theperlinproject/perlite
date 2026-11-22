#include "network/session/SessionInternal.hpp"

namespace perlite {
    void Session::SendTitle(int type, const std::string& text, int fadeIn, int stay, int fadeOut) {
        proto::SetTitlePacket p;
        static const proto::SetTitlePacketType kinds[] = {proto::SetTitlePacketType::Clear,
                                                          proto::SetTitlePacketType::Reset,
                                                          proto::SetTitlePacketType::SetTitle,
                                                          proto::SetTitlePacketType::SetSubtitle,
                                                          proto::SetTitlePacketType::ActionBarMessage,
                                                          proto::SetTitlePacketType::SetDurations};
        p.type = kinds[std::clamp(type, 0, 5)];
        p.text = text;
        p.fadeInTime = fadeIn;
        p.stayTime = stay;
        p.fadeOutTime = fadeOut;
        p.xuid = xuid;
        Send(p);
    }

    void Session::SendActionBar(const std::string& text) {
        SendText(static_cast<uint8_t>(proto::TextPacketType::JukeboxPopup), {}, text);
    }

    void Session::SendRaw(const std::string& message) {
        size_t from = 0;
        while (from <= message.size()) {
            const size_t nl = message.find('\n', from);
            const std::string line =
                message.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
            SendText(0, {}, line);
            if (nl == std::string::npos)
                break;
            from = nl + 1;
        }
    }

    void Session::OnText(std::span<const uint8_t> body) {
        proto::TextPacket t;
        if (!Decode(body, t) || state != State::InGame)
            return;
        if (t.type != proto::TextPacketType::Chat || t.message.empty())
            return;
        std::string message = t.message;
        if (message.size() > 512)
            message.resize(512);
        lastActive = std::chrono::steady_clock::now();
        server.HandleChat(*this, message);
    }

    void Session::OnCommand(std::span<const uint8_t> body) {
        proto::CommandRequestPacket c;
        if (!Decode(body, c) || state != State::InGame)
            return;
        lastActive = std::chrono::steady_clock::now();
        server.RunPlayerCommand(*this, c.command);
    }

    void Session::SendText(uint8_t type, const std::string& source, const std::string& message) {
        proto::TextPacket t;
        t.needsTranslation = false;
        // The body variant must agree with the message type (MessageOnly / AuthorAndMessage / MessageAndParams).
        switch (static_cast<proto::TextPacketType>(type)) {
        case proto::TextPacketType::Chat:
        case proto::TextPacketType::Whisper:
        case proto::TextPacketType::Announcement:
            t.category = proto::TextPacketCategory::Authored;
            break;
        case proto::TextPacketType::Translation:
        case proto::TextPacketType::Popup:
        case proto::TextPacketType::JukeboxPopup:
            t.category = proto::TextPacketCategory::Parameters;
            break;
        default:
            t.category = proto::TextPacketCategory::MessageOnly;
            break;
        }
        t.type = static_cast<proto::TextPacketType>(type);
        t.sourceName = source;
        t.message = message;
        Send(t);
    }

    void Session::Transfer(const std::string& host, uint16_t port) {
        proto::TransferPacket p;
        p.serverAddress = host;
        p.port = port;
        p.reloadWorld = true;
        Send(p);
        Flush();
    }

    uint32_t Session::ShowForm(const std::string& json) {
        const uint32_t id = nextFormId++;
        if (openForms.size() > 64)
            openForms.clear();
        openForms.insert(id);
        proto::ModalFormRequestPacket p;
        p.formId = id;
        p.data = json;
        Send(p);
        return id;
    }

    void Session::OnFormResponse(std::span<const uint8_t> body) {
        proto::ModalFormResponsePacket p;
        if (!Decode(body, p) || state != State::InGame || !openForms.erase(p.formId))
            return;
        server.HookFormResponse(*this, p.formId, p.data ? *p.data : std::string());
    }
}
