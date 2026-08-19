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

TEST(DebounceTest, SpuriousDoubleClickIsSquashed) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    auto pipeline = context | timed_sequence{spurious_left_click} | debounce[{.type = EV_KEY, .code = BTN_LEFT}] | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 2U);
    EXPECT_EQ(col[0].code(), BTN_LEFT);
    EXPECT_EQ(col[0].value(), 1);
    EXPECT_EQ(col[1].code(), BTN_LEFT);
    EXPECT_EQ(col[1].value(), 0);
}

TEST(DebounceTest, RealDoubleClickPasses) {
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

TEST(DebounceTest, UntouchedButtonsPassThrough) {
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

TEST(DebounceTest, AllThreeButtonsAreDebounced) {
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
      | debounce[BTN_LEFT, BTN_RIGHT, BTN_MIDDLE]
      | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 6U);
    for (std::size_t i = 0; i < col.size(); ++i) {
        EXPECT_EQ(col[i].value(), i % 2 == 0 ? 1 : 0);
    }
}

TEST(DebounceTest, RuntimeThreshold) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    using msec_type = basic_debounce<1>::msec_type;

    // A 10ms window squashes a click whose presses are 5ms apart...
    auto pipeline =
      context
      | timed_sequence{std::array{
          timed_ev(EV_KEY, BTN_LEFT, 1, 0us),
          timed_ev(EV_KEY, BTN_LEFT, 0, 2ms),
          timed_ev(EV_KEY, BTN_LEFT, 1, 5ms),
          timed_ev(EV_KEY, BTN_LEFT, 0, 8ms),
        }}
      | debounce[BTN_LEFT]
      | record;

    pipeline.mod(debounce[BTN_LEFT]).set_time_threshold(msec_type{10ms});
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
      | debounce[BTN_LEFT]
      | record;

    pipeline2.mod(debounce[BTN_LEFT]).set_time_threshold(msec_type{1ms});
    pipeline2();

    EXPECT_EQ(pipeline2.mod<basic_record>().size(), 4U);
}

TEST(DebounceTest, EventModeDropsFastRepeats) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    // In `event` mode any event of the code landing within the window is dropped;
    // only changes that settle for longer than the window pass through.
    auto pipeline =
      context
      | timed_sequence{std::array{
          timed_ev(EV_ABS, ABS_X, 50, 0us),
          timed_ev(EV_ABS, ABS_X, 55, 2ms),
          timed_ev(EV_ABS, ABS_X, 60, 5ms),
          timed_ev(EV_ABS, ABS_X, 70, 35ms),
          timed_ev(EV_ABS, ABS_X, 80, 37ms),
        }}
      | debounce[{.type = EV_ABS, .code = ABS_X}].event()
      | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 2U);
    EXPECT_EQ(col[0].value(), 50);
    EXPECT_EQ(col[1].value(), 70);
}

TEST(DebounceTest, ClickModeDegradesForNonKeyCodes) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    // A scroll wheel that double-fires: `click` mode has no press/release pair for
    // a non-key code, so it behaves like `event` mode and swallows the repeats.
    auto pipeline =
      context
      | timed_sequence{std::array{
          timed_ev(EV_REL, REL_WHEEL, 1, 0us),
          timed_ev(EV_REL, REL_WHEEL, 1, 2ms),
          timed_ev(EV_REL, REL_WHEEL, 1, 5ms),
          timed_ev(EV_REL, REL_WHEEL, 1, 40ms),
        }}
      | debounce[{.type = EV_REL, .code = REL_WHEEL}]
      | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 2U);
    EXPECT_EQ(col[0].value(), 1);
    EXPECT_EQ(col[1].value(), 1);
}

TEST(DebounceTest, MixedCodesVariadic) {
    using namespace fs8; // NOLINT(*-build-using-namespace)

    // A key code (inferred EV_KEY) and an ABS code can be mixed in one debounce;
    // each code keeps its own window state.
    auto pipeline =
      context
      | timed_sequence{std::array{
          timed_ev(EV_KEY, BTN_LEFT, 1, 0us),
          timed_ev(EV_KEY, BTN_LEFT, 0, 2ms),
          timed_ev(EV_KEY, BTN_LEFT, 1, 5ms),
          timed_ev(EV_KEY, BTN_LEFT, 0, 8ms),
          timed_ev(EV_ABS, ABS_X, 50, 12ms),
          timed_ev(EV_ABS, ABS_X, 55, 14ms),
          timed_ev(EV_ABS, ABS_X, 70, 50ms),
        }}
      | debounce[BTN_LEFT, event_code{.type = EV_ABS, .code = ABS_X}]
      | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 4U);
    EXPECT_EQ(col[0].code(), BTN_LEFT);
    EXPECT_EQ(col[0].value(), 1);
    EXPECT_EQ(col[1].code(), BTN_LEFT);
    EXPECT_EQ(col[1].value(), 0);
    EXPECT_EQ(col[2].code(), ABS_X);
    EXPECT_EQ(col[2].value(), 50);
    EXPECT_EQ(col[3].code(), ABS_X);
    EXPECT_EQ(col[3].value(), 70);
}

TEST(DebounceTest, RuntimeCodesAndMode) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    using debounce_t = basic_debounce<4>;

    // A default-constructed debounce can be pointed at codes and a mode at runtime.
    auto pipeline =
      context
      | timed_sequence{std::array{
          timed_ev(EV_KEY, BTN_LEFT, 1, 0us),
          timed_ev(EV_KEY, BTN_LEFT, 0, 2ms),
          timed_ev(EV_KEY, BTN_LEFT, 1, 5ms),
          timed_ev(EV_KEY, BTN_LEFT, 0, 8ms),
        }}
      | debounce_t{}
      | record;

    std::array const codes{
      event_code{.type = EV_KEY, .code = BTN_LEFT}
    };
    pipeline.mod(debounce_t{}).set_codes(codes);
    pipeline.mod(debounce_t{}).set_mode(debounce_mode::click);
    pipeline();

    // click mode: the spurious press and its release are swallowed.
    EXPECT_EQ(pipeline.mod<basic_record>().size(), 2U);

    // ...while `event` mode swallows everything after the first event.
    auto pipeline2 =
      context
      | timed_sequence{std::array{
          timed_ev(EV_KEY, BTN_LEFT, 1, 0us),
          timed_ev(EV_KEY, BTN_LEFT, 0, 2ms),
          timed_ev(EV_KEY, BTN_LEFT, 1, 5ms),
          timed_ev(EV_KEY, BTN_LEFT, 0, 8ms),
        }}
      | debounce_t{}
      | record;

    pipeline2.mod(debounce_t{}).set_codes(codes);
    pipeline2.mod(debounce_t{}).set_mode(debounce_mode::event);
    pipeline2();

    EXPECT_EQ(pipeline2.mod<basic_record>().size(), 1U);
}

TEST(DebounceTest, LegacyAliasStillDebounces) {
    using namespace fs8; // NOLINT(*-build-using-namespace}

    static_assert(basic_ignore_fast_double_clicks<1>::default_time_threshold == basic_debounce<1>::default_time_threshold);

    auto pipeline = context | timed_sequence{spurious_left_click} | ignore_fast_double_clicks[BTN_LEFT] | record;

    pipeline();

    auto const& col = pipeline.mod<basic_record>();
    ASSERT_EQ(col.size(), 2U);
    EXPECT_EQ(col[0].code(), BTN_LEFT);
    EXPECT_EQ(col[0].value(), 1);
    EXPECT_EQ(col[1].code(), BTN_LEFT);
    EXPECT_EQ(col[1].value(), 0);
}
