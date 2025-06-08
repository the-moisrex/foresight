// Created by moisrex on 6/8/25.

module;
#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>
export module context;
import foresight.mods.event;

export namespace foresight {
    template <typename Func = void>
    struct basic_context;

    template <typename T>
    concept context = requires(T ctx) {
        ctx.next();
        ctx.event();
    };

    template <typename Func>
    struct basic_context {
        using function_type = Func;
        static_assert(std::is_invocable_v<Func, basic_context<>>, "Function is not invokable.");

        explicit basic_context(event_type &&inp_ev) noexcept : ev{std::move(inp_ev)} {}

        void next() const noexcept(std::is_nothrow_invocable_v<Func, basic_context>) {
            func(*this);
        }

      private:
        event_type                 ev;
        [[no_unique_address]] Func func;
    };

    template <>
    struct basic_context<void> {
        using function_type = std::function<void(basic_context const &)>;

        explicit basic_context(event_type &&inp_ev) : ev{std::move(inp_ev)} {}

        void next() const {
            func(*this);
        }

        [[nodiscard]] event_type const &event() const noexcept {
            return ev;
        }

      private:
        event_type    ev;
        function_type func;
    };

    [[nodiscard]] event_type::type_type type(context auto const &ctx) noexcept {
        return ctx.event().type();
    }

    [[nodiscard]] event_type::code_type code(context auto const &ctx) noexcept {
        return ctx.event().code();
    }

    [[nodiscard]] event_type::value_type value(context auto const &ctx) noexcept {
        return ctx.event().value();
    }

} // namespace foresight
