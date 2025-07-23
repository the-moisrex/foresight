module;
#include <ranges>
#include <type_traits>
#include <utility>
export module foresight.main.utils;

export namespace foresight {
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
} // namespace foresight
