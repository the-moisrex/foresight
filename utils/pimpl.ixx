// Created by moisrex on 8/7/26.

module;
#include <cassert>
#include <utility>
export module fs8.pimpl;
import fs8.traits;
import fs8.nullable_indirect;

export namespace fs8 {

    /**
     * This is a custom pimpl idiom that helps with hiding implementation details from other TUs.
     * This also a consteval-only-copyable which mods in this project must be.
     */
    template <typename>
    struct [[nodiscard]] pimpl_idiom : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        // [[nodiscard]] constexpr bool has_impl() const noexcept {
        //     return static_cast<bool>(pimpl);
        // }

        // [[nodiscard]] constexpr impl* get_impl() noexcept {
        //     return pimpl.get();
        // }

        // [[nodiscard]] constexpr impl const* get_impl() const noexcept {
        //     return pimpl.get();
        // }

      protected:
        struct [[nodiscard]] impl;

        template <typename... Args>
        constexpr void init_impl(Args&&... args) {
            assert(pimpl.get() == nullptr);
            pimpl = nullable_indirect<impl>::make(std::forward<Args>(args)...);
        }

        nullable_indirect<impl> pimpl{};
    };

} // namespace fs8
