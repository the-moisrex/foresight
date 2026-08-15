module;
#include <ranges>
#include <type_traits>
#include <utility>
export module fs8.utils;
export import fs8.cli;

export namespace fs8 {

    /// Check if type T has a constexpr constructor
    /// and thus can be used in constexpr contexts, in other words,
    /// the type T can be constructed in compile-time.
    template <typename T, typename... Args>
    concept constexpr_constructible = requires {
        []<T = {
            Args{}...
        }>{}();
    };

    template <typename T>
    struct construct_it_from {
        template <typename... Args>
        [[nodiscard]] constexpr T operator()(Args&&... args) noexcept(std::constructible_from<T>) {
            return T{std::forward<Args>(args)...};
        }
    };

    template <typename T>
    [[nodiscard]] constexpr auto transform_to() {
        return std::views::transform(construct_it_from<T>{});
    }

    template <typename Inp, typename T>
        requires(std::is_invocable_v<T, construct_it_from<Inp>>)
    [[nodiscard]] constexpr auto into(T&& obj) {
        return std::forward<T>(obj)(construct_it_from<Inp>{});
    }

    constexpr struct basic_noop {
        constexpr void operator()() const noexcept {
            // do nothing
        }

        constexpr void operator()([[maybe_unused]] auto&&) const noexcept {
            // do nothing
        }

        // This is Context, but we don't want to include anything here.
        template <typename CtxT>
            requires requires(CtxT ctx) {
                typename CtxT::mods_type;
                CtxT::is_nothrow;
                ctx.event();
                ctx.get_mods();
            }
        [[nodiscard]] constexpr bool operator()([[maybe_unused]] CtxT&) const noexcept {
            return false;
        }
    } noop;

} // namespace fs8
