// Created by moisrex on 6/8/25.

module;
#include <concepts>
#include <cstdint>
#include <linux/input.h>
#include <string_view>
#include <tuple>
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
    constexpr std::size_t index_at =
      index_at_impl<0, std::remove_cvref_t<F>, std::remove_cvref_t<Ts>...>::value;

} // namespace foresight

export namespace foresight {
    template <typename T>
    concept Context = requires(T ctx) { ctx.event(); };

    template <typename T>
    concept Modifier = std::copyable<std::remove_cvref_t<T>>;

    template <typename T>
    concept OutputModifier =
      Modifier<T> &&
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
        static constexpr bool value = OutputModifier<T>;
    } output_mod;

    template <bool... T>
    [[nodiscard]] consteval bool blowup_if(bool const res = true) noexcept {
        static_assert(!static_cast<bool>((T && ...)), "Reverse the order of params.");
        return res;
    }

    template <typename Mod, typename CtxT>
    concept has_mod =
      blowup_if<Modifier<CtxT>, Context<Mod>>() && Modifier<Mod> && Context<CtxT> && requires(CtxT ctx) {
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

    // For something<X, Y, Z>
    template <template <typename...> typename TT, typename... T, typename... U, typename... Funcs>
    struct mod_of_t<TT<T...>, TT<U...>, Funcs...> {
        using type = TT<U...>;
    };

    /// For uinput_picker<N>
    template <template <auto...> typename TT, auto... T, auto... U, typename... Funcs>
    struct mod_of_t<TT<T...>, TT<U...>, Funcs...> {
        using type = TT<U...>;
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

    /// Invoke Condition
    template <typename CondT, typename CtxT>
    constexpr bool invoke_cond(CondT &cond, CtxT &ctx) {
        using enum context_action;
        if constexpr (std::invocable<CondT, CtxT &>) {
            using result = std::invoke_result_t<CondT, CtxT &>;
            if constexpr (std::same_as<result, bool>) {
                return cond(ctx);
            } else if constexpr (std::same_as<result, context_action>) {
                return cond(ctx) == next;
            } else {
                cond(ctx);
                return true;
            }
        } else if constexpr (std::invocable<CondT, event_type &>) {
            auto &event  = ctx.event();
            using result = std::invoke_result_t<CondT, event_type &>;
            if constexpr (std::same_as<result, bool>) {
                return cond(event);
            } else if constexpr (std::same_as<result, context_action>) {
                return cond(event) == next;
            } else {
                cond(event);
                return true;
            }
        } else if constexpr (std::invocable<CondT>) {
            using result = std::invoke_result_t<CondT>;
            if constexpr (std::same_as<result, bool>) {
                return cond();
            } else if constexpr (std::same_as<result, context_action>) {
                return cond() == next;
            } else {
                cond();
                return true;
            }
        } else {
            static_assert(false, "We're not able to run this function.");
        }
    }

    constexpr struct no_init_type {
    } no_init;

    /// Run the functions and give them the specified context
    template <Context CtxT, typename... Funcs>
    constexpr context_action invoke_mods(CtxT &ctx, std::tuple<Funcs...> &funcs) noexcept(CtxT::is_nothrow) {
        using enum context_action;
        return [&]<std::size_t... I>(std::index_sequence<I...>) constexpr noexcept(CtxT::is_nothrow) {
            auto action = next;
            std::ignore = (([&]<std::size_t K>() constexpr noexcept(CtxT::is_nothrow) {
                               auto current_fork_view = ctx.template fork_view<K>();
                               action                 = invoke_mod(get<K>(funcs), current_fork_view);
                               return action == next;
                           }).template operator()<I>() &&
                           ...);
            return action;
        }(std::make_index_sequence<sizeof...(Funcs)>{});
    }

    /// Invoke .init() functions
    template <Context CtxT, typename... Funcs>
    constexpr void invoke_init(CtxT &ctx, std::tuple<Funcs...> &mods) noexcept(CtxT::is_nothrow) {
        std::apply(
          [&](auto &...actions) constexpr noexcept(CtxT::is_nothrow) {
              (
                [&]<typename Func>(Func &action) constexpr noexcept(CtxT::is_nothrow) {
                    if constexpr (requires { action.init(ctx); }) {
                        static_cast<void>(action.init(ctx));
                    } else if constexpr (requires { action.init(); }) {
                        static_cast<void>(action.init());
                    } else {
                        // Intentionally Ignored since most mods don't need init.
                    }
                }(actions),
                ...);
          },
          mods);
    }


    template <std::size_t Index, Modifier... Funcs>
    struct [[nodiscard]] basic_context_view;

    template <Modifier... Funcs>
    struct [[nodiscard]] basic_context {
        // static constexpr bool is_nothrow =
        //   (std::is_nothrow_invocable_v<std::remove_cvref_t<Funcs>, basic_context &> && ...);
        static constexpr bool is_nothrow = true;

      private:
        event_type                                ev;
        std::tuple<std::remove_cvref_t<Funcs>...> mods;

      public:
        constexpr basic_context() noexcept = default;

        consteval explicit basic_context(event_type inp_ev, std::remove_cvref_t<Funcs>... inp_funcs) noexcept
          : ev{std::move(inp_ev)},
            mods{inp_funcs...} {}

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

        [[nodiscard]] constexpr auto const &get_mods() const noexcept {
            return mods;
        }

        [[nodiscard]] constexpr auto &get_mods() noexcept {
            return mods;
        }

        constexpr void event(input_event const &inp_event) noexcept {
            ev = inp_event;
        }

        constexpr void event(event_type const &inp_event) noexcept {
            ev = inp_event;
        }

        template <typename Func>
            requires((std::same_as<mod_of<Func, Funcs...>, Funcs> || ...))
        [[nodiscard]] constexpr auto &mod() noexcept {
            using mod_type = mod_of<Func, Funcs...>;
            // we're not using Func directly because we may have duplicate types in the tuple, and we want the
            // first one to be returned instead of throwing error that there's multiple of that type.
            return get<index_at<mod_type, Funcs...>>(mods);
        }

        template <typename Func>
            requires((std::same_as<mod_of<Func, Funcs...>, Funcs> || ...))
        [[nodiscard]] constexpr auto const &mod() const noexcept {
            using mod_type = mod_of<Func, Funcs...>;
            return get<index_at<mod_type, Funcs...>>(mods);
        }

        template <typename Func>
            requires((std::same_as<mod_of<Func, Funcs...>, Funcs> || ...))
        [[nodiscard]] constexpr auto &mod([[maybe_unused]] Func const &) noexcept {
            using mod_type = mod_of<Func, Funcs...>;
            return get<index_at<mod_type, Funcs...>>(mods);
        }

        template <typename Func>
            requires((std::same_as<mod_of<Func, Funcs...>, Funcs> || ...))
        [[nodiscard]] constexpr auto const &mod([[maybe_unused]] Func const &) const noexcept {
            using mod_type = mod_of<Func, Funcs...>;
            return get<index_at<mod_type, Funcs...>>(mods);
        }

        template <std::size_t Index = 0>
        [[nodiscard]] constexpr auto const &mod() const noexcept {
            return get<Index>(mods);
        }

        template <std::size_t Index = 0>
        [[nodiscard]] constexpr auto &mod() noexcept {
            return get<Index>(mods);
        }

        /// Unwrap basic_context
        template <Modifier... NMods>
        [[nodiscard]] consteval auto operator|(basic_context<NMods...> const &ctx) const noexcept {
            return std::apply(
              [&](auto const &...funcs) constexpr noexcept {
                  return std::apply(
                    [&](auto const &...funcs2) constexpr noexcept {
                        return basic_context<std::remove_cvref_t<Funcs>..., NMods...>{
                          ev,
                          funcs...,
                          funcs2...};
                    },
                    ctx.get_mods());
              },
              mods);
        }

        template <Modifier Mod>
        [[nodiscard]] consteval auto operator|(Mod &&inp_mod) const noexcept {
            return std::apply(
              [&](auto const &...funcs) constexpr noexcept {
                  return basic_context<std::remove_cvref_t<Funcs>..., std::remove_cvref_t<Mod>>{
                    ev,
                    funcs...,
                    inp_mod};
              },
              mods);
        }

        template <std::size_t Index = 0, Context CtxT = basic_context>
        constexpr context_action reemit(CtxT &ctx) const noexcept(CtxT::is_nothrow) {
            static_assert(Index <= sizeof...(Funcs) - 1, "Index out of range.");
            using enum context_action;
            auto       ctx_view = ctx.template fork_view<Index>();
            auto const action   = invoke_mod(ctx.template mod<Index>(), ctx_view);
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
            return reemit<Index, basic_context>(*this);
        }

        // /**
        //  * Usage:
        //  *   ctx.fork_emit(*this);
        //  */
        // template <modifier Func>
        // constexpr context_action fork_emit([[maybe_unused]] Func const &inp_mod) noexcept(is_nothrow) {
        //     return reemit<index_at<std::remove_cvref_t<Func>, Funcs...> + 1U>();
        // }
        //
        // template <modifier Func>
        // constexpr context_action fork_emit() noexcept(is_nothrow) {
        //     return reemit<index_at<std::remove_cvref_t<Func>, Funcs...> + 1U>();
        // }
        //
        // template <modifier Func, Context CtxT = basic_context>
        // constexpr context_action fork_emit(CtxT &ctx, [[maybe_unused]] Func const &inp_mod) const
        //   noexcept(CtxT::is_nothrow) {
        //     return ctx.template reemit<index_at<std::remove_cvref_t<Func>, Funcs...> + 1U>(ctx);
        // }
        //
        // template <modifier Func>
        // constexpr context_action fork_emit([[maybe_unused]] Func const &inp_mod,
        //                                    event_type const            &inp_event) noexcept(is_nothrow) {
        //     auto const cur_ev = std::exchange(ev, inp_event);
        //     auto const res    = fork_emit(inp_mod);
        //     ev                = cur_ev;
        //     return res;
        // }
        //
        // template <modifier Func>
        // constexpr context_action fork_emit(event_type const &inp_event) noexcept(is_nothrow) {
        //     auto const cur_ev = std::exchange(ev, inp_event);
        //     auto const res    = fork_emit<Func>();
        //     ev                = cur_ev;
        //     return res;
        // }

        template <std::size_t Index>
        constexpr context_action fork_emit(event_type const &inp_event) noexcept(is_nothrow) {
            auto const cur_ev = std::exchange(ev, inp_event);
            auto const res    = reemit<Index>();
            ev                = cur_ev;
            return res;
        }

        template <std::size_t Index, typename... Args>
            requires(std::constructible_from<event_type, Args...> && sizeof...(Args) >= 2)
        constexpr context_action fork_emit(Args &&...args) noexcept(is_nothrow) {
            return fork_emit<Index>(event_type{std::forward<Args>(args)...});
        }

        // template <modifier Func, typename... Args>
        //     requires(std::constructible_from<event_type, Args...> && sizeof...(Args) >= 2)
        // constexpr context_action fork_emit([[maybe_unused]] Func const &inp_mod, Args &&...args) noexcept(
        //   is_nothrow) {
        //     return fork_emit<Func>(inp_mod, event_type{std::forward<Args>(args)...});
        // }

        // template <modifier Func>
        // constexpr auto fork_view([[maybe_unused]] Func const &) noexcept {
        //     return basic_context_view<index_at<std::remove_cvref_t<Func>, Funcs...> + 1U, Funcs...>{*this};
        // }

        template <std::size_t Index>
        constexpr auto fork_view() noexcept {
            static_assert(Index <= sizeof...(Funcs) - 1, "Index out of range.");
            return basic_context_view<Index + 1U, Funcs...>{*this};
        }

        // Re-Emit the context
        constexpr context_action reemit_all() noexcept(is_nothrow) {
            return invoke_mods(*this, mods);
        }

        constexpr void init() noexcept(is_nothrow) {
            invoke_init(*this, mods);
        }

        constexpr void operator()() noexcept(is_nothrow) {
            using enum context_action;
            init();
            operator()(no_init);
        }

        constexpr void operator()([[maybe_unused]] no_init_type) noexcept(is_nothrow) {
            using enum context_action;
            while (reemit_all() != exit) {
                // keep going
            }
        }

        /// Pass-through
        constexpr context_action operator()(Context auto &ctx) noexcept(is_nothrow) {
            using enum context_action;
            ev             = ctx.event();
            auto const res = reemit_all();
            ctx.event(ev);
            return res;
        }
    };

    template <std::size_t Index, Modifier... Funcs>
    struct [[nodiscard]] basic_context_view {
        using ctx_type                   = basic_context<Funcs...>;
        using type_type                  = event_type::type_type;
        using code_type                  = event_type::code_type;
        using value_type                 = event_type::value_type;
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

        constexpr context_action fork_emit(user_event const &inp_ev) noexcept(is_nothrow) {
            return fork_emit(event_type{inp_ev});
        }

        constexpr context_action fork_emit(
          type_type const  inp_type,
          code_type const  inp_code,
          value_type const inp_val) noexcept(is_nothrow) {
            return fork_emit(event_type{inp_type, inp_code, inp_val});
        }

        [[nodiscard]] constexpr event_type const &event() const noexcept {
            return ctx->event();
        }

        [[nodiscard]] constexpr event_type &event() noexcept {
            return ctx->event();
        }

        constexpr void event(input_event const &inp_event) noexcept {
            ctx->event(inp_event);
        }

        constexpr void event(event_type const &inp_event) noexcept {
            ctx->event(inp_event);
        }

        template <typename Func>
            requires((std::same_as<mod_of<Func, Funcs...>, Funcs> || ...))
        [[nodiscard]] constexpr auto &mod() noexcept {
            return ctx->template mod<Func>();
        }

        template <typename Func>
            requires((std::same_as<mod_of<Func, Funcs...>, Funcs> || ...))
        [[nodiscard]] constexpr auto const &mod() const noexcept {
            return ctx->template mod<Func>();
        }

        template <typename Func>
            requires((std::same_as<mod_of<Func, Funcs...>, Funcs> || ...))
        [[nodiscard]] constexpr auto &mod([[maybe_unused]] Func const &) noexcept {
            return ctx->template mod<Func>();
        }

        template <typename Func>
            requires((std::same_as<mod_of<Func, Funcs...>, Funcs> || ...))
        [[nodiscard]] constexpr auto const &mod([[maybe_unused]] Func const &) const noexcept {
            return ctx->template mod<Func>();
        }

        template <std::size_t NIndex = Index>
        [[nodiscard]] constexpr auto const &mod() const noexcept {
            return ctx->template mod<NIndex>();
        }

        // Re-Forking
        template <std::size_t NIndex>
        constexpr auto fork_view() noexcept {
            static_assert(NIndex <= sizeof...(Funcs) - 1, "Index out of range.");
            return basic_context_view<NIndex + 1U, Funcs...>{*ctx};
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
