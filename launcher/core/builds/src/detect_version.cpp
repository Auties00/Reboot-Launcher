#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "builds_error.hpp"
#include "messages.hpp"
#include "reboot/builds/detect_version.hpp"
#include "reboot/builds/file_byte_source.hpp"
#include "reboot/builds/pe_version_reader.hpp"

namespace rb::builds {

namespace {

enum class Read : u8 { Resource, Scan };

struct Attempt {
    const BuildLayout& layout;
    const PeVersionReader& reader;
    const DetectionTables& tables;
    const CancelToken& token;
    NeedsUserVersion& needs;
};

// Why a marker settled nothing: a Cert/Next build whose changelist the table lacks, or any other shape.
[[nodiscard]] Diagnostic unsettled(const ReleaseMarker& marker, const NativePath& path) {
    const bool cl_named = marker.tail == "Cert" || marker.tail == "Next";
    if (cl_named && marker.engine_cl)
        return make_diag(ErrorDomain::Builds, msg::kUnknownChangelist)
            .arg("cl", marker.engine_cl->value)
            .severity(Severity::Warning)
            .build();
    return make_diag(ErrorDomain::Builds, msg::kUnknownReleaseShape)
        .arg("raw", marker.tail)
        .arg("path", path)
        .severity(Severity::Warning)
        .build();
}

// The version `file` settles, or nullopt with the reason recorded; fails only when cancelled.
Result<std::optional<DetectedVersion>> try_file(Attempt& attempt, const NativePath& file, Read read) {
    if (attempt.token.cancelled())
        return std::unexpected(to_diagnostic(BuildsError{.code = BuildsErrorCode::Cancelled}));
    const NativePath path = attempt.layout.root / file;
    const auto unreadable = [&](Diagnostic cause) {
        Diagnostic diag = make_diag(ErrorDomain::Builds, msg::kVersionFileUnreadable)
                              .arg("path", path)
                              .severity(Severity::Warning)
                              .cause(std::move(cause))
                              .build();
        attempt.needs.reasons.push_back(std::move(diag));
        return std::optional<DetectedVersion>();
    };

    Result<FileByteSource> source = FileByteSource::open(path);
    if (!source) return unreadable(std::move(source.error()));
    Result<std::optional<ReleaseMarker>> marker = read == Read::Resource
                                                      ? attempt.reader.read_marker(*source)
                                                      : attempt.reader.scan_for_marker(*source, attempt.token);
    if (!marker) {
        if (marker.error().kind == ErrorKind::Cancelled) return std::unexpected(std::move(marker.error()));
        return unreadable(std::move(marker.error()));
    }
    if (!*marker) {
        attempt.needs.reasons.push_back(
            make_diag(ErrorDomain::Builds, msg::kNoReleaseMarker).arg("path", path).severity(Severity::Warning));
        return std::optional<DetectedVersion>();
    }

    const VersionSource source_kind = read == Read::Resource ? VersionSource::PeResource : VersionSource::RawScan;
    std::optional<DetectedVersion> detected = derive_version(**marker, source_kind, attempt.tables);
    if (!detected) {
        if (!attempt.needs.raw) attempt.needs.raw = (*marker)->tail;
        attempt.needs.reasons.push_back(unsettled(**marker, path));
        return std::optional<DetectedVersion>();
    }
    detected->file = file;
    return detected;
}

}  // namespace

Result<VersionDetection> detect_version(const BuildLayout& layout, const PeVersionReader& reader,
                                        const DetectionTables& tables, const CancelToken& token) {
    NeedsUserVersion needs;
    Attempt attempt{.layout = layout, .reader = reader, .tables = tables, .token = token, .needs = needs};

    std::vector<std::pair<NativePath, Read>> order;
    for (const NativePath& crash_report_client : layout.crash_report_clients)
        order.emplace_back(crash_report_client, Read::Resource);
    order.emplace_back(layout.shipping_exe, Read::Resource);
    order.emplace_back(layout.shipping_exe, Read::Scan);

    for (const auto& [file, read] : order) {
        Result<std::optional<DetectedVersion>> detected = try_file(attempt, file, read);
        if (!detected) return std::unexpected(std::move(detected.error()));
        if (*detected) return VersionDetection(std::move(**detected));
    }
    return VersionDetection(std::move(needs));
}

}  // namespace rb::builds
