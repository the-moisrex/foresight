// Created for unstretch_tablet: monitor detection implementation.

module;
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module fs8.compositor.monitor_detection;
import fs8.log;
import fs8.compositor.drm_loader;
import fs8.compositor.wayland_loader;
import fs8.compositor.x11_loader;

using fs8::compositor::display_server;
using fs8::compositor::drm_lib;
using fs8::compositor::drm_lib_is_loaded;
using fs8::compositor::drm_lib_load;
using fs8::compositor::monitor_enum_result;
using fs8::compositor::monitor_info;
using fs8::compositor::RRMode;
using fs8::compositor::Time;
using fs8::compositor::wayland_lib;
using fs8::compositor::wayland_lib_is_loaded;
using fs8::compositor::wayland_lib_load;
using fs8::compositor::Window;
using fs8::compositor::wl_argument;
using fs8::compositor::wl_interface;
using fs8::compositor::WL_MARSHAL_FLAG_DESTROY;
using fs8::compositor::wl_message;
using fs8::compositor::wl_output_global;
using fs8::compositor::x11_lib;
using fs8::compositor::x11_lib_is_loaded;
using fs8::compositor::x11_lib_load;
using fs8::compositor::xdg_manager_global;
using fs8::compositor::xdg_output_iface;
using fs8::compositor::xdg_output_interfaces;
using fs8::compositor::XID;

// ============================================================================
// DRM struct definitions (layout-compatible with libdrm, no header needed)
// ============================================================================

namespace {

    inline constexpr int DRM_DISPLAY_MODE_LEN = 32;

    inline constexpr int DRM_MODE_UNKNOWNCONNECTION = 0;

    struct drm_mode_modeinfo {
        uint32_t clock;
        uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
        uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
        uint32_t vrefresh;
        uint32_t flags;
        uint32_t type;
        char     name[DRM_DISPLAY_MODE_LEN];
    };

    struct drm_mode_card_res {
        uint64_t* fb_id_ptr;
        uint64_t* crtc_id_ptr;
        uint64_t* connector_id_ptr;
        uint64_t* encoder_id_ptr;
        uint32_t  count_fbs;
        uint32_t  count_crtcs;
        uint32_t  count_connectors;
        uint32_t  count_encoders;
        uint32_t  min_width, max_width;
        uint32_t  min_height, max_height;
    };

    struct drm_mode_get_connector {
        uint64_t* encoders_ptr;
        uint64_t* modes_ptr;
        uint64_t* props_ptr;
        uint64_t* prop_values_ptr;
        uint32_t  count_modes;
        uint32_t  count_props;
        uint32_t  count_encoders;
        uint32_t  encoder_id;
        uint32_t  connector_id;
        uint32_t  connector_type;
        uint32_t  connector_type_id;
        uint32_t  connection;
        uint32_t  mm_width, mm_height;
        uint32_t  subpixel;
    };

    struct drm_mode_get_encoder {
        uint32_t encoder_id;
        uint32_t encoder_type;
        uint32_t crtc_id;
        uint64_t possible_crtcs;
        uint64_t possible_clones;
    };

    struct drm_mode_crtc {
        uint64_t*         set_connectors_ptr;
        uint32_t          count_connectors;
        uint32_t          crtc_id;
        uint32_t          fb_id;
        uint32_t          x, y;
        uint32_t          gamma_size;
        uint32_t          mode_valid;
        drm_mode_modeinfo mode;
    };

} // anonymous namespace

// ============================================================================
// X11/XRandR struct definitions (layout-compatible with Xlib, no header needed)
// ============================================================================

namespace {

    inline constexpr int RR_Connected = 0;

    struct XRRScreenResources {
        Time  timestamp;
        Time  configTimestamp;
        int   ncrtc;
        XID*  crtcs;
        int   noutput;
        XID*  outputs;
        int   nmode;
        void* modes;
    };

    struct XRRCrtcInfo {
        Time         timestamp;
        int          x, y;
        unsigned int width, height;
        RRMode       mode;
        int          rotation;
        int          noutput;
        XID*         outputs;
        int          npossible;
        XID*         possible;
    };

    struct XRROutputInfo {
        Time          timestamp;
        XID           crtc;
        char*         name;
        int           nameLen;
        unsigned long mm_width;
        unsigned long mm_height;
        int           connection;
        int           subpixel_order;
        int           ncrtc;
        XID*          crtcs;
        int           nclone;
        XID*          clones;
        int           nmode;
        int           npreferred;
        void*         modes;
    };

} // anonymous namespace

// ============================================================================
// Display server detection
// ============================================================================

display_server fs8::compositor::detect_display_server() noexcept {
    if (auto* wd = std::getenv("WAYLAND_DISPLAY"); wd && wd[0] != '\0') {
        return display_server::wayland;
    }

    if (auto* st = std::getenv("XDG_SESSION_TYPE")) {
        if (std::strcmp(st, "wayland") == 0) {
            return display_server::wayland;
        }
        if (std::strcmp(st, "x11") == 0) {
            return display_server::x11;
        }
    }

    if (auto* d = std::getenv("DISPLAY"); d && d[0] != '\0') {
        return display_server::x11;
    }

    return display_server::unknown;
}

// ============================================================================
// sysfs-based enumeration (no external deps)
// ============================================================================

namespace {

    [[nodiscard]] std::string read_text_file(std::filesystem::path const& p) noexcept {
        std::ifstream f(p);
        if (!f) {
            return {};
        }
        std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == '\0')) {
            s.pop_back();
        }
        return s;
    }

    [[nodiscard]] std::vector<uint8_t> read_binary_file(std::filesystem::path const& p) noexcept {
        std::ifstream f(p, std::ios::binary);
        if (!f) {
            return {};
        }
        return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    }

    struct edid_info {
        std::string monitor_name;
        uint16_t    phys_width_cm  = 0;
        uint16_t    phys_height_cm = 0;
        uint32_t    width_px       = 0;
        uint32_t    height_px      = 0;
    };

    [[nodiscard]] edid_info parse_edid(std::vector<uint8_t> const& data) noexcept {
        edid_info info{};
        if (data.size() < 128) {
            return info;
        }

        info.phys_width_cm  = data[21];
        info.phys_height_cm = data[22];

        // The first detailed timing descriptor (bytes 54..71) carries the
        // preferred mode.  When bytes 54/55 are both zero it is a monitor
        // descriptor (e.g. the name), not a timing, so skip it.  Parsing the
        // EDID avoids reading /sys/class/drm/*/modes, whose kernel handler
        // probes the connector and emits a hotplug uevent (feedback loop).
        if (data[54] != 0 || data[55] != 0) {
            info.width_px  = static_cast<uint32_t>(data[56]) | ((static_cast<uint32_t>(data[58]) & 0xF0U) << 4U);
            info.height_px = static_cast<uint32_t>(data[59]) | ((static_cast<uint32_t>(data[61]) & 0xF0U) << 4U);
        }

        for (int i = 0; i < 4; ++i) {
            std::size_t off = 54 + static_cast<std::size_t>(i) * 18;
            if (off + 18 > data.size()) {
                break;
            }
            if (data[off] == 0 && data[off + 1] == 0 && data[off + 2] == 0 && data[off + 3] == 0xFC) {
                std::string name(reinterpret_cast<char const*>(&data[off + 5]), 13);
                while (!name.empty() && (name.back() == ' ' || name.back() == '\n' || name.back() == '\r' || name.back() == '\0')) {
                    name.pop_back();
                }
                info.monitor_name = std::move(name);
                break;
            }
        }
        return info;
    }

    void enumerate_from_sysfs_and_drm(monitor_enum_result& result) noexcept {
        namespace fs           = std::filesystem;
        constexpr auto drm_dir = "/sys/class/drm";

        if (!fs::exists(drm_dir)) {
            return;
        }

        drm_lib drm{};
        drm_lib_load(drm);

        for (auto const& entry : fs::directory_iterator(drm_dir)) {
            auto name = entry.path().filename().string();

            if (name.find('-') == std::string::npos) {
                continue;
            }
            if (name.find("render") != std::string::npos) {
                continue;
            }
            if (name.find("control") != std::string::npos) {
                continue;
            }

            auto status    = read_text_file(entry.path() / "status");
            bool connected = (status == "connected");

            monitor_info mon{};
            mon.is_enabled = connected;

            // Extract connector name from sysfs entry (e.g. "card1-HDMI-A-2" → "HDMI-A-2").
            {
                auto dash = name.find('-');
                mon.connector = (dash != std::string::npos) ? name.substr(dash + 1) : name;
                mon.name      = mon.connector;
            }

            if (!connected) {
                continue;
            }

            auto edid_data = read_binary_file(entry.path() / "edid");
            auto edid      = parse_edid(edid_data);
            mon.width_mm   = edid.phys_width_cm * 10;
            mon.height_mm  = edid.phys_height_cm * 10;
            mon.width_px   = edid.width_px;
            mon.height_px  = edid.height_px;

            if (!edid.monitor_name.empty()) {
                mon.name = std::move(edid.monitor_name);
            }

            if (drm_lib_is_loaded(drm)) {
                // TODO: open /dev/dri/cardN and enumerate connectors for active mode
            }

            if (mon.width_px > 0 && mon.height_px > 0) {
                result.monitors.push_back(std::move(mon));
            }
        }
    }

    // ========================================================================
    // Wayland xdg-output enumeration (gets compositor-assigned positions)
    // ========================================================================

    // Per-output state collected while dispatching xdg_output events.
    struct xdg_output_state {
        int32_t x            = 0;
        int32_t y            = 0;
        int32_t logical_w    = 0;
        int32_t logical_h    = 0;
        char    name[64]     = {};
        bool    got_position = false;
        bool    got_name     = false;
    };

    // Registry listener: collects wl_output and zxdg_output_manager_v1 globals.
    struct registry_state {
        std::vector<wl_output_global> outputs;
        xdg_manager_global            manager{};
    };

    // ── Listener function implementations ─────────────────────────────
    // Each receives (void* data, void* proxy, ...event-args).
    // The `data` pointer is whatever we pass to wl_proxy_add_listener.

    static void on_registry_global(void* data, void* /*registry*/, uint32_t id, char const* interface, uint32_t version) {
        auto* state = static_cast<registry_state*>(data);
        if (std::string_view(interface) == "wl_output") {
            state->outputs.push_back({.id = id, .version = version});
        } else if (std::string_view(interface) == "zxdg_output_manager_v1") {
            state->manager = {.id = id, .version = version, .found = true};
        }
    }

    static void on_registry_global_remove(void* /*data*/, void* /*registry*/, uint32_t /*id*/) {
        // Not used; we only need the initial enumeration.
    }

    static void on_xdg_logical_position(void* data, void* /*output*/, int32_t x, int32_t y) {
        auto* s         = static_cast<xdg_output_state*>(data);
        s->x            = x;
        s->y            = y;
        s->got_position = true;
    }

    static void on_xdg_logical_size(void* data, void* /*output*/, int32_t w, int32_t h) {
        auto* s      = static_cast<xdg_output_state*>(data);
        s->logical_w = w;
        s->logical_h = h;
    }

    static void on_xdg_done(void* /*data*/, void* /*output*/) {
        // Version 3: done is deprecated; compositor sends wl_output.done instead.
        // We don't need to do anything here.
    }

    static void on_xdg_name(void* data, void* /*output*/, char const* name) {
        auto* s = static_cast<xdg_output_state*>(data);
        if (name) {
            std::strncpy(s->name, name, sizeof(s->name) - 1);
            s->name[sizeof(s->name) - 1] = '\0';
            s->got_name                  = true;
        }
    }

    static void on_xdg_description(void* /*data*/, void* /*output*/, char const* /*desc*/) {
        // Not used.
    }

    // Listener vtable structs.  These match the ABI expected by
    // wl_proxy_add_listener — an array of function pointers cast to void*.
    // We define them as structs so the layout is guaranteed.

    struct registry_listener {
        void (*global)(void*, void*, uint32_t, char const*, uint32_t);
        void (*global_remove)(void*, void*, uint32_t);
    };

    struct xdg_output_listener {
        void (*logical_position)(void*, void*, int32_t, int32_t);
        void (*logical_size)(void*, void*, int32_t, int32_t);
        void (*done)(void*, void*);
        void (*name)(void*, void*, char const*);
        void (*description)(void*, void*, char const*);
    };

    // Non-const so we can cast to void* for wl_proxy_add_listener.
    static registry_listener   s_registry_vtable   = {&on_registry_global, &on_registry_global_remove};
    static xdg_output_listener s_xdg_output_vtable = {
      &on_xdg_logical_position,
      &on_xdg_logical_size,
      &on_xdg_done,
      &on_xdg_name,
      &on_xdg_description,
    };

    // Helper: extract the connector name from a sysfs-style path.
    // "card0-HDMI-A-1" → "HDMI-A-1"
    // "BenQ GW2790QT"  → "BenQ GW2790QT" (EDID name, no dash prefix)

    void enumerate_from_wayland(monitor_enum_result& result) noexcept {
        wayland_lib wl{};
        wayland_lib_load(wl);
        if (!wayland_lib_is_loaded(wl)) {
            fs8::log("monitor_detection: wayland-client not loadable, skipping xdg-output");
            return;
        }

        auto* display = wl.display_connect(nullptr);
        if (!display) {
            fs8::log("monitor_detection: wl_display_connect failed");
            return;
        }

        auto* registry = wl.display_get_registry(display);
        if (!registry) {
            fs8::log("monitor_detection: wl_display_get_registry failed");
            wl.display_disconnect(display);
            return;
        }

        // Collect globals via registry listener.
        registry_state reg_state{};
        wl.proxy_add_listener(registry, static_cast<void*>(&s_registry_vtable), &reg_state);
        wl.display_dispatch(display);
        wl.display_roundtrip(display);

        if (!reg_state.manager.found) {
            fs8::log("monitor_detection: zxdg_output_manager_v1 not available");
            wl.proxy_destroy(registry);
            wl.display_dispatch(display);
            wl.display_disconnect(display);
            return;
        }

        fs8::log("monitor_detection: found {} wl_output globals, manager v{}", reg_state.outputs.size(), reg_state.manager.version);
        for (auto const& out : reg_state.outputs) {
            fs8::log("monitor_detection:   wl_output global id={}, version={}", out.id, out.version);
        }

        // Bind to the xdg-output manager.
        auto const manager_version = std::min(reg_state.manager.version, 3U);
        auto&      ifaces          = xdg_output_iface();
        auto*      manager         = wl.registry_bind(registry, reg_state.manager.id, &ifaces.manager, manager_version);
        if (!manager) {
            fs8::log("monitor_detection: failed to bind zxdg_output_manager_v1");
            wl.proxy_destroy(registry);
            wl.display_dispatch(display);
            wl.display_disconnect(display);
            return;
        }

        // Load the real wl_output_interface from libwayland.  Our fabricated
        // placeholder has event_count=0 which may confuse the server-side
        // wl_closure_lookup_objects when validating the output argument.
        auto* real_wl_output_iface = static_cast<wl_interface const*>(
            dlsym(wl.handle, "wl_output_interface"));
        if (!real_wl_output_iface) {
            fs8::log("monitor_detection: wl_output_interface not found via dlsym, using fabricated");
        }

        // Create xdg_output for each wl_output.  Each gets a heap-allocated
        // state that the listener writes into.
        struct xdg_handle {
            void*             proxy = nullptr;
            xdg_output_state* state = nullptr;
        };

        std::vector<xdg_handle> handles;
        handles.reserve(reg_state.outputs.size());

        // Keep wl_output proxies alive until after the roundtrip so the server
        // can look them up when processing get_xdg_output.
        struct wl_out_handle {
            void* proxy = nullptr;
        };
        std::vector<wl_out_handle> wl_outs;
        wl_outs.reserve(reg_state.outputs.size());

        for (auto const& out : reg_state.outputs) {
            auto const* out_iface = real_wl_output_iface ? real_wl_output_iface : &ifaces.wl_output;
            auto* wl_out = wl.registry_bind(registry, out.id, out_iface, std::min(out.version, 4U));
            if (!wl_out) {
                fs8::log("monitor_detection: registry_bind failed for wl_output id={}", out.id);
                continue;
            }

            auto const wl_out_id = wl.proxy_get_id(wl_out);
            fs8::log("monitor_detection: bound wl_output id={} -> proxy_id={}, calling get_xdg_output (manager_id={})",
                     out.id, wl_out_id, wl.proxy_get_id(manager));

            // get_xdg_output: opcode 1, signature "no" (new_id + object).
            // The 'n' (new_id) vararg must be NULL — the actual interface is
            // passed as the 3rd parameter to wl_proxy_marshal_flags.
            auto* xdg = wl.proxy_marshal_flags(manager, 1 /*get_xdg_output*/, &ifaces.output, manager_version, 0 /*flags*/, nullptr, wl_out);

            if (!xdg) {
                fs8::log("monitor_detection: get_xdg_output returned NULL for wl_output id={}", out.id);
                wl.proxy_destroy(wl_out);
                continue;
            }

            fs8::log("monitor_detection: get_xdg_output succeeded, xdg proxy_id={}", wl.proxy_get_id(xdg));

            auto* state = new xdg_output_state{};
            wl.proxy_set_user_data(xdg, state);
            wl.proxy_add_listener(xdg, static_cast<void*>(&s_xdg_output_vtable), state);
            handles.push_back({xdg, state});
            wl_outs.push_back({wl_out});
        }

        // Receive all xdg_output events.
        // wl_display_roundtrip hangs when called a second time via dlsym —
        // the internal reader-loop_count tracking gets confused by events
        // arriving during proxy_marshal_flags opportunistic reads.  Instead,
        // flush and dispatch in a loop until all handles are populated.
        {
            wl.display_flush(display);

            for (int i = 0; i < 100; ++i) {
                bool all_done = true;
                for (auto const& h : handles) {
                    if (!h.state->got_position || !h.state->got_name ||
                        h.state->logical_w == 0 || h.state->logical_h == 0) {
                        all_done = false;
                        break;
                    }
                }
                if (all_done) break;

                // Dispatch available events (may block until one arrives).
                wl.display_dispatch(display);
            }
        }

        // Now safe to destroy wl_output proxies.
        for (auto& h : wl_outs) {
            wl.proxy_destroy(h.proxy);
        }

        // Match xdg-output names to sysfs-detected monitors and update positions.
        for (auto& h : handles) {
            auto const& st = *h.state;
            if (!st.got_position) {
                continue;
            }

            for (auto& m : result.monitors) {
                if (st.got_name && m.connector == st.name) {
                    m.x = st.x;
                    m.y = st.y;
                    // Use logical size if the sysfs monitor has no EDID resolution.
                    if (st.logical_w > 0 && st.logical_h > 0 && (m.width_px == 0 || m.height_px == 0)) {
                        m.width_px  = static_cast<uint32_t>(st.logical_w);
                        m.height_px = static_cast<uint32_t>(st.logical_h);
                    }
                    fs8::log("monitor_detection: xdg-output '{}' at ({}, {}) {}x{}", st.name, st.x, st.y, m.width_px, m.height_px);
                    break;
                }
            }
        }

        // Cleanup: destroy xdg_outputs, manager, registry, disconnect.
        for (auto& h : handles) {
            wl.proxy_marshal_flags(h.proxy, 0 /*destroy*/, nullptr, 0, WL_MARSHAL_FLAG_DESTROY);
            delete h.state;
        }

        wl.proxy_marshal_flags(manager, 0 /*destroy*/, nullptr, 0, WL_MARSHAL_FLAG_DESTROY);
        wl.proxy_destroy(registry);
        wl.display_disconnect(display);

        fs8::log("monitor_detection: xdg-output enumeration complete ({} outputs)", handles.size());
    }

    void enumerate_from_x11(monitor_enum_result& result) noexcept {
        x11_lib x11{};
        x11_lib_load(x11);
        if (!x11_lib_is_loaded(x11)) {
            return;
        }

        auto* display = x11.open_display(nullptr);
        if (!display) {
            return;
        }

        int  screen = x11.default_screen(display);
        auto root   = x11.root_window(display, screen);

        auto* res = static_cast<XRRScreenResources*>(x11.get_screen_resources(display, root));
        if (!res) {
            x11.close_display(display);
            return;
        }

        result.server = display_server::x11;

        XID primary = x11.get_output_primary(display, root);

        for (int i = 0; i < res->noutput; ++i) {
            auto* out = static_cast<XRROutputInfo*>(x11.get_output_info(display, res, res->outputs[i]));
            if (!out) {
                continue;
            }

            if (out->connection != RR_Connected || !out->crtc) {
                x11.free_output_info(out);
                continue;
            }

            auto* crtc = static_cast<XRRCrtcInfo*>(x11.get_crtc_info(display, res, out->crtc));
            if (!crtc || crtc->mode == 0) {
                if (crtc) {
                    x11.free_crtc_info(crtc);
                }
                x11.free_output_info(out);
                continue;
            }

            monitor_info mon{};
            mon.name       = out->name ? out->name : "";
            mon.x          = crtc->x;
            mon.y          = crtc->y;
            mon.width_px   = crtc->width;
            mon.height_px  = crtc->height;
            mon.width_mm   = static_cast<uint32_t>(out->mm_width);
            mon.height_mm  = static_cast<uint32_t>(out->mm_height);
            mon.is_primary = (res->outputs[i] == primary);
            mon.is_enabled = true;

            result.monitors.push_back(std::move(mon));

            x11.free_crtc_info(crtc);
            x11.free_output_info(out);
        }

        x11.free_screen_resources(res);
        x11.close_display(display);
    }

    void detect_mirror_mode(std::vector<monitor_info>& monitors) noexcept {
        for (std::size_t i = 0; i < monitors.size(); ++i) {
            for (std::size_t j = i + 1; j < monitors.size();) {
                if (are_monitors_mirrored(monitors[i], monitors[j])) {
                    monitors.erase(monitors.begin() + static_cast<std::ptrdiff_t>(j));
                } else {
                    ++j;
                }
            }
        }
    }

} // anonymous namespace

// ============================================================================
// Desktop bounds
// ============================================================================

fs8::compositor::desktop_bounds fs8::compositor::compute_desktop_bounds(std::span<monitor_info const> monitors) noexcept {
    if (monitors.empty()) {
        return {};
    }

    int32_t min_x = monitors[0].x;
    int32_t min_y = monitors[0].y;
    int32_t max_x = monitors[0].x + static_cast<int32_t>(monitors[0].width_px);
    int32_t max_y = monitors[0].y + static_cast<int32_t>(monitors[0].height_px);

    for (auto const& m : monitors) {
        if (m.x < min_x) {
            min_x = m.x;
        }
        if (m.y < min_y) {
            min_y = m.y;
        }
        auto right  = m.x + static_cast<int32_t>(m.width_px);
        auto bottom = m.y + static_cast<int32_t>(m.height_px);
        if (right > max_x) {
            max_x = right;
        }
        if (bottom > max_y) {
            max_y = bottom;
        }
    }

    return {
      .x      = min_x,
      .y      = min_y,
      .width  = static_cast<uint32_t>(max_x - min_x),
      .height = static_cast<uint32_t>(max_y - min_y),
    };
}

fs8::compositor::tablet_remap fs8::compositor::compute_tablet_remap(
  monitor_info const&   target,
  desktop_bounds const& desktop,
  int32_t               tablet_min_x,
  int32_t               tablet_max_x,
  int32_t               tablet_min_y,
  int32_t               tablet_max_y) noexcept {
    if (desktop.width == 0 || desktop.height == 0) {
        return {};
    }

    auto const tablet_range_x = static_cast<float>(tablet_max_x - tablet_min_x);
    auto const tablet_range_y = static_cast<float>(tablet_max_y - tablet_min_y);
    auto const desktop_width  = static_cast<float>(desktop.width);
    auto const desktop_height = static_cast<float>(desktop.height);

    auto const offset_x = static_cast<float>(target.x - desktop.x);
    auto const offset_y = static_cast<float>(target.y - desktop.y);

    return {
      .offset_x = static_cast<float>(tablet_min_x) + offset_x / desktop_width * tablet_range_x,
      .offset_y = static_cast<float>(tablet_min_y) + offset_y / desktop_height * tablet_range_y,
      .scale_x  = static_cast<float>(target.width_px) / desktop_width * tablet_range_x,
      .scale_y  = static_cast<float>(target.height_px) / desktop_height * tablet_range_y,
    };
}

// ============================================================================
// Main enumeration function
// ============================================================================

monitor_enum_result fs8::compositor::enumerate_monitors() noexcept {
    monitor_enum_result result{};
    result.server = detect_display_server();

    enumerate_from_sysfs_and_drm(result);

    // On Wayland, query compositor positions via xdg-output protocol.
    if (result.server == display_server::wayland) {
        enumerate_from_wayland(result);
    }

    if (result.server == display_server::x11 || result.server == display_server::unknown) {
        enumerate_from_x11(result);
    }

    // Deduplicate by name: when both sysfs and X11 enumerate the same monitor,
    // keep the entry with position data (non-zero x/y) and discard the other.
    {
        std::vector<monitor_info> deduped;
        for (auto& m : result.monitors) {
            auto it = std::find_if(deduped.begin(), deduped.end(), [&m](monitor_info const& d) noexcept {
                return d.name == m.name;
            });
            if (it == deduped.end()) {
                deduped.push_back(std::move(m));
            } else if (m.x != 0 || m.y != 0) {
                *it = std::move(m);
            }
        }
        result.monitors = std::move(deduped);
    }

    // Only report position info if the compositor actually gave us positions.
    // On Wayland, xdg-output provides compositor-assigned positions.
    // On X11, XRandR provides positions.  When neither is available, the
    // tiled fallback arranges monitors by name order from (0,0).
    result.has_position_info = has_known_positions(result.monitors);

    if (result.monitors.empty()) {
        monitor_info default_mon{};
        default_mon.name       = "default";
        default_mon.width_px   = 1920;
        default_mon.height_px  = 1080;
        default_mon.is_enabled = true;
        result.monitors.push_back(std::move(default_mon));
    }

    // When the compositor didn't tell us where the outputs sit, tile them in
    // name order.  This must happen before mirror detection: with unknown
    // positions every monitor shares (0,0), and identical sizes would otherwise
    // be mistaken for a mirrored pair.
    if (!result.has_position_info) {
        arrange_monitors_tiled(result.monitors);
    }

    detect_mirror_mode(result.monitors);

    return result;
}

// ============================================================================
// Cursor helpers
// ============================================================================

int fs8::compositor::monitor_at_position(std::span<monitor_info const> const monitors, int32_t const x, int32_t const y) noexcept {
    for (std::size_t i = 0; i < monitors.size(); ++i) {
        auto const& m = monitors[i];
        if (x >= m.x && x < m.x + static_cast<int32_t>(m.width_px) && y >= m.y && y < m.y + static_cast<int32_t>(m.height_px)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool fs8::compositor::has_known_positions(std::span<monitor_info const> monitors) noexcept {
    for (auto const& m : monitors) {
        if (m.x != 0 || m.y != 0) {
            return true;
        }
    }
    return false;
}

void fs8::compositor::arrange_monitors_tiled(std::vector<monitor_info>& monitors) noexcept {
    if (monitors.empty()) {
        return;
    }

    // Sort by name for stable order
    std::sort(monitors.begin(), monitors.end(), [](monitor_info const& a, monitor_info const& b) noexcept {
        return a.name < b.name;
    });

    int32_t cursor_x = 0;
    for (auto& m : monitors) {
        m.x       = cursor_x;
        m.y       = 0;
        cursor_x += static_cast<int32_t>(m.width_px);
    }
}
