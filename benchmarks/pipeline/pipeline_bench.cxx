// Pipeline-level benchmark: measures real-world event processing latency
// through a realistic pipeline with multiple mods.
//
// Run: ./bench-pipeline
// Compare output across runs to track performance changes.

#include <array>
#include <cstdio>
#include <linux/input-event-codes.h>

import fs8.mods;

using namespace fs8;

// Realistic event sequence: keyboard + mouse events
static constexpr auto make_events() noexcept {
    return std::array<user_event, 20>{{
        // Key press
        user_event{EV_KEY, KEY_A, 1}, user_event{EV_SYN, SYN_REPORT, 0},
        // Key release
        user_event{EV_KEY, KEY_A, 0}, user_event{EV_SYN, SYN_REPORT, 0},
        // Mouse movement
        user_event{EV_REL, REL_X, 5}, user_event{EV_REL, REL_Y, 3}, user_event{EV_SYN, SYN_REPORT, 0},
        user_event{EV_REL, REL_X, 10}, user_event{EV_REL, REL_Y, 7}, user_event{EV_SYN, SYN_REPORT, 0},
        // Mouse click
        user_event{EV_KEY, BTN_LEFT, 1}, user_event{EV_SYN, SYN_REPORT, 0},
        user_event{EV_KEY, BTN_LEFT, 0}, user_event{EV_SYN, SYN_REPORT, 0},
        // More mouse movement
        user_event{EV_REL, REL_X, -3}, user_event{EV_REL, REL_Y, 2}, user_event{EV_SYN, SYN_REPORT, 0},
        user_event{EV_REL, REL_X, 15}, user_event{EV_REL, REL_Y, -5}, user_event{EV_SYN, SYN_REPORT, 0},
    }};
}

int main() {
    constexpr std::size_t iterations = 1000;

    std::fprintf(stderr, "Pipeline Benchmarks — compare across runs\n");
    std::fprintf(stderr, "==========================================\n\n");

    // --- Passthrough ---
    {
        std::fprintf(stderr, "--- passthrough (%zu iterations) ---\n", iterations);
        for (std::size_t i = 0; i < iterations; ++i) {
            auto pipeline = context | emit_all[make_events()] | record;
            pipeline();
        }
    }

    // --- Debounce ---
    {
        std::fprintf(stderr, "--- debounce (%zu iterations) ---\n", iterations);
        for (std::size_t i = 0; i < iterations; ++i) {
            auto pipeline = context | emit_all[make_events()] | debounce[BTN_LEFT] | record;
            pipeline();
        }
    }

    // --- Drop filters ---
    {
        std::fprintf(stderr, "--- drop_mouse_moves + drop_msc_scan (%zu iterations) ---\n", iterations);
        for (std::size_t i = 0; i < iterations; ++i) {
            auto pipeline = context | emit_all[make_events()] | drop_mouse_moves | drop_msc_scan | record;
            pipeline();
        }
    }

    // --- Lerp ---
    {
        std::fprintf(stderr, "--- lerp[8] (%zu iterations) ---\n", iterations);
        for (std::size_t i = 0; i < iterations; ++i) {
            auto pipeline = context | emit_all[make_events()] | lerp[8] | record;
            pipeline();
        }
    }

    // --- Low-pass filter ---
    {
        std::fprintf(stderr, "--- low_pass_filter[0.5] (%zu iterations) ---\n", iterations);
        for (std::size_t i = 0; i < iterations; ++i) {
            auto pipeline = context | emit_all[make_events()] | low_pass_filter[0.5f] | record;
            pipeline();
        }
    }

    // --- Full pipeline ---
    {
        std::fprintf(stderr, "--- full (debounce + drop + lerp) (%zu iterations) ---\n", iterations);
        for (std::size_t i = 0; i < iterations; ++i) {
            auto pipeline = context
                | emit_all[make_events()]
                | debounce[BTN_LEFT]
                | drop_mouse_moves
                | drop_msc_scan
                | lerp[8]
                | record;
            pipeline();
        }
    }

    // --- Instrumented pipeline (with benchmark_all overhead) ---
    {
        std::fprintf(stderr, "--- instrumented (benchmark_all) (%zu iterations) ---\n", iterations);
        for (std::size_t i = 0; i < iterations; ++i) {
            auto pipeline = context
                | emit_all[make_events()]
                | benchmark_all[context | debounce[BTN_LEFT] | drop_mouse_moves | lerp[8] | record];
            pipeline();
        }
    }

    std::fprintf(stderr, "\nDone. Use `time ./bench-pipeline` for wall-clock timing.\n");
    return 0;
}
