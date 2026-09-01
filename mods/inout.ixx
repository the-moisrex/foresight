// Created by moisrex on 6/9/25.

module;
#include <charconv>
#include <cstdint>
#include <ctime>
#include <linux/uinput.h>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
export module fs8.mods:inout;
import fs8.context;
import fs8.event;
import fs8.lib.evtest;
import fs8.traits;
import fs8.log;

export namespace fs8 {

    constexpr struct [[nodiscard]] basic_std_output : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        int file_descriptor = STDOUT_FILENO;

      public:
        constexpr explicit basic_std_output(int const inp_fd) noexcept : file_descriptor(inp_fd) {}

        constexpr void set_output(int const inp_fd) noexcept {
            file_descriptor = inp_fd;
        }

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool emit(event_type const& event) const noexcept;

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool operator()(event_type& event) const noexcept;
    } std_output;

    static_assert(OutputModifier<basic_std_output>, "Must be a output modifier.");

    constexpr struct [[nodiscard]] basic_from_input : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        int file_descriptor = STDIN_FILENO;

      public:
        constexpr explicit basic_from_input(int const inp_fd) noexcept : file_descriptor(inp_fd) {}

        context_action operator()(event_type& event, special_event const& tag) const noexcept;
    } from_input;

    /// Default evtest format: standard evtest text with libevdev annotations.
    struct [[nodiscard]] default_evtest_format {
        /// Parse one evtest text line. Returns false for non-event lines
        /// (headers, SYN_REPORT separators, junk).
        [[nodiscard]] bool parse(std::string_view line, parsed_evtest_event& out) const noexcept;

        /// Format an event into `buf`. Returns the written slice, or empty on failure.
        [[nodiscard]] std::string_view format(event_type const& event, std::span<char> buf) const noexcept;
    };

    static_assert(EvtestFormat<default_evtest_format>);

    /// Maximum length of an evtest-format event line (including trailing newline).
    inline constexpr std::size_t evtest_format_buf_size = 128;

    /// Read evtest-format text from a file descriptor, parse it, and feed events
    /// into the pipeline. Silently skips header/preamble lines and SYN_REPORT
    /// separators.
    template <EvtestFormat Format = default_evtest_format>
    struct [[nodiscard]] basic_from_evtest : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        int                 file_descriptor = STDIN_FILENO;
        Format              format{};
        mutable std::string line_buffer;

      public:
        constexpr basic_from_evtest() noexcept = default;

        constexpr explicit basic_from_evtest(int const inp_fd) noexcept : file_descriptor(inp_fd) {}

        consteval auto operator[](int const inp_fd) const noexcept {
            basic_from_evtest from;
            from.file_descriptor = inp_fd;
            return from;
        }

        constexpr void set_input(int const inp_fd) noexcept {
            file_descriptor = inp_fd;
        }

        context_action operator()(event_type& event, special_event const& tag) noexcept try {
            using enum context_action;
            if (tag.code != load_event.code) {
                return drop_event;
            }
            // Try to parse existing lines in the buffer first.
            while (true) {
                auto const newline = line_buffer.find('\n');
                if (newline == std::string::npos) {
                    break;
                }
                std::string_view const line{line_buffer.data(), newline};
                parsed_evtest_event    parsed;
                if (format.parse(line, parsed)) {
                    line_buffer.erase(0, newline + 1);
                    event = event_type{parsed.event};
                    event.source(sid(from_input));
                    return next;
                }
                // Not an event line (header / junk) — skip it and try the next.
                line_buffer.erase(0, newline + 1);
            }

            // Read more data from the fd.
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

                // If we got data, loop to try parsing again (we may now have a complete line).
                if (n > 0) {
                    continue;
                }
                // n < 0: read error — treat as drop.
                return drop_event;
            }
        } catch (...) {
            // Allocation failure in string operations: treat as drop.
            log("Unknown exception");
            return context_action::drop_event;
        }
    };

    /// Write events to a file descriptor in evtest text format.
    template <EvtestFormat Format = default_evtest_format>
    struct [[nodiscard]] basic_evtest_output : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        int    file_descriptor = STDOUT_FILENO;
        Format format{};

      public:
        constexpr explicit basic_evtest_output(int const inp_fd) noexcept : file_descriptor(inp_fd) {}

        constexpr void set_output(int const inp_fd) noexcept {
            file_descriptor = inp_fd;
        }

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool emit(event_type const& event) const noexcept {
            char       buf[evtest_format_buf_size];
            auto const text = format.format(event, buf);
            if (text.empty()) [[unlikely]] {
                return false;
            }
            return write(file_descriptor, text.data(), text.size()) == static_cast<ssize_t>(text.size());
        }

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool operator()(event_type& event) const noexcept {
            return emit(event);
        }
    };

    template <EvtestFormat Format>
    inline constexpr basic_evtest_output<Format> evtest_output;

    inline basic_from_evtest<> const from_evtest;
    constexpr auto                   to_evtest = evtest_output<default_evtest_format>;

    static_assert(OutputModifier<basic_evtest_output<>>, "Must be a output modifier.");

} // namespace fs8
