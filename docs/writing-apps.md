# Writing your own app

Foresight is designed to **be used as a library**. The library exposes a
type-safe, compile-time **pipeline**: you chain *mods* (event modifiers) with
`operator|`, and the pipeline drives them with an event *context*.

The full API surface is documented in the [API Reference](api-reference.md),
which is generated from the code with Doxygen. This page is a quick tour of the
concepts you need to write your own app.

## The pipeline

A pipeline is built by chaining mods together:

```cpp
import fs8.mods;

static constinit auto pipeline =
    fs8::context
    | fs8::io_manager
    | fs8::input_manager
    | fs8::intercept[tablet | required | grab]
    | fs8::keys_state
    | fs8::on[fs8::pressed[KEY_A], /* do something */]
    | fs8::output;

int main() {
    pipeline();
}
```

`fs8::context` is the empty starting point. Each `| mod` appends a mod to the
context's compile-time tuple. `pipeline()` starts the mods, then runs the event
loop until an event provider signals exit.

## Mods and context

Every mod is a callable object. The context (`fs8::basic_context`) carries the
current `event_type` and the tuple of mods. A mod can be invoked in several
forms depending on what it accepts:

- `mod(ctx)` — sees the whole context (event + sibling mods).
- `mod(event)` — only needs the current event.
- `mod(ctx, tag)` — a *tag* request, e.g. `start_tag`, `load_event_tag`, `next_event_tag`.

Mods return a `context_action`:

| Action         | Meaning                                            |
|----------------|----------------------------------------------------|
| `next`         | Pass the event to the next mod.                    |
| `drop_event` | Drop this event.                                   |
| `idle`         | Restart/enter watch mode.                          |
| `exit`         | Exit the pipeline.                                 |

A mod that returns `bool` is interpreted as `true` → `next`, `false` →
`drop_event`.

## Events

`fs8::event_type` wraps a kernel `input_event` and adds helpers:

- `event.is(type, code)`, `event.is(type, code, value)` — match an event.
- `event.type()`, `event.code()`, `event.value()` — accessors.
- `event.source()` — a `device_id` telling where the event came from
  (`stdin`, `self`, or a hash of the device's sysname).

Common helpers exist as constants, e.g. `fs8::syn()` for `EV_SYN / SYN_REPORT`.

## Conditions and actions

The `on` mod is the main control structure. `on[condition, ...actions]` runs the
actions whenever the condition is true (and toggles on/off when it switches):

```cpp
using namespace fs8;

auto pipeline = context
    | on[op | pressed[KEY_F1], context
        | replace[KEY_A, KEY_B]
        | emit[press(KEY_C)]];

// `op` is an empty `and`; `op | condition` is the idiomatic way to start a condition.
```

- **Conditions** are callables returning `bool`. Built-ins include `pressed`,
  `pressed_any`, `keydown`, `keyup`, `held`, `led_on`, `led_off`, `multi_click`,
  `swipe_left/right/up/down`, `longtime_released`, `limit_mouse_travel`,
  `timed_typed`, and the combinators `op` (`and`/`or` via `&`/`|`).
- **Actions** are callables too — mods, emitters, or lambdas. `on` composes with
  `operator|` and `[]`, e.g. `on[cond, context | mod_a | mod_b]`.
- `once[...]` is like `on` but only fires when the condition *switches on*.

## Emitting events

To synthesize events, use `emit` and `press`, or `ctx.fork_emit(...)` directly
inside a mod:

```cpp
ctx.fork_emit(event_type{EV_KEY, KEY_A, 1});
ctx.fork_emit(fs8::syn());
```

`fork_emit` re-runs the pipeline from a given point with the new event, so the
emitted event also flows through the downstream mods.

## Device queries

`device_query` values select devices. A query can be a device name, a path
(e.g. `/dev/input/event1`), or a udev term (e.g. `"keyboard"`). The `intercept`
mod accepts queries through `operator[]`:

```cpp
intercept[tablet | required | grab, keyboard | required | grab]
```

`required` fails startup if nothing matches; `grab` steals the device so other
processes can't read it.

## Example

Here is a small, realistic app that turns your pen tablet into a mouse — a
reduced version of [`apps/pen2mice`](apps.md):

```cpp
#include <linux/input-event-codes.h>
import fs8.mods;
import fs8.cli;
import fs8.devices.queries;

static constinit auto pipeline =
    fs8::context
    | fs8::io_manager
    | fs8::input_manager
    | fs8::intercept[fs8::tablet | fs8::required | fs8::grab]
    | fs8::abs2rel                // absolute → relative moves
    | fs8::pen2mice               // translate pen buttons
    | fs8::keys_state
    | fs8::on[fs8::pressed[BTN_RIGHT], fs8::drop_start_moves]
    | fs8::router[fs8::caps::mouse >> fs8::uinput];

int main() {
    pipeline();
}
```

Each step in that pipeline is a `fs8::mods` module export. See the
[Mods overview](mods.md) for the catalog, and `apps/*/` in the repository for
more complete examples.