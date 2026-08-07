// Created by moisrex on 8/7/26.

export module fs8.traits;

export namespace fs8 {
    struct [[nodiscard]] consteval_copyable {
        consteval consteval_copyable() noexcept                                     = default;
        consteval consteval_copyable(consteval_copyable const&) noexcept            = default;
        constexpr consteval_copyable(consteval_copyable&&) noexcept                 = default;
        constexpr consteval_copyable& operator=(consteval_copyable&&) noexcept      = default;
        consteval consteval_copyable& operator=(consteval_copyable const&) noexcept = default;
        constexpr ~consteval_copyable() noexcept                                    = default;
    };
} // namespace fs8
