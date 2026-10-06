#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <string>

#include "core/types.hpp"

namespace sb::ops {

// Counter written by exactly one thread and read by the scraper: relaxed load/store, no RMW.
class Counter {
public:
    void inc(u64 n = 1) noexcept { v_.store(v_.load(std::memory_order_relaxed) + n, std::memory_order_relaxed); }
    void set(u64 n) noexcept { v_.store(n, std::memory_order_relaxed); }
    [[nodiscard]] u64 get() const noexcept { return v_.load(std::memory_order_relaxed); }

private:
    std::atomic<u64> v_{0};
};

// Single-writer log2 histogram in microseconds (1us .. ~33s).
class Histogram {
public:
    static constexpr std::size_t kBuckets = 26;

    void observe_us(u64 us) noexcept {
        const std::size_t b = us == 0 ? 0 : std::min<std::size_t>(kBuckets - 1, std::bit_width(us) - 1);
        buckets_[b].store(buckets_[b].load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        sum_.store(sum_.load(std::memory_order_relaxed) + us, std::memory_order_relaxed);
    }

    // Upper bound (inclusive) of bucket i in microseconds.
    [[nodiscard]] static constexpr u64 bound_us(std::size_t i) noexcept { return (u64{2} << i) - 1; }
    [[nodiscard]] u64 bucket(std::size_t i) const noexcept { return buckets_[i].load(std::memory_order_relaxed); }
    [[nodiscard]] u64 sum_us() const noexcept { return sum_.load(std::memory_order_relaxed); }

private:
    std::array<std::atomic<u64>, kBuckets> buckets_{};
    std::atomic<u64> sum_{0};
};

// Minimal Prometheus text exposition writer.
class Exposition {
public:
    void family(std::string_view name, std::string_view type, std::string_view help) {
        out_ += "# HELP ";
        out_ += name;
        out_ += ' ';
        out_ += help;
        out_ += "\n# TYPE ";
        out_ += name;
        out_ += ' ';
        out_ += type;
        out_ += '\n';
    }

    void sample(std::string_view name, std::string_view labels, u64 value) {
        out_ += name;
        if (!labels.empty()) {
            out_ += '{';
            out_ += labels;
            out_ += '}';
        }
        out_ += ' ';
        out_ += std::to_string(value);
        out_ += '\n';
    }

    void sample(std::string_view name, std::string_view labels, double value) {
        out_ += name;
        if (!labels.empty()) {
            out_ += '{';
            out_ += labels;
            out_ += '}';
        }
        out_ += ' ';
        out_ += std::to_string(value);
        out_ += '\n';
    }

    // Emits cumulative buckets (seconds) for several single-writer histograms merged together.
    template <class Range>
    void histogram(std::string_view name, std::string_view labels, const Range& hists) {
        u64 cumulative = 0;
        u64 sum_us = 0;
        for (std::size_t i = 0; i < Histogram::kBuckets; ++i) {
            for (const Histogram* h : hists) cumulative += h->bucket(i);
            std::string l(labels);
            if (!l.empty()) l += ',';
            l += "le=\"" + std::to_string(static_cast<double>(Histogram::bound_us(i)) / 1e6) + "\"";
            sample(std::string(name) + "_bucket", l, cumulative);
        }
        for (const Histogram* h : hists) sum_us += h->sum_us();
        std::string l(labels);
        if (!l.empty()) l += ',';
        sample(std::string(name) + "_bucket", l + "le=\"+Inf\"", cumulative);
        sample(std::string(name) + "_sum", labels, static_cast<double>(sum_us) / 1e6);
        sample(std::string(name) + "_count", labels, cumulative);
    }

    [[nodiscard]] std::string take() { return std::move(out_); }

private:
    std::string out_;
};

}  // namespace sb::ops
