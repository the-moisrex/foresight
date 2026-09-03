#include <benchmark/benchmark.h>
#include <linux/input-event-codes.h>

import fs8.mods;

using namespace fs8;

// Helper: create a realistic mouse movement event sequence
static constexpr auto mouse_events() noexcept {
    return std::array<user_event, 15>{{
        user_event{EV_REL, REL_X, 5}, user_event{EV_REL, REL_Y, 3}, user_event{EV_SYN, SYN_REPORT, 0},
        user_event{EV_REL, REL_X, 10}, user_event{EV_REL, REL_Y, 7}, user_event{EV_SYN, SYN_REPORT, 0},
        user_event{EV_REL, REL_X, -3}, user_event{EV_REL, REL_Y, 2}, user_event{EV_SYN, SYN_REPORT, 0},
        user_event{EV_REL, REL_X, 15}, user_event{EV_REL, REL_Y, -5}, user_event{EV_SYN, SYN_REPORT, 0},
        user_event{EV_REL, REL_X, 8}, user_event{EV_REL, REL_Y, 12}, user_event{EV_SYN, SYN_REPORT, 0},
    }};
}

// Lerp: default settings
static void BM_Lerp_Default(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        auto pipeline = context
            | emit_all[mouse_events()]
            | lerp
            | record;
        state.ResumeTiming();
        pipeline();
    }
}
BENCHMARK(BM_Lerp_Default);

// Lerp: 8 steps
static void BM_Lerp_8Steps(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        auto pipeline = context
            | emit_all[mouse_events()]
            | lerp[8]
            | record;
        state.ResumeTiming();
        pipeline();
    }
}
BENCHMARK(BM_Lerp_8Steps);

// Lerp: 32 steps
static void BM_Lerp_32Steps(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        auto pipeline = context
            | emit_all[mouse_events()]
            | lerp[32]
            | record;
        state.ResumeTiming();
        pipeline();
    }
}
BENCHMARK(BM_Lerp_32Steps);

// Low-pass filter: alpha=0.5
static void BM_LowPassFilter_05(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        auto pipeline = context
            | emit_all[mouse_events()]
            | low_pass_filter[0.5f]
            | record;
        state.ResumeTiming();
        pipeline();
    }
}
BENCHMARK(BM_LowPassFilter_05);

// Low-pass filter: alpha=0.1 (smoother)
static void BM_LowPassFilter_01(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        auto pipeline = context
            | emit_all[mouse_events()]
            | low_pass_filter[0.1f]
            | record;
        state.ResumeTiming();
        pipeline();
    }
}
BENCHMARK(BM_LowPassFilter_01);

// Kalman filter: default
static void BM_KalmanFilter_Default(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        auto pipeline = context
            | emit_all[mouse_events()]
            | kalman_filter
            | record;
        state.ResumeTiming();
        pipeline();
    }
}
BENCHMARK(BM_KalmanFilter_Default);

// Kalman filter: low noise
static void BM_KalmanFilter_LowNoise(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        auto pipeline = context
            | emit_all[mouse_events()]
            | kalman_filter[0.01f, 0.1f]
            | record;
        state.ResumeTiming();
        pipeline();
    }
}
BENCHMARK(BM_KalmanFilter_LowNoise);

BENCHMARK_MAIN();
