#include "store_layout.hpp"

#include <optional>
#include <string>

namespace rb::secrets::detail {

namespace {

std::optional<SecretKind> kind_from_name(std::string_view name) {
    for (const SecretKind kind : {SecretKind::RemoteBackendPassword, SecretKind::HostJoinPassword, SecretKind::JoinPassword})
        if (kind_name(kind) == name) return kind;
    return std::nullopt;
}

}  // namespace

StoreLayout::StoreLayout(std::string_view root_hash16) : prefix_(std::string(root_hash16) + "/") {}

std::string StoreLayout::index_key(std::size_t chunk) const {
    std::string key = prefix_ + "index";
    if (chunk != 0) key += "." + std::to_string(chunk);
    return key;
}

std::string StoreLayout::value_key(const SecretTarget& target) const {
    std::string key = prefix_;
    key += kind_name(target.kind);
    key += '/';
    key += target.scope.text();
    return key;
}

std::vector<std::vector<u8>> encode_index(const std::set<SecretTarget>& targets) {
    std::vector<std::vector<u8>> chunks;
    for (const SecretTarget& target : targets) {
        const std::string_view kind = kind_name(target.kind);
        const std::string& scope = target.scope.text();
        const std::size_t line = kind.size() + 1 + scope.size() + 1;
        if (chunks.empty() || chunks.back().size() + line > kIndexChunkBytes) chunks.emplace_back();
        std::vector<u8>& out = chunks.back();
        out.insert(out.end(), kind.begin(), kind.end());
        out.push_back('/');
        out.insert(out.end(), scope.begin(), scope.end());
        out.push_back('\n');
    }
    return chunks;
}

std::set<SecretTarget> decode_index(std::span<const u8> bytes) {
    std::set<SecretTarget> out;
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = text.substr(at, end - at);
        at = end + 1;
        const std::size_t slash = line.find('/');
        if (slash == std::string_view::npos) continue;
        const std::optional<SecretKind> kind = kind_from_name(line.substr(0, slash));
        if (!kind || !storable(*kind)) continue;
        if (Result<SecretTarget> target = SecretTarget::parse(*kind, line.substr(slash + 1)))
            out.insert(std::move(*target));
    }
    return out;
}

}  // namespace rb::secrets::detail
