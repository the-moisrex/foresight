# Mods

Mods are the building blocks of a Foresight pipeline. Import everything with:

```cpp
import fs8.mods;
```

Everything lives in the `fs8` namespace. Full signatures are in the
[API Reference](api-reference.md).

## Quick reference

| Mod | Category | What it does |
|-----|----------|--------------|
| `intercept` | Event provider | Reads kernel input devices and feeds events into the pipeline. |
| `io_manager` | Event provider | Poll-based fd readiness watcher; wakes the pipeline. |
| `input_manager` | Event provider | Owns devices, resolves queries, tracks hotplug. |
| `idle_detector` | Event provider | Detects pipeline idle and broadcasts an event. |
| `scheduler` | Event provider | Timer-based event scheduling via `timerfd`. |
| `replay` | Event provider | Plays back captured events from a file. |
| `output` | Output | Writes events to an fd (stdout by default). |
| `uinput` | Output | Creates virtual devices under `/dev/uinput`. |
| `router` | Output | Routes events to outputs by capability. |
| `emit` / `schedule_emit` | Output | Synthesize events (`emit[press(...)]`). |
| `type_string` | Output | Type text into the current app via xkb. |
| `capture` | Output | Buffer events and flush to a file on idle. |
| `replace` / `put` | Transformer | Rewrite one key/chord into another sequence. |
| `abs2rel` | Transformer | Absolute (tablet) to relative (mouse) movement. |
| `pen2mice` | Transformer | Pen buttons/tools to mouse clicks. |
| `pen2touch` | Transformer | Pen events to touch events. |
| `pressure2mouse_clicks` | Transformer | Pen pressure to mouse clicks by threshold. |
| `mouse_to_scroll` | Transformer | Mouse movement to scroll-wheel events. |
| `smooth` | Transformer | Mouse smoothing: `lerp`, `low_pass_filter`, `kalman_filter`. |
| `momentum_scroll` | Transformer | Inertial scrolling after input stops. |
| `scale_pen` / `scale_move` | Transformer | Scale pen or mouse movement by a factor. |
| `tilt_speed` / `tilt_freeze` / `tilt_push` | Transformer | Tilt-based speed, freeze, and directional push. |
| `debounce` | Filter | Drop events arriving too soon after the same code. |
| `drop_*` (30+ variants) | Filter | Drop big jumps, init moves, fast repeats, etc. |
| `enforce_key_state` | Filter | Enforce valid key state transitions. |
| `sanitizer` / `diagnostics` | Filter | Pre-built filter groups for common bad events. |
| `on` | Control flow | Run actions while a condition is true. |
| `once` | Control flow | Fire once when a condition switches on. |
| `held` / `on_held` | Control flow | True while keys are held; gate with decider. |
| `hold_mod` | Control flow | Run a mod while modifiers are held; tap re-emits. |
| `pressed` / `pressed_any` | Condition | True when specific keys are down. |
| `keydown` / `keyup` | Condition | Match a key press or release event. |
| `longtime_released` | Condition | True when a key has been released for a while. |
| `limit_mouse_travel` | Condition | True while mouse travel stays under a limit. |
| `multi_click` | Condition | Double/triple click detection. |
| `swipe_*` | Condition | Swipe detection (left/right/up/down). |
| `led_on` / `led_off` | Condition | Keyboard LED state conditions. |
| `tilted` / `tilt_changing` | Condition | Pen tilt magnitude / rate conditions. |
| `op` | Condition | Boolean combinators (`&`, `\|`, `!`). |
| `modes` / `switch_mode` | Control flow | Vim-like modes/layers. |
| `on_fail` | Control flow | Invoke an action when a condition fails. |
| `stopper` | Control flow | Exit the pipeline on demand. |
| `typed` / `timed_typed` | Condition | Match what the user is typing. |
| `keys_state` | State | Track the current state of every key. |
| `led_state` / `led_toggle` | State | Track keyboard LED state. |
| `mouse_history` | State | Track current/previous mouse positions. |
| `quantifier` / `mice_quantifier` | State | Threshold-step accumulation for movement. |
| `tilt_state` | State | Track pen tilt (`ABS_TILT_X` / `ABS_TILT_Y`) relative to a captured neutral hold. |
| `var_type` | State | Typed pipeline variables (`context["name"]`). |
| `startup_key_releases` | State | Sync pipeline with physical keyboard on launch. |
| `device` (11 variants) | Condition | Filter by which device an event came from. |
| `run` | Utility | Wrap functions as mods/callbacks. |
| `record` | Utility | Record events into a buffer for replay. |
| `group_mod` | Utility | Run a tuple of mods as a single mod. |
| `benchmark` | Utility | Measure mod performance (latency, percentiles). |
| `singleton` | Utility | Prevent multiple pipeline instances. |
| `live_view` | Output | Terminal event display for `foresight replay --live`. |

## Event providers

These put events into the pipeline.

### `intercept`

Query-driven event provider. Reads kernel devices matching `device_query`es
and feeds their events in.

```cpp
intercept[keyboard | required | grab, mouse | grab]
```

Needs `input_manager` in the pipeline. `io_manager` is optional: without it
devices are still read, but fd readiness is polled by `next_event` instead of
waking the pipeline.

### `io_manager`

Poll-based fd readiness manager. Registers file descriptors, waits for events,
and dispatches each ready fd to its handler. Supports idle timeout.

Mods never reach into it with `ctx.mod(io_manager)` — they configure it by
broadcasting one of the four `io_*` control events (they share control code 14;
`value` picks the operation) and read the result back out of the payload:

| Event | Payload | Direction |
|-------|---------|-----------|
| `io_watch` | `io_watch_request` (`fd`, `callback`, `status`) | register an fd |
| `io_unwatch` | `int` (the fd) | unregister an fd |
| `io_idle_timeout` | `std::chrono::microseconds` | arm / disarm the idle timeout |
| `io_idle_callback` | `basic_io_manager::idle_callback` (moved out) | install the idle callback |

`io_watch` is the only one with a reply: build it with
`watch_of(fd, handler)`, then

```cpp
auto req = watch_of(io_fd{.fd = fd, .events = io_event::in}, handler);
std::ignore = dynamic_context.broadcast(io_watch + &req);
if (req.status != io_watch_status::registered) { /* handle it */ }
```

`io_watch_status` is `no_poller` (no `io_manager` in this pipeline),
`failed` (present but refused the fd), or `registered`. Callers read the
payload, never the returned `context_action`.

Without an `io_manager`, all four broadcasts go unhandled (the framework logs
"A required control event was not handled"), `io_watch` reports `no_poller`,
and every caller keeps working.

### `input_manager`

Owns and monitors discovered evdev devices. Resolves queries, tracks hotplug
via udev, answers "which device did this event come from?". Does NOT provide
events itself -- `intercept` is the event provider.

### `idle_detector`

Detects pipeline idle (no events for a configurable period) and broadcasts an
`idle` control event. Supports repeat patterns: `once`,
`consistent<PeriodUs>`, `exponential<BaseUs>`. Arms the `io_manager` timeout
via `io_idle_timeout` / `io_idle_callback`, so without an `io_manager` there is
no idle source (and the framework logs the unhandled events).

### `scheduler`

Timer-based event scheduler using `timerfd` registered with `io_manager`.
Required by `momentum_scroll`. Without an `io_manager` the timerfd is dropped
and `next_event` polling drives the ticks instead.

### `replay`

Reads captured events from a file and injects them into the pipeline.
Auto-detects format (binary or evtest text). Returns `exit` when exhausted.

## Output

### `output`

Writes events to a file descriptor (stdout by default). The library-side
redirect.

### `uinput`

Creates virtual devices under `/dev/uinput` that events can be written to.
Can clone a physical device's capabilities into a virtual device.

```cpp
uinput  // creates a virtual device and writes events to it
```

### `router`

Routes events to specific output devices by capability. Uses a hash-based
lookup table.

```cpp
router[caps::mouse >> uinput, caps::keyboard >> keyboard_pipeline]
```

### `emit` / `schedule_emit`

Synthesize events. Build event sequences with `press(...)`, `down(...)`,
`up(...)`, `keypress(...)`, `turn_led_on(...)`, `turn_led_off(...)`.

```cpp
emit[press(KEY_LEFTMETA, KEY_TAB)]
schedule_emit + press(BTN_RIGHT)
```

### `type_string`

Type text into the current application via xkb `how2type`.

```cpp
type_string("hello world")
```

### `capture`

Buffer events in memory and flush to a file during idle periods. Supports
daily, hourly, weekly, monthly, uptime-based, single-file, and manual
rotation. Output formats: `capture_binary_format` (raw `.fs8`) and
`capture_evtest_format` (human-readable `.evtest.fs8`).

## Transformers

### `replace` / `put`

Rewrite one key/chord into another sequence. On press, replacements are emitted
in order; on release, they are emitted in reverse order.

```cpp
replace[KEY_D, KEY_LEFTMETA, KEY_LEFTCTRL, KEY_RIGHT]
put[KEY_A]  // rewrite the current event's code to KEY_A
```

### `abs2rel`

Convert absolute events (drawing tablets) into relative events (mouse).

### `pen2mice`

Translate a pen tablet's buttons/tools into mouse clicks.

### `pen2touch`

Convert pen events to touch events.

### `pressure2mouse_clicks`

Convert pen pressure into mouse clicks based on a threshold.

### `mouse_to_scroll`

Convert mouse movement into scroll-wheel events. Pure transformer with no
condition of its own -- gate it with `hold_mod`.

```cpp
on_held[KEY_CAPSLOCK, BTN_MIDDLE, context | kalman_filter[0.3f] | mouse_to_scroll]
```

Requires `mice_quantifier` in the pipeline.

### `smooth`

Mouse movement smoothing. Three strategies:

```cpp
lerp[max_steps, easing]         // interpolate with easing (default: easeOutQuad)
low_pass_filter[alpha]          // exponential moving average
kalman_filter[q, r]             // 1D Kalman filter per axis
```

All require `mouse_history` placed before them in the pipeline.

### `split_move`

Decompose each mouse-movement frame into smaller per-unit frames. A `REL_X=5`
frame becomes five `REL_X=1` frames, each terminated by its own `SYN_REPORT`.
Both axes are spread over the whole frame proportionally so they finish
together: the longer axis emits more frames than the shorter one, interleaved
as evenly as possible (e.g. two of the longer for one of the shorter). The total
movement is preserved exactly.

```cpp
split_move       // unit chunks (REL_X=5 -> 5 x REL_X=1)
split_move[2]    // REL_X=5 -> 2, 2, 1
```

The emitted frames are stamped with evenly spaced timestamps inside the frame's
interval (previous `SYN` to current `SYN`), so velocity derived from event times
ramps smoothly instead of sharing one instant.

This is still a synchronous transformer -- it does no time management (no
sleeps/scheduling), only timestamp rewriting, so all emitted frames are
delivered in the same batch. Consumers that sum same-code `REL` events per `SYN`
frame (libinput, X11) may still coalesce them.

### `momentum_scroll`

Inertial scrolling after input stops. Tracks velocity and schedules momentum
events via `scheduler`. Requires `scheduler` in the pipeline.

### `scale_pen` / `scale_move`

Scale movement events by a factor.

```cpp
on[held[KEY_LEFTSHIFT], context | scale_move[0.5f] | scale_pen[0.5f]]
```

### `tilt_speed` / `tilt_freeze` / `tilt_push`

Pen-tilt driven movement control. All three require `tilt_state` earlier in the
pipeline (or in an enclosing pipeline, since sub-pipelines share the context).

Detection and response are separate, so each pipeline picks the behaviour it
wants. The domain (ABS before `abs2rel`, REL after), the tilt mapping (one
factor vs per-axis) and the easing curve are passed as ordinary callables, so
you can use the provided ones or your own function of the matching signature.

```cpp
import fs8.easings;

context
  | tilt_state[tilt_base_options{.recenter_time = 3.0F}]
  | abs2rel
  | tilt_speed[tilt_rel, tilt_isotropic, easeOutCubic<float>,
               tilt_speed_options{.base = 1.0F, .max = 2.5F, .start = 0.2F, .end = 1.0F}]
  | tilt_freeze[0.15F]                    // hold still while the hand stretches
  | tilt_push[tilt_rel, tilt_push_options{.gain = 2.0F, .dead_zone = 0.3F}]
```

- `tilt_speed` scales movement between `base` (no tilt) and `max` (full tilt):
  `max > base` accelerates, `max < base` damps. Use `tilt_abs` to rewrite
  `ABS_X`/`ABS_Y` before `abs2rel`, or `tilt_rel` for `REL_X`/`REL_Y` after it.
  `tilt_per_axis` maps each tilt axis separately; `tilt_isotropic` uses the tilt
  magnitude for both. The curve is any `float(float)` easing from
  `fs8.easings` — e.g. `linear<float>`, `easeOutQuad<float>`,
  `easeOutCubic<float>`, `easeOutSine<float>` — or your own function.
- `tilt_freeze[threshold]` freezes movement while the per-event tilt change is
  at/above `threshold`. In `tilt_abs` domain it holds the emitted absolute
  position so `abs2rel` sees a zero delta; in `tilt_rel` domain it zeroes the
  movement events.
- `tilt_push[gain]` nudges movement in the tilt direction, only on movement
  events (merely tilting does not drift). `gain` is in pixels for `tilt_rel`
  and raw `ABS_*` counts for `tilt_abs`; `dead_zone` ignores small tilts.

The three callables are `using`-style function pointers, so any free function
(or captureless lambda) of the matching signature works:

```cpp
using tilt_domain_fn  = tilt_axis (*)(event_type const&) noexcept;
using tilt_mapping_fn = void (*)(basic_tilt_state const&, float& t_x, float& t_y) noexcept;
using tilt_curve_fn   = float (*)(float t) noexcept;
```

`tilt_abs`, `tilt_rel`, `tilt_isotropic` and `tilt_per_axis` are instances of
those. Arguments can be given in any order; anything omitted keeps its default
(`tilt_rel`, `tilt_isotropic`, `easeOutCubic<float>`).

The scale factor is cached and recomputed only when the tilt actually changes
(`tilt_state::version()`), so movement events cost a compare and a multiply.
All of these read the **base-relative** tilt from `tilt_state`, so the user's
natural hold is neutral (see `tilt_state` below).

### `autocomplete`

Watch typed patterns of the form `PREFIX<TAG>COMPLETION` and auto-complete
them. In trigger mode, fires on a trigger keypress; in `auto_mode`, fires as
soon as the prefix is fully typed.

## Filters

### `debounce`

Drop events that arrive too soon after a previous event of the same code. For
`EV_KEY`, a fast second press and its release are dropped (faulty double-clicks,
bouncing keys). For other event types, every event within the window is dropped.

```cpp
debounce[BTN_LEFT, BTN_RIGHT]
debounce[{.type = EV_ABS, .code = ABS_X}]
```

### `drop_*` family

Over 30 variants for filtering bad or unwanted events:

| Mod | What it drops |
|-----|---------------|
| `drop_big_jumps` | Large mouse movements |
| `drop_init_moves` | Initial mouse movements after connect |
| `drop_start_moves` | Mouse moves at the start of an action |
| `drop_mouse_moves` | All mouse movement events |
| `drop_zero_mouse_moves` | Mouse moves with zero delta |
| `drop_mouse_clicks` | Mouse button events |
| `drop_fast_repeats` | Fast auto-repeat events |
| `drop_adjacent_repeats` | Consecutive duplicate events |
| `drop_adjacent_syns` | Consecutive SYN_REPORTs |
| `drop_fast_left_clicks` | Fast left-click press+release |
| `drop_fast_right_clicks` | Fast right-click press+release |
| `drop_fast_double_clicks` | Fast double-click sequences |
| `drop_keys[...]` | Specific key events |
| `drop_repeats_of[...]` | Auto-repeat (value 2) of specific keys |
| `drop_caps[...]` | Events matching specific device capabilities |
| `drop_abs` | All absolute axis events |
| `drop_tablet` | All tablet events |
| `drop_caps` | CapsLock-related events |
| `drop_msc_scan` | MSC_SCAN events |
| `drop_late_syn` | Empty sync packets after long idle |
| `drop_pen_out_of_bounds` | Pen ABS values outside device bounds |
| `drop_orphan_abs` | ABS position events before any tool press |
| `drop_missing_syns` | Events when SYN_REPORT framing is broken |
| `drop_event` | Returns `drop_event` (any event) |
| `exit_pipeline` | Returns `exit` (stops the pipeline) |

### `enforce_key_state`

Enforce valid key state transitions: no orphan releases, no repeats without
prior press, no double presses.

### `sanitizer` / `diagnostics` / `sieve`

Pre-built filter groups:

- `sanitizer` -- all default checks, silent drop
- `diagnostics` -- same checks, log only (no drop)
- `sieve` -- drop bad events and log each one

## Conditions and control flow

### `on` / `once`

`on[cond, ...actions]` runs actions while a condition is true (toggles
on/off). `once[cond, ...actions]` fires only when the condition switches on.

```cpp
on[pressed[BTN_RIGHT], drop_start_moves]
once[pressed[KEY_CAPSLOCK, KEY_LEFTSHIFT, KEY_ESC], exit_pipeline]

// Sub-pipeline inside on:
on[pressed[KEY_CAPSLOCK] | led_off[LED_CAPSL],
   context | abs2rel | pen2mice | router[mouse >> uinput]]
```

### `pressed` / `pressed_any`

True when specific key(s) are currently down.

```cpp
pressed[KEY_A]
pressed_any[BTN_LEFT, BTN_RIGHT]
```

### `keydown` / `keyup`

Match a key press or release event (edge-triggered, not level).

```cpp
keydown[KEY_X]
keyup[BTN_LEFT]
```

### `held` / `on_held`

`held[key]` is true while a key is held (suppresses auto-repeat).
`held[key, decider]` gates whether the held key is swallowed or emitted.

`on_held[modifier, key, mod]` runs a mod while modifier keys are held. A quick
tap re-emits as a real press+release; a hold swallows the release.

```cpp
on[held[KEY_LEFTSHIFT], context | scale_move[0.5f]]
on_held[KEY_CAPSLOCK, BTN_MIDDLE, context | mouse_to_scroll]
```

### `hold_mod`

Run a mod while modifier keys are held. Quick taps re-emit as real
press+release, holds swallow the release.

```cpp
hold_mod[KEY_CAPSLOCK, BTN_MIDDLE, mouse_to_scroll]
```

### `longtime_released`

True when a key has been released for a while.

### `limit_mouse_travel`

True while mouse travel stays under a limit.

```cpp
once[limit_mouse_travel[pressed[KEY_CAPSLOCK], 50] & keyup[BTN_LEFT],
     schedule_emit + press(BTN_RIGHT)]
```

### `multi_click` / `double_click` / `triple_click`

Double/triple click detection. Requires `keys_state` in the pipeline.

```cpp
once[pressed[BTN_MIDDLE] & triple_click, emit[press(KEY_LEFTMETA, KEY_TAB)]]
```

### `swipe_*`

Swipe detection: `swipe_left`, `swipe_right`, `swipe_up`, `swipe_down`.
Requires `swipe_detector` in the pipeline.

```cpp
on[pressed_any[KEY_CAPSLOCK, BTN_MIDDLE] & pressed[BTN_LEFT],
   context
     | on[swipe_right, emit[press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_RIGHT)]]
     | on[swipe_left, emit[press(KEY_LEFTCTRL, KEY_LEFTMETA, KEY_LEFT)]]]
```

### `led_on` / `led_off`

Conditions based on keyboard LED state. Requires `led_state` in the pipeline.

```cpp
on[pressed[KEY_CAPSLOCK] | led_off[LED_CAPSL], ...]
```

### `tilted` / `tilt_changing`

Pen-tilt conditions. Require `tilt_state` in the pipeline.

```cpp
on[tilted[0.5F], ...]         // tilt magnitude (0..1) at/above the threshold
on[tilt_changing[0.15F], ...] // per-event tilt change at/above the threshold
```

### `op`

Boolean combinators. `op` is an empty `and`; `op | condition` is the
idiomatic way to start a condition.

```cpp
op & pressed[KEY_A] & pressed[KEY_B]
op | pressed[KEY_F1]
!always_disable
```

### `modes` / `switch_mode`

Vim-like modes/layers. Cycles through mod pipelines when a trigger condition
is true.

```cpp
modes[multi_click[KEY_RIGHTCTRL],
      context,                                       // Mode 0: empty
      context                                         // Mode 1: express
        | replace[KEY_D, KEY_LEFTMETA, KEY_LEFTCTRL, KEY_RIGHT]
        | on[pressed[KEY_ESC], switch_mode[0]]]
```

### `on_fail`

When a condition check fails, invoke a side-effect action.

```cpp
on_fail[condition, action, extra...]
```

### `stopper`

Exit the pipeline on demand. Call `stopper.stop()` to set the flag; the next
event causes the pipeline to return `exit`.

## State

### `keys_state`

Tracks the current state of every key via a bitset. Provides `is_pressed()`,
`is_pressed_any()`, `is_released()`, `release_all()`. Seeds from the device's
`EVIOCGKEY` bitmap on connect.

### `led_state` / `led_toggle`

`led_state` tracks keyboard LED state (CapsLock, NumLock, ScrollLock).
`led_toggle` flips the CapsLock mode. Also provides `capslock_off`,
`numlock_off`, `scrolllock_off`.

### `tilt_state`

Tracks the pen's `ABS_TILT_X` / `ABS_TILT_Y`, normalized against the device's
tilt range. Exposes `norm_x()`, `norm_y()`, `normalized_magnitude()`,
`change()`, `is_tilted()` and `is_changing()`. Derived values are computed only
on tilt events, and `version()` lets the actions cache their factor. Required
by `tilt_speed`, `tilt_freeze`, `tilt_push`, `tilted` and `tilt_changing`.

The values are relative to a **base (neutral) tilt** — the angle the user
naturally holds the pen at — so a natural hold reads as zero and the actions
respond to how far the pen is tilted *from* that hold:

- The base is captured as a per-axis vector every time the pen comes into
  proximity (`BTN_TOOL_*`), so picking the pen up re-zeroes it.
- The base is also continuously recentered toward the current tilt, with a
  configurable time constant:

```cpp
tilt_state[tilt_base_options{.recenter_time = 3.0F}]   // seconds; <= 0 disables
```

`recenter_time` is frame-rate independent (`alpha = 1 - exp(-dt / tau)`), so
smaller values adapt faster but also absorb a deliberately held tilt sooner.
`change()` stays measured from the raw tilt, so `tilt_freeze` is unaffected by
the base. `base_x()` / `base_y()` expose the current base (normalized).

### `mouse_history`

Tracks current/previous mouse positions in a circular buffer. Updated on
`EV_REL` events, pushed on `EV_SYN`. Required by smoothing mods (`lerp`,
`low_pass_filter`, `kalman_filter`).

### `quantifier` / `mice_quantifier`

Threshold-step accumulation for movement. `quantifier` works for a single
event code; `mice_quantifier` tracks both X and Y axes. Required by
`mouse_to_scroll`.

### `var_type`

Typed pipeline variables. Share values between mods via `context["name"]`
lookup.

### `startup_key_releases`

On device connect, queries the `EVIOCGKEY` bitmap and releases any held keys.
Useful when the pipeline is launched by a key press.

## Device conditions

The `device` mod provides conditions and filters based on which device an
event came from:

| Mod | What it does |
|-----|--------------|
| `from_device` | True if event is from a specific device node |
| `from_stdin` | True if event is from stdin (redirect mode) |
| `self_emitted` | True if event was synthesized by this pipeline |
| `from_chained` | True if event is from another foresight process |
| `device_is` | Check device identity |
| `only_device` | Drop events not from the specified device |
| `drop_device` | Drop events from a specific device |
| `drop_origin` | Drop events by source ID |
| `drop_self` | Drop synthesized + owned events |
| `drop_owned` | Drop only events from owned uinput devices |
| `drop_emitted` | Drop only synthesized events |

## Utility

### `run` (lambda)

Wrap one or more functions so they can be used as mods or callbacks.

```cpp
on[pressed[KEY_A], run([](Context auto& ctx) { /* ... */ })]
```

### `record`

Record events into a buffer for later replay or inspection. Provides
`events()`, `count()`, `any()`, `all()`, `filter()`, `keys()`.

### `group_mod`

Run a tuple of mods as a single mod. If any mod returns `drop_event`, the
event is dropped.

### `benchmark`

Performance measurement wrapper. Records call count, total/average/min/max
latency, standard deviation, and percentiles (p50/p95/p99).

### `singleton`

Prevent multiple instances of the same pipeline from running concurrently. Uses
a Linux abstract Unix socket.

### `live_view`

Terminal event display. `condensed_view_output` aggregates mouse movements by
direction and tracks keyboard hold durations. Used by
`foresight replay --live`.

## Context actions

Mods return a `context_action` to communicate intent:

| Action | Meaning |
|--------|---------|
| `next` | Pass the event to the next mod. |
| `drop_event` | Drop this event. |
| `recovery` | Restart / enter watch mode. |
| `exit` | Exit the pipeline. |
