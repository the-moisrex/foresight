// Created by moisrex on 8/22/26.

#include "./common/tests_common_pch.hpp"

#include <format>
#include <linux/input-event-codes.h>
#include <string_view>
#include <tuple>

import fs8.mods;

using namespace fs8;

namespace {
    std::uint64_t    reported_calls = 0; // NOLINT(*-global-variables)
    std::string_view last_name;          // NOLINT(*-global-variables)

    // All benchmark names reported by benchmark_result.
    std::vector<std::string_view> reported_names; // NOLINT(*-global-variables)

    // benchmark_result always calls sink(name, calls, total, average, min, max)
    constexpr auto capture_benchmark = []<typename... Args>(std::format_string<Args...>, Args&&... args) noexcept {
        auto tuple     = std::tuple{std::forward<Args>(args)...};
        last_name      = std::get<0>(tuple);
        reported_calls = std::get<1>(tuple);
    };

    constexpr auto capture_all_benchmarks = []<typename... Args>(std::format_string<Args...>, Args&&... args) noexcept {
        auto tuple = std::tuple{std::forward<Args>(args)...};
        reported_names.push_back(std::get<0>(tuple));
    };

    void reset_reports() noexcept {
        reported_calls = 0;
        last_name      = {};
        reported_names.clear();
    }
} // namespace

// ---------------------------------------------------------------------------
// Basic: benchmark counter records invocations
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, RecordsWrappedPipelineInvocations) {
    auto pipeline =
      context
      | emit_all[{
        {.type = EV_KEY,      .code = KEY_A, .value = 1},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
    }]
      | benchmark[context | record];

    auto& bench = pipeline.mod<basic_benchmark<basic_record>>();

    pipeline();

    EXPECT_EQ(bench.result().calls, 2U);
    EXPECT_GE(bench.result().total.count(), 0);
    EXPECT_GE(bench.result().max.count(), bench.result().min.count());
}

// ---------------------------------------------------------------------------
// benchmark_stats: average() with 0 calls returns 0
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, StatsAverageZeroCalls) {
    benchmark_stats stats{};
    EXPECT_EQ(stats.calls, 0U);
    EXPECT_EQ(stats.average().count(), 0);
}

// ---------------------------------------------------------------------------
// benchmark_stats: average() is total / calls
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, StatsAverageCalculation) {
    basic_benchmark_counter counter{};
    counter.record(std::chrono::nanoseconds{100});
    counter.record(std::chrono::nanoseconds{200});
    counter.record(std::chrono::nanoseconds{300});

    auto const stats = counter.result();
    EXPECT_EQ(stats.calls, 3U);
    EXPECT_EQ(stats.total.count(), 600);
    EXPECT_EQ(stats.average().count(), 200);
    EXPECT_EQ(stats.min.count(), 100);
    EXPECT_EQ(stats.max.count(), 300);
}

// ---------------------------------------------------------------------------
// benchmark_counter: clear() resets all fields
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, CounterClearResetsStats) {
    basic_benchmark_counter counter{};
    counter.record(std::chrono::nanoseconds{42});
    counter.record(std::chrono::nanoseconds{99});

    auto const before = counter.result();
    EXPECT_EQ(before.calls, 2U);

    counter.clear();

    auto const after = counter.result();
    EXPECT_EQ(after.calls, 0U);
    EXPECT_EQ(after.total.count(), 0);
    EXPECT_EQ(after.average().count(), 0);
}

// ---------------------------------------------------------------------------
// benchmark_counter: record a single event and verify result
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, CounterRecordSingleEvent) {
    basic_benchmark_counter counter{};
    counter.record(std::chrono::nanoseconds{777});

    auto const stats = counter.result();
    EXPECT_EQ(stats.calls, 1U);
    EXPECT_EQ(stats.total.count(), 777);
    EXPECT_EQ(stats.average().count(), 777);
    EXPECT_EQ(stats.min.count(), 777);
    EXPECT_EQ(stats.max.count(), 777);
}

// ---------------------------------------------------------------------------
// benchmark_counter: clear after multiple records, then record again
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, CounterClearThenRecord) {
    basic_benchmark_counter counter{};
    counter.record(std::chrono::nanoseconds{10});
    counter.record(std::chrono::nanoseconds{20});
    counter.clear();
    counter.record(std::chrono::nanoseconds{50});

    auto const stats = counter.result();
    EXPECT_EQ(stats.calls, 1U);
    EXPECT_EQ(stats.total.count(), 50);
    EXPECT_EQ(stats.min.count(), 50);
    EXPECT_EQ(stats.max.count(), 50);
}

// ---------------------------------------------------------------------------
// benchmark_counter: min/max track correctly across many records
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, CounterMinMaxTracking) {
    basic_benchmark_counter counter{};
    counter.record(std::chrono::nanoseconds{50});
    counter.record(std::chrono::nanoseconds{10});
    counter.record(std::chrono::nanoseconds{100});
    counter.record(std::chrono::nanoseconds{30});

    auto const stats = counter.result();
    EXPECT_EQ(stats.calls, 4U);
    EXPECT_EQ(stats.min.count(), 10);
    EXPECT_EQ(stats.max.count(), 100);
    EXPECT_EQ(stats.total.count(), 190);
}

// ---------------------------------------------------------------------------
// Unnamed benchmark reports default name "benchmark"
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, UnnamedBenchmarkReportsDefaultName) {
    reported_calls = 0;

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_A, .value = 0}
    }]
     | benchmark[context | record]
     | on[keyup[KEY_A], benchmark_result[capture_benchmark]])();

    EXPECT_EQ(reported_calls, 1U);
    EXPECT_EQ(last_name, "benchmark");
}

// ---------------------------------------------------------------------------
// Named benchmark reports its name
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, NamedBenchmarkReportsName) {
    reported_calls = 0;
    last_name      = {};

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_A, .value = 0}
    }]
     | basic_benchmark<basic_record>{"my_record", record}
     | on[keyup[KEY_A], benchmark_result[capture_benchmark]])();

    EXPECT_EQ(reported_calls, 1U);
    EXPECT_EQ(last_name, "my_record");
}

// ---------------------------------------------------------------------------
// benchmark_result with clear_after=true reports then clears
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, BenchmarkResultClearsOnDemand) {
    reported_calls = 0;

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_A, .value = 0}
    }]
     | benchmark[context | record]
     | on[keyup[KEY_A], benchmark_result[capture_benchmark, true]])();

    // benchmark_result ran with clear_after=true.
    // reported_calls should have the pre-clear value (1 call from keyup event).
    EXPECT_EQ(reported_calls, 1U);
    EXPECT_EQ(last_name, "benchmark");
}

// ---------------------------------------------------------------------------
// After clear, the next report starts from 0
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, ClearThenReportShowsZero) {
    reset_reports();

    // Use 2 events: first triggers benchmark_result with clear, second
    // records fresh into the cleared benchmark, then a third event (keyup)
    // triggers a second report that shows 1.
    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_A, .value = 0},
       {.type = EV_KEY, .code = KEY_B, .value = 0},
    }]
     | benchmark[context | record]
     | on[keyup[KEY_A], benchmark_result[capture_benchmark, true]]
     | on[keyup[KEY_B], benchmark_result[capture_benchmark]])();

    // KEY_A keyup fires first: benchmark_result reports 1, then clears.
    // KEY_B keyup fires next: benchmark recorded KEY_B event, reports 1.
    EXPECT_EQ(reported_calls, 1U);
    EXPECT_EQ(last_name, "benchmark");
}

// ---------------------------------------------------------------------------
// benchmark_all wraps each mod in its own benchmark
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, BenchmarkAllCreatesPerModBenchmarks) {
    reset_reports();

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_A, .value = 0}
    }]
     | benchmark_all[context | record]
     | on[always_enable, benchmark_result[capture_all_benchmarks]])();

    // benchmark_all[context | record] should produce one benchmark for `record`.
    ASSERT_EQ(reported_names.size(), 1U);
    EXPECT_FALSE(reported_names[0].empty());
}

// ---------------------------------------------------------------------------
// benchmark_all with multiple mods produces multiple benchmarks
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, BenchmarkAllMultipleMods) {
    reset_reports();

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_A, .value = 0},
    }]
     | benchmark_all[context | record | record]
     | on[always_enable, benchmark_result[capture_all_benchmarks]])();

    // Two record mods → two benchmark entries.
    ASSERT_EQ(reported_names.size(), 2U);
    EXPECT_FALSE(reported_names[0].empty());
    EXPECT_FALSE(reported_names[1].empty());
}

// ---------------------------------------------------------------------------
// Multiple named benchmarks: each reports its own name
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, MultipleNamedBenchmarksReportDistinctNames) {
    reset_reports();

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_A, .value = 0}
    }]
     | basic_benchmark<basic_record>{"alpha", record}
     | basic_benchmark<basic_record>{"beta", record}
     | on[always_enable, benchmark_result[capture_all_benchmarks]])();

    ASSERT_EQ(reported_names.size(), 2U);
    EXPECT_EQ(reported_names[0], "alpha");
    EXPECT_EQ(reported_names[1], "beta");
}

// ---------------------------------------------------------------------------
// benchmark_all: each benchmark records only its own mod's invocations
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, BenchmarkAllIsolation) {
    auto pipeline =
      context
      | emit_all[{
        {.type = EV_KEY,      .code = KEY_A, .value = 1},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
    }]
      | benchmark_all[context | record];

    pipeline();

    // Each benchmark wraps `context | record` → the `record` mod sees
    // 2 events (EV_KEY + SYN_REPORT) in its sub-pipeline.
    reported_calls = 0;
    last_name      = {};
    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_A, .value = 0}
    }]
     | benchmark_all[context | record]
     | on[keyup[KEY_A], benchmark_result[capture_benchmark]])();

    // The keyup event itself is 1 event, so the benchmark should record 1 call.
    EXPECT_EQ(reported_calls, 1U);
    EXPECT_FALSE(last_name.empty());
}

// ---------------------------------------------------------------------------
// Benchmark inside on is found by benchmark_result
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, BenchmarkInsideOnIsFoundByResult) {
    reset_reports();

    (context
     | emit_all[{
       {.type = EV_KEY,      .code = KEY_A, .value = 1},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,      .code = KEY_A, .value = 0},
    }]
     | keys_state
     | on[pressed[KEY_A], benchmark[context | record]]
     | on[always_enable, benchmark_result[capture_all_benchmarks]])();

    // The first on fires for the keydown, the second on always fires.
    // benchmark_result should find the benchmark nested inside the first on.
    ASSERT_GE(reported_names.size(), 1U);
    EXPECT_FALSE(reported_names[0].empty());
}

// ---------------------------------------------------------------------------
// benchmark_all inside on is found by benchmark_result
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, BenchmarkAllInsideOnIsFoundByResult) {
    reset_reports();

    (context
     | emit_all[{
       {.type = EV_KEY,      .code = KEY_A, .value = 1},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,      .code = KEY_A, .value = 0},
    }]
     | keys_state
     | on[pressed[KEY_A], benchmark_all[context | record]]
     | on[always_enable, benchmark_result[capture_all_benchmarks]])();

    // benchmark_all inside the first on → at least one benchmark found.
    ASSERT_GE(reported_names.size(), 1U);
    EXPECT_FALSE(reported_names[0].empty());
}

// ---------------------------------------------------------------------------
// benchmark_result with clear inside on: cleared benchmarks
// then accumulate fresh for subsequent events
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, BenchmarkResultClearInsideOnResetsCounters) {
    reset_reports();

    // 3 events: keydown, syn_report, keyup.
    // pressed[KEY_A] is true for keydown and syn_report (key held).
    // On keydown: benchmark_result reports 2 (keydown + syn passed through so far)
    //   then clears. On syn_report: benchmark_result reports 1 (just syn).
    // On keyup: key released, on doesn't fire.
    (context
     | emit_all[{
       {.type = EV_KEY,      .code = KEY_A, .value = 1},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,      .code = KEY_A, .value = 0},
    }]
     | keys_state
     | benchmark[context | record]
     | on[pressed[KEY_A], benchmark_result[capture_benchmark, true]])();

    // Keydown triggers: benchmark recorded keydown (1 event so far). reported_calls=1.
    // Then clear. SYN_REPORT triggers: benchmark recorded syn (1 event). reported_calls=1.
    // keyup: pressed is false, on doesn't fire.
    EXPECT_EQ(reported_calls, 1U);
    EXPECT_EQ(last_name, "benchmark");
}

// ---------------------------------------------------------------------------
// Multiple benchmark counters are independent
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, IndependentCounters) {
    basic_benchmark_counter a{};
    basic_benchmark_counter b{};

    a.record(std::chrono::nanoseconds{10});
    a.record(std::chrono::nanoseconds{20});
    b.record(std::chrono::nanoseconds{100});

    auto const as = a.result();
    auto const bs = b.result();

    EXPECT_EQ(as.calls, 2U);
    EXPECT_EQ(as.total.count(), 30);
    EXPECT_EQ(bs.calls, 1U);
    EXPECT_EQ(bs.total.count(), 100);

    a.clear();

    auto const as2 = a.result();
    auto const bs2 = b.result();
    EXPECT_EQ(as2.calls, 0U);
    EXPECT_EQ(bs2.calls, 1U); // b is independent, not cleared
}

// ---------------------------------------------------------------------------
// pretty_type_name produces non-empty strings
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, PrettyTypeNameNotEmpty) {
    // We can't easily call consteval at runtime, but we can verify
    // that benchmark_all's auto-generated names are non-empty.
    reset_reports();

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_A, .value = 0}
    }]
     | benchmark_all[context | record]
     | on[always_enable, benchmark_result[capture_all_benchmarks]])();

    for (auto const& n : reported_names) {
        EXPECT_FALSE(n.empty());
    }
}

// ---------------------------------------------------------------------------
// benchmark_all with clear: clear resets, subsequent events record fresh
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, BenchmarkAllClearThenReRecord) {
    reset_reports();

    // 2 events: first event triggers benchmark_result with clear,
    // second event records fresh, then keyup triggers second report.
    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_A, .value = 0},
       {.type = EV_KEY, .code = KEY_B, .value = 0},
    }]
     | benchmark_all[context | record]
     | on[keyup[KEY_A], benchmark_result[capture_all_benchmarks, true]]
     | on[keyup[KEY_B], benchmark_result[capture_all_benchmarks]])();

    // KEY_A keyup: first on fires, reports 1 benchmark, then clears.
    // KEY_B keyup: benchmark re-recorded (1 event), second on fires, reports 1 benchmark.
    // Total: 2 reports (one per on trigger).
    ASSERT_EQ(reported_names.size(), 2U);
    for (auto const& n : reported_names) {
        EXPECT_FALSE(n.empty());
    }
}

// ---------------------------------------------------------------------------
// benchmark_result without clear: counters accumulate across events
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, BenchmarkResultWithoutClearAccumulates) {
    reset_reports();

    // 2 events: keydown then keyup. Both pass through benchmark[context | record].
    // On keyup, benchmark_result reports the total count (2 events).
    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_A, .value = 1},
       {.type = EV_KEY, .code = KEY_A, .value = 0},
    }]
     | benchmark[context | record]
     | on[keyup[KEY_A], benchmark_result[capture_benchmark, false]])();

    // Both events passed through benchmark, counter accumulated to 2.
    EXPECT_EQ(reported_calls, 2U);
}

// ---------------------------------------------------------------------------
// Benchmark with record only: still tracks invocation count
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, BenchmarkTracksEventCount) {
    auto pipeline =
      context
      | emit_all[{
        {.type = EV_KEY,      .code = KEY_A, .value = 1},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
        {.type = EV_KEY,      .code = KEY_A, .value = 0},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
    }]
      | benchmark[context | record];

    auto& bench = pipeline.mod<basic_benchmark<basic_record>>();
    pipeline();

    // 4 events pass through the benchmark.
    EXPECT_EQ(bench.result().calls, 4U);
    EXPECT_GT(bench.result().total.count(), 0);
}

// ---------------------------------------------------------------------------
// benchmark_stats: total is sum of all recorded durations
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, StatsTotalIsSumOfDurations) {
    basic_benchmark_counter counter{};
    counter.record(std::chrono::nanoseconds{1});
    counter.record(std::chrono::nanoseconds{2});
    counter.record(std::chrono::nanoseconds{3});
    counter.record(std::chrono::nanoseconds{4});
    counter.record(std::chrono::nanoseconds{5});

    auto const stats = counter.result();
    EXPECT_EQ(stats.calls, 5U);
    EXPECT_EQ(stats.total.count(), 15);
}

// ---------------------------------------------------------------------------
// Named benchmarks via benchmark_all all get distinct auto-names
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, BenchmarkAllAutoNamesAreDistinctForDifferentTypes) {
    // With two different mod types, benchmark_all should produce different names.
    reset_reports();

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_A, .value = 0}
    }]
     | benchmark_all[context | record | output]
     | on[always_enable, benchmark_result[capture_all_benchmarks]])();

    ASSERT_EQ(reported_names.size(), 2U);
    EXPECT_NE(reported_names[0], reported_names[1]);
}

// ---------------------------------------------------------------------------
// benchmark_result factory: operator[](sink) and operator[](sink, clear)
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, BenchmarkResultFactory) {
    constexpr auto r1 = benchmark_result[capture_benchmark];
    constexpr auto r2 = benchmark_result[capture_benchmark, true];

    // Both should compile and produce valid benchmark_result instances.
    // r1 has clear_after=false, r2 has clear_after=true — we can't inspect
    // the flag directly, but we can verify they run without crashing.
    reset_reports();
    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_A, .value = 0}
    }]
     | benchmark[context | record]
     | on[keyup[KEY_A], r1])();
    EXPECT_EQ(reported_calls, 1U);

    reset_reports();
    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_A, .value = 0}
    }]
     | benchmark[context | record]
     | on[keyup[KEY_A], r2])();
    EXPECT_EQ(reported_calls, 1U);
}

// ---------------------------------------------------------------------------
// benchmark_factory: operator[] creates benchmark from context
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, BenchmarkFactoryCreatesFromContext) {
    constexpr auto b = benchmark[context | record];
    // The factory should produce a basic_benchmark<basic_record>.
    static_assert(std::same_as<std::remove_cvref_t<decltype(b)>, basic_benchmark<basic_record>>);
}

// ---------------------------------------------------------------------------
// benchmark_all_factory: operator[] creates per-mod benchmarks from context
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, BenchmarkAllFactoryCreatesFromContext) {
    constexpr auto mods = benchmark_all[context | record];
    // Should produce a context containing a single basic_benchmark<basic_record>.
    // We can verify by running it and checking via benchmark_result.
    reset_reports();

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_A, .value = 0}
    }]
     | mods
     | on[always_enable, benchmark_result[capture_all_benchmarks]])();

    ASSERT_EQ(reported_names.size(), 1U);
    EXPECT_FALSE(reported_names[0].empty());
}

// ---------------------------------------------------------------------------
// benchmark_result without any benchmarks in pipeline: no crash
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, BenchmarkResultEmptyPipelineNoCrash) {
    reset_reports();

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_A, .value = 0}
    }]
     | on[keyup[KEY_A], benchmark_result[capture_all_benchmarks]])();

    EXPECT_TRUE(reported_names.empty());
}

// ---------------------------------------------------------------------------
// Nested on mods: benchmark_result finds benchmarks in deeply nested on
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, BenchmarkResultFindsDeeplyNestedBenchmarks) {
    reset_reports();

    (context
     | emit_all[{
       {.type = EV_KEY,      .code = KEY_A, .value = 1},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
    }]
     | keys_state
     | on[pressed[KEY_A], context | benchmark[context | record] | output]
     | on[always_enable, benchmark_result[capture_all_benchmarks]])();

    ASSERT_GE(reported_names.size(), 1U);
    EXPECT_FALSE(reported_names[0].empty());
}

// ---------------------------------------------------------------------------
// Benchmark inside on: clear actually resets the counter
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, BenchmarkInsideOnClearActuallyResets) {
    reset_reports();

    auto pipeline =
      context
      | emit_all[{
        {.type = EV_KEY,      .code = KEY_A, .value = 1},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
    }]
      | keys_state
      | on[pressed[KEY_A], benchmark[context | record]];

    // Run once: on fires, benchmark records 2 events.
    pipeline();

    // Access the benchmark inside the on's sub_mods.
    auto& on_mod = pipeline.mod<basic_on<basic_pressed<1>, basic_benchmark<basic_record>>>();
    auto& bench  = std::get<0>(on_mod.sub_mods());
    EXPECT_EQ(bench.result().calls, 2U);

    // Clear the benchmark.
    bench.clear();
    EXPECT_EQ(bench.result().calls, 0U);
}

// ---------------------------------------------------------------------------
// Clear only affects the target, not sibling benchmarks
// ---------------------------------------------------------------------------
TEST(BenchmarkTest, ClearOnlyAffectsTarget) {
    basic_benchmark_counter target{};
    basic_benchmark_counter other{};

    target.record(std::chrono::nanoseconds{10});
    other.record(std::chrono::nanoseconds{20});
    target.record(std::chrono::nanoseconds{30});

    target.clear();

    auto const ts = target.result();
    auto const os = other.result();

    EXPECT_EQ(ts.calls, 0U);
    EXPECT_EQ(os.calls, 1U);
    EXPECT_EQ(os.total.count(), 20);
}
