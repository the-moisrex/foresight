// Created by moisrex on 8/29/26.

module;
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
export module fs8.mods:output_selector;
import fs8.context;
import fs8.event;
import fs8.traits;
import :inout;
import :uinput;
import :live_view;

namespace fs8 {

    namespace detail {
        template <std::size_t I>
        struct os_visit_impl {
            template <typename T, typename F>
            static constexpr decltype(auto) visit(T& tup, std::size_t idx, F&& fun) noexcept {
                if (idx == I - 1) {
                    return std::forward<F>(fun)(std::get<I - 1>(tup));
                }
                return os_visit_impl<I - 1>::visit(tup, idx, fun);
            }
        };

        template <>
        struct os_visit_impl<0> {
            template <typename T, typename F>
            static constexpr decltype(auto)
            visit([[maybe_unused]] T& tup, [[maybe_unused]] std::size_t, [[maybe_unused]] F&& fun) noexcept {
                assert(false);
                return std::forward<F>(fun)(std::get<0>(tup));
            }
        };

        template <typename F, typename... Ts>
        constexpr decltype(auto) visit_at(std::tuple<Ts...>& tup, std::size_t idx, F&& fun) noexcept {
            return os_visit_impl<sizeof...(Ts)>::visit(tup, idx, std::forward<F>(fun));
        }

        template <typename Out, typename CtxT>
        context_action start_output(Out& out, CtxT& ctx) noexcept {
            if constexpr (requires { out(ctx, start); }) {
                return out(ctx, start);
            } else {
                return context_action::next;
            }
        }
    } // namespace detail

    export template <OutputModifier... Outputs>
    struct [[nodiscard]] basic_output_selector : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        static_assert(sizeof...(Outputs) <= std::numeric_limits<std::uint8_t>::max(), "Too many output types.");

      private:
        std::uint8_t           selected_ = 0;
        std::tuple<Outputs...> outputs_{};

      public:
        [[nodiscard]] constexpr std::uint8_t selected() const noexcept {
            return selected_;
        }

        constexpr void set_selected(std::uint8_t const index) noexcept {
            selected_ = index;
        }

        template <std::size_t N>
        [[nodiscard]] constexpr auto& output() noexcept {
            return std::get<N>(outputs_);
        }

        template <std::size_t N>
        [[nodiscard]] constexpr auto const& output() const noexcept {
            return std::get<N>(outputs_);
        }

        // NOLINTNEXTLINE(*-use-nodiscard)
        bool emit(event_type const& event) noexcept {
            return detail::visit_at(outputs_, selected_, [&](auto& out) -> bool {
                return out.emit(event);
            });
        }

        // NOLINTNEXTLINE(*-use-nodiscard)
        context_action operator()(event_type& event) noexcept {
            return detail::visit_at(outputs_, selected_, [&]<typename T>(T& out) -> context_action {
                using Out = std::remove_cvref_t<T>;
                if constexpr (std::is_nothrow_invocable_v<Out, event_type&>) {
                    using R = std::invoke_result_t<Out, event_type&>;
                    if constexpr (std::same_as<R, bool>) {
                        return out(event) ? context_action::next : context_action::drop_event;
                    } else {
                        return out(event);
                    }
                } else {
                    static_assert(false, "Output type must accept event_type&.");
                    return context_action::exit;
                }
            });
        }

        /// Forward start_tag to the selected output if it accepts (CtxT&, start_tag).
        template <typename CtxT>
        context_action operator()(CtxT& ctx, start_tag) noexcept {
            return detail::visit_at(outputs_, selected_, [&](auto& out) -> context_action {
                return detail::start_output(out, ctx);
            });
        }

        void operator()(auto&&, Tag auto) = delete;
        void operator()(Tag auto)         = delete;
    };

    /// Default output selector with all built-in output types.
    ///
    /// Index mapping:
    ///   0 — basic_output (raw stdout)
    ///   1 — basic_uinput (kernel virtual device)
    ///   2 — basic_evtest_output<> (evtest text format)
    ///   3 — basic_live_view_output<> (live view text format)
    export using output_selector = basic_output_selector<basic_std_output, basic_uinput, basic_evtest_output<>, basic_live_view_output<>>;

    export constexpr output_selector output{};

    static_assert(OutputModifier<output_selector>, "Must be an output modifier.");

} // namespace fs8
