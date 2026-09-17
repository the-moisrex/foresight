// Created for unstretch_tablet: runtime-loaded X11 + XRandR interface.

module;
#include <cstdint>

export module fs8.compositor.x11_loader;

export namespace fs8::compositor {

    // ── Minimal X11/XRandR type aliases ───────────────────────────────

    using XID    = unsigned long;
    using Window = unsigned long;
    using RRMode = unsigned long;
    using Time   = unsigned long;

    // ── Function pointer types (void* returns; structs defined in .cxx) ─

    // libX11
    using fn_XOpenDisplay   = void* (*) (char const* display_name);
    using fn_XCloseDisplay  = int (*)(void* display);
    using fn_XDefaultScreen = int (*)(void* display);
    using fn_XRootWindow    = Window (*)(void* display, int screen);

    // libXrandr
    using fn_XRRGetScreenResourcesCurrent = void* (*) (void* display, Window window);
    using fn_XRRFreeScreenResources       = void (*)(void* resources);
    using fn_XRRGetCrtcInfo               = void* (*) (void* display, void* resources, XID crtc);
    using fn_XRRFreeCrtcInfo              = void (*)(void* crtc_info);
    using fn_XRRGetOutputInfo             = void* (*) (void* display, void* resources, XID output);
    using fn_XRRFreeOutputInfo            = void (*)(void* output_info);
    using fn_XRRGetOutputPrimary          = XID (*)(void* display, Window window);

    /// Runtime-loaded X11 + XRandR data. Call x11_lib_load() to populate.
    struct [[nodiscard]] x11_lib {
        void* x11_handle    = nullptr;
        void* xrandr_handle = nullptr;

        // libX11
        fn_XOpenDisplay   open_display   = nullptr;
        fn_XCloseDisplay  close_display  = nullptr;
        fn_XDefaultScreen default_screen = nullptr;
        fn_XRootWindow    root_window    = nullptr;

        // libXrandr
        fn_XRRGetScreenResourcesCurrent get_screen_resources  = nullptr;
        fn_XRRFreeScreenResources       free_screen_resources = nullptr;
        fn_XRRGetCrtcInfo               get_crtc_info         = nullptr;
        fn_XRRFreeCrtcInfo              free_crtc_info        = nullptr;
        fn_XRRGetOutputInfo             get_output_info       = nullptr;
        fn_XRRFreeOutputInfo            free_output_info      = nullptr;
        fn_XRRGetOutputPrimary          get_output_primary    = nullptr;
    };

    void               x11_lib_load(x11_lib& lib) noexcept;
    void               x11_lib_unload(x11_lib& lib) noexcept;
    void               x11_lib_move(x11_lib& dst, x11_lib& src) noexcept;
    [[nodiscard]] bool x11_lib_is_loaded(x11_lib const& lib) noexcept;

} // namespace fs8::compositor
