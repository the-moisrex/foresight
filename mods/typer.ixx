// Created by moisrex on 10/11/25.

module;
#include <string_view>
#include <vector>
export module foresight.mods.typer;
import foresight.mods.context;
import foresight.mods.event;
import foresight.lib.xkb.how2type;

namespace foresight {

    export template <typename CharT>
    [[nodiscard]] constexpr std::basic_string_view<CharT> to_string(
      std::basic_string_view<CharT> str) noexcept {
        return str;
    }

    export template <typename Func>
        requires(std::invocable<Func>)
    constexpr auto to_string(Func&& func) {
        return std::forward<Func>(func)();
    }

    /// Emit the events in the string
    void emit(std::u32string_view str, user_event_callback);
    void emit(std::string_view str, user_event_callback);

    /**
     * This struct will help you emit events corresponding to a string
     */
    export template <typename StrGetter = std::u32string_view>
    struct [[nodiscard]] basic_type_string {
      private:
        // we ust optional to make `constexpr` possible
        [[no_unique_address]] StrGetter event_getter;

      public:
        template <typename Getter>
            requires(std::convertible_to<Getter, StrGetter>)
        explicit constexpr basic_type_string(Getter&& getter) : event_getter{std::forward<Getter>(getter)} {}

        constexpr basic_type_string()                                        = default;
        constexpr basic_type_string(basic_type_string const&)                = default;
        constexpr basic_type_string(basic_type_string&&) noexcept            = default;
        constexpr basic_type_string& operator=(basic_type_string const&)     = default;
        constexpr basic_type_string& operator=(basic_type_string&&) noexcept = default;
        constexpr ~basic_type_string()                                       = default;

        consteval auto operator()(std::u32string_view const str) const noexcept {
            return basic_type_string<std::u32string_view>{str};
        }

        consteval auto operator()(std::u8string_view const str) const noexcept {
            return basic_type_string<std::u8string_view>{str};
        }

        consteval auto operator()(std::string_view const str) const noexcept {
            return basic_type_string<std::string_view>{str};
        }

        template <typename T>
            requires(!std::is_array_v<std::remove_cvref_t<T>>) // no strings
        consteval auto operator()(T&& getter) const noexcept {
            return basic_type_string<std::remove_cvref_t<T>>{std::forward<T>(getter)};
        }

        void operator()(Context auto& ctx) noexcept {
            auto const str = to_string(event_getter);
            emit(str, [&](user_event const& event) noexcept {
                std::ignore = ctx.fork_emit(event_type{event});
            });
        }
    };

    export constexpr basic_type_string<> type_string;
} // namespace foresight
