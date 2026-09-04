#include <benchmark/benchmark.h>
#include <linux/input-event-codes.h>

import fs8.mods;

#include "events.hxx"

using namespace fs8;

// Drop keys: pass-through
static void BM_DropKeys_Passthrough(benchmark::State& state) {
    auto dk = drop_keys[KEY_A, KEY_B, KEY_C];
    auto events = bench::load_events("benchmarks/events/mouse.movement");
    std::size_t i = 0;
    for (auto _ : state) {
        auto const& ev = events[i % events.size()];
        benchmark::DoNotOptimize(dk(ev));
        ++i;
    }
}
BENCHMARK(BM_DropKeys_Passthrough);

// Drop keys: drops matching key
static void BM_DropKeys_Drop(benchmark::State& state) {
    auto dk = drop_keys[KEY_A, KEY_B, KEY_C];
    auto events = bench::load_events("benchmarks/events/keyboard.typing");
    std::size_t i = 0;
    for (auto _ : state) {
        auto const& ev = events[i % events.size()];
        benchmark::DoNotOptimize(dk(ev));
        ++i;
    }
}
BENCHMARK(BM_DropKeys_Drop);

// Drop mouse moves: pass-through
static void BM_DropMouseMoves_Passthrough(benchmark::State& state) {
    auto events = bench::load_events("benchmarks/events/keyboard.typing");
    std::size_t i = 0;
    for (auto _ : state) {
        auto const& ev = events[i % events.size()];
        benchmark::DoNotOptimize(drop_mouse_moves(ev));
        ++i;
    }
}
BENCHMARK(BM_DropMouseMoves_Passthrough);

// Drop mouse moves: drops REL events
static void BM_DropMouseMoves_Drop(benchmark::State& state) {
    auto events = bench::load_events("benchmarks/events/mouse.movement");
    std::size_t i = 0;
    for (auto _ : state) {
        auto const& ev = events[i % events.size()];
        benchmark::DoNotOptimize(drop_mouse_moves(ev));
        ++i;
    }
}
BENCHMARK(BM_DropMouseMoves_Drop);

// Drop big jumps: pass-through (small movements)
static void BM_DropBigJumps_Passthrough(benchmark::State& state) {
    auto dj = drop_big_jumps[100];
    auto events = bench::load_events("benchmarks/events/mouse.movement");
    std::size_t i = 0;
    for (auto _ : state) {
        auto const& ev = events[i % events.size()];
        benchmark::DoNotOptimize(dj(ev));
        ++i;
    }
}
BENCHMARK(BM_DropBigJumps_Passthrough);

// Drop big jumps: drops large movements
static void BM_DropBigJumps_Drop(benchmark::State& state) {
    auto dj = drop_big_jumps[1];
    auto events = bench::load_events("benchmarks/events/mouse.movement");
    std::size_t i = 0;
    for (auto _ : state) {
        auto const& ev = events[i % events.size()];
        benchmark::DoNotOptimize(dj(ev));
        ++i;
    }
}
BENCHMARK(BM_DropBigJumps_Drop);

// Drop ABS: pass-through
static void BM_DropAbs_Passthrough(benchmark::State& state) {
    auto events = bench::load_events("benchmarks/events/keyboard.typing");
    std::size_t i = 0;
    for (auto _ : state) {
        auto const& ev = events[i % events.size()];
        benchmark::DoNotOptimize(drop_abs(ev));
        ++i;
    }
}
BENCHMARK(BM_DropAbs_Passthrough);

// Drop ABS: drops ABS events
static void BM_DropAbs_Drop(benchmark::State& state) {
    auto events = bench::load_events("benchmarks/events/mouse.movement");
    std::size_t i = 0;
    for (auto _ : state) {
        auto const& ev = events[i % events.size()];
        benchmark::DoNotOptimize(drop_abs(ev));
        ++i;
    }
}
BENCHMARK(BM_DropAbs_Drop);

// Drop MSC_SCAN: pass-through
static void BM_DropMscScan_Passthrough(benchmark::State& state) {
    auto events = bench::load_events("benchmarks/events/mouse.movement");
    std::size_t i = 0;
    for (auto _ : state) {
        auto const& ev = events[i % events.size()];
        benchmark::DoNotOptimize(drop_msc_scan(ev));
        ++i;
    }
}
BENCHMARK(BM_DropMscScan_Passthrough);

// Drop adjacent repeats: first call passes
static void BM_DropAdjacentRepeats_First(benchmark::State& state) {
    auto events = bench::load_events("benchmarks/events/keyboard.typing");
    std::size_t i = 0;
    for (auto _ : state) {
        state.PauseTiming();
        auto dr = drop_adjacent_repeats[event_code{.type = EV_KEY, .code = BTN_LEFT}];
        state.ResumeTiming();
        auto const& ev = events[i % events.size()];
        benchmark::DoNotOptimize(dr(ev));
        ++i;
    }
}
BENCHMARK(BM_DropAdjacentRepeats_First);

// Drop adjacent repeats: second call drops
static void BM_DropAdjacentRepeats_Drop(benchmark::State& state) {
    auto dr = drop_adjacent_repeats[event_code{.type = EV_KEY, .code = BTN_LEFT}];
    auto events = bench::load_events("benchmarks/events/keyboard.typing");
    for (auto const& ev : events) {
        benchmark::DoNotOptimize(dr(ev));  // seed with first events
    }
    std::size_t i = 0;
    for (auto _ : state) {
        auto const& ev = events[i % events.size()];
        benchmark::DoNotOptimize(dr(ev));
        ++i;
    }
}
BENCHMARK(BM_DropAdjacentRepeats_Drop);

BENCHMARK_MAIN();
