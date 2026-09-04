module;
#include <array>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <print>
#include <string_view>
#include <utility>
export module fs8.cli;

export namespace fs8 {

    /// Describes a single command-line flag.
    struct [[nodiscard]] flag {
        std::string_view name;  // e.g. "--grab"
        std::string_view alias; // e.g. "-g"; may be empty
        std::string_view help;  // one-line description
        bool             takes_value = false;
    };

    /// Result of parsing `argc`/`argv` through `arguments`.
    ///
    /// The positional arguments are copied into inline storage (as `argv`
    /// pointers, which outlive `main`) so flags can be interspersed freely.
    /// The object itself is a range over those positionals, so it can be piped
    /// through query tags directly, e.g. `parsed | grab | required`.
    struct [[nodiscard]] parsed_args {
      private:
        static constexpr std::size_t max_positionals = 16;
        static constexpr std::size_t max_flags       = 8;
        static constexpr std::size_t max_names       = 16;

        using positional_array = std::array<char const*, max_positionals>;

      public:
        using iterator       = positional_array::const_pointer;
        using const_iterator = iterator;

        /// Inline storage for the positional arguments.
        positional_array storage{};
        std::size_t      positional_count = 0;

        /// Copies of the registered flag definitions (for help + lookup).
        std::array<flag, max_flags>  flags{};
        std::size_t                        flag_count = 0;
        std::array<bool, max_flags>        flag_seen{};
        std::array<char const*, max_flags> flag_values{};

        /// Placeholder names for the auto-generated help.
        std::array<std::string_view, max_names> names{};
        std::size_t                             names_count = 0;

        std::string_view program_name{};
        std::string_view help_text{};
        bool             help_requested    = false;
        bool             version_requested = false;

        [[nodiscard]] constexpr iterator begin() const noexcept {
            return storage.data();
        }

        [[nodiscard]] constexpr iterator end() const noexcept {
            return storage.data() + positional_count;
        }

        [[nodiscard]] constexpr bool empty() const noexcept {
            return positional_count == 0;
        }

        [[nodiscard]] constexpr std::size_t size() const noexcept {
            return positional_count;
        }

        /// True when `-h`/`--help` was passed.
        [[nodiscard]] bool help() const noexcept {
            return help_requested;
        }

        /// True when `-v`/`--version` was passed.
        [[nodiscard]] bool version() const noexcept {
            return version_requested;
        }

        /// If help or version was requested, print it and exit.
        void exit_if_needed() const noexcept {
            if (help_requested) {
                print_help();
                std::exit(0);
            }
            if (version_requested) {
                print_version();
                std::exit(0);
            }
        }

        /// Print the version string.
        void print_version() const noexcept {
#ifdef FORESIGHT_VERSION
            std::println("{}", FORESIGHT_VERSION);
#endif
        }

        /// True when the flag with the given name (or alias) was passed.
        [[nodiscard]] bool has_flag(std::string_view const name) const noexcept {
            for (std::size_t i = 0; i < flag_count; ++i) {
                if (flags[i].name == name || flags[i].alias == name) {
                    return flag_seen[i];
                }
            }
            return false;
        }

        /// The value of a value-taking flag, if it was passed.
        [[nodiscard]] std::optional<std::string_view> flag_value(std::string_view const name) const noexcept {
            for (std::size_t i = 0; i < flag_count; ++i) {
                if (flags[i].name == name || flags[i].alias == name) {
                    if (!flag_seen[i] || flag_values[i] == nullptr) {
                        return std::nullopt;
                    }
                    return std::string_view{flag_values[i]};
                }
            }
            return std::nullopt;
        }

        /// Print the help text: a configured custom text if any, otherwise a
        /// generated usage derived from the positional names and flags.
        void print_help() const noexcept {
            if (!help_text.empty()) {
                std::println("{}", help_text);
                return;
            }
            std::println("Usage: {} [options] [positional...]", program_name);
            if (names_count > 0) {
                std::println();
                std::println("Positionals:");
                for (std::size_t i = 0; i < names_count; ++i) {
                    std::println("  {}", names[i]);
                }
            }
            std::println();
            std::println("Options:");
            std::println("  -h | --help      Print this help.");
            std::println("  -v | --version   Print version.");
            for (std::size_t i = 0; i < flag_count; ++i) {
                flag const& f = flags[i];
                if (f.alias.empty()) {
                    std::println("  {}  {}", f.name, f.help);
                } else {
                    std::println("  {} | {}  {}", f.alias, f.name, f.help);
                }
            }
        }
    };

    /// Command-line configuration for an app: default positionals, positional
    /// placeholder names (for help), extra flags, and an optional custom help
    /// text. Configure it through method chaining (e.g. `arguments["x"].help("...").add_flag(...)`)
    /// and parse with `operator()(argc, argv)`.
    template <std::size_t DefaultsN>
    struct [[nodiscard]] basic_arguments {
      private:
        static constexpr std::size_t max_positionals = 16;
        static constexpr std::size_t max_flags       = 8;
        static constexpr std::size_t max_names       = 16;

        std::array<char const*, DefaultsN + 1>  defaults_{};
        std::array<flag, max_flags>       flags_{};
        std::size_t                             flags_count = 0;
        std::array<std::string_view, max_names> names_{};
        std::size_t                             names_count = 0;
        std::string_view                        help_text_{};

      public:
        /// Default constructor for zero defaults.
        consteval basic_arguments() noexcept = default;

        template <typename... Args>
            requires(sizeof...(Args) == DefaultsN && DefaultsN > 0)
        explicit consteval basic_arguments(Args&&... defaults) noexcept : defaults_{"", std::forward<Args>(defaults)...} {}

        /// Set the default positional values, used when no arguments are given.
        template <typename... Args>
        consteval auto operator[](Args&&... defaults) const noexcept {
            return basic_arguments<sizeof...(Args)>{std::forward<Args>(defaults)...};
        }

        /// Add placeholder names for the positional arguments in the help text.
        template <typename... Names>
        constexpr basic_arguments positional(Names... names) const noexcept {
            basic_arguments out = *this;
            for (std::string_view const name : {names...}) {
                if (out.names_count < max_names) [[likely]] {
                    out.names_[out.names_count++] = name;
                }
            }
            return out;
        }

        /// Set a custom help text; when empty, help is auto-generated.
        constexpr basic_arguments help(std::string_view const text) const noexcept {
            basic_arguments out = *this;
            out.help_text_      = text;
            return out;
        }

        /// Register an extra flag.
        constexpr basic_arguments add_flag(flag const fl) const noexcept {
            basic_arguments out = *this;
            if (out.flags_count < max_flags) [[likely]] {
                out.flags_[out.flags_count++] = fl;
            }
            return out;
        }

        /// Register multiple flags from a range (e.g. `std::array<flag, N>`).
        template <typename Range>
            requires requires(Range const& r) {
                std::begin(r);
                std::end(r);
            }
        constexpr basic_arguments add_flags(Range const& flags) const noexcept {
            basic_arguments out = *this;
            for (flag const& fl : flags) {
                if (out.flags_count < max_flags) [[likely]] {
                    out.flags_[out.flags_count++] = fl;
                }
            }
            return out;
        }

        /// Parse `argc`/`argv` into positional arguments and flag state.
        [[nodiscard]] parsed_args operator()(int const argc, char const* const* argv) const& noexcept {
            parsed_args out;

            out.flag_count  = flags_count;
            out.names_count = names_count;
            for (std::size_t i = 0; i < names_count; ++i) {
                out.names[i] = names_[i];
            }
            for (std::size_t i = 0; i < flags_count; ++i) {
                out.flags[i] = flags_[i];
            }
            out.help_text = help_text_;
            if (argc >= 1) {
                out.program_name = argv[0];
            }

            char const* const* const beg   = argc <= 1 ? defaults_.data() : argv;
            std::size_t const        count = argc <= 1 ? defaults_.size() : static_cast<std::size_t>(argc);

            for (std::size_t i = 1; i < count; ++i) {
                std::string_view const cur{beg[i]};
                if (cur == "-h" || cur == "--help") {
                    out.help_requested = true;
                    continue;
                }
                if (cur == "-v" || cur == "--version") {
                    out.version_requested = true;
                    continue;
                }
                bool matched = false;
                for (std::size_t f = 0; f < flags_count; ++f) {
                    flag const& fl = flags_[f];
                    if (cur == fl.name || (!fl.alias.empty() && cur == fl.alias)) {
                        out.flag_seen[f] = true;
                        if (fl.takes_value && i + 1 < count) [[likely]] {
                            out.flag_values[f] = beg[++i];
                        }
                        matched = true;
                        break;
                    }
                }
                if (matched) {
                    continue;
                }
                if (out.positional_count < max_positionals) [[likely]] {
                    out.storage[out.positional_count++] = beg[i];
                }
            }
            return out;
        }

        // Prevent dangling references to defaults_
        auto operator()(int argc, char const* const* argv) const&& noexcept = delete;
    };

    /// The default argument configuration: no defaults, no extra flags.
    inline constexpr basic_arguments<0> arguments{};

} // namespace fs8
