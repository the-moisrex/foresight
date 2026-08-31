// Created by moisrex on 8/29/26.

module;
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <variant>
export module fs8.mods:output_selector;
import fs8.context;
import fs8.event;
import fs8.traits;
import :inout;
import :uinput;
import :live_view;

namespace fs8 {

    namespace detail {

        template <typename Out, typename CtxT>
        context_action start_output(Out& out, CtxT& ctx) noexcept {
            if constexpr (requires { out(ctx, start); }) {
                return out(ctx, start);
            } else {
                return context_action::next;
            }
        }

        template <typename... Ts, std::size_t... Is>
        constexpr void emplace_at(std::variant<Ts...>& var, std::size_t const idx, std::index_sequence<Is...>) noexcept {
            // NOLINTNEXTLINE(*-unused-result)
            ([&] {
                if (idx == Is) {
                    var.template emplace<Is>();
                    return true;
                }
                return false;
            }()
             || ...);
        }

    } // namespace detail

    export template <OutputModifier... Outputs>
    struct [[nodiscard]] basic_output_selector : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        static_assert(sizeof...(Outputs) <= std::numeric_limits<std::uint8_t>::max(), "Too many output types.");

      private:
        std::variant<Outputs...> outputs_{};

      public:
        [[nodiscard]] constexpr std::uint8_t selected() const noexcept {
            return static_cast<std::uint8_t>(outputs_.index());
        }

        constexpr void set_selected(std::size_t const index) noexcept {
            detail::emplace_at(outputs_, index, std::index_sequence_for<Outputs...>{});
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
            return std::visit(
              [&](auto& out) -> bool {
                  return out.emit(event);
              },
              outputs_);
        }

        // NOLINTNEXTLINE(*-use-nodiscard)
        context_action operator()(event_type& event) noexcept {
            return std::visit(
              [&]<typename T>(T& out) -> context_action {
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
              },
              outputs_);
        }

        /// Forward start_tag to the selected output if it accepts (CtxT&, start_tag).
        template <typename CtxT>
        context_action operator()(CtxT& ctx, special_event const& tag) noexcept {
            if (tag.code != special_start.code) {
                return context_action::drop_event;
            }
            return std::visit(
              [&](auto& out) -> context_action {
                  return detail::start_output(out, ctx);
              },
              outputs_);
        }
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
