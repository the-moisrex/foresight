// Created by moisrex on 6/8/25.

module;
#include <concepts>
#include <cstdint>
#include <linux/input.h>
#include <string_view>
#include <type_traits>
#include <utility>
export module foresight.mods.context;
export import foresight.mods.event;
import foresight.mods.event;

namespace foresight {
    // Base case: index 0, type is the first type T
    template <std::size_t I, typename T, typename... Ts>
    struct type_at_impl {
        using type = typename type_at_impl<I - 1, Ts...>::type;
    };

    // Specialization for index 0
    template <typename T, typename... Ts>
    struct type_at_impl<0, T, Ts...> {
        using type = T;
    };

    template <std::size_t I, typename... Ts>
    using type_at = typename type_at_impl<I, Ts...>::type;

    template <std::size_t Index, typename F, typename T1, typename... Ts>
    struct index_at_impl {
        static constexpr auto value = index_at_impl<Index + 1, F, Ts...>::value;
    };

    template <std::size_t Index, typename F, typename... Ts>
    struct index_at_impl<Index, F, F, Ts...> {
        static constexpr auto value = Index;
    };

    template <typename F, typename... Ts>
    constexpr std::size_t index_at = index_at_impl<0, F, Ts...>::value;

} // namespace foresight

export namespace foresight {
    template <typename T>
    concept Context = requires(T ctx) { ctx.event(); };

    template <typename T>
    concept modifier = std::copyable<std::remove_cvref_t<T>>;

    template <typename T>
    concept output_modifier =
      modifier<T> &&
      requires(T                      out,
               event_type             event,
               event_type::code_type  code,
               event_type::type_type  type,
               event_type::value_type value) {
          {
              out.emit(event)
          } noexcept -> std::same_as<bool>;

          {
              out.emit(type, code, value)
          } noexcept -> std::same_as<bool>;

          {
              out.emit_syn()
          } noexcept -> std::same_as<bool>;
      };

    constexpr struct output_mod_t {
        template <typename T>
        static constexpr bool value = output_modifier<T>;
    } output_mod;

    template <typename Mod, typename T>
    concept has_mod = modifier<Mod> && Context<T> && requires(T ctx) {
        {
            ctx.template mod<Mod>()
        } noexcept -> std::same_as<Mod &>;
    };

    template <typename ModConcept, typename...>
    struct mod_of_t {
        using type = std::remove_cvref_t<ModConcept>;
    };

    template <typename ModConcept, typename Func, typename... Funcs>
        requires(std::remove_cvref_t<ModConcept>::template value<Func>)
    struct mod_of_t<ModConcept, Func, Funcs...> {
        using type = std::remove_cvref_t<Func>;
    };

    template <typename ModConcept, typename Func, typename... Funcs>
    struct mod_of_t<ModConcept, Func, Funcs...> : mod_of_t<ModConcept, Funcs...> {};

    template <typename ModConcept, typename... Funcs>
    using mod_of = typename mod_of_t<ModConcept, Funcs...>::type;

    enum struct [[nodiscard]] context_action : std::uint8_t {
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
    constexpr context_action invoke_mod(ModT &mod, CtxT &ctx) {
        using enum context_action;
        if constexpr (std::invocable<ModT, CtxT &>) {
            using result = std::invoke_result_t<ModT, CtxT &>;
            if constexpr (std::same_as<result, bool>) {
                return mod(ctx) ? next : ignore_event;
            } else if constexpr (std::same_as<result, context_action>) {
                return mod(ctx);
            } else {
                mod(ctx);
                return next;
            }
        } else if constexpr (std::invocable<ModT, event_type &>) {
            auto &event  = ctx.event();
            using result = std::invoke_result_t<ModT, event_type &>;
            if constexpr (std::same_as<result, bool>) {
                return mod(event) ? next : ignore_event;
            } else if constexpr (std::same_as<result, context_action>) {
                return mod(event);
            } else {
                mod(event);
                return next;
            }
        } else if constexpr (std::invocable<ModT>) {
            using result = std::invoke_result_t<ModT>;
            if constexpr (std::same_as<result, bool>) {
                return mod() ? next : ignore_event;
            } else if constexpr (std::same_as<result, context_action>) {
                return mod();
            } else {
                mod();
                return next;
            }
        } else {
            static_assert(false, "We're not able to run this function.");
        }
    }


    template <std::size_t Index, modifier... Funcs>
    struct [[nodiscard]] basic_context_view;

    template <modifier... Funcs>
    struct [[nodiscard]] basic_context : std::remove_cvref_t<Funcs>... {
        // static constexpr bool is_nothrow =
        //   (std::is_nothrow_invocable_v<std::remove_cvref_t<Funcs>, basic_context &> && ...);
        static constexpr bool is_nothrow = true;


        constexpr basic_context() noexcept = default;

        consteval explicit basic_context(event_type inp_ev, std::remove_cvref_t<Funcs>... inp_funcs) noexcept
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

        constexpr void event(input_event const inp_event) noexcept {
            ev = inp_event;
        }

        constexpr void event(event_type const inp_event) noexcept {
            ev = inp_event;
        }

        template <typename Func>
            requires((std::same_as<mod_of<Func, Funcs...>, Funcs> || ...))
        [[nodiscard]] constexpr auto &mod() noexcept {
            return static_cast<mod_of<Func, Funcs...> &>(*this);
        }

        template <typename Func>
            requires((std::same_as<mod_of<Func, Funcs...>, Funcs> || ...))
        [[nodiscard]] constexpr auto const &mod() const noexcept {
            return static_cast<mod_of<Func, Funcs...> const &>(*this);
        }

        template <typename Func>
            requires((std::same_as<mod_of<Func, Funcs...>, Funcs> || ...))
        [[nodiscard]] constexpr auto &mod([[maybe_unused]] Func const &inp_mod) noexcept {
            return static_cast<mod_of<Func, Funcs...> &>(*this);
        }

        template <typename Func>
            requires((std::same_as<mod_of<Func, Funcs...>, Funcs> || ...))
        [[nodiscard]] constexpr auto const &mod([[maybe_unused]] Func const &inp_mod) const noexcept {
            return static_cast<mod_of<Func, Funcs...> const &>(*this);
        }

        template <std::size_t Index = 0>
        [[nodiscard]] constexpr auto const &mod() const noexcept {
            return static_cast<type_at<Index, Funcs...> const &>(*this);
        }

        template <std::size_t Index = 0>
        [[nodiscard]] constexpr auto &mod() noexcept {
            return static_cast<type_at<Index, Funcs...> &>(*this);
        }

        template <modifier Mod>
        [[nodiscard]] consteval auto operator|(Mod &&inp_mod) const noexcept {
            static_assert(std::is_invocable_v<std::remove_cvref_t<Mod>, basic_context &>,
                          "Mods must have a operator()(Context auto&) member function.");
            return basic_context<std::remove_cvref_t<Funcs>..., std::remove_cvref_t<Mod>>{
              ev,
              mod<Funcs>()...,
              inp_mod};
        }

        template <std::size_t Index = 0, Context CtxT = basic_context>
            requires(Index <= sizeof...(Funcs) - 1)
        constexpr context_action reemit(CtxT &ctx) const noexcept(CtxT::is_nothrow) {
            using enum context_action;
            auto const action = invoke_mod(ctx.template mod<Index>(), ctx);
            if constexpr (Index >= sizeof...(Funcs) - 1) {
                return action;
            } else {
                if (action != next) {
                    return action;
                }
                return ctx.template reemit<Index + 1U>(ctx);
            }
        }

        template <std::size_t Index = 0>
        constexpr context_action reemit() noexcept(is_nothrow) {
            return reemit<Index>(*this);
        }

        /**
         * Usage:
         *   ctx.fork_emit(*this);
         */
        template <modifier Func>
        constexpr context_action fork_emit([[maybe_unused]] Func const &inp_mod) noexcept(is_nothrow) {
            return reemit<index_at<std::remove_cvref_t<Func>, Funcs...> + 1U>();
        }

        template <modifier Func>
        constexpr context_action fork_emit() noexcept(is_nothrow) {
            return reemit<index_at<std::remove_cvref_t<Func>, Funcs...> + 1U>();
        }

        template <modifier Func, Context CtxT = basic_context>
        constexpr context_action fork_emit(CtxT &ctx, [[maybe_unused]] Func const &inp_mod) const
          noexcept(CtxT::is_nothrow) {
            return ctx.template reemit<index_at<std::remove_cvref_t<Func>, Funcs...> + 1U>(ctx);
        }

        template <modifier Func>
        constexpr context_action fork_emit([[maybe_unused]] Func const &inp_mod,
                                           event_type const            &inp_event) noexcept(is_nothrow) {
            auto const cur_ev = std::exchange(ev, inp_event);
            auto const res    = fork_emit(inp_mod);
            ev                = cur_ev;
            return res;
        }

        template <modifier Func>
        constexpr context_action fork_emit(event_type const &inp_event) noexcept(is_nothrow) {
            auto const cur_ev = std::exchange(ev, inp_event);
            auto const res    = fork_emit<Func>();
            ev                = cur_ev;
            return res;
        }

        template <std::size_t Index>
        constexpr context_action fork_emit(event_type const &inp_event) noexcept(is_nothrow) {
            auto const cur_ev = std::exchange(ev, inp_event);
            auto const res    = reemit<Index>();
            ev                = cur_ev;
            return res;
        }

        template <modifier Func, typename... Args>
            requires(std::constructible_from<event_type, Args...> && sizeof...(Args) >= 2)
        constexpr context_action fork_emit([[maybe_unused]] Func const &inp_mod, Args &&...args) noexcept(
          is_nothrow) {
            return fork_emit<Func>(inp_mod, event_type{std::forward<Args>(args)...});
        }

        template <modifier Func>
        constexpr auto fork_view([[maybe_unused]] Func const &inp_mod) noexcept {
            return basic_context_view<index_at<std::remove_cvref_t<Func>, Funcs...> + 1U, Funcs...>{*this};
        }

        // Re-Emit the context
        constexpr context_action reemit_all() noexcept(is_nothrow) {
            using enum context_action;
            return [this]<std::size_t... I>(std::index_sequence<I...>) constexpr noexcept(is_nothrow) {
                auto action = next;
                std::ignore = (((action = invoke_mod(mod<I>(), *this)), action == next) && ...);
                return action;
            }(std::make_index_sequence<sizeof...(Funcs)>{});
        }

        constexpr void operator()() noexcept(is_nothrow) {
            using enum context_action;
            static_assert((std::is_invocable_v<std::remove_cvref_t<Funcs>, basic_context &> && ...),
                          "Mods must have a operator()(Context auto&) member function.");

            for (;;) {
                if (reemit_all() == exit) {
                    break;
                }
            }
        }

      private:
        event_type ev;
    };

    template <std::size_t Index, modifier... Funcs>
    struct [[nodiscard]] basic_context_view {
        using ctx_type                   = basic_context<Funcs...>;
        static constexpr bool is_nothrow = ctx_type::is_nothrow;

      private:
        ctx_type *ctx;

      public:
        explicit constexpr basic_context_view(ctx_type &inp_ctx) noexcept : ctx(&inp_ctx) {}

        constexpr basic_context_view(basic_context_view const &)                = default;
        constexpr basic_context_view(basic_context_view &&) noexcept            = default;
        constexpr basic_context_view &operator=(basic_context_view const &)     = default;
        constexpr basic_context_view &operator=(basic_context_view &&) noexcept = default;
        constexpr ~basic_context_view() noexcept                                = default;

        [[nodiscard]] constexpr ctx_type &context() noexcept {
            return *ctx;
        }

        [[nodiscard]] constexpr ctx_type const &context() const noexcept {
            return *ctx;
        }

        constexpr context_action fork_emit() noexcept(is_nothrow) {
            return ctx->template reemit<Index>();
        }

        constexpr context_action fork_emit(event_type const &event) noexcept(is_nothrow) {
            return ctx->template fork_emit<Index>(event);
        }

        [[nodiscard]] constexpr event_type const &event() const noexcept {
            return ctx->event();
        }

        [[nodiscard]] constexpr event_type &event() noexcept {
            return ctx->event();
        }
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
