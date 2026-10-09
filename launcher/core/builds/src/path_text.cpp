#include "path_text.hpp"

namespace rb::builds {

std::string utf8_name(const NativePath& path) {
    const std::u8string text = path.generic_u8string();
    return {text.begin(), text.end()};
}

std::string folded_key(const NativePath& path) {
    std::string key = utf8_name(path);
    for (char& c : key)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return key;
}

}  // namespace rb::builds
