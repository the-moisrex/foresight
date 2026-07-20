// Created by moisrex on 12/12/25.

module;
#include <concepts>
#include <utility>
export module fs8.mods.lambda;

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

        consteval explicit(false) run(Bases... bases) : Bases(std::move(bases))... {}

        consteval run()
            requires(std::default_initializable<Bases> && ...)
        = default;

        constexpr run(run const&)                = default;
        constexpr run(run&&) noexcept            = default;
        constexpr run& operator=(run const&)     = default;
        constexpr run& operator=(run&&) noexcept = default;
        constexpr ~run()                         = default;

        using Bases::operator()...;
    };

    template <typename... Ts>
    run(Ts&&...) -> run<std::remove_cvref_t<Ts>...>;

} // namespace fs8
