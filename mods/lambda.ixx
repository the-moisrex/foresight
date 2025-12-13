// Created by moisrex on 12/12/25.

module;
#include <concepts>
#include <utility>
export module foresight.mods.lambda;

export namespace fs8 {

    /**
     * Usage:
     * run([] (this auto& self, Context auto& ctx) {
     *   // do anything you want
     * })
     */
    template <typename... Bases>
    struct [[nodiscard]] run : Bases... {
        static_assert((std::copyable<Bases> && ...), "The bases must be copyable");

        template <typename... Args>
        explicit(false) consteval run(Args&&... args) : Bases{std::forward<Args>(args)...}... {}

        // template <typename... Args>
        // explicit(false) run(Args&&...) {
        //     static_assert(false, "Your lambda must be constructible at compile time.");
        // }

        consteval run()                          = default;
        constexpr run(run const&)                = default;
        constexpr run(run&&) noexcept            = default;
        constexpr run& operator=(run const&)     = default;
        constexpr run& operator=(run&&) noexcept = default;
        constexpr ~run()                         = default;
    };

    template <typename... Bases>
    run(Bases&&...) -> run<Bases...>;

} // namespace fs8
