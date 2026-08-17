// Created by moisrex on 10/12/25.

module;
#include <array>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <xkbcommon/xkbcommon.h>
module fs8.lib.xkb;

using fs8::xkb::basic_state;
using fs8::xkb::context;
using fs8::xkb::keymap;

static constexpr std::size_t XKB_KEYSYM_NAME_MAX_SIZE = 28;

namespace {

    void ensure(bool const cond, std::string_view const msg) {
        if (!cond) {
            throw fs8::xkb::xkb_error(std::string(msg));
        }
    }

    /// Resolved system XKB configuration; empty fields mean "use xkbcommon defaults".
    struct system_keyboard {
        std::string rules;
        std::string model;
        std::string layout;
        std::string variant;
        std::string options;
    };

    /// Trim ASCII whitespace from both ends of a string.
    [[nodiscard]] std::string_view trim(std::string_view str) noexcept {
        while (!str.empty() && (str.front() == ' ' || str.front() == '\t' || str.front() == '\r' || str.front() == '\n')) {
            str.remove_prefix(1);
        }
        while (!str.empty() && (str.back() == ' ' || str.back() == '\t' || str.back() == '\r' || str.back() == '\n')) {
            str.remove_suffix(1);
        }
        return str;
    }

    /// Strip surrounding double quotes if present.
    [[nodiscard]] std::string_view unquote(std::string_view const str) noexcept {
        if (str.size() >= 2 && str.front() == '"' && str.back() == '"') {
            return str.substr(1, str.size() - 2);
        }
        return str;
    }

    /// Read a line-based "KEY=value" config file, calling `on_value` for each matched line.
    /// Blank lines and comments (lines starting with '#') are ignored.
    template <typename Fn>
    void read_config_file(char const* path, Fn&& on_value) {
        std::ifstream file{path};
        if (!file) {
            return;
        }
        std::string line;
        while (std::getline(file, line)) {
            std::string_view const sv = trim(line);
            if (sv.empty() || sv.front() == '#') {
                continue;
            }
            auto const eq = sv.find('=');
            if (eq == std::string_view::npos) {
                continue;
            }
            auto const key   = trim(sv.substr(0, eq));
            auto const value = unquote(trim(sv.substr(eq + 1)));
            if (!value.empty()) {
                on_value(key, value);
            }
        }
    }

    /// Determine the system's configured XKB names from env vars and config files,
    /// in priority order: environment, /etc/default/keyboard, /etc/vconsole.conf.
    system_keyboard detect_system_keyboard() {
        system_keyboard out;

        // Priority 1: explicit environment overrides (same convention as xkbcommon).
        if (char const* const v = std::getenv("XKB_DEFAULT_RULES"); v != nullptr && *v != '\0') {
            out.rules = v;
        }
        if (char const* const v = std::getenv("XKB_DEFAULT_MODEL"); v != nullptr && *v != '\0') {
            out.model = v;
        }
        if (char const* const v = std::getenv("XKB_DEFAULT_LAYOUT"); v != nullptr && *v != '\0') {
            out.layout = v;
        }
        if (char const* const v = std::getenv("XKB_DEFAULT_VARIANT"); v != nullptr && *v != '\0') {
            out.variant = v;
        }
        if (char const* const v = std::getenv("XKB_DEFAULT_OPTIONS"); v != nullptr && *v != '\0') {
            out.options = v;
        }

        // Priority 2: /etc/default/keyboard (written by systemd-localed). Only fills
        // fields that the environment left unset.
        read_config_file("/etc/default/keyboard", [&](std::string_view const key, std::string_view const value) {
            if (key == "XKBLAYOUT" && out.layout.empty()) {
                out.layout = value;
            } else if (key == "XKBVARIANT" && out.variant.empty()) {
                out.variant = value;
            } else if (key == "XKBMODEL" && out.model.empty()) {
                out.model = value;
            } else if (key == "XKBOPTIONS" && out.options.empty()) {
                out.options = value;
            }
        });

        // Priority 3: /etc/vconsole.conf. Only consulted when /etc/default/keyboard
        // didn't provide a layout.
        if (out.layout.empty()) {
            read_config_file("/etc/vconsole.conf", [&](std::string_view const key, std::string_view const value) {
                if (key == "XKBLAYOUT" && out.layout.empty()) {
                    out.layout = value;
                } else if (key == "XKBVARIANT" && out.variant.empty()) {
                    out.variant = value;
                }
            });
        }

        return out;
    }

} // namespace

context::context(xkb_context_flags const flags) : ctx{xkb_context_new(flags)} {
    ensure(ctx != nullptr, "Failed to create xkb_context");
}

context::~context() noexcept {
    if (ctx != nullptr) {
        xkb_context_unref(ctx);
    }
}

xkb_context* context::get() const noexcept {
    return ctx;
}

void context::set_log_level(xkb_log_level const level) const noexcept {
    xkb_context_set_log_level(get(), level);
}

xkb_log_level context::log_level() const noexcept {
    return xkb_context_get_log_level(get());
}

context& fs8::xkb::get_default_context() {
    static context ctx;
    return ctx;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////       KeyMap      ////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

keymap::keymap(context const& ctx, char const* rules, char const* model, char const* layout, char const* variant, char const* options) {
    xkb_rule_names names{};
    names.rules   = rules;
    names.model   = model;
    names.layout  = layout;
    names.variant = variant;
    names.options = options;
    load(ctx, &names);
}

void keymap::load(context const& ctx, xkb_rule_names const* names, xkb_keymap_format const keymap_format) {
    if (handle != nullptr) [[unlikely]] {
        xkb_keymap_unref(handle);
    }
    handle = xkb_keymap_new_from_names2(ctx.get(), names, keymap_format, XKB_KEYMAP_COMPILE_NO_FLAGS);
    ensure(handle != nullptr, "Failed to create xkb_keymap from names");
}

keymap keymap::from_string(context const& ctx, std::string_view const xml) {
    return keymap{xkb_keymap_new_from_buffer(ctx.get(), xml.data(), xml.size(), XKB_KEYMAP_FORMAT_TEXT_V2, XKB_KEYMAP_COMPILE_NO_FLAGS)};
}

keymap::keymap(xkb_keymap* km) : handle{km} {
    ensure(km != nullptr, "Failed to create xkb_keymap from string");
}

keymap::~keymap() noexcept {
    if (handle != nullptr) {
        xkb_keymap_unref(handle);
    }
}

xkb_keymap* keymap::get() const noexcept {
    return handle;
}

xkb_keycode_t keymap::min_keycode() const noexcept {
    return xkb_keymap_min_keycode(get());
}

xkb_keycode_t keymap::max_keycode() const noexcept {
    return xkb_keymap_max_keycode(get());
}

std::string keymap::as_string() const {
    char const* str = xkb_keymap_get_as_string(get(), XKB_KEYMAP_FORMAT_TEXT_V1);
    // no need to check for nullptr, std::string can handle that
    return {str};
}

keymap& fs8::xkb::get_default_keymap() {
    // Use the user's real keyboard layout(s) from the environment and system config
    // (e.g. /etc/default/keyboard) instead of always falling back to plain "us",
    // so characters from any configured layout are typable.
    static system_keyboard const sys = detect_system_keyboard();
    static keymap map{
      get_default_context(),
      sys.rules.empty() ? nullptr : sys.rules.c_str(),
      sys.model.empty() ? nullptr : sys.model.c_str(),
      sys.layout.empty() ? nullptr : sys.layout.c_str(),
      sys.variant.empty() ? nullptr : sys.variant.c_str(),
      sys.options.empty() ? nullptr : sys.options.c_str(),
    };
    return map;
}

std::string fs8::xkb::name(xkb_keysym_t const keysym) {
    std::array<char, XKB_KEYSYM_NAME_MAX_SIZE> name{};
    int const                                  ret = xkb_keysym_get_name(keysym, name.data(), name.size());
    if (ret < 0 || static_cast<size_t>(ret) >= name.size()) {
        return {};
    }
    return {name.data()};
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////       State       ////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

basic_state::basic_state(keymap const& inp_map) : handle{xkb_state_new(inp_map.get())} {
    ensure(handle != nullptr, "Cannot create xkb state with xkb_state_new");
}

void basic_state::destroy() noexcept {
    if (handle != nullptr) {
        xkb_state_unref(handle);
        handle = nullptr;
    }
}

void basic_state::initialize(keymap const& inp_map) {
    if (handle != nullptr) {
        xkb_state_unref(handle);
    }
    handle = xkb_state_new(inp_map.get());
    ensure(handle != nullptr, "Cannot create xkb state with xkb_state_new on initialization.");
}

xkb_state* basic_state::get() const noexcept {
    assert(handle != nullptr);
    return handle;
}
