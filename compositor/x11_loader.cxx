// Created for unstretch_tablet: runtime-loaded X11 + XRandR implementation.

module;
#include <dlfcn.h>

module fs8.compositor.x11_loader;

void fs8::compositor::x11_lib_load(x11_lib& lib) noexcept {
    // Load libX11
    lib.x11_handle = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (!lib.x11_handle) {
        lib.x11_handle = dlopen("libX11.so", RTLD_LAZY | RTLD_LOCAL);
    }

    if (lib.x11_handle) {
        lib.open_display   = reinterpret_cast<fn_XOpenDisplay>(dlsym(lib.x11_handle, "XOpenDisplay"));
        lib.close_display  = reinterpret_cast<fn_XCloseDisplay>(dlsym(lib.x11_handle, "XCloseDisplay"));
        lib.default_screen = reinterpret_cast<fn_XDefaultScreen>(dlsym(lib.x11_handle, "XDefaultScreen"));
        lib.root_window    = reinterpret_cast<fn_XRootWindow>(dlsym(lib.x11_handle, "XRootWindow"));
    }

    // Load libXrandr
    lib.xrandr_handle = dlopen("libXrandr.so.2", RTLD_LAZY | RTLD_LOCAL);
    if (!lib.xrandr_handle) {
        lib.xrandr_handle = dlopen("libXrandr.so", RTLD_LAZY | RTLD_LOCAL);
    }

    if (lib.xrandr_handle) {
        lib.get_screen_resources =
          reinterpret_cast<fn_XRRGetScreenResourcesCurrent>(dlsym(lib.xrandr_handle, "XRRGetScreenResourcesCurrent"));
        lib.free_screen_resources = reinterpret_cast<fn_XRRFreeScreenResources>(dlsym(lib.xrandr_handle, "XRRFreeScreenResources"));
        lib.get_crtc_info         = reinterpret_cast<fn_XRRGetCrtcInfo>(dlsym(lib.xrandr_handle, "XRRGetCrtcInfo"));
        lib.free_crtc_info        = reinterpret_cast<fn_XRRFreeCrtcInfo>(dlsym(lib.xrandr_handle, "XRRFreeCrtcInfo"));
        lib.get_output_info       = reinterpret_cast<fn_XRRGetOutputInfo>(dlsym(lib.xrandr_handle, "XRRGetOutputInfo"));
        lib.free_output_info      = reinterpret_cast<fn_XRRFreeOutputInfo>(dlsym(lib.xrandr_handle, "XRRFreeOutputInfo"));
        lib.get_output_primary    = reinterpret_cast<fn_XRRGetOutputPrimary>(dlsym(lib.xrandr_handle, "XRRGetOutputPrimary"));
    }
}

void fs8::compositor::x11_lib_unload(x11_lib& lib) noexcept {
    if (lib.xrandr_handle) {
        dlclose(lib.xrandr_handle);
        lib.xrandr_handle = nullptr;
    }
    if (lib.x11_handle) {
        dlclose(lib.x11_handle);
        lib.x11_handle = nullptr;
    }
}

void fs8::compositor::x11_lib_move(x11_lib& dst, x11_lib& src) noexcept {
    dst.x11_handle    = src.x11_handle;
    src.x11_handle    = nullptr;
    dst.xrandr_handle = src.xrandr_handle;
    src.xrandr_handle = nullptr;

    dst.open_display   = src.open_display;
    src.open_display   = nullptr;
    dst.close_display  = src.close_display;
    src.close_display  = nullptr;
    dst.default_screen = src.default_screen;
    src.default_screen = nullptr;
    dst.root_window    = src.root_window;
    src.root_window    = nullptr;

    dst.get_screen_resources  = src.get_screen_resources;
    src.get_screen_resources  = nullptr;
    dst.free_screen_resources = src.free_screen_resources;
    src.free_screen_resources = nullptr;
    dst.get_crtc_info         = src.get_crtc_info;
    src.get_crtc_info         = nullptr;
    dst.free_crtc_info        = src.free_crtc_info;
    src.free_crtc_info        = nullptr;
    dst.get_output_info       = src.get_output_info;
    src.get_output_info       = nullptr;
    dst.free_output_info      = src.free_output_info;
    src.free_output_info      = nullptr;
    dst.get_output_primary    = src.get_output_primary;
    src.get_output_primary    = nullptr;
}

[[nodiscard]] bool fs8::compositor::x11_lib_is_loaded(x11_lib const& lib) noexcept {
    return lib.x11_handle
           && lib.open_display
           && lib.close_display
           && lib.default_screen
           && lib.root_window
           && lib.xrandr_handle
           && lib.get_screen_resources
           && lib.free_screen_resources
           && lib.get_crtc_info
           && lib.free_crtc_info
           && lib.get_output_info
           && lib.free_output_info;
}
