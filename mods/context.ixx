// Created by moisrex on 6/8/25.

module;
#include <functional>
#include <type_traits>
#include <utility>
export module context;
import foresight.mods.event;

export namespace foresight {

    template <typename Func = void>
    struct context {
        static_assert(std::is_invocable_v<Func, context<>>, "Function is not invokable.");

        explicit context(event &&inp_ev) noexcept : ev{std::move(inp_ev)} {}

        void next() const noexcept(std::is_nothrow_invocable_v<Func, context>) {
            func(*this);
        }

      private:
        event                      ev;
        [[no_unique_address]] Func func;
    };

    template <>
    struct context<void> {
        explicit context(event &&inp_ev) : ev{std::move(inp_ev)} {}

        void next() const {
            func(*this);
        }

      private:
        event                                ev;
        std::function<void(context const &)> func;
    };

} // namespace foresight
