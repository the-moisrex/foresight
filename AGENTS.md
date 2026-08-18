# AGENTS.md

Foresight is a Linux-only input manager: a C++26 library (`libforesight`) plus
the `foresight` CLI, built with CMake + Ninja. Needs GCC 14+ or Clang 16+.

## Build / test

Presets: `gcc-debug`, `gcc-release`, `clang-debug`, `clang-release`; build dirs
are `cmake-build-{debug,release}[-clang]`.

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

## C++26 modules (the big gotcha)

All library code is C++26 modules: interfaces are `.ixx`, paired with `.cxx`
implementation units. Module names use the `fs8.*` namespace (`fs8.mods.*`,
`fs8.devices.*`, `fs8.context`, ...). Umbrella modules: `fs8`
(`main/main.ixx`) and `fs8.mods` (`mods/mods.ixx`). Apps import `fs8.mods`.

**Adding a module requires registering the files in the root `CMakeLists.txt`
in two places**, or the build silently omits them:
1. `target_sources(... PRIVATE ...)` for the `.cxx`,
2. the `PUBLIC FILE_SET foresight TYPE CXX_MODULES FILES` list for the `.ixx`.

A new pipeline mod must also be `export import`-ed from `mods/mods.ixx`. Example
apps live in `apps/` (each links `foresight::foresight`; register new ones via
`add_subdirectory` in `apps/CMakeLists.txt`). `foresight new` scaffolds
standalone apps that build with `-DFORESIGHT_SOURCE_DIR=<foresight checkout>`.

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
  `mkdocs serve`).
- API reference: Doxygen generated from the `///` comments in the `.ixx` files.
  Build with `cmake --build <build> --target docs`, or run
  `FS8_DOC_OUTPUT=<dir> doxygen Doxyfile` from the repo root.