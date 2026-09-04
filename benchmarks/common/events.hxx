#pragma once

#include <charconv>
#include <cstdio>
#include <string_view>
#include <vector>

import fs8.mods;

namespace bench {

    // Load events from a text file.
    // Format: one event per line, "type code value" (space-separated integers).
    // Lines starting with '#' are comments. Empty lines are ignored.
    inline std::vector<fs8::event_type> load_events(char const* path) {
        std::vector<fs8::event_type> events;
        std::FILE* f = std::fopen(path, "r");
        if (!f) {
            std::fprintf(stderr, "Failed to open event file: %s\n", path);
            return events;
        }

        char line[256];
        while (std::fgets(line, sizeof(line), f)) {
            // Skip comments and empty lines
            char* p = line;
            while (*p == ' ' || *p == '\t') ++p;
            if (*p == '#' || *p == '\n' || *p == '\0') continue;

            // Parse: type code value
            fs8::user_event ev{};
            auto const [p1, ec1] = std::from_chars(p, p + 64, ev.type);
            if (ec1 != std::errc{}) continue;
            p = const_cast<char*>(p1);
            while (*p == ' ' || *p == '\t') ++p;

            auto const [p2, ec2] = std::from_chars(p, p + 64, ev.code);
            if (ec2 != std::errc{}) continue;
            p = const_cast<char*>(p2);
            while (*p == ' ' || *p == '\t') ++p;

            auto const [p3, ec3] = std::from_chars(p, p + 64, ev.value);
            if (ec3 != std::errc{}) continue;

            events.emplace_back(ev);
        }
        std::fclose(f);
        return events;
    }

} // namespace bench
