# Mods overview

A *mod* (module / modifier) is the building block of a Foresight pipeline. The
library exports all of them through a single umbrella module:

```cpp
import fs8.mods;
```

Everything in the `fs8` namespace. The full signatures live in the
[API Reference](api-reference.md); this page is a curated catalog of what each
mod is for and when you'd reach for it.

## Getting events in and out

| Mod | What it does |
|-----|--------------|
| `intercept` | Event provider. Reads kernel input devices (selected by `device_query`) and feeds their events into the pipeline. |
| `io_manager` | Watches file descriptors (poll-based readiness) and wakes the pipeline when an event is available. |
| `input_manager` | Owns and monitors input devices; resolves queries, tracks hotplug, and answers "which device did this event come from?". |
| `output` | Writes events to a file descriptor (stdout by default) — the library-side `redirect`. |
| `uinput` | Creates virtual devices (`/dev/uinput`) that events can be written to. |
| `router` | Routes events to specific output devices, e.g. `router[caps::mouse >> uinput]`. |

## Transforming events

| Mod | What it does |
|-----|--------------|
| `replace` | Replace one key (or chord) with another sequence, e.g. `replace[KEY_D, KEY_LEFTMETA, KEY_LEFTCTRL, KEY_RIGHT]`. |
| `abs2rel` | Convert absolute events (drawing tablets) into relative events (mouse). |
| `pen2mice` | Translate a pen tablet's buttons/tools into mouse clicks. |
| `add_scroll` | Convert mouse movement into scroll wheel events (conditionally). |
| `smooth` | Smooth mouse movement / ease the output: `lerp[max_steps, easing]`, `low_pass_filter[alpha]`, `kalman_filter[q, r]`. Requires `mouse_history` placed before it in the pipeline. |
| `momentum` | Keep mouse momentum going after you stop moving. |
| `ignore` | Family of "ignore" filters: big jumps, starting moves, fast repeats, adjacent repeats, fast double clicks, and full event ignoring. |
| `typed` | Track what the user is typing/editing. |
| `timed_typed` | Like `typed`, but only matches if the pattern is typed within a time window (`timed_typed["test", 2s]`); pauses longer than the window discard the partial match. |
| `typer` | Type text (how2type) into the current application. |
| `autocomplete` | Watch typed patterns and auto-complete them into longer strings. |
| `record` | Record events into a buffer for later replay or comparison. |

## Conditions and control flow

| Mod | What it does |
|-----|--------------|
| `on` | Run actions while a condition is true (`on[cond, ...actions]`); toggles on/off. |
| `once` | Like `on`, but only fires when the condition *switches on*. |
| `held` | True while a key/chord is held; `held[key, decider]` gates the held keys (swallow or emit). |
| `pressed` / `pressed_any` | True when specific key(s) are currently down. |
| `keydown` / `keyup` | Match a key press / release event. |
| `multi_click` | Double/triple click detection (`double_click`, `triple_click`). |
| `swipe_*` | Swipe detection (`swipe_left`, `swipe_right`, `swipe_up`, `swipe_down`). |
| `longtime_released` | True when a key has been released for a while. |
| `limit_mouse_travel` | True while mouse travel stays under a limit. |
| `led_on` / `led_off` | Conditions based on keyboard LED state. |
| `op` | Boolean combination: `op & cond & cond`, `op | cond | cond`. |
| `modes` | Vim-like modes and layers, e.g. `modes[trigger, normal_ctx, express_ctx]`. |
| `lambda` | Wrap one or more functions so they can be used as mods/callbacks (`run`). |

## State and context

| Mod | What it does |
|-----|--------------|
| `keys_status` | Tracks the current state of every key. |
| `mouse_status` | Tracks the current mouse buttons. |
| `quantifier` | Quantifies/measures events (e.g. mouse movement thresholds). |
| `device` | Conditions/filters based on which device an event came from (`device_is`, `only_device`, `ignore_device`). |
| `vars` | Pipeline variables — share values between mods (`context[name]` lookup). |
| `emitter` / `scheduled_emitter` | Synthesize events (`emit[press(...)]`) or schedule them to fire. |

## Context actions

Mods communicate their intent by returning a `context_action`:

| Action         | Meaning                                            |
|----------------|----------------------------------------------------|
| `next`         | Pass the event to the next mod.                    |
| `ignore_event` | Drop this event.                                   |
| `idle`         | Restart / enter watch mode.                        |
| `exit`         | Exit the pipeline.                                 |

For the definitive list and signatures, see the [API Reference](api-reference.md),
which is generated directly from the source with Doxygen.