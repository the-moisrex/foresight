#include "./common/tests_common_pch.hpp"

#include <chrono>
#include <linux/input-event-codes.h>
#include <sys/time.h>

import fs8.mods;

namespace {
    using namespace std::chrono_literals; // NOLINT(*-using-namespace)

    /// Build an event with an explicit microsecond timestamp so tests can control
    /// how much "wall time" passes between clicks.
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

    /// A load_event provider (like `emit_all`) that feeds events with explicit
    /// timestamps into the pipeline instead of stamping them with the current time.
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

    /// A spurious double-click: press/release/press/release within a few ms.
    constexpr auto spurious_left_click = std::array{
      timed_ev(EV_KEY, BTN_LEFT, 1, 0us),
      timed_ev(EV_KEY, BTN_LEFT, 0, 2ms),
      timed_ev(EV_KEY, BTN_LEFT, 1, 5ms),
      timed_ev(EV_KEY, BTN_LEFT, 0, 8ms),
    };
} // namespace

TEST(IgnoreFastDoubleClicksTest, SpuriousDoubleClickIsSquashed) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    auto pipeline = context | timed_sequence{spurious_left_click} | ignore_fast_double_clicks[{.type = EV_KEY, .code = BTN_LEFT}] | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 2U);
    EXPECT_EQ(col[0].code(), BTN_LEFT);
    EXPECT_EQ(col[0].value(), 1);
    EXPECT_EQ(col[1].code(), BTN_LEFT);
    EXPECT_EQ(col[1].value(), 0);
}

TEST(IgnoreFastDoubleClicksTest, RealDoubleClickPasses) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    auto pipeline =
      context
      | timed_sequence{std::array{
          timed_ev(EV_KEY, BTN_LEFT, 1, 0us),
          timed_ev(EV_KEY, BTN_LEFT, 0, 50ms),
          timed_ev(EV_KEY, BTN_LEFT, 1, 100ms),
          timed_ev(EV_KEY, BTN_LEFT, 0, 150ms),
        }}
      | ignore_fast_left_double_clicks
      | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 4U);
    EXPECT_EQ(col[0].value(), 1);
    EXPECT_EQ(col[1].value(), 0);
    EXPECT_EQ(col[2].value(), 1);
    EXPECT_EQ(col[3].value(), 0);
}

TEST(IgnoreFastDoubleClicksTest, UntouchedButtonsPassThrough) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    auto pipeline =
      context
      | timed_sequence{std::array{
          timed_ev(EV_KEY, BTN_LEFT, 1, 0us),
          timed_ev(EV_KEY, BTN_LEFT, 0, 2ms),
          timed_ev(EV_KEY, BTN_LEFT, 1, 5ms),
          timed_ev(EV_KEY, BTN_LEFT, 0, 8ms),
          timed_ev(EV_KEY, BTN_RIGHT, 1, 12ms),
          timed_ev(EV_KEY, BTN_RIGHT, 0, 14ms),
        }}
      | ignore_fast_left_double_clicks
      | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 4U);
    EXPECT_EQ(col[0].code(), BTN_LEFT);
    EXPECT_EQ(col[1].code(), BTN_LEFT);
    EXPECT_EQ(col[2].code(), BTN_RIGHT);
    EXPECT_EQ(col[3].code(), BTN_RIGHT);
    EXPECT_EQ(col[2].value(), 1);
    EXPECT_EQ(col[3].value(), 0);
}

TEST(IgnoreFastDoubleClicksTest, AllThreeButtonsAreDebounced) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    auto pipeline =
      context
      | timed_sequence{std::array{
          timed_ev(EV_KEY, BTN_LEFT, 1, 0us),
          timed_ev(EV_KEY, BTN_LEFT, 0, 2ms),
          timed_ev(EV_KEY, BTN_LEFT, 1, 5ms),
          timed_ev(EV_KEY, BTN_LEFT, 0, 8ms),
          timed_ev(EV_KEY, BTN_RIGHT, 1, 12ms),
          timed_ev(EV_KEY, BTN_RIGHT, 0, 14ms),
          timed_ev(EV_KEY, BTN_RIGHT, 1, 17ms),
          timed_ev(EV_KEY, BTN_RIGHT, 0, 20ms),
          timed_ev(EV_KEY, BTN_MIDDLE, 1, 24ms),
          timed_ev(EV_KEY, BTN_MIDDLE, 0, 26ms),
          timed_ev(EV_KEY, BTN_MIDDLE, 1, 29ms),
          timed_ev(EV_KEY, BTN_MIDDLE, 0, 32ms),
        }}
      | ignore_fast_double_clicks[BTN_LEFT, BTN_RIGHT, BTN_MIDDLE]
      | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 6U);
    for (std::size_t i = 0; i < col.size(); ++i) {
        EXPECT_EQ(col[i].value(), i % 2 == 0 ? 1 : 0);
    }
}

TEST(IgnoreFastDoubleClicksTest, RuntimeThreshold) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    using msec_type = basic_ignore_fast_double_clicks<3>::msec_type;

    // A 10ms window squashes a click whose presses are 5ms apart...
    auto pipeline =
      context
      | timed_sequence{std::array{
          timed_ev(EV_KEY, BTN_LEFT, 1, 0us),
          timed_ev(EV_KEY, BTN_LEFT, 0, 2ms),
          timed_ev(EV_KEY, BTN_LEFT, 1, 5ms),
          timed_ev(EV_KEY, BTN_LEFT, 0, 8ms),
        }}
      | ignore_fast_double_clicks[BTN_LEFT]
      | record;

    pipeline.mod(ignore_fast_double_clicks[BTN_LEFT]).set_time_threshold(msec_type{10ms});
    pipeline();

    EXPECT_EQ(pipeline.mod<basic_record>().size(), 2U);

    // ...but the same clicks pass once the window shrinks to 1ms.
    auto pipeline2 =
      context
      | timed_sequence{std::array{
          timed_ev(EV_KEY, BTN_LEFT, 1, 0us),
          timed_ev(EV_KEY, BTN_LEFT, 0, 2ms),
          timed_ev(EV_KEY, BTN_LEFT, 1, 5ms),
          timed_ev(EV_KEY, BTN_LEFT, 0, 8ms),
        }}
      | ignore_fast_double_clicks[BTN_LEFT]
      | record;

    pipeline2.mod(ignore_fast_double_clicks[BTN_LEFT]).set_time_threshold(msec_type{1ms});
    pipeline2();

    EXPECT_EQ(pipeline2.mod<basic_record>().size(), 4U);
}
