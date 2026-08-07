// Created by moisrex on 8/7/26.

module;
#include <type_traits>
#include <utility>
export module fs8.pimpl;
import fs8.traits;
import fs8.nullable_indirect;

export namespace fs8 {


    template <typename>
    struct [[nodiscard]] pimpl_idiom : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        struct [[nodiscard]] impl;

      protected:
        template <typename... Args>
        constexpr void init_impl(Args&&... args) noexcept(std::is_nothrow_default_constructible_v<impl>) {
            pimpl = nullable_indirect<impl>::make(std::forward<Args>(args)...);
        }

        nullable_indirect<impl> pimpl{};
    };

} // namespace fs8
