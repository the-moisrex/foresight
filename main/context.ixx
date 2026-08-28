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
        idle,       // idle mode, or watching mode, or restart mode
        exit,       // exit the software
    };

    [[nodiscard]] std::string_view to_string(context_action action) noexcept;

    [[nodiscard]] constexpr bool is_exiting(context_action const action) noexcept {
        using enum context_action;
        return action == idle || action == exit;
    }

    [[nodiscard]] constexpr bool operator!(context_action const action) noexcept {
        return is_exiting(action);
    }

    /// Runtime tag identifiers for dispatching tags through the type-erased interface.
    enum struct [[nodiscard]] dynamic_tag : std::uint8_t {
        none,
        start,
        no_init,
        load_event,
        next_event,
        toggle_on,
        toggle_off,
    };

    /// A tag; carries its runtime `dynamic_tag` id so type-erased contexts can dispatch on it.
    template <dynamic_tag ID>
    struct [[nodiscard]] basic_tag {
        static constexpr bool        is_tag = true;
        static constexpr dynamic_tag id     = ID;
    };

    using no_init_tag    = basic_tag<dynamic_tag::no_init>;
    using start_tag      = basic_tag<dynamic_tag::start>;
    using toggle_on_tag  = basic_tag<dynamic_tag::toggle_on>;
    using toggle_off_tag = basic_tag<dynamic_tag::toggle_off>;
    using next_event_tag = basic_tag<dynamic_tag::next_event>;
    using load_event_tag = basic_tag<dynamic_tag::load_event>;

    /// Run the context mods, don't run the initialization and other setup actions of the mods.
    constexpr no_init_tag no_init{};

    /// Initialization and the setup parts of the mods will happen in actions using this tag.
    constexpr start_tag start{};

    constexpr toggle_on_tag toggle_on{};

    constexpr toggle_off_tag toggle_off{};

    /// This will let the mods set an event to the context, and send them through the whole pipeline.
    constexpr next_event_tag next_event{};

    /// This will wait for an event to be loaded, so this is blocking, and shall be called after the
    /// set_events are done.
    constexpr load_event_tag load_event{};

    template <typename ModT, typename CtxT, typename... Args>
    concept invokable_mod =
      std::is_nothrow_invocable_v<ModT, CtxT &, Args...>
      || std::is_nothrow_invocable_v<ModT, event_type &, Args...>
      || std::is_nothrow_invocable_v<ModT, Args...>;

    template <typename T>
    concept Tag = requires {
        std::remove_cvref_t<T>::is_tag;
        requires std::remove_cvref_t<T>::is_tag;
    } && std::is_trivially_copy_constructible_v<std::remove_cvref_t<T>>;

    template <typename ModT, typename... Args>
    constexpr context_action invoke_mod_inorder(ModT &mod, context_action const default_action, Args &&...args) noexcept {
        using enum context_action;
        using result = std::invoke_result_t<ModT, Args...>;
        static_assert(std::is_nothrow_invocable_v<ModT, Args...>, "Mark the mod as nothrow.");
        if constexpr (std::same_as<result, bool>) {
            return mod(std::forward<Args>(args)...) ? next : drop_event;
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

    template <typename ParentT, Modifier... Funcs>
    struct [[nodiscard]] basic_context_view;

    template <Context CtxT, typename... Funcs>
    constexpr context_action invoke_mods_from(
      CtxT                 &ctx,
      std::tuple<Funcs...> &funcs,
      std::size_t           start_index,
      context_action        default_action = context_action::next) noexcept;

    template <Context CtxT, typename... Funcs>
    constexpr context_action invoke_mod_at(CtxT &ctx, std::tuple<Funcs...> &funcs, std::size_t const index) noexcept {
        using enum context_action;
        return [&]<std::size_t... I>(std::index_sequence<I...>) constexpr noexcept {
            auto action = next;
            std::ignore = (([&]<std::size_t K>() constexpr noexcept {
                               if (K == index) {
                                   // Entries are alternatives: a forked event skips the rest of this tuple.
                                   basic_context_view<CtxT, Funcs...> view{ctx, funcs, sizeof...(Funcs)};
                                   action = invoke_mod(get<K>(funcs), view);
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
            basic_context_view<CtxT, Funcs...> view{ctx, funcs, Index + 1U};
            return invoke_mod(get<Index>(funcs), view, default_action, args...);
        } else if constexpr (sizeof...(Args) >= 2) {
            // Let invoke_mod's drop fallback try calling this mod with fewer args.
            if constexpr (!Tag<type_at<0, Args...>> && Tag<type_at<sizeof...(Args) - 1, Args...>>) {
                basic_context_view<CtxT, Funcs...> view{ctx, funcs, Index + 1U};
                return invoke_mod(get<Index>(funcs), view, default_action, args...);
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
            auto action = drop_event;
            std::ignore = (((action = fork_mod<I>(ctx, funcs, drop_event, args...)) != next) && ...);
            return action;
        }(std::make_index_sequence<sizeof...(Funcs)>{});
    }

    /// A unique, address-stable identity token for each mod type. Comparing
    /// `&type_id<M>` works across translation units because this is an inline
    /// variable template (COMDAT-merged), and distinct specializations are
    /// distinct objects.
    template <typename T>
    struct type_id_t {};

    template <typename T>
    inline constexpr type_id_t<T> type_id{};

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
        if constexpr (requires { mod.self_devnode(); }) {
            if (auto const node = mod.self_devnode(); !node.empty()) {
                out(node);
            }
        }
        if constexpr (requires { mod.sub_mods(); }) {
            std::apply(
              [&](auto &...sub) constexpr noexcept {
                  (collect_self_devnodes_impl(sub, out), ...);
              },
              mod.sub_mods());
        }
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

        /// Invoke the mod at `index` with the given default action and optional tag.
        virtual context_action invoke_mod(std::size_t index, context_action default_action, dynamic_tag tag) noexcept = 0;

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

        context_action invoke_mod(std::size_t const index, context_action const default_action, dynamic_tag const tag) noexcept override {
            return [&]<std::size_t... I>(std::index_sequence<I...>) constexpr noexcept {
                context_action action = default_action;
                std::ignore = (((I == index) ? (action = invoke_dynamic_tagged_mod<I>(*ctx, tag, default_action), true) : false) || ...);
                return action;
            }(std::make_index_sequence<std::tuple_size_v<typename CtxT::mods_type>>{});
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

    /// Type-erased analog of `basic_context_view`: a handle to the mod at a
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
            return ctx->invoke_mod(NIndex, context_action::next, dynamic_tag::none);
        }

        /// Invoke the mod at NIndex with the given tag.
        template <typename TagT>
            requires Tag<TagT>
        context_action operator()(TagT tag) noexcept {
            return ctx->invoke_mod(NIndex, context_action::next, to_dynamic_tag(tag));
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

    template <typename ParentT, Modifier... Funcs>
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
        mods_type                                               mods_{};
        std::array<variable_pointer, variable_size_v<Funcs...>> variables = extract_variables(mods_);

      public:
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

        /// Terminal continuation for the parent chain: the pipeline ends here.
        context_action fork_emit() noexcept {
            return context_action::next;
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

        context_action operator()(start_tag) noexcept try {
            // Make the dynamic context point at this pipeline for the whole start phase.
            dynamic_scope scope{dynamic_context, *this};
            // invoke the mods
            return invoke_mods(*this, mods_, start);
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
            // Make the dynamic context point at this pipeline for the whole run loop
            // so mods reached from event callbacks (e.g. input_manager hotplug) can
            // introspect the active pipeline.
            dynamic_scope scope{dynamic_context, *this};
            using enum context_action;
            using ctx_view = basic_context_view<basic_context<std::remove_cvref_t<Funcs>...>, std::remove_cvref_t<Funcs>...>;
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
                    switch (invoke_first_mod_of(*this, mods_, next_event)) {
                        case next:
                            if (!restart_if(invoke_mods(*this, mods_))) {
                                return;
                            }
                            continue;
                        [[likely]] case drop_event:
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
                        switch (invoke_mods(*this, mods_, load_event)) {
                            [[likely]] case next:
                            case drop_event:
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
                    if (!restart_if(invoke_mods(*this, mods_))) [[unlikely]] {
                        return;
                    }
                } else if constexpr (load_event_count > 0) {
                    // Legacy: load_event providers load events directly (old intercept).
                    switch (invoke_mods(*this, mods_, load_event)) {
                        [[likely]] case next:
                            break;
                        case drop_event:
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
                    if (!restart_if(invoke_mods(*this, mods_))) [[unlikely]] {
                        return;
                    }
                }
            }
        }

        /// Pass-through
        context_action operator()(Context auto &ctx) noexcept {
            return invoke_mods(ctx, mods_);
        }

        /// Pass-through a plain start to the mods.
        context_action operator()(Context auto &ctx, start_tag) noexcept {
            return invoke_mods(ctx, mods_, start);
        }

        /// Pass-through with extra arguments (e.g. a device_query pushed by the router on start).
        /// The trailing argument is expected to be a tag.
        template <typename... Args>
            requires(sizeof...(Args) >= 2)
        context_action operator()(Context auto &ctx, Args const &...args) noexcept {
            return invoke_mods(ctx, mods_, args...);
        }
    };

    /**
     * A lightweight view over a (sub-)tuple of mods, chained to its enclosing
     * continuation. `fork_emit` re-runs the remaining mods of this tuple and
     * then continues through the parent chain up to the root context. All
     * event/state accessors delegate up the parent chain to the root.
     */
    template <typename ParentT, Modifier... SubFuncs>
    struct [[nodiscard]] basic_context_view {
        using type_type  = event_type::type_type;
        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;
        using mods_type  = std::tuple<std::remove_cvref_t<SubFuncs>...>;

      private:
        ParentT    &parent;
        mods_type  &subs;
        std::size_t index;

      public:
        constexpr basic_context_view(ParentT &inp_parent, mods_type &inp_subs, std::size_t const inp_index) noexcept
          : parent{inp_parent},
            subs{inp_subs},
            index{inp_index} {}

        constexpr basic_context_view(basic_context_view const &)                = default;
        constexpr basic_context_view(basic_context_view &&) noexcept            = default;
        constexpr basic_context_view &operator=(basic_context_view const &)     = default;
        constexpr basic_context_view &operator=(basic_context_view &&) noexcept = default;
        constexpr ~basic_context_view() noexcept                                = default;

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) context(this Self &&self) noexcept {
            if constexpr (requires { self.parent.context(); }) {
                return std::forward_like<Self>(self.parent.context());
            } else {
                return std::forward_like<Self>(self.parent);
            }
        }

        context_action fork_emit() noexcept {
            auto const res = invoke_mods_from(parent, subs, index);
            return res == context_action::next ? parent.fork_emit() : res;
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

        context_action fork_emit(type_type const inp_type, code_type const inp_code, value_type const inp_val) noexcept {
            auto ev = event();
            ev.set(inp_type, inp_code, inp_val);
            return fork_emit(ev);
        }

        template <typename Self>
        [[nodiscard]] constexpr auto &&event(this Self &&self) noexcept {
            return std::forward_like<Self>(self.parent.event());
        }

        constexpr void event(event_type const &inp_event) noexcept {
            parent.event(inp_event);
        }

        template <typename Self>
        [[nodiscard]] constexpr auto &&get_mods(this Self &&self) noexcept {
            return std::forward_like<Self>(self.parent.get_mods());
        }

        template <typename Func, typename Self>
            requires((std::same_as<mod_of<Func, SubFuncs...>, SubFuncs> || ...) || requires(ParentT &p) { p.template mod<Func>(); })
        [[nodiscard]] constexpr decltype(auto) mod(this Self &&self) noexcept {
            if constexpr ((std::same_as<mod_of<Func, SubFuncs...>, SubFuncs> || ...)) {
                return std::forward_like<Self>(get<index_at<mod_of<Func, SubFuncs...>, SubFuncs...>>(self.subs));
            } else {
                return std::forward_like<Self>(self.parent.template mod<Func>());
            }
        }

        template <typename Func, typename Self>
            requires((std::same_as<mod_of<Func, SubFuncs...>, SubFuncs> || ...) || requires(ParentT &p) { p.template mod<Func>(); })
        [[nodiscard]] constexpr decltype(auto) mod(this Self &&self, [[maybe_unused]] Func const &) noexcept {
            if constexpr ((std::same_as<mod_of<Func, SubFuncs...>, SubFuncs> || ...)) {
                return std::forward_like<Self>(get<index_at<mod_of<Func, SubFuncs...>, SubFuncs...>>(self.subs));
            } else {
                return std::forward_like<Self>(self.parent.template mod<Func>());
            }
        }

        template <std::size_t NIndex, typename Self>
        [[nodiscard]] constexpr decltype(auto) mod(this Self &&self) noexcept {
            return std::forward_like<Self>(self.parent.template mod<NIndex>());
        }

        /// Enumerate the top-level mods of the underlying context whose type matches `T`.
        template <typename T>
        [[nodiscard]] std::vector<std::reference_wrapper<std::remove_cvref_t<T>>> mods(T const & = {}) noexcept {
            return context().template mods<T>();
        }

        /// Enumerate mods matching `T`, recursing into routers/sub-pipelines.
        template <typename T>
        [[nodiscard]] std::vector<std::reference_wrapper<std::remove_cvref_t<T>>> rmods(T const & = {}) noexcept {
            return context().template rmods<T>();
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

    /// Translate a compile-time tag into its runtime `dynamic_tag` id.
    template <typename TagT>
        requires Tag<TagT>
    [[nodiscard]] constexpr dynamic_tag to_dynamic_tag(TagT) noexcept {
        return std::remove_cvref_t<TagT>::id;
    }

    /// Run the mod at a runtime `Index` (compile-time dispatch) with the given tag.
    template <std::size_t Index, Context CtxT>
    constexpr context_action invoke_dynamic_tagged_mod(CtxT &ctx, dynamic_tag const tag, context_action const default_action) noexcept {
        switch (tag) {
            case dynamic_tag::start: return fork_mod<Index>(ctx, ctx.get_mods(), default_action, start);
            case dynamic_tag::no_init: return fork_mod<Index>(ctx, ctx.get_mods(), default_action);
            case dynamic_tag::load_event: return fork_mod<Index>(ctx, ctx.get_mods(), default_action, load_event);
            case dynamic_tag::next_event: return fork_mod<Index>(ctx, ctx.get_mods(), default_action, next_event);
            case dynamic_tag::toggle_on: return fork_mod<Index>(ctx, ctx.get_mods(), default_action, toggle_on);
            case dynamic_tag::toggle_off: return fork_mod<Index>(ctx, ctx.get_mods(), default_action, toggle_off);
            default: return fork_mod<Index>(ctx, ctx.get_mods(), default_action);
        }
    }

    /// Run the mods starting at a runtime `start_index`, stopping early on a non-`next` action.
    template <Context CtxT, typename... Funcs>
    constexpr context_action
    invoke_mods_from(CtxT &ctx, std::tuple<Funcs...> &funcs, std::size_t const start_index, context_action const default_action) noexcept {
        using enum context_action;
        return [&]<std::size_t... I>(std::index_sequence<I...>) constexpr noexcept {
            context_action action = default_action;
            std::ignore =
              ((((I >= start_index) ? (action = fork_mod<I>(ctx, funcs, default_action), true) : true) && (action == next)) && ...);
            return action;
        }(std::make_index_sequence<sizeof...(Funcs)>{});
    }

} // namespace fs8
