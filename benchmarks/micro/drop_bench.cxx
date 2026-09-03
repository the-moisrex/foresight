#include <benchmark/benchmark.h>
#include <linux/input-event-codes.h>

import fs8.mods;

using namespace fs8;

// Drop keys: pass-through (non-matching key)
static void BM_DropKeys_Passthrough(benchmark::State& state) {
    auto dk = drop_keys[KEY_A, KEY_B, KEY_C];
    auto const ev = event_type{EV_KEY, KEY_D, 1};
    for (auto _ : state) {
        benchmark::DoNotOptimize(dk(ev));
    }
}
BENCHMARK(BM_DropKeys_Passthrough);

// Drop keys: drops matching key
static void BM_DropKeys_Drop(benchmark::State& state) {
    auto dk = drop_keys[KEY_A, KEY_B, KEY_C];
    auto const ev = event_type{EV_KEY, KEY_A, 1};
    for (auto _ : state) {
        benchmark::DoNotOptimize(dk(ev));
    }
}
BENCHMARK(BM_DropKeys_Drop);

// Drop mouse moves: pass-through (EV_KEY event)
static void BM_DropMouseMoves_Passthrough(benchmark::State& state) {
    auto const ev = event_type{EV_KEY, KEY_A, 1};
    for (auto _ : state) {
        benchmark::DoNotOptimize(drop_mouse_moves(ev));
    }
}
BENCHMARK(BM_DropMouseMoves_Passthrough);

// Drop mouse moves: drops REL_X
static void BM_DropMouseMoves_Drop(benchmark::State& state) {
    auto const ev = event_type{EV_REL, REL_X, 5};
    for (auto _ : state) {
        benchmark::DoNotOptimize(drop_mouse_moves(ev));
    }
}
BENCHMARK(BM_DropMouseMoves_Drop);

// Drop big jumps: pass-through (small movement)
static void BM_DropBigJumps_Passthrough(benchmark::State& state) {
    auto dj = drop_big_jumps[100];
    auto const ev = event_type{EV_REL, REL_X, 5};
    for (auto _ : state) {
        benchmark::DoNotOptimize(dj(ev));
    }
}
BENCHMARK(BM_DropBigJumps_Passthrough);

// Drop big jumps: drops large movement
static void BM_DropBigJumps_Drop(benchmark::State& state) {
    auto dj = drop_big_jumps[100];
    auto const ev = event_type{EV_REL, REL_X, 200};
    for (auto _ : state) {
        benchmark::DoNotOptimize(dj(ev));
    }
}
BENCHMARK(BM_DropBigJumps_Drop);

// Drop ABS events: pass-through
static void BM_DropAbs_Passthrough(benchmark::State& state) {
    auto const ev = event_type{EV_KEY, KEY_A, 1};
    for (auto _ : state) {
        benchmark::DoNotOptimize(drop_abs(ev));
    }
}
BENCHMARK(BM_DropAbs_Passthrough);

// Drop ABS events: drops ABS_X
static void BM_DropAbs_Drop(benchmark::State& state) {
    auto const ev = event_type{EV_ABS, ABS_X, 500};
    for (auto _ : state) {
        benchmark::DoNotOptimize(drop_abs(ev));
    }
}
BENCHMARK(BM_DropAbs_Drop);

// Drop MSC_SCAN events: pass-through
static void BM_DropMscScan_Passthrough(benchmark::State& state) {
    auto const ev = event_type{EV_KEY, KEY_A, 1};
    for (auto _ : state) {
        benchmark::DoNotOptimize(drop_msc_scan(ev));
    }
}
BENCHMARK(BM_DropMscScan_Passthrough);

// Drop MSC_SCAN events: drops MSC_SCAN
static void BM_DropMscScan_Drop(benchmark::State& state) {
    auto const ev = event_type{EV_MSC, MSC_SCAN, 0};
    for (auto _ : state) {
        benchmark::DoNotOptimize(drop_msc_scan(ev));
    }
}
BENCHMARK(BM_DropMscScan_Drop);

// Drop adjacent repeats: first call passes
static void BM_DropAdjacentRepeats_First(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        auto dr = drop_adjacent_repeats[event_code{.type = EV_KEY, .code = BTN_LEFT}];
        state.ResumeTiming();
        auto const ev = event_type{EV_KEY, BTN_LEFT, 1};
        benchmark::DoNotOptimize(dr(ev));
    }
}
BENCHMARK(BM_DropAdjacentRepeats_First);

// Drop adjacent repeats: second call drops
static void BM_DropAdjacentRepeats_Drop(benchmark::State& state) {
    auto dr = drop_adjacent_repeats[event_code{.type = EV_KEY, .code = BTN_LEFT}];
    auto const ev = event_type{EV_KEY, BTN_LEFT, 1};
    benchmark::DoNotOptimize(dr(ev));  // first — passes
    for (auto _ : state) {
        benchmark::DoNotOptimize(dr(ev));
    }
}
BENCHMARK(BM_DropAdjacentRepeats_Drop);

BENCHMARK_MAIN();
