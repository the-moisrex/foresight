// Created by moisrex on 6/9/25.

module;
#include <ctime>
#include <linux/uinput.h>
#include <unistd.h>
#include <utility>
export module foresight.mods.inout;
import foresight.mods.context;
import foresight.mods.event;

export namespace foresight {

    constexpr struct basic_output {
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

        void emit(event_type       event,
                  ev_type const    type,
                  code_type const  code,
                  value_type const value) const noexcept {
            event.type(type);
            event.code(code);
            event.value(value);
            std::ignore = write(file_descriptor, &event.native(), sizeof(event));
        }

        void emit(input_event      event,
                  ev_type const    type,
                  code_type const  code,
                  value_type const value) const noexcept {
            event.type  = type;
            event.code  = code;
            event.value = value;
            std::ignore = write(file_descriptor, &event, sizeof(event));
        }

        void emit(ev_type const type, code_type const code, value_type const value) const noexcept {
            input_event event{};
            gettimeofday(&event.time, nullptr);
            event.type  = type;
            event.code  = code;
            event.value = value;
            std::ignore = write(file_descriptor, &event, sizeof(event));
        }

        void emit_syn() const noexcept {
            emit(EV_SYN, SYN_REPORT, 0);
        }

        void operator()(Context auto& ctx) const noexcept {
            std::ignore = write(file_descriptor, &ctx.event().native(), sizeof(event_type));
        }
    } output;

    constexpr struct basic_input {
      private:
        int file_descriptor = STDOUT_FILENO;

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
            auto const res = read(file_descriptor, &ctx.event(), sizeof(event_type));
            if (res == 0) [[unlikely]] {
                return exit;
            }
            if (res != sizeof(event_type)) [[unlikely]] {
                return ignore_event;
            }
            return next;
        }
    } input;
} // namespace foresight
