#pragma once

#include <string>
#include <vector>

#include <roaring/roaring.hh>

#include "core/flat_map.hpp"
#include "registry/model.hpp"
#include "wire/messages.hpp"

namespace sb::registry {

// Substring search over name, author and id, owned by one search worker thread and kept in
// sync from the ring. Queries of 3+ bytes intersect trigram posting lists and verify the
// candidates; shorter ones scan the visible documents.
class SearchIndex {
public:
    void upsert(const PubEntry& p);
    void remove(u32 handle);

    // Filters by the view's partition, matches `text` case-insensitively, then returns one
    // keyset page in the view's sort order.
    [[nodiscard]] wire::QueryResult query(const wire::ViewSpec& spec, std::string_view text, u32 limit,
                                          const wire::Bytes& cursor, bool& bad_cursor) const;

    [[nodiscard]] std::size_t size() const noexcept { return live_.cardinality(); }

private:
    struct Doc {
        bool live = false;
        std::string text;  // folded "name \x1f author \x1f uuid-hex"
        std::string folded_name;
        wire::ListEntry entry;
    };

    static void trigrams(std::string_view s, std::vector<u32>& out);
    void index_text(u32 handle, const std::string& text, bool add);
    [[nodiscard]] static bool partition_match(const wire::ViewSpec& spec, const Doc& d) noexcept;

    std::vector<Doc> docs_;
    FlatMap<u32, roaring::Roaring> postings_;
    roaring::Roaring live_;  // visible documents
    mutable std::vector<u32> scratch_;
};

}  // namespace sb::registry
