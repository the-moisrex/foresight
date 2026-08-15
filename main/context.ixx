// Created by moisrex on 6/8/25.

module;
#include <concepts>
#include <cstdint>
#include <functional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
export module fs8.context;
export import fs8.event;
export import :vars;
import fs8.event;
import fs8.log;
import fs8.traits;
import dynamic_scoping;

namespace fs8 {
    // Base case: index 0, type is the first type T
    template <std::size_t I, typename T, typename... Ts>
    struct type_at_impl {
        using type = type_at_impl<I - 1, Ts...>::type;
    };

    // Specialization for index 0
    template <typename T, typename... Ts>
    struct type_at_impl<0, T, Ts...> {
        using type = T;
    };

    template <std::size_t I, typename... Ts>
    using type_at = type_at_impl<I, Ts...>::type;

    template <std::size_t Index, typename F, typename T1, typename... Ts>
    struct index_at_impl {
        static constexpr auto value = index_at_impl<Index + 1, F, Ts...>::value;
    };

    template <std::size_t Index, typename F, typename... Ts>
    struct index_at_impl<Index, F, F, Ts...> {
        static constexpr auto value = Index;
    };

    template <typename F, typename... Ts>
    constexpr std::size_t index_at = index_at_impl<0, std::remove_cvref_t<F>, std::remove_cvref_t<Ts>...>::value;

    // constexpr struct output_mod_t {
    //     template <typename T>
    //     static constexpr bool value = OutputModifier<T>;
    // } output_mod;

    template <bool... T>
    [[nodiscard]] consteval bool blowup_if(bool const res = true) noexcept {
        static_assert(!static_cast<bool>((T && ...)), "Reverse the order of params.");
        return res;
    }

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

} // namespace fs8

export namespace fs8 {
    template <typename T>
    concept Context = requires(T ctx) {
        typename T::mods_type;
        ctx.event();
        ctx.get_mods();
    };

    template <typename T>
    concept Modifier = std::copyable<std::remove_cvref_t<T>> && std::movable<std::remove_cvref_t<T>>;

    template <typename T>
    concept OutputModifier =
      Modifier<T>
      && requires(T out, event_type event, event_type::code_type code, event_type::type_type type, event_type::value_type value) {
             { out.emit(event) } noexcept -> std::same_as<bool>;
             { out.emit(type, code, value) } noexcept -> std::same_as<bool>;
             { out.emit_syn() } noexcept -> std::same_as<bool>;
         };

    template <typename Mod, typename CtxT>
    concept has_mod =
      blowup_if<Modifier<CtxT>, Context<Mod>>() && Modifier<Mod> && Context<CtxT> && requires(CtxT &ctx) { ctx.template mod<Mod>(); };


    template <typename ModConcept, typename... Funcs>
    using mod_of = mod_of_t<ModConcept, Funcs...>::type;

    /// Contexts that have these specific mods
    /// Context is the last item
    template <typename... T>
    concept ContextWith = (has_mod<T...[sizeof...(T) - 1], T> || ...);


    /**
     * Actions that each mod can take
     */
    enum struct [[nodiscard]] context_action : std::uint8_t {
        next,         // pass it to the next mod
        ignore_event, // ignore this event (drop it)
        idle,         // idle mode, or watching mode, or restart mode
        exit,         // exit the software
    };

    [[nodiscard]] std::string_view to_string(context_action action) noexcept;

    [[nodiscard]] constexpr bool is_exiting(context_action const action) noexcept {
        using enum context_action;
        return action == idle || action == exit;
    }

    [[nodiscard]] constexpr bool operator!(context_action const action) noexcept {
        return is_exiting(action);
    }

    /// Run the context mods, don't run the initialization and other setup actions of the mods.
    constexpr struct [[nodiscard]] no_init_tag {
        static constexpr bool is_tag = true;
    } no_init;

    /// Initialization and the setup parts of the mods will happen in actions using this tag.
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
      std::is_nothrow_invocable_v<ModT, CtxT &, Args...>
      || std::is_nothrow_invocable_v<ModT, event_type &, Args...>
      || std::is_nothrow_invocable_v<ModT, Args...>;

    template <typename T>
    concept Tag = requires {
        T::is_tag;
        requires T::is_tag;
    } && std::is_trivially_copy_constructible_v<T>;

    template <typename ModT, typename... Args>
    constexpr context_action invoke_mod_inorder(ModT &mod, context_action const default_action, Args &&...args) noexcept {
        using enum context_action;
        using result = std::invoke_result_t<ModT, Args...>;
        static_assert(std::is_nothrow_invocable_v<ModT, Args...>, "Mark the mod as nothrow.");
        if constexpr (std::same_as<result, bool>) {
            return mod(std::forward<Args>(args)...) ? next : ignore_event;
        } else if constexpr (std::same_as<result, context_action>) {
            return mod(std::forward<Args>(args)...);
        } else {
            static_cast<void>(mod(std::forward<Args>(args)...));
            return default_action;
        }
    }

    template <typename ModT, typename CtxT, typename... Args>
    constexpr context_action invoke_mod(ModT &mod, CtxT &ctx, context_action const default_action, Args... args) noexcept {
        using enum context_action;
        if constexpr (std::invocable<ModT, CtxT &, Args...>) {
            return invoke_mod_inorder(mod, default_action, ctx, args...);
        } else if constexpr (std::invocable<ModT, event_type &, Args...>) {
            auto &event = ctx.event();
            return invoke_mod_inorder(mod, default_action, event, args...);
        } else if constexpr (std::invocable<ModT, Args...>) {
            return invoke_mod_inorder(mod, default_action, args...);
        } else if constexpr (sizeof...(Args) >= 2) {
            // Some mods don't accept the tag-specific arguments (e.g. the device_query the router
            // pushes down a pipeline); drop the leading non-tag argument and retry with fewer args.
            if constexpr (!Tag<type_at<0, Args...>> && Tag<type_at<sizeof...(Args) - 1, Args...>>) {
                return [&]<std::size_t... I>(std::index_sequence<I...>) constexpr noexcept {
                    auto const args_tuple = std::tuple{args...};
                    return invoke_mod(mod, ctx, default_action, std::get<I + 1>(args_tuple)...);
                }(std::make_index_sequence<sizeof...(Args) - 1>{});
            } else {
                return default_action;
            }
        } else {
            // static_assert(false, "We're not able to run this function.");
            return default_action;
        }
    }

    template <typename ModT, typename CtxT, typename... Args>
    constexpr context_action invoke_mod(ModT &mod, CtxT &ctx, Args... args) noexcept {
        return invoke_mod(mod, ctx, context_action::next, args...);
    }

    template <typename CondT, typename... Args>
    constexpr bool invoke_cond_inorder(CondT &cond, Args &&...args) noexcept {
        using enum context_action;
        using result = std::invoke_result_t<CondT, Args...>;
        static_assert(std::is_nothrow_invocable_v<CondT, Args...>, "Mark the mod as nothrow.");
        if constexpr (std::same_as<result, bool>) {
            return cond(std::forward<Args>(args)...);
        } else if constexpr (std::same_as<result, context_action>) {
            return cond(std::forward<Args>(args)...) == next;
        } else {
            static_cast<void>(cond(std::forward<Args>(args)...));
            return true;
        }
    }

    /// Invoke Condition
    template <typename CondT, typename CtxT, typename... Args>
    constexpr bool invoke_cond(CondT &cond, CtxT &ctx, Args... args) noexcept {
        using enum context_action;
        if constexpr (std::invocable<CondT, CtxT &, Args...>) {
            return invoke_cond_inorder(cond, ctx, args...);
        } else if constexpr (std::invocable<CondT, event_type &, Args...>) {
            auto &event = ctx.event();
            return invoke_cond_inorder(cond, event, args...);
        } else if constexpr (std::invocable<CondT, Args...>) {
            return invoke_cond_inorder(cond, args...);
        } else {
            // static_assert(false, "We're not able to run this function.");
            return false;
        }
    }

    template <typename ModT, typename CtxT>
    context_action invoke_start(ModT &mod, CtxT &ctx) noexcept {
        return invoke_mod(mod, ctx, start);
    }

    template <typename ModT, typename CtxT>
    constexpr context_action invoke_toggle_on(ModT &mod, CtxT &ctx) noexcept {
        return invoke_mod(mod, ctx, toggle_on);
    }

    template <typename ModT, typename CtxT>
    constexpr context_action invoke_toggle_off(ModT &mod, CtxT &ctx) noexcept {
        return invoke_mod(mod, ctx, toggle_off);
    }

    template <typename ModT, typename CtxT>
    constexpr context_action invoke_set_event(ModT &mod, CtxT &ctx) noexcept {
        return invoke_mod(mod, ctx, next_event);
    }

    template <typename ModT, typename CtxT>
    context_action invoke_load_event(ModT &mod, CtxT &ctx) noexcept {
        return invoke_mod(mod, ctx, load_event);
    }

    template <Context CtxT, typename... Funcs>
    constexpr context_action invoke_mod_at(CtxT &ctx, std::tuple<Funcs...> &funcs, std::size_t const index) noexcept {
        using enum context_action;
        return [&]<std::size_t... I>(std::index_sequence<I...>) constexpr noexcept {
            auto action = next;
            std::ignore = (([&]<std::size_t K>() constexpr noexcept {
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

    template <std::size_t Index, Context CtxT, typename... Funcs, typename... Args>
    constexpr context_action fork_mod(CtxT &ctx, std::tuple<Funcs...> &funcs, context_action default_action, Args... args) noexcept {
        using enum context_action;
        using tuple_type = std::tuple<Funcs...>;
        using mod_type   = std::tuple_element_t<Index, tuple_type>;
        if constexpr (invokable_mod<mod_type, CtxT, Args...>) {
            auto current_fork_view = ctx.template fork_view<Index>();
            return invoke_mod(get<Index>(funcs), current_fork_view, default_action, args...);
        } else if constexpr (sizeof...(Args) >= 2) {
            // Let invoke_mod's drop fallback try calling this mod with fewer args.
            if constexpr (!Tag<type_at<0, Args...>> && Tag<type_at<sizeof...(Args) - 1, Args...>>) {
                auto current_fork_view = ctx.template fork_view<Index>();
                return invoke_mod(get<Index>(funcs), current_fork_view, default_action, args...);
            } else {
                return default_action;
            }
        } else {
            return default_action;
        }
    }

    /// Run the functions and give them the specified context and arguments (optionally)
    template <Context CtxT, typename... Mods, typename... Args>
    constexpr context_action invoke_mods(CtxT &ctx, std::tuple<Mods...> &mods, Args... args) noexcept {
        using enum context_action;
        return [&]<std::size_t... I>(std::index_sequence<I...>) constexpr noexcept {
            auto action = next;
            std::ignore = (((action = fork_mod<I>(ctx, mods, next, args...)) == next) && ...);
            return action;
        }(std::make_index_sequence<sizeof...(Mods)>{});
    }

    /// Run functions until one of them return "context_action::next"
    template <Context CtxT, typename... Funcs, typename... Args>
    constexpr context_action invoke_first_mod_of(CtxT &ctx, std::tuple<Funcs...> &funcs, Args... args) noexcept {
        using enum context_action;
        return [&]<std::size_t... I>(std::index_sequence<I...>) constexpr noexcept {
            auto action = ignore_event;
            std::ignore = (((action = fork_mod<I>(ctx, funcs, ignore_event, args...)) != next) && ...);
            return action;
        }(std::make_index_sequence<sizeof...(Funcs)>{});
    }

    template <std::size_t Index, Modifier... Funcs>
    struct [[nodiscard]] basic_context_view;

    /**
     * This is the main context object that holds all the mods in it, and runs them all, and also holds the event.
     * @tparam Funcs Modules or event Modifiers
     */
    template <Modifier... Funcs>
    struct [[nodiscard]] basic_context : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        // static constexpr std::size_t variables_count = variable_size_v<Funcs...>;

        using mods_type = std::tuple<std::remove_cvref_t<Funcs>...>;

        template <typename T>
        using mod_type = mod_of<T, Funcs...>;

      private:
        event_type                                              ev{};
        mods_type                                               mods{};
        std::array<variable_pointer, variable_size_v<Funcs...>> variables = extract_variables(mods);

      public:
        consteval explicit basic_context(event_type const &inp_ev, std::remove_cvref_t<Funcs>... inp_funcs) noexcept
          : ev{inp_ev},
            mods{inp_funcs...} {}

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) event(this Self &&self) noexcept {
            return std::forward_like<Self>(self.ev);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) get_mods(this Self &&self) noexcept {
            return std::forward_like<Self>(self.mods);
        }

        constexpr void event(event_type const &inp_event) noexcept {
            ev = inp_event;
        }

        template <typename Func, typename Self>
            requires((std::same_as<mod_type<Func>, Funcs> || ...))
        [[nodiscard]] constexpr auto &mod(this Self &&self) noexcept {
            using mod_type = mod_type<Func>;
            // we're not using Func directly because we may have duplicate types in the tuple, and we want the
            // first one to be returned instead of throwing error that there's multiple of that type.
            return get<index_at<mod_type, Funcs...>>(std::forward_like<Self>(self.mods));
        }

        template <typename Func, typename Self>
            requires((std::same_as<mod_type<Func>, Funcs> || ...))
        [[nodiscard]] constexpr auto &mod(this Self &&self, [[maybe_unused]] Func const &) noexcept {
            using mod_type = mod_type<Func>;
            return get<index_at<mod_type, Funcs...>>(std::forward_like<Self>(self.mods));
        }

        template <std::size_t Index = 0, typename Self>
        [[nodiscard]] constexpr auto &mod(this Self &&self) noexcept {
            return get<Index>(std::forward_like<Self>(self.mods));
        }

        /// @returns variant<monostate, Var::value_type...>
        [[nodiscard]] constexpr auto operator[](std::string_view const name) const {
            return find_variable(name, variables, mods);
        }

        /// Unwrap basic_context
        template <Modifier... NMods>
        [[nodiscard]] consteval auto operator|(basic_context<NMods...> const &ctx) const noexcept {
            return std::apply(
              [&](auto const &...funcs) constexpr noexcept {
                  return std::apply(
                    [&](auto const &...funcs2) constexpr noexcept {
                        return basic_context<std::remove_cvref_t<Funcs>..., NMods...>{ev, funcs..., funcs2...};
                    },
                    ctx.get_mods());
              },
              mods);
        }

        template <Modifier Mod>
        [[nodiscard]] consteval auto operator|(Mod &&inp_mod) const noexcept {
            return std::apply(
              [&](auto const &...funcs) constexpr noexcept {
                  return basic_context<std::remove_cvref_t<Funcs>..., std::remove_cvref_t<Mod>>{ev, funcs..., inp_mod};
              },
              mods);
        }

        template <std::size_t Index = 0, Context CtxT = basic_context>
        constexpr context_action reemit(CtxT &ctx) const noexcept {
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
        constexpr context_action reemit() noexcept {
            return reemit<Index, basic_context>(*this);
        }

        template <std::size_t Index>
        context_action fork_emit(event_type const &inp_event) noexcept {
            auto const cur_ev = std::exchange(ev, inp_event);
            auto const res    = reemit<Index>();
            ev                = cur_ev;
            return res;
        }

        template <std::size_t Index, typename... Args>
            requires(std::constructible_from<event_type, Args...> && sizeof...(Args) >= 2)
        context_action fork_emit(Args &&...args) noexcept {
            return fork_emit<Index>(event_type{std::forward<Args>(args)...});
        }

        template <std::size_t Index>
        constexpr auto fork_view() noexcept {
            static_assert(Index <= sizeof...(Funcs) - 1, "Index out of range.");
            return basic_context_view<Index + 1U, Funcs...>{*this};
        }

        template <typename Mod>
        constexpr auto fork_view() noexcept {
            static_assert((std::same_as<Mod, Funcs> || ...), "Index out of range.");
            return basic_context_view<index_at<Mod, Funcs...>, Funcs...>{*this};
        }

        context_action operator()(start_tag) noexcept try {
            // invoke the mods
            return invoke_mods(*this, mods, start);
        } catch (...) {
            // We don't know how to handle this.
            return context_action::exit;
        }

        /// returns false if we need to terminate
        [[nodiscard]] bool restart_if(context_action const prev_action = context_action::idle) noexcept try {
            using enum context_action;
            if (!prev_action) {
                if (prev_action == idle) {
                    log("Restarting pipeline...");
                }
                auto const action = operator()(start);
                // idle is ignored here
                return action != exit;
            }
            return !!prev_action;
        } catch (...) {
            // terminate
            return false;
        }

        void operator()() noexcept {
            if (!restart_if(context_action::exit)) {
                return;
            }
            operator()(no_init);
        }

        void operator()(auto &&, Tag auto) = delete;
        void operator()(Tag auto)          = delete;

        void operator()([[maybe_unused]] no_init_tag) noexcept {
            using enum context_action;
            using ctx_view = basic_context_view<0, Funcs...>;
            static_assert(((invokable_mod<Funcs, ctx_view>
                            || invokable_mod<Funcs, ctx_view, load_event_tag>
                            || invokable_mod<Funcs, ctx_view, next_event_tag>)
                           && ...),
                          "At least one of the mods are not callable");
            static constexpr auto load_event_count = (0 + ... + (invokable_mod<Funcs, ctx_view, load_event_tag> ? 1 : 0));
            static constexpr auto next_event_count = (0 + ... + (invokable_mod<Funcs, ctx_view, next_event_tag> ? 1 : 0));
            static_assert(load_event_count <= 1, "There should only be one single load_event in the mods");
            static_assert(load_event_count + next_event_count >= 1, "Someone needs to provide the events.");
            for (;;) {
                // Exhaust the next events until there's no more events.
                if constexpr (next_event_count > 0) {
                    switch (invoke_first_mod_of(*this, mods, next_event)) {
                        case next:
                            if (!restart_if(invoke_mods(*this, mods))) {
                                return;
                            }
                            continue;
                        [[likely]] case ignore_event:
                            break;
                        [[unlikely]] default:
                        [[unlikely]] case idle:
                            if (!restart_if(idle)) {
                                return;
                            }
                            break;
                        [[unlikely]] case exit:
                            return;
                    }
                    // next_event exhausted -> block in load_event (pure wait; it does
                    // NOT load an event). After it wakes, loop back to next_event.
                    if constexpr (load_event_count > 0) {
                        switch (invoke_mods(*this, mods, load_event)) {
                            [[likely]] case next:
                            case ignore_event:
                                continue; // key change (was `break` -> trailing invoke_mods)
                            [[unlikely]] default:
                            [[unlikely]] case idle:
                                if (!restart_if(idle)) {
                                    return;
                                }
                                break;
                            [[unlikely]] case exit:
                                return;
                        }
                    }
                    // no load_event provider
                    if (!restart_if(invoke_mods(*this, mods))) [[unlikely]] {
                        return;
                    }
                } else if constexpr (load_event_count > 0) {
                    // Legacy: load_event providers load events directly (old intercept).
                    switch (invoke_mods(*this, mods, load_event)) {
                        [[likely]] case next:
                            break;
                        case ignore_event:
                            continue;
                        [[unlikely]] default:
                        [[unlikely]] case idle:
                            if (!restart_if(idle)) {
                                return;
                            }
                            break;
                        [[unlikely]] case exit:
                            return;
                    }
                    if (!restart_if(invoke_mods(*this, mods))) [[unlikely]] {
                        return;
                    }
                }
            }
        }

        /// Pass-through
        context_action operator()(Context auto &ctx) noexcept {
            return invoke_mods(ctx, mods);
        }

        /// Pass-through a plain start to the mods.
        context_action operator()(Context auto &ctx, start_tag) noexcept {
            return invoke_mods(ctx, mods, start);
        }

        /// Pass-through with extra arguments (e.g. a device_query pushed by the router on start).
        /// The trailing argument is expected to be a tag.
        template <typename... Args>
            requires(sizeof...(Args) >= 2)
        context_action operator()(Context auto &ctx, Args const &...args) noexcept {
            return invoke_mods(ctx, mods, args...);
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

        template <typename T>
        using mod_type = mod_of<T, Funcs...>;

        template <typename Func>
        static constexpr bool is_mod = (std::same_as<mod_type<Func>, Funcs> || ...);

      private:
        ctx_type *ctx;

      public:
        explicit constexpr basic_context_view(ctx_type &inp_ctx) noexcept : ctx(&inp_ctx) {}

        constexpr basic_context_view(basic_context_view const &)                = default;
        constexpr basic_context_view(basic_context_view &&) noexcept            = default;
        constexpr basic_context_view &operator=(basic_context_view const &)     = default;
        constexpr basic_context_view &operator=(basic_context_view &&) noexcept = default;
        constexpr ~basic_context_view() noexcept                                = default;

        template <typename Self>
        [[nodiscard]] constexpr auto &&context(this Self &&self) noexcept {
            return std::forward_like<Self>(*self.ctx);
        }

        context_action fork_emit() noexcept {
            return ctx->template reemit<Index>();
        }

        context_action fork_emit(event_type const &event) noexcept {
            return ctx->template fork_emit<Index>(event);
        }

        context_action fork_emit(user_event const &inp_ev) noexcept {
            return fork_emit(event_type{inp_ev});
        }

        context_action fork_emit(type_type const inp_type, code_type const inp_code, value_type const inp_val) noexcept {
            return fork_emit(event_type{inp_type, inp_code, inp_val});
        }

        template <typename Self>
        [[nodiscard]] constexpr auto &&event(this Self &&self) noexcept {
            return std::forward_like<Self>(self.ctx->event());
        }

        constexpr void event(event_type const &inp_event) noexcept {
            ctx->event(inp_event);
        }

        template <typename Self>
        [[nodiscard]] constexpr auto &&get_mods(this Self &&self) noexcept {
            return std::forward_like<Self>(self.ctx->get_mods());
        }

        template <typename Func, typename Self>
            requires is_mod<Func>
        [[nodiscard]] constexpr decltype(auto) mod(this Self &&self) noexcept {
            return std::forward_like<Self>(self.ctx->template mod<Func>());
        }

        template <typename Func, typename Self>
            requires is_mod<Func>
        [[nodiscard]] constexpr decltype(auto) mod(this Self &&self, [[maybe_unused]] Func const &) noexcept {
            return std::forward_like<Self>(self.ctx->template mod<Func>());
        }

        template <std::size_t NIndex = Index, typename Self>
        [[nodiscard]] constexpr decltype(auto) mod(this Self &&self) noexcept {
            return std::forward_like<Self>(self.ctx->template mod<NIndex>());
        }

        // Re-Forking
        template <std::size_t NIndex>
        constexpr auto fork_view() const noexcept {
            static_assert(NIndex <= sizeof...(Funcs) - 1, "Index out of range.");
            return basic_context_view<NIndex + 1U, Funcs...>{*ctx};
        }

        // Re-Forking
        template <typename Mod>
        constexpr auto fork_view() const noexcept {
            static_assert(is_mod<Mod>, "Index out of range.");
            return basic_context_view<index_at<Mod, Funcs...>, Funcs...>{*ctx};
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
