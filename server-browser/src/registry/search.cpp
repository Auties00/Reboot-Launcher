#include "registry/search.hpp"

#include <algorithm>

#include "registry/validation.hpp"

namespace sb::registry {

namespace {

struct Match {
    u64 primary;
    u32 handle;
    const std::string* folded_name;
};

bool match_less(const Match& a, const Match& b, bool by_name) {
    if (a.primary != b.primary) return a.primary < b.primary;
    if (by_name && a.handle != b.handle) {
        const int c = a.folded_name->compare(*b.folded_name);
        if (c != 0) return c < 0;
    }
    return a.handle < b.handle;
}

u64 primary_of(wire::Sort sort, const wire::ListEntry& e, const std::string& folded) {
    switch (sort) {
        case wire::Sort::players: return players_primary(e.players, e.created_ms);
        case wire::Sort::newest: return newest_primary(e.created_ms);
        case wire::Sort::name: return name_primary(folded);
    }
    return 0;
}

// Same cursor layout as the replica's keyset cursor.
wire::Bytes encode_cursor(wire::Sort sort, const Match& m) {
    wire::Bytes b;
    b.push_back(1);
    b.push_back(static_cast<u8>(sort));
    for (int i = 0; i < 8; ++i) b.push_back(static_cast<u8>(m.primary >> (8 * i)));
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<u8>(m.handle >> (8 * i)));
    if (sort == wire::Sort::name) b.insert(b.end(), m.folded_name->begin(), m.folded_name->end());
    return b;
}

bool decode_cursor(const wire::Bytes& b, wire::Sort sort, Match& out, std::string& name) {
    if (b.size() < 14 || b[0] != 1 || b[1] != static_cast<u8>(sort)) return false;
    out.primary = 0;
    for (int i = 0; i < 8; ++i) out.primary |= u64{b[2 + i]} << (8 * i);
    out.handle = 0;
    for (int i = 0; i < 4; ++i) out.handle |= u32{b[10 + i]} << (8 * i);
    name.assign(reinterpret_cast<const char*>(b.data() + 14), b.size() - 14);
    if (sort != wire::Sort::name && !name.empty()) return false;
    out.folded_name = &name;
    return true;
}

}  // namespace

void SearchIndex::trigrams(std::string_view s, std::vector<u32>& out) {
    out.clear();
    for (std::size_t i = 0; i + 3 <= s.size(); ++i)
        out.push_back((u32{static_cast<u8>(s[i])} << 16) | (u32{static_cast<u8>(s[i + 1])} << 8) | static_cast<u8>(s[i + 2]));
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

void SearchIndex::index_text(u32 handle, const std::string& text, bool add) {
    trigrams(text, scratch_);
    for (u32 t : scratch_) {
        if (add) {
            postings_[t].add(handle);
        } else if (auto it = postings_.find(t); it != postings_.end()) {
            it->second.remove(handle);
            if (it->second.isEmpty()) postings_.erase(it);
        }
    }
}

void SearchIndex::upsert(const PubEntry& p) {
    if (p.handle >= docs_.size()) docs_.resize(p.handle + 1);
    Doc& d = docs_[p.handle];
    std::string text = fold(p.name);
    text.push_back('\x1f');
    text += fold(p.author);
    text.push_back('\x1f');
    text += p.id.to_string();
    if (d.live && d.text != text) index_text(p.handle, d.text, false);
    if (!d.live || d.text != text) {
        d.text = std::move(text);
        index_text(p.handle, d.text, true);
    }
    d.live = true;
    d.folded_name = fold(p.name);
    p.to_list_entry(d.entry);
    if (visible_flags(p.flags)) live_.add(p.handle);
    else live_.remove(p.handle);
}

void SearchIndex::remove(u32 handle) {
    if (handle >= docs_.size() || !docs_[handle].live) return;
    Doc& d = docs_[handle];
    index_text(handle, d.text, false);
    d = Doc{};
    live_.remove(handle);
}

bool SearchIndex::partition_match(const wire::ViewSpec& spec, const Doc& d) noexcept {
    if (spec.bucket != wire::kBucketAll && d.entry.bucket != spec.bucket) return false;
    const bool pwd = d.entry.flags & wire::entry_flag::has_password;
    if (spec.password == wire::PasswordFilter::none && pwd) return false;
    if (spec.password == wire::PasswordFilter::only && !pwd) return false;
    if (spec.region != wire::Region::all && d.entry.region != spec.region) return false;
    return true;
}

wire::QueryResult SearchIndex::query(const wire::ViewSpec& spec, std::string_view text, u32 limit,
                                     const wire::Bytes& cursor, bool& bad_cursor) const {
    bad_cursor = false;
    wire::QueryResult res;
    const std::string q = fold(text);
    const bool by_name = spec.sort == wire::Sort::name;

    Match after{};
    std::string after_name;
    const bool has_cursor = !cursor.empty();
    if (has_cursor && !decode_cursor(cursor, spec.sort, after, after_name)) {
        bad_cursor = true;
        return res;
    }

    std::vector<Match> matches;
    auto consider = [&](u32 h) {
        const Doc& d = docs_[h];
        if (!partition_match(spec, d)) return;
        if (d.text.find(q) == std::string::npos) return;
        ++res.total;
        const Match m{primary_of(spec.sort, d.entry, d.folded_name), h, &d.folded_name};
        if (has_cursor && !match_less(after, m, by_name)) return;
        matches.push_back(m);
    };

    if (q.size() >= 3) {
        std::vector<u32> grams;
        trigrams(q, grams);
        std::vector<const roaring::Roaring*> lists;
        for (u32 t : grams) {
            auto it = postings_.find(t);
            if (it == postings_.end()) return res;
            lists.push_back(&it->second);
        }
        std::sort(lists.begin(), lists.end(), [](auto* a, auto* b) { return a->cardinality() < b->cardinality(); });
        roaring::Roaring cand = *lists.front() & live_;
        for (std::size_t i = 1; i < lists.size() && !cand.isEmpty(); ++i) cand &= *lists[i];
        for (u32 h : cand) consider(h);
    } else {
        for (u32 h : live_) consider(h);
    }

    const std::size_t take = std::min<std::size_t>(limit, matches.size());
    auto cmp = [&](const Match& a, const Match& b) { return match_less(a, b, by_name); };
    std::partial_sort(matches.begin(), matches.begin() + static_cast<std::ptrdiff_t>(take), matches.end(), cmp);
    res.entries.reserve(take);
    for (std::size_t i = 0; i < take; ++i) res.entries.push_back(docs_[matches[i].handle].entry);
    if (matches.size() > take && take > 0) res.next_cursor = encode_cursor(spec.sort, matches[take - 1]);
    return res;
}

}  // namespace sb::registry
