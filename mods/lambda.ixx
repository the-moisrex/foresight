// Created by moisrex on 12/12/25.

module;
#include <concepts>
#include <utility>
export module fs8.mods:lambda;
import fs8.context;

export namespace fs8 {

    /**
     * Wrap one or more functions so they can be used as mods and/or callbacks.
     *
     * Usage as a mod (a function run on each event):
     * run([] (this auto& self, Context auto& ctx) {
     *   // do anything you want
     * })
     *
     * Usage as a runtime callback (e.g. how2type::emit), capturing state by
     * reference:
     * std::vector<user_event> events;
     * run rec{[&events](user_event const& event) noexcept {
     *   events.push_back(event);
     * }};
     */
    template <typename... Bases>
    struct [[nodiscard]] run : Bases... {
        static_assert((std::copy_constructible<Bases> && ...), "The bases must be copy constructible");

        constexpr explicit(false) run(Bases... bases) : Bases(std::move(bases))... {}

        constexpr run()
            requires(std::default_initializable<Bases> && ...)
        = default;

        constexpr run(run const&)                = default;
        constexpr run(run&&) noexcept            = default;
        constexpr run& operator=(run const&)     = default;
        constexpr run& operator=(run&&) noexcept = default;
        constexpr ~run()                         = default;

        template <Context CtxT>
        constexpr context_action operator()(CtxT& ctx) noexcept {
            using enum context_action;
            context_action action = next;
            std::ignore           = ((action = invoke_mod(static_cast<Bases&>(*this), ctx), action == next) && ...);
            return action;
        }

        constexpr context_action operator()(event_type& event) noexcept {
            using enum context_action;
            context_action action = next;
            std::ignore           = ((action = invoke_mod(static_cast<Bases&>(*this), event), action == next) && ...);
            return action;
        }

        constexpr context_action operator()(event_type const& event) const noexcept {
            using enum context_action;
            context_action action = next;
            std::ignore           = ((action = invoke_mod(static_cast<Bases const&>(*this), event), action == next) && ...);
            return action;
        }

        constexpr context_action operator()(user_event const& usr) const noexcept {
            using enum context_action;
            event_type     event{usr};
            context_action action = next;
            std::ignore           = ((action = invoke_mod(static_cast<Bases const&>(*this), event), action == next) && ...);
            return action;
        }
    };

    template <typename... Ts>
    run(Ts&&...) -> run<std::remove_cvref_t<Ts>...>;

} // namespace fs8
