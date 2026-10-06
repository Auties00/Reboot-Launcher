#include <catch2/catch_test_macros.hpp>

#include <map>
#include <random>

#include "edge/dirty_set.hpp"

using namespace sb;
using sb::edge::DirtySet;

namespace {

std::vector<std::pair<u64, u8>> drain(DirtySet& d) {
    std::vector<std::pair<u64, u8>> out;
    d.take_sorted(out);
    return out;
}

}  // namespace

TEST_CASE("dirty set merges masks inline and after spilling", "[dirty]") {
    DirtySet d;
    CHECK(d.empty());
    CHECK(d.mark(1, 10, 1));
    CHECK_FALSE(d.mark(1, 10, 2));
    CHECK(d.size() == 1);
    for (u32 h = 0; h < 20; ++h) d.mark(2, h, 4);  // spills past the inline capacity
    CHECK_FALSE(d.mark(1, 10, 8));
    CHECK(d.size() == 21);
    CHECK(d.count_view(1) == 1);
    CHECK(d.count_view(2) == 20);

    const auto out = drain(d);
    CHECK(d.empty());
    REQUIRE(out.size() == 21);
    CHECK(out.front() == std::pair<u64, u8>{DirtySet::key(1, 10), u8{1 | 2 | 8}});
    CHECK(std::is_sorted(out.begin(), out.end()));

    // Back to inline mode after draining.
    CHECK(d.mark(3, 1, 1));
    CHECK(d.size() == 1);
}

TEST_CASE("dirty set erase and retain work in both modes", "[dirty]") {
    for (u32 n : {2u, 40u}) {  // 4 pairs stay inline, 80 spill
        DirtySet d;
        for (u32 h = 0; h < n; ++h) {
            d.mark(1, h, 1);
            d.mark(2, h, 1);
        }
        d.retain_view(1, [](u32 h) { return h % 2 == 0; });
        CHECK(d.count_view(1) == (n + 1) / 2);
        CHECK(d.count_view(2) == n);
        d.erase_view(2);
        CHECK(d.count_view(2) == 0);
        d.erase_view(1);
        CHECK(d.empty());
        CHECK(d.mark(1, 0, 1));  // usable again
    }
}

TEST_CASE("dirty set matches a reference map under random operations", "[dirty]") {
    std::mt19937 rng(7);
    DirtySet d;
    std::map<u64, u8> ref;
    for (int step = 0; step < 20000; ++step) {
        const u32 view = rng() % 3 + 1;
        const u32 handle = rng() % 12;
        switch (rng() % 10) {
            case 0: {
                const auto out = drain(d);
                CHECK(out == std::vector<std::pair<u64, u8>>(ref.begin(), ref.end()));
                ref.clear();
                break;
            }
            case 1:
                d.erase_view(view);
                std::erase_if(ref, [&](const auto& kv) { return static_cast<u32>(kv.first >> 32) == view; });
                break;
            default: {
                const u8 mask = static_cast<u8>(1u << (rng() % 4));
                const u64 k = DirtySet::key(view, handle);
                const bool fresh = !ref.contains(k);
                CHECK(d.mark(view, handle, mask) == fresh);
                ref[k] |= mask;
            }
        }
        CHECK(d.size() == ref.size());
        CHECK(d.empty() == ref.empty());
    }
}
