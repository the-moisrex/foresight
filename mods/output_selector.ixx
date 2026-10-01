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

        /// Forward start_tag to the selected output if it accepts (CtxT&, control_event).
        template <typename CtxT>
        context_action operator()(CtxT& ctx, control_event const& tag) noexcept {
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
    ///   3 — basic_event_line_output<> (event-line text format)
    export using output_selector = basic_output_selector<basic_std_output, basic_uinput, basic_evtest_output<>, basic_event_line_output<>>;

    export constexpr output_selector output{};

    static_assert(OutputModifier<output_selector>, "Must be an output modifier.");

    /// A reusable flag group for output selection.
    ///
    /// Provides the `--output`/`-o` flag definition and knows how to apply
    /// it to an `output_selector`.  Register the flag with `arguments`
    /// via `add_flags()`, then call `configure()` after parsing.  The
    /// flag's help text advertises the group's default selection, so
    /// `add_flags(output_flags["uinput"])` documents uinput as the default.
    ///
    /// Usage:
    /// ```cpp
    /// static constexpr auto args =
    ///   fs8::arguments["Mouse"]
    ///     .positional("mouse_device")
    ///     .add_flags(fs8::output_flags["uinput"]);
    ///
    /// auto const parsed = args(argc, argv);
    /// parsed.exit_if_needed();
    /// fs8::output_flags["uinput"].configure(pipeline.mod(output), parsed);
    /// ```
    struct [[nodiscard]] output_flag_group {
        consteval explicit output_flag_group(std::uint8_t const sel = 0) noexcept : default_selected_(sel) {
            flag_ = flag{.name = "--output", .alias = "-o", .help = help_for(sel), .takes_value = true};
        }

        consteval output_flag_group operator[](std::uint8_t const sel) const noexcept {
            return output_flag_group{sel};
        }

        consteval output_flag_group operator[](std::string_view const name) const noexcept {
            return output_flag_group{index_of(name)};
        }

        /// The `--output` flag descriptor for registration with `arguments`.
        [[nodiscard]] consteval flag get_flag() const noexcept {
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
        [[nodiscard]] constexpr flag const* begin() const noexcept {
            return &flag_;
        }

        [[nodiscard]] constexpr flag const* end() const noexcept {
            return &flag_ + 1;
        }

      private:
        std::uint8_t default_selected_ = 0;
        flag         flag_{};

        /// One help line per default selection; static storage so the
        /// `string_view` inside `flag_` never dangles.
        static constexpr std::string_view help_texts_[] = {
          "Output: stdout, uinput, evtest, live-view (default: stdout).",
          "Output: stdout, uinput, evtest, live-view (default: uinput).",
          "Output: stdout, uinput, evtest, live-view (default: evtest).",
          "Output: stdout, uinput, evtest, live-view (default: live-view).",
        };

        [[nodiscard]] static consteval std::string_view help_for(std::uint8_t const sel) noexcept {
            return help_texts_[sel < sizeof(help_texts_) / sizeof(help_texts_[0]) ? sel : 0U];
        }

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
