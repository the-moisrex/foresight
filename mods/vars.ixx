// Created by moisrex on 12/12/25.

module;
#include <any>
#include <array>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string_view>
export module foresight.mods.vars;
import foresight.mods.context;
import foresight.utils.hash;

export namespace fs8 {

    /**
     * Variable support
     * This struct only holds one single variable
     */
    template <std::uint32_t Hash, typename T = std::any>
    struct [[nodiscard]] var_type {
        static constexpr std::size_t hash = Hash;

        static_assert(std::default_initializable<T> && std::copyable<T>, "T Must be Constructible and Copyable");

        // We don't want to wrap the value in std::optional when we don't need to.
        // todo: if there's a way to check if we can call the constructor of T at compile time, we should use that instead
        static constexpr bool is_constructible_at_compile_time =
          std::integral<T>
          || std::floating_point<T>
          // || std::same_as<T, std::any>
          || std::same_as<T, std::string>
          || std::same_as<T, std::u32string>;

        using value_type   = T;
        using storage_type = std::conditional_t<is_constructible_at_compile_time, value_type, std::optional<T>>;

      private:
        storage_type obj{};

      public:
        template <typename Arg>
            requires(std::convertible_to<Arg, T>)
        consteval var_type(Arg&& initial_value) noexcept : obj{std::forward<Arg>(initial_value)} {}

        consteval var_type(var_type const&)                = default;
        constexpr var_type(var_type&&) noexcept            = default;
        consteval var_type& operator=(var_type const&)     = default;
        constexpr var_type& operator=(var_type&&) noexcept = default;
        constexpr ~var_type()                              = default;

        /// Get variable names
        [[nodiscard]] consteval std::array<std::uint32_t, 1> operator[](get_variables_tag) const noexcept {
            return {hash};
        }

        /// Get variable
        [[nodiscard]] constexpr T const& operator[](std::string_view const inp_name) const noexcept {
            assert(ci_hash(inp_name) == hash);
            return value();
        }

        /// Get variable
        [[nodiscard]] constexpr T& operator[](std::string_view const inp_name) noexcept {
            assert(ci_hash(inp_name) == hash);
            return value();
        }

        [[nodiscard]] constexpr T const& value() const noexcept {
            if constexpr (is_constructible_at_compile_time) {
                return obj;
            } else {
                assert(obj.has_value());
                return obj.value();
            }
        }

        [[nodiscard]] constexpr T& value() noexcept {
            if constexpr (is_constructible_at_compile_time) {
                return obj;
            } else {
                assert(obj.has_value());
                return obj.value();
            }
        }

        /// Default construct if it's wrapped in std::optional
        void operator()(start_tag)
            requires(!is_constructible_at_compile_time)
        {
            obj = T{};
        }
    };

} // namespace fs8
