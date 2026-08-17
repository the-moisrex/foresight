// Created by moisrex on 8/17/26.

#include "./common/tests_common_pch.hpp"

#include <linux/input-event-codes.h>

import fs8.mods;

using namespace fs8;

namespace {
    /// External sink for `record[captured_events]`.
    std::vector<fs8::event_type> captured_events; // NOLINT(*-global-variables)

    /// Events captured by a `run` mod.
    std::vector<fs8::event_type> run_events; // NOLINT(*-global-variables)
} // namespace

TEST(RecordTest, InternalStorageRecordsAndPassesThrough) {
    auto pipeline =
      context
      | emit_all[{
        {.type = EV_KEY,      .code = KEY_A, .value = 1},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
        {.type = EV_KEY,      .code = KEY_A, .value = 0},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
    }]
      | record;
    auto &col = pipeline.mod<basic_record>();

    pipeline();

    ASSERT_EQ(col.size(), 4U);
    EXPECT_FALSE(col.empty());
    EXPECT_EQ(col.front().code(), KEY_A);
    EXPECT_EQ(col.front().value(), 1);
    EXPECT_EQ(col.back().type(), EV_SYN);
    EXPECT_EQ(col.at(0).code(), KEY_A);
    EXPECT_EQ(col[1].code(), SYN_REPORT);
    EXPECT_EQ(col.at(2).value(), 0);

    // Records everything in order, including SYN_REPORTs.
    std::vector<event_type::code_type> codes;
    for (auto const &event : col) {
        codes.push_back(event.code());
    }
    EXPECT_EQ(codes, (std::vector<event_type::code_type>{KEY_A, SYN_REPORT, KEY_A, SYN_REPORT}));
}

TEST(RecordTest, ExternalSink) {
    captured_events.clear();

    (context
     | emit_all[{
       {.type = EV_KEY,      .code = KEY_B, .value = 1},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
       {.type = EV_KEY,      .code = KEY_B, .value = 0},
    }]
     | record[captured_events])();

    ASSERT_EQ(captured_events.size(), 3U);
    EXPECT_EQ(captured_events.at(0).code(), KEY_B);
    EXPECT_EQ(captured_events.at(0).value(), 1);
    EXPECT_EQ(captured_events.at(2).value(), 0);
}

TEST(RecordTest, Clear) {
    auto pipeline =
      context
      | emit_all[{
        {.type = EV_KEY,      .code = KEY_A, .value = 1},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
    }]
      | record;
    auto &col = pipeline.mod<basic_record>();

    pipeline();
    ASSERT_FALSE(col.empty());
    col.clear();
    EXPECT_TRUE(col.empty());
    EXPECT_EQ(col.size(), 0U);
}

TEST(RecordTest, Queries) {
    auto pipeline =
      context
      | emit_all[{
        {.type = EV_KEY,      .code = KEY_A, .value = 1},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
        {.type = EV_KEY,      .code = KEY_A, .value = 0},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
        {.type = EV_KEY,      .code = KEY_B, .value = 1},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
        {.type = EV_KEY,      .code = KEY_B, .value = 0},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
    }]
      | record;
    auto &col = pipeline.mod<basic_record>();

    pipeline();

    ASSERT_EQ(col.size(), 8U);
    EXPECT_EQ(col.count(EV_KEY), 4U);
    EXPECT_EQ(col.count(EV_KEY, KEY_A), 2U);
    EXPECT_EQ(col.count([](event_type const &event) noexcept {
        return event.value() == 1;
    }),
              2U);

    EXPECT_TRUE(col.any([](event_type const &event) noexcept {
        return event.is(EV_KEY, KEY_B);
    }));
    EXPECT_FALSE(col.any([](event_type const &event) noexcept {
        return event.is(EV_REL, REL_X);
    }));
    EXPECT_TRUE(col.all([](event_type const &event) noexcept {
        return event.type() == EV_KEY || event.type() == EV_SYN;
    }));

    auto const a_keys = col.filter([](event_type const &event) noexcept {
        return event.is(EV_KEY, KEY_A);
    });
    ASSERT_EQ(a_keys.size(), 2U);
    EXPECT_EQ(a_keys.front().value(), 1);
    EXPECT_EQ(a_keys.back().value(), 0);

    EXPECT_EQ(col.keys().size(), 4U);
    EXPECT_EQ(col.without_syn().size(), 4U);

    auto const user_events = col.as_user_events();
    ASSERT_EQ(user_events.size(), 8U);
    EXPECT_EQ(user_events.front().type, EV_KEY);
}

TEST(RecordTest, AtOutOfRangeThrows) {
    auto pipeline =
      context
      | emit_all[{
        {.type = EV_KEY, .code = KEY_A, .value = 1},
    }]
      | record;
    auto &col = pipeline.mod<basic_record>();

    pipeline();
    ASSERT_EQ(col.size(), 1U);
    EXPECT_THROW(std::ignore = col.at(1), std::out_of_range);
}

TEST(RecordTest, RunAsMod) {
    run_events.clear();

    (context
     | emit_all[{
       {.type = EV_KEY,      .code = KEY_C, .value = 1},
       {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
    }]
     | run{[](auto &ctx) noexcept {
           run_events.push_back(ctx.event());
       }})();

    ASSERT_EQ(run_events.size(), 2U);
    EXPECT_EQ(run_events.at(0).code(), KEY_C);
    EXPECT_EQ(run_events.at(1).type(), EV_SYN);
}

TEST(RecordTest, RunAsCallback) {
    std::vector<user_event> events;
    run                     rec{[&events](user_event const &event) noexcept {
        events.push_back(event);
    }};
    rec(user_event{.type = EV_KEY, .code = KEY_A, .value = 1});
    rec(user_event{.type = EV_KEY, .code = KEY_B, .value = 1});
    rec(user_event{.type = EV_KEY, .code = KEY_C, .value = 1});

    ASSERT_EQ(events.size(), 3U);
    EXPECT_EQ(events.at(0).code, KEY_A);
    EXPECT_EQ(events.at(2).code, KEY_C);
}
