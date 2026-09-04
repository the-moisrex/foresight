#include <benchmark/benchmark.h>
#include <linux/input-event-codes.h>

import fs8.mods;

#include "events.hxx"

using namespace fs8;

// Debounce: pass-through (event not in tracked set)
static void BM_Debounce_Passthrough(benchmark::State& state) {
    auto db = debounce[BTN_LEFT];
    auto events = bench::load_events("benchmarks/events/mouse.movement");
    std::size_t i = 0;
    for (auto _ : state) {
        auto const& ev = events[i % events.size()];
        benchmark::DoNotOptimize(db(ev));
        ++i;
    }
}
BENCHMARK(BM_Debounce_Passthrough);

// Debounce: tracked event (first call passes)
static void BM_Debounce_TrackedFirst(benchmark::State& state) {
    auto events = bench::load_events("benchmarks/events/keyboard.typing");
    std::size_t i = 0;
    for (auto _ : state) {
        state.PauseTiming();
        auto db = debounce[BTN_LEFT];
        state.ResumeTiming();
        auto const& ev = events[i % events.size()];
        benchmark::DoNotOptimize(db(ev));
        ++i;
    }
}
BENCHMARK(BM_Debounce_TrackedFirst);

// Debounce: tracked event (second call drops)
static void BM_Debounce_Drop(benchmark::State& state) {
    auto db = debounce[BTN_LEFT];
    auto events = bench::load_events("benchmarks/events/keyboard.typing");
    for (auto const& ev : events) {
        benchmark::DoNotOptimize(db(ev));  // seed
    }
    std::size_t i = 0;
    for (auto _ : state) {
        auto const& ev = events[i % events.size()];
        benchmark::DoNotOptimize(db(ev));
        ++i;
    }
}
BENCHMARK(BM_Debounce_Drop);

// Debounce: multiple codes
static void BM_Debounce_MultiCode(benchmark::State& state) {
    auto db = debounce[BTN_LEFT, BTN_RIGHT, BTN_MIDDLE];
    auto events = bench::load_events("benchmarks/events/mixed.keyboard_mouse");
    std::size_t i = 0;
    for (auto _ : state) {
        auto const& ev = events[i % events.size()];
        benchmark::DoNotOptimize(db(ev));
        ++i;
    }
}
BENCHMARK(BM_Debounce_MultiCode);

// Debounce: event mode
static void BM_Debounce_EventMode(benchmark::State& state) {
    auto db = debounce[{.type = EV_ABS, .code = ABS_X}].event();
    auto events = bench::load_events("benchmarks/events/mouse.movement");
    std::size_t i = 0;
    for (auto _ : state) {
        auto const& ev = events[i % events.size()];
        benchmark::DoNotOptimize(db(ev));
        ++i;
    }
}
BENCHMARK(BM_Debounce_EventMode);

BENCHMARK_MAIN();
