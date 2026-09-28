// Created by moisrex on 9/17/26.

export module fs8.mods:pen2touch;
import fs8.context;
import fs8.event;
import fs8.pimpl;

export namespace fs8 {

    /// Convert a single stylus contact into a Linux Type-B multitouch contact.
    ///
    /// The source tablet reports:
    ///
    ///   BTN_TOOL_PEN / BTN_TOOL_RUBBER / ...
    ///   BTN_TOUCH
    ///   ABS_X / ABS_Y
    ///   ABS_PRESSURE
    ///
    /// This mod converts the tool identity to BTN_TOOL_FINGER and mirrors
    /// the tablet's absolute position/pressure into the Type-B multitouch
    /// protocol:
    ///
    ///   ABS_MT_SLOT            (always 0 for a single-contact device)
    ///   ABS_MT_TRACKING_ID     (non-negative starts a contact, -1 ends it)
    ///   ABS_MT_POSITION_X
    ///   ABS_MT_POSITION_Y
    ///
    /// The MT contact opens when the pen enters proximity (BTN_TOOL_*) and
    /// closes when it leaves, so a hovering pen already moves the cursor.
    /// The output BTN_TOUCH therefore tracks the contact ("pen is near"),
    /// not the tip: libinput keeps a tracking-id contact in TOUCH_HOVERING
    /// (no motion) until it sees BTN_TOUCH, so passing the source tip state
    /// through would leave the cursor glued in place for the whole hover.
    ///
    /// The source tip press (BTN_TOUCH) instead becomes the physical left
    /// button BTN_LEFT: pressing the tip is the left click, and
    /// advertising BTN_LEFT flips libinput's tap-to-click default off, so
    /// hover enter/leave can never fire a spurious tap.  Tablets that
    /// never send BTN_TOOL_* fall back to the tip alone for the contact
    /// lifecycle.
    ///
    /// The legacy ABS_X / ABS_Y / ABS_PRESSURE events are retained because
    /// real touchpads commonly expose both the legacy single-touch interface
    /// and the Type-B MT interface.  Pressure is deliberately *not* mirrored
    /// into ABS_MT_PRESSURE: advertising it makes libinput switch to
    /// pressure-based touch detection (begin at 12% of the axis range) while
    /// treating any pressure above its palm threshold (~130) as a palm —
    /// pen pressure satisfies neither bound, so contacts would be ignored.
    ///
    /// The barrel buttons (BTN_STYLUS / BTN_STYLUS2 / BTN_STYLUS3) are
    /// remapped onto regular mouse buttons (BTN_RIGHT / BTN_MIDDLE /
    /// BTN_SIDE), which the `profile_device` answer advertises on the
    /// virtual device alongside BTN_LEFT.  Tilt (ABS_TILT_X / ABS_TILT_Y)
    /// and tool width (ABS_TOOL_WIDTH) are dropped because they have no
    /// touchpad equivalent.
    ///
    /// The mod also answers the `profile_device` control event: when the
    /// pipeline's `uinput` is about to create the virtual device, this mod
    /// reshapes the template (a clone of the tablet) into a multitouch
    /// touchpad — stripping the tablet tool buttons/axes, enabling the MT
    /// slots with proper absinfo, advertising the remapped barrel buttons,
    /// fixing the input properties, and giving the device the fixed name
    /// "Foresight Virtual Touchpad" — so libinput sees a touchpad instead
    /// of a second tablet.
    ///
    /// All per-instance state (the contact lifecycle, cached coordinates,
    /// held buttons) lives behind the pimpl idiom in `pen2touch.cxx`, and
    /// synthesized events are forked through `dynamic_context`.
    ///
    /// Place this *before* any mod that would consume ABS_X/ABS_Y (such as
    /// abs2rel) or *instead of* abs2rel when touchpad-style output is
    /// desired.
    ///
    /// @par Example
    /// @code
    ///   | intercept[tablet | required | grab]
    ///   | pen2touch
    ///   | output
    /// @endcode
    constexpr struct [[nodiscard]] basic_pen2touch : pimpl_idiom<basic_pen2touch> {
        using pimpl_idiom::pimpl_idiom;

        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

        /// Handle lifecycle tags (start, toggle_off, device_disconnected,
        /// profile_device); releases are forked through `dynamic_context`,
        /// which the pipeline binds while it runs.
        context_action operator()(control_event const& tag) noexcept;

        /// Translate a single input event; all state lives in the impl.
        context_action operator()(event_type& event) noexcept;
    } pen2touch;

    static_assert(Modifier<basic_pen2touch>);

} // namespace fs8
