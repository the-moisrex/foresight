// Created by moisrex on 8/25/26.

module;
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
module fs8.mods;

using fs8::context_action;
using fs8::event_type;

// ── helpers ─────────────────────────────────────────────────────────────────

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

    void write_usec(char* pos, std::int32_t const usec) noexcept {
        auto [ptr, ec]     = std::to_chars(pos, pos + 6, usec);
        auto const written = static_cast<std::size_t>(ptr - pos);
        if (written < 6) {
            auto const gap = 6 - written;
            for (auto i = written; i > 0u; --i) {
                pos[i + gap - 1] = pos[i - 1];
            }
            for (auto i = 0u; i < gap; ++i) {
                pos[i] = '0';
            }
        }
    }

    [[nodiscard]] int utf8_encode(char32_t cp, char* out) noexcept {
        if (cp < 0x80) {
            out[0] = static_cast<char>(cp);
            return 1;
        }
        if (cp < 0x800) {
            out[0] = static_cast<char>(0xC0 | (cp >> 6));
            out[1] = static_cast<char>(0x80 | (cp & 0x3F));
            return 2;
        }
        if (cp < 0x1'0000) {
            out[0] = static_cast<char>(0xE0 | (cp >> 12));
            out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out[2] = static_cast<char>(0x80 | (cp & 0x3F));
            return 3;
        }
        out[0] = static_cast<char>(0xF0 | (cp >> 18));
        out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out[3] = static_cast<char>(0x80 | (cp & 0x3F));
        return 4;
    }

    [[nodiscard]] std::string_view format_char(char32_t cp, char* buf) noexcept {
        if (cp == U'\n') {
            buf[0] = '\\';
            buf[1] = 'n';
            return {buf, 2};
        }
        if (cp == U'\r') {
            buf[0] = '\\';
            buf[1] = 'r';
            return {buf, 2};
        }
        if (cp == U'\t') {
            buf[0] = '\\';
            buf[1] = 't';
            return {buf, 2};
        }
        if (cp == U'\0') {
            return {buf, 0};
        }
        auto const n = utf8_encode(cp, buf);
        return {buf, static_cast<std::size_t>(n)};
    }

    /// Helper: work with a raw char array as a span.
    template <std::size_t N>
    [[nodiscard]] std::span<char, N> as_span(char (&arr)[N]) noexcept {
        return std::span<char, N>{arr};
    }
} // namespace

// ── live_view_format ────────────────────────────────────────────────────────

bool fs8::live_view_format::parse(std::string_view const line, parsed_evtest_event& out) const noexcept {
    return parse_evtest_line(line, out);
}

std::string_view fs8::live_view_format::format(event_type const& event, std::span<char> const buf) const noexcept {
    auto const tv   = event.native().time;
    auto const sec  = static_cast<std::int64_t>(tv.tv_sec);
    auto const usec = static_cast<std::int32_t>(tv.tv_usec);

    auto* pos = buf.data();

    pos = append(buf, pos, "Event: time ");
    if (pos == nullptr) [[unlikely]] {
        return {};
    }
    pos = append_int(buf, pos, sec);
    if (pos == nullptr) [[unlikely]] {
        return {};
    }
    pos = append(buf, pos, ".");
    if (pos == nullptr) [[unlikely]] {
        return {};
    }
    {
        auto const remaining = static_cast<std::size_t>(buf.data() + buf.size() - pos);
        if (remaining < 6) [[unlikely]] {
            return {};
        }
        write_usec(pos, usec);
        pos += 6;
    }

    if (is_syn(event)) {
        pos = append(buf, pos, ", -------------- SYN_REPORT ------------");
        if (pos == nullptr) [[unlikely]] {
            return {};
        }
    } else {
        std::string_view type_name = event.type_name();
        std::string_view code_name = event.code_name();

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

    pos = append(buf, pos, "\n");
    if (pos == nullptr) [[unlikely]] {
        return {};
    }

    return std::string_view{buf.data(), static_cast<std::size_t>(pos - buf.data())};
}

// ── live_view ───────────────────────────────────────────────────────────────

fs8::live_view::live_view(bool const force_terminal) noexcept
  : terminal_mode_{force_terminal || isatty(STDOUT_FILENO) == 1},
    use_ansi_{terminal_mode_} {}

fs8::device_live_state& fs8::live_view::state_for(std::uint32_t const id) {
    return devices_[id];
}

fs8::xkb::basic_state& fs8::live_view::get_xkb_state(device_live_state& st) {
    if (!st.xkb_state.has_value()) {
        if (!xkb_keymap_.has_value()) {
            xkb_keymap_.emplace(xkb_ctx_);
        }
        st.xkb_state.emplace(*xkb_keymap_);
    }
    return *st.xkb_state;
}

char32_t fs8::live_view::key_to_text(device_live_state& st, event_type const& event) {
    if (event.type() != EV_KEY) {
        return U'\0';
    }
    auto& xkb_st = get_xkb_state(st);
    return xkb::event2unicode(xkb_st, static_cast<key_event>(event));
}

bool fs8::live_view::direction_changed(mouse_accum const& m, int const dx, int const dy) const noexcept {
    if (!m.has_direction) {
        return false;
    }
    if (dx == 0 && dy == 0) {
        return false;
    }
    auto const fx   = static_cast<float>(dx);
    auto const fy   = static_cast<float>(dy);
    auto const mag1 = std::sqrt(m.prev_dir_x * m.prev_dir_x + m.prev_dir_y * m.prev_dir_y);
    auto const mag2 = std::sqrt(fx * fx + fy * fy);
    if (mag1 < 0.001f || mag2 < 0.001f) {
        return false;
    }
    auto const dot       = m.prev_dir_x * fx + m.prev_dir_y * fy;
    auto const cos_angle = dot / (mag1 * mag2);
    return cos_angle < direction_epsilon_;
}

std::string_view fs8::live_view::format_device_id(std::uint32_t const id, std::span<char> /*buf*/) noexcept {
    return fs8::to_source_string(id);
}

std::string_view fs8::live_view::format_time(event_type const& event, std::span<char> const buf) noexcept {
    auto const tv   = event.native().time;
    auto const sec  = static_cast<std::int64_t>(tv.tv_sec);
    auto const usec = static_cast<std::int32_t>(tv.tv_usec);

    auto* pos = buf.data();
    pos       = append_int(buf, pos, sec);
    if (pos == nullptr) [[unlikely]] {
        return {};
    }
    pos = append(buf, pos, ".");
    if (pos == nullptr) [[unlikely]] {
        return {};
    }
    {
        auto const remaining = static_cast<std::size_t>(buf.data() + buf.size() - pos);
        if (remaining < 6) [[unlikely]] {
            return {};
        }
        write_usec(pos, usec);
        pos += 6;
    }
    return std::string_view{buf.data(), static_cast<std::size_t>(pos - buf.data())};
}

void fs8::live_view::write_line(std::string_view const line, int const fd, bool const is_live_status) {
    if (is_live_status && use_ansi_) {
        (void) write(fd, ansi_clear_line.data(), ansi_clear_line.size());
        (void) write(fd, line.data(), line.size());
        (void) write(fd, "\r", 1);
    } else {
        (void) write(fd, line.data(), line.size());
        (void) write(fd, "\n", 1);
    }
}

void fs8::live_view::write_mouse_summary(std::uint32_t const id, mouse_accum const& m, int const fd) {
    char  line_buf[256];
    auto  span = as_span(line_buf);
    auto* pos  = line_buf;

    // Device ID prefix
    if (use_ansi_) {
        pos = append(span, pos, ansi_dim);
        if (pos == nullptr) {
            return;
        }
    }
    pos = append(span, pos, format_device_id(id, span));
    if (pos == nullptr) {
        return;
    }
    pos = append(span, pos, " ");
    if (pos == nullptr) {
        return;
    }
    if (use_ansi_) {
        pos = append(span, pos, ansi_reset);
        if (pos == nullptr) {
            return;
        }
    }

    // Timestamp (dim) - use last event time
    if (use_ansi_) {
        pos = append(span, pos, ansi_dim);
        if (pos == nullptr) {
            return;
        }
    }

    // Format time from last_event_time
    {
        auto const sec  = static_cast<std::int64_t>(m.last_event_time.count() / 1'000'000);
        auto const usec = static_cast<std::int32_t>(m.last_event_time.count() % 1'000'000);
        pos             = append_int(span, pos, sec);
        if (pos == nullptr) {
            return;
        }
        pos = append(span, pos, ".");
        if (pos == nullptr) {
            return;
        }
        auto const remaining = static_cast<std::size_t>(span.data() + span.size() - pos);
        if (remaining < 6) {
            return;
        }
        write_usec(pos, usec);
        pos += 6;
    }
    if (use_ansi_) {
        pos = append(span, pos, ansi_reset);
        if (pos == nullptr) {
            return;
        }
    }
    pos = append(span, pos, " ");
    if (pos == nullptr) {
        return;
    }

    if (use_ansi_) {
        pos = append(span, pos, ansi_bright_green);
        if (pos == nullptr) {
            return;
        }
    }
    pos = append(span, pos, "[mouse]");
    if (pos == nullptr) {
        return;
    }
    if (use_ansi_) {
        pos = append(span, pos, ansi_reset);
        if (pos == nullptr) {
            return;
        }
    }

    // Mouse X/Y
    if (m.delta_x != 0 || m.delta_y != 0) {
        if (use_ansi_) {
            pos = append(span, pos, ansi_green);
            if (pos == nullptr) {
                return;
            }
        }
        pos = append(span, pos, " ");
        if (pos == nullptr) {
            return;
        }
        pos = append_int(span, pos, m.delta_x);
        if (pos == nullptr) {
            return;
        }
        pos = append(span, pos, ",");
        if (pos == nullptr) {
            return;
        }
        pos = append_int(span, pos, m.delta_y);
        if (pos == nullptr) {
            return;
        }
        if (use_ansi_) {
            pos = append(span, pos, ansi_reset);
            if (pos == nullptr) {
                return;
            }
        }
    }

    // Wheel information - combine all into one total
    bool const has_wheel = m.wheel_hi != 0 || m.wheel != 0 || m.hwheel_hi != 0 || m.hwheel != 0;
    if (has_wheel) {
        if (use_ansi_) {
            pos = append(span, pos, ansi_cyan);
            if (pos == nullptr) {
                return;
            }
        }
        // Show combined wheel as single total
        auto const total_v = m.wheel_hi + m.wheel * 120;
        auto const total_h = m.hwheel_hi + m.hwheel * 120;
        if (total_v != 0) {
            pos = append(span, pos, " v=");
            if (pos == nullptr) {
                return;
            }
            pos = append_int(span, pos, total_v);
            if (pos == nullptr) {
                return;
            }
        }
        if (total_h != 0) {
            pos = append(span, pos, " h=");
            if (pos == nullptr) {
                return;
            }
            pos = append_int(span, pos, total_h);
            if (pos == nullptr) {
                return;
            }
        }
        if (use_ansi_) {
            pos = append(span, pos, ansi_reset);
            if (pos == nullptr) {
                return;
            }
        }
    }

    // Stats: event count, syn count, duration
    if (m.event_count > 0) {
        if (use_ansi_) {
            pos = append(span, pos, ansi_dim);
            if (pos == nullptr) {
                return;
            }
        }
        pos = append(span, pos, " (");
        if (pos == nullptr) {
            return;
        }
        pos = append_int(span, pos, static_cast<std::int64_t>(m.event_count));
        if (pos == nullptr) {
            return;
        }
        pos = append(span, pos, " events, ");
        if (pos == nullptr) {
            return;
        }
        pos = append_int(span, pos, static_cast<std::int64_t>(m.syn_count));
        if (pos == nullptr) {
            return;
        }
        pos = append(span, pos, " syns, ");
        if (pos == nullptr) {
            return;
        }
        auto const duration_us = m.last_event_time - m.first_event_time;
        auto const duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration_us);
        pos                    = append_int(span, pos, static_cast<std::int64_t>(duration_ms.count()));
        if (pos == nullptr) {
            return;
        }
        pos = append(span, pos, "ms)");
        if (pos == nullptr) {
            return;
        }
        if (use_ansi_) {
            pos = append(span, pos, ansi_reset);
            if (pos == nullptr) {
                return;
            }
        }
    }

    auto const line = std::string_view{line_buf, static_cast<std::size_t>(pos - line_buf)};
    write_line(line, fd, false); // Use \n so mouse history persists in scroll
}

void fs8::live_view::write_diagnostic(std::uint32_t const id, sanitizer_issue const issue, event_type const& event, int const fd) {
    char  line_buf[256];
    auto  span = as_span(line_buf);
    auto* pos  = line_buf;

    // Device ID prefix
    if (use_ansi_) {
        pos = append(span, pos, ansi_dim);
        if (pos == nullptr) {
            return;
        }
    }
    pos = append(span, pos, format_device_id(id, span));
    if (pos == nullptr) {
        return;
    }
    pos = append(span, pos, " ");
    if (pos == nullptr) {
        return;
    }
    if (use_ansi_) {
        pos = append(span, pos, ansi_reset);
        if (pos == nullptr) {
            return;
        }
    }

    if (use_ansi_) {
        pos = append(span, pos, ansi_bright_yellow);
        if (pos == nullptr) {
            return;
        }
    }
    pos = append(span, pos, "[diag] ");
    if (pos == nullptr) {
        return;
    }
    pos = append(span, pos, to_string(issue));
    if (pos == nullptr) {
        return;
    }
    if (use_ansi_) {
        pos = append(span, pos, ansi_reset);
        if (pos == nullptr) {
            return;
        }
    }
    pos = append(span, pos, " - ");
    if (pos == nullptr) {
        return;
    }
    if (use_ansi_) {
        pos = append(span, pos, ansi_cyan);
        if (pos == nullptr) {
            return;
        }
    }
    pos = append(span, pos, event.type_name());
    if (pos == nullptr) {
        return;
    }
    pos = append(span, pos, " ");
    if (pos == nullptr) {
        return;
    }
    if (use_ansi_) {
        pos = append(span, pos, ansi_yellow);
        if (pos == nullptr) {
            return;
        }
    }
    pos = append(span, pos, event.code_name());
    if (pos == nullptr) {
        return;
    }
    if (use_ansi_) {
        pos = append(span, pos, ansi_reset);
        if (pos == nullptr) {
            return;
        }
    }
    pos = append(span, pos, " ");
    if (pos == nullptr) {
        return;
    }
    if (use_ansi_) {
        pos = append(span, pos, (event.value() >= 0) ? ansi_green : ansi_red);
        if (pos == nullptr) {
            return;
        }
    }
    pos = append_int(span, pos, static_cast<std::int64_t>(event.value()));
    if (pos == nullptr) {
        return;
    }
    if (use_ansi_) {
        pos = append(span, pos, ansi_reset);
        if (pos == nullptr) {
            return;
        }
    }

    auto const line = std::string_view{line_buf, static_cast<std::size_t>(pos - line_buf)};
    write_line(line, fd, false);
}

void fs8::live_view::write_key_event(std::uint32_t const id, event_type const& event, int const fd, char32_t const text) {
    char       time_buf[32];
    auto const time_str = format_time(event, as_span(time_buf));

    char  line_buf[256];
    auto  span = as_span(line_buf);
    auto* pos  = line_buf;

    // Device ID prefix
    if (use_ansi_) {
        pos = append(span, pos, ansi_dim);
        if (pos == nullptr) {
            return;
        }
    }
    pos = append(span, pos, format_device_id(id, span));
    if (pos == nullptr) {
        return;
    }
    pos = append(span, pos, " ");
    if (pos == nullptr) {
        return;
    }
    if (use_ansi_) {
        pos = append(span, pos, ansi_reset);
        if (pos == nullptr) {
            return;
        }
    }

    // Timestamp (dim)
    if (use_ansi_) {
        pos = append(span, pos, ansi_dim);
        if (pos == nullptr) {
            return;
        }
    }
    pos = append(span, pos, time_str);
    if (pos == nullptr) {
        return;
    }
    if (use_ansi_) {
        pos = append(span, pos, ansi_reset);
        if (pos == nullptr) {
            return;
        }
    }
    pos = append(span, pos, " ");
    if (pos == nullptr) {
        return;
    }

    // Type name (colored by type)
    if (use_ansi_) {
        pos = append(span, pos, ansi_cyan);
        if (pos == nullptr) {
            return;
        }
    }
    pos = append(span, pos, event.type_name());
    if (pos == nullptr) {
        return;
    }
    if (use_ansi_) {
        pos = append(span, pos, ansi_reset);
        if (pos == nullptr) {
            return;
        }
    }
    pos = append(span, pos, " ");
    if (pos == nullptr) {
        return;
    }

    // Code name
    if (use_ansi_) {
        pos = append(span, pos, ansi_yellow);
        if (pos == nullptr) {
            return;
        }
    }
    pos = append(span, pos, event.code_name());
    if (pos == nullptr) {
        return;
    }
    if (use_ansi_) {
        pos = append(span, pos, ansi_reset);
        if (pos == nullptr) {
            return;
        }
    }
    pos = append(span, pos, " ");
    if (pos == nullptr) {
        return;
    }

    // Value (green for positive, red for negative)
    if (use_ansi_) {
        pos = append(span, pos, (event.value() >= 0) ? ansi_green : ansi_red);
        if (pos == nullptr) {
            return;
        }
    }
    pos = append_int(span, pos, static_cast<std::int64_t>(event.value()));
    if (pos == nullptr) {
        return;
    }
    if (use_ansi_) {
        pos = append(span, pos, ansi_reset);
        if (pos == nullptr) {
            return;
        }
    }

    if (text != U'\0') {
        pos = append(span, pos, "  ");
        if (pos == nullptr) {
            return;
        }
        if (use_ansi_) {
            pos = append(span, pos, ansi_bright_cyan);
            if (pos == nullptr) {
                return;
            }
        }
        pos = append(span, pos, "'");
        if (pos == nullptr) {
            return;
        }
        char       char_buf[8];
        auto const char_str = format_char(text, char_buf);
        pos                 = append(span, pos, char_str);
        if (pos == nullptr) {
            return;
        }
        pos = append(span, pos, "'");
        if (pos == nullptr) {
            return;
        }
        if (use_ansi_) {
            pos = append(span, pos, ansi_reset);
            if (pos == nullptr) {
                return;
            }
        }
    }

    auto const line = std::string_view{line_buf, static_cast<std::size_t>(pos - line_buf)};
    write_line(line, fd, false);
}

void fs8::live_view::write_generic_event(std::uint32_t const id, event_type const& event, int const fd) {
    char             fmt_buf[live_view_format_buf_size];
    live_view_format fmt;
    auto const       text = fmt.format(event, fmt_buf);
    if (!text.empty()) {
        if (use_ansi_) {
            // Add colors to the formatted text
            char                   colored_buf[live_view_format_buf_size * 2];
            auto                   span = as_span(colored_buf);
            auto*                  pos  = colored_buf;
            std::string_view const raw{text};

            // Device ID prefix
            pos = append(span, pos, ansi_dim);
            if (pos == nullptr) {
                return;
            }
            pos = append(span, pos, format_device_id(id, span));
            if (pos == nullptr) {
                return;
            }
            pos = append(span, pos, " ");
            if (pos == nullptr) {
                return;
            }
            pos = append(span, pos, ansi_reset);
            if (pos == nullptr) {
                return;
            }

            // Find and colorize the components
            // Format: "Event: time X.XXXXXX, type N (TYPE), code N (CODE), value N"
            auto const time_pos  = raw.find("Event: time ");
            auto const type_pos  = raw.find("type ");
            auto const code_pos  = raw.find("code ");
            auto const value_pos = raw.find("value ");

            if (time_pos != std::string_view::npos && type_pos != std::string_view::npos) {
                // Timestamp (dim)
                pos = append(span, pos, ansi_dim);
                if (pos == nullptr) {
                    return;
                }
                pos = append(span, pos, raw.substr(0, type_pos));
                if (pos == nullptr) {
                    return;
                }
                pos = append(span, pos, ansi_reset);
                if (pos == nullptr) {
                    return;
                }

                // Type (cyan)
                pos = append(span, pos, ansi_cyan);
                if (pos == nullptr) {
                    return;
                }
                if (code_pos != std::string_view::npos) {
                    pos = append(span, pos, raw.substr(type_pos, code_pos - type_pos));
                } else {
                    pos = append(span, pos, raw.substr(type_pos));
                }
                if (pos == nullptr) {
                    return;
                }
                pos = append(span, pos, ansi_reset);
                if (pos == nullptr) {
                    return;
                }

                if (code_pos != std::string_view::npos) {
                    // Code (yellow)
                    pos = append(span, pos, ansi_yellow);
                    if (pos == nullptr) {
                        return;
                    }
                    if (value_pos != std::string_view::npos) {
                        pos = append(span, pos, raw.substr(code_pos, value_pos - code_pos));
                    } else {
                        pos = append(span, pos, raw.substr(code_pos));
                    }
                    if (pos == nullptr) {
                        return;
                    }
                    pos = append(span, pos, ansi_reset);
                    if (pos == nullptr) {
                        return;
                    }
                }

                if (value_pos != std::string_view::npos) {
                    // Value (green/red)
                    auto const val_str   = raw.substr(value_pos);
                    // Extract the numeric value after "value "
                    auto const num_start = val_str.find(' ');
                    if (num_start != std::string_view::npos) {
                        auto const num_str  = val_str.substr(num_start + 1);
                        bool const negative = !num_str.empty() && num_str[0] == '-';
                        pos                 = append(span, pos, val_str.substr(0, num_start + 1));
                        if (pos == nullptr) {
                            return;
                        }
                        pos = append(span, pos, negative ? ansi_red : ansi_green);
                        if (pos == nullptr) {
                            return;
                        }
                        pos = append(span, pos, num_str);
                        if (pos == nullptr) {
                            return;
                        }
                        pos = append(span, pos, ansi_reset);
                        if (pos == nullptr) {
                            return;
                        }
                    } else {
                        pos = append(span, pos, val_str);
                        if (pos == nullptr) {
                            return;
                        }
                    }
                }
            } else {
                // Fallback: no color
                pos = append(span, pos, raw);
                if (pos == nullptr) {
                    return;
                }
            }

            auto const colored = std::string_view{colored_buf, static_cast<std::size_t>(pos - colored_buf)};
            write_line(colored, fd, false);
        } else {
            write_line(text, fd, false);
        }
    }
}

void fs8::live_view::flush_mouse(std::uint32_t const id, device_live_state& st, int const fd) {
    if (st.mouse.event_count == 0) {
        return;
    }
    write_mouse_summary(id, st.mouse, fd);
    st.mouse = mouse_accum{};
}

void fs8::live_view::flush_keyboard(std::uint32_t const /*id*/, device_live_state& /*st*/, int const /*fd*/) {}

void fs8::live_view::flush(int const fd) {
    for (auto& [id, st] : devices_) {
        flush_mouse(id, st, fd);
        st.pending_issue = sanitizer_issue::none;
    }
}

void fs8::live_view::process_event(event_type const& event, int const fd, sanitizer_issue const issue) {
    auto& st = state_for(event.source());

    if (issue != sanitizer_issue::none) {
        write_diagnostic(event.source(), issue, event, fd);
    }

    auto const type  = event.type();
    auto const code  = event.code();
    auto const value = event.value();

    constexpr auto is_mouse_rel = [](std::uint16_t t, std::uint16_t c) noexcept -> bool {
        return t
               == EV_REL
               && (c == REL_X || c == REL_Y || c == REL_WHEEL || c == REL_HWHEEL || c == REL_WHEEL_HI_RES || c == REL_HWHEEL_HI_RES);
    };

    if (is_mouse_rel(type, code)) {
        // Mark all held keys as having intervening events
        for (auto& [_, hk] : st.held_keys) {
            hk.has_intervening_events = true;
        }

        auto const now = event.micro_time();

        bool const should_flush =
          (st.mouse.event_count > 0 && direction_changed(st.mouse, (code == REL_X) ? value : 0, (code == REL_Y) ? value : 0))
          || (st.mouse.event_count > 0 && (now - st.mouse.last_event_time) > flush_timeout_);

        if (should_flush) {
            flush_mouse(event.source(), st, fd);
        }

        if (st.mouse.event_count == 0) {
            st.mouse.first_event_time = now;
        }

        if (code == REL_X) {
            st.mouse.delta_x += value;
        } else if (code == REL_Y) {
            st.mouse.delta_y += value;
        } else if (code == REL_WHEEL_HI_RES) {
            st.mouse.wheel_hi += value;
        } else if (code == REL_WHEEL) {
            st.mouse.wheel += value;
        } else if (code == REL_HWHEEL_HI_RES) {
            st.mouse.hwheel_hi += value;
        } else if (code == REL_HWHEEL) {
            st.mouse.hwheel += value;
        }
        st.mouse.event_count++;
        st.mouse.last_event_time = now;

        if (code == REL_X || code == REL_Y) {
            auto const fx = static_cast<float>((code == REL_X) ? value : 0);
            auto const fy = static_cast<float>((code == REL_Y) ? value : 0);
            if (fx != 0.f || fy != 0.f) {
                auto const mag         = std::sqrt(fx * fx + fy * fy);
                st.mouse.prev_dir_x    = fx / mag;
                st.mouse.prev_dir_y    = fy / mag;
                st.mouse.has_direction = true;
            }
        }
    } else if (type == EV_SYN) {
        st.mouse.syn_count++;
        // Don't flush on every SYN — let accumulation handle it via direction change or timeout
    } else if (type == EV_KEY) {
        if (st.mouse.event_count > 0) {
            flush_mouse(event.source(), st, fd);
        }

        auto const text = key_to_text(st, event);

        if (value == 1) {
            held_key hk{.code = code, .press_time = event.micro_time()};
            st.held_keys[code] = hk;
            write_key_event(event.source(), event, fd, text);
            return;
        }
        if (value == 0) {
            auto const it = st.held_keys.find(code);
            if (it != st.held_keys.end()) {
                auto const held_ms = std::chrono::duration_cast<std::chrono::milliseconds>(event.micro_time() - it->second.press_time);
                bool const had_intervening = it->second.has_intervening_events;
                st.held_keys.erase(it);

                if (!had_intervening) {
                    // No other events between press and release — overwrite press line with combined
                    if (use_ansi_) {
                        (void) write(fd, ansi_clear_line.data(), ansi_clear_line.size());
                        (void) write(fd, "\r", 1);
                    }

                    char       time_buf[32];
                    auto const time_str = format_time(event, as_span(time_buf));

                    char  line_buf[256];
                    auto  span = as_span(line_buf);
                    auto* pos  = line_buf;

                    // Device ID prefix
                    if (use_ansi_) {
                        pos = append(span, pos, ansi_dim);
                        if (pos == nullptr) {
                            return;
                        }
                    }
                    pos = append(span, pos, format_device_id(event.source(), span));
                    if (pos == nullptr) {
                        return;
                    }
                    pos = append(span, pos, " ");
                    if (pos == nullptr) {
                        return;
                    }
                    if (use_ansi_) {
                        pos = append(span, pos, ansi_reset);
                        if (pos == nullptr) {
                            return;
                        }
                    }

                    // Timestamp (dim)
                    if (use_ansi_) {
                        pos = append(span, pos, ansi_dim);
                        if (pos == nullptr) {
                            return;
                        }
                    }
                    pos = append(span, pos, time_str);
                    if (pos == nullptr) {
                        return;
                    }
                    if (use_ansi_) {
                        pos = append(span, pos, ansi_reset);
                        if (pos == nullptr) {
                            return;
                        }
                    }
                    pos = append(span, pos, " ");
                    if (pos == nullptr) {
                        return;
                    }

                    // Type (cyan)
                    if (use_ansi_) {
                        pos = append(span, pos, ansi_cyan);
                        if (pos == nullptr) {
                            return;
                        }
                    }
                    pos = append(span, pos, event.type_name());
                    if (pos == nullptr) {
                        return;
                    }
                    if (use_ansi_) {
                        pos = append(span, pos, ansi_reset);
                        if (pos == nullptr) {
                            return;
                        }
                    }
                    pos = append(span, pos, " ");
                    if (pos == nullptr) {
                        return;
                    }

                    // Code (yellow)
                    if (use_ansi_) {
                        pos = append(span, pos, ansi_yellow);
                        if (pos == nullptr) {
                            return;
                        }
                    }
                    pos = append(span, pos, event.code_name());
                    if (pos == nullptr) {
                        return;
                    }
                    if (use_ansi_) {
                        pos = append(span, pos, ansi_reset);
                        if (pos == nullptr) {
                            return;
                        }
                    }

                    // Combined 1 → 0 (green)
                    if (use_ansi_) {
                        pos = append(span, pos, ansi_green);
                        if (pos == nullptr) {
                            return;
                        }
                    }
                    pos = append(span, pos, " 1 → 0");
                    if (pos == nullptr) {
                        return;
                    }
                    if (use_ansi_) {
                        pos = append(span, pos, ansi_reset);
                        if (pos == nullptr) {
                            return;
                        }
                    }

                    // Held duration (cyan)
                    if (use_ansi_) {
                        pos = append(span, pos, ansi_bright_cyan);
                        if (pos == nullptr) {
                            return;
                        }
                    }
                    pos = append(span, pos, " [");
                    if (pos == nullptr) {
                        return;
                    }
                    pos = append_int(span, pos, static_cast<std::int64_t>(held_ms.count()));
                    if (pos == nullptr) {
                        return;
                    }
                    pos = append(span, pos, "ms]");
                    if (pos == nullptr) {
                        return;
                    }
                    if (use_ansi_) {
                        pos = append(span, pos, ansi_reset);
                        if (pos == nullptr) {
                            return;
                        }
                    }

                    // Unicode text
                    if (text != U'\0') {
                        pos = append(span, pos, "  ");
                        if (pos == nullptr) {
                            return;
                        }
                        if (use_ansi_) {
                            pos = append(span, pos, ansi_bright_cyan);
                            if (pos == nullptr) {
                                return;
                            }
                        }
                        pos = append(span, pos, "'");
                        if (pos == nullptr) {
                            return;
                        }
                        char       char_buf[8];
                        auto const char_str = format_char(text, char_buf);
                        pos                 = append(span, pos, char_str);
                        if (pos == nullptr) {
                            return;
                        }
                        pos = append(span, pos, "'");
                        if (pos == nullptr) {
                            return;
                        }
                        if (use_ansi_) {
                            pos = append(span, pos, ansi_reset);
                            if (pos == nullptr) {
                                return;
                            }
                        }
                    }

                    auto const line = std::string_view{line_buf, static_cast<std::size_t>(pos - line_buf)};
                    write_line(line, fd, false);
                    return;
                }
                // Had intervening events — just print release
            }
        }

        write_key_event(event.source(), event, fd, text);
    } else {
        if (st.mouse.event_count > 0) {
            flush_mouse(event.source(), st, fd);
        }
        write_generic_event(event.source(), event, fd);
    }
}

// ── basic_from_live_view ────────────────────────────────────────────────────

template <fs8::EvtestFormat Format>
fs8::context_action fs8::basic_from_live_view<Format>::operator()(event_type& event, special_event const& tag) noexcept {
    using enum context_action;
    if (tag.code != load_event.code) {
        return drop_event;
    }

    // 1. Try to parse lines already in the buffer.
    if (try_parse_buffered(event)) {
        return next;
    }

    // 2. Read more data from the fd, then try parsing again.
    while (true) {
        auto const buf_size = line_buffer.size();
        auto const cap      = buf_size + 4096;
        line_buffer.resize(cap);
        auto const n = read(file_descriptor, line_buffer.data() + buf_size, 4096);
        line_buffer.resize(buf_size + static_cast<std::size_t>(n));

        if (n == 0) {
            // EOF — try to flush any remaining partial line.
            if (!line_buffer.empty()) {
                parsed_evtest_event parsed;
                if (format.parse(line_buffer, parsed)) {
                    line_buffer.clear();
                    event = event_type{parsed.event};
                    event.source(sid(from_input));
                    return next;
                }
                line_buffer.clear();
            }
            return exit;
        }

        // Parse every complete line in the buffer.
        while (true) {
            auto const nl = line_buffer.find('\n');
            if (nl == std::string::npos) {
                break;
            }
            std::string_view const line{line_buffer.data(), nl};
            parsed_evtest_event    parsed;
            if (format.parse(line, parsed)) {
                line_buffer.erase(0, nl + 1);
                event = event_type{parsed.event};
                event.source(sid(from_input));
                return next;
            }
            line_buffer.erase(0, nl + 1);
        }

        if (n > 0) {
            continue;
        }
        // n < 0: read error — treat as drop.
        return drop_event;
    }
}

template struct fs8::basic_from_live_view<fs8::live_view_format>;
