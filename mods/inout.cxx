// Created by moisrex on 8/17/26.

module;
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <linux/uinput.h>
#include <span>
#include <string_view>
#include <unistd.h>
module fs8.mods;
import fs8.event;
import fs8.context;
import fs8.lib.evtest;

using fs8::context_action;
using fs8::event_type;

// ── basic_output (raw binary) ───────────────────────────────────────────────

bool fs8::basic_output::emit(event_type const& event) const noexcept {
    return write(file_descriptor, &event.native(), sizeof(input_event)) == sizeof(input_event);
}

bool fs8::basic_output::emit(input_event const& event) const noexcept {
    return write(file_descriptor, &event, sizeof(input_event)) == sizeof(input_event);
}

bool fs8::basic_output::emit(ev_type const type, code_type const code, value_type const value) const noexcept {
    input_event event{};
    gettimeofday(&event.time, nullptr);
    event.type  = type;
    event.code  = code;
    event.value = value;
    return write(file_descriptor, &event, sizeof(input_event)) == sizeof(input_event);
}

bool fs8::basic_output::emit_syn() const noexcept {
    return emit(EV_SYN, SYN_REPORT, 0);
}

bool fs8::basic_output::operator()(event_type& event) const noexcept {
    return write(file_descriptor, &event.native(), sizeof(input_event)) == sizeof(input_event);
}

// ── basic_from_input (raw binary) ───────────────────────────────────────────

context_action fs8::basic_from_input::operator()(event_type& event, load_event_tag) const noexcept {
    using enum context_action;
    auto const res = read(file_descriptor, &event.native(), sizeof(input_event));
    if (res == 0) [[unlikely]] {
        return exit;
    }
    if (res != sizeof(input_event)) [[unlikely]] {
        return drop_event;
    }
    event.source(device_id::stdin);
    return next;
}

// ── default_evtest_format helpers ────────────────────────────────────────────

namespace {
    [[nodiscard]] char* append(std::span<char> const buf, char* pos, std::string_view const str) noexcept {
        auto const remaining = static_cast<std::size_t>(buf.data() + buf.size() - pos);
        if (str.size() > remaining) [[unlikely]] {
            return nullptr;
        }
        std::memcpy(pos, str.data(), str.size());
        return pos + str.size();
    }

    template <std::integral T>
    [[nodiscard]] char* append_int(std::span<char> const buf, char* pos, T const value) noexcept {
        auto const remaining = static_cast<std::size_t>(buf.data() + buf.size() - pos);
        if (remaining < 21) [[unlikely]] {
            return nullptr;
        }
        auto const [ptr, ec] = std::to_chars(pos, pos + remaining, value);
        if (ec != std::errc{}) [[unlikely]] {
            return nullptr;
        }
        return ptr;
    }

    /// Write `digits` zero-padded microseconds. No overflow check needed — caller
    /// must guarantee at least 6 bytes of space.
    void write_usec(char* pos, std::int32_t const usec) noexcept {
        auto [ptr, ec]     = std::to_chars(pos, pos + 6, usec);
        // Pad with leading zeros if the number had fewer than 6 digits.
        auto const written = static_cast<std::size_t>(ptr - pos);
        if (written < 6) {
            // Shift the number right and fill the gap with zeros.
            auto const gap = 6 - written;
            for (auto i = written; i > 0u; --i) {
                pos[i + gap - 1] = pos[i - 1];
            }
            for (auto i = 0u; i < gap; ++i) {
                pos[i] = '0';
            }
        }
    }
} // namespace

// ── default_evtest_format ───────────────────────────────────────────────────

bool fs8::default_evtest_format::parse(std::string_view const line, parsed_evtest_event& out) const noexcept {
    return parse_evtest_line(line, out);
}

std::string_view fs8::default_evtest_format::format(event_type const& event, std::span<char> const buf) const noexcept {
    auto const       tv        = event.native().time;
    auto const       sec       = static_cast<std::int64_t>(tv.tv_sec);
    auto const       usec      = static_cast<std::int32_t>(tv.tv_usec);
    std::string_view type_name = event.type_name();
    std::string_view code_name = event.code_name();

    auto* pos = buf.data();

    // "Event: time "
    pos = append(buf, pos, "Event: time ");
    if (pos == nullptr) [[unlikely]] {
        return {};
    }

    // seconds
    pos = append_int(buf, pos, sec);
    if (pos == nullptr) [[unlikely]] {
        return {};
    }

    // "."
    pos = append(buf, pos, ".");
    if (pos == nullptr) [[unlikely]] {
        return {};
    }

    // microseconds (zero-padded to 6 digits)
    {
        auto const remaining = static_cast<std::size_t>(buf.data() + buf.size() - pos);
        if (remaining < 6) [[unlikely]] {
            return {};
        }
        write_usec(pos, usec);
        pos += 6;
    }

    if (is_syn(event)) {
        // SYN_REPORT separator
        pos = append(buf, pos, ", -------------- SYN_REPORT ------------");
        if (pos == nullptr) [[unlikely]] {
            return {};
        }
    } else {
        // ", type N (TYPE_NAME), code N (CODE_NAME), value N"
        pos = append(buf, pos, ", type ");
        if (pos == nullptr) [[unlikely]] {
            return {};
        }
        pos = append_int(buf, pos, static_cast<std::int64_t>(event.type()));
        if (pos == nullptr) [[unlikely]] {
            return {};
        }
        if (!type_name.empty()) {
            pos = append(buf, pos, " (");
            if (pos == nullptr) [[unlikely]] {
                return {};
            }
            pos = append(buf, pos, type_name);
            if (pos == nullptr) [[unlikely]] {
                return {};
            }
            pos = append(buf, pos, ")");
            if (pos == nullptr) [[unlikely]] {
                return {};
            }
        }

        pos = append(buf, pos, ", code ");
        if (pos == nullptr) [[unlikely]] {
            return {};
        }
        pos = append_int(buf, pos, static_cast<std::int64_t>(event.code()));
        if (pos == nullptr) [[unlikely]] {
            return {};
        }
        if (!code_name.empty()) {
            pos = append(buf, pos, " (");
            if (pos == nullptr) [[unlikely]] {
                return {};
            }
            pos = append(buf, pos, code_name);
            if (pos == nullptr) [[unlikely]] {
                return {};
            }
            pos = append(buf, pos, ")");
            if (pos == nullptr) [[unlikely]] {
                return {};
            }
        }

        pos = append(buf, pos, ", value ");
        if (pos == nullptr) [[unlikely]] {
            return {};
        }
        pos = append_int(buf, pos, static_cast<std::int64_t>(event.value()));
        if (pos == nullptr) [[unlikely]] {
            return {};
        }
    }

    // Trailing newline so the line-based reader can delimit events.
    pos = append(buf, pos, "\n");
    if (pos == nullptr) [[unlikely]] {
        return {};
    }

    return std::string_view{buf.data(), static_cast<std::size_t>(pos - buf.data())};
}
