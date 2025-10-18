// Created by moisrex on 10/12/25.

module;
#include <array>
#include <memory>
#include <string_view>
#include <utility>
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

context::context(private_tag, xkb_context_flags const flags) : ctx{xkb_context_new(flags)} {
    ensure(ctx != nullptr, "Failed to create xkb_context");
}

context::pointer context::create(xkb_context_flags flags) {
    return std::make_shared<context>(private_tag{}, flags);
}

context::~context() noexcept {
    if (ctx != nullptr) {
        xkb_context_unref(ctx);
    }
}

context::pointer context::getptr() {
    return shared_from_this();
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
  private_tag,
  context::pointer inp_ctx,
  char const*      rules,
  char const*      model,
  char const*      layout,
  char const*      variant,
  char const*      options)
  : ctx{std::move(inp_ctx)} {
    xkb_rule_names names{};
    names.rules   = rules;
    names.model   = model;
    names.layout  = layout;
    names.variant = variant;
    names.options = options;
    load(&names);
}

keymap::pointer keymap::create(
  context::pointer const& inp_ctx,
  char const*             rules,
  char const*             model,
  char const*             layout,
  char const*             variant,
  char const*             options) {
    return std::make_shared<keymap>(private_tag{}, inp_ctx, rules, model, layout, variant, options);
}

keymap::pointer keymap::create(
  char const* rules,
  char const* model,
  char const* layout,
  char const* variant,
  char const* options) {
    return create(context::create(), rules, model, layout, variant, options);
}

void keymap::load(xkb_rule_names const* names, xkb_keymap_format const keymap_format) {
    if (handle != nullptr) [[unlikely]] {
        xkb_keymap_unref(handle);
    }
    handle = xkb_keymap_new_from_names2(ctx->get(), names, keymap_format, XKB_KEYMAP_COMPILE_NO_FLAGS);
    ensure(handle != nullptr, "Failed to create xkb_keymap from names");
}

keymap::pointer keymap::from_string(context::pointer const& ctx, std::string_view const xml) {
    return std::make_shared<keymap>(
      ctx,
      xkb_keymap_new_from_buffer(
        ctx->get(),
        xml.data(),
        xml.size(),
        XKB_KEYMAP_FORMAT_TEXT_V2,
        XKB_KEYMAP_COMPILE_NO_FLAGS));
}

keymap::keymap(context::pointer inp_ctx, xkb_keymap* km) : ctx{std::move(inp_ctx)}, handle{km} {
    ensure(km != nullptr, "Failed to create xkb_keymap from string");
}

keymap::~keymap() noexcept {
    if (handle != nullptr) {
        xkb_keymap_unref(handle);
    }
}

keymap::pointer keymap::getptr() {
    return shared_from_this();
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

context::pointer keymap::get_context() const {
    return ctx;
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
