// Created by moisrex on 8/29/26.

module;
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <variant>
export module fs8.mods:output_selector;
import fs8.context;
import fs8.cli;
import fs8.event;
import fs8.traits;
import :inout;
import :uinput;
import :live_view;

namespace fs8 {

    namespace detail {

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
              [&](auto& out) -> context_action {
                  return out.emit(event) ? context_action::next : context_action::drop_event;
              },
              outputs_);
        }

        /// Forward start_tag to the selected output if it accepts (CtxT&, special_event).
        template <typename CtxT>
        context_action operator()(CtxT& ctx, special_event const& tag) noexcept {
            if (tag.code != start.code) {
                return context_action::drop_event;
            }
            return std::visit(
              [&](auto& out) -> context_action {
                  if constexpr (requires { out(ctx, start); }) {
                      return out(ctx, start);
                  } else {
                      return context_action::next;
                  }
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

    /// A reusable flag group for output selection.
    ///
    /// Provides the `--output`/`-o` flag definition and knows how to apply
    /// it to an `output_selector`.  Register the flag with `basic_arguments`
    /// via `add_flags()`, then call `configure()` after parsing.
    ///
    /// Usage:
    /// ```cpp
    /// static constexpr auto args =
    ///   fs8::arguments["Mouse"]
    ///     .positional("mouse_device")
    ///     .add_flags(fs8::output_flags);
    ///
    /// auto const parsed = args(argc, argv);
    /// parsed.exit_if_needed();
    /// fs8::output_flags.configure(output, parsed);
    /// ```
    struct [[nodiscard]] output_flag_group {
        consteval explicit output_flag_group(std::uint8_t const sel = 0) noexcept : default_selected_(sel) {}

        consteval output_flag_group operator[](std::uint8_t const sel) const noexcept {
            return output_flag_group{sel};
        }

        consteval output_flag_group operator[](std::string_view const name) const noexcept {
            return output_flag_group{index_of(name)};
        }

        /// The `--output` flag descriptor for registration with `basic_arguments`.
        [[nodiscard]] consteval basic_flag flag() const noexcept {
            return flag_;
        }

        /// Apply the parsed `--output` value to `sel`.
        constexpr void configure(output_selector& sel, parsed_args const& args) const noexcept {
            if (auto const val = args.flag_value("--output"); val.has_value()) {
                sel.set_selected(index_of(*val));
            } else {
                sel.set_selected(default_selected_);
            }
        }

        /// Satisfy the range concept so `add_flags(output_flags)` works.
        [[nodiscard]] constexpr basic_flag const* begin() const noexcept {
            return &flag_;
        }

        [[nodiscard]] constexpr basic_flag const* end() const noexcept {
            return &flag_ + 1;
        }

      private:
        std::uint8_t default_selected_ = 0;

        static constexpr basic_flag flag_{
          .name        = "--output",
          .alias       = "-o",
          .help        = "Output: stdout, uinput, evtest, live-view (default: stdout).",
          .takes_value = true,
        };

        [[nodiscard]] static constexpr std::uint8_t index_of(std::string_view const name) noexcept {
            if (name == "stdout") {
                return 0;
            }
            if (name == "uinput") {
                return 1;
            }
            if (name == "evtest") {
                return 2;
            }
            if (name == "live-view") {
                return 3;
            }
            return 0;
        }
    };

    /// Default output flag group (stdout, index 0).
    export inline constexpr output_flag_group output_flags{};

} // namespace fs8
