// Created by moisrex on 8/22/26.

module;
#include <chrono>
module fs8.mods;

using fs8::basic_benchmark_counter;
using fs8::benchmark_stats;

template <>
struct fs8::pimpl_idiom<basic_benchmark_counter>::impl {
    benchmark_stats stats;
};

void basic_benchmark_counter::record(benchmark_stats::duration const elapsed) noexcept {
    if (pimpl.get() == nullptr) {
        init_impl();
    }
    auto& stats = pimpl->stats;
    ++stats.calls;
    stats.total += elapsed;
    if (elapsed < stats.min) {
        stats.min = elapsed;
    }
    if (elapsed > stats.max) {
        stats.max = elapsed;
    }
}

benchmark_stats basic_benchmark_counter::result() const noexcept {
    if (pimpl.get() == nullptr) {
        return {};
    }
    return pimpl->stats;
}

void basic_benchmark_counter::clear() noexcept {
    if (pimpl.get() != nullptr) {
        pimpl->stats = {};
    }
}
