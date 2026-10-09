#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/winhost/control_connection.hpp"

#include <algorithm>
#include <climits>
#include <vector>

#include "reboot/foundation/version.hpp"

namespace rb::os_windows::winhost {
namespace {

enum class ReadStatus : u8 { Ok, Eof, Error };

struct ReadResult {
    ReadStatus status = ReadStatus::Ok;
    // Bytes read before EOF, so a frame cut off midway is told apart from a clean end.
    std::size_t got = 0;
    int error = 0;
};

ReadResult read_exact(SOCKET socket, u8* out, std::size_t size) {
    std::size_t got = 0;
    while (got < size) {
        const int want = static_cast<int>(std::min<std::size_t>(size - got, INT_MAX));
        const int n = recv(socket, reinterpret_cast<char*>(out + got), want, 0);
        if (n == 0) return {ReadStatus::Eof, got, 0};
        if (n == SOCKET_ERROR) return {ReadStatus::Error, got, WSAGetLastError()};
        got += static_cast<std::size_t>(n);
    }
    return {ReadStatus::Ok, got, 0};
}

}  // namespace

ControlConnection::ControlConnection(std::uintptr_t socket) noexcept : socket_(socket) {}

ControlConnection::~ControlConnection() {
    closesocket(static_cast<SOCKET>(socket_));
    WSACleanup();
}

Expected<std::unique_ptr<ControlConnection>> ControlConnection::connect(u16 port) {
    WSADATA data{};
    if (const int started = WSAStartup(MAKEWORD(2, 2), &data); started != 0)
        return std::unexpected(WinhostFailure{FailureStep::Connect, started});
    const auto fail = [](SOCKET socket) {
        const int code = WSAGetLastError();
        if (socket != INVALID_SOCKET) closesocket(socket);
        WSACleanup();
        return std::unexpected(WinhostFailure{FailureStep::Connect, code});
    };

    // Not inheritable: the game and its companions must never hold the engine's channel open.
    const SOCKET socket =
        WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (socket == INVALID_SOCKET) return fail(socket);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
        return fail(socket);
    const BOOL no_delay = TRUE;
    if (setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&no_delay), sizeof(no_delay)) ==
        SOCKET_ERROR)
        return fail(socket);

    std::unique_ptr<ControlConnection> connection(new ControlConnection(static_cast<std::uintptr_t>(socket)));
    const auto preamble = game_control_preamble(VersionStreams::payload_abi);
    if (auto sent = connection->send(std::span<const u8>{preamble}); !sent)
        return std::unexpected(WinhostFailure{FailureStep::Connect, sent.error().os_code});
    return connection;
}

Expected<void> ControlConnection::send(std::span<const u8> frame) {
    const std::scoped_lock lock(send_mutex_);
    const auto socket = static_cast<SOCKET>(socket_);
    while (!frame.empty()) {
        const int want = static_cast<int>(std::min<std::size_t>(frame.size(), INT_MAX));
        const int n = ::send(socket, reinterpret_cast<const char*>(frame.data()), want, 0);
        if (n == SOCKET_ERROR) return std::unexpected(WinhostFailure{FailureStep::Relay, WSAGetLastError()});
        frame = frame.subspan(static_cast<std::size_t>(n));
    }
    return {};
}

Expected<std::optional<ControlFrame>> ControlConnection::next_frame() {
    const auto socket = static_cast<SOCKET>(socket_);
    const auto protocol = [] { return std::unexpected(WinhostFailure{FailureStep::Protocol, std::nullopt}); };
    // After shutdown() a pending or later recv fails, cancelled or refused; that is our own EOF.
    const auto failed = [&](const ReadResult& read) -> Expected<std::optional<ControlFrame>> {
        if (shut_.load(std::memory_order_acquire)) return std::nullopt;
        return std::unexpected(WinhostFailure{FailureStep::Relay, read.error});
    };

    // Two QUIC varints: the first byte's top two bits give the length of each.
    std::array<u64, 2> header{};
    for (std::size_t field = 0; field < header.size(); ++field) {
        std::array<u8, 8> raw{};
        ReadResult read = read_exact(socket, raw.data(), 1);
        if (read.status == ReadStatus::Eof) {
            if (field == 0) return std::nullopt;
            return protocol();
        }
        if (read.status == ReadStatus::Error) return failed(read);
        const std::size_t width = std::size_t{1} << (raw[0] >> 6);
        read = read_exact(socket, raw.data() + 1, width - 1);
        if (read.status == ReadStatus::Eof) return protocol();
        if (read.status == ReadStatus::Error) return failed(read);
        u64 value = raw[0] & 0x3Fu;
        for (std::size_t i = 1; i < width; ++i) value = (value << 8) | raw[i];
        header[field] = value;
    }
    if (header[1] > kGameControlFrameCap) return protocol();

    std::vector<u8> payload(static_cast<std::size_t>(header[1]));
    const ReadResult read = read_exact(socket, payload.data(), payload.size());
    if (read.status != ReadStatus::Ok) {
        secure_wipe(payload.data(), payload.size());
        if (read.status == ReadStatus::Eof) return protocol();
        return failed(read);
    }
    return std::optional<ControlFrame>{ControlFrame{header[0], SecretBytes{std::move(payload)}}};
}

void ControlConnection::shutdown() noexcept {
    shut_.store(true, std::memory_order_release);
    const auto socket = static_cast<SOCKET>(socket_);
    ::shutdown(socket, SD_BOTH);
    // shutdown() alone does not wake a recv that is already blocked on Windows.
    CancelIoEx(reinterpret_cast<HANDLE>(socket), nullptr);
}

}  // namespace rb::os_windows::winhost
