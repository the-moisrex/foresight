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

    /// Set to `dynamic_context.bound()` when the probe runs (global because
    /// pipeline construction is consteval and cannot capture a local's address).
    bool bound_seen = false; // NOLINT(*-global-variables)

    /// A minimal mod that only reacts to `start`.
    struct start_aware_mod {
        constexpr context_action operator()(Context auto&, special_event const& tag) noexcept {
            if (tag.code != start.code) {
                return context_action::drop_event;
            }
            ++start_count;
            return context_action::next;
        }

        constexpr context_action operator()(Context auto&) noexcept {
            return context_action::next;
        }
    };

    /// Records whether the dynamic context was bound when it was invoked.
    struct bound_probe {
        constexpr context_action operator()(Context auto&, special_event const& tag) noexcept {
            if (tag.code != start.code) {
                return context_action::drop_event;
            }
            bound_seen = dynamic_context.bound();
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

TEST(DynamicContextTest, PipelineRunAutoBinds) {
    bound_seen    = false;
    auto pipeline = context | bound_probe{};

    EXPECT_EQ(pipeline(start), context_action::next);
    EXPECT_TRUE(bound_seen) << "Starting a pipeline must bind the dynamic context automatically.";

    // Without a running pipeline the binding must be gone.
    EXPECT_FALSE(dynamic_context.bound());
}

TEST(DynamicContextTest, TypedModEnumeration) {
    sink1.clear();
    auto pipeline = context | record[sink1];

    // Concrete context: typed getters work without any binding.
    auto const concrete = pipeline.mods<basic_record>();
    ASSERT_EQ(concrete.size(), 1U);
    EXPECT_EQ(std::addressof(concrete.at(0).get()), std::addressof(pipeline.mod<basic_record>()));

    // Type-erased access point: same result through the dynamic context.
    {
        dynamic_scope scope{dynamic_context, pipeline};
        auto const    dyn = dynamic_context.mods<basic_record>();
        ASSERT_EQ(dyn.size(), 1U);
        EXPECT_EQ(std::addressof(dyn.at(0).get()), std::addressof(pipeline.mod<basic_record>()));

        // A type that is not in the pipeline yields an empty range.
        EXPECT_TRUE(dynamic_context.mods<basic_uinput>().empty());
    }
}

TEST(DynamicContextTest, RmodsRecursesThroughRouter) {
    auto pipeline = context | router[caps::keyboard >> uinput, caps::mouse >> uinput];

    auto const top = pipeline.mods<basic_uinput>();
    EXPECT_EQ(top.size(), 0U) << "mods<T> must not descend into the router.";

    auto const rec = pipeline.rmods<basic_uinput>();
    ASSERT_EQ(rec.size(), 2U) << "rmods<T> must recurse through the router's routes.";
}

TEST(DynamicContextTest, ForEachSelfDevnodeTraversesRouter) {
    auto pipeline = context | router[caps::keyboard >> uinput];

    std::vector<std::string> nodes;
    {
        dynamic_scope scope{dynamic_context, pipeline};
        dynamic_context.for_each_self_devnode([&](std::string_view node) {
            nodes.emplace_back(node);
        });
    }

    // Not started, so the uinputs have no device yet; the traversal must still
    // recurse into the router without crashing.
    EXPECT_TRUE(nodes.empty());
    ASSERT_EQ(pipeline.rmods<basic_uinput>().size(), 1U);
}
