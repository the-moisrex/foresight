// Created by moisrex on 9/17/26.

module;
#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cmath>
#include <initializer_list>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <utility>
module fs8.mods;
import fs8.devices.evdev;

using fs8::basic_pen2touch;
using fs8::evdev;

namespace {
    using code_type  = fs8::event_type::code_type;
    using value_type = fs8::event_type::value_type;

    /// Pick the source axis' absinfo when it exists, otherwise fall back
    /// to `fallback` (used when the template has no source device to
    /// copy the ranges from).
    [[nodiscard]] input_absinfo absinfo_from(evdev const& tmpl, code_type const source, input_absinfo const& fallback) noexcept {
        if (auto const* info = tmpl.abs_info(source); info != nullptr) {
            return *info;
        }
        return fallback;
    }

    /// Reshape the not-yet-created virtual device so it advertises as a
    /// two-contact multitouch touchpad instead of a tablet clone.
    /// Called through the `profile_device` control event from uinput's
    /// finalize_device, before the template hits /dev/uinput.
    void profile_template(evdev& tmpl) noexcept {
        // ── Strip the tablet identity ──────────────────────────────
        // Tool proximity + stylus buttons: no touchpad equivalent (the
        // mod renames the tool to BTN_TOOL_FINGER / BTN_TOOL_DOUBLETAP
        // at runtime, so those keys must be the ones advertised).
        for (code_type const btn :
             std::initializer_list<code_type>{
               BTN_TOOL_PEN,
               BTN_TOOL_RUBBER,
               BTN_TOOL_BRUSH,
               BTN_TOOL_PENCIL,
               BTN_TOOL_AIRBRUSH,
               BTN_TOOL_MOUSE,
               BTN_TOOL_LENS,
               BTN_STYLUS,
               BTN_STYLUS2,
               BTN_STYLUS3})
        {
            if (tmpl.has_event_code(EV_KEY, btn)) {
                tmpl.disable_event_code(EV_KEY, btn);
            }
        }
        // Stylus-only axes.
        for (code_type const code : std::initializer_list<code_type>{ABS_TILT_X, ABS_TILT_Y, ABS_TOOL_WIDTH}) {
            if (tmpl.has_event_code(EV_ABS, code)) {
                tmpl.disable_event_code(EV_ABS, code);
            }
        }
        // The parts of the touch profile we actually emit.
        tmpl.enable_event_code(EV_KEY, BTN_TOUCH);
        tmpl.enable_event_code(EV_KEY, BTN_TOOL_FINGER);
        // Two contacts are announced via the legacy two-finger key:
        // libinput derives its expected finger count from BTN_TOOL_*
        // and keeps extra slots TOUCH_HOVERING (inert) without it.
        tmpl.enable_event_code(EV_KEY, BTN_TOOL_DOUBLETAP);
        // The barrel buttons become regular mouse buttons (the mod remaps
        // BTN_STYLUS{,2,3} at runtime).  BTN_LEFT is the pen tip press: the
        // mod rewrites the source BTN_TOUCH into it, and advertising it flips
        // libinput's tap-to-click default off — with the contact following
        // proximity, tap-to-click would misfire on hover enter/leave.
        tmpl.enable_event_code(EV_KEY, BTN_RIGHT);
        tmpl.enable_event_code(EV_KEY, BTN_MIDDLE);
        tmpl.enable_event_code(EV_KEY, BTN_SIDE);
        tmpl.enable_event_code(EV_KEY, BTN_LEFT);

        // A fixed identity: finalize_device has already stamped a "(Virtual)"
        // clone of the tablet's name on the template, but inheriting the
        // tablet's name would still masquerade as hardware and could match
        // libinput quirks keyed on vendor/model strings.
        tmpl.device_name("Foresight Virtual Touchpad");

        // ── Multitouch absinfo ─────────────────────────────────────
        // Two contacts: slot 0 is the pen, slot 1 the synthetic scroll
        // finger (a few spare slots keep the kernel from clamping).
        static constexpr input_absinfo slot_info{.value = 0, .minimum = 0, .maximum = 4, .fuzz = 0, .flat = 0, .resolution = 0};
        static constexpr input_absinfo tracking_info{.value = 0, .minimum = 0, .maximum = 65'535, .fuzz = 0, .flat = 0, .resolution = 0};
        static constexpr input_absinfo xy_fallback{.value = 0, .minimum = 0, .maximum = 32'767, .fuzz = 0, .flat = 0, .resolution = 0};
        static constexpr input_absinfo pressure_fallback{.value = 0, .minimum = 0, .maximum = 1023, .fuzz = 0, .flat = 0, .resolution = 0};

        // Position mirrors the source axis one-to-one, so copy the range when
        // the source is part of the template.
        tmpl.abs_info(ABS_MT_SLOT, slot_info);
        tmpl.abs_info(ABS_MT_TRACKING_ID, tracking_info);
        tmpl.abs_info(ABS_MT_POSITION_X, absinfo_from(tmpl, ABS_X, xy_fallback));
        tmpl.abs_info(ABS_MT_POSITION_Y, absinfo_from(tmpl, ABS_Y, xy_fallback));
        // ABS_MT_PRESSURE must stay off: a multitouch device advertising it
        // makes libinput switch to pressure-based touch detection (begin at 12%
        // of the range) and treat pressure above its palm threshold (~130) as a
        // palm — pen pressure satisfies neither bound, so libinput would drop
        // every contact before it ever becomes a pointer event.
        if (tmpl.has_event_code(EV_ABS, ABS_MT_PRESSURE)) {
            tmpl.disable_event_code(EV_ABS, ABS_MT_PRESSURE);
        }

        // The mod also forwards the legacy single-touch axes.
        if (!tmpl.has_abs_info(ABS_X)) {
            tmpl.abs_info(ABS_X, xy_fallback);
        }
        if (!tmpl.has_abs_info(ABS_Y)) {
            tmpl.abs_info(ABS_Y, xy_fallback);
        }
        if (!tmpl.has_abs_info(ABS_PRESSURE)) {
            tmpl.abs_info(ABS_PRESSURE, pressure_fallback);
        }

        // ── Input properties ───────────────────────────────────────
        // Inherited properties (notably INPUT_PROP_DIRECT from direct
        // tablets) would make libinput classify the device as a
        // touchscreen; a touchpad is a POINTER.
        for (unsigned prop = 0; prop <= INPUT_PROP_MAX; ++prop) {
            if (prop == INPUT_PROP_POINTER) {
                continue;
            }
            if (tmpl.has_property(prop)) {
                tmpl.disable_property(prop);
            }
        }
        tmpl.enable_property(INPUT_PROP_POINTER);
    }

    /// libinput's `touchpad_accel_profile_linear` shape: a linear ramp to
    /// 6 mm/s, the 0.9 plateau up to 130 mm/s, then a quadratic incline
    /// capped at 4x the threshold.
    [[nodiscard]] double accel_curve(double const v) noexcept {
        if (v < 6.0) {
            return 0.1 * v + 0.3;
        }
        if (v < 130.0) {
            return 0.9;
        }
        double const capped = v < 520.0 ? v : 520.0;
        return 0.0025 * (capped / 130.0) * (capped - 130.0) + 0.9;
    }

    /// The simpsons average of `accel_curve` over `last`..`speed`
    /// normalized by the plateau.  `curve_last` is precomputed by the
    /// caller: it does not depend on `speed`, so a caller iterating on
    /// `speed` (the bisection in `accel_divisor`) evaluates the curve
    /// twice per step instead of three times.
    [[nodiscard]] double accel_gain(double const speed, double const last, double const curve_last) noexcept {
        double const averaged = (accel_curve(speed) + curve_last + 4.0 * accel_curve((speed + last) * 0.5)) / 6.0;
        return averaged / 0.9;
    }
} // namespace

double fs8::touchpad_accel_gain(double const speed, double const last) noexcept {
    return accel_gain(speed, last, accel_curve(last));
}

template <>
struct fs8::pimpl_idiom<basic_pen2touch>::impl {
    using event_type = event_type;

    /// Fork one event to the downstream mods through the pipeline's
    /// currently bound dynamic context.
    static void emit(code_type const type, code_type const code, value_type const value) noexcept {
        std::ignore = dynamic_context.fork_emit(event_type{type, code, value});
    }

    /// Current tracking ID.  Non-negative means a contact is active;
    /// -1 means no contact.
    value_type tracking_id_ = -1;

    /// Last-seen raw coordinates, cached so we can emit them alongside
    /// the MT equivalents.
    value_type x_ = 0;
    value_type y_ = 0;

    /// Whether the pen tool is in proximity (BTN_TOOL_* == 1).
    bool tool_active_ = false;

    /// Whether the MT contact is open.  Follows the tool (proximity),
    /// not the tip: hovering already moves the cursor.
    bool touching_ = false;

    /// Whether the pen tip is pressed (source BTN_TOUCH == 1); the
    /// virtual device's BTN_LEFT follows this state.
    bool tip_down_ = false;

    /// Whether BTN_LEFT is currently *emitted*.  Differs from the tip
    /// while a scroll hold suppresses the click: the tip can be down
    /// with no left button out there.
    bool left_down_ = false;

    /// Whether BTN_MIDDLE is currently *emitted* by the tip: clicking
    /// the tip while a scroll is active becomes a middle click (the tip
    /// can never start a left drag mid-scroll).  The release must track
    /// this, not the scroll state, so it lifts the right button even
    /// after the scroll has ended.
    bool middle_down_ = false;

    /// Whether we have received at least one ABS_X / ABS_Y since the
    /// last touch-down, so we know the values are valid.
    bool have_x_ = false;
    bool have_y_ = false;

    /// A tap's button code waiting for its release: libinput diffs
    /// physical buttons across frames, so press and release must not
    /// share one frame (a same-frame pair nets to no change and is
    /// dropped).  0 = nothing pending.
    code_type pending_tap_release_ = 0;

    /// The mouse buttons the BTN_STYLUS* codes are remapped to.
    static constexpr std::array<code_type, 3> barrel_buttons{BTN_RIGHT, BTN_MIDDLE, BTN_SIDE};

    /// Index into `barrel_buttons` for a stylus-button code (the
    /// BTN_STYLUS* codes are not contiguous, so no arithmetic).
    [[nodiscard]] static constexpr std::size_t barrel_index(code_type const code) noexcept {
        switch (code) {
            case BTN_STYLUS: return 0;
            case BTN_STYLUS2: return 1;
            case BTN_STYLUS3: return 2;
            default: std::unreachable();
        }
    }

    /// Which mapped buttons are currently held, so cleanup can release
    /// a press whose release event never arrived.
    std::array<bool, 3> barrel_held_{};

    /// Slot reported in MT events; slot 0 is the pen contact, slot 1
    /// the synthetic scroll finger.
    static constexpr value_type mt_slot     = 0;
    static constexpr value_type scroll_slot = 1;

    /// The slot the output currently has selected, or -1 when we never
    /// selected one (fresh pimpl, or after `reset` — the virtual
    /// device's kernel-side selection survives a restart, so forget it
    /// too and re-select defensively).
    value_type selected_slot_ = -1;

    /// Select `s` for the MT events that follow, emitting ABS_MT_SLOT
    /// only when it differs from the last selection: re-selecting the
    /// current slot is a no-op for consumers but still costs a
    /// `fork_emit` walk plus a `write()` syscall per frame.
    void select_slot(value_type const s) noexcept {
        if (selected_slot_ != s) {
            emit(EV_ABS, ABS_MT_SLOT, s);
            selected_slot_ = s;
        }
    }

    /// Scroll mode: while active a second contact (slot 1) mirrors the
    /// pen contact, which libinput reads as a two-finger scroll.
    bool scroll_active_ = false;

    /// The synthetic scroll contact's tracking id (-1 = lifted).
    value_type scroll_id_ = -1;

    /// A barrel button buffered for the tap-vs-scroll decision, indexed
    /// like `barrel_held_`.  Nothing is emitted until release: a quick
    /// unused tap clicks, a hold (or a hold with movement) scrolls.
    /// BTN_STYLUS and BTN_STYLUS2 both trigger (many pens only report
    /// BTN_STYLUS); BTN_STYLUS3 passes through immediately.
    struct trigger_state {
        bool                      held       = false;
        bool                      used       = false;
        std::chrono::microseconds press_time = {};
    };

    std::array<trigger_state, 3> triggers_{};

    /// Whether any trigger barrel button is down right now.  Movement
    /// marks every held trigger as used on each moved axis, so this
    /// keeps the common (nothing held) case a single predicted branch
    /// instead of a three-entry scan per axis event.
    bool any_trigger_held_ = false;

    /// How long a barrel button must stay down before its release stops
    /// counting as a tap (the same 200 ms window `on_held` uses).
    static constexpr std::chrono::milliseconds tap_window{200};

    /// Capslock hold — the second scroll trigger (on_held downstream
    /// owns the swallow/LED behavior; this only mirrors the hold).
    bool caps_held_ = false;

    /// Scroll-finger geometry: its X sits one eighth of the axis away
    /// from the pen contact.  Defaults match the unprofiled fallback
    /// range; `profile_device` overwrites them with the real axis.
    value_type scroll_offset_x_ = 32'767 / 8;
    value_type scroll_min_x_    = 0;
    value_type scroll_max_x_    = 32'767;

    /// The X position the scroll-finger open mirrored around: the pen
    /// coordinate at the moment slot 1 appeared (or at the first X seen
    /// while it is open, when it opened before any position arrived).
    /// The mirror equals the raw value here, so flipping X never kicks
    /// a delta into a running gesture.
    value_type scroll_anchor_x_     = 0;
    bool       scroll_anchor_valid_ = false;

    /// The current event's timestamp, sampled at the top of every
    /// `handle` — all compensation timing is read from it.
    std::chrono::microseconds now_{};

    /// Motion held back until the frame's SYN_REPORT, where
    /// compensation divides it by libinput's own gain (see
    /// `accel_compensation`).  Only the pointer path holds motion
    /// back: two-finger frames bypass libinput's pointer accelerator,
    /// so they are never deferred.
    double comp_dx_ = 0;
    double comp_dy_ = 0;

    /// When the last compensated frame was flushed, and the velocity
    /// it ended on — the previous-frame half of the simpsons average
    /// libinput's gain runs over (libinput feeds the tracker the
    /// deltas it received, i.e. our emitted ones).
    std::chrono::microseconds comp_last_syn_{};
    double                    comp_last_vel_ = 0;

    /// The first flush of a contact is reported raw: libinput
    /// baselines on a fresh touch before any gain applies to it.
    bool comp_first_flush_ = true;

    /// Slot 0's last emitted position, so a compensated frame moves
    /// the contact incrementally from where libinput saw it.
    value_type emitted_x_ = 0;
    value_type emitted_y_ = 0;

    /// The profiled virtual device's axis resolution (units/mm), the
    /// scale libinput derives mm/s velocity from.  Read after the
    /// speed factor has divided it (the factor rides on the
    /// resolution); 0 = nothing to scale against, compensation stays
    /// off.  Configuration, survives `reset`.
    double res_x_ = 0;
    double res_y_ = 0;

    /// Monotonically increasing tracking ID generator.  Bounded by the
    /// advertised ABS_MT_TRACKING_ID range (0..65535) so the kernel never
    /// clamps or rejects an id.
    [[nodiscard]] value_type next_tracking_id() noexcept {
        if (tracking_id_ >= 65'535) {
            tracking_id_ = 0;
        } else {
            ++tracking_id_;
        }
        return tracking_id_;
    }

    /// Reset all internal state (called on start / toggle_off /
    /// device_disconnected).  The profiled scroll geometry is
    /// configuration, not per-run state, and survives.
    void reset() noexcept {
        tracking_id_ = -1;
        x_           = 0;
        y_           = 0;
        tool_active_ = false;
        touching_    = false;
        tip_down_    = false;
        left_down_   = false;
        middle_down_ = false;
        have_x_      = false;
        have_y_      = false;
        barrel_held_.fill(false);
        scroll_active_       = false;
        scroll_id_           = -1;
        scroll_anchor_x_     = 0;
        scroll_anchor_valid_ = false;
        pending_tap_release_ = 0;
        for (auto& trigger : triggers_) {
            trigger = {};
        }
        any_trigger_held_ = false;
        caps_held_        = false;
        now_              = {};
        comp_dx_          = 0;
        comp_dy_          = 0;
        comp_last_syn_    = {};
        comp_last_vel_    = 0;
        comp_first_flush_ = true;
        emitted_x_        = 0;
        emitted_y_        = 0;
        // The virtual device keeps its kernel-side slot selection across a
        // restart; forget ours too so the next contact re-selects it.
        selected_slot_    = -1;
        // res_x_/res_y_ are configuration (the profiled device), not state.
    }

    /// Begin a new MT contact (ABS_MT_TRACKING_ID >= 0).
    void begin_contact() noexcept {
        if (touching_) {
            return;
        }
        touching_ = true;

        // A fresh contact is a fresh compensation context: libinput
        // restarts its trackers on touch begin, and the opening
        // frame reaches it raw.
        comp_dx_          = 0;
        comp_dy_          = 0;
        comp_last_syn_    = now_;
        comp_last_vel_    = 0;
        comp_first_flush_ = true;

        value_type const id = next_tracking_id();

        select_slot(mt_slot);
        emit(EV_ABS, ABS_MT_TRACKING_ID, id);

        if (have_x_) {
            emit(EV_ABS, ABS_MT_POSITION_X, x_);
            emitted_x_ = x_;
        }
        if (have_y_) {
            emit(EV_ABS, ABS_MT_POSITION_Y, y_);
            emitted_y_ = y_;
        }
    }

    /// End the current MT contact (ABS_MT_TRACKING_ID = -1).
    void end_contact() noexcept {
        if (!touching_) {
            return;
        }
        // Every MT event belongs to the currently selected slot; lift on
        // slot 0 (the re-select only emits when it is not already there).
        select_slot(mt_slot);
        emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
        touching_ = false;
        have_x_   = false;
        have_y_   = false;
        comp_dx_  = 0;
        comp_dy_  = 0;
    }

    /// X position of the synthetic scroll finger: the pen contact's X
    /// pushed one eighth of the axis to the side, flipped near the edge
    /// so it stays inside the advertised range.
    [[nodiscard]] value_type scroll_x() const noexcept {
        value_type const right = x_ + scroll_offset_x_;
        if (right <= scroll_max_x_) {
            return right;
        }
        value_type const left = x_ - scroll_offset_x_;
        if (left >= scroll_min_x_) {
            return left;
        }
        return x_;
    }

    /// Mirror an X position around the engage anchor.  libinput derives
    /// the scroll delta from the *average* of both contacts' motion, so
    /// horizontal scroll only changes direction when both slots mirror
    /// together; at the anchor the mirror equals the raw value, so the
    /// flip never injects a delta of its own.  Values may leave the
    /// advertised range this way — scroll is delta-based and the kernel
    /// does not clamp injected ABS events.
    [[nodiscard]] value_type mirrored_x(value_type const x) const noexcept {
        return 2 * scroll_anchor_x_ - x;
    }

    /// Open the synthetic scroll contact next to the pen contact.
    void begin_scroll_finger() noexcept {
        if (scroll_id_ >= 0 || !scroll_active_ || !touching_) {
            return;
        }
        // Any motion held back for the pointer path belongs to this
        // frame, not to the gesture: hand it over raw before the
        // mirror opens (the gesture itself feeds libinput's constant
        // scroll filter, never the pointer accelerator).
        flush_raw_pending();
        scroll_id_           = next_tracking_id();
        scroll_anchor_x_     = x_;
        scroll_anchor_valid_ = have_x_;

        select_slot(scroll_slot);
        emit(EV_ABS, ABS_MT_TRACKING_ID, scroll_id_);
        if (have_x_) {
            emit(EV_ABS, ABS_MT_POSITION_X, mirrored_x(scroll_x()));
        }
        if (have_y_) {
            emit(EV_ABS, ABS_MT_POSITION_Y, y_);
        }
        // Leave the pen's slot selected for whatever comes next.
        select_slot(mt_slot);
        // Two contacts: light the legacy two-finger key and drop the
        // one-finger key.  libinput resolves the tool keys once per
        // frame, so only the frame-final state matters — exactly one of
        // the two may be lit (it logs a kernel bug otherwise).
        emit(EV_KEY, BTN_TOOL_DOUBLETAP, 1);
        emit(EV_KEY, BTN_TOOL_FINGER, 0);
    }

    /// Lift the synthetic scroll contact (no-op when it is already up).
    /// `rebase` additionally snaps the pen contact back to its real X
    /// through a fresh tracking id (see `rebase_pen_contact`); contact
    /// teardown passes false — the contact dies anyway.
    void end_scroll_finger(bool const rebase) noexcept {
        if (scroll_id_ < 0) {
            return;
        }
        select_slot(scroll_slot);
        emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
        select_slot(mt_slot);
        scroll_id_     = -1;
        // The gesture rode libinput's constant scroll filter; the
        // pointer accelerator's frame history ends with it.
        comp_last_vel_ = 0;
        // Back to one contact (or none): FINGER again only while the
        // tool is still near, and never keep DOUBLETAP lit.
        emit(EV_KEY, BTN_TOOL_FINGER, tool_active_ ? 1 : 0);
        emit(EV_KEY, BTN_TOOL_DOUBLETAP, 0);
        if (rebase) {
            rebase_pen_contact();
        }
    }

    /// Hand the pen contact a fresh tracking id (announced with its
    /// real X) when the mirror would otherwise snap back at scroll end:
    /// the id change makes libinput baseline on the current point
    /// instead of reading the jump as pointer motion.  Only X is
    /// re-emitted — Y never changed representation, and the slot keeps
    /// its stored values across the id change.  No-op when the pen
    /// never left the anchor — there is no jump to hide.
    void rebase_pen_contact() noexcept {
        bool const jump      = scroll_anchor_valid_ && touching_ && x_ != scroll_anchor_x_;
        scroll_anchor_valid_ = false;
        if (!jump) {
            return;
        }
        select_slot(mt_slot);
        emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
        emit(EV_ABS, ABS_MT_TRACKING_ID, next_tracking_id());
        emit(EV_ABS, ABS_MT_POSITION_X, x_);
        emitted_x_     = x_;   // the slot holds the real position now
        comp_last_syn_ = now_; // libinput restarts its trackers on the id change
    }

    /// Movement while a barrel button is held turns that hold into a
    /// scroll: its release will never click.
    void mark_triggers_used() noexcept {
        if (!any_trigger_held_) [[likely]] {
            return;
        }
        for (auto& trigger : triggers_) {
            if (trigger.held) {
                trigger.used = true;
            }
        }
    }

    /// Enter/leave scroll mode.  Engaging lifts a running left-button
    /// drag (the tip click is suppressed for as long as the hold lasts)
    /// and opens the scroll finger; releasing it just lifts the finger.
    void update_scroll() noexcept {
        bool const want = caps_held_ || triggers_[barrel_index(BTN_STYLUS)].held || triggers_[barrel_index(BTN_STYLUS2)].held;
        if (want == scroll_active_) {
            return;
        }
        scroll_active_ = want;
        if (!scroll_active_) {
            // The gesture is over but the contact stays: snap the pen
            // back to its real position without a visible jump.
            end_scroll_finger(true);
            return;
        }
        if (left_down_) {
            emit(EV_KEY, BTN_LEFT, 0);
            left_down_ = false;
        }
        if (touching_) {
            begin_scroll_finger();
        }
    }

    /// Open the MT contact (if closed) and announce it with the output
    /// BTN_TOUCH=1.  libinput needs both: the tracking id alone only
    /// yields a TOUCH_HOVERING slot that never moves the cursor.
    void open_contact() noexcept {
        if (touching_) {
            return;
        }
        begin_contact();
        if (scroll_active_) {
            begin_scroll_finger();
        }
        emit(EV_KEY, BTN_TOUCH, 1);
    }

    /// Close the MT contact (if open), dropping the output BTN_TOUCH
    /// first so libinput hovers the slot before the tracking id dies.
    void close_contact() noexcept {
        if (!touching_) {
            return;
        }
        end_scroll_finger(false);
        emit(EV_KEY, BTN_TOUCH, 0);
        end_contact();
    }

    /// Release everything we currently hold (contact + tool + tip +
    /// barrel buttons) as a self-contained event frame.  Used when the
    /// pipeline is restarted, disabled, or loses its source device
    /// mid-contact: consumers must never see a stuck tracking ID, a
    /// held BTN_TOOL_FINGER, a held left/middle button, or a held
    /// right-click.
    void release_stale_state() noexcept {
        bool any = touching_ || tool_active_ || tip_down_ || scroll_id_ >= 0;
        if (pending_tap_release_ != 0) {
            emit(EV_KEY, pending_tap_release_, 0);
            pending_tap_release_ = 0;
            any                  = true;
        }
        for (std::size_t i = 0; i < barrel_held_.size(); ++i) {
            if (barrel_held_[i]) {
                emit(EV_KEY, barrel_buttons[i], 0);
                barrel_held_[i] = false;
                any             = true;
            }
        }
        if (left_down_) {
            emit(EV_KEY, BTN_LEFT, 0);
            left_down_ = false;
            any        = true;
        }
        if (middle_down_) {
            emit(EV_KEY, BTN_MIDDLE, 0);
            middle_down_ = false;
            any          = true;
        }
        if (!any) {
            return;
        }
        if (touching_) {
            end_scroll_finger(false);
            emit(EV_KEY, BTN_TOUCH, 0);
            end_contact();
        }
        if (tool_active_) {
            emit(EV_KEY, BTN_TOOL_FINGER, 0);
        }
        // Control events carry no frame of their own, so close the one
        // we just synthesized — otherwise the sanitizer may drop the
        // bare key/ABS writes for lacking a SYN_REPORT.
        emit(EV_SYN, SYN_REPORT, 0);
    }

    /// Is this motion frame held back for compensation?  Only the
    /// pointer path benefits: two-finger frames feed libinput's
    /// constant scroll filter, and axes without a profiled
    /// resolution leave us no mm/s to work with.
    [[nodiscard]] bool comp_defer(bool const accel_comp) const noexcept {
        return accel_comp && scroll_id_ < 0 && touching_ && res_x_ > 0 && res_y_ > 0;
    }

    /// Frame speed in mm/s the way libinput's two-entry tracker reads
    /// it: the delta over the profiled axis resolution, per elapsed
    /// time — clamped to libinput's 1 s motion timeout, and +1 µs
    /// guards a zero dt.  Reads the *emitted* position history, like
    /// libinput's own trackers.
    [[nodiscard]] double velocity(double const dx, double const dy) const noexcept {
        auto const dt = std::clamp(now_ - comp_last_syn_, std::chrono::microseconds::zero(), std::chrono::microseconds{1'000'000});
        return std::hypot(dx / res_x_, dy / res_y_) * 1e6 / (static_cast<double>(dt.count()) + 1.0);
    }

    /// The divisor that cancels libinput's own acceleration: solve
    /// `gain(v / d, last) == d`, the scale at which the gain libinput
    /// applies to our emitted frame multiplies back to the pen's
    /// motion.  Bisection — the direct fixed-point iteration
    /// oscillates around the root — over [1, 6], which brackets it
    /// since the gain curve tops out at ~5.34.  At or below the
    /// deceleration floor (gain <= 1) libinput already slows the
    /// frame: leave it alone.
    ///
    /// Twenty steps shrink the bracket to 5 / 2^20 ~= 4.8e-6, orders
    /// of magnitude below the integer rounding the result feeds, and
    /// `curve(last)` is hoisted out of the loop (it is invariant in
    /// `mid`), so a compensated frame costs ~40 curve evaluations
    /// instead of ~135.
    [[nodiscard]] static double accel_divisor(double const v_raw, double const last) noexcept {
        double const curve_last = accel_curve(last);
        if (accel_gain(v_raw, last, curve_last) <= 1.0) {
            return 1.0;
        }
        double lo = 1.0;
        double hi = 6.0;
        for (int i = 0; i < 20; ++i) {
            double const mid = (lo + hi) * 0.5;
            if (accel_gain(v_raw / mid, last, curve_last) > mid) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        return (lo + hi) * 0.5;
    }

    /// Hand held-back motion over untouched — the absolute position
    /// an uncompensated stream would have reported.  Used for the
    /// contact's baseline frame and for the frame that opens the
    /// scroll finger.
    void flush_raw_pending() noexcept {
        if (comp_dx_ == 0 && comp_dy_ == 0) {
            return;
        }
        double const dx  = comp_dx_;
        double const dy  = comp_dy_;
        comp_dx_         = 0;
        comp_dy_         = 0;
        double const vel = velocity(dx, dy); // before the syn below moves it
        select_slot(mt_slot);
        if (dx != 0 && have_x_) {
            emit(EV_ABS, ABS_MT_POSITION_X, x_);
            emitted_x_ = x_;
        }
        if (dy != 0 && have_y_) {
            emit(EV_ABS, ABS_MT_POSITION_Y, y_);
            emitted_y_ = y_;
        }
        comp_last_vel_ = vel;
        comp_last_syn_ = now_;
    }

    /// Consume the frame's held-back motion at its SYN_REPORT: the
    /// frame boundary is where libinput reads a contact's position,
    /// so dividing there is indistinguishable from a slower pen.
    void flush_accel() noexcept {
        if (comp_dx_ == 0 && comp_dy_ == 0) {
            return;
        }
        if (comp_first_flush_) {
            comp_first_flush_ = false;
            flush_raw_pending();
            return;
        }
        double const dx      = comp_dx_;
        double const dy      = comp_dy_;
        comp_dx_             = 0;
        comp_dy_             = 0;
        double const divisor = accel_divisor(velocity(dx, dy), comp_last_vel_);
        auto const   out_dx  = static_cast<value_type>(std::llround(dx / divisor));
        auto const   out_dy  = static_cast<value_type>(std::llround(dy / divisor));
        if (out_dx == 0 && out_dy == 0) {
            return;
        }
        select_slot(mt_slot);
        if (out_dx != 0) {
            emitted_x_ = static_cast<value_type>(emitted_x_ + out_dx);
            emit(EV_ABS, ABS_MT_POSITION_X, emitted_x_);
        }
        if (out_dy != 0) {
            emitted_y_ = static_cast<value_type>(emitted_y_ + out_dy);
            emit(EV_ABS, ABS_MT_POSITION_Y, emitted_y_);
        }
        comp_last_vel_ = velocity(out_dx, out_dy); // before the syn below moves it
        comp_last_syn_ = now_;
    }

    /// Translate a single input event.  `accel_comp` is the mod's
    /// runtime switch (`accel_compensation`), re-read per event so an
    /// app can flip it on the built pipeline before the run starts.
    context_action handle(event_type& event, bool accel_comp) noexcept;
};

fs8::context_action fs8::pimpl_idiom<basic_pen2touch>::impl::handle(event_type& event, bool const accel_comp) noexcept {
    using enum context_action;

    now_ = event.micro_time();

    // Deliver a deferred tap release first so it lands in a later
    // frame than its press (normally the source SYN follows right
    // after and closes the frame for us).
    if (pending_tap_release_ != 0) {
        emit(EV_KEY, pending_tap_release_, 0);
        pending_tap_release_ = 0;
    }

    switch (event.type()) {
        // ── Keys ──────────────────────────────────────────────────
        case EV_KEY:
            switch (event.code()) {
                // Tablet proximity / tool state -> touchpad contact
                // state.  Proximity opens the MT contact: a
                // hovering pen must move the cursor, and libinput
                // produces no motion from a TOUCH_HOVERING slot.
                // The output BTN_TOUCH belongs to the contact
                // ("pen is near"), so it rises/falls here too.
                case BTN_TOOL_PEN:
                case BTN_TOOL_RUBBER:
                case BTN_TOOL_BRUSH:
                case BTN_TOOL_PENCIL:
                case BTN_TOOL_AIRBRUSH:
                case BTN_TOOL_MOUSE:
                case BTN_TOOL_LENS:
                    tool_active_ = event.value() != 0;
                    if (tool_active_) {
                        open_contact();
                    } else {
                        // The tip cannot stay down once the tool is
                        // gone; the tablet may never send
                        // BTN_TOUCH=0 afterwards, so release it
                        // ourselves.
                        if (left_down_) {
                            emit(EV_KEY, BTN_LEFT, 0);
                            left_down_ = false;
                        }
                        if (middle_down_) {
                            emit(EV_KEY, BTN_MIDDLE, 0);
                            middle_down_ = false;
                        }
                        tip_down_ = false;
                        close_contact();
                    }
                    // Rename to the tool key of the *current* contact
                    // count: while the scroll finger is open the frame
                    // must end on DOUBLETAP, not FINGER (contact open
                    // above may already have flipped them).
                    event.code(scroll_id_ >= 0 ? BTN_TOOL_DOUBLETAP : BTN_TOOL_FINGER);
                    return next;

                // The source tip press is the physical left button.
                // The output BTN_TOUCH is owned by the contact
                // (proximity), so the source one never passes
                // through.  The tip also doubles as the
                // contact-begin/end signal on tablets that never
                // send BTN_TOOL_*.  While a scroll trigger is held
                // the tip becomes a middle click instead — it can
                // never start a left drag mid-scroll, but it should
                // not be dead either.
                case BTN_TOUCH: {
                    tip_down_ = event.value() != 0;
                    if (tip_down_) {
                        open_contact();
                    } else if (!tool_active_) {
                        close_contact();
                    }
                    if (tip_down_) {
                        if (scroll_active_) {
                            // A repeated press while already down stays
                            // dropped, like the old suppression.
                            if (middle_down_) {
                                return drop_event;
                            }
                            middle_down_ = true;
                            event.code(BTN_MIDDLE);
                            return next;
                        }
                        left_down_ = true;
                        event.code(BTN_LEFT);
                        return next;
                    }
                    // Release whichever button the press actually
                    // emitted; a press that was suppressed (the scroll
                    // started after it and ended before this release)
                    // has nothing to lift.
                    if (middle_down_) {
                        middle_down_ = false;
                        event.code(BTN_MIDDLE);
                        return next;
                    }
                    if (!left_down_) {
                        return drop_event;
                    }
                    left_down_ = false;
                    event.code(BTN_LEFT);
                    return next;
                }

                // The third barrel button becomes a regular mouse
                // button (the profile advertises it on the virtual
                // device).  Track the hold so cleanup can release a
                // press whose release event never arrived.
                case BTN_STYLUS3: {
                    auto const index    = barrel_index(event.code());
                    barrel_held_[index] = event.value() != 0;
                    event.code(barrel_buttons[index]);
                    return next;
                }

                // The other barrel buttons double as the scroll
                // trigger.  They are buffered instead of passed
                // through: a quick unused tap becomes a real
                // right/middle click, any longer hold (or a hold with
                // movement) turns into a two-finger scroll and swallows
                // the release.  BTN_STYLUS triggers too, because many
                // pens (including single-button ones) only report it.
                case BTN_STYLUS:
                case BTN_STYLUS2: {
                    auto& trigger = triggers_[barrel_index(event.code())];
                    if (event.value() != 0) {
                        if (!trigger.held) {
                            trigger.held       = true;
                            trigger.used       = false;
                            trigger.press_time = event.micro_time();
                            any_trigger_held_  = true;
                        }
                        update_scroll();
                        return drop_event;
                    }
                    if (!trigger.held) {
                        return drop_event;
                    }
                    trigger.held      = false;
                    any_trigger_held_ = false;
                    for (auto const& held_trigger : triggers_) {
                        any_trigger_held_ = any_trigger_held_ || held_trigger.held;
                    }
                    bool const tap = !trigger.used && (event.micro_time() - trigger.press_time < tap_window);
                    update_scroll();
                    if (tap) {
                        // Split the click across frames: libinput
                        // diffs physical buttons frame-to-frame, so a
                        // same-frame press+release nets to no change
                        // and is dropped.  handle() flushes the
                        // release on the next event.
                        emit(EV_KEY, barrel_buttons[barrel_index(event.code())], 1);
                        emit(EV_SYN, SYN_REPORT, 0);
                        pending_tap_release_ = barrel_buttons[barrel_index(event.code())];
                    }
                    return drop_event;
                }

                // Capslock is the other scroll trigger; on_held
                // downstream owns the swallow/LED behavior, so the key
                // itself passes through untouched.
                case KEY_CAPSLOCK:
                    caps_held_ = event.value() != 0;
                    update_scroll();
                    return next;

                default: return next;
            }

        // ── Absolute axes ─────────────────────────────────────────
        case EV_ABS:
            switch (event.code()) {
                case ABS_X: {
                    if (have_x_ && event.value() == x_) {
                        // Nothing moved: no trigger to mark, no anchor to
                        // seed, no position to mirror, and a deferred
                        // delta of zero would not change.  Sources that
                        // resend every axis each frame (synthetic,
                        // replayed, captured) hit this on every packet.
                        return next;
                    }
                    if (x_ != event.value()) {
                        mark_triggers_used();
                    }
                    auto const previous = x_;
                    x_                  = event.value();
                    have_x_             = true;
                    if (scroll_id_ >= 0 && !scroll_anchor_valid_) {
                        // The finger opened before any position arrived:
                        // this first X becomes the anchor, which mirrors
                        // it onto itself.
                        scroll_anchor_x_     = x_;
                        scroll_anchor_valid_ = true;
                    }
                    if (comp_defer(accel_comp)) {
                        // Held back to the frame boundary, where the
                        // SYN below divides it by libinput's gain.
                        comp_dx_ += static_cast<double>(x_ - previous);
                        return next;
                    }
                    comp_dx_                  = 0;
                    bool const       mirrored = scroll_id_ >= 0 && scroll_anchor_valid_;
                    value_type const out      = mirrored ? mirrored_x(x_) : x_;
                    if (touching_) {
                        emit(EV_ABS, ABS_MT_POSITION_X, out);
                        emitted_x_ = out;
                    }
                    if (scroll_id_ >= 0) {
                        select_slot(scroll_slot);
                        emit(EV_ABS, ABS_MT_POSITION_X, mirrored ? mirrored_x(scroll_x()) : scroll_x());
                        select_slot(mt_slot);
                    }
                    return next;
                }

                case ABS_Y: {
                    if (have_y_ && event.value() == y_) {
                        // See ABS_X: an unchanged axis is pure overhead.
                        return next;
                    }
                    if (y_ != event.value()) {
                        mark_triggers_used();
                    }
                    auto const previous = y_;
                    y_                  = event.value();
                    have_y_             = true;
                    if (comp_defer(accel_comp)) {
                        comp_dy_ += static_cast<double>(y_ - previous);
                        return next;
                    }
                    comp_dy_ = 0;
                    if (touching_) {
                        emit(EV_ABS, ABS_MT_POSITION_Y, y_);
                        emitted_y_ = y_;
                    }
                    if (scroll_id_ >= 0) {
                        select_slot(scroll_slot);
                        emit(EV_ABS, ABS_MT_POSITION_Y, y_);
                        select_slot(mt_slot);
                    }
                    return next;
                }

                // Legacy pressure passes through untouched; it is
                // deliberately not mirrored into ABS_MT_PRESSURE
                // (see the class doc: libinput's pressure-based
                // touch detection cannot be satisfied by pen input).
                case ABS_PRESSURE: return next;

                // Tablet-specific properties not representable on a
                // touchpad.
                case ABS_TILT_X:
                case ABS_TILT_Y:
                case ABS_TOOL_WIDTH: return drop_event;

                // Tablet proximity distance: keep it (useful for
                // hover-aware touchpad consumers).
                case ABS_DISTANCE: return next;

                default: return next;
            }

        // The original SYN_REPORT must survive; the forked MT events
        // above belong to the same synchronization frame.  It is also
        // where the frame closes: libinput reads a contact's position
        // at SYN_REPORT, so held-back motion is divided by the gain
        // there (nothing is ever held back with the switch off).
        case EV_SYN:
            if (accel_comp) {
                flush_accel();
            } else {
                comp_dx_ = 0;
                comp_dy_ = 0;
            }
            return next;

        default: return next;
    }
}

fs8::context_action basic_pen2touch::operator()(event_type& event) noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    return pimpl->handle(event, accel_comp_);
}

fs8::context_action basic_pen2touch::operator()(control_event const& tag) noexcept {
    using enum fs8::context_action;
    switch (tag.code) {
        case profile_device.code: {
            auto& tmpl = payload<profile_device>(tag);
            // Only a pen source gets reshaped.  The keyboard-route virtual
            // device has no ABS_X: turning it into a touchpad would break
            // typing, so it keeps whatever profile it was built with.
            if (!tmpl.has_event_code(EV_ABS, ABS_X)) {
                return next;
            }
            // uinput is about to create the virtual device: turn the
            // tablet clone into a multitouch touchpad and remember the
            // axis range, which sizes the scroll finger's offset.
            if (pimpl.get() == nullptr) [[unlikely]] {
                init_impl();
            }
            profile_template(tmpl);
            if (auto const* info = tmpl.abs_info(ABS_MT_POSITION_X); info != nullptr) {
                pimpl->scroll_min_x_    = info->minimum;
                pimpl->scroll_max_x_    = info->maximum;
                pimpl->scroll_offset_x_ = (info->maximum - info->minimum) / 8;
            }
            // The speed factor rides on the axis resolution: libinput
            // derives pointer/scroll motion from delta/resolution, so
            // dividing the resolution scales the motion while the
            // coordinate stream stays raw (no wall, no drift).  Axes
            // without a resolution are left to libinput's own fallback.
            if (speed_ > 0.0F && speed_ != 1.0F) {
                for (auto const code : {ABS_MT_POSITION_X, ABS_MT_POSITION_Y, ABS_X, ABS_Y}) {
                    auto const typed_code = static_cast<code_type>(code);
                    if (auto const* info = tmpl.abs_info(typed_code); info != nullptr && info->resolution != 0) {
                        auto       updated = *info;
                        auto const scaled  = std::lround(static_cast<double>(info->resolution) / static_cast<double>(speed_));
                        updated.resolution = static_cast<decltype(updated.resolution)>(std::clamp(scaled, 1L, static_cast<long>(INT_MAX)));
                        tmpl.abs_info(typed_code, updated);
                    }
                }
            }
            // The acceleration compensation works in mm/s: remember the
            // resolution libinput will advertise (post-speed-scaling —
            // the factor rides on the resolution).  0 = no scale to
            // convert through, the mod stays raw.
            if (auto const* info = tmpl.abs_info(ABS_MT_POSITION_X); info != nullptr) {
                pimpl->res_x_ = static_cast<double>(info->resolution);
            }
            if (auto const* info = tmpl.abs_info(ABS_MT_POSITION_Y); info != nullptr) {
                pimpl->res_y_ = static_cast<double>(info->resolution);
            }
            return next;
        }

        case devices_changed.code:
            // The source tablet vanished: it may never send the matching
            // BTN_TOUCH=0, so close what we hold ourselves.  Any other
            // device change is irrelevant to us.
            if (tag != device_disconnected) {
                return drop_event;
            }
            break;

        case start.code:
        case toggle_off.code:
            // A restart / disable must not orphan a contact from the
            // previous run: release it before forgetting our state.
            break;

        default: return drop_event;
    }
    // release_stale_state needs no pipeline mod, so this works with or
    // without keys_state; a mod that never saw an event holds nothing.
    // A null pimpl means this is the very first start: nothing of ours
    // can be stuck yet, and emitting here would race the mods further
    // down the pipeline whose start has not run yet (the output's
    // uinput device does not exist until then).
    if (pimpl.get() == nullptr) {
        return next;
    }
    pimpl->release_stale_state();
    pimpl->reset();
    if (tag.code != start.code) {
        return next;
    }
    // If a pen tool was left active from a previous run, release it so
    // consumers don't see a stuck contact.
    for (auto const& keys : dynamic_context.mods<basic_keys_state>()) {
        for (code_type const tool : std::initializer_list<
               code_type>{BTN_TOOL_PEN, BTN_TOOL_RUBBER, BTN_TOOL_BRUSH, BTN_TOOL_PENCIL, BTN_TOOL_AIRBRUSH, BTN_TOOL_MOUSE, BTN_TOOL_LENS})
        {
            if (keys.get().is_pressed(tool)) {
                impl::emit(EV_KEY, tool, 0);
                impl::emit(EV_SYN, SYN_REPORT, 0);
                impl::emit(EV_KEY, BTN_TOOL_FINGER, 0);
                impl::emit(EV_SYN, SYN_REPORT, 0);
            }
        }
    }
    return next;
}
