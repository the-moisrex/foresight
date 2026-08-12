// Created by moisrex on 8/11/26.

#include "common/tests_common_pch.hpp"

#include <linux/input-event-codes.h>

import fs8.mods;
import fs8.devices.queries;

using namespace fs8;

namespace {

    /// A mod that only accepts `start_tag` and the plain context, so the router has to drop the
    /// device_query it pushes down the pipeline before the mod can be started.
    struct counting_mod {
        int* counter;

        context_action operator()(start_tag) noexcept {
            ++*counter;
            return context_action::next;
        }

        context_action operator()(Context auto&) noexcept {
            ++*counter;
            return context_action::next;
        }
    };

    static_assert(Modifier<counting_mod>);

    int key_counter   = 0;
    int mouse_counter = 0;

    using router_t = basic_router<basic_context<counting_mod>, basic_context<counting_mod>>;

    static constinit auto pipeline =
      context
      | router[caps::keyboard >> (context | counting_mod{&key_counter}), // sub-pipeline 1
               caps::mouse >> (context | counting_mod{&mouse_counter})   // sub-pipeline 2
    ];

    /// A mod that wants the device_query itself (the full-match path through the pipeline).
    struct query_record {
        device_query received_query{};
        bool         called = false;
    };

    struct query_consumer {
        query_record* record;

        context_action operator()(Context auto&, device_query const& inp_query, start_tag) noexcept {
            record->received_query = inp_query;
            record->called         = true;
            return context_action::next;
        }

        context_action operator()(Context auto&) noexcept {
            return context_action::next;
        }
    };

    static_assert(Modifier<query_consumer>);

    query_record keyboard_query_record;
    query_record mouse_query_record;

    using query_router_t = basic_router<basic_context<query_consumer>, basic_context<query_consumer>>;

    static constinit auto query_pipeline =
      context
      | router[caps::keyboard >> (context | query_consumer{&keyboard_query_record}),
               caps::mouse >> (context | query_consumer{&mouse_query_record})];

} // namespace

TEST(Router, PipelineRouteStartPassThrough) {
    key_counter   = 0;
    mouse_counter = 0;

    EXPECT_EQ(pipeline(start), context_action::next);

    // The router forwards each route's device_query into the pipeline; `counting_mod` can't accept
    // the query, so the drop path kicks in and it is started via `(start)`.
    EXPECT_EQ(key_counter, 1);
    EXPECT_EQ(mouse_counter, 1);
}

TEST(Router, PipelineRouteEventDispatch) {
    key_counter   = 0;
    mouse_counter = 0;

    ASSERT_EQ(pipeline(start), context_action::next);

    // KEY_A belongs to the keyboard caps, so it must be dispatched to the keyboard route only.
    pipeline.event(event_type{EV_KEY, KEY_A, 1});
    EXPECT_EQ(pipeline.mod<router_t>()(pipeline), context_action::next);

    EXPECT_EQ(key_counter, 2);
    EXPECT_EQ(mouse_counter, 1);
}

TEST(Router, PipelineRouteQueryForwarding) {
    keyboard_query_record = {};
    mouse_query_record    = {};

    ASSERT_EQ(query_pipeline(start), context_action::next);

    // The device_query pushed by each route must reach the query_consumer mod unchanged.
    EXPECT_TRUE(keyboard_query_record.called);
    EXPECT_EQ(keyboard_query_record.received_query.caps, view(caps::keyboard));
    EXPECT_TRUE(mouse_query_record.called);
    EXPECT_EQ(mouse_query_record.received_query.caps, view(caps::mouse));
}
