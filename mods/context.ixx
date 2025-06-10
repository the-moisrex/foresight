// Created by moisrex on 6/8/25.

module;
#include <concepts>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>
export module foresight.mods.context;
import foresight.mods.event;

export namespace foresight {
    template <typename T>
    concept Context = requires(T ctx) { ctx.event(); };

    template <typename T>
    concept modifier = std::copyable<std::remove_cvref_t<T>>;

    template <typename Mod, typename T>
    concept has_mod = modifier<Mod> && Context<T> && requires(T ctx) {
        {
            ctx.template mod<Mod>()
        } noexcept -> std::same_as<Mod &>;
    };

    enum struct context_action : std::uint8_t {
        next,
        ignore_event,
        exit,
    };

    [[nodiscard]] constexpr std::string_view to_string(context_action const action) noexcept {
        using enum context_action;
        switch (action) {
            case next: return {"Next"};
            case ignore_event: return {"Ignore Event"};
            case exit: return {"Exit"};
            default: return {"<unknown>"};
        }
        std::unreachable();
    }

    template <typename ModT, typename CtxT>
    [[nodiscard]] constexpr context_action invoke_mod(ModT &mod, CtxT &ctx) {
        using enum context_action;
        using result = std::invoke_result_t<ModT, CtxT &>;
        if constexpr (std::same_as<result, bool>) {
            return mod(ctx) ? next : ignore_event;
        } else if constexpr (std::same_as<result, context_action>) {
            return mod(ctx);
        } else {
            mod(ctx);
            return next;
        }
    }

    template <modifier... Funcs>
    struct [[nodiscard]] basic_context : std::remove_cvref_t<Funcs>... {
        static constexpr bool is_nothrow =
          (std::is_nothrow_invocable_v<std::remove_cvref_t<Funcs>, basic_context &> && ...);

        constexpr basic_context() noexcept = default;

        consteval explicit basic_context(event_type inp_ev, Funcs... inp_funcs) noexcept
          : std::remove_cvref_t<Funcs>{inp_funcs}...,
            ev{std::move(inp_ev)} {}

        consteval basic_context(basic_context const &inp_ctx)                = default;
        constexpr basic_context(basic_context &&inp_ctx) noexcept            = default;
        constexpr basic_context &operator=(basic_context &&inp_ctx) noexcept = default;
        consteval basic_context &operator=(basic_context const &inp_ctx)     = default;
        constexpr ~basic_context() noexcept                                  = default;

        [[nodiscard]] constexpr event_type const &event() const noexcept {
            return ev;
        }

        [[nodiscard]] constexpr event_type &event() noexcept {
            return ev;
        }

        template <typename Func>
        [[nodiscard]] constexpr std::remove_cvref_t<Func> &mod() noexcept {
            return static_cast<std::remove_cvref_t<Func> &>(*this);
        }

        template <typename Func>
        [[nodiscard]] constexpr std::remove_cvref_t<Func> const &mod() const noexcept {
            return static_cast<std::remove_cvref_t<Func> const &>(*this);
        }

        template <typename Func>
        [[nodiscard]] constexpr std::remove_cvref_t<Func> &mod([[maybe_unused]] Func const &) noexcept {
            return static_cast<std::remove_cvref_t<Func> &>(*this);
        }

        template <typename Func>
        [[nodiscard]] constexpr std::remove_cvref_t<Func> const &mod(
          [[maybe_unused]] Func const &) const noexcept {
            return static_cast<std::remove_cvref_t<Func> const &>(*this);
        }

        template <modifier Mod>
        [[nodiscard]] consteval basic_context<Funcs..., Mod> operator|(Mod &&inp_mod) const noexcept {
            static_assert(std::is_invocable_v<std::remove_cvref_t<Mod>, basic_context &>,
                          "Mods must have a operator()(Context auto&) member function.");
            return basic_context<Funcs..., Mod>{ev, mod<Funcs>()..., inp_mod};
        }

        constexpr void operator()() noexcept(is_nothrow) {
            using enum context_action;
            static_assert((std::is_invocable_v<std::remove_cvref_t<Funcs>, basic_context &> && ...),
                          "Mods must have a operator()(Context auto&) member function.");

            for (;;) {
                auto action = next;
                std::ignore = (((action = invoke_mod(mod<Funcs>(), *this)), action == next) && ...);
                if (action == exit) {
                    break;
                }
            }
        }

      private:
        event_type ev;
    };

    constexpr basic_context<> context;

    [[nodiscard]] constexpr event_type::type_type type(Context auto const &ctx) noexcept {
        return ctx.event().type();
    }

    [[nodiscard]] constexpr event_type::code_type code(Context auto const &ctx) noexcept {
        return ctx.event().code();
    }

    [[nodiscard]] constexpr event_type::value_type value(Context auto const &ctx) noexcept {
        return ctx.event().value();
    }


} // namespace foresight
