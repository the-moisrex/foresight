#include "./common/tests_common_pch.hpp"

#include <chrono>
#include <linux/input-event-codes.h>
#include <sys/time.h>

import fs8.mods;

namespace {
    int happened = 0;                     // NOLINT

    using namespace std::chrono_literals; // NOLINT(*-using-namespace)

    /// Build an event with an explicit microsecond timestamp so tests can control
    /// how much "wall time" passes between keys.
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
} // namespace

TEST(TimedTypedTest, MatchWithinWindow) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    happened = 0;

    (context
     | timed_sequence{std::array{
         timed_ev(EV_KEY, KEY_T, 1, 0us),
         timed_ev(EV_KEY, KEY_E, 1, 50ms),
         timed_ev(EV_KEY, KEY_S, 1, 100ms),
         timed_ev(EV_KEY, KEY_T, 1, 150ms),
       }}
     | search_engine
     | on[timed_typed["test", 500ms], [] noexcept {
           happened = 1;
       }])();
    EXPECT_EQ(happened, 1);
}

TEST(TimedTypedTest, DefaultDuration) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    happened = 0;

    (context
     | timed_sequence{std::array{
         timed_ev(EV_KEY, KEY_T, 1, 0us),
         timed_ev(EV_KEY, KEY_E, 1, 50ms),
         timed_ev(EV_KEY, KEY_S, 1, 100ms),
         timed_ev(EV_KEY, KEY_T, 1, 150ms),
       }}
     | search_engine
     | on[timed_typed["test"], [] noexcept {
           happened = 1;
       }])();
    EXPECT_EQ(happened, 1);
}

TEST(TimedTypedTest, MatchAtWindowBoundary) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    happened = 0;

    // Gaps are exactly the window; a pause strictly *greater* than the window resets.
    (context
     | timed_sequence{std::array{
         timed_ev(EV_KEY, KEY_T, 1, 0us),
         timed_ev(EV_KEY, KEY_E, 1, 500ms),
         timed_ev(EV_KEY, KEY_S, 1, 1000ms),
         timed_ev(EV_KEY, KEY_T, 1, 1500ms),
       }}
     | search_engine
     | on[timed_typed["test", 500ms], [] noexcept {
           happened = 1;
       }])();
    EXPECT_EQ(happened, 1);
}

TEST(TimedTypedTest, NoMatchWhenGapExceedsWindow) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    happened = 0;

    (context
     | timed_sequence{std::array{
         timed_ev(EV_KEY, KEY_T, 1, 0us),
         timed_ev(EV_KEY, KEY_E, 1, 1000ms),
         timed_ev(EV_KEY, KEY_S, 1, 2000ms),
         timed_ev(EV_KEY, KEY_T, 1, 3000ms),
       }}
     | search_engine
     | on[timed_typed["test", 500ms], [] noexcept {
           happened = 1;
       }])();
    EXPECT_EQ(happened, 0);
}

TEST(TimedTypedTest, IssueScenario) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    happened = 0;

    // The user types "@" (shift+2) and then "test" two minutes later; the pause
    // between "@" and "test" must invalidate the match.
    (context
     | timed_sequence{std::array{
         timed_ev(EV_KEY, KEY_LEFTSHIFT, 1, 0us),
         timed_ev(EV_KEY, KEY_2, 1, 10ms), // "@"
         timed_ev(EV_KEY, KEY_LEFTSHIFT, 0, 20ms),
         timed_ev(EV_KEY, KEY_T, 1, 120'000ms), // two minutes later
         timed_ev(EV_KEY, KEY_E, 1, 120'050ms),
         timed_ev(EV_KEY, KEY_S, 1, 120'100ms),
         timed_ev(EV_KEY, KEY_T, 1, 120'150ms),
       }}
     | search_engine
     | on[timed_typed["@test", 1s], [] noexcept {
           happened = 1;
       }])();
    EXPECT_EQ(happened, 0);
}

TEST(TimedTypedTest, ReleaseDoesNotRefreshWindow) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    happened = 0;

    // The T key is held for 600ms (longer than the 500ms window) before the rest
    // of "test" is typed. Releasing the key must NOT count as a fresh typing event,
    // so the gap from the T press to "est" still exceeds the window.
    (context
     | timed_sequence{std::array{
         timed_ev(EV_KEY, KEY_T, 1, 0us),
         timed_ev(EV_KEY, KEY_T, 0, 600ms),
         timed_ev(EV_KEY, KEY_E, 1, 610ms),
         timed_ev(EV_KEY, KEY_S, 1, 620ms),
         timed_ev(EV_KEY, KEY_T, 1, 630ms),
       }}
     | search_engine
     | on[timed_typed["test", 500ms], [] noexcept {
           happened = 1;
       }])();
    EXPECT_EQ(happened, 0);
}

TEST(TimedTypedTest, KeyUpWithinWindow) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    happened = 0;

    // `[x][y]` fires on the releases, which must all arrive within the window.
    (context
     | timed_sequence{std::array{
         timed_ev(EV_KEY, KEY_X, 1, 0us),
         timed_ev(EV_KEY, KEY_X, 0, 10ms),
         timed_ev(EV_KEY, KEY_Y, 1, 20ms),
         timed_ev(EV_KEY, KEY_Y, 0, 30ms),
       }}
     | search_engine
     | on[timed_typed["[x][y]", 500ms], [] noexcept {
           ++happened;
       }])();
    EXPECT_EQ(happened, 1);
}

TEST(TimedTypedTest, KeyUpPauseTooLong) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    happened = 0;

    // The release of Y comes ~1s after the release of X; the partial match is dropped.
    (context
     | timed_sequence{std::array{
         timed_ev(EV_KEY, KEY_X, 1, 0us),
         timed_ev(EV_KEY, KEY_X, 0, 10ms),
         timed_ev(EV_KEY, KEY_Y, 1, 20ms),
         timed_ev(EV_KEY, KEY_Y, 0, 1000ms),
       }}
     | search_engine
     | on[timed_typed["[x][y]", 500ms], [] noexcept {
           ++happened;
       }])();
    EXPECT_EQ(happened, 0);
}

TEST(TimedTypedTest, ExpiredPartialThenFreshRetype) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    happened = 0;

    // A first, too-slow attempt ("t" then "e" a second later) is discarded; a
    // fresh fast "test" afterwards still matches.
    (context
     | timed_sequence{std::array{
         timed_ev(EV_KEY, KEY_T, 1, 0us),
         timed_ev(EV_KEY, KEY_E, 1, 1000ms),
         timed_ev(EV_KEY, KEY_T, 1, 2000ms),
         timed_ev(EV_KEY, KEY_E, 1, 2001ms),
         timed_ev(EV_KEY, KEY_S, 1, 2002ms),
         timed_ev(EV_KEY, KEY_T, 1, 2003ms),
       }}
     | search_engine
     | on[timed_typed["test", 500ms], [] noexcept {
           happened = 1;
       }])();
    EXPECT_EQ(happened, 1);
}

TEST(TimedTypedTest, UntypedCharsInWindow) {
    using namespace fs8; // NOLINT(*-build-using-namespace)
    happened = 0;

    // Typing "qtest" quickly still matches "test" (a trailing substring), like typed.
    (context
     | timed_sequence{std::array{
         timed_ev(EV_KEY, KEY_Q, 1, 0us),
         timed_ev(EV_KEY, KEY_T, 1, 10ms),
         timed_ev(EV_KEY, KEY_E, 1, 20ms),
         timed_ev(EV_KEY, KEY_S, 1, 30ms),
         timed_ev(EV_KEY, KEY_T, 1, 40ms),
       }}
     | search_engine
     | on[timed_typed["test", 500ms], [] noexcept {
           happened = 1;
       }])();
    EXPECT_EQ(happened, 1);
}
