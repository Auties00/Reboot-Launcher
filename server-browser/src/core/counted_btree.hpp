#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>

#include "core/features.hpp"
#include "core/types.hpp"

namespace sb {

// Order-statistic B+tree over unique keys: insert, erase, rank and select in O(log n),
// with a doubly linked leaf level for ordered scans. Inner nodes keep per-child counts.
// Keys are compared with a possibly stateful strict-weak-order `Cmp`.
template <class K, class Cmp, int LCap = 64, int ICap = 64>
class CountedBTree {
    static_assert(LCap >= 4 && ICap >= 4);
    static constexpr int LMin = LCap / 2;
    static constexpr int IMin = ICap / 2;

    struct Node {
        u16 n = 0;
        bool leaf = false;
    };
    struct Leaf : Node {
        Leaf* prev = nullptr;
        Leaf* next = nullptr;
        K keys[LCap];
        Leaf() { this->leaf = true; }
    };
    struct Inner : Node {
        Node* ch[ICap];
        u32 cnt[ICap];
        K sep[ICap];  // sep[i] <= every key in ch[i] (i >= 1); sep[0] unused
    };

public:
    class Iterator {
    public:
        Iterator() = default;
        const K& operator*() const noexcept { return leaf_->keys[pos_]; }
        const K* operator->() const noexcept { return &leaf_->keys[pos_]; }
        Iterator& operator++() noexcept {
            if (++pos_ >= leaf_->n) {
                leaf_ = leaf_->next;
                pos_ = 0;
            }
            return *this;
        }
        Iterator& operator--() noexcept {
            if (pos_ == 0) {
                leaf_ = leaf_->prev;
                pos_ = leaf_ ? leaf_->n - 1 : 0;
            } else {
                --pos_;
            }
            return *this;
        }
        bool operator==(const Iterator& o) const noexcept { return leaf_ == o.leaf_ && pos_ == o.pos_; }
        [[nodiscard]] bool valid() const noexcept { return leaf_ != nullptr; }

    private:
        friend class CountedBTree;
        Iterator(Leaf* l, int p) : leaf_(l), pos_(p) {}
        Leaf* leaf_ = nullptr;
        int pos_ = 0;
    };

    explicit CountedBTree(Cmp cmp = Cmp{}) : cmp_(std::move(cmp)) {}
    CountedBTree(const CountedBTree&) = delete;
    CountedBTree& operator=(const CountedBTree&) = delete;
    CountedBTree(CountedBTree&& o) noexcept { swap(o); }
    CountedBTree& operator=(CountedBTree&& o) noexcept {
        if (this != &o) {
            clear();
            swap(o);
        }
        return *this;
    }
    ~CountedBTree() { clear(); }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] Iterator begin() const noexcept { return size_ ? Iterator(first_, 0) : Iterator(); }
    [[nodiscard]] Iterator end() const noexcept { return Iterator(); }
    [[nodiscard]] Cmp& comparator() noexcept { return cmp_; }

    // Returns false if an equal key is already present.
    bool insert(const K& key) {
        if (!root_) {
            auto* l = new Leaf();
            l->keys[0] = key;
            l->n = 1;
            root_ = first_ = last_ = l;
            size_ = 1;
            return true;
        }
        bool inserted = false;
        if (auto split = insert_rec(root_, key, inserted)) {
            auto* r = new Inner();
            r->ch[0] = root_;
            r->cnt[0] = count_of(root_);
            r->ch[1] = split->right;
            r->cnt[1] = split->right_count;
            r->sep[1] = split->sep;
            r->n = 2;
            root_ = r;
        }
        if (inserted) ++size_;
        return inserted;
    }

    bool erase(const K& key) {
        if (!root_) return false;
        bool erased = false;
        erase_rec(root_, key, erased);
        if (!erased) return false;
        --size_;
        if (!root_->leaf && root_->n == 1) {
            Node* old = root_;
            root_ = static_cast<Inner*>(old)->ch[0];
            delete static_cast<Inner*>(old);
        } else if (root_->leaf && root_->n == 0) {
            delete static_cast<Leaf*>(root_);
            root_ = nullptr;
            first_ = last_ = nullptr;
        }
        return true;
    }

    template <class Q>
    [[nodiscard]] bool contains(const Q& key) const {
        auto it = lower_bound(key);
        return it.valid() && !cmp_(key, *it);
    }

    // Number of keys strictly less than `key`. Q may be any type the comparator accepts on both sides.
    template <class Q>
    [[nodiscard]] std::size_t rank(const Q& key) const {
        std::size_t r = 0;
        const Node* n = root_;
        if (!n) return 0;
        while (!n->leaf) {
            const auto* in = static_cast<const Inner*>(n);
            const int i = route(in, key);
            for (int j = 0; j < i; ++j) r += in->cnt[j];
            n = in->ch[i];
        }
        const auto* l = static_cast<const Leaf*>(n);
        return r + static_cast<std::size_t>(leaf_lower(l, key));
    }

    // Key at 0-based rank r; r must be < size().
    [[nodiscard]] const K& select(std::size_t r) const SB_PRE(r < size_) {
        const Node* n = root_;
        while (!n->leaf) {
            const auto* in = static_cast<const Inner*>(n);
            int i = 0;
            while (r >= in->cnt[i]) r -= in->cnt[i++];
            n = in->ch[i];
        }
        return static_cast<const Leaf*>(n)->keys[r];
    }

    [[nodiscard]] Iterator iterator_at(std::size_t r) const {
        if (r >= size_) return end();
        const Node* n = root_;
        while (!n->leaf) {
            const auto* in = static_cast<const Inner*>(n);
            int i = 0;
            while (r >= in->cnt[i]) r -= in->cnt[i++];
            n = in->ch[i];
        }
        return Iterator(const_cast<Leaf*>(static_cast<const Leaf*>(n)), static_cast<int>(r));
    }

    template <class Q>
    [[nodiscard]] Iterator lower_bound(const Q& key) const {
        const Node* n = root_;
        if (!n) return end();
        while (!n->leaf) {
            const auto* in = static_cast<const Inner*>(n);
            n = in->ch[route(in, key)];
        }
        auto* l = const_cast<Leaf*>(static_cast<const Leaf*>(n));
        const int p = leaf_lower(l, key);
        if (p < l->n) return Iterator(l, p);
        return l->next ? Iterator(l->next, 0) : end();
    }

    template <class Q>
    [[nodiscard]] Iterator upper_bound(const Q& key) const {
        auto it = lower_bound(key);
        if (it.valid() && !cmp_(key, *it)) ++it;
        return it;
    }

    void clear() noexcept {
        if (root_) destroy(root_);
        root_ = nullptr;
        first_ = last_ = nullptr;
        size_ = 0;
    }

    // Structural self-check used by tests: ordering, counts, fill bounds and leaf links.
    [[nodiscard]] bool validate() const {
        if (!root_) return size_ == 0 && !first_ && !last_;
        std::size_t total = 0;
        const Leaf* prev_leaf = nullptr;
        const K* prev_key = nullptr;
        if (!validate_rec(root_, true, total, prev_leaf, prev_key, nullptr, nullptr)) return false;
        return total == size_ && prev_leaf == last_;
    }

private:
    struct Split {
        Node* right;
        K sep;
        u32 right_count;
    };

    template <class Q>
    [[nodiscard]] int route(const Inner* in, const Q& key) const {
        // First separator strictly greater than key; child is the one before it.
        int lo = 1, hi = in->n;
        while (lo < hi) {
            const int mid = (lo + hi) / 2;
            if (cmp_(key, in->sep[mid])) hi = mid;
            else lo = mid + 1;
        }
        return lo - 1;
    }

    template <class Q>
    [[nodiscard]] int leaf_lower(const Leaf* l, const Q& key) const {
        int lo = 0, hi = l->n;
        while (lo < hi) {
            const int mid = (lo + hi) / 2;
            if (cmp_(l->keys[mid], key)) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    }

    [[nodiscard]] static u32 count_of(const Node* n) noexcept {
        if (n->leaf) return n->n;
        const auto* in = static_cast<const Inner*>(n);
        u32 c = 0;
        for (int i = 0; i < in->n; ++i) c += in->cnt[i];
        return c;
    }

    [[nodiscard]] const K& min_key(const Node* n) const noexcept {
        while (!n->leaf) n = static_cast<const Inner*>(n)->ch[0];
        return static_cast<const Leaf*>(n)->keys[0];
    }

    std::optional<Split> insert_rec(Node* n, const K& key, bool& inserted) {
        if (n->leaf) {
            auto* l = static_cast<Leaf*>(n);
            const int p = leaf_lower(l, key);
            if (p < l->n && !cmp_(key, l->keys[p])) return std::nullopt;
            inserted = true;
            if (l->n < LCap) {
                std::move_backward(l->keys + p, l->keys + l->n, l->keys + l->n + 1);
                l->keys[p] = key;
                ++l->n;
                return std::nullopt;
            }
            auto* r = new Leaf();
            const int half = LCap / 2;
            std::move(l->keys + half, l->keys + LCap, r->keys);
            r->n = static_cast<u16>(LCap - half);
            l->n = static_cast<u16>(half);
            r->next = l->next;
            r->prev = l;
            if (l->next) l->next->prev = r;
            else last_ = r;
            l->next = r;
            Leaf* target = p <= half ? l : r;
            const int tp = p <= half ? p : p - half;
            std::move_backward(target->keys + tp, target->keys + target->n, target->keys + target->n + 1);
            target->keys[tp] = key;
            ++target->n;
            return Split{r, r->keys[0], r->n};
        }

        auto* in = static_cast<Inner*>(n);
        const int i = route(in, key);
        auto split = insert_rec(in->ch[i], key, inserted);
        if (!inserted) return std::nullopt;
        if (!split) {
            ++in->cnt[i];
            return std::nullopt;
        }
        in->cnt[i] = count_of(in->ch[i]);
        if (in->n < ICap) {
            insert_child(in, i + 1, split->right, split->right_count, split->sep);
            return std::nullopt;
        }
        // Full inner node: lay out ICap + 1 children, keep the lower half, move the upper half.
        Node* tch[ICap + 1];
        u32 tcnt[ICap + 1];
        K tsep[ICap + 1];
        const int at = i + 1;
        for (int j = 0, k = 0; j <= ICap; ++j) {
            if (j == at) {
                tch[j] = split->right;
                tcnt[j] = split->right_count;
                tsep[j] = split->sep;
            } else {
                tch[j] = in->ch[k];
                tcnt[j] = in->cnt[k];
                tsep[j] = in->sep[k];
                ++k;
            }
        }
        constexpr int total = ICap + 1;
        constexpr int lh = total / 2;
        for (int j = 0; j < lh; ++j) {
            in->ch[j] = tch[j];
            in->cnt[j] = tcnt[j];
            in->sep[j] = tsep[j];
        }
        in->n = static_cast<u16>(lh);
        auto* r = new Inner();
        for (int j = lh; j < total; ++j) {
            r->ch[j - lh] = tch[j];
            r->cnt[j - lh] = tcnt[j];
            r->sep[j - lh] = tsep[j];
        }
        r->n = static_cast<u16>(total - lh);
        return Split{r, tsep[lh], count_of(r)};
    }

    static void insert_child(Inner* in, int at, Node* child, u32 count, const K& sep) {
        for (int j = in->n; j > at; --j) {
            in->ch[j] = in->ch[j - 1];
            in->cnt[j] = in->cnt[j - 1];
            in->sep[j] = in->sep[j - 1];
        }
        in->ch[at] = child;
        in->cnt[at] = count;
        in->sep[at] = sep;
        ++in->n;
    }

    static void remove_child(Inner* in, int at) {
        for (int j = at; j + 1 < in->n; ++j) {
            in->ch[j] = in->ch[j + 1];
            in->cnt[j] = in->cnt[j + 1];
            in->sep[j] = in->sep[j + 1];
        }
        --in->n;
    }

    // Returns true if `n` dropped below its minimum fill.
    bool erase_rec(Node* n, const K& key, bool& erased) {
        if (n->leaf) {
            auto* l = static_cast<Leaf*>(n);
            const int p = leaf_lower(l, key);
            if (p >= l->n || cmp_(key, l->keys[p])) return false;
            std::move(l->keys + p + 1, l->keys + l->n, l->keys + p);
            --l->n;
            erased = true;
            return l->n < LMin;
        }
        auto* in = static_cast<Inner*>(n);
        const int i = route(in, key);
        const bool under = erase_rec(in->ch[i], key, erased);
        if (!erased) return false;
        --in->cnt[i];
        if (under) rebalance(in, i);
        return in->n < IMin;
    }

    void rebalance(Inner* parent, int i) {
        Node* c = parent->ch[i];
        Node* left = i > 0 ? parent->ch[i - 1] : nullptr;
        Node* right = i + 1 < parent->n ? parent->ch[i + 1] : nullptr;
        const int cmin = c->leaf ? LMin : IMin;
        if (left && left->n > cmin) {
            borrow_from_left(parent, i);
        } else if (right && right->n > cmin) {
            borrow_from_right(parent, i);
        } else if (left) {
            merge(parent, i - 1);
        } else if (right) {
            merge(parent, i);
        }
    }

    void borrow_from_left(Inner* parent, int i) {
        Node* c = parent->ch[i];
        Node* left = parent->ch[i - 1];
        if (c->leaf) {
            auto* cl = static_cast<Leaf*>(c);
            auto* ll = static_cast<Leaf*>(left);
            std::move_backward(cl->keys, cl->keys + cl->n, cl->keys + cl->n + 1);
            cl->keys[0] = ll->keys[ll->n - 1];
            --ll->n;
            ++cl->n;
            parent->sep[i] = cl->keys[0];
            --parent->cnt[i - 1];
            ++parent->cnt[i];
        } else {
            auto* ci = static_cast<Inner*>(c);
            auto* li = static_cast<Inner*>(left);
            const int last = li->n - 1;
            const u32 moved = li->cnt[last];
            // Old first child of c gets the parent's separator; borrowed child becomes c's first.
            insert_child(ci, 0, li->ch[last], moved, K{});
            ci->sep[1] = parent->sep[i];
            parent->sep[i] = li->sep[last];
            --li->n;
            parent->cnt[i - 1] -= moved;
            parent->cnt[i] += moved;
        }
    }

    void borrow_from_right(Inner* parent, int i) {
        Node* c = parent->ch[i];
        Node* right = parent->ch[i + 1];
        if (c->leaf) {
            auto* cl = static_cast<Leaf*>(c);
            auto* rl = static_cast<Leaf*>(right);
            cl->keys[cl->n++] = rl->keys[0];
            std::move(rl->keys + 1, rl->keys + rl->n, rl->keys);
            --rl->n;
            parent->sep[i + 1] = rl->keys[0];
            ++parent->cnt[i];
            --parent->cnt[i + 1];
        } else {
            auto* ci = static_cast<Inner*>(c);
            auto* ri = static_cast<Inner*>(right);
            const u32 moved = ri->cnt[0];
            ci->ch[ci->n] = ri->ch[0];
            ci->cnt[ci->n] = moved;
            ci->sep[ci->n] = parent->sep[i + 1];
            ++ci->n;
            parent->sep[i + 1] = ri->sep[1];
            remove_child(ri, 0);
            parent->cnt[i] += moved;
            parent->cnt[i + 1] -= moved;
        }
    }

    // Merges ch[i + 1] into ch[i].
    void merge(Inner* parent, int i) {
        Node* a = parent->ch[i];
        Node* b = parent->ch[i + 1];
        if (a->leaf) {
            auto* al = static_cast<Leaf*>(a);
            auto* bl = static_cast<Leaf*>(b);
            std::move(bl->keys, bl->keys + bl->n, al->keys + al->n);
            al->n = static_cast<u16>(al->n + bl->n);
            al->next = bl->next;
            if (bl->next) bl->next->prev = al;
            else last_ = al;
            delete bl;
        } else {
            auto* ai = static_cast<Inner*>(a);
            auto* bi = static_cast<Inner*>(b);
            const int base = ai->n;
            for (int j = 0; j < bi->n; ++j) {
                ai->ch[base + j] = bi->ch[j];
                ai->cnt[base + j] = bi->cnt[j];
                ai->sep[base + j] = bi->sep[j];
            }
            ai->sep[base] = parent->sep[i + 1];
            ai->n = static_cast<u16>(base + bi->n);
            delete bi;
        }
        parent->cnt[i] += parent->cnt[i + 1];
        remove_child(parent, i + 1);
    }

    void destroy(Node* n) noexcept {
        if (n->leaf) {
            delete static_cast<Leaf*>(n);
            return;
        }
        auto* in = static_cast<Inner*>(n);
        for (int i = 0; i < in->n; ++i) destroy(in->ch[i]);
        delete in;
    }

    bool validate_rec(const Node* n, bool is_root, std::size_t& total, const Leaf*& prev_leaf, const K*& prev_key,
                      const K* lo, const K* hi) const {
        if (n->leaf) {
            const auto* l = static_cast<const Leaf*>(n);
            if (!is_root && l->n < LMin) return false;
            if (l->prev != prev_leaf) return false;
            if (prev_leaf ? prev_leaf->next != l : first_ != l) return false;
            for (int i = 0; i < l->n; ++i) {
                if (prev_key && !cmp_(*prev_key, l->keys[i])) return false;
                if (lo && cmp_(l->keys[i], *lo)) return false;
                if (hi && !cmp_(l->keys[i], *hi)) return false;
                prev_key = &l->keys[i];
            }
            total += l->n;
            prev_leaf = l;
            return true;
        }
        const auto* in = static_cast<const Inner*>(n);
        if (!is_root && in->n < IMin) return false;
        if (is_root && in->n < 2) return false;
        for (int i = 0; i < in->n; ++i) {
            const std::size_t before = total;
            const K* clo = i == 0 ? lo : &in->sep[i];
            const K* chi = i + 1 < in->n ? &in->sep[i + 1] : hi;
            if (!validate_rec(in->ch[i], false, total, prev_leaf, prev_key, clo, chi)) return false;
            if (total - before != in->cnt[i]) return false;
        }
        return true;
    }

    void swap(CountedBTree& o) noexcept {
        std::swap(cmp_, o.cmp_);
        std::swap(root_, o.root_);
        std::swap(first_, o.first_);
        std::swap(last_, o.last_);
        std::swap(size_, o.size_);
    }

    Cmp cmp_;
    Node* root_ = nullptr;
    Leaf* first_ = nullptr;
    Leaf* last_ = nullptr;
    std::size_t size_ = 0;
};

}  // namespace sb
