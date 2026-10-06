#include <catch2/catch_test_macros.hpp>

#include <random>
#include <set>
#include <vector>

#include "core/counted_btree.hpp"

namespace {
struct Less {
    bool operator()(int a, int b) const { return a < b; }
};
}  // namespace

TEST_CASE("counted B+tree matches std::set under random operations", "[btree]") {
    for (int seed = 0; seed < 24; ++seed) {
        std::mt19937 rng(static_cast<unsigned>(seed));
        sb::CountedBTree<int, Less, 8, 6> t;  // tiny nodes force deep trees and every rebalance path
        std::set<int> ref;
        const int range = seed < 8 ? 50 : (seed < 16 ? 2000 : 100000);
        for (int step = 0; step < 20000; ++step) {
            const int k = static_cast<int>(rng() % static_cast<unsigned>(range));
            if (rng() % 10 < 6) REQUIRE(t.insert(k) == ref.insert(k).second);
            else REQUIRE(t.erase(k) == (ref.erase(k) > 0));
            if (step % 997 == 0) {
                REQUIRE(t.validate());
                std::size_t r = 0;
                for (int v : ref) {
                    REQUIRE(t.rank(v) == r);
                    REQUIRE(t.select(r) == v);
                    ++r;
                }
                std::vector<int> fw;
                for (auto it = t.begin(); it != t.end(); ++it) fw.push_back(*it);
                REQUIRE(fw == std::vector<int>(ref.begin(), ref.end()));
                const int q = static_cast<int>(rng() % static_cast<unsigned>(range));
                auto lb = t.lower_bound(q);
                auto rl = ref.lower_bound(q);
                REQUIRE(lb.valid() == (rl != ref.end()));
                if (lb.valid()) REQUIRE(*lb == *rl);
                if (!ref.empty()) {
                    auto it = t.iterator_at(ref.size() - 1);
                    std::size_t c = 0;
                    while (it.valid()) {
                        --it;
                        ++c;
                    }
                    REQUIRE(c == ref.size());
                }
            }
        }
        while (!ref.empty()) {
            REQUIRE(t.erase(*ref.begin()));
            ref.erase(ref.begin());
        }
        REQUIRE(t.validate());
        REQUIRE(t.size() == 0);
    }
}
