// Created by moisrex on 8/18/26.

#include "./common/tests_common_pch.hpp"

#include <linux/input-event-codes.h>

import fs8.context;
import fs8.mods;
import dynamic_scoping;

using namespace fs8;

namespace {
    /// External sinks for `record[sink]`.
    std::vector<event_type> sink1; // NOLINT(*-global-variables)
    std::vector<event_type> sink2; // NOLINT(*-global-variables)

    /// Counts how many times it's run with a `start` tag.
    int start_count = 0; // NOLINT(*-global-variables)

    /// A minimal mod that only reacts to `start`.
    struct start_aware_mod {
        constexpr context_action operator()(Context auto&, start_tag) noexcept {
            ++start_count;
            return context_action::next;
        }

        constexpr context_action operator()(Context auto&) noexcept {
            return context_action::next;
        }
    };
} // namespace

TEST(DynamicContextTest, EventAccess) {
    sink1.clear();
    auto pipeline = context | record[sink1];

    {
        dynamic_scope scope{dynamic_context, pipeline};

        dynamic_context.event(event_type{EV_KEY, KEY_A, 1});
        EXPECT_EQ(dynamic_context.event().type(), EV_KEY);
        EXPECT_EQ(dynamic_context.event().code(), KEY_A);
        EXPECT_EQ(dynamic_context.event().value(), 1);

        dynamic_context.event(event_type{EV_KEY, KEY_A, 0});
        EXPECT_EQ(dynamic_context.event().value(), 0);
    }
}

TEST(DynamicContextTest, ModInvocation) {
    sink1.clear();
    auto pipeline = context | record[sink1];

    {
        dynamic_scope scope{dynamic_context, pipeline};

        dynamic_context.event(event_type{EV_KEY, KEY_A, 1});
        std::ignore = dynamic_context.mod<0>()();

        ASSERT_EQ(sink1.size(), 1U);
        EXPECT_EQ(sink1.at(0).type(), EV_KEY);
        EXPECT_EQ(sink1.at(0).code(), KEY_A);
        EXPECT_EQ(sink1.at(0).value(), 1);
    }
}

TEST(DynamicContextTest, ModTagInvocation) {
    start_count   = 0;
    auto pipeline = context | start_aware_mod{};

    {
        dynamic_scope scope{dynamic_context, pipeline};

        std::ignore = dynamic_context.mod<0>()(start);
        EXPECT_EQ(start_count, 1);

        // A plain (tagless) invocation does not hit the `start` overload.
        std::ignore = dynamic_context.mod<0>()();
        EXPECT_EQ(start_count, 1);
    }
}

TEST(DynamicContextTest, ForkEmitRunsAllDownstream) {
    sink1.clear();
    sink2.clear();
    auto pipeline = context | record[sink1] | record[sink2];

    {
        dynamic_scope scope{dynamic_context, pipeline};

        std::ignore = dynamic_context.mod<0>().fork_emit(event_type{EV_KEY, KEY_B, 1});

        ASSERT_EQ(sink1.size(), 1U);
        ASSERT_EQ(sink2.size(), 1U);
        EXPECT_EQ(sink1.at(0).code(), KEY_B);
        EXPECT_EQ(sink2.at(0).code(), KEY_B);
    }
}

TEST(DynamicContextTest, ForkEmitStartsAtIndex) {
    sink1.clear();
    sink2.clear();
    auto pipeline = context | record[sink1] | record[sink2];

    {
        dynamic_scope scope{dynamic_context, pipeline};

        // Fork from index 1: only the second record sees the event.
        std::ignore = dynamic_context.mod<1>().fork_emit(event_type{EV_KEY, KEY_C, 1});

        EXPECT_EQ(sink1.size(), 0U);
        ASSERT_EQ(sink2.size(), 1U);
        EXPECT_EQ(sink2.at(0).code(), KEY_C);
    }
}

TEST(DynamicContextTest, NestedScopesSwitchPipeline) {
    sink1.clear();
    sink2.clear();
    auto pipeline_a = context | record[sink1];
    auto pipeline_b = context | record[sink2];

    {
        dynamic_scope scope_a{dynamic_context, pipeline_a};

        dynamic_context.event(event_type{EV_KEY, KEY_A, 1});
        std::ignore = dynamic_context.mod<0>()();
        ASSERT_EQ(sink1.size(), 1U);

        {
            dynamic_scope scope_b{dynamic_context, pipeline_b};

            dynamic_context.event(event_type{EV_KEY, KEY_B, 1});
            std::ignore = dynamic_context.mod<0>()();
            EXPECT_EQ(sink1.size(), 1U);
            ASSERT_EQ(sink2.size(), 1U);
            EXPECT_EQ(sink2.at(0).code(), KEY_B);
        }

        // After scope_b ends, the dynamic context points back at pipeline_a.
        dynamic_context.event(event_type{EV_KEY, KEY_A, 2});
        std::ignore = dynamic_context.mod<0>()();
        ASSERT_EQ(sink1.size(), 2U);
        EXPECT_EQ(sink1.at(1).code(), KEY_A);
        EXPECT_EQ(sink2.size(), 1U);
    }
}
