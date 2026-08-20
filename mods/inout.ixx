// Created by moisrex on 6/9/25.

module;
#include <ctime>
#include <linux/uinput.h>
#include <unistd.h>
export module fs8.mods:inout;
import fs8.context;
import fs8.event;
import fs8.traits;

export namespace fs8 {

    constexpr struct [[nodiscard]] basic_output : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using ev_type    = event_type::type_type;
        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

      private:
        int file_descriptor = STDOUT_FILENO;

      public:
        constexpr explicit basic_output(int const inp_fd) noexcept : file_descriptor(inp_fd) {}

        constexpr void set_output(int const inp_fd) noexcept {
            file_descriptor = inp_fd;
        }

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool emit(event_type const& event) const noexcept;

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool emit(input_event const& event) const noexcept;

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool emit(ev_type type, code_type code, value_type value) const noexcept;

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool emit_syn() const noexcept;

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool operator()(event_type& event) const noexcept;
    } output;

    static_assert(OutputModifier<basic_output>, "Must be a output modifier.");

    constexpr struct [[nodiscard]] basic_from_input : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        int file_descriptor = STDIN_FILENO;

      public:
        constexpr explicit basic_from_input(int const inp_fd) noexcept : file_descriptor(inp_fd) {}

        context_action operator()(event_type& event, load_event_tag) const noexcept;
    } from_input;
} // namespace fs8
