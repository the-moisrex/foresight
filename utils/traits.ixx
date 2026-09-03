// Created by moisrex on 8/7/26.

module;
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string_view>
export module fs8.traits;

namespace fs8::detail {

    constexpr std::string_view trim(std::string_view s) noexcept {
        constexpr std::string_view ws{" \t"};
        auto const                 b = s.find_first_not_of(ws);
        if (b == std::string_view::npos) {
            return {};
        }
        auto const e = s.find_last_not_of(ws);
        return s.substr(b, e - b + 1);
    }

    /// Slice the type token, keeping nested <...> and (...).
    constexpr std::string_view type_token(std::string_view s) noexcept {
        int depth = 0;
        for (std::size_t i = 0; i < s.size(); ++i) {
            char const c = s[i];
            if (c == '<' || c == '(') {
                ++depth;
            } else if (c == '>' || c == ')') {
                if (depth == 0) {
                    return trim(s.substr(0, i));
                }
                --depth;
            } else if (depth == 0 && (c == ']' || c == ',' || c == ';')) {
                return trim(s.substr(0, i));
            }
        }
        return trim(s);
    }

    constexpr std::string_view extract_type(std::string_view pretty) noexcept {
        // GCC / Clang: "... [T = Type]" or "... [with T = Type; ...]"
        if (auto const p = pretty.find("T = "); p != std::string_view::npos) {
            return type_token(pretty.substr(p + 4));
        }
        return {};
    }

    constexpr std::string_view unqualified(std::string_view name) noexcept {
        auto const args = name.find('<');
        auto const head = name.substr(0, args == std::string_view::npos ? name.size() : args);
        auto const ns   = head.rfind("::");
        return ns == std::string_view::npos ? name : name.substr(ns + 2);
    }

    constexpr std::string_view short_name(std::string_view pretty) noexcept {
        auto const type = extract_type(pretty);
        return type.empty() ? pretty : unqualified(type);
    }

} // namespace fs8::detail

export namespace fs8 {

    struct [[nodiscard]] consteval_copyable {
        // A user-provided body (not `= default`) so the definition is emitted
        // into the BMI and usable through inherited ctors by importing modules;
        // clang 22 leaves an unused defaulted ctor out of the pcm, making it
        // appear undefined when a constexpr global (e.g. `and_op<>`/`router`)
        // default-initializes it via `using base::base`.
        constexpr consteval_copyable() noexcept {}

        // Compile-time copy OK, runtime copy rejected.
        // unfortunately, marking copy-ctor consteval doesn't prevent compilers from copying it at runtime.
        constexpr consteval_copyable(consteval_copyable const&) noexcept {
            if !consteval {
                // don't use std::println since that one throws
                std::fprintf(stderr, "You're trying to copy an object that's copyable at compile time only.");
                std::abort();
            }
        }

        constexpr consteval_copyable(consteval_copyable&&) noexcept = default;

        constexpr consteval_copyable& operator=(consteval_copyable&&) noexcept = default;

        constexpr consteval_copyable& operator=(consteval_copyable const&) noexcept {
            if !consteval {
                std::fprintf(stderr, "You're trying to copy an object that's copyable at compile time only.");
                std::abort();
            }
            return *this;
        }

        constexpr ~consteval_copyable() noexcept = default;
    };

    /// Extract a short readable name from __PRETTY_FUNCTION__.
    /// Turns "...pretty_type_name() [T = fs8::basic_abs2rel]" into "basic_abs2rel".
    template <typename T>
    consteval std::string_view pretty_type_name() noexcept {
        constexpr std::string_view raw = __PRETTY_FUNCTION__;
        return detail::short_name(raw);
    }

} // namespace fs8
