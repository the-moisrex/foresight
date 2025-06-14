// Created by moisrex on 6/9/25.

module;
#include <ctime>
#include <linux/uinput.h>
#include <oneapi/tbb/profiling.h>
#include <unistd.h>
#include <utility>
export module foresight.mods.inout;
import foresight.mods.context;
import foresight.mods.event;

export namespace foresight {

    constexpr struct [[nodiscard]] basic_output {
        using ev_type    = event_type::type_type;
        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

      private:
        int file_descriptor = STDOUT_FILENO;

      public:
        constexpr basic_output() noexcept = default;

        constexpr explicit basic_output(int const inp_fd) noexcept : file_descriptor(inp_fd) {}

        consteval basic_output(basic_output const&) noexcept            = default;
        constexpr basic_output(basic_output&&) noexcept                 = default;
        consteval basic_output& operator=(basic_output const&) noexcept = default;
        constexpr basic_output& operator=(basic_output&&) noexcept      = default;
        constexpr ~basic_output() noexcept                              = default;

        constexpr void set_output(int const inp_fd) noexcept {
            file_descriptor = inp_fd;
        }

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool emit(event_type const& event) const noexcept {
            return write(file_descriptor, &event.native(), sizeof(input_event)) == sizeof(input_event);
        }

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool emit(input_event const& event) const noexcept {
            return write(file_descriptor, &event, sizeof(input_event)) == sizeof(input_event);
        }

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool emit(ev_type const type, code_type const code, value_type const value) const noexcept {
            input_event event{};
            gettimeofday(&event.time, nullptr);
            event.type  = type;
            event.code  = code;
            event.value = value;
            return write(file_descriptor, &event, sizeof(input_event)) == sizeof(input_event);
        }

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool emit_syn() const noexcept {
            return emit(EV_SYN, SYN_REPORT, 0);
        }

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool operator()(Context auto& ctx) const noexcept {
            return write(file_descriptor, &ctx.event().native(), sizeof(input_event)) == sizeof(input_event);
        }
    } output;

    static_assert(output_modifier<basic_output>, "Must be a output modifier.");

    constexpr struct [[nodiscard]] basic_input {
      private:
        int file_descriptor = STDIN_FILENO;

      public:
        constexpr basic_input() noexcept = default;

        constexpr explicit basic_input(int const inp_fd) noexcept : file_descriptor(inp_fd) {}

        consteval basic_input(basic_input const&) noexcept            = default;
        constexpr basic_input(basic_input&&) noexcept                 = default;
        consteval basic_input& operator=(basic_input const&) noexcept = default;
        constexpr basic_input& operator=(basic_input&&) noexcept      = default;
        constexpr ~basic_input() noexcept                             = default;

        [[nodiscard]] context_action operator()(Context auto& ctx) const noexcept {
            using enum context_action;
            auto const res = read(file_descriptor, &ctx.event().native(), sizeof(input_event));
            if (res == 0) [[unlikely]] {
                return exit;
            }
            if (res != sizeof(input_event)) [[unlikely]] {
                return ignore_event;
            }
            return next;
        }
    } input;
} // namespace foresight
