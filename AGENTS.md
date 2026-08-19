# AGENTS.md

Foresight is a Linux-only input manager: a C++26 library (`libforesight`) plus
the `foresight` CLI, built with CMake + Ninja. Needs GCC 14+ or Clang 16+.

## Repository layout

All library code lives behind C++26 modules; the directory layout mirrors the
`fs8.*` module namespaces.

| Directory   | Modules / contents                                                      |
|-------------|-------------------------------------------------------------------------|
| `apps/`     | Example apps (`pen2mice`, `x2y`, ...). Each links `foresight::foresight`; register new ones via `add_subdirectory` in `apps/CMakeLists.txt`. |
| `bash/`     | `fs8.bash` — `bash_runner` (embedded bash interpreter).                 |
| `devices/`  | `fs8.devices.*` — `evdev`, `uinput`, `udev`, `queries`, `capabilities`, `key_codes`, and the generated `inputs-event-codes` (`fs8.devices.event_codes`). |
| `io/`       | liburing header-only wrapper (via CPM). **Not a module.**               |
| `lib/`      | `fs8.lib.*` — `mod_parser` (key/modifier-tag parsing), `xkb`, `xkb.how2type`, `xkb.event2unicode`, `evtest`. |
| `main/`     | The `fs8` umbrella (`keyboard`, `translate`, `log`), plus `fs8.context`, `fs8.event`, `fs8.cli`, `fs8.utils` (re-exports `fs8.cli`), `fs8.log`, `fs8.systemd`, `fs8.scaffold`. The `foresight` CLI entry is `main/main.cxx`. |
| `mods/`     | The `fs8.mods.*` pipeline mods, the `fs8.mods` umbrella, and the `fs8.context:vars` partition. |
| `tests/`    | GoogleTest suites (built in Debug only).                                |
| `tools/`    | Codegen: `update.sh` + `gen-keys.awk` (regenerate `inputs-event-codes`). |
| `utils/`    | `fs8.pimpl`, `fs8.traits`, `fs8.hash`, `fs8.strings`, `fs8.nullable_indirect`, `fs8.easings`, `dynamic_scoping`. |

## Build / test

Presets: `gcc-debug`, `gcc-release`, `clang-debug`, `clang-release`; build dirs
are `cmake-build-{debug,release}[-clang]`. Each also has a matching test preset
and a `cmake --workflow` preset (configure + build + test).

```sh
cmake --preset gcc-debug
cmake --build --preset gcc-debug
```

- Tests are GoogleTest and are **only built in Debug** (the root CMakeLists.txt
  guards `add_subdirectory(tests)` on `IS_DEBUG`).
- `enable_testing()` is called from `tests/CMakeLists.txt` (a subdir), so no
  root `CTestTestfile.cmake` is generated. Run ctest from the tests dir:
  `ctest --test-dir cmake-build-debug/tests`. (The docs' `cmake --test --preset`
  is a typo; that flag doesn't exist.)
- Each `tests/*_test.cxx` also builds a dedicated target `test-<file>` (e.g.
  `test-bash`, `test-io-manager`), plus a combined `foresight-tests`. Run one
  suite with `./cmake-build-debug/tests/test-bash` or
  `ctest --test-dir cmake-build-debug/tests -R test-bash`.
- `cmake --workflow --preset gcc-debug` = configure + build + test.
- There is **no CI that compiles or runs the C++** (`.github/workflows/docs.yml`
  only builds docs) — verify locally after changes.

## The pipeline and mods (core concept)

The library is a type-safe, compile-time pipeline of **mods**. A mod is a
`consteval`-copyable callable object in the `fs8` namespace; pipelines chain
them with `operator|` starting from `fs8::context`:

```cpp
import fs8.mods;

static constinit auto pipeline =
    fs8::context
    | fs8::io_manager
    | fs8::input_manager
    | fs8::intercept[fs8::keyboard | fs8::required | fs8::grab]
    | fs8::keys_status
    | fs8::on[fs8::pressed[KEY_A], /* do something */]
    | fs8::output;

int main() { pipeline(); }
```

`pipeline()` runs the `start` phase (open devices, register fds, seed state),
then loops: `next_event` providers drain ready events, `load_event` blocks for
the next batch, and each event is pushed through the mod tuple.

### Invocation

A mod can be invoked in several forms depending on what it accepts:
- `mod(ctx)` — sees the whole context (event + sibling mods).
- `mod(event)` — only needs the current event.
- `mod(ctx, tag)` — a *tag* request (see below).

Mods return a `context_action`; a mod returning `bool` means `true` → `next`,
`false` → `ignore_event`.

| Action         | Meaning                                            |
|----------------|----------------------------------------------------|
| `next`         | Pass the event to the next mod.                    |
| `ignore_event` | Drop this event.                                   |
| `idle`         | Restart / enter watch mode.                        |
| `exit`         | Exit the pipeline.                                 |

Tags are constexpr sentinels passed as the last argument:
`start`, `no_init`, `load_event` (blocking wait for input), `next_event`
(non-blocking event pull), `toggle_on` / `toggle_off` (on condition switches),
`get_variables` (mod variable introspection).

### Mod catalog

**Event providers** (put events into the pipeline):
| Mod | What it does | Needs in pipeline |
|-----|--------------|-------------------|
| `intercept` | Query-driven provider; reads kernel devices matching `device_query`es and feeds their events in. | `io_manager`, `input_manager` |
| `io_manager` | poll()-based fd readiness; watches fds, wakes the pipeline via `load_event`. | — |
| `input_manager` | Owns/monitors devices: resolves queries, hotplug, "which device did this event come from?". | — |
| `from_input` | Reads raw events from stdin (redirect mode). | — |
| `emit_all` | Plays back a fixed event array (a `load_event` provider). | — |

**Output** (write/synthesize events):
| Mod | What it does |
|-----|--------------|
| `output` | Writes events to an fd (stdout by default). An `OutputModifier`. |
| `uinput` | Creates virtual devices under `/dev/uinput`. |
| `router` | Routes events to outputs by capability, e.g. `router[caps::mouse >> uinput]`. |
| `emit` / `schedule_emit` | Synthesize events (`emit[press(...)]`); `press`, `keypress`, `down`, `up`, `turn_led_on/off` are the helpers. |

**Transformers** (change events):
| Mod | What it does |
|-----|--------------|
| `replace` / `put` | Rewrite one key/chord into another sequence. |
| `abs2rel`, `pen2mice`, `pen2touch`, `pressure2mouse_clicks` | Convert drawing-tablet absolute events into relative mouse events / clicks. |
| `add_scroll` | Convert mouse movement into scroll-wheel events. | `mice_quantifier` |
| `smooth` (`lerp`, `low_pass_filter`, `kalman_filter`) | Smooth mouse movement. | `mouse_history` |
| `momentum` | Keep motion going after the input stops (`velocity_tracker`, `momentum_calculator`). | — |
| `ignore_*` | Filters: `ignore_big_jumps`, `ignore_init_moves`, `ignore_start_moves`, `ignore_mouse_moves`, `ignore_fast_repeats`, `ignore_adjacent_repeats`, `ignore_repeats_of`, `ignore_keys`, `ignore_abs`, `ignore_tablet`, `ignore_caps`, plus `ignore_event` and `exit_pipeline`. | — |
| `typed`, `timed_typed` | Match what the user typed (`timed_typed` adds a time window). | `search_engine` |
| `type_string` (`typer`) | Type text into the app via xkb how2type. | — |
| `autocomplete` | Watch typed patterns and complete them (`PREFIX<TAG>COMPLETION`). | — |

**Conditions and control flow** (usable inside `on[...]`/`once[...]`):
| Mod | What it does | Needs in pipeline |
|-----|--------------|-------------------|
| `on`, `once` | Run actions while/once when a condition is true. | — |
| `held` | True while a key/chord is held; `held[key, decider]` gates it. | — |
| `pressed`, `pressed_any` | True when specific keys are down. | `keys_status` |
| `keydown`, `keyup` | Match a key press / release event. | — |
| `multi_click` (`double_click`, `triple_click`) | Double/triple click detection. | — |
| `swipe_left/right/up/down` | Swipe detection. | `swipe_detector` |
| `longtime_released`, `limit_mouse_travel` | Time / distance gates. | — |
| `led_on`, `led_off` | Keyboard LED state conditions. | `led_status` |
| `op` (`&`, `|`, `!`), `always_enable`, `always_disable` | Boolean combinators. | — |
| `modes` / `switch_mode` | Vim-like modes/layers. | — |
| `run` (`lambda`) | Wrap arbitrary functions as mods/callbacks. | — |

**State** (tracked by the pipeline):
| Mod | What it does |
|-----|--------------|
| `keys_status` | Current state of every key (+ `led_status`, `led_toggle`). |
| `mouse_status` / `mouse_history` | Current/previous mouse positions. |
| `quantifier` / `mice_quantifier` | Threshold-step accumulation for movement. |
| `device` (`device_is`, `only_device`, `ignore_device`, `ignore_origin`, `ignore_self`, `from_device`, `from_stdin`, `self_emitted`, `from_chained`) | Conditions on which device an event came from. |
| `var_type` (`vars`) | Typed pipeline variables; read back via `context[name]`. |

**Misc**: `record` (record events into a buffer for tests/inspection), `stopper`
(exits the pipeline on demand).

Inter-mod dependencies are enforced at compile time via `static_assert`s (e.g.
"We need keys_status to be in the pipeline."), so a pipeline that forgets a
state mod fails to build.

### Mod invariants

- Mods derive from `consteval_copyable`: **runtime copies abort** — they are
  copyable at compile time only. Stateful mods hide storage behind
  `pimpl_idiom` (see `utils/pimpl.ixx`).
- Every mod must be `nothrow`-invocable (`static_assert`s in
  `main/context.ixx` enforce this).
- `pimpl_idiom`-based mods (`intercept`, `io_manager`, `input_manager`, ...)
  allocate lazily at `start`; handlers are bound by reference and must outlive
  the pipeline.

## C++26 modules (the big gotcha)

All library code is C++26 modules: interfaces are `.ixx`, paired with `.cxx`
implementation units. Module names use the `fs8.*` namespace (`fs8.mods.*`,
`fs8.devices.*`, `fs8.context`, ...).

Umbrella / re-export modules:
- `fs8` (`main/main.ixx`) — exports `fs8.keyboard`, `fs8.translate`, `fs8.log`.
- `fs8.mods` (`mods/mods.ixx`) — re-exports the context/event/capabilities
  plumbing, the state/utility mods, and **every pipeline mod**.
- `fs8.utils` (`main/utils.ixx`) — re-exports `fs8.cli` alongside small helpers.
- Apps `import fs8.mods` (and often `fs8.cli`, `fs8.log`, `fs8.devices.*`).

Partitions: `mods/context_vars.ixx` is a **module partition** of `fs8.context`
(`fs8.context:vars`) even though it lives in `mods/`. `io/liburing.ixx` is not
a module — it only includes liburing's headers.

**Adding a module requires registering the files in the root `CMakeLists.txt`
in two places**, or the build silently omits them:
1. `target_sources(... PRIVATE ...)` for the `.cxx`,
2. the `PUBLIC FILE_SET foresight TYPE CXX_MODULES FILES` list for the `.ixx`.

A new pipeline mod must also be `export import`-ed from `mods/mods.ixx` and,
where relevant, `static_assert` its `Modifier` / `OutputModifier` concept.
`foresight new` scaffolds standalone apps that build with
`-DFORESIGHT_SOURCE_DIR=<foresight checkout>`.

## Generated code — do not hand-edit

`devices/inputs-event-codes.ixx` is generated from
`/usr/include/linux/input-event-codes.h` by `tools/update.sh` (awk +
clang-format). Regenerate with that script instead of editing.

## Toolchain quirks

- `compile_commands.json` is only exported for **Clang** builds (`IS_CLANG`);
  `.clangd` points at `cmake-build-debug/compile_commands.json`. Use the
  `clang-debug` preset for editor/analysis support.
- Formatting/lint: `.clang-format` and `.clang-tidy` are configured but not
  wired into any build target; run `clang-format` yourself on changed files.

## Dependencies

- libevdev (system via pkg-config preferred, else CPM builds it from source),
  libudev, libxkbcommon; liburing is header-only via CPM (`io/`).
- GTest is fetched via CPM in Debug if `find_package(GTest)` fails.

## Docs

- Guides: MkDocs Material in `docs/` (`mkdocs build --strict`; preview with
  `mkdocs serve`). `docs/mods.md` is a curated catalog of the mods above.
- API reference: Doxygen generated from the `///` comments in the `.ixx` files.
  Build with `cmake --build <build> --target docs`, or run
  `FS8_DOC_OUTPUT=<dir> doxygen Doxyfile` from the repo root.