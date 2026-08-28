#include "./common/tests_common_pch.hpp"

#include <array>
#include <chrono>
#include <linux/input-event-codes.h>
#include <sys/time.h>

import fs8.mods;

namespace {
    using namespace std::chrono_literals; // NOLINT(*-using-namespace)

    consteval fs8::event_type timed_ev(
      fs8::event_type::type_type const  type,
      fs8::event_type::code_type const  code,
      fs8::event_type::value_type const value,
      std::chrono::microseconds const   us) {
        fs8::event_type ev{type, code, value};
        timeval         t{};
        t.tv_sec  = static_cast<time_t>(us.count() / 1'000'000);
        t.tv_usec = static_cast<suseconds_t>(us.count() % 1'000'000);
        ev.time(t);
        return ev;
    }

    template <std::size_t N>
    struct timed_sequence {
        std::array<fs8::event_type, N> events{};
        std::size_t                    index = 0;

        explicit constexpr timed_sequence(std::array<fs8::event_type, N> evs) noexcept : events{evs} {}

        template <fs8::Context CtxT>
        fs8::context_action operator()(CtxT& ctx, fs8::load_event_tag) noexcept {
            if (index == N) {
                return fs8::context_action::exit;
            }
            ctx.event() = events[index++];
            return fs8::context_action::next;
        }
    };
} // namespace

TEST(SanitizerTest, FiltersAdjacentSynReports) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    auto pipeline = context
                  | timed_sequence{std::array{
                      timed_ev(EV_SYN, SYN_REPORT, 0, 0us),
                      timed_ev(EV_SYN, SYN_REPORT, 0, 1ms),
                      timed_ev(EV_KEY, KEY_A, 1, 2ms),
                    }}
                  | event_sanitizer
                  | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 2U);
    EXPECT_EQ(col[0].type(), EV_SYN);
    EXPECT_EQ(col[1].code(), KEY_A);
}

TEST(SanitizerTest, FiltersOrphanReleaseButKeepsMatchedRelease) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    auto pipeline = context
                  | timed_sequence{std::array{
                      timed_ev(EV_KEY, KEY_A, 0, 0us),
                      timed_ev(EV_KEY, KEY_A, 1, 1ms),
                      timed_ev(EV_KEY, KEY_A, 0, 2ms),
                    }}
                  | event_sanitizer
                  | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 2U);
    EXPECT_EQ(col[0].value(), 1);
    EXPECT_EQ(col[1].value(), 0);
}

TEST(SanitizerTest, AppliesConfiguredBigJumpThreshold) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    auto pipeline = context
                  | timed_sequence{std::array{
                      timed_ev(EV_REL, REL_X, 15, 0us),
                      timed_ev(EV_REL, REL_Y, 25, 1ms),
                    }}
                  | event_sanitizer.threshold(20)
                  | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 1U);
    EXPECT_EQ(col[0].code(), REL_X);
}

TEST(SanitizerTest, DisabledChecksPassEventsThrough) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    auto pipeline = context
                  | timed_sequence{std::array{
                      timed_ev(EV_SYN, SYN_REPORT, 0, 0us),
                      timed_ev(EV_SYN, SYN_REPORT, 0, 1ms),
                      timed_ev(EV_KEY, KEY_A, 0, 2ms),
                    }}
                  | event_sanitizer.adjacent_syns(false).orphan_releases(false)
                  | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    EXPECT_EQ(col.size(), 3U);
}

TEST(SanitizerTest, DiagnosticsModeReportsButKeepsBadEvents) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    auto pipeline = context
                  | timed_sequence{std::array{
                      timed_ev(EV_SYN, SYN_REPORT, 0, 0us),
                      timed_ev(EV_SYN, SYN_REPORT, 0, 1ms),
                      timed_ev(EV_KEY, KEY_A, 1, 2ms),
                    }}
                  | event_diagnostics
                  | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 3U);
    EXPECT_EQ(col[0].type(), EV_SYN);
    EXPECT_EQ(col[1].type(), EV_SYN);
    EXPECT_EQ(col[2].code(), KEY_A);
}

TEST(SanitizerTest, DiagnosticsShorthandAcceptsCallback) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    struct callback {
        constexpr void operator()(event_type const&, sanitizer_issue) noexcept {}
    };

    auto pipeline = context
                  | timed_sequence{std::array{
                      timed_ev(EV_SYN, SYN_REPORT, 0, 0us),
                      timed_ev(EV_SYN, SYN_REPORT, 0, 1ms),
                    }}
                  | event_diagnostics[callback{}]
                  | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 2U);
}

TEST(SanitizerTest, DetectsMissingSynTime) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    // The first data event after a SYN passes through (idle gap, not a missing SYN).
    // The second data event in the same frame triggers missing_syn_time.
    auto pipeline = context
                  | timed_sequence{std::array{
                      timed_ev(EV_SYN, SYN_REPORT, 0, 0us),
                      timed_ev(EV_REL, REL_X, 10, 1ms),
                      timed_ev(EV_REL, REL_X, 10, 200ms),
                    }}
                  | event_sanitizer.missing_syn_count(false).missing_syn_travel(false)
                    .missing_syn_time_threshold(100ms)
                  | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 2U);
    EXPECT_EQ(col[0].type(), EV_SYN);
    EXPECT_EQ(col[0].code(), SYN_REPORT);
    EXPECT_EQ(col[1].code(), REL_X);
}

TEST(SanitizerTest, IdleGapDoesNotTriggerMissingSynTime) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    // A data event arriving long after the last SYN is a normal idle gap, not a
    // missing-SYN issue. The first data event after a SYN should pass through
    // even if the time gap exceeds the threshold.
    auto pipeline = context
                  | timed_sequence{std::array{
                      timed_ev(EV_SYN, SYN_REPORT, 0, 0us),
                      timed_ev(EV_KEY, KEY_A, 1, 200ms),
                      timed_ev(EV_SYN, SYN_REPORT, 0, 201ms),
                    }}
                  | event_sanitizer.missing_syn_count(false).missing_syn_travel(false)
                    .missing_syn_time_threshold(100ms)
                  | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 3U);
    EXPECT_EQ(col[0].type(), EV_SYN);
    EXPECT_EQ(col[0].code(), SYN_REPORT);
    EXPECT_EQ(col[1].code(), KEY_A);
    EXPECT_EQ(col[2].type(), EV_SYN);
    EXPECT_EQ(col[2].code(), SYN_REPORT);
}

TEST(SanitizerTest, MultiEventFrameAfterIdleGapDoesNotTriggerMissingSynTime) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    // Simulates a tablet frame: REL_X + REL_Y + SYN_REPORT. After an idle gap,
    // both REL events in the same frame should pass through without triggering
    // missing_syn_time, because the first data event resets last_syn_time.
    auto pipeline = context
                  | timed_sequence{std::array{
                      timed_ev(EV_SYN, SYN_REPORT, 0, 0us),
                      timed_ev(EV_REL, REL_X, 10, 200ms),
                      timed_ev(EV_REL, REL_Y, 5, 200ms),
                      timed_ev(EV_SYN, SYN_REPORT, 0, 201ms),
                      timed_ev(EV_REL, REL_X, 10, 202ms),
                      timed_ev(EV_REL, REL_Y, 5, 202ms),
                      timed_ev(EV_SYN, SYN_REPORT, 0, 203ms),
                    }}
                  | event_sanitizer.missing_syn_count(false).missing_syn_travel(false)
                    .missing_syn_time_threshold(100ms)
                  | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 7U);
    // First frame: SYN, REL_X, REL_Y, SYN
    EXPECT_EQ(col[0].type(), EV_SYN);
    EXPECT_EQ(col[0].code(), SYN_REPORT);
    EXPECT_EQ(col[1].code(), REL_X);
    EXPECT_EQ(col[2].code(), REL_Y);
    EXPECT_EQ(col[3].type(), EV_SYN);
    EXPECT_EQ(col[3].code(), SYN_REPORT);
    // Second frame: REL_X, REL_Y, SYN
    EXPECT_EQ(col[4].code(), REL_X);
    EXPECT_EQ(col[5].code(), REL_Y);
    EXPECT_EQ(col[6].type(), EV_SYN);
    EXPECT_EQ(col[6].code(), SYN_REPORT);
}

TEST(SanitizerTest, DetectsMissingSynCount) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    auto pipeline = context
                  | timed_sequence{std::array{
                      timed_ev(EV_SYN, SYN_REPORT, 0, 0us),
                      timed_ev(EV_REL, REL_X, 1, 1000us),
                      timed_ev(EV_REL, REL_X, 1, 2000us),
                      timed_ev(EV_REL, REL_X, 1, 3000us),
                      timed_ev(EV_REL, REL_X, 1, 4000us),
                      timed_ev(EV_REL, REL_X, 1, 5000us),
                      timed_ev(EV_REL, REL_X, 1, 6000us),
                      timed_ev(EV_REL, REL_X, 1, 7000us),
                      timed_ev(EV_REL, REL_X, 1, 8000us),
                      timed_ev(EV_REL, REL_X, 1, 9000us),
                      timed_ev(EV_REL, REL_X, 1, 10'000us),
                      timed_ev(EV_REL, REL_X, 1, 11'000us),
                      timed_ev(EV_REL, REL_X, 1, 12'000us),
                      timed_ev(EV_REL, REL_X, 1, 13'000us),
                      timed_ev(EV_REL, REL_X, 1, 14'000us),
                      timed_ev(EV_REL, REL_X, 1, 15'000us),
                      timed_ev(EV_REL, REL_X, 1, 16'000us),
                      timed_ev(EV_REL, REL_X, 1, 17'000us),
                      timed_ev(EV_REL, REL_X, 1, 18'000us),
                      timed_ev(EV_REL, REL_X, 1, 19'000us),
                      timed_ev(EV_REL, REL_X, 1, 20'000us),
                      timed_ev(EV_REL, REL_X, 1, 21'000us),
                    }}
                  | event_sanitizer.missing_syn_time(false).missing_syn_travel(false)
                  | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 21U);
    EXPECT_EQ(col[0].type(), EV_SYN);
    EXPECT_EQ(col[0].code(), SYN_REPORT);
    for (std::size_t i = 1; i < 21; ++i) {
        EXPECT_EQ(col[i].code(), REL_X);
    }
}

TEST(SanitizerTest, DetectsMissingSynTravel) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    auto pipeline = context
                  | timed_sequence{std::array{
                      timed_ev(EV_SYN, SYN_REPORT, 0, 0us),
                      timed_ev(EV_REL, REL_X, 300, 1ms),
                      timed_ev(EV_REL, REL_Y, 300, 2ms),
                    }}
                  | event_sanitizer.missing_syn_time(false).missing_syn_count(false).big_jumps(false)
                    .missing_syn_travel_threshold(500)
                  | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 2U);
    EXPECT_EQ(col[0].type(), EV_SYN);
    EXPECT_EQ(col[0].code(), SYN_REPORT);
    EXPECT_EQ(col[1].code(), REL_X);
}

TEST(SanitizerTest, FiltersAbsPositionsWithoutTool) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    // ABS_X/ABS_Y before any BTN_TOOL_* press should be dropped.
    // After BTN_TOOL_PEN=1, ABS_X should pass.
    auto pipeline = context
                  | timed_sequence{std::array{
                      timed_ev(EV_ABS, ABS_X, 500, 0us),
                      timed_ev(EV_ABS, ABS_Y, 300, 1ms),
                      timed_ev(EV_KEY, BTN_TOOL_PEN, 1, 2ms),
                      timed_ev(EV_ABS, ABS_X, 510, 3ms),
                    }}
                  | event_sanitizer
                  | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 2U);
    EXPECT_EQ(col[0].type(), EV_KEY);
    EXPECT_EQ(col[0].code(), BTN_TOOL_PEN);
    EXPECT_EQ(col[1].type(), EV_ABS);
    EXPECT_EQ(col[1].code(), ABS_X);
}

TEST(SanitizerTest, DropsAbsAfterToolRelease) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    // After BTN_TOOL_PEN=0, ABS_X should be dropped again.
    auto pipeline = context
                  | timed_sequence{std::array{
                      timed_ev(EV_KEY, BTN_TOOL_PEN, 1, 0us),
                      timed_ev(EV_ABS, ABS_X, 500, 1ms),
                      timed_ev(EV_KEY, BTN_TOOL_PEN, 0, 2ms),
                      timed_ev(EV_ABS, ABS_X, 510, 3ms),
                    }}
                  | event_sanitizer
                  | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 3U);
    EXPECT_EQ(col[0].type(), EV_KEY);
    EXPECT_EQ(col[0].code(), BTN_TOOL_PEN);
    EXPECT_EQ(col[0].value(), 1);
    EXPECT_EQ(col[1].type(), EV_ABS);
    EXPECT_EQ(col[1].code(), ABS_X);
    EXPECT_EQ(col[2].type(), EV_KEY);
    EXPECT_EQ(col[2].code(), BTN_TOOL_PEN);
    EXPECT_EQ(col[2].value(), 0);
}

TEST(SanitizerTest, DisabledOrphanAbsCheckPassesEventsThrough) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    auto pipeline = context
                  | timed_sequence{std::array{
                      timed_ev(EV_ABS, ABS_X, 500, 0us),
                      timed_ev(EV_ABS, ABS_Y, 300, 1ms),
                    }}
                  | event_sanitizer.orphan_abs(false)
                  | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    EXPECT_EQ(col.size(), 2U);
}
