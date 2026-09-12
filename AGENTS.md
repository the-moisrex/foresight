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
| `devices/`  | `fs8.devices.*` — `evdev`, `udev`, `queries`, `capabilities`, `key_codes`, and the generated `inputs-event-codes` (`fs8.devices.event_codes`). `uinput.ixx`/`uinput.cxx` are the `fs8.mods:uinput` partition (they live here but belong to the `fs8.mods` module). |
| `io/`       | liburing header-only wrapper (via CPM). **Not a module.**               |
| `lib/`      | `fs8.lib.*` — `mod_parser` (key/modifier-tag parsing), `xkb`, `xkb.how2type`, `xkb.event2unicode`, `evtest`. |
| `main/`     | The `fs8` umbrella (`keyboard`, `translate`, `log`), plus `fs8.context`, `fs8.event`, `fs8.cli`, `fs8.utils` (re-exports `fs8.cli`), `fs8.log`, `fs8.systemd`, `fs8.scaffold`. The `foresight` CLI entry is `main/main.cxx`. |
| `mods/`     | The `fs8.mods` umbrella and its partitions `fs8.mods:<name>` (one per pipeline mod), plus the `fs8.context:vars` partition. |
| `tests/`    | GoogleTest suites (built in Debug only).                                |
| `tools/`    | Codegen: `update.sh` + `gen-keys.awk` (regenerate `inputs-event-codes`). |
| `utils/`    | `fs8.pimpl`, `fs8.traits`, `fs8.hash`, `fs8.strings`, `fs8.nullable_indirect`, `fs8.easings`, `dynamic_scoping`. |

## Build / test

Presets: `debug-gcc`, `release-gcc`, `debug-clang`, `release-clang`; build dirs
are `build-{debug,release}-{gcc,clang}`. Each also has a matching test preset
and a `cmake --workflow` preset (configure + build + test).

```sh
cmake --preset debug-gcc
cmake --build --preset debug-gcc
```

- Tests are GoogleTest and are **only built in Debug** (the root CMakeLists.txt
  guards `add_subdirectory(tests)` on `IS_DEBUG`).
- `enable_testing()` is called from `tests/CMakeLists.txt` (a subdir), so no
  root `CTestTestfile.cmake` is generated. Run ctest from the tests dir:
  `ctest --test-dir build-debug-gcc/tests`. (The docs' `cmake --test --preset`
  is a typo; that flag doesn't exist.)
- Each `tests/*_test.cxx` also builds a dedicated target `test-<file>` (e.g.
  `test-bash`, `test-io-manager`), plus a combined `foresight-tests`. Run one
  suite with `./build-debug-gcc/tests/test-bash` or
  `ctest --test-dir build-debug-gcc/tests -R test-bash`.
- `cmake --workflow --preset debug-gcc` = configure + build + test.
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
    | fs8::keys_state
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
`false` → `drop_event`.

| Action         | Meaning                                            |
|----------------|----------------------------------------------------|
| `next`         | Pass the event to the next mod.                    |
| `drop_event` | Drop this event.                                   |
| `recovery`     | Restart / enter watch mode.                        |
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
| `mouse_to_scroll` | Convert mouse movement into scroll-wheel events. Pure transformer; gate it with `hold_mod`. | `mice_quantifier` |
| `smooth` (`lerp`, `low_pass_filter`, `kalman_filter`) | Smooth mouse movement. | `mouse_history` |
| `momentum` | Keep motion going after the input stops (`velocity_tracker`, `momentum_calculator`). | — |
| `ignore_*` | Filters: `drop_big_jumps`, `drop_init_moves`, `drop_start_moves`, `drop_mouse_moves`, `drop_fast_repeats`, `drop_adjacent_repeats`, `drop_repeats_of`, `drop_keys`, `drop_abs`, `drop_tablet`, `drop_caps`, plus `drop_event` and `exit_pipeline`. | — |
| `debounce` | Drop events arriving within a window of the same code. For `EV_KEY` codes a fast second press + its release are dropped; for other event types every event within the window is dropped. Any `event_code`: `debounce[BTN_LEFT]`, `debounce[{.type = EV_ABS, .code = ABS_X}]`. | — |
| `typed`, `timed_typed` | Match what the user typed (`timed_typed` adds a time window). | `search_engine` |
| `type_string` (`typer`) | Type text into the app via xkb how2type. | — |
| `autocomplete` | Watch typed patterns and complete them (`PREFIX<TAG>COMPLETION`). | — |

**Conditions and control flow** (usable inside `on[...]`/`once[...]`):
| Mod | What it does | Needs in pipeline |
|-----|--------------|-------------------|
| `on`, `once` | Run actions while/once when a condition is true. | — |
| `held` | True while a key/chord is held; `held[key, decider]` gates it. | — |
| `hold_mod` | Run a mod while modifier keys are held (`hold_mod[KEY_CAPSLOCK, BTN_MIDDLE, mouse_to_scroll]`); quick taps re-emit as a real press+release, holds/swallowed keys don't. | — |
| `pressed`, `pressed_any` | True when specific keys are down. | `keys_state` |
| `keydown`, `keyup` | Match a key press / release event. | — |
| `multi_click` (`double_click`, `triple_click`) | Double/triple click detection. | — |
| `swipe_left/right/up/down` | Swipe detection. | `swipe_detector` |
| `longtime_released`, `limit_mouse_travel` | Time / distance gates. | — |
| `led_on`, `led_off` | Keyboard LED state conditions. | `led_state` |
| `op` (`&`, `|`, `!`), `always_enable`, `always_disable` | Boolean combinators. | — |
| `modes` / `switch_mode` | Vim-like modes/layers. | — |
| `run` (`lambda`) | Wrap arbitrary functions as mods/callbacks. | — |

**State** (tracked by the pipeline):
| Mod | What it does |
|-----|--------------|
| `keys_state` | Current state of every key (+ `led_state`, `led_toggle`). |
| `mouse_state` / `mouse_history` | Current/previous mouse positions. |
| `quantifier` / `mice_quantifier` | Threshold-step accumulation for movement. |
| `device` (`device_is`, `only_device`, `drop_device`, `drop_origin`, `drop_self`, `from_device`, `from_stdin`, `self_emitted`, `from_chained`) | Conditions on which device an event came from. |
| `var_type` (`vars`) | Typed pipeline variables; read back via `context[name]`. |

**Misc**: `record` (record events into a buffer for tests/inspection), `stopper`
(exits the pipeline on demand).

Inter-mod dependencies are enforced at compile time via `static_assert`s (e.g.
"We need keys_state to be in the pipeline."), so a pipeline that forgets a
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

## Common utilities & idioms (read this before grepping)

These are the building blocks used everywhere. Paths are relative to the repo
root; import the listed module instead of re-deriving the pattern.

### `consteval_copyable` — `utils/traits.ixx`, module `fs8.traits`

Base class every mod derives from. Copying is allowed **only during constant
evaluation**; a runtime copy calls `fprintf(stderr, ...)` + `std::abort()`
(the check uses `if !consteval`). Moving is normal. The default ctor is
hand-written (not `= default`) so clang emits it into the BMI for down-stream
modules.

```cpp
struct [[nodiscard]] my_mod : consteval_copyable {
    using consteval_copyable::consteval_copyable;
};
```

`pretty_type_name<T>()` is a `consteval` helper returning the short type name
from `__PRETTY_FUNCTION__` (e.g. `basic_abs2rel`). `fs8.traits` also holds the
`detail::trim/type_token/extract_type/unqualified/short_name` string helpers.

### pimpl idiom — `utils/pimpl.ixx`, module `fs8.pimpl`

Two variants; both store a `nullable_indirect<Derived::impl>` named `pimpl` and
provide a protected `init_impl(args...)`:

| Base                    | Copyable at runtime? | Use for                                  |
|-------------------------|----------------------|------------------------------------------|
| `pimpl_idiom<Derived>`  | no (consteval only)  | pipeline mods (inherits `consteval_copyable`) |
| `plain_pimpl_idiom<T>`  | yes (deep-clones)    | runners/services (`keyboard_runner`, `bash_runner`, `systemd_service`, ...) |

Canonical pattern:

```cpp
// foo.ixx
export module fs8.mods:foo;
import fs8.pimpl;

export namespace fs8 {
    constexpr struct [[nodiscard]] basic_foo : pimpl_idiom<basic_foo> {
        using pimpl_idiom::pimpl_idiom;
        context_action operator()(special_event const& tag) noexcept;
    } foo;
}

// foo.cxx
module fs8.mods;
struct fs8::pimpl_idiom<basic_foo>::impl { /* real members */ };
```

`impl` is only forward-declared in the header; define it as a specialization in
the `.cxx`. Allocate lazily in `start` / on first use:
`if (pimpl.get() == nullptr) init_impl();` (see `mods/io_manager.cxx:114`).
Handlers are `std::function_ref` bound by reference — they must outlive the
pipeline.

### `nullable_indirect<T>` — `utils/nullable_indirect.ixx`, module `fs8.nullable_indirect`

Nullable, allocator-aware owning pointer with value semantics: copy deep-clones
through a type-erased `clone_fn`. A runtime copy of a consteval-only impl
`std::abort()`s; the consteval branch constructs normally. Construct with the
`static make(...)` / `make_allocated(...)` factories. Observers: `get()`,
`operator*/->`, `operator bool`, `reset()`, `swap()`. This is the storage
behind `pimpl_idiom`.

### Logging — `main/log.ixx`, module `fs8.log`

A single non-throwing global consteval object `fs8::log` that prints to `stderr`
via `std::println` (terminates on formatting failure). Do **not** import
`fs8.log` from `event.ixx` (circular). Usage:

```cpp
import fs8.log;
log("Restarting pipeline...");
log("io_manager: poll failed: {}", std::strerror(errno));
log[event];          // prefix a message with an event
log(event);          // type_name(), code_name(), value()
```

### Events — `main/event.ixx`, module `fs8.event`

- `event_type` wraps a native `input_event` plus a `source_id`; it carries
  `type/code/value/time`, `is(...)`/`is_of(...)`, `micro_time()`, `hash()`.
- `special_event` is the lifecycle tag type (`start`, `no_init`, `load_event`,
  `next_event`, `toggle_on`, `toggle_off`, `idle`); its `type` is
  `special_event_type` (`EV_MAX + 1`), and `hashed()`/`operator+`/`==` make it
  usable in `switch`.
- `user_event`, `event_code`, `key_event` are plain POD-ish helpers;
  `key_code`/`key_codes` build `EV_KEY` codes.
- `source_id` (a `uint32_t`: high 16 = mod id, low 16 = source index) encodes
  event origin: `make_source_id(mod, idx)`, `sid(mod[, idx])`, `mod_id_of<T>()`
  (reads `T::mod_id` or hashes `__PRETTY_FUNCTION__`), plus `mod_id()` /
  `source_index()` unpackers. `source_id_none == 0` means unset.

### Context — `main/context.ixx`, module `fs8.context`

- `basic_context<Mods...>` is the mod tuple + current event; `fs8::context` is
  the empty one pipelines are built from.
- `context_action { next, drop_event, recovery, exit }`.
- `dynamic_context` is a `thread_binding<any_dynamic_context>` giving mods
  type-erased access to the running context; bind with
  `dynamic_scope scope{dynamic_context, *this}` (see `start_mods` /
  `run_loop`).
- `type_id<T>` (address-stable token) plus `mods<T>()` / `rmods<T>()` find mods
  by type, `recursive` descending into routers/sub-pipelines.
- Concepts: `Context`, `Modifier`, `OutputModifier`, `ContextWith`, `has_mod`,
  `invokable_mod`, `PipelineTag`.

### CLI — `main/cli.ixx`, module `fs8.cli` (re-exported by `fs8.utils`)

Configure with method chaining, parse via `operator()(argc, argv)`:

```cpp
import fs8.cli;
static constexpr auto args = fs8::arguments["default_device"]
    .positional("device")
    .add_flag({.name = "--grab", .alias = "-g", .help = "grab the device"});
auto parsed = args(argc, argv);
parsed.exit_if_needed();                  // handles -h/--help, -v/--version
if (parsed.has_flag("--grab")) { ... }
auto val = parsed.flag_value("--timeout"); // std::optional<std::string_view>
```

`parsed_args` is a range over positionals, so it can be piped through query tags
(`parsed | grab | required`). `basic_arguments<N>` holds default positionals.

### Small utilities (`utils/`)

| Module (`import`)          | What's in it |
|----------------------------|--------------|
| `fs8.traits` (`utils/traits.ixx`) | `consteval_copyable`, `pretty_type_name`, string helpers |
| `fs8.strings` (`utils/strings.ixx`) | `operator+(string_view) -> string`, `is_surrogate`, `is_empty`, `iequals` |
| `fs8.hash` (`utils/hash.ixx`) | `ci_hash` (constexpr case-insensitive FNV-1a), FNV constants, runtime `fnv1a_init`/`fnv1a_hash` |
| `fs8.easings` (`utils/easings.ixx`) | `linear`, `easeIn/Out{Quad,Cubic,Quart,Quint,Sine,Expo}` |
| `dynamic_scoping` (`utils/dynamic_scoping.ixx`) | `global_binding`, `thread_binding`, `dynamic_scope`, `binder_instance`/`dynamically_scoped`/`polymorphic_scoped` |
| `fs8.utils` (`main/utils.ixx`) | `noop`, `constexpr_constructible`, `construct_it_from`, `transform_to`, `into` |
| `fs8.nullable_indirect` | see above |

### Anatomy of a new mod (checklist)

1. Write `mods/<name>.ixx` as `export module fs8.mods:<name>;`, put
   `basic_<name>` in `export namespace fs8` inside `consteval_copyable` (via
   `pimpl_idiom` if it holds state), and expose a constexpr instance `<name>`.
2. Write `mods/<name>.cxx` as `module fs8.mods;` with the `impl` definition and
   the method bodies.
3. Register **both** files in the root `CMakeLists.txt` (`target_sources`
   PRIVATE for `.cxx`, `FILE_SET foresight` for `.ixx`).
4. Add `export import :<name>;` to `mods/mods.ixx`.
5. `static_assert` the `Modifier`/`OutputModifier` concept where relevant and
   any inter-mod dependency (e.g. "We need keys_state to be in the pipeline.").
6. Handle lifecycle tags by inspecting `tag.code` in
   `operator()(special_event const&)`; return `next`/`drop_event` for ordinary
   events. All invocations must be `noexcept` (custom `log` etc. included).

## C++26 modules (the big gotcha)

All library code is C++26 modules: interfaces are `.ixx`, paired with `.cxx`
implementation units. Module names use the `fs8.*` namespace (`fs8.mods`,
`fs8.devices.*`, `fs8.context`, ...).

Umbrella / re-export modules:
- `fs8` (`main/main.ixx`) — exports `fs8.keyboard`, `fs8.translate`, `fs8.log`.
- `fs8.mods` (`mods/mods.ixx`) — re-exports the context/event/capabilities
  plumbing, the state/utility mods, and **every pipeline mod**.
- `fs8.utils` (`main/utils.ixx`) — re-exports `fs8.cli` alongside small helpers.
- Apps `import fs8.mods` (and often `fs8.cli`, `fs8.log`, `fs8.devices.*`).

Partitions: `mods/context_vars.ixx` is a **module partition** of `fs8.context`
(`fs8.context:vars`) even though it lives in `mods/`. The pipeline mods and
`devices/uinput.ixx` are all partitions of `fs8.mods` (`fs8.mods:<name>`).
`io/liburing.ixx` is not a module — it only includes liburing's headers.

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
  `.clangd` points at `build-debug-gcc/compile_commands.json`. Use the
  `debug-clang` preset for editor/analysis support.
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