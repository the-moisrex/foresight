// Created by moisrex on 9/17/26.

export module fs8.mods:pen2touch;
import fs8.context;
import fs8.event;
import fs8.pimpl;

export namespace fs8 {

    /// libinput's adaptive touchpad acceleration as the factor it
    /// multiplies a motion delta by, relative to its unaccelerated
    /// path (1.0 = plateau / no boost, >1 = fast-motion boost).
    ///
    /// Mirrors `touchpad_accel_profile_linear` (double incline with a
    /// plateau: linear ramp to 6 mm/s, 0.9 baseline to a 130 mm/s
    /// threshold, quadratic incline capped at 4x the threshold) as
    /// averaged by `calculate_acceleration_simpsons` over the current
    /// and previous frame's velocity — with libinput's default
    /// two-entry tracker, velocity is the instantaneous frame speed.
    /// `speed` and `last` are in mm/s (frame delta divided by the
    /// device's advertised axis resolution); `last` is 0 on the first
    /// frame of a contact.  Compensating means dividing by the result;
    /// callers clamp to >= 1 so libinput's slow-motion deceleration
    /// is left alone.
    [[nodiscard]] double touchpad_accel_gain(double speed, double last) noexcept;

    /// Convert a single stylus contact into a Linux Type-B multitouch contact.
    ///
    /// The source tablet reports:
    ///
    ///   BTN_TOOL_PEN / BTN_TOOL_RUBBER / ...
    ///   BTN_TOUCH
    ///   ABS_X / ABS_Y
    ///   ABS_PRESSURE
    ///
    /// This mod converts the tool identity to BTN_TOOL_FINGER — flipped
    /// to BTN_TOOL_DOUBLETAP while the scroll finger is open, since
    /// libinput derives its expected finger count from BTN_TOOL_* and
    /// leaves further contacts in the non-motion hovering state — and
    /// mirrors the tablet's absolute position/pressure into the Type-B
    /// multitouch protocol:
    ///
    ///   ABS_MT_SLOT            (slot 0: the contact; slot 1: the scroll finger)
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
    /// BTN_STYLUS3 is remapped onto the side mouse button, which the
    /// `profile_device` answer advertises on the virtual device
    /// alongside BTN_LEFT.  BTN_STYLUS, BTN_STYLUS2 (the barrel
    /// buttons; many pens only report BTN_STYLUS) and KEY_CAPSLOCK act
    /// as a *scroll trigger* instead: while any is held, a second MT
    /// contact (slot 1) mirrors the pen contact one eighth of the axis
    /// away, which libinput reads as a two-finger scroll — the cursor
    /// freezes, the tip click is suppressed, and motion keeps scrolling
    /// (including libinput's inertial scrolling) until the hold ends.  A
    /// quick tap of a barrel button (<200 ms without movement) still
    /// emits its real mouse click (BTN_RIGHT for BTN_STYLUS, BTN_MIDDLE
    /// for BTN_STYLUS2).  While that gesture runs, *both* contacts' X
    /// mirror around the engage position: libinput derives the scroll
    /// delta from the average of both contacts' motion, so flipping one
    /// side only would cancel out — this makes horizontal scroll follow
    /// the pen's direction instead of running it backwards.  Ending the
    /// scroll with the pen somewhere else rebases slot 0 through a fresh
    /// tracking id, so the jump back to the real position never reaches
    /// the cursor.  Tilt (ABS_TILT_X / ABS_TILT_Y) and tool width
    /// (ABS_TOOL_WIDTH) are dropped because they have no touchpad
    /// equivalent.
    ///
    /// Cursor speed is a parameter: `pen2touch[1.5f]` divides the axis
    /// resolution of the profiled touchpad by 1.5, which makes libinput
    /// turn every motion delta into 1.5x the pointer/scroll travel.
    /// Coordinates pass through raw — the mapping is one-to-one at any
    /// factor, so no wall or drift can appear mid-tablet.  Apps can
    /// override it at runtime with
    /// `pipeline.mod(pen2touch).speed(...)` before the pipeline runs.
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
    ///   | pen2touch          // default: 1:1 mapping
    ///   | output
    ///
    ///   | pen2touch[1.5f]    // cursor speed 1.5x
    /// @endcode
    constexpr struct [[nodiscard]] basic_pen2touch : pimpl_idiom<basic_pen2touch> {
        using pimpl_idiom::pimpl_idiom;

        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

        /// Scale cursor speed by `factor`: at `profile_device` time the
        /// axis resolution is divided by it, which is the divisor
        /// libinput derives pointer/scroll motion from — the emitted
        /// coordinates themselves never change.  Requires the source
        /// axes to advertise a resolution; 1.0 is the identity.
        constexpr explicit basic_pen2touch(float const speed) noexcept : speed_{speed} {}

        /// Build a variant with a different speed factor: `pen2touch[1.5f]`.
        consteval basic_pen2touch operator[](float const speed) const noexcept {
            return basic_pen2touch{speed};
        }

        /// Set the speed factor at runtime — apps call this on the
        /// built pipeline before `pipeline()`:
        /// `pipeline.mod(pen2touch).speed(1.5f)`.
        constexpr void speed(float const factor) noexcept {
            speed_ = factor;
        }

        /// Divide fast frames by libinput's own adaptive-acceleration
        /// gain (`touchpad_accel_gain`) so the cursor keeps the pen's
        /// speed instead of libinput's fast-motion boost.  Opt-in:
        /// off by default, apps turn it on unless the user passes
        /// their `--native-accel` escape hatch (non-default compositor
        /// accel-speed settings change the gain and defeat this).
        /// Motion is held to the frame's SYN_REPORT and divided there —
        /// libinput reads a contact's position only at the frame
        /// boundary — and passes through untouched while the scroll
        /// finger is open (two-finger frames never reach libinput's
        /// pointer accelerator).
        constexpr void accel_compensation(bool const on) noexcept {
            accel_comp_ = on;
        }

        /// Handle lifecycle tags (start, toggle_off, device_disconnected,
        /// profile_device); releases are forked through `dynamic_context`,
        /// which the pipeline binds while it runs.
        context_action operator()(control_event const& tag) noexcept;

        /// Translate a single input event; all state lives in the impl.
        context_action operator()(event_type& event) noexcept;

      private:
        /// Motion factor; `profile_device` divides the axis resolution
        /// by it when it reshapes the template.
        float speed_ = 1.0f;

        /// Whether to compensate libinput's fast-motion acceleration
        /// (see `accel_compensation`); consumed per event in `handle`.
        bool accel_comp_ = false;
    } pen2touch;

    static_assert(Modifier<basic_pen2touch>);

} // namespace fs8
