// Created for unstretch_tablet: monitor detection and geometry queries.

module;
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module fs8.compositor.monitor_detection;

export namespace fs8::compositor {

    /// Display server type detected at runtime.
    enum struct [[nodiscard]] display_server : std::uint8_t {
        unknown,
        wayland,
        x11,
    };

    /// Geometry of a single monitor/output.
    struct [[nodiscard]] monitor_info {
        std::string name;           // EDID display name or connector fallback
        std::string connector;      // DRM connector, e.g. "DP-1", "HDMI-A-2"
        int32_t     x          = 0; // position in compositor space
        int32_t     y          = 0;
        uint32_t    width_px   = 0; // active resolution in pixels
        uint32_t    height_px  = 0;
        uint32_t    width_mm   = 0; // physical size in mm (from EDID)
        uint32_t    height_mm  = 0;
        bool        is_primary = false;
        bool        is_enabled = false;
    };

    /// Result of monitor enumeration.
    struct [[nodiscard]] monitor_enum_result {
        std::vector<monitor_info> monitors{};
        display_server            server            = display_server::unknown;
        bool                      has_position_info = false; // true if x,y are from compositor
    };

    /// Detect the current display server (Wayland, X11, or unknown).
    [[nodiscard]] display_server detect_display_server() noexcept;

    /// Enumerate connected monitors using the best available method.
    ///
    /// Strategy:
    /// 1. sysfs + libdrm: enumerate connectors, get active mode and physical size
    /// 2. Wayland xdg-output: get compositor position (x,y)
    /// 3. X11 XRandR: fallback for position
    ///
    /// The returned monitor_info list always has at least one entry
    /// (the primary or first detected monitor).
    [[nodiscard]] monitor_enum_result enumerate_monitors() noexcept;

    /// Detect if two monitors are in mirror mode (cloned).
    ///
    /// Uses the position-overlap heuristic: two monitors at the same (x,y)
    /// with the same dimensions are likely mirrored.
    [[nodiscard]] inline bool are_monitors_mirrored(monitor_info const& a, monitor_info const& b) noexcept {
        return a.x == b.x && a.y == b.y && a.width_px == b.width_px && a.height_px == b.height_px;
    }

    /// Compute the combined desktop bounding box from a list of monitors.
    ///
    /// For extended mode, this is the union of all monitor rectangles.
    /// For mirror mode, this is a single monitor's rectangle.
    struct [[nodiscard]] desktop_bounds {
        int32_t  x = 0, y = 0;
        uint32_t width = 0, height = 0;
    };

    [[nodiscard]] desktop_bounds compute_desktop_bounds(std::span<monitor_info const> monitors) noexcept;

    /// If `preferred_name` is non-empty and matches a monitor's `name`, that
    /// monitor is returned. Otherwise the primary monitor is preferred, then
    /// the first enabled one, then the first entry. Returns nullptr only when
    /// `monitors` is empty.
    [[nodiscard]] inline monitor_info const* resolve_target_monitor(
      std::span<monitor_info const> monitors,
      std::string_view              preferred_name) noexcept {
        if (monitors.empty()) {
            return nullptr;
        }

        if (!preferred_name.empty()) {
            for (auto const& mon : monitors) {
                if (mon.name == preferred_name) {
                    return &mon;
                }
            }
        }

        for (auto const& mon : monitors) {
            if (mon.is_primary) {
                return &mon;
            }
        }
        for (auto const& mon : monitors) {
            if (mon.is_enabled) {
                return &mon;
            }
        }
        return &monitors.front();
    }

    /// Linear remap from a tablet's full range onto a single target monitor,
    /// expressed in tablet coordinate space.
    ///
    /// A value `V` in the tablet range is remapped as:
    ///   normalized = (V - tablet_min) / (tablet_max - tablet_min)
    ///   V' = offset + normalized * scale
    ///
    /// The compositor maps the tablet's full range across the combined
    /// desktop; remapping in tablet space lets a downstream compositor (or
    /// abs2rel) place the pen on `target` only.
    struct [[nodiscard]] tablet_remap {
        float offset_x = 0.0f;
        float offset_y = 0.0f;
        float scale_x  = 1.0f;
        float scale_y  = 1.0f;
    };

    [[nodiscard]] tablet_remap compute_tablet_remap(
      monitor_info const&   target,
      desktop_bounds const& desktop,
      int32_t               tablet_min_x,
      int32_t               tablet_max_x,
      int32_t               tablet_min_y,
      int32_t               tablet_max_y) noexcept;

    /// Find which monitor contains the point (x, y) in compositor desktop space.
    /// Returns the index, or -1 if the point is outside all monitors.
    [[nodiscard]] int monitor_at_position(std::span<monitor_info const> monitors, int32_t x, int32_t y) noexcept;

    /// Whether at least one monitor has a non-zero position (from a compositor
    /// that provides x,y layout info, e.g. XRandR).
    [[nodiscard]] bool has_known_positions(std::span<monitor_info const> monitors) noexcept;

    /// If no monitor has known positions, arrange them in scan order from (0,0),
    /// tiled left-to-right sorted by name.
    void arrange_monitors_tiled(std::vector<monitor_info>& monitors) noexcept;

} // namespace fs8::compositor
