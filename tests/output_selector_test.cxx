// Created by moisrex on 8/29/26.

#include "./common/tests_common_pch.hpp"

#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <unistd.h>

import fs8.mods;
import fs8.cli;

using namespace fs8;

namespace {
    std::vector<fs8::event_type> captured_events; // NOLINT(*-global-variables)
} // namespace

TEST(OutputSelector, SatisfiesOutputModifierConcept) {
    static_assert(OutputModifier<output_selector>, "output_selector must satisfy OutputModifier");
    static_assert(OutputModifier<basic_output_selector<basic_std_output>>,
                  "basic_output_selector<basic_output> must satisfy OutputModifier");
}

TEST(OutputSelector, DefaultSelectedIsZero) {
    constexpr output_selector sel{};
    EXPECT_EQ(sel.selected(), 0);
}

TEST(OutputSelector, EmitToRawStdoutViaPipe) {
    int pipe_fds[2];
    ASSERT_EQ(pipe(pipe_fds), 0);

    output_selector sel{};
    sel.set_selected(0); // basic_output
    sel.output<0>().set_output(pipe_fds[1]);

    event_type press{EV_KEY, KEY_A, 1};
    EXPECT_TRUE(sel.emit(press));

    event_type syn{EV_SYN, SYN_REPORT, 0};
    EXPECT_TRUE(sel.emit(syn));

    event_type release{EV_KEY, KEY_A, 0};
    EXPECT_TRUE(sel.emit(release));

    close(pipe_fds[1]);

    input_event buf[3];
    auto const  n = read(pipe_fds[0], buf, sizeof(buf));
    close(pipe_fds[0]);

    ASSERT_EQ(n, static_cast<ssize_t>(sizeof(buf)));
    EXPECT_EQ(buf[0].type, EV_KEY);
    EXPECT_EQ(buf[0].code, KEY_A);
    EXPECT_EQ(buf[0].value, 1);
    EXPECT_EQ(buf[1].type, EV_SYN);
    EXPECT_EQ(buf[1].code, SYN_REPORT);
    EXPECT_EQ(buf[2].value, 0);
}

TEST(OutputSelector, EmitSynViaPipe) {
    int pipe_fds[2];
    ASSERT_EQ(pipe(pipe_fds), 0);

    output_selector sel{};
    sel.set_selected(0);
    sel.output<0>().set_output(pipe_fds[1]);

    EXPECT_TRUE(sel.emit(event_type{EV_SYN, SYN_REPORT, 0}));

    close(pipe_fds[1]);

    input_event ev{};
    auto const  n = read(pipe_fds[0], &ev, sizeof(ev));
    close(pipe_fds[0]);

    ASSERT_EQ(n, static_cast<ssize_t>(sizeof(ev)));
    EXPECT_EQ(ev.type, EV_SYN);
    EXPECT_EQ(ev.code, SYN_REPORT);
    EXPECT_EQ(ev.value, 0);
}

TEST(OutputSelector, EmitTypeCodeValueViaPipe) {
    int pipe_fds[2];
    ASSERT_EQ(pipe(pipe_fds), 0);

    output_selector sel{};
    sel.set_selected(0);
    sel.output<0>().set_output(pipe_fds[1]);

    EXPECT_TRUE(sel.emit(event_type{EV_KEY, KEY_B, 1}));

    close(pipe_fds[1]);

    input_event ev{};
    auto const  n = read(pipe_fds[0], &ev, sizeof(ev));
    close(pipe_fds[0]);

    ASSERT_EQ(n, static_cast<ssize_t>(sizeof(ev)));
    EXPECT_EQ(ev.type, EV_KEY);
    EXPECT_EQ(ev.code, KEY_B);
    EXPECT_EQ(ev.value, 1);
}

TEST(OutputSelector, OperatorPassesEventsToSelectedOutput) {
    int pipe_fds[2];
    ASSERT_EQ(pipe(pipe_fds), 0);

    output_selector sel{};
    sel.set_selected(0);
    sel.output<0>().set_output(pipe_fds[1]);

    event_type event{EV_KEY, KEY_C, 1};
    auto const action = sel(event);
    EXPECT_EQ(action, context_action::next);

    close(pipe_fds[1]);

    input_event ev{};
    auto const  n = read(pipe_fds[0], &ev, sizeof(ev));
    close(pipe_fds[0]);

    ASSERT_EQ(n, static_cast<ssize_t>(sizeof(ev)));
    EXPECT_EQ(ev.type, EV_KEY);
    EXPECT_EQ(ev.code, KEY_C);
}

TEST(OutputSelector, SwitchSelectedRoutesDifferently) {
    int pipe_a[2], pipe_b[2];
    ASSERT_EQ(pipe(pipe_a), 0);
    ASSERT_EQ(pipe(pipe_b), 0);

    output_selector sel{};

    // Select output 0 (basic_std_output), then configure its fd
    sel.set_selected(0);
    sel.output<0>().set_output(pipe_a[1]);

    event_type event{EV_KEY, KEY_D, 1};
    EXPECT_TRUE(sel.emit(event));

    close(pipe_a[1]);

    input_event ev_a{};
    auto const  n_a = read(pipe_a[0], &ev_a, sizeof(ev_a));
    close(pipe_a[0]);

    ASSERT_EQ(n_a, static_cast<ssize_t>(sizeof(ev_a)));
    EXPECT_EQ(ev_a.type, EV_KEY);
    EXPECT_EQ(ev_a.code, KEY_D);

    // Close pipe_b (unused in this test variant)
    close(pipe_b[0]);
    close(pipe_b[1]);
}

TEST(OutputSelector, MultipleEventsInOrder) {
    int pipe_fds[2];
    ASSERT_EQ(pipe(pipe_fds), 0);

    output_selector sel{};
    sel.set_selected(0);
    sel.output<0>().set_output(pipe_fds[1]);

    EXPECT_TRUE(sel.emit(event_type{EV_KEY, KEY_A, 1}));
    EXPECT_TRUE(sel.emit(event_type{EV_SYN, SYN_REPORT, 0}));
    EXPECT_TRUE(sel.emit(event_type{EV_KEY, KEY_A, 0}));
    EXPECT_TRUE(sel.emit(event_type{EV_SYN, SYN_REPORT, 0}));
    EXPECT_TRUE(sel.emit(event_type{EV_KEY, KEY_B, 1}));
    EXPECT_TRUE(sel.emit(event_type{EV_SYN, SYN_REPORT, 0}));

    close(pipe_fds[1]);

    input_event buf[6];
    auto const  n = read(pipe_fds[0], buf, sizeof(buf));
    close(pipe_fds[0]);

    ASSERT_EQ(n, static_cast<ssize_t>(sizeof(buf)));
    EXPECT_EQ(buf[0].type, EV_KEY);
    EXPECT_EQ(buf[0].code, KEY_A);
    EXPECT_EQ(buf[0].value, 1);
    EXPECT_EQ(buf[1].type, EV_SYN);
    EXPECT_EQ(buf[2].type, EV_KEY);
    EXPECT_EQ(buf[2].code, KEY_A);
    EXPECT_EQ(buf[2].value, 0);
    EXPECT_EQ(buf[3].type, EV_SYN);
    EXPECT_EQ(buf[4].type, EV_KEY);
    EXPECT_EQ(buf[4].code, KEY_B);
    EXPECT_EQ(buf[4].value, 1);
    EXPECT_EQ(buf[5].type, EV_SYN);
}

TEST(OutputSelector, CustomTemplateParams) {
    basic_output_selector<basic_std_output> sel{};
    sel.set_selected(0);

    int pipe_fds[2];
    ASSERT_EQ(pipe(pipe_fds), 0);
    sel.output<0>().set_output(pipe_fds[1]);

    EXPECT_TRUE(sel.emit(event_type{EV_KEY, KEY_A, 1}));

    close(pipe_fds[1]);

    input_event ev{};
    auto const  n = read(pipe_fds[0], &ev, sizeof(ev));
    close(pipe_fds[0]);

    ASSERT_EQ(n, static_cast<ssize_t>(sizeof(ev)));
    EXPECT_EQ(ev.type, EV_KEY);
    EXPECT_EQ(ev.code, KEY_A);
}

TEST(OutputSelector, WorksInPipeline) {
    captured_events.clear();

    int pipe_fds[2];
    ASSERT_EQ(pipe(pipe_fds), 0);

    auto pipeline =
      context
      | emit_all[{
        {.type = EV_KEY,      .code = KEY_A, .value = 1},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
        {.type = EV_KEY,      .code = KEY_A, .value = 0},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
    }]
      | output
      | record[captured_events];

    // Modify the pipeline's copy of output_switch after consteval construction
    auto& sel = pipeline.mod(output);
    sel.set_selected(0);
    sel.output<0>().set_output(pipe_fds[1]);

    pipeline();

    close(pipe_fds[1]);

    // record sits after the output_selector in the pipeline, so events pass
    // through output_selector (writing to the pipe) then into record.
    ASSERT_EQ(captured_events.size(), 4U);
    EXPECT_EQ(captured_events[0].code(), KEY_A);
    EXPECT_EQ(captured_events[0].value(), 1);
    EXPECT_EQ(captured_events[1].type(), EV_SYN);
    EXPECT_EQ(captured_events[2].code(), KEY_A);
    EXPECT_EQ(captured_events[2].value(), 0);
    EXPECT_EQ(captured_events[3].type(), EV_SYN);

    // The pipe should also have the raw events
    input_event buf[4];
    auto const  n = read(pipe_fds[0], buf, sizeof(buf));
    close(pipe_fds[0]);

    ASSERT_EQ(n, static_cast<ssize_t>(sizeof(buf)));
    EXPECT_EQ(buf[0].type, EV_KEY);
    EXPECT_EQ(buf[0].code, KEY_A);
    EXPECT_EQ(buf[0].value, 1);
}

TEST(OutputSelector, OutputSwitchGlobalVariable) {
    EXPECT_EQ(output.selected(), 0);
}

// ---------------------------------------------------------------------------
// output_flag_group: the --output flag's help must advertise the group's
// own default selection, not the hardcoded stdout one.
// ---------------------------------------------------------------------------

TEST(OutputSelector, FlagGroupHelpReflectsDefaultSelection) {
    EXPECT_TRUE(output_flags.get_flag().help.ends_with("(default: stdout)."));
    EXPECT_TRUE(output_flags["uinput"].get_flag().help.ends_with("(default: uinput)."));
    EXPECT_TRUE(output_flags["evtest"].get_flag().help.ends_with("(default: evtest)."));
    EXPECT_TRUE(output_flags["live-view"].get_flag().help.ends_with("(default: live-view)."));
}

TEST(OutputSelector, HelpListsTheGroupDefault) {
    static constexpr auto args   = arguments.add_flags(output_flags["uinput"]);
    char const* const     argv[] = {"prog", "--help"};
    auto const            parsed = args(2, argv);
    EXPECT_TRUE(parsed.help());

    testing::internal::CaptureStdout();
    parsed.print_help();
    auto const out = testing::internal::GetCapturedStdout();
    EXPECT_NE(out.find("(default: uinput)"), std::string::npos) << out;
}

TEST(OutputSelector, ConfigureAppliesGroupDefault) {
    static constexpr auto args   = arguments.add_flags(output_flags["uinput"]);
    char const* const     argv[] = {"prog"};
    auto const            parsed = args(1, argv);

    output_selector sel{};
    output_flags["uinput"].configure(sel, parsed);
    EXPECT_EQ(sel.selected(), 1);

    // An explicit --output still wins over the group default.
    char const* const argv2[] = {"prog", "--output", "evtest"};
    auto const        parsed2 = args(3, argv2);
    output_selector   sel2{};
    output_flags["uinput"].configure(sel2, parsed2);
    EXPECT_EQ(sel2.selected(), 2);

    // The stdout group keeps its own default.
    output_selector sel3{};
    output_flags.configure(sel3, parsed);
    EXPECT_EQ(sel3.selected(), 0);
}
