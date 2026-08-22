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
