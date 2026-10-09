#pragma once

#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include <array>
#include <climits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::os_windows::winhost::test {

// The engine's end of the game channel: a loopback listener and the one connection it accepts.
// Reads time out, so a test that waits for a frame winhost never sends fails instead of hanging.
class LoopbackEngine {
public:
    struct Frame {
        u64 type = 0;
        std::vector<u8> payload;
    };

    LoopbackEngine() {
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(listener_, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
        listen(listener_, 1);
        int size = sizeof(address);
        getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size);
        port_ = ntohs(address.sin_port);
    }

    ~LoopbackEngine() {
        close_peer();
        closesocket(listener_);
        WSACleanup();
    }

    LoopbackEngine(const LoopbackEngine&) = delete;
    LoopbackEngine& operator=(const LoopbackEngine&) = delete;

    [[nodiscard]] u16 port() const noexcept { return port_; }

    void accept_peer() {
        peer_ = accept(listener_, nullptr, nullptr);
        const DWORD timeout_ms = 20000;
        setsockopt(peer_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
    }

    void close_peer() {
        if (peer_ != INVALID_SOCKET) closesocket(peer_);
        peer_ = INVALID_SOCKET;
    }

    [[nodiscard]] bool read_exact(u8* out, std::size_t size) {
        std::size_t got = 0;
        while (got < size) {
            const int n = recv(peer_, reinterpret_cast<char*>(out + got), static_cast<int>(size - got), 0);
            if (n <= 0) {
                last_error_ = n == 0 ? 0 : WSAGetLastError();
                return false;
            }
            got += static_cast<std::size_t>(n);
        }
        return true;
    }

    [[nodiscard]] std::optional<std::array<u8, kGameControlPreambleSize>> read_preamble() {
        std::array<u8, kGameControlPreambleSize> bytes{};
        if (!read_exact(bytes.data(), bytes.size())) return std::nullopt;
        return bytes;
    }

    // nullopt on EOF, a reset or a timeout.
    [[nodiscard]] std::optional<Frame> read_frame() {
        Frame frame;
        std::array<u64, 2> header{};
        for (u64& value : header) {
            std::array<u8, 8> raw{};
            if (!read_exact(raw.data(), 1)) return std::nullopt;
            const std::size_t width = std::size_t{1} << (raw[0] >> 6);
            if (!read_exact(raw.data() + 1, width - 1)) return std::nullopt;
            value = raw[0] & 0x3Fu;
            for (std::size_t i = 1; i < width; ++i) value = (value << 8) | raw[i];
        }
        frame.type = header[0];
        frame.payload.resize(static_cast<std::size_t>(header[1]));
        if (!read_exact(frame.payload.data(), frame.payload.size())) return std::nullopt;
        return frame;
    }

    // The next frame of type T; frames of other types are kept in `skipped`.
    template <ContractMessage T>
    [[nodiscard]] std::optional<T> expect() {
        while (auto frame = read_frame()) {
            if (frame->type != contract_frame_type_v<T>) {
                skipped.push_back(std::move(*frame));
                continue;
            }
            T message{};
            if (!sb::wire::decode(frame->payload, message)) return std::nullopt;
            return message;
        }
        return std::nullopt;
    }

    // True once winhost has closed its end; every frame before that is kept in `skipped`.
    [[nodiscard]] bool drain_to_eof() {
        while (auto frame = read_frame()) skipped.push_back(std::move(*frame));
        return last_error_ != WSAETIMEDOUT;
    }

    template <ContractMessage T>
    void send(const T& message) {
        send_raw(encode_contract_frame(message));
    }

    void send_raw(std::span<const u8> bytes) {
        while (!bytes.empty()) {
            const int n = ::send(peer_, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), 0);
            if (n <= 0) return;
            bytes = bytes.subspan(static_cast<std::size_t>(n));
        }
    }

    template <ContractMessage T>
    [[nodiscard]] std::vector<T> skipped_of() const {
        std::vector<T> out;
        for (const Frame& frame : skipped) {
            T message{};
            if (frame.type == contract_frame_type_v<T> && sb::wire::decode(frame.payload, message))
                out.push_back(std::move(message));
        }
        return out;
    }

    std::vector<Frame> skipped;

private:
    SOCKET listener_ = INVALID_SOCKET;
    SOCKET peer_ = INVALID_SOCKET;
    u16 port_ = 0;
    int last_error_ = 0;
};

// UTF-16LE bytes of a wide string, without a terminator.
[[nodiscard]] inline std::vector<u8> utf16(std::wstring_view text) {
    std::vector<u8> out;
    for (const wchar_t unit : text) {
        out.push_back(static_cast<u8>(unit & 0xFF));
        out.push_back(static_cast<u8>((unit >> 8) & 0xFF));
    }
    return out;
}

[[nodiscard]] inline std::wstring system_path(std::wstring_view file) {
    std::array<wchar_t, MAX_PATH> dir{};
    const UINT length = GetSystemDirectoryW(dir.data(), static_cast<UINT>(dir.size()));
    return std::wstring(dir.data(), length) + L"\\" + std::wstring(file);
}

// The REBOOT_CTL_TOKEN text of `bytes`: unpadded base64url.
[[nodiscard]] inline std::string base64url(std::span<const u8> bytes) {
    constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    u32 bits = 0;
    int pending = 0;
    for (const u8 byte : bytes) {
        bits = (bits << 8) | byte;
        pending += 8;
        while (pending >= 6) {
            pending -= 6;
            out += kAlphabet[(bits >> pending) & 0x3F];
        }
    }
    if (pending > 0) out += kAlphabet[(bits << (6 - pending)) & 0x3F];
    return out;
}

}  // namespace rb::os_windows::winhost::test
