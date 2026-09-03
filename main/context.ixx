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
        ctx.fork_emit();
    };

    template <typename T>
    concept Modifier = std::copyable<std::remove_cvref_t<T>> && std::movable<std::remove_cvref_t<T>>;

    template <typename T>
    concept OutputModifier = Modifier<T> && requires(T out, event_type event) {
        { out.emit(event) } noexcept -> std::same_as<bool>;
    };

    template <typename Mod, typename CtxT>
    concept has_mod =
      blowup_if<Modifier<CtxT>, Context<Mod>>() && Modifier<Mod> && Context<CtxT> && requires(CtxT &ctx) { ctx.template mod<Mod>(); };


    template <typename ModConcept, typename... Funcs>
    using mod_of = mod_of_t<ModConcept, Funcs...>::type;

    /// Contexts that have these specific mods
    /// Context is the first item (the compiler prepends the deduced context type)
    template <typename... T>
    struct context_with_impl;

    template <>
    struct context_with_impl<> {
        static constexpr bool value = false;
    };

    template <typename CtxT, typename... Mods>
    struct context_with_impl<CtxT, Mods...> {
        static constexpr bool value = (has_mod<Mods, CtxT> || ...);
    };

    template <typename... T>
    concept ContextWith = context_with_impl<T...>::value;


    /**
     * Actions that each mod can take
     */
    enum struct [[nodiscard]] context_action : std::uint8_t {
        next,       // pass it to the next mod
        drop_event, // drop this event
        recovery,   // recovery mode, or watching mode, or restart mode
        exit,       // exit the software
    };

    [[nodiscard]] std::string_view to_string(context_action action) noexcept;

    [[nodiscard]] constexpr bool is_exiting(context_action const action) noexcept {
        using enum context_action;
        return action == recovery || action == exit;
    }

    [[nodiscard]] constexpr bool operator!(context_action const action) noexcept {
        return is_exiting(action);
    }

    template <typename ModT, typename CtxT, typename... Args>
    concept invokable_mod =
      std::is_nothrow_invocable_v<ModT, CtxT &, Args...>
      || std::is_nothrow_invocable_v<ModT, event_type &, Args...>
      || std::is_nothrow_invocable_v<ModT, Args...>;

    namespace detail {
        template <typename... Args>
        constexpr bool args_contain_special_event = (std::same_as<std::remove_cvref_t<Args>, special_event> || ...);

        /// True if T has a `static constexpr bool is_tag = true` member, or is `special_event`.
        /// Used to exclude sentinel types (get_variables_tag, auto_mode_tag,
        /// pass_trigger_tag) and `special_event` from generic operator[] overloads.
        template <typename T, typename = void>
        inline constexpr bool is_tag_type = false;

        template <typename T>
        inline constexpr bool is_tag_type<T, std::void_t<decltype(std::remove_cvref_t<T>::is_tag)>> = std::remove_cvref_t<T>::is_tag;

        template <>
        inline constexpr bool is_tag_type<special_event, void> = true;
    } // namespace detail

    /// Concept for pipeline lifecycle tags. All lifecycle events are
    /// `special_event`; this replaces the old `Tag` concept.
    template <typename T>
    concept PipelineTag = std::same_as<std::remove_cvref_t<T>, special_event>;

    template <typename ModT, typename... Args>
    context_action invoke_mod_inorder(ModT &mod, context_action const default_action, Args &&...args) noexcept {
        using enum context_action;
        using result = std::invoke_result_t<ModT, Args...>;
        static_assert(std::is_nothrow_invocable_v<ModT, Args...>, "Mark the mod as nothrow.");
        if constexpr (std::same_as<result, bool>) {
            return mod(std::forward<Args>(args)...) ? next : drop_event;
        } else if constexpr (std::same_as<result, context_action>) {
            return mod(std::forward<Args>(args)...);
        } else {
            static_cast<void>(mod(std::forward<Args>(args)...));
            if constexpr (detail::args_contain_special_event<Args...>) {
                // Mods that return void for special_event signals are
                // transparent — they don't claim to have handled the event.
                return drop_event;
            } else {
                return default_action;
            }
        }
    }

    template <typename ModT, typename CtxT, typename... Args>
    context_action invoke_mod(ModT &mod, CtxT &ctx, context_action const default_action, Args... args) noexcept {
        using enum context_action;
        if constexpr (sizeof...(Args) == 0) {
            // No extra args: try (ctx), then (event&), then ().
            if constexpr (std::invocable<ModT, CtxT &>) {
                return invoke_mod_inorder(mod, default_action, ctx);
            } else if constexpr (std::invocable<ModT, event_type &>) {
                auto &event = ctx.event();
                return invoke_mod_inorder(mod, default_action, event);
            } else if constexpr (std::invocable<ModT>) {
                return invoke_mod_inorder(mod, default_action);
            } else {
                return default_action;
            }
        } else if constexpr (std::invocable<ModT, CtxT &, Args...>) {
            return invoke_mod_inorder(mod, default_action, ctx, args...);
        } else if constexpr (
          sizeof...(Args) == 1 && std::same_as<std::remove_cvref_t<type_at<0, Args...>>, special_event> && std::invocable<ModT, Args...>)
        {
            // When the single arg is a special_event, try it as a standalone calling convention.
            // This allows mods to accept `(special_event)` or `(special_event const&)` directly.
            return invoke_mod_inorder(mod, default_action, args...);
        } else if constexpr (std::invocable<ModT, event_type &, Args...>) {
            auto &event = ctx.event();
            return invoke_mod_inorder(mod, default_action, event, args...);
        } else if constexpr (std::invocable<ModT, Args...>) {
            return invoke_mod_inorder(mod, default_action, args...);
        } else if constexpr (sizeof...(Args) >= 2) {
            // Some mods don't accept the first argument (e.g. the device_query the router
            // pushes down a pipeline); drop the leading non-tag argument and retry with fewer args.
            if constexpr (!std::same_as<std::remove_cvref_t<type_at<0, Args...>>, special_event>
                          && std::same_as<std::remove_cvref_t<type_at<sizeof...(Args) - 1, Args...>>, special_event>)
            {
                return [&]<std::size_t... I>(std::index_sequence<I...>) constexpr noexcept {
                    auto const args_tuple = std::tuple{args...};
                    return invoke_mod(mod, ctx, default_action, std::get<I + 1>(args_tuple)...);
                }(std::make_index_sequence<sizeof...(Args) - 1>{});
            } else {
                return default_action;
            }
        } else {
            return default_action;
        }
    }

    template <typename ModT, typename CtxT, typename... Args>
    context_action invoke_mod(ModT &mod, CtxT &ctx, Args... args) noexcept {
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
        } else if constexpr (std::invocable<CondT, CtxT &>) {
            return invoke_cond_inorder(cond, ctx);
        } else if constexpr (std::invocable<CondT, event_type &>) {
            auto &event = ctx.event();
            return invoke_cond_inorder(cond, event);
        } else if constexpr (std::invocable<CondT>) {
            return invoke_cond_inorder(cond);
        } else {
            // static_assert(false, "We're not able to run this function.");
            return false;
        }
    }

    template <Context CtxT, typename... Funcs>
    context_action invoke_mods_from(
      CtxT                 &ctx,
      std::tuple<Funcs...> &funcs,
      std::size_t           start_index,
      context_action        default_action = context_action::next) noexcept;

    /// RAII guard that saves and restores a std::size_t value (e.g. fork_index).
    struct [[nodiscard]] fork_index_guard {
        explicit fork_index_guard(std::size_t &idx, std::size_t const new_val) noexcept : target_(idx), saved_(idx) {
            target_ = new_val;
        }

        ~fork_index_guard() noexcept {
            target_ = saved_;
        }

        fork_index_guard(fork_index_guard const &)            = delete;
        fork_index_guard(fork_index_guard &&)                 = delete;
        fork_index_guard &operator=(fork_index_guard const &) = delete;
        fork_index_guard &operator=(fork_index_guard &&)      = delete;

      private:
        std::size_t &target_;
        std::size_t  saved_;
    };

    /// RAII guard that enters a sub-pipeline on construction and exits on destruction.
    template <typename CtxT, typename... SubFuncs>
    struct [[nodiscard]] sub_pipeline_guard {
        explicit sub_pipeline_guard(CtxT &c, std::tuple<SubFuncs...> &sub_mods) noexcept : ctx_(c) {
            ctx_.enter_sub_pipeline(sub_mods);
        }

        ~sub_pipeline_guard() noexcept {
            ctx_.exit_sub_pipeline();
        }

        sub_pipeline_guard(sub_pipeline_guard const &)            = delete;
        sub_pipeline_guard(sub_pipeline_guard &&)                 = delete;
        sub_pipeline_guard &operator=(sub_pipeline_guard const &) = delete;
        sub_pipeline_guard &operator=(sub_pipeline_guard &&)      = delete;

      private:
        CtxT &ctx_;
    };

    template <Context CtxT, typename... Funcs>
    context_action invoke_mod_at(CtxT &ctx, std::tuple<Funcs...> &funcs, std::size_t const index) noexcept {
        using enum context_action;
        return [&]<std::size_t... I>(std::index_sequence<I...>) noexcept {
            auto action = next;
            std::ignore = (([&]<std::size_t K>() noexcept {
                               if (K == index) {
                                   // Entries are alternatives: a forked event skips the rest of this tuple.
                                   auto guard = fork_index_guard{ctx.current_frame().fork_index, sizeof...(Funcs)};
                                   action     = invoke_mod(get<K>(funcs), ctx);
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
            auto guard = fork_index_guard{ctx.current_frame().fork_index, Index + 1U};
            return invoke_mod(get<Index>(funcs), ctx, default_action, args...);
        } else if constexpr (sizeof...(Args) >= 2) {
            // Let invoke_mod's drop fallback try calling this mod with fewer args.
            if constexpr (!std::same_as<std::remove_cvref_t<type_at<0, Args...>>, special_event>
                          && std::same_as<std::remove_cvref_t<type_at<sizeof...(Args) - 1, Args...>>, special_event>)
            {
                auto guard = fork_index_guard{ctx.current_frame().fork_index, Index + 1U};
                return invoke_mod(get<Index>(funcs), ctx, default_action, args...);
            } else {
                return default_action;
            }
        } else {
            return default_action;
        }
    }

    /// Run the functions and give them the specified context and arguments (optionally)
    template <Context CtxT, typename... Mods, typename... Args>
    context_action invoke_mods(CtxT &ctx, std::tuple<Mods...> &mods, Args... args) noexcept {
        using enum context_action;
        if constexpr (sizeof...(Args) == 1) {
            if constexpr (std::same_as<std::remove_cvref_t<type_at<0, Args...>>, special_event>) {
                // Special events (start, load_event, etc.) should be delivered to ALL mods.
                // Mods that don't handle a code return drop_event — we ignore those and
                // keep the last "interesting" result (next, recovery, exit).
                // Use drop_event as the default so non-invocable mods don't leak through as "handled".
                return [&]<std::size_t... I>(std::index_sequence<I...>) noexcept {
                    auto result = drop_event;
                    (([&]() noexcept {
                         auto action = fork_mod<I>(ctx, mods, drop_event, args...);
                         if (action != drop_event) {
                             result = action;
                         }
                     })(),
                     ...);
                    return result;
                }(std::make_index_sequence<sizeof...(Mods)>{});
            } else {
                return [&]<std::size_t... I>(std::index_sequence<I...>) noexcept {
                    auto action = next;
                    std::ignore = (((action = fork_mod<I>(ctx, mods, next, args...)) == next) && ...);
                    return action;
                }(std::make_index_sequence<sizeof...(Mods)>{});
            }
        } else if constexpr (detail::args_contain_special_event<Args...>) {
            // Special events with extra args (e.g. device_query + special_event
            // pushed by the router) should be delivered to ALL mods, same as
            // single-arg special events.  Void handlers return drop_event which
            // we skip — we keep the last "interesting" result (next, recovery, exit).
            return [&]<std::size_t... I>(std::index_sequence<I...>) noexcept {
                auto result = drop_event;
                (([&]() noexcept {
                     auto action = fork_mod<I>(ctx, mods, drop_event, args...);
                     if (action != drop_event) {
                         result = action;
                     }
                 })(),
                 ...);
                return result;
            }(std::make_index_sequence<sizeof...(Mods)>{});
        } else {
            return [&]<std::size_t... I>(std::index_sequence<I...>) noexcept {
                auto action = next;
                std::ignore = (((action = fork_mod<I>(ctx, mods, next, args...)) == next) && ...);
                return action;
            }(std::make_index_sequence<sizeof...(Mods)>{});
        }
    }

    /// Run functions until one of them return "context_action::next"
    template <Context CtxT, typename... Funcs, typename... Args>
    context_action invoke_first_mod_of(CtxT &ctx, std::tuple<Funcs...> &funcs, Args... args) noexcept {
        using enum context_action;
        return [&]<std::size_t... I>(std::index_sequence<I...>) noexcept {
            auto action = drop_event;
            std::ignore = (((action = fork_mod<I>(ctx, funcs, drop_event, args...)) != next) && ...);
            return action;
        }(std::make_index_sequence<sizeof...(Funcs)>{});
    }

    /// Enter a sub-pipeline, invoke_mods, exit — the common enter/invoke/exit triple.
    template <Context CtxT, typename... Funcs>
    context_action invoke_sub_pipeline(CtxT &ctx, std::tuple<Funcs...> &mods) noexcept {
        auto guard = sub_pipeline_guard<CtxT, Funcs...>{ctx, mods};
        return invoke_mods(ctx, mods);
    }

    /// Enter a sub-pipeline, invoke_mods with a special_event, exit.
    template <Context CtxT, typename... Funcs>
    context_action invoke_sub_pipeline(CtxT &ctx, std::tuple<Funcs...> &mods, special_event const &tag) noexcept {
        auto guard = sub_pipeline_guard<CtxT, Funcs...>{ctx, mods};
        return invoke_mods(ctx, mods, tag);
    }

    /// Enter a sub-pipeline, invoke_first_mod_of, exit.
    template <Context CtxT, typename... Funcs>
    context_action invoke_first_mod_of_sub_pipeline(CtxT &ctx, std::tuple<Funcs...> &mods, special_event const &tag) noexcept {
        auto guard = sub_pipeline_guard<CtxT, Funcs...>{ctx, mods};
        return invoke_first_mod_of(ctx, mods, tag);
    }

    /// A unique, address-stable identity token for each mod type. Comparing
    /// `&type_id<M>` works across translation units because this is an inline
    /// variable template (COMDAT-merged), and distinct specializations are
    /// distinct objects.
    template <typename T>
    struct type_id_t {};

    template <typename T>
    inline constexpr type_id_t<T> type_id{};

    /// Depth-first walk: call fn(mod) for every mod in the tree,
    /// recursing into sub_mods() when present.
    template <typename Mod, typename Fn>
    void walk_mod_tree(Mod& mod, Fn&& fn) noexcept {
        fn(mod);
        if constexpr (requires { mod.sub_mods(); }) {
            std::apply(
              [&](auto&... sub) noexcept {
                  (walk_mod_tree(sub, fn), ...);
              },
              mod.sub_mods());
        }
    }

    /// True if tag is a lifecycle event (toggle_on or toggle_off).
    /// Both share the same code; they are distinguished by value.
    [[nodiscard]] constexpr bool is_lifecycle_event(special_event const& tag) noexcept {
        return tag.code == toggle_on.code;
    }

    /// Recurse into a mod's `sub_mods()` (if any) and report the mod to `out`
    /// when its type matches `token`. Used by the typed `mods<T>`/`rmods<T>`.
    template <typename Mod>
    void collect_mods_of_impl(Mod &mod, void const *token, std::function_ref<void(void *)> out, bool const recursive) noexcept {
        if constexpr (requires { mod.sub_mods(); }) {
            if (recursive) {
                std::apply(
                  [&](auto &...sub) constexpr noexcept {
                      (collect_mods_of_impl(sub, token, out, recursive), ...);
                  },
                  mod.sub_mods());
            }
        }
        if (&type_id<std::remove_cvref_t<Mod>> == token) {
            out(&mod);
        }
    }

    template <typename CtxT>
    void collect_mods_of(CtxT &ctx, void const *token, std::function_ref<void(void *)> out, bool const recursive) noexcept {
        std::apply(
          [&](auto &...mod) constexpr noexcept {
              (collect_mods_of_impl(mod, token, out, recursive), ...);
          },
          ctx.get_mods());
    }

    /// Report the devnodes of the mods that self-identify as device creators
    /// (uinput) via `self_devnode()`, recursing into `sub_mods()` (routers /
    /// sub-pipelines).
    template <typename Mod>
    void collect_self_devnodes_impl(Mod &mod, std::function_ref<void(std::string_view)> out) noexcept {
        walk_mod_tree(mod, [&](auto& m) noexcept {
            if constexpr (requires { m.self_devnode(); }) {
                if (auto const node = m.self_devnode(); !node.empty()) {
                    out(node);
                }
            }
        });
    }

    template <typename CtxT>
    void collect_self_devnodes(CtxT &ctx, std::function_ref<void(std::string_view)> out) noexcept {
        std::apply(
          [&](auto &...mod) constexpr noexcept {
              (collect_self_devnodes_impl(mod, out), ...);
          },
          ctx.get_mods());
    }

    /// Dynamic Context Interface
    struct [[nodiscard]] any_dynamic_context {
        any_dynamic_context() noexcept                                  = default;
        any_dynamic_context(any_dynamic_context const &)                = default;
        any_dynamic_context(any_dynamic_context &&) noexcept            = default;
        any_dynamic_context &operator=(any_dynamic_context const &)     = default;
        any_dynamic_context &operator=(any_dynamic_context &&) noexcept = default;
        virtual ~any_dynamic_context() noexcept                         = default;

        [[nodiscard]] virtual event_type const &event() const noexcept             = 0;
        [[nodiscard]] virtual event_type       &event() noexcept                   = 0;
        virtual void                            event(event_type const &) noexcept = 0;

        /// Invoke the mod at `index` with the given default action and optional special event.
        virtual context_action invoke_mod(std::size_t index, context_action default_action, special_event const &tag) noexcept = 0;

        /// Invoke the mod at `index` with the given default action (no special event — plain call).
        virtual context_action invoke_mod(std::size_t index, context_action default_action) noexcept = 0;

        /// Re-emit the current event through the mods starting at `from_index`.
        virtual context_action reemit(std::size_t from_index) noexcept = 0;

        /// Re-emit `inp_event` through the mods starting at `from_index`.
        virtual context_action reemit(std::size_t from_index, event_type const &inp_event) noexcept = 0;

        /// Invoke `out` for the devnode of every mod in the pipeline that
        /// self-identifies as a device creator (recursing into sub-pipelines).
        virtual void for_each_self_devnode(std::function_ref<void(std::string_view)> out) noexcept = 0;

        /// Invoke `out` with a pointer to each mod whose type matches `token`
        /// (see `type_id`); recurses into sub-pipelines when `recursive`.
        virtual void for_each_mod_of(void const *token, std::function_ref<void(void *)> out, bool recursive) noexcept = 0;
    };

    /// Implementation of the a dynamic context
    template <typename CtxT>
    struct [[nodiscard]] any_dynamic_context_model final : any_dynamic_context {
      private:
        CtxT *ctx;

      public:
        explicit any_dynamic_context_model(CtxT *inp_ctx) noexcept
            requires Context<CtxT>
          : ctx{inp_ctx} {}

        explicit any_dynamic_context_model(CtxT &inp_ctx) noexcept
            requires Context<CtxT>
          : ctx{std::addressof(inp_ctx)} {}

        any_dynamic_context_model(any_dynamic_context_model &&) noexcept            = default;
        any_dynamic_context_model &operator=(any_dynamic_context_model &&) noexcept = default;
        any_dynamic_context_model(any_dynamic_context_model const &)                = delete;
        any_dynamic_context_model &operator=(any_dynamic_context_model const &)     = delete;
        ~any_dynamic_context_model() noexcept override                              = default;

        [[nodiscard]] event_type const &event() const noexcept override {
            return ctx->event();
        }

        [[nodiscard]] event_type &event() noexcept override {
            return ctx->event();
        }

        void event(event_type const &inp_event) noexcept override {
            ctx->event(inp_event);
        }

        context_action
        invoke_mod(std::size_t const index, context_action const default_action, special_event const &tag) noexcept override {
            return [&]<std::size_t... I>(std::index_sequence<I...>) constexpr noexcept {
                context_action action = default_action;
                std::ignore = (((I == index) ? (action = invoke_mod_with_special<I>(*ctx, tag, default_action), true) : false) || ...);
                return action;
            }(std::make_index_sequence<std::tuple_size_v<typename CtxT::mods_type>>{});
        }

        context_action invoke_mod(std::size_t const index, context_action const /*default_action*/) noexcept override {
            return invoke_mod_at(*ctx, ctx->get_mods(), index);
        }

        context_action reemit(std::size_t const from_index) noexcept override {
            return invoke_mods_from(*ctx, ctx->get_mods(), from_index);
        }

        context_action reemit(std::size_t const from_index, event_type const &inp_event) noexcept override {
            auto const cur_ev = std::exchange(ctx->event(), inp_event);
            auto const res    = invoke_mods_from(*ctx, ctx->get_mods(), from_index);
            ctx->event(cur_ev);
            return res;
        }

        void for_each_self_devnode(std::function_ref<void(std::string_view)> out) noexcept override {
            collect_self_devnodes(*ctx, out);
        }

        void for_each_mod_of(void const *token, std::function_ref<void(void *)> out, bool const recursive) noexcept override {
            collect_mods_of(*ctx, token, out, recursive);
        }
    };

    /// Type-erased analog of the fork stack: a handle to the mod at a
    /// compile-time index of the currently bound dynamic context.
    template <std::size_t NIndex>
    struct [[nodiscard]] basic_dynamic_context_view {
      private:
        any_dynamic_context *ctx;

      public:
        explicit constexpr basic_dynamic_context_view(any_dynamic_context *inp_ctx) noexcept : ctx{inp_ctx} {}

        [[nodiscard]] event_type const &event() const noexcept {
            return ctx->event();
        }

        [[nodiscard]] event_type &event() noexcept {
            return ctx->event();
        }

        void event(event_type const &inp_event) noexcept {
            ctx->event(inp_event);
        }

        context_action fork_emit() noexcept {
            return ctx->reemit(NIndex);
        }

        context_action fork_emit(event_type const &inp_event) noexcept {
            return ctx->reemit(NIndex, inp_event);
        }

        context_action fork_emit(user_event const &inp_ev) noexcept {
            return fork_emit(event_type{inp_ev});
        }

        context_action fork_emit(event_type::type_type const  inp_type,
                                 event_type::code_type const  inp_code,
                                 event_type::value_type const inp_val) noexcept {
            return fork_emit(event_type{inp_type, inp_code, inp_val});
        }

        /// Invoke the mod at NIndex directly (no tag).
        context_action operator()() noexcept {
            return ctx->invoke_mod(NIndex, context_action::next);
        }

        /// Invoke the mod at NIndex with the given special_event.
        context_action operator()(special_event const &tag) noexcept {
            return ctx->invoke_mod(NIndex, context_action::next, tag);
        }
    };

    constexpr struct [[nodiscard]] basic_dynamic_context : thread_binding<any_dynamic_context> {
        template <typename ConcreteT>
        using model_type = any_dynamic_context_model<ConcreteT>;

        static constexpr thread_binding self{};

        /// Access the mod at a compile-time index of the currently bound context.
        template <std::size_t NIndex>
        [[nodiscard]] constexpr auto mod() const noexcept {
            return basic_dynamic_context_view<NIndex>{self.ptr()};
        }

        [[nodiscard]] event_type const &event() const noexcept {
            return self->event();
        }

        [[nodiscard]] event_type &event() noexcept {
            return self->event();
        }

        void event(event_type const &inp_event) const noexcept {
            self->event(inp_event);
        }

        /// Whether a dynamic context is currently bound (inside a pipeline run).
        [[nodiscard]] bool bound() const noexcept {
            return binding::instance() != nullptr;
        }

        /// Invoke `out` for the devnode of every mod in the current pipeline.
        void for_each_self_devnode(std::function_ref<void(std::string_view)> out) const noexcept {
            self->for_each_self_devnode(out);
        }

        /// Enumerate the top-level mods of the current pipeline matching `T`.
        template <typename T>
        [[nodiscard]] std::vector<std::reference_wrapper<std::remove_cvref_t<T>>> mods(T const & = {}) const noexcept {
            std::vector<std::reference_wrapper<std::remove_cvref_t<T>>> out;
            self->for_each_mod_of(
              &type_id<std::remove_cvref_t<T>>,
              [&](void *ptr) noexcept {
                  out.emplace_back(*static_cast<std::remove_cvref_t<T> *>(ptr));
              },
              false);
            return out;
        }

        /// Enumerate mods matching `T`, recursing into routers/sub-pipelines.
        template <typename T>
        [[nodiscard]] std::vector<std::reference_wrapper<std::remove_cvref_t<T>>> rmods(T const & = {}) const noexcept {
            std::vector<std::reference_wrapper<std::remove_cvref_t<T>>> out;
            self->for_each_mod_of(
              &type_id<std::remove_cvref_t<T>>,
              [&](void *ptr) noexcept {
                  out.emplace_back(*static_cast<std::remove_cvref_t<T> *>(ptr));
              },
              true);
            return out;
        }
    } dynamic_context;

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

        /// Type-erased function pointer for invoking active mods from a given index.
        using invoke_active_fn_t = context_action (*)(basic_context &, void *, std::size_t);

        /// A frame on the fork stack: tracks which mods tuple to iterate and where to start.
        struct fork_frame {
            void              *active_mods = nullptr; ///< pointer to the mods tuple (mods_type* or sub-mods tuple*)
            invoke_active_fn_t invoke_fn   = nullptr; ///< static function that invokes mods from the tuple
            std::size_t        fork_index  = 0;       ///< starting index for fork_emit within this frame
        };

        static constexpr std::size_t max_fork_depth = 8;

      private:
        event_type                                              ev{};
        mods_type                                               mods_{};
        std::array<variable_pointer, variable_size_v<Funcs...>> variables = extract_variables(mods_);

        /// Fork stack: tracks nested sub-pipeline contexts for fork_emit delegation.
        std::array<fork_frame, max_fork_depth> fork_stack_{};
        std::size_t                            fork_depth_ = 0;

      public:
        /// Access the current fork frame (used by fork_mod and invoke_mod_at).
        [[nodiscard]] fork_frame &current_frame() noexcept {
            return fork_stack_[fork_depth_];
        }

      private:
        /// Static helper for invoking root mods (used as invoke_fn in the root frame).
        static context_action invoke_root_mods(basic_context &ctx, void *ptr, std::size_t const idx) noexcept {
            return invoke_mods_from(ctx, *static_cast<mods_type *>(ptr), idx);
        }

        /// Static helper for invoking sub-pipeline mods (used as invoke_fn in sub-pipeline frames).
        template <typename... SubFuncs>
        static context_action invoke_sub_mods(basic_context &ctx, void *ptr, std::size_t const idx) noexcept {
            return invoke_mods_from(ctx, *static_cast<std::tuple<SubFuncs...> *>(ptr), idx);
        }

      public:
        constexpr basic_context() noexcept = default;

        consteval explicit basic_context(event_type const &inp_ev, std::remove_cvref_t<Funcs>... inp_funcs) noexcept
          : ev{inp_ev},
            mods_{inp_funcs...} {}

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) event(this Self &&self) noexcept {
            return std::forward_like<Self>(self.ev);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) get_mods(this Self &&self) noexcept {
            return std::forward_like<Self>(self.mods_);
        }

        constexpr void event(event_type const &inp_event) noexcept {
            ev = inp_event;
        }

        /// Broadcast a special event to ALL mods in this pipeline.
        context_action broadcast(special_event const &tag) noexcept {
            return invoke_mods(*this, mods_, tag);
        }

        template <typename Func, typename Self>
            requires((std::same_as<mod_type<Func>, Funcs> || ...))
        [[nodiscard]] constexpr auto &mod(this Self &&self) noexcept {
            using mod_type = mod_type<Func>;
            // we're not using Func directly because we may have duplicate types in the tuple, and we want the
            // first one to be returned instead of throwing error that there's multiple of that type.
            return get<index_at<mod_type, Funcs...>>(std::forward_like<Self>(self.mods_));
        }

        template <typename Func, typename Self>
            requires((std::same_as<mod_type<Func>, Funcs> || ...))
        [[nodiscard]] constexpr auto &mod(this Self &&self, [[maybe_unused]] Func const &) noexcept {
            using mod_type = mod_type<Func>;
            return get<index_at<mod_type, Funcs...>>(std::forward_like<Self>(self.mods_));
        }

        template <std::size_t Index = 0, typename Self>
        [[nodiscard]] constexpr auto &mod(this Self &&self) noexcept {
            return get<Index>(std::forward_like<Self>(self.mods_));
        }

        /// @returns variant<monostate, Var::value_type...>
        [[nodiscard]] constexpr auto operator[](std::string_view const name) const {
            return find_variable(name, variables, mods_);
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
              mods_);
        }

        template <Modifier Mod>
        [[nodiscard]] consteval auto operator|(Mod &&inp_mod) const noexcept {
            return std::apply(
              [&](auto const &...funcs) constexpr noexcept {
                  return basic_context<std::remove_cvref_t<Funcs>..., std::remove_cvref_t<Mod>>{ev, funcs..., inp_mod};
              },
              mods_);
        }

        /// Run the remaining mods from each fork stack frame, walking up to the root.
        context_action fork_emit() noexcept {
            auto const     saved_depth = fork_depth_;
            context_action res         = context_action::next;
            for (;;) {
                auto &frame     = current_frame();
                auto  saved_idx = frame.fork_index;
                if (frame.active_mods) {
                    res = frame.invoke_fn(*this, frame.active_mods, frame.fork_index);
                } else {
                    // Root frame: active_mods is null; use mods_ directly.
                    res = invoke_mods_from(*this, mods_, frame.fork_index);
                }
                frame.fork_index = saved_idx;
                if (res != context_action::next || fork_depth_ == 0) {
                    fork_depth_ = saved_depth;
                    return res;
                }
                --fork_depth_;
            }
        }

        context_action fork_emit(event_type const &inp_event) noexcept {
            auto const cur = event();
            event(inp_event);
            auto const res = fork_emit();
            event(cur);
            return res;
        }

        context_action fork_emit(user_event const &inp_ev) noexcept {
            return fork_emit(event_type{inp_ev});
        }

        context_action fork_emit(event_type::type_type const  inp_type,
                                 event_type::code_type const  inp_code,
                                 event_type::value_type const inp_val) noexcept {
            auto cur_rv = event();
            cur_rv.set(inp_type, inp_code, inp_val);
            return fork_emit(cur_rv);
        }

        /// Enter a sub-pipeline: push a new fork frame for the given sub-mods tuple.
        template <typename... SubFuncs>
        void enter_sub_pipeline(std::tuple<SubFuncs...> &sub_mods) noexcept {
            ++fork_depth_;
            current_frame() = fork_frame{&sub_mods, &invoke_sub_mods<SubFuncs...>, 0};
        }

        /// Exit a sub-pipeline: pop the current fork frame.
        void exit_sub_pipeline() noexcept {
            --fork_depth_;
        }

        /// The mods of this context, exposed for recursion into sub-pipelines.
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) sub_mods(this Self &&self) noexcept {
            return std::forward_like<Self>(self.mods_);
        }

        /// Enumerate the top-level mods of this context whose type matches `T`.
        template <typename T>
        [[nodiscard]] std::vector<std::reference_wrapper<std::remove_cvref_t<T>>> mods(T const & = {}) noexcept {
            std::vector<std::reference_wrapper<std::remove_cvref_t<T>>> out;
            collect_mods_of(
              *this,
              &type_id<std::remove_cvref_t<T>>,
              [&](void *ptr) noexcept {
                  out.emplace_back(*static_cast<std::remove_cvref_t<T> *>(ptr));
              },
              false);
            return out;
        }

        /// Enumerate mods matching `T`, recursing into routers/sub-pipelines.
        template <typename T>
        [[nodiscard]] std::vector<std::reference_wrapper<std::remove_cvref_t<T>>> rmods(T const & = {}) noexcept {
            std::vector<std::reference_wrapper<std::remove_cvref_t<T>>> out;
            collect_mods_of(
              *this,
              &type_id<std::remove_cvref_t<T>>,
              [&](void *ptr) noexcept {
                  out.emplace_back(*static_cast<std::remove_cvref_t<T> *>(ptr));
              },
              true);
            return out;
        }

        context_action operator()(special_event const &tag) noexcept {
            switch (tag.code) {
                case start.code:      // start
                    return start_mods();
                case no_init.code:    // no_init
                    return run_loop();
                case load_event.code: // load_event
                case next_event.code: // next_event
                case toggle_on.code:  // toggle_on / toggle_off (distinguished by value)
                    // Forward to the mods directly.
                    return invoke_mods(*this, mods_, tag);
                default: return context_action::next;
            }
        }

        /// Start the pipeline for the first time or after recovery.
        /// Returns true if start succeeded.
        [[nodiscard]] bool start_pipeline() noexcept {
            auto const action = operator()(start);
            return action != context_action::exit;
        }

        /// Process the current event through all mods and handle the result.
        /// Returns true if the pipeline should continue.
        [[nodiscard]] bool process_event() noexcept {
            return handle_action(invoke_mods(*this, mods_));
        }

        /// Handle an action from event processing or a provider in the loop.
        /// recovery → restart and continue; exit → stop; next/drop_event → continue.
        [[nodiscard]] bool handle_action(context_action const action) noexcept {
            using enum context_action;
            if (action == recovery) {
                log("Restarting pipeline...");
                return start_pipeline();
            }
            if (action == exit) {
                return false;
            }
            return true;
        }

        void operator()() noexcept {
            if (!start_pipeline()) {
                return;
            }
            std::ignore = run_loop();
        }

      private:
        context_action start_mods() noexcept try {
            dynamic_scope scope{dynamic_context, *this};
            return invoke_mods(*this, mods_, start);
        } catch (...) {
            return context_action::exit;
        }

        context_action run_loop() noexcept {
            dynamic_scope scope{dynamic_context, *this};
            using enum context_action;
            using self_type = basic_context<std::remove_cvref_t<Funcs>...>;
            static_assert(((invokable_mod<Funcs, self_type> || invokable_mod<Funcs, self_type, special_event>) && ...),
                          "At least one of the mods are not callable");
            for (;;) {
                // Try next_event providers (non-blocking event pull).
                switch (auto const provider = invoke_first_mod_of(*this, mods_, next_event)) {
                    case next:
                        if (!handle_action(invoke_mods(*this, mods_))) {
                            return {};
                        }
                        continue;
                    [[likely]] case drop_event:
                        break;
                    [[unlikely]] default:
                    [[unlikely]] case recovery:
                    [[unlikely]] case exit:
                        if (!handle_action(provider)) {
                            return {};
                        }
                        break;
                }
                // next_event exhausted -> block in load_event (pure wait; it does
                // NOT load an event). After it wakes, loop back to next_event.
                switch (auto const load_result = invoke_mods(*this, mods_, load_event)) {
                    case next:
                        // Event was loaded — process it through the pipeline.
                        if (!handle_action(invoke_mods(*this, mods_))) {
                            return {};
                        }
                        continue;
                    [[likely]] case drop_event:
                        continue;
                    [[unlikely]] default:
                    [[unlikely]] case recovery:
                    [[unlikely]] case exit:
                        if (!handle_action(load_result)) {
                            return {};
                        }
                        break;
                }
            }
        }

      public:
        /// Pass-through: invoke our mods as a sub-pipeline so that fork_emit()
        /// from within a child sees the correct frame (our mods tuple + index)
        /// and then falls through to the parent frame.
        context_action operator()(Context auto &ctx) noexcept {
            return invoke_sub_pipeline(ctx, mods_);
        }

        /// Pass-through a special_event to the mods.
        context_action operator()(Context auto &ctx, special_event const &tag) noexcept {
            return invoke_sub_pipeline(ctx, mods_, tag);
        }

        /// Pass-through with extra arguments (e.g. a device_query pushed by the router on start).
        template <Context CtxT, typename... Args>
            requires(sizeof...(Args) >= 2)
        context_action operator()(CtxT &ctx, Args const &...args) noexcept {
            auto guard = sub_pipeline_guard<CtxT, Funcs...>{ctx, mods_};
            return invoke_mods(ctx, mods_, args...);
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

    /// Run the mod at a runtime `Index` (compile-time dispatch) with the given special_event.
    template <std::size_t Index, Context CtxT>
    constexpr context_action invoke_mod_with_special(CtxT &ctx, special_event const &tag, context_action const default_action) noexcept {
        return fork_mod<Index>(ctx, ctx.get_mods(), default_action, tag);
    }

    /// Run the mods starting at a runtime `start_index`, stopping early on a non-`next` action.
    template <Context CtxT, typename... Funcs>
    context_action
    invoke_mods_from(CtxT &ctx, std::tuple<Funcs...> &funcs, std::size_t const start_index, context_action const default_action) noexcept {
        using enum context_action;
        return [&]<std::size_t... I>(std::index_sequence<I...>) noexcept {
            context_action action = default_action;
            std::ignore =
              ((((I >= start_index) ? (action = fork_mod<I>(ctx, funcs, default_action), true) : true) && (action == next)) && ...);
            return action;
        }(std::make_index_sequence<sizeof...(Funcs)>{});
    }

} // namespace fs8
