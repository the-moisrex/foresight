// Created by moisrex on 9/13/26.

module;
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <linux/input-event-codes.h>
#include <utility>
export module fs8.mods:split_move;
import fs8.context;
import fs8.event;
import fs8.traits;

export namespace fs8 {

    /**
     * Decompose a mouse-movement frame into smaller per-unit frames.
     *
     * Accumulates the `REL_X`/`REL_Y` deltas belonging to a frame and, on
     * `EV_SYN`, re-emits the movement as a sequence of chunks of at most
     * `step` units each, one axis per `SYN_REPORT` frame. Both axes are spread
     * over the whole frame proportionally so they finish together: the longer
     * axis emits more chunks than the shorter one, interleaved as evenly as
     * possible. The total movement is preserved exactly (drift-free).
     *
     * The emitted frames are also stamped with evenly spaced timestamps inside
     * the frame's interval (from the previous `SYN` — or this frame's first
     * movement event — to the current `SYN`), so consumers that derive
     * velocity from event times see a smooth ramp instead of every unit
     * sharing a single instant.
     *
     * This is still a pure synchronous transformer: it does no time
     * management (no sleeps/scheduling), it only rewrites event timestamps, so
     * all emitted frames are delivered in the same batch. Whether a consumer
     * sees distinct moves therefore depends on it (libinput and X11 sum
     * same-code `REL` events per `SYN` frame and timestamp them themselves).
     *
     * @par Example
     * @code
     *   ... | split_move     | output  // REL_X=5 -> 5 x REL_X=1
     *   ... | split_move[2]  | output  // REL_X=5 -> 2, 2, 1
     * @endcode
     */
    constexpr struct [[nodiscard]] basic_split_move : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using value_type = event_type::value_type;
        using time_type  = event_type::time_type;

      private:
        value_type step = 1;

        /// Accumulated (signed) frame deltas awaiting decomposition.
        value_type x_accum = 0;
        value_type y_accum = 0;

        /// Timeline anchors for timestamp interpolation.
        std::chrono::microseconds last_syn_time{};
        std::chrono::microseconds frame_start_time{};
        bool                      has_last_syn    = false;
        bool                      has_frame_start = false;

        /// Hard upper bound on the number of frames emitted per input frame.
        /// Guards against pathological jumps; any excess is folded into a
        /// single trailing chunk so the total stays drift-free.
        static constexpr value_type max_units = 256;

        [[nodiscard]] static constexpr value_type chunk_count(value_type const value, value_type const unit) noexcept {
            auto const mag = std::abs(value);
            return (mag + unit - 1) / unit;
        }

        [[nodiscard]] static constexpr time_type from_micros(std::chrono::microseconds const us) noexcept {
            auto const secs = std::chrono::duration_cast<std::chrono::seconds>(us);
            auto const rem  = us - secs;
            time_type  result{};
            result.tv_sec  = static_cast<decltype(result.tv_sec)>(secs.count());
            result.tv_usec = static_cast<decltype(result.tv_usec)>(rem.count());
            return result;
        }

      public:
        constexpr basic_split_move() noexcept = default;

        constexpr explicit basic_split_move(value_type const inp_step) noexcept : step{inp_step < 1 ? 1 : inp_step} {}

        consteval basic_split_move operator[](value_type const inp_step) const noexcept {
            return basic_split_move{inp_step};
        }

        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            using enum context_action;

            auto const& event = ctx.event();

            if (is_mouse_movement(event)) {
                if (!has_frame_start) {
                    frame_start_time = event.micro_time();
                    has_frame_start  = true;
                }
                if (event.code() == REL_X) {
                    x_accum += event.value();
                } else {
                    y_accum += event.value();
                }
                return drop_event;
            }

            if (!is_syn(event)) {
                return next;
            }

            auto const end        = event.micro_time();
            auto const frame_start = has_last_syn ? last_syn_time : (has_frame_start ? frame_start_time : end);

            // The current SYN becomes the interpolation base of the next frame.
            last_syn_time   = end;
            has_last_syn    = true;
            has_frame_start = false;

            if (x_accum == 0 && y_accum == 0) {
                return next;
            }

            // Number of chunks each axis needs, capped to bound pathological
            // jumps.
            auto const chunks_x = std::min(chunk_count(x_accum, step), max_units);
            auto const chunks_y = std::min(chunk_count(y_accum, step), max_units);

            // Both axes are distributed over the frame proportionally so they
            // finish together: the longer axis emits more chunks, interleaved
            // as evenly as possible (so the longer one may emit twice where the
            // shorter one emits once).
            //
            // The interleaving below is Bresenham's line algorithm (Jack
            // Bresenham, 1962): an integer-only method for rasterising a line
            // onto a pixel grid. It walks the dominant axis one pixel at a time
            // and uses an accumulated error term — here, "how far each axis is
            // behind its proportional share of the frames" — to decide when the
            // minor axis advances too, producing an evenly spaced staircase
            // without any floating point. We reuse it to merge the two event
            // streams: the X and Y chunk counts are the two axis lengths, and
            // each frame advances whichever stream is behind. The comparisons
            // are cross-multiplied so the whole thing stays in integers (no
            // division and no rounding drift).
            auto const frames = chunks_x + chunks_y;
            auto const span   = end - frame_start;

            value_type emitted   = 0;
            auto const next_time = [&]() noexcept {
                return frame_start + span * (++emitted) / frames;
            };

            auto const emit_axis = [&](event_type::code_type const code, value_type& accum, std::chrono::microseconds const at) noexcept {
                auto const mag    = std::min<value_type>(step, std::abs(accum));
                auto const chunk  = accum > 0 ? mag : -mag;
                accum            -= chunk;
                auto out          = event;
                out.set(EV_REL, code, chunk);
                out.time(from_micros(at));
                std::ignore = ctx.fork_emit(out);
            };
            auto const emit_syn = [&](std::chrono::microseconds const at) noexcept {
                auto sync = event;
                sync.set(EV_SYN, SYN_REPORT, 0);
                sync.time(from_micros(at));
                std::ignore = ctx.fork_emit(sync);
            };

            value_type xi = 0;
            value_type yi = 0;
            while (xi < chunks_x || yi < chunks_y) {
                // Bresenham decision, cross-multiplied: emit the axis furthest
                // behind its proportional share (see the note above).
                bool const take_x = xi < chunks_x && (yi >= chunks_y || (2 * xi + 1) * chunks_y <= (2 * yi + 1) * chunks_x);
                auto const at     = next_time();
                if (take_x) {
                    emit_axis(REL_X, x_accum, at);
                    ++xi;
                } else {
                    emit_axis(REL_Y, y_accum, at);
                    ++yi;
                }
                emit_syn(at);
            }

            // Only reachable when `max_units` capped the loop: fold whatever is
            // left into single-axis trailing frames at the real frame end.
            if (x_accum != 0) {
                auto out = event;
                out.set(EV_REL, REL_X, std::exchange(x_accum, value_type{0}));
                out.time(from_micros(end));
                std::ignore = ctx.fork_emit(out);
                emit_syn(end);
            }
            if (y_accum != 0) {
                auto out = event;
                out.set(EV_REL, REL_Y, std::exchange(y_accum, value_type{0}));
                out.time(from_micros(end));
                std::ignore = ctx.fork_emit(out);
                emit_syn(end);
            }

            return drop_event;
        }
    } split_move;

    static_assert(Modifier<basic_split_move>);

} // namespace fs8
