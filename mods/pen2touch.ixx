// Created by moisrex on 9/17/26.

module;
#include <climits>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <utility>
export module fs8.mods:pen2touch;
import :keys_state;
import fs8.context;
import fs8.event;
import fs8.traits;

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
    ///   ABS_MT_PRESSURE
    ///
    /// The legacy ABS_X / ABS_Y / ABS_PRESSURE events are retained because
    /// real touchpads commonly expose both the legacy single-touch interface
    /// and the Type-B MT interface.
    ///
    /// Stylus buttons (BTN_STYLUS / BTN_STYLUS2 / BTN_STYLUS3), tilt
    /// (ABS_TILT_X / ABS_TILT_Y), and tool width (ABS_TOOL_WIDTH) are
    /// dropped because they have no touchpad equivalent.
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
    constexpr struct [[nodiscard]] basic_pen2touch : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

      private:
        /// Current tracking ID.  Non-negative means a contact is active;
        /// -1 means no contact.
        value_type tracking_id_ = -1;

        /// Last-seen raw coordinates and pressure, cached so we can emit
        /// them alongside the MT equivalents.
        value_type x_        = 0;
        value_type y_        = 0;
        value_type pressure_ = 0;

        /// Whether the pen tool is in proximity (BTN_TOOL_* == 1).
        bool tool_active_ = false;

        /// Whether the pen is touching the surface (BTN_TOUCH == 1).
        bool touching_ = false;

        /// Whether we have received at least one ABS_X / ABS_Y / ABS_PRESSURE
        /// since the last touch-down, so we know the values are valid.
        bool have_x_        = false;
        bool have_y_        = false;
        bool have_pressure_ = false;

        /// Slot reported in MT events; always 0 for a single-contact device.
        static constexpr value_type mt_slot = 0;

        /// Monotonically increasing tracking ID generator.
        [[nodiscard]] value_type next_tracking_id() noexcept {
            if (tracking_id_ == INT_MAX) {
                tracking_id_ = 0;
            } else {
                ++tracking_id_;
            }
            return tracking_id_;
        }

        /// Reset all internal state (called on start / toggle_off).
        void reset() noexcept {
            tracking_id_   = -1;
            x_             = 0;
            y_             = 0;
            pressure_      = 0;
            tool_active_   = false;
            touching_      = false;
            have_x_        = false;
            have_y_        = false;
            have_pressure_ = false;
        }

        /// Emit a single event through the pipeline (downstream mods see it).
        template <Context CtxT>
        static void emit(CtxT& ctx, code_type const type, code_type const code, value_type const value) noexcept {
            std::ignore = ctx.fork_emit(event_type{type, code, value});
        }

        /// Begin a new MT contact (ABS_MT_TRACKING_ID >= 0).
        template <Context CtxT>
        void begin_contact(CtxT& ctx) noexcept {
            if (touching_) {
                return;
            }
            touching_ = true;

            value_type const id = next_tracking_id();

            emit(ctx, EV_ABS, ABS_MT_SLOT, mt_slot);
            emit(ctx, EV_ABS, ABS_MT_TRACKING_ID, id);

            if (have_x_) {
                emit(ctx, EV_ABS, ABS_MT_POSITION_X, x_);
            }
            if (have_y_) {
                emit(ctx, EV_ABS, ABS_MT_POSITION_Y, y_);
            }
            if (have_pressure_) {
                emit(ctx, EV_ABS, ABS_MT_PRESSURE, pressure_);
            }
        }

        /// End the current MT contact (ABS_MT_TRACKING_ID = -1).
        template <Context CtxT>
        void end_contact(CtxT& ctx) noexcept {
            if (!touching_) {
                return;
            }
            emit(ctx, EV_ABS, ABS_MT_TRACKING_ID, -1);
            touching_ = false;
        }

      public:
        /// Handle lifecycle tags (start, toggle_off).
        template <Context CtxT>
        context_action operator()(CtxT& ctx, special_event const& tag) noexcept {
            using enum context_action;

            switch (tag.code) {
                case start.code: {
                    reset();
                    // If a pen tool was left active from a previous run,
                    // release it so consumers don't see a stuck contact.
                    if constexpr (has_mod<basic_keys_state, CtxT>) {
                        auto const& keys = ctx.mod(keys_state);
                        for (code_type const tool :
                             std::initializer_list<code_type>{
                               BTN_TOOL_PEN,
                               BTN_TOOL_RUBBER,
                               BTN_TOOL_BRUSH,
                               BTN_TOOL_PENCIL,
                               BTN_TOOL_AIRBRUSH,
                               BTN_TOOL_MOUSE,
                               BTN_TOOL_LENS})
                        {
                            if (keys.is_pressed(tool)) {
                                std::ignore = ctx.fork_emit(event_type{EV_KEY, tool, 0});
                                std::ignore = ctx.fork_emit(syn());
                                std::ignore = ctx.fork_emit(event_type{EV_KEY, BTN_TOOL_FINGER, 0});
                                std::ignore = ctx.fork_emit(syn());
                            }
                        }
                    }
                    return next;
                }
                case toggle_off.code:
                    // If the mod is disabled mid-contact, clean up so we
                    // don't leak a stale tracking ID.
                    if (touching_) {
                        if constexpr (has_mod<basic_keys_state, CtxT>) {
                            end_contact(ctx);
                        }
                    }
                    reset();
                    return next;

                default: return drop_event;
            }
        }

        /// Translate a single input event.
        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            using enum context_action;

            auto& event = ctx.event();

            switch (event.type()) {
                // ── Keys ──────────────────────────────────────────────────
                case EV_KEY:
                    switch (event.code()) {
                        // Tablet proximity / tool state -> touchpad finger state.
                        //
                        // BTN_TOOL_FINGER is independent of BTN_TOUCH, so a
                        // hovering pen can legitimately produce
                        // BTN_TOOL_FINGER=1, BTN_TOUCH=0 just as a
                        // hover-capable touch device can.
                        case BTN_TOOL_PEN:
                        case BTN_TOOL_RUBBER:
                        case BTN_TOOL_BRUSH:
                        case BTN_TOOL_PENCIL:
                        case BTN_TOOL_AIRBRUSH:
                        case BTN_TOOL_MOUSE:
                        case BTN_TOOL_LENS:
                            tool_active_ = event.value() != 0;
                            event.code(BTN_TOOL_FINGER);
                            return next;

                        // BTN_TOUCH is the actual contact boundary.  Keep the
                        // original event because it is also part of the legacy
                        // single-touch interface.
                        case BTN_TOUCH:
                            if (event.value() != 0) {
                                begin_contact(ctx);
                            } else {
                                end_contact(ctx);
                            }
                            return next;

                        // Stylus buttons have no touchpad equivalent.
                        case BTN_STYLUS:
                        case BTN_STYLUS2:
                        case BTN_STYLUS3:
                            return drop_event;

                        default: return next;
                    }

                // ── Absolute axes ─────────────────────────────────────────
                case EV_ABS:
                    switch (event.code()) {
                        case ABS_X:
                            x_      = event.value();
                            have_x_ = true;
                            if (touching_) {
                                emit(ctx, EV_ABS, ABS_MT_POSITION_X, x_);
                            }
                            return next;

                        case ABS_Y:
                            y_      = event.value();
                            have_y_ = true;
                            if (touching_) {
                                emit(ctx, EV_ABS, ABS_MT_POSITION_Y, y_);
                            }
                            return next;

                        case ABS_PRESSURE:
                            pressure_      = event.value();
                            have_pressure_ = true;
                            if (touching_) {
                                emit(ctx, EV_ABS, ABS_MT_PRESSURE, pressure_);
                            }
                            return next;

                        // Tablet-specific properties not representable on a
                        // touchpad.
                        case ABS_TILT_X:
                        case ABS_TILT_Y:
                        case ABS_TOOL_WIDTH:
                            return drop_event;

                        // Tablet proximity distance: keep it (useful for
                        // hover-aware touchpad consumers).
                        case ABS_DISTANCE:
                            return next;

                        default: return next;
                    }

                // The original SYN_REPORT must survive; the forked MT events
                // above belong to the same synchronization frame.
                case EV_SYN:
                    return next;

                default: return next;
            }
        }
    } pen2touch;

    static_assert(Modifier<basic_pen2touch>);

} // namespace fs8
