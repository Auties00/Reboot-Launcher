#include "reboot/builds/file_finder.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#include "builds_error.hpp"
#include "path_text.hpp"
#include "reboot/foundation/text.hpp"

namespace rb::builds {

namespace {

namespace fs = std::filesystem;

struct Child {
    NativePath name;
    fs::file_type type = fs::file_type::none;
};

[[nodiscard]] Diagnostic io_error(const NativePath& path, const std::error_code& error) {
    return to_diagnostic(
        BuildsError{.code = BuildsErrorCode::Io, .path = path, .os_error = SystemError{.code = error.value()}});
}

// The directory's entries sorted by name, links reported as links.
Result<std::vector<Child>> list_dir(const NativePath& dir) {
    std::error_code error;
    fs::directory_iterator it(dir, error);
    if (error) return std::unexpected(io_error(dir, error));
    std::vector<Child> children;
    for (; it != fs::directory_iterator(); it.increment(error)) {
        if (error) break;
        std::error_code status_error;
        const fs::file_status status = it->symlink_status(status_error);
        children.push_back(
            Child{.name = it->path().filename(), .type = status_error ? fs::file_type::unknown : status.type()});
    }
    if (error) return std::unexpected(io_error(dir, error));
    std::ranges::sort(children, {}, &Child::name);
    return children;
}

struct Walk {
    const NativePath& root;
    std::span<const std::string_view> names;
    const CancelToken& token;
    u32 max_depth;
    FindResult result;
    // The root itself must be listable; deeper failures only become WalkErrors.
    std::optional<Diagnostic> root_error;
};

// False when cancelled.
bool visit(Walk& walk, const NativePath& relative, u32 depth) {
    if (walk.token.cancelled()) return false;
    const NativePath dir = relative.empty() ? walk.root : walk.root / relative;
    Result<std::vector<Child>> children = list_dir(dir);
    if (!children && depth == 0) {
        walk.root_error = std::move(children.error());
        return true;
    }
    if (!children) {
        walk.result.errors.push_back(
            WalkError{.path = dir, .os_error = children.error().os_error.value_or(SystemError{})});
        return true;
    }
    for (const Child& child : *children) {
        const NativePath child_relative = relative / child.name;
        if (child.type == fs::file_type::directory) {
            if (depth + 1 > walk.max_depth) {
                walk.result.depth_capped = true;
                continue;
            }
            if (!visit(walk, child_relative, depth + 1)) return false;
        } else if (child.type == fs::file_type::regular) {
            const std::string name = utf8_name(child.name);
            for (std::size_t i = 0; i < walk.names.size(); ++i)
                if (iequals_ascii(name, walk.names[i]))
                    walk.result.files.push_back(FoundFile{.name_index = i, .relative = child_relative});
        }
    }
    return true;
}

}  // namespace

Result<FindResult> FileFinder::find(const NativePath& root, std::span<const std::string_view> names,
                                    const CancelToken& token) const {
    std::error_code error;
    const fs::file_status status = fs::status(root, error);
    if (error) return std::unexpected(io_error(root, error));
    if (status.type() != fs::file_type::directory)
        return std::unexpected(to_diagnostic(BuildsError{.code = BuildsErrorCode::NotADirectory, .path = root}));
    Walk walk{.root = root,
              .names = names,
              .token = token,
              .max_depth = options_.max_depth,
              .result = {},
              .root_error = {}};
    if (!visit(walk, NativePath{}, 0))
        return std::unexpected(to_diagnostic(BuildsError{.code = BuildsErrorCode::Cancelled}));
    if (walk.root_error) return std::unexpected(std::move(*walk.root_error));
    return std::move(walk.result);
}

}  // namespace rb::builds
