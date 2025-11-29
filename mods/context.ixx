// Created by moisrex on 6/8/25.

module;
#include <concepts>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
export module foresight.mods.context;
export import foresight.mods.event;
import foresight.mods.event;

namespace fs8 {
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

} // namespace fs8

export namespace fs8 {
    template <typename T>
    concept Context = requires(T ctx) {
        typename T::mods_type;
        T::is_nothrow;
        ctx.event();
        ctx.get_mods();
    };

    template <typename T>
    concept Modifier = std::copyable<std::remove_cvref_t<T>> && std::movable<std::remove_cvref_t<T>>;

    template <typename T>
    concept OutputModifier =
      Modifier<T>
      && requires(T                      out,
                  event_type             event,
                  event_type::code_type  code,
                  event_type::type_type  type,
                  event_type::value_type value) {
             { out.emit(event) } noexcept -> std::same_as<bool>;

             { out.emit(type, code, value) } noexcept -> std::same_as<bool>;

             { out.emit_syn() } noexcept -> std::same_as<bool>;
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
      blowup_if<Modifier<CtxT>, Context<Mod>>() && Modifier<Mod> && Context<CtxT> && requires(CtxT &ctx) {
          ctx.template mod<Mod>();
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
    using mod_of = mod_of_t<ModConcept, Funcs...>::type;

    /// Contexts that have these specific mods
    /// Context is the last item
    template <typename... T>
    concept ContextWith = (has_mod<T...[sizeof...(T) - 1], T> || ...);


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

    constexpr struct [[nodiscard]] no_init_tag {
        static constexpr bool is_tag = true;
    } no_init;

    constexpr struct [[nodiscard]] start_tag {
        static constexpr bool is_tag = true;
    } start;

    constexpr struct [[nodiscard]] toggle_on_tag {
        static constexpr bool is_tag = true;
    } toggle_on;

    constexpr struct [[nodiscard]] toggle_off_tag {
        static constexpr bool is_tag = true;
    } toggle_off;

    /// This will let the mods set an event to the context, and send them through the whole pipeline.
    constexpr struct [[nodiscard]] next_event_tag {
        static constexpr bool is_tag = true;
    } next_event;

    /// This will wait for an event to be loaded, so this is blocking, and shall be called after the
    /// set_events are done.
    constexpr struct [[nodiscard]] load_event_tag {
        static constexpr bool is_tag = true;
    } load_event;

    template <typename ModT, typename CtxT, typename... Args>
    concept invokable_mod =
      std::invocable<ModT, CtxT &, Args...>
      || std::invocable<ModT, event_type &, Args...>
      || std::invocable<ModT, Args...>;

    template <typename T>
    concept tag = requires {
        T::is_tag;
        requires T::is_tag;
    };

    template <context_action DefaultAction = context_action::next,
              typename ModT,
              typename CtxT,
              typename... Args>
    constexpr context_action invoke_mod(ModT &mod, CtxT &ctx, Args &&...args) {
        using enum context_action;
        if constexpr (std::invocable<ModT, CtxT &, Args...>) {
            using result = std::invoke_result_t<ModT, CtxT &, Args...>;
            if constexpr (std::same_as<result, bool>) {
                return mod(ctx, std::forward<Args>(args)...) ? next : ignore_event;
            } else if constexpr (std::same_as<result, context_action>) {
                return mod(ctx, std::forward<Args>(args)...);
            } else {
                static_cast<void>(mod(ctx, std::forward<Args>(args)...));
                return DefaultAction;
            }
        } else if constexpr (std::invocable<ModT, event_type &, Args...>) {
            auto &event  = ctx.event();
            using result = std::invoke_result_t<ModT, event_type &, Args...>;
            if constexpr (std::same_as<result, bool>) {
                return mod(event, std::forward<Args>(args)...) ? next : ignore_event;
            } else if constexpr (std::same_as<result, context_action>) {
                return mod(event, std::forward<Args>(args)...);
            } else {
                static_cast<void>(mod(event, std::forward<Args>(args)...));
                return DefaultAction;
            }
        } else if constexpr (std::invocable<ModT, Args...>) {
            using result = std::invoke_result_t<ModT, Args...>;
            if constexpr (std::same_as<result, bool>) {
                return mod(std::forward<Args>(args)...) ? next : ignore_event;
            } else if constexpr (std::same_as<result, context_action>) {
                return mod(std::forward<Args>(args)...);
            } else {
                static_cast<void>(mod(std::forward<Args>(args)...));
                return DefaultAction;
            }
        } else {
            // static_assert(false, "We're not able to run this function.");
            return DefaultAction;
        }
    }

    /// Invoke Condition
    template <typename CondT, typename CtxT, typename... Args>
    constexpr bool invoke_cond(CondT &cond, CtxT &ctx, Args &&...args) {
        using enum context_action;
        if constexpr (std::invocable<CondT, CtxT &, Args...>) {
            using result = std::invoke_result_t<CondT, CtxT &, Args...>;
            if constexpr (std::same_as<result, bool>) {
                return cond(ctx, std::forward<Args>(args)...);
            } else if constexpr (std::same_as<result, context_action>) {
                return cond(ctx, std::forward<Args>(args)...) == next;
            } else {
                cond(ctx, std::forward<Args>(args)...);
                return true;
            }
        } else if constexpr (std::invocable<CondT, event_type &, Args...>) {
            auto &event  = ctx.event();
            using result = std::invoke_result_t<CondT, event_type &, Args...>;
            if constexpr (std::same_as<result, bool>) {
                return cond(event, std::forward<Args>(args)...);
            } else if constexpr (std::same_as<result, context_action>) {
                return cond(event, std::forward<Args>(args)...) == next;
            } else {
                cond(event, std::forward<Args>(args)...);
                return true;
            }
        } else if constexpr (std::invocable<CondT, Args...>) {
            using result = std::invoke_result_t<CondT, Args...>;
            if constexpr (std::same_as<result, bool>) {
                return cond(std::forward<Args>(args)...);
            } else if constexpr (std::same_as<result, context_action>) {
                return cond(std::forward<Args>(args)...) == next;
            } else {
                cond(std::forward<Args>(args)...);
                return true;
            }
        } else {
            // static_assert(false, "We're not able to run this function.");
            return false;
        }
    }

    template <typename ModT, typename CtxT>
    context_action invoke_start(ModT &mod, CtxT &ctx) {
        return invoke_mod(mod, ctx, start);
    }

    template <typename ModT, typename CtxT>
    constexpr context_action invoke_toggle_on(ModT &mod, CtxT &ctx) {
        return invoke_mod(mod, ctx, toggle_on);
    }

    template <typename ModT, typename CtxT>
    constexpr context_action invoke_toggle_off(ModT &mod, CtxT &ctx) {
        return invoke_mod(mod, ctx, toggle_off);
    }

    template <typename ModT, typename CtxT>
    constexpr context_action invoke_set_event(ModT &mod, CtxT &ctx) {
        return invoke_mod(mod, ctx, next_event);
    }

    template <typename ModT, typename CtxT>
    context_action invoke_load_event(ModT &mod, CtxT &ctx) {
        return invoke_mod(mod, ctx, load_event);
    }

    template <Context CtxT, typename... Funcs>
    constexpr context_action invoke_mod_at(
      CtxT                 &ctx,
      std::tuple<Funcs...> &funcs,
      std::size_t const     index) noexcept(CtxT::is_nothrow) {
        using enum context_action;
        return [&]<std::size_t... I>(std::index_sequence<I...>) constexpr noexcept(CtxT::is_nothrow) {
            auto action = next;
            std::ignore = (([&]<std::size_t K>() constexpr noexcept(CtxT::is_nothrow) {
                               if (K == index) {
                                   auto current_fork_view = ctx.template fork_view<K>();
                                   action                 = invoke_mod(get<K>(funcs), current_fork_view);
                               }
                               return action == next;
                           }).template operator()<I>()
                           && ...);
            return action;
        }(std::make_index_sequence<sizeof...(Funcs)>{});
    }

    template <std::size_t    Index,
              context_action DefaultAction = context_action::next,
              Context        CtxT,
              typename... Funcs,
              typename... Args>
    constexpr context_action fork_mod(CtxT &ctx, std::tuple<Funcs...> &funcs, Args &&...args) noexcept(
      CtxT::is_nothrow) {
        using enum context_action;
        using tuple_type = std::tuple<Funcs...>;
        using mod_type   = std::tuple_element_t<Index, tuple_type>;
        if constexpr (invokable_mod<mod_type, CtxT &, Args...>) {
            auto current_fork_view = ctx.template fork_view<Index>();
            return invoke_mod<DefaultAction>(
              get<Index>(funcs),
              current_fork_view,
              std::forward<Args>(args)...);
        } else {
            return DefaultAction;
        }
    }

    /// Run the functions and give them the specified context and arguments (optionally)
    template <Context CtxT, typename... Funcs, typename... Args>
        requires(std::is_trivially_copy_constructible_v<Args> && ...)
    constexpr context_action invoke_mods(CtxT &ctx, std::tuple<Funcs...> &funcs, Args... args) noexcept(
      CtxT::is_nothrow) {
        using enum context_action;
        // todo: replace with C++26 "template for" when compilers support it
        return [&]<std::size_t... I>(std::index_sequence<I...>) constexpr noexcept(CtxT::is_nothrow) {
            auto action = next;
            std::ignore = (((action = fork_mod<I>(ctx, funcs, args...)) == next) && ...);
            return action;
        }(std::make_index_sequence<sizeof...(Funcs)>{});
    }

    /// Run functions until one of them return "context_action::next"
    template <Context CtxT, typename... Funcs, typename... Args>
        requires(std::is_trivially_copy_constructible_v<Args> && ...)
    constexpr context_action
    invoke_first_mod_of(CtxT &ctx, std::tuple<Funcs...> &funcs, Args... args) noexcept(CtxT::is_nothrow) {
        using enum context_action;
        // todo: replace with C++26 "template for" when compilers support it
        return [&]<std::size_t... I>(std::index_sequence<I...>) constexpr noexcept(CtxT::is_nothrow) {
            auto action = ignore_event;
            std::ignore = (((action = fork_mod<I, ignore_event>(ctx, funcs, args...)) != next) && ...);
            return action;
        }(std::make_index_sequence<sizeof...(Funcs)>{});
    }

    template <std::size_t Index, Modifier... Funcs>
    struct [[nodiscard]] basic_context_view;

    template <Modifier... Funcs>
    struct [[nodiscard]] basic_context {
        // static constexpr bool is_nothrow =
        //   (std::is_nothrow_invocable_v<std::remove_cvref_t<Funcs>, basic_context &> && ...);

        using mods_type                  = std::tuple<std::remove_cvref_t<Funcs>...>;
        static constexpr bool is_nothrow = true;

      private:
        event_type ev;
        mods_type  mods;

      public:
        constexpr basic_context() noexcept = default;

        consteval explicit basic_context(event_type const &inp_ev,
                                         std::remove_cvref_t<Funcs>... inp_funcs) noexcept
          : ev{inp_ev},
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

        template <std::size_t Index>
        context_action fork_emit(event_type const &inp_event) noexcept(is_nothrow) {
            auto const cur_ev = std::exchange(ev, inp_event);
            auto const res    = reemit<Index>();
            ev                = cur_ev;
            return res;
        }

        template <std::size_t Index, typename... Args>
            requires(std::constructible_from<event_type, Args...> && sizeof...(Args) >= 2)
        context_action fork_emit(Args &&...args) noexcept(is_nothrow) {
            return fork_emit<Index>(event_type{std::forward<Args>(args)...});
        }

        template <std::size_t Index>
        constexpr auto fork_view() noexcept {
            static_assert(Index <= sizeof...(Funcs) - 1, "Index out of range.");
            return basic_context_view<Index + 1U, Funcs...>{*this};
        }

        context_action operator()(start_tag) noexcept(false) {
            return invoke_mods(*this, mods, start);
        }

        void operator()() noexcept(is_nothrow) {
            using enum context_action;
            if (operator()(start) == exit) [[unlikely]] {
                return;
            }
            operator()(no_init);
        }

        void operator()(auto &&, tag auto) = delete;
        void operator()(tag auto)          = delete;

        void operator()([[maybe_unused]] no_init_tag) noexcept(is_nothrow) {
            using enum context_action;
            using ctx_view = basic_context_view<0, Funcs...>;
            static_assert(((invokable_mod<Funcs, ctx_view>
                            || invokable_mod<Funcs, ctx_view, load_event_tag>
                            || invokable_mod<Funcs, ctx_view, next_event_tag>)
                           && ...),
                          "At least one of the mods are not callable");
            static constexpr auto load_event_count =
              (0 + ... + (invokable_mod<Funcs, ctx_view, load_event_tag> ? 1 : 0));
            static constexpr auto next_event_count =
              (0 + ... + (invokable_mod<Funcs, ctx_view, next_event_tag> ? 1 : 0));
            static_assert(load_event_count <= 1, "There should only be one single load_event in the mods");
            static_assert(load_event_count + next_event_count >= 1, "Someone needs to provide the events.");
            for (;;) {
                // Exhaust the next events until there's no more events:
                if constexpr (next_event_count > 0) {
                    switch (invoke_first_mod_of(*this, mods, next_event)) {
                        case next:
                            if (invoke_mods(*this, mods) == exit) {
                                return;
                            }
                            continue;
                        [[likely]] case ignore_event:
                            break;
                        [[unlikely]] default:
                        [[unlikely]] case exit:
                            return;
                    }
                }
                // Wait until new event comes, we should only have one single load_event
                if constexpr (load_event_count > 0) {
                    switch (invoke_mods(*this, mods, load_event)) {
                        [[likely]] case next:
                            break;
                        case ignore_event:
                            continue;
                        [[unlikely]] default:
                        [[unlikely]] case exit:
                            return;
                    }
                }

                if (invoke_mods(*this, mods) == exit) [[unlikely]] {
                    return;
                }
            }
        }

        /// Pass-through
        context_action operator()(Context auto &ctx) noexcept(is_nothrow) {
            return invoke_mods(ctx, mods);
        }
    };

    /**
     * It's essentially a lightweight version of the context above.
     */
    template <std::size_t Index, Modifier... Funcs>
    struct [[nodiscard]] basic_context_view {
        using ctx_type   = basic_context<Funcs...>;
        using type_type  = event_type::type_type;
        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;
        using mods_type  = ctx_type::mods_type;

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

        context_action fork_emit() noexcept(is_nothrow) {
            return ctx->template reemit<Index>();
        }

        context_action fork_emit(event_type const &event) noexcept(is_nothrow) {
            return ctx->template fork_emit<Index>(event);
        }

        context_action fork_emit(user_event const &inp_ev) noexcept(is_nothrow) {
            return fork_emit(event_type{inp_ev});
        }

        context_action fork_emit(type_type const  inp_type,
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

        constexpr void event(event_type const &inp_event) noexcept {
            ctx->event(inp_event);
        }

        [[nodiscard]] constexpr auto &get_mods() noexcept {
            return ctx->get_mods();
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

} // namespace fs8
