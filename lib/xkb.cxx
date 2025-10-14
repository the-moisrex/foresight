// Created by moisrex on 10/12/25.

module;
#include <array>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>
module foresight.lib.xkb;

using foresight::xkb::context;
using foresight::xkb::keymap;
using foresight::xkb::state;

static constexpr std::size_t XKB_KEYSYM_NAME_MAX_SIZE = 28;

namespace {

    void ensure(bool const cond, std::string_view const msg) {
        if (!cond) {
            throw foresight::xkb::xkb_error(std::string(msg));
        }
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

/////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////       KeyMap      ////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

keymap::keymap(
  context const& ctx,
  char const*    rules,
  char const*    model,
  char const*    layout,
  char const*    variant,
  char const*    options) {
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
    xkb_keymap* km = xkb_keymap_new_from_string(
      ctx.get(),
      xml.data(),
      XKB_KEYMAP_FORMAT_TEXT_V2,
      XKB_KEYMAP_COMPILE_NO_FLAGS);
    ensure(km != nullptr, "Failed to create xkb_keymap from string");
    return keymap(km);
}

keymap::keymap(xkb_keymap* km) : handle{km} {}

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

std::string foresight::xkb::name(xkb_keysym_t const keysym) {
    std::array<char, XKB_KEYSYM_NAME_MAX_SIZE> name{};
    int const                                  ret = xkb_keysym_get_name(keysym, name.data(), name.size());
    if (ret < 0 || static_cast<size_t>(ret) >= name.size()) {
        return {};
    }
    return {name.data()};
}
