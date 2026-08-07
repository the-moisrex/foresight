// Created by moisrex on 8/7/26.

module;
#include <cstdio>
#include <cstdlib>
export module fs8.traits;

export namespace fs8 {

    struct [[nodiscard]] consteval_copyable {
        // Still consteval so the type can only be default-constructed
        // in a constant-expression context.
        consteval consteval_copyable() noexcept = default;

        // Compile-time copy OK, runtime copy rejected.
        // unfortunately, marking copy-ctor consteval doesn't prevent compilers copying it at runtime.
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
            if consteval {
                std::fprintf(stderr, "You're trying to copy an object that's copyable at compile time only.");
                std::abort();
            }
            return *this;
        }

        constexpr ~consteval_copyable() noexcept = default;
    };

} // namespace fs8
