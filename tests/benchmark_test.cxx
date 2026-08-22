// Created by moisrex on 8/22/26.

#include "./common/tests_common_pch.hpp"

#include <format>
#include <linux/input-event-codes.h>
#include <tuple>

import fs8.mods;

using namespace fs8;

namespace {
    std::uint64_t reported_calls = 0; // NOLINT(*-global-variables)

    constexpr auto capture_benchmark = []<typename... Args>(std::format_string<Args...>, Args&&... args) noexcept {
        reported_calls = std::get<0>(std::tuple{std::forward<Args>(args)...});
    };
} // namespace

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

TEST(BenchmarkTest, ResultActionReportsBenchmarks) {
    reported_calls = 0;

    (context
     | emit_all[{
       {.type = EV_KEY, .code = KEY_A, .value = 0}
    }]
     | benchmark[context | record]
     | on[keyup[KEY_A], benchmark_result[capture_benchmark]])();

    EXPECT_EQ(reported_calls, 1U);
}
