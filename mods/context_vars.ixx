// Created by moisrex on 12/12/25.

module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <variant>
export module fs8.context:vars;
import fs8.utils.strings;
import fs8.utils.hash;

namespace details {


    template <typename TupleT, typename T>
    struct prepend;

    template <template <typename...> typename TupleT, typename F, typename... L>
    struct prepend<TupleT<L...>, F> {
        using type = TupleT<F, L...>;
    };

    template <typename TupleT>
    struct unique_types;

    template <template <typename...> typename TupleT, typename First, typename... U>
    struct unique_types<TupleT<First, U...>> {
        using the_rest = unique_types<TupleT<U...>>::type;
        using type     = std::conditional_t<((!std::is_same_v<First, U>) && ...), typename prepend<the_rest, First>::type, the_rest>;
    };

    // the end condition
    template <template <typename...> typename TupleT>
    struct unique_types<TupleT<>> {
        using type = TupleT<>;
    };

    // make the template parameters of the specified tuple-like type, "unique" (remove the duplicates)
    template <typename T>
    using unique_parameters = unique_types<T>::type;

} // namespace details

export namespace fs8 {


    /// Get the variable names from mods
    constexpr struct [[nodiscard]] get_variables_tag {
        static constexpr bool is_tag = true;
    } get_variables;

    template <typename ModT>
    concept has_variables = requires(std::remove_cvref_t<ModT> mod) {
        typename std::remove_cvref_t<ModT>::value_type;
        mod[get_variables];
        // mod[name];
        mod.value();
    };

    /**
     * Calculate the number of variables that the input mods will provide
     */
    template <typename... ModsT>
    struct variable_size {
        static constexpr std::size_t value = (variable_size<ModsT>::value + ...);
    };

    template <typename ModT>
        requires requires(ModT mod) { mod[get_variables]; }
    struct variable_size<ModT> {
        static constexpr std::size_t value = std::tuple_size_v<decltype(std::declval<ModT>()[get_variables])>;
    };

    template <typename ModT>
    struct variable_size<ModT> {
        static constexpr std::size_t value = 0;
    };

    // End condition
    template <>
    struct variable_size<> {
        static constexpr std::size_t value = 0U;
    };

    template <typename... ModsT>
    constexpr std::size_t variable_size_v = variable_size<ModsT...>::value;

    /**
     * Get a tuple of types of variables
     */
    template <template <typename...> typename, typename, typename...>
    struct variable_types {};

    template <template <typename...> typename Templ, typename T, typename ModT, typename... ModsT>
    struct variable_types<Templ, T, ModT, ModsT...> : variable_types<Templ, T, ModsT...> {};

    template <template <typename...> typename Templ, typename... T, typename ModT, typename... ModsT>
        requires(has_variables<ModT>)
    struct variable_types<Templ, Templ<T...>, ModT, ModsT...>
      : variable_types<Templ, Templ<T..., typename std::remove_cvref_t<ModT>::value_type>, ModsT...> {};

    // End condition
    template <template <typename...> typename Templ, typename... T>
    struct variable_types<Templ, Templ<T...>> {
        using type = details::unique_parameters<Templ<T...>>;
    };

    template <template <typename...> typename Templ, typename... ModsT>
    using variable_types_t = variable_types<Templ, Templ<>, ModsT...>::type;

    constexpr std::size_t variable_not_found = std::string_view::npos;

    // template <std::size_t N>
    // [[nodiscard]] consteval bool contains_hash(std::array<std::string_view, N> const arr, std::uint32_t hash) noexcept {
    //     for (auto const str : arr) {
    //         if (ci_hash(str) == hash) {
    //             return true;
    //         }
    //     }
    //     return false;
    // }
    //
    // /**
    //  * Find the variable's index
    //  */
    // template <std::size_t, std::uint32_t, typename...>
    // struct variable_index {};
    //
    // template <std::size_t Index, std::uint32_t Hash, typename ModT, typename... ModsT>
    // struct variable_index<Index, Hash, ModT, ModsT...> {
    //     static constexpr std::size_t value = variable_index<Index + 1U, Hash, ModsT...>::value;
    // };
    //
    // template <std::size_t Index, std::uint32_t Hash, typename ModT, typename... ModsT>
    //     requires(has_variables<ModT> && contains_hash(std::declval<ModT>().operator[](get_variables), Hash))
    // struct variable_index<Index, Hash, ModT, ModsT...> {
    //     static constexpr std::size_t value = Index;
    // };
    //
    // // End condition
    // template <std::size_t Index, std::uint32_t Hash>
    // struct variable_index<Index, Hash> {
    //     static constexpr std::size_t value = variable_not_found;
    // };
    //
    // template <std::uint32_t Hash, typename... ModsT>
    // constexpr std::size_t variable_index_v = variable_index<0U, Hash, ModsT...>::value;

    /**
     * This describes the variable
     * This is used in a hash-map like table
     */
    struct [[nodiscard]] variable_pointer {
        std::string_view name; // variable name
        std::uint32_t    hash; // hash of the variable name
        std::uint32_t    index;
    };

    /// Extract variables
    /// @returns array<variable_pointer, N>
    [[nodiscard]] consteval auto extract_variables(auto& tup) noexcept {
        return std::apply(
          []<typename... ModsT>(ModsT const&... mods) {
              std::array<variable_pointer, variable_size_v<ModsT...>> values;
              std::size_t                                             index = 0;
              (
                [&]<typename ModT>(ModT const& mod) {
                    if constexpr (has_variables<ModT>) {
                        std::size_t sub_index = 0;
                        for (auto const variable_name : mod[get_variables]) {
                            values.at(index + sub_index) =
                              variable_pointer{.name  = variable_name,
                                               .hash  = ci_hash(variable_name),
                                               .index = static_cast<std::uint32_t>(index)};
                            ++sub_index;
                        }
                        ++index;
                    }
                }(mods),
                ...);
              return values;
          },
          tup);
    }

    template <std::size_t N>
    [[nodiscard]] constexpr std::size_t find_variable(std::string_view const name, std::array<variable_pointer, N> variables) noexcept {
        auto const hid = ci_hash(name);

        // probe over table entries, hash-first to avoid expensive compares
        auto const it = std::ranges::lower_bound(variables, hid, {}, [](auto const& entry) {
            return entry.hash;
        });
        if (it == std::end(variables)) [[unlikely]] {
            return variable_not_found;
        }
        auto const& [var_name, _, index] = *it;
        if (!iequals(var_name, name)) [[unlikely]] {
            return variable_not_found;
        }
        return index;
    }

    template <std::size_t N, typename TupT>
    [[nodiscard]] constexpr auto find_variable(std::string_view const name, std::array<variable_pointer, N> variables, TupT&& tup) {
        auto const index = find_variable(name, variables);
        if (index == variable_not_found) {
            throw std::invalid_argument("Variable not found");
        }
        return std::apply(
          [&]<typename... T>(T const&... funcs) constexpr noexcept {
              using res_type = variable_types_t<std::variant, std::monostate, std::conditional_t<std::is_const_v<TupT>, T const&, T&>...>;
              res_type                     res;
              [[maybe_unused]] std::size_t cur_i = 0;

              std::ignore = ((cur_i++ == index && (res = funcs.value(), false)), ...);
              return res;
          },
          tup);
    }

} // namespace fs8
