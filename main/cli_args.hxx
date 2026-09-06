#pragma once

// Internal header for the foresight CLI — shared types and declarations.
// This is NOT a module; it is #included by non-module translation units.
//
// This header includes all its own standard library dependencies.
// It must be included BEFORE any module import directives to avoid
// GCC's global-module-fragment header conflicts.

#include <csignal>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// Holds all the user options for everything situation that this software can handle
struct options {
    enum struct action_type : std::uint8_t {
        none = 0,
        help,
        intercept,
        redirect,
        systemd,
        list_devices,
        how_to_type,
        new_app,
        matches,
        evtest,
        live,
        version,
        capture,
        replay,
    } action = action_type::none;

    /// intercept/redirect/capture query strings (converted to owned_query at use)
    std::vector<std::string> queries;

    /// matches patterns
    std::vector<std::string_view> patterns;

    /// Echo the triggering events for `matches`
    bool echo_events = false;

    /// Grab the device (intercept/evtest)
    bool grab = false;

    /// Capture output format: "binary" or "evtest"
    std::string_view capture_format = "binary";

    /// Capture naming strategy: "daily", "hourly", "weekly", "monthly", "uptime", "system-uptime", "single-file", "manual"
    std::string_view capture_naming = "daily";

    /// Custom capture filename (used with --naming manual).
    std::string_view capture_name;

    /// Replay file path
    std::string_view replay_file;

    /// All args
    std::span<char const* const> args;
};

void set_action(options& opt, options::action_type const inp_action);

options parse_arguments(std::span<char const* const> const argv);

// ---- Signals ----

namespace signals {
    // NOLINTBEGIN(*-avoid-non-const-global-variables)
    extern std::sig_atomic_t volatile sig;
    extern std::vector<std::move_only_function<void(std::sig_atomic_t) const>> actions;
    // NOLINTEND(*-avoid-non-const-global-variables)

    template <typename T>
    void register_stop_signal(T& obj) {
        actions.emplace_back([&obj](std::sig_atomic_t const cur_sig) {
            switch (cur_sig) {
                case SIGINT:
                case SIGTERM: obj.stop(); break;
                default: break;
            }
        });
    }
} // namespace signals

void handle_signals(int signal);

// ---- Help / display ----

void print_version();
void print_help();
void print_new_help();
void print_input_devices_table();

// ---- Actions ----

int create_new_app(std::span<char const* const> const args);
int run_matches(std::span<std::string_view const> const patterns, bool echo_events);
int run_evtest(options const& opts);
int run_live(options const& opts);
int run_capture_action(options const& opts);
int run_replay_action(options const& opts);
