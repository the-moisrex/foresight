// Pipeline-level benchmark: measures real-world event processing latency
// through a realistic pipeline with multiple mods.
//
// Run: time ./bench-pipeline
// Compare output across runs to track performance changes.

#include <array>
#include <cstdio>
#include <linux/input-event-codes.h>

import fs8.mods;

using namespace fs8;

// Large mouse movement sequence (simulates real desktop usage)
static constexpr auto mouse_sequence = std::array<user_event, 60>{
  {
   user_event{EV_REL, REL_X, 15},  user_event{EV_REL, REL_Y, 8},   user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_REL, REL_X, 20},  user_event{EV_REL, REL_Y, 12},  user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_REL, REL_X, -5},  user_event{EV_REL, REL_Y, 3},   user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_REL, REL_X, 30},  user_event{EV_REL, REL_Y, -10}, user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_REL, REL_X, -15}, user_event{EV_REL, REL_Y, -8},  user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_REL, REL_X, 8},   user_event{EV_REL, REL_Y, 20},  user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_REL, REL_X, -3},  user_event{EV_REL, REL_Y, 5},   user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_REL, REL_X, 25},  user_event{EV_REL, REL_Y, -15}, user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_REL, REL_X, 10},  user_event{EV_REL, REL_Y, 2},   user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_REL, REL_X, -8},  user_event{EV_REL, REL_Y, 12},  user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_REL, REL_X, 12},  user_event{EV_REL, REL_Y, -3},  user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_REL, REL_X, -20}, user_event{EV_REL, REL_Y, 7},   user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_REL, REL_X, 5},   user_event{EV_REL, REL_Y, -12}, user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_REL, REL_X, -10}, user_event{EV_REL, REL_Y, 18},  user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_REL, REL_X, 35},  user_event{EV_REL, REL_Y, -5},  user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_REL, REL_X, -25}, user_event{EV_REL, REL_Y, 10},  user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_REL, REL_X, 7},   user_event{EV_REL, REL_Y, -20}, user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_REL, REL_X, 18},  user_event{EV_REL, REL_Y, 3},   user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_REL, REL_X, -12}, user_event{EV_REL, REL_Y, -7},  user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_REL, REL_X, 40},  user_event{EV_REL, REL_Y, 15},  user_event{EV_SYN, SYN_REPORT, 0},
   }
};

// Mixed keyboard + mouse sequence
static constexpr auto mixed_sequence = std::array<user_event, 48>{
  {
   // Type 'h'
    user_event{EV_KEY, KEY_H, 1},
   user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_KEY, KEY_H, 0},
   user_event{EV_SYN, SYN_REPORT, 0},
   // Mouse move
    user_event{EV_REL, REL_X, 10},
   user_event{EV_REL, REL_Y, 5},
   user_event{EV_SYN, SYN_REPORT, 0},
   // Click
    user_event{EV_KEY, BTN_LEFT, 1},
   user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_KEY, BTN_LEFT, 0},
   user_event{EV_SYN, SYN_REPORT, 0},
   // Type 'e'
    user_event{EV_KEY, KEY_E, 1},
   user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_KEY, KEY_E, 0},
   user_event{EV_SYN, SYN_REPORT, 0},
   // Mouse move
    user_event{EV_REL, REL_X, -5},
   user_event{EV_REL, REL_Y, 15},
   user_event{EV_SYN, SYN_REPORT, 0},
   // Click
    user_event{EV_KEY, BTN_LEFT, 1},
   user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_KEY, BTN_LEFT, 0},
   user_event{EV_SYN, SYN_REPORT, 0},
   // Type 'l'
    user_event{EV_KEY, KEY_L, 1},
   user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_KEY, KEY_L, 0},
   user_event{EV_SYN, SYN_REPORT, 0},
   // Mouse move
    user_event{EV_REL, REL_X, 20},
   user_event{EV_REL, REL_Y, -10},
   user_event{EV_SYN, SYN_REPORT, 0},
   // Click
    user_event{EV_KEY, BTN_LEFT, 1},
   user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_KEY, BTN_LEFT, 0},
   user_event{EV_SYN, SYN_REPORT, 0},
   // Type 'l'
    user_event{EV_KEY, KEY_L, 1},
   user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_KEY, KEY_L, 0},
   user_event{EV_SYN, SYN_REPORT, 0},
   // Type 'o'
    user_event{EV_KEY, KEY_O, 1},
   user_event{EV_SYN, SYN_REPORT, 0},
   user_event{EV_KEY, KEY_O, 0},
   user_event{EV_SYN, SYN_REPORT, 0},
   }
};

int main() {
    constexpr std::size_t iterations = 1000;

    std::fprintf(stderr, "Pipeline Benchmarks — compare across runs\n");
    std::fprintf(stderr, "Usage: time ./bench-pipeline\n");
    std::fprintf(stderr, "==========================================\n\n");

    // --- Passthrough ---
    {
        std::fprintf(stderr, "--- passthrough (%zu iterations) ---\n", iterations);
        for (std::size_t i = 0; i < iterations; ++i) {
            auto pipeline = context | emit_all[mouse_sequence] | record;
            pipeline();
        }
    }

    // --- Debounce ---
    {
        std::fprintf(stderr, "--- debounce (%zu iterations) ---\n", iterations);
        for (std::size_t i = 0; i < iterations; ++i) {
            auto pipeline = context | emit_all[mixed_sequence] | debounce[BTN_LEFT] | record;
            pipeline();
        }
    }

    // --- Drop filters ---
    {
        std::fprintf(stderr, "--- drop_mouse_moves + drop_msc_scan (%zu iterations) ---\n", iterations);
        for (std::size_t i = 0; i < iterations; ++i) {
            auto pipeline = context | emit_all[mouse_sequence] | drop_mouse_moves | drop_msc_scan | record;
            pipeline();
        }
    }

    // --- Lerp ---
    {
        std::fprintf(stderr, "--- lerp[8] (%zu iterations) ---\n", iterations);
        for (std::size_t i = 0; i < iterations; ++i) {
            auto pipeline = context | emit_all[mouse_sequence] | lerp[8] | record;
            pipeline();
        }
    }

    // --- Low-pass filter ---
    {
        std::fprintf(stderr, "--- low_pass_filter[0.5] (%zu iterations) ---\n", iterations);
        for (std::size_t i = 0; i < iterations; ++i) {
            auto pipeline = context | emit_all[mouse_sequence] | low_pass_filter[0.5f] | record;
            pipeline();
        }
    }

    // --- Full pipeline ---
    {
        std::fprintf(stderr, "--- full (debounce + drop + lerp) (%zu iterations) ---\n", iterations);
        for (std::size_t i = 0; i < iterations; ++i) {
            auto pipeline = context | emit_all[mixed_sequence] | debounce[BTN_LEFT] | drop_mouse_moves | drop_msc_scan | lerp[8] | record;
            pipeline();
        }
    }

    // --- Instrumented pipeline (with benchmark_all overhead) ---
    {
        std::fprintf(stderr, "--- instrumented (benchmark_all) (%zu iterations) ---\n", iterations);
        for (std::size_t i = 0; i < iterations; ++i) {
            auto pipeline =
              context | emit_all[mixed_sequence] | benchmark_all[context | debounce[BTN_LEFT] | drop_mouse_moves | lerp[8] | record];
            pipeline();
        }
    }

    std::fprintf(stderr, "\nDone.\n");
    return 0;
}
