# Example apps

The repository ships a set of example apps under [`apps/`](https://github.com/the-moisrex/foresight/tree/main/apps).
They are small, focused demonstrations of what you can build with the Foresight library — from
simple intercept/redirect filters to full pipeline apps.

## x2y

Change `x` on your keyboard to `y` when you press it. The simplest possible
example, written in plain C — no Foresight library needed, just the
[intercept/redirect](cli.md) pipeline.

## pen2mice

Converts pen tablet input into mouse-like actions, letting a drawing tablet act
as a mouse with extended capabilities:

- absolute pen moves → relative mouse moves (`abs2rel`, `pen2mice`)
- gesture detection for navigation commands
- normal and "express" modes with custom key bindings (`modes`)
- Caps Lock as a modifier key, scroll enhancement, and click-jump fixes

## auto-typer

Watches a keyboard for typed patterns and auto-completes them into longer strings.

## debounce

Debounces events on the general `debounce` mod: events that arrive within the
debounce window of a previous event of the same code are dropped. By default it
fixes a faulty mouse that occasionally double clicks (a press and its matching
release that land within the window are dropped), but `--codes` make it debounce
any event — bouncing keyboard keys, noisy tablet axes, double-firing scroll
wheels. The window defaults to 30ms and is adjustable via `-t | --time`
(e.g. `debounce --time 50ms`).

## flat-accelerate

Accelerates the mouse through a `foresight intercept | flat-accelerate | foresight redirect`
pipeline.

## drop-big-jumps

Drops the big jumps of a faulty mouse.

## long-press

Turns a long press into a right-click action.

## on-gestures

Turns `Meta` + `Ctrl` + mouse move into scrollbars.

## Writing your own

Each app links against the Foresight library (`foresight::foresight`) and
compiles as C++26. To scaffold a new app, see [writing your own app](writing-apps.md)
or use the CLI's `foresight new` template generator.