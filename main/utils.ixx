module;
#include <ranges>
#include <type_traits>
#include <utility>
export module foresight.main.utils;

export namespace fs8 {
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

    template <std::size_t N>
    struct basic_arguments : std::ranges::range_adaptor_closure<basic_arguments<N>> {
      private:
        std::array<char const*, N + 1> defaults_;

      public:
        template <typename... Args>
        explicit consteval basic_arguments(Args&&... defaults) noexcept : defaults_{"", std::forward<Args>(defaults)...} {}

        auto operator()(int const argc, char const* const* argv) const& noexcept {
            auto const        beg   = argc <= 1 ? defaults_.data() : argv;
            std::size_t const count = argc <= 1 ? defaults_.size() : static_cast<std::size_t>(argc);
            return std::span{beg, count} | std::views::drop(1) | transform_to<std::string_view>();
        }

        auto operator()(std::span<char const* const> const args) const& noexcept {
            return args | transform_to<std::string_view>();
        }

        // Prevent dangling references to defaults_
        auto operator()(int argc, char const* const* argv) const&& noexcept = delete;

        template <typename... DefArgs>
        consteval auto operator[](DefArgs&&... defaults) const noexcept {
            return basic_arguments<sizeof...(DefArgs)>{std::forward<DefArgs>(defaults)...};
        }
    };

    inline constexpr basic_arguments<0> arguments{};

} // namespace fs8
