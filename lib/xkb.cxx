// Created by moisrex on 10/12/25.

module;
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>
module foresight.lib.xkb;

using foresight::xkb::context;
using foresight::xkb::keymap;
using foresight::xkb::state;

void foresight::xkb::ensure(bool cond, std::string_view msg) {
    if (!cond) {
        throw xkb_error(std::string(msg));
    }
}

context::context(xkb_context_flags const flags) : ctx{nullptr} {
    ctx = xkb_context_new(flags);
    ensure(ctx != nullptr, "Failed to create xkb_context");
}

context::~context() noexcept {
    if (ctx) {
        xkb_context_unref(ctx);
    }
}

xkb_context* context::get() const noexcept {
    return ctx;
}

void context::set_log_level(xkb_log_level const level) const noexcept {
    xkb_context_set_log_level(get(), level);
}

int context::log_level() const noexcept {
    return xkb_context_get_log_level(get());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////       KeyMap      ////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

keymap::keymap(
  context const&    ctx,
  char const* rules,
  char const* model,
  char const* layout,
  char const* variant,
  char const* options) {
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

keymap keymap::from_string(context& ctx, std::string_view const xml) {
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
    char const* s = xkb_keymap_get_as_string(get(), XKB_KEYMAP_FORMAT_TEXT_V1);
    if (!s) {
        return {}; // no throw — optional
    }
    return {s};
}
