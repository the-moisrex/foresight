// Created for unstretch_tablet: runtime-loaded wayland-client interface.
//
// Provides just enough of the Wayland client protocol to query output
// positions via the xdg-output-unstable-v1 protocol (zxdg_output_manager_v1 /
// zxdg_output_v1).  Everything is dlopen'd at runtime so the library has no
// link-time dependency on libwayland-client.

module;
#include <cstdint>

export module fs8.compositor.wayland_loader;

export namespace fs8::compositor {

    // ── Minimal opaque Wayland types (no header dependency) ───────────

    struct wl_display;
    struct wl_registry;

    // ── Wayland wire-protocol structs ─────────────────────────────────
    // These match the definitions in wayland-client.h / wayland-util.h.
    // We only declare the subset we actually use.

    struct wl_message {
        char const*                       name;
        char const*                       signature;
        struct wl_interface const* const* types;
    };

    struct wl_interface {
        char const*       name;
        int               version;
        int               method_count;
        wl_message const* methods;
        int               event_count;
        wl_message const* events;
    };

    union wl_argument {
        int32_t          i;
        uint32_t         u;
        int32_t          f; // wl_fixed_t is int32_t on the wire
        char const*      s;
        void*            o;
        uint32_t         n;
        struct wl_array* a;
        int32_t          h;
    };

    struct wl_array {
        std::size_t size;
        std::size_t alloc;
        void*       data;
    };

    // ── Core Wayland function pointer types ───────────────────────────

    using fn_wl_display_connect    = wl_display* (*) (char const* name);
    using fn_wl_display_disconnect = void (*)(wl_display* display);
    using fn_wl_display_get_fd     = int (*)(wl_display* display);
    using fn_wl_display_dispatch   = int (*)(wl_display* display);
    using fn_wl_display_roundtrip  = int (*)(wl_display* display);
    using fn_wl_display_flush      = int (*)(wl_display* display);
    using fn_wl_proxy_get_version  = uint32_t (*)(void* proxy);

    // display_get_registry / registry_bind are not exported from
    // libwayland-client.so.0 since Wayland 1.22 (they became inline header
    // functions using wl_proxy_marshal_flags).  We provide wrapper types that
    // match the original signatures; the loader implements them via
    // wl_proxy_marshal_flags + wl_proxy_get_version.
    using fn_display_get_registry = wl_registry* (*) (wl_display * display);
    using fn_registry_bind        = void* (*) (wl_registry * registry, uint32_t name, wl_interface const* iface, uint32_t version);

    // ── Proxy function pointer types ──────────────────────────────────
    // For sending requests and receiving events on Wayland protocol objects.

    /// Send a request that creates a new proxy.  Returns the new proxy.
    /// `interface` is the target interface; `version` the target version;
    /// `flags` is 0 or WL_MARSHAL_FLAG_DESTROY; varargs are the request args.
    using fn_wl_proxy_marshal_flags =
      void* (*) (void* proxy, uint32_t opcode, wl_interface const* interface, uint32_t version, uint32_t flags, ...);

    /// Attach an event listener vtable to a proxy.  Returns 0 on success.
    /// The vtable is an array of function pointers, one per event opcode.
    /// We use void* to avoid complex function-pointer-array types; the caller
    /// must ensure the vtable has the correct layout.
    using fn_wl_proxy_add_listener = int (*)(void* proxy, void* vtable, void* data);

    /// Destroy a client-side proxy (sends the destructor request).
    using fn_wl_proxy_destroy = void (*)(void* proxy);

    using fn_wl_proxy_get_user_data = void* (*) (void* proxy);
    using fn_wl_proxy_set_user_data = void (*)(void* proxy, void* data);
    using fn_wl_proxy_get_id        = uint32_t (*)(void* proxy);

    // ── WL_MARSHAL_FLAG_DESTROY constant ──────────────────────────────

    inline constexpr uint32_t WL_MARSHAL_FLAG_DESTROY = 1U;

    // ── xdg-output-unstable-v1 protocol interface data ────────────────
    //
    // These match the generated output of wayland-scanner for the
    // zxdg_output_manager_v1 / zxdg_output_v1 interfaces.
    //
    // We define a struct holding all the protocol metadata and provide a
    // function that returns a reference to a process-lifetime instance.
    // The types arrays for the manager's get_xdg_output request point into
    // the same struct, so their addresses are stable.

    struct [[nodiscard]] xdg_output_interfaces {
        // wl_output placeholder (we only need its address for bind/marshal).
        wl_interface wl_output{};

        // zxdg_output_manager_v1
        wl_message   manager_methods[2]{};
        wl_interface manager{};

        // Per-request types arrays (must outlive the wl_message that references them)
        wl_interface const* manager_get_types[2]{}; // for get_xdg_output

        // zxdg_output_v1
        wl_message   output_requests[1]{};
        wl_message   output_events[5]{};
        wl_interface output{};
    };

    /// Access the singleton xdg-output protocol interfaces.
    /// Initialized once on first call; safe from concurrent init (C++11 magic statics).
    /// IMPORTANT: The static must be initialized in-place (not via a returned
    /// temporary) because the struct contains internal pointers (e.g.
    /// manager.methods → manager_methods).  Copy-initializing from a lambda
    /// return would copy pointer values that still point into the destroyed
    /// temporary, producing dangling pointers.
    [[nodiscard]] inline xdg_output_interfaces const& xdg_output_iface() noexcept {
        static xdg_output_interfaces i{};

        // Two-phase init: default-construct first, then fixup pointers.
        // Static locals are zero-initialized, so we can detect first call.
        static bool init = [] {
            // wl_output placeholder
            i.wl_output.name         = "wl_output";
            i.wl_output.version      = 4;
            i.wl_output.method_count = 0;
            i.wl_output.methods      = nullptr;
            i.wl_output.event_count  = 0;
            i.wl_output.events       = nullptr;

            // zxdg_output_manager_v1
            i.manager_methods[0]   = {"destroy", "", nullptr};
            // "no" = new_id (zxdg_output_v1, interface via param → types[0]=NULL)
            //         + object (wl_output → types[1]=&wl_output_interface)
            i.manager_get_types[0] = nullptr;      // new_id: interface from marshal param
            i.manager_get_types[1] = &i.wl_output; // object: wl_output
            i.manager_methods[1]   = {"get_xdg_output", "no", i.manager_get_types};

            i.manager.name         = "zxdg_output_manager_v1";
            i.manager.version      = 3;
            i.manager.method_count = 2;
            i.manager.methods      = i.manager_methods;
            i.manager.event_count  = 0;
            i.manager.events       = nullptr;

            // zxdg_output_v1 requests
            i.output_requests[0] = {"destroy", "", nullptr};

            // zxdg_output_v1 events
            i.output_events[0] = {"logical_position", "ii", nullptr};
            i.output_events[1] = {"logical_size", "ii", nullptr};
            i.output_events[2] = {"done", "", nullptr};
            i.output_events[3] = {"name", "2s", nullptr};
            i.output_events[4] = {"description", "2s", nullptr};

            i.output.name         = "zxdg_output_v1";
            i.output.version      = 3;
            i.output.method_count = 1;
            i.output.methods      = i.output_requests;
            i.output.event_count  = 5;
            i.output.events       = i.output_events;

            return true;
        }();
        (void) init;
        return i;
    }

    // ── Registry event data for wl_output globals ─────────────────────

    struct [[nodiscard]] wl_output_global {
        uint32_t id      = 0;
        uint32_t version = 0;
    };

    struct [[nodiscard]] xdg_manager_global {
        uint32_t id      = 0;
        uint32_t version = 0;
        bool     found   = false;
    };

    // ── Runtime-loaded library ────────────────────────────────────────

    /// Runtime-loaded wayland-client data. Call wayland_lib_load() to populate.
    struct [[nodiscard]] wayland_lib {
        void* handle = nullptr;

        // Core display
        fn_wl_display_connect    display_connect      = nullptr;
        fn_wl_display_disconnect display_disconnect   = nullptr;
        fn_wl_display_get_fd     display_get_fd       = nullptr;
        fn_wl_display_dispatch   display_dispatch     = nullptr;
        fn_wl_display_roundtrip  display_roundtrip    = nullptr;
        fn_wl_display_flush      display_flush        = nullptr;
        fn_display_get_registry  display_get_registry = nullptr;
        fn_registry_bind         registry_bind        = nullptr;

        // Proxy
        fn_wl_proxy_marshal_flags proxy_marshal_flags = nullptr;
        fn_wl_proxy_get_version   proxy_get_version   = nullptr;
        fn_wl_proxy_add_listener  proxy_add_listener  = nullptr;
        fn_wl_proxy_destroy       proxy_destroy       = nullptr;
        fn_wl_proxy_get_user_data proxy_get_user_data = nullptr;
        fn_wl_proxy_set_user_data proxy_set_user_data = nullptr;
        fn_wl_proxy_get_id        proxy_get_id        = nullptr;
    };

    void               wayland_lib_load(wayland_lib& lib) noexcept;
    void               wayland_lib_unload(wayland_lib& lib) noexcept;
    void               wayland_lib_move(wayland_lib& dst, wayland_lib& src) noexcept;
    [[nodiscard]] bool wayland_lib_is_loaded(wayland_lib const& lib) noexcept;

} // namespace fs8::compositor
