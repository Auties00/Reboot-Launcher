#include <chrono>
#include <string>

#include "messages.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/net/download_error.hpp"
#include "reboot/net/http_error.hpp"
#include "reboot/net/http_response.hpp"
#include "reboot/net/port_conflict.hpp"
#include "reboot/net/resolve_error.hpp"

namespace rb::net {

namespace {

[[nodiscard]] DiagBuilder net_diag(MessageId message) { return make_diag(ErrorDomain::Net, message); }

[[nodiscard]] std::chrono::milliseconds limit_of(const HttpError& error) {
    return error.limit.value_or(std::chrono::milliseconds{0});
}

[[nodiscard]] DiagBuilder http_builder(const HttpError& error) {
    switch (error.code) {
        case HttpErrorCode::InvalidUrl:
            return net_diag(kInvalidUrl).arg("url", error.host).kind(ErrorKind::InvalidInput);
        case HttpErrorCode::Dns: return net_diag(kDnsFailed).arg("host", error.host).retryable();
        case HttpErrorCode::Connect: return net_diag(kConnectFailed).arg("host", error.host).retryable();
        case HttpErrorCode::ConnectTimeout:
            return net_diag(kConnectTimeout).arg("host", error.host).arg("limit", limit_of(error)).retryable();
        case HttpErrorCode::Tls: return net_diag(kTlsFailed).arg("host", error.host);
        case HttpErrorCode::TotalTimeout:
            return net_diag(kRequestTimeout).arg("host", error.host).arg("limit", limit_of(error)).retryable();
        case HttpErrorCode::Stalled:
            return net_diag(kTransferStalled).arg("host", error.host).arg("limit", limit_of(error)).retryable();
        case HttpErrorCode::ResponseTooLarge:
            return net_diag(kResponseTooLarge).arg("host", error.host).arg("byte_limit", error.byte_limit.value_or(0));
        case HttpErrorCode::PlainHttpNeedsConsent:
            return net_diag(kPlainHttpNeedsConsent).arg("host", error.host).kind(ErrorKind::Conflict);
        case HttpErrorCode::DowngradeRefused:
            return net_diag(kHttpsDowngradeRefused).arg("host", error.host).kind(ErrorKind::Conflict);
        case HttpErrorCode::Transport: return net_diag(kTransportFailed).arg("host", error.host).retryable();
        case HttpErrorCode::Cancelled:
            return net_diag(kRequestCancelled).arg("host", error.host).kind(ErrorKind::Cancelled);
    }
    return net_diag(kTransportFailed).arg("host", error.host);
}

[[nodiscard]] DiagBuilder download_builder(const DownloadError& error) {
    switch (error.code) {
        case DownloadErrorCode::InsufficientSpace:
            return net_diag(kInsufficientSpace)
                .arg("file", error.file)
                .arg("needed", error.needed_bytes.value_or(0))
                .arg("available", error.free_bytes.value_or(0))
                .kind(ErrorKind::Conflict);
        case DownloadErrorCode::RangeNotHonored:
            return net_diag(kRangeNotHonored).arg("host", error.host).arg("offset", error.offset).retryable();
        case DownloadErrorCode::SourceChanged: return net_diag(kDownloadSourceChanged).arg("host", error.host).retryable();
        case DownloadErrorCode::SizeMismatch:
            return net_diag(kDownloadSizeMismatch)
                .arg("host", error.host)
                .arg("received", error.offset)
                .arg("expected", error.expected_bytes.value_or(0))
                .retryable();
        case DownloadErrorCode::HttpStatus:
            return net_diag(kDownloadHttpStatus).arg("host", error.host).arg("status", error.status.value_or(0));
        case DownloadErrorCode::Io: return net_diag(kDownloadWriteFailed).arg("file", error.file);
        case DownloadErrorCode::AttemptsExhausted:
            return net_diag(kDownloadAttemptsExhausted).arg("host", error.host).arg("attempts", error.attempts).retryable();
        case DownloadErrorCode::Cancelled:
            return net_diag(kDownloadCancelled).arg("host", error.host).kind(ErrorKind::Cancelled);
    }
    return net_diag(kDownloadWriteFailed).arg("file", error.file);
}

[[nodiscard]] DiagBuilder resolve_builder(const ResolveError& error) {
    switch (error.code) {
        case ResolveErrorCode::InvalidAddress:
            return net_diag(kAddressInvalid).arg("address", error.address).kind(ErrorKind::InvalidInput);
        case ResolveErrorCode::NotFound:
            return net_diag(kHostNotFound).arg("address", error.address).kind(ErrorKind::NotFound).retryable();
        case ResolveErrorCode::NoIpv4Address:
            return net_diag(kNoIpv4Address).arg("address", error.address).kind(ErrorKind::NotFound);
        case ResolveErrorCode::Timeout:
            return net_diag(kResolveTimeout)
                .arg("address", error.address)
                .arg("limit", default_deadline(OpKind::Dns))
                .retryable();
        case ResolveErrorCode::Failed: return net_diag(kResolveFailed).arg("address", error.address).retryable();
        case ResolveErrorCode::Cancelled:
            return net_diag(kResolveCancelled).arg("address", error.address).kind(ErrorKind::Cancelled);
    }
    return net_diag(kResolveFailed).arg("address", error.address);
}

[[nodiscard]] std::string owner_name(const PortOwnerInfo& owner) {
    if (owner.owner.exe && !owner.owner.exe->filename().empty()) return display_utf8(owner.owner.exe->filename());
    return std::to_string(owner.owner.pid);
}

}  // namespace

const std::string* find_header(std::span<const ports::HttpHeader> headers, std::string_view name) {
    for (const ports::HttpHeader& header : headers)
        if (iequals_ascii(header.name, name)) return &header.value;
    return nullptr;
}

Diagnostic to_diagnostic(const HttpError& error) {
    DiagBuilder builder = http_builder(error);
    if (error.detail) std::move(builder).detail(normalize_detail(*error.detail));
    if (error.os_error) std::move(builder).os(*error.os_error);
    return std::move(builder).build();
}

Diagnostic to_diagnostic(const DownloadError& error) {
    DiagBuilder builder = download_builder(error);
    if (error.cause) std::move(builder).cause(*error.cause);
    return std::move(builder).build();
}

Diagnostic to_diagnostic(const ResolveError& error) {
    DiagBuilder builder = resolve_builder(error);
    if (error.parse_error) std::move(builder).arg("parse_error", *error.parse_error);
    if (error.cause) std::move(builder).cause(*error.cause);
    return std::move(builder).build();
}

Diagnostic to_diagnostic(const PortConflict& conflict) {
    const std::string protocol(protocol_name(conflict.protocol));
    const u16 port = conflict.bind.port.value;
    DiagBuilder builder = [&] {
        if (conflict.kind == PortConflictKind::AccessDenied)
            return net_diag(kPortAccessDenied).arg("protocol", protocol).arg("port", port);
        for (const PortOwnerInfo& owner : conflict.owners)
            if (owner.owner_class == PortOwnerClass::System)
                return net_diag(kPortHeldBySystem).arg("protocol", protocol).arg("port", port);
        // An owner without a pid is one the OS would not name to us.
        if (conflict.owners.empty() || conflict.owners.front().owner.pid == 0)
            return net_diag(kPortOwnerUnknown).arg("protocol", protocol).arg("port", port);
        return net_diag(kPortBusy)
            .arg("protocol", protocol)
            .arg("port", port)
            .arg("owner", owner_name(conflict.owners.front()))
            .arg("owned_by_us", conflict.owned_by_us());
    }();
    std::move(builder).kind(ErrorKind::Conflict);
    if (conflict.lookup_error) std::move(builder).cause(*conflict.lookup_error);
    for (const PortOwnerInfo& owner : conflict.owners)
        if (owner.lookup_error) std::move(builder).cause(*owner.lookup_error);
    return std::move(builder).build();
}

}  // namespace rb::net
