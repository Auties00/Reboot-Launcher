#pragma once

#ifndef QUIC_API_ENABLE_PREVIEW_FEATURES
#define QUIC_API_ENABLE_PREVIEW_FEATURES 1
#endif
#include <msquic.h>

#include <cstddef>
#include <stdexcept>
#include <string>

#include "core/types.hpp"
#include "registry/model.hpp"

namespace sb::quic {

// Pre-encoded frames are handed to MsQuic without copying through this layout match.
static_assert(sizeof(registry::BufferRef) == sizeof(QUIC_BUFFER));
static_assert(offsetof(registry::BufferRef, length) == offsetof(QUIC_BUFFER, Length));
static_assert(offsetof(registry::BufferRef, data) == offsetof(QUIC_BUFFER, Buffer));

[[nodiscard]] inline const QUIC_BUFFER* as_quic(const registry::BufferRef* b) noexcept {
    return reinterpret_cast<const QUIC_BUFFER*>(b);
}

class Error : public std::runtime_error {
public:
    Error(const char* what, QUIC_STATUS st) : std::runtime_error(std::string(what) + ": status 0x" + hex(st)), status(st) {}
    QUIC_STATUS status;

private:
    static std::string hex(QUIC_STATUS st) {
        static constexpr char d[] = "0123456789abcdef";
        std::string s;
        for (int i = 28; i >= 0; i -= 4) s.push_back(d[(static_cast<unsigned>(st) >> i) & 0xF]);
        return s;
    }
};

inline void check(QUIC_STATUS st, const char* what) {
    if (QUIC_FAILED(st)) throw Error(what, st);
}

// Process-wide MsQuic API table.
class Library {
public:
    Library() { check(MsQuicOpen2(&api_), "MsQuicOpen2"); }
    ~Library() {
        if (api_) MsQuicClose(api_);
    }
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    [[nodiscard]] const QUIC_API_TABLE* operator->() const noexcept { return api_; }
    [[nodiscard]] const QUIC_API_TABLE* get() const noexcept { return api_; }

private:
    const QUIC_API_TABLE* api_ = nullptr;
};

[[nodiscard]] inline IpAddr ip_of(const QUIC_ADDR& a) noexcept {
    if (QuicAddrGetFamily(&a) == QUIC_ADDRESS_FAMILY_INET) {
        const u32 v = ntohl(a.Ipv4.sin_addr.s_addr);
        return IpAddr::v4(v);
    }
    IpAddr r;
    std::memcpy(r.bytes.data(), &a.Ipv6.sin6_addr, 16);
    return r;
}

[[nodiscard]] inline QUIC_ADDR quic_addr(const IpAddr& ip, u16 port) noexcept {
    QUIC_ADDR a{};
    if (ip.is_v4()) {
        QuicAddrSetFamily(&a, QUIC_ADDRESS_FAMILY_INET);
        std::memcpy(&a.Ipv4.sin_addr, ip.bytes.data() + 12, 4);
    } else {
        QuicAddrSetFamily(&a, QUIC_ADDRESS_FAMILY_INET6);
        std::memcpy(&a.Ipv6.sin6_addr, ip.bytes.data(), 16);
    }
    QuicAddrSetPort(&a, port);
    return a;
}

}  // namespace sb::quic
