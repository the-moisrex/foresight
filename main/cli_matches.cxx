#include "cli_args.hxx"

#include <cstdint>
#include <iostream>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import fs8;
import fs8.lib.evtest;
import fs8.lib.xkb;

int run_matches(std::span<std::string_view const> const patterns, bool const echo_events) {
    using std::println;

    // Reuse the same search engine the `typed` mod uses, so patterns behave
    // identically to library pipelines: `<...>`/`[...]`/`<<...>>`/`[[...]]`
    // and plain text.
    fs8::basic_search_engine    engine;
    std::vector<std::uint16_t>  trigger_ids;
    std::vector<fs8::aho_state> states;
    trigger_ids.reserve(patterns.size());
    states.reserve(patterns.size());
    for (auto const& pattern : patterns) {
        trigger_ids.emplace_back(engine.emplace_pattern(pattern));
        states.emplace_back(fs8::aho_state{0u});
    }

    fs8::xkb::basic_state keyboard_state;
    keyboard_state.initialize(fs8::xkb::get_default_keymap());

    bool        matched_any = false;
    std::string line;
    while (std::getline(std::cin, line)) {
        fs8::parsed_evtest_event parsed;
        if (!fs8::parse_evtest_line(line, parsed)) {
            continue;
        }
        fs8::event_type const event{parsed.event};
        for (std::size_t index = 0; index < trigger_ids.size(); ++index) {
            if (engine.search(event, trigger_ids[index], keyboard_state, states[index])) {
                if (echo_events) {
                    println("{}", line);
                }
                println("Matched {} at {:.6f}", patterns[index], parsed.time);
                matched_any = true;
            }
        }
    }

    return matched_any ? EXIT_SUCCESS : EXIT_FAILURE;
}
