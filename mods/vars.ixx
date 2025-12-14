// Created by moisrex on 12/12/25.

module;
#include <any>
#include <array>
#include <cassert>
#include <optional>
#include <string_view>
export module foresight.mods.vars;
import foresight.mods.context;
import foresight.main.utils;

export namespace fs8 {

    /**
     * Variable support
     * This struct only holds one single variable
     */
    template <typename T = std::any>
    struct [[nodiscard]] var_type {
        static_assert(std::default_initializable<T> && std::copyable<T>, "T Must be Constructible and Copyable");

        // We don't want to wrap the value in std::optional when we don't need to.
        static constexpr bool is_constructible_at_compile_time = constexpr_constructible<T>;

        using value_type   = T;
        using storage_type = std::conditional_t<is_constructible_at_compile_time, value_type, std::optional<T>>;

      private:
        std::string_view name;
        storage_type     obj{};

      public:
        template <typename Arg>
            requires(std::convertible_to<Arg, T>)
        consteval var_type(std::string_view const inp_name, Arg&& initial_value) noexcept
          : name{inp_name},
            obj{std::forward<Arg>(initial_value)} {}

        explicit(false) consteval var_type(std::string_view const inp_name) noexcept : name{inp_name} {}

        consteval var_type(var_type const&)                = default;
        constexpr var_type(var_type&&) noexcept            = default;
        consteval var_type& operator=(var_type const&)     = default;
        constexpr var_type& operator=(var_type&&) noexcept = default;
        constexpr ~var_type()                              = default;

        /// Get variable names
        [[nodiscard]] consteval std::array<std::string_view, 1> operator[](get_variables_tag) const noexcept {
            return {name};
        }

        /// Get variable
        [[nodiscard]] constexpr T const& operator[](std::string_view const inp_name) const noexcept {
            assert(inp_name == name);
            return value();
        }

        /// Get variable
        [[nodiscard]] constexpr T& operator[](std::string_view const inp_name) noexcept {
            assert(inp_name == name);
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

    template <typename T = std::any>
    var_type(std::string_view, T&&) -> var_type<T>;

} // namespace fs8
