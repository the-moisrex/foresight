// Created by moisrex on 10/11/25.

module;
#include <string_view>
#include <vector>
export module fs8.mods:typer;
import fs8.context;
import fs8.event;
import fs8.lib.xkb.how2type;
import fs8.traits;

namespace fs8 {

    export template <typename CharT>
    [[nodiscard]] constexpr std::basic_string_view<CharT> to_string(std::basic_string_view<CharT> str) noexcept {
        return str;
    }

    export template <typename Func>
        requires(std::invocable<Func>)
    constexpr auto to_string(Func&& func) {
        return std::forward<Func>(func)();
    }

    /// Emit the events in the string
    export void emit_str(std::u32string_view str, user_event_callback);
    export void emit_str(std::u8string_view str, user_event_callback);
    export void emit_str(std::string_view str, user_event_callback);

    /**
     * This struct will help you emit events corresponding to a string
     */
    export template <typename StrGetter = std::u32string_view>
    struct [[nodiscard]] basic_type_string : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        // we ust optional to make `constexpr` possible
        [[no_unique_address]] StrGetter event_getter;

      public:
        template <typename Getter>
            requires(std::convertible_to<Getter, StrGetter>)
        explicit constexpr basic_type_string(Getter&& getter) : event_getter{std::forward<Getter>(getter)} {}

        consteval auto operator[](std::u32string_view const str) const noexcept {
            return basic_type_string<std::u32string_view>{str};
        }

        consteval auto operator[](std::u8string_view const str) const noexcept {
            return basic_type_string<std::u8string_view>{str};
        }

        consteval auto operator[](std::string_view const str) const noexcept {
            return basic_type_string<std::string_view>{str};
        }

        template <typename T>
            requires(!std::is_array_v<std::remove_cvref_t<T>> && !detail::is_tag_type<T>) // no strings, no tags
        consteval auto operator[](T&& getter) const noexcept {
            return basic_type_string<std::remove_cvref_t<T>>{std::forward<T>(getter)};
        }

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
            requires(!std::is_array_v<std::remove_cvref_t<T>>
                     && !detail::is_tag_type<T>
                     && !Context<std::remove_cvref_t<T>>) // no strings, no tags, no pipeline contexts
        consteval auto operator()(T&& getter) const noexcept {
            return basic_type_string<std::remove_cvref_t<T>>{std::forward<T>(getter)};
        }

        void operator()(Context auto& ctx) noexcept try {
            auto const str = to_string(event_getter);
            // NOTE: fork_emit re-sends each synthesized event through the mods that come AFTER this one
            // (never back into `typed`/`search_engine`, which sit earlier in the pipeline), so an emitted
            // string can't re-trigger the pattern that produced it as long as emitters stay downstream of matchers.
            emit_str(str, [&](user_event const& event) noexcept {
                std::ignore = ctx.fork_emit(event_type{event});
            });
        } catch (...) {
            // drop the emission on failure
        }
    };

    export constexpr basic_type_string<> type_string;
} // namespace fs8
