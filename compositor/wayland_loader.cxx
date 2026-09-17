// Created for unstretch_tablet: runtime-loaded wayland-client implementation.
//
// Loads libwayland-client.so at runtime and populates function pointers
// for display connection, registry, and proxy operations needed by the
// xdg-output enumeration in monitor_detection.

module;
#include <cstdint>
#include <dlfcn.h>

module fs8.compositor.wayland_loader;

import fs8.log;

// ============================================================================
// Load / unload / move
// ============================================================================

void fs8::compositor::wayland_lib_load(wayland_lib& lib) noexcept {
    lib.handle = dlopen("libwayland-client.so.0", RTLD_LAZY | RTLD_LOCAL);
    if (!lib.handle) {
        log("wayland_loader: dlopen(libwayland-client.so.0) failed: {}", dlerror());
        lib.handle = dlopen("libwayland-client.so", RTLD_LAZY | RTLD_LOCAL);
    }
    if (!lib.handle) {
        log("wayland_loader: dlopen(libwayland-client.so) failed: {}", dlerror());
        return;
    }
    log("wayland_loader: dlopen succeeded");

    // Core display functions
    auto load = [&](void*& ptr, char const* name) noexcept {
        ptr = dlsym(lib.handle, name);
        if (!ptr) {
            log("wayland_loader: dlsym({}) failed: {}", name, dlerror());
        }
    };

    load(reinterpret_cast<void*&>(lib.display_connect),      "wl_display_connect");
    load(reinterpret_cast<void*&>(lib.display_disconnect),   "wl_display_disconnect");
    load(reinterpret_cast<void*&>(lib.display_get_fd),       "wl_display_get_fd");
    load(reinterpret_cast<void*&>(lib.display_dispatch),     "wl_display_dispatch");
    load(reinterpret_cast<void*&>(lib.display_roundtrip),    "wl_display_roundtrip");
    load(reinterpret_cast<void*&>(lib.display_flush),        "wl_display_flush");
    // wl_display_get_registry / wl_registry_bind: not exported since Wayland 1.22+
    // (moved to inline header functions).  Implemented as wrappers below.

    // Proxy functions (for xdg-output protocol)
    load(reinterpret_cast<void*&>(lib.proxy_marshal_flags), "wl_proxy_marshal_flags");
    load(reinterpret_cast<void*&>(lib.proxy_get_version),   "wl_proxy_get_version");
    load(reinterpret_cast<void*&>(lib.proxy_add_listener),  "wl_proxy_add_listener");
    load(reinterpret_cast<void*&>(lib.proxy_destroy),       "wl_proxy_destroy");
    load(reinterpret_cast<void*&>(lib.proxy_get_user_data), "wl_proxy_get_user_data");
    load(reinterpret_cast<void*&>(lib.proxy_set_user_data), "wl_proxy_set_user_data");
    load(reinterpret_cast<void*&>(lib.proxy_get_id),        "wl_proxy_get_id");

    // Load the real wl_registry_interface from the library.  Our fabricated
    // one had events=NULL which caused wl_proxy_add_listener to crash when
    // it iterates interface->events[i].
    auto* real_registry_iface = static_cast<wl_interface const*>(
        dlsym(lib.handle, "wl_registry_interface"));
    if (!real_registry_iface) {
        log("wayland_loader: wl_registry_interface not found");
    }

    // Implement display_get_registry / registry_bind as wrappers using
    // wl_proxy_marshal_flags + wl_proxy_get_version when the real symbols
    // are missing from libwayland-client.so.0 (Wayland 1.22+).
    // We store raw function pointers in static locals so the wrapper
    // functions (which cannot capture) can reach them.
    if (!lib.display_get_registry && lib.proxy_marshal_flags && lib.proxy_get_version && real_registry_iface) {
        static fn_wl_proxy_marshal_flags s_marshal = nullptr;
        static fn_wl_proxy_get_version   s_get_ver = nullptr;
        static wl_interface const*       s_iface   = nullptr;
        s_marshal = lib.proxy_marshal_flags;
        s_get_ver = lib.proxy_get_version;
        s_iface   = real_registry_iface;
        lib.display_get_registry = [](wl_display* display) noexcept -> wl_registry* {
            auto version = s_get_ver(display);
            return static_cast<wl_registry*>(
                s_marshal(display, 1 /*opcode*/, s_iface, version, 0 /*flags*/));
        };
        log("wayland_loader: display_get_registry implemented via wl_proxy_marshal_flags");
    }

    if (!lib.registry_bind && lib.proxy_marshal_flags && lib.proxy_get_version) {
        static fn_wl_proxy_marshal_flags s_marshal = nullptr;
        s_marshal = lib.proxy_marshal_flags;
        lib.registry_bind = [](wl_registry* registry, uint32_t name, wl_interface const* iface, uint32_t version) noexcept -> void* {
            // "usun": u=name, s=iface->name, u=version, n=new_id (handled by iface param).
            // The version parameter of proxy_marshal_flags is the created proxy's version,
            // which must match the version sent on the wire (the requested version, NOT the
            // registry's version).
            return s_marshal(registry, 0 /*opcode*/, iface, version, 0 /*flags*/,
                             name, iface->name, version);
        };
        log("wayland_loader: registry_bind implemented via wl_proxy_marshal_flags");
    }

    if (!wayland_lib_is_loaded(lib)) {
        log("wayland_loader: incomplete — some symbols missing");
    }
}

void fs8::compositor::wayland_lib_unload(wayland_lib& lib) noexcept {
    if (lib.handle) {
        dlclose(lib.handle);
        lib.handle = nullptr;
    }
}

void fs8::compositor::wayland_lib_move(wayland_lib& dst, wayland_lib& src) noexcept {
    dst.handle = src.handle;
    src.handle = nullptr;

    dst.display_connect      = src.display_connect;
    src.display_connect      = nullptr;
    dst.display_disconnect   = src.display_disconnect;
    src.display_disconnect   = nullptr;
    dst.display_get_fd       = src.display_get_fd;
    src.display_get_fd       = nullptr;
    dst.display_dispatch     = src.display_dispatch;
    src.display_dispatch     = nullptr;
    dst.display_roundtrip    = src.display_roundtrip;
    src.display_roundtrip    = nullptr;
    dst.display_flush        = src.display_flush;
    src.display_flush        = nullptr;
    dst.display_get_registry = src.display_get_registry;
    src.display_get_registry = nullptr;
    dst.registry_bind        = src.registry_bind;
    src.registry_bind        = nullptr;

    dst.proxy_marshal_flags = src.proxy_marshal_flags;
    src.proxy_marshal_flags = nullptr;
    dst.proxy_get_version   = src.proxy_get_version;
    src.proxy_get_version   = nullptr;
    dst.proxy_add_listener  = src.proxy_add_listener;
    src.proxy_add_listener  = nullptr;
    dst.proxy_destroy       = src.proxy_destroy;
    src.proxy_destroy       = nullptr;
    dst.proxy_get_user_data = src.proxy_get_user_data;
    src.proxy_get_user_data = nullptr;
    dst.proxy_set_user_data = src.proxy_set_user_data;
    src.proxy_set_user_data = nullptr;
    dst.proxy_get_id        = src.proxy_get_id;
    src.proxy_get_id        = nullptr;
}

[[nodiscard]] bool fs8::compositor::wayland_lib_is_loaded(wayland_lib const& lib) noexcept {
    return lib.handle
           && lib.display_connect
           && lib.display_disconnect
           && lib.display_get_fd
           && lib.display_dispatch
           && lib.display_roundtrip
           && lib.display_get_registry
           && lib.registry_bind
           && lib.proxy_marshal_flags
           && lib.proxy_get_version
           && lib.proxy_add_listener
           && lib.proxy_destroy
           && lib.proxy_get_user_data
           && lib.proxy_set_user_data
           && lib.proxy_get_id;
}
