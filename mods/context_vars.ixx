// Created by moisrex on 12/12/25.

module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <tuple>
export module foresight.mods.context:vars;
import foresight.utils.strings;
import foresight.utils.hash;

export namespace fs8 {

    /// Get the variable names from mods
    constexpr struct [[nodiscard]] get_variables_tag {
        static constexpr bool is_tag = true;
    } get_variables;

    template <typename ModT>
    concept has_variables = requires(ModT mod, std::string_view name) {
        mod[get_variables];
        mod[name];
    };

    /**
     * Calculate the number of variables that the input mods will provide
     */
    template <typename... ModsT>
    struct variable_size {
        static constexpr std::size_t value = (variable_size<ModsT>::value + ...);
    };

    template <typename ModT>
    struct variable_size<ModT> {
        static constexpr std::size_t value = std::tuple_size_v<decltype(ModT::operator[](get_variables))>;
    };

    // End condition
    template <>
    struct variable_size<> {
        static constexpr std::size_t value = 0U;
    };

    template <typename... ModsT>
    constexpr std::size_t variable_size_v = variable_size<ModsT...>::value;

    constexpr std::size_t variable_not_found = std::string_view::npos;

    template <std::size_t N>
    [[nodiscard]] consteval bool contains_hash(std::array<std::string_view, N> const arr, std::uint32_t hash) noexcept {
        for (auto const str : arr) {
            if (ci_hash(str) == hash) {
                return true;
            }
        }
        return false;
    }

    /**
     * Find the variable's index
     */
    template <std::size_t, std::uint32_t, typename...>
    struct variable_index {};

    template <std::size_t Index, std::uint32_t Hash, typename ModT, typename... ModsT>
    struct variable_index<Index, Hash, ModT, ModsT...> {
        static constexpr std::size_t value = variable_index<Index + 1U, Hash, ModsT...>::value;
    };

    template <std::size_t Index, std::uint32_t Hash, typename ModT, typename... ModsT>
        requires(has_variables<ModT> && contains_hash(std::declval<ModT>().operator[](get_variables), Hash))
    struct variable_index<Index, Hash, ModT, ModsT...> {
        static constexpr std::size_t value = Index;
    };

    // End condition
    template <std::size_t Index, std::uint32_t Hash>
    struct variable_index<Index, Hash> {
        static constexpr std::size_t value = variable_not_found;
    };

    template <std::uint32_t Hash, typename... ModsT>
    constexpr std::size_t variable_index_v = variable_index<0U, Hash, ModsT...>::value;

    // /**
    //  * This describes the variable
    //  * This is used in a hash-map like table
    //  */
    // struct variable_pointer {
    //     std::string_view name; // variable name
    //     std::uint32_t    hash; // hash of the variable name
    //     std::uint32_t    index;
    // };
    //
    // /// Extract variables
    // /// @returns array<variable_storage, N>
    // [[nodiscard]] consteval auto extract_variables(auto& tup) noexcept {
    //     return std::apply(
    //       []<typename... ModsT>(ModsT const&... mods) {
    //           std::array<variable_pointer, variable_size_v<ModsT...>> values;
    //           std::size_t                                             index = 0;
    //           (
    //             [&]<typename ModT>(ModT const& mod) {
    //                 std::size_t sub_index = 0;
    //                 for (auto const variable_name : mod.operator[](get_variables)) {
    //                     values.at(index + sub_index) =
    //                       variable_pointer{.name  = variable_name,
    //                                        .hash  = ci_hash(variable_name),
    //                                        .index = static_cast<std::uint32_t>(index)};
    //                     ++sub_index;
    //                 }
    //                 ++index;
    //             }(mods),
    //             ...);
    //           return values;
    //       },
    //       tup);
    // }
    //
    //
    // template <std::size_t N>
    // [[nodiscard]] constexpr std::size_t find_variable(std::string_view const name, std::array<variable_pointer, N> variables) noexcept {
    //     auto const hid = ci_hash(name);
    //
    //     // probe over table entries, hash-first to avoid expensive compares
    //     auto const it = std::ranges::lower_bound(variables, hid, {}, [](auto const& entry) {
    //         return entry.hash;
    //     });
    //     if (it == std::end(variables)) [[unlikely]] {
    //         return variable_not_found;
    //     }
    //     auto const& [var_name, _, index] = *it;
    //     if (!iequals(var_name, name)) [[unlikely]] {
    //         return variable_not_found;
    //     }
    //     return index;
    // }
    //
    // template <std::size_t N>
    // [[nodiscard]] constexpr auto find_variable(std::string_view const name, std::array<variable_pointer, N> variables, auto&& tup) {
    //     auto const index = find_variable(name, variables);
    //     if (index == variable_not_found) {
    //         throw std::invalid_argument("Variable not found");
    //     }
    //     // todo
    // }

} // namespace fs8
