#include <benchmark/benchmark.h>
#include <linux/input-event-codes.h>

import fs8.mods;

using namespace fs8;

// Debounce: pass-through (event not in tracked set)
static void BM_Debounce_Passthrough(benchmark::State& state) {
    auto db = debounce[BTN_LEFT];
    auto const ev = event_type{EV_KEY, KEY_A, 1};
    for (auto _ : state) {
        benchmark::DoNotOptimize(db(ev));
    }
}
BENCHMARK(BM_Debounce_Passthrough);

// Debounce: tracked event (first call passes, second drops)
static void BM_Debounce_TrackedFirst(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        auto db = debounce[BTN_LEFT];
        state.ResumeTiming();
        auto const ev = event_type{EV_KEY, BTN_LEFT, 1};
        benchmark::DoNotOptimize(db(ev));
    }
}
BENCHMARK(BM_Debounce_TrackedFirst);

// Debounce: tracked event (second call drops — measures drop path)
static void BM_Debounce_Drop(benchmark::State& state) {
    auto db = debounce[BTN_LEFT];
    auto const ev = event_type{EV_KEY, BTN_LEFT, 1};
    benchmark::DoNotOptimize(db(ev));  // first call — passes
    for (auto _ : state) {
        benchmark::DoNotOptimize(db(ev));
    }
}
BENCHMARK(BM_Debounce_Drop);

// Debounce: multiple codes
static void BM_Debounce_MultiCode(benchmark::State& state) {
    auto db = debounce[BTN_LEFT, BTN_RIGHT, BTN_MIDDLE];
    auto const ev = event_type{EV_KEY, BTN_LEFT, 1};
    for (auto _ : state) {
        benchmark::DoNotOptimize(db(ev));
    }
}
BENCHMARK(BM_Debounce_MultiCode);

// Debounce: event mode (drops any event within window)
static void BM_Debounce_EventMode(benchmark::State& state) {
    auto db = debounce[{.type = EV_ABS, .code = ABS_X}].event();
    auto const ev = event_type{EV_ABS, ABS_X, 100};
    for (auto _ : state) {
        benchmark::DoNotOptimize(db(ev));
    }
}
BENCHMARK(BM_Debounce_EventMode);

BENCHMARK_MAIN();
