# Getting Started

Foresight currently **only supports Linux**. It requires a compiler with C++26 modules support
(GCC 14+ or Clang 16+ is a good starting point) and a few system libraries.

## Dependencies

- A C++26 compiler that supports C++ modules (`.ixx`), e.g. GCC 14+ or Clang 16+.
- [CMake](https://cmake.org/) 3.23+ (a `CMakePresets.json` is provided).
- A build generator such as [Ninja](https://ninja-build.org/).
- Linux development packages for:
  - `libevdev`
  - `libudev`
  - `libxkbcommon`

On Arch-based systems you can install the headers with:

```bash
sudo pacman -S libevdev libxkbcommon systemd
```

## Building

The repository ships CMake presets for both GCC and Clang, in Debug and Release configurations.

```bash
# Configure
cmake --preset debug-gcc

# Build everything (library, foresight binary, and the example apps)
cmake --build --preset debug-gcc
```

The available presets are `debug-gcc`, `release-gcc`, `debug-clang`, and `release-clang`.

The build produces:

- `libforesight` — the library
- `foresight` — the CLI binary (see [CLI](cli.md))
- The example apps from [`apps/`](apps.md) (e.g. `pen2mice`, `x2y`)

## Running tests

Tests are built in Debug configurations and run through CTest:

```bash
cmake --test --preset debug-gcc
```

## Building the docs

The API reference is generated with Doxygen and can be built through CMake:

```bash
cmake --build build --target docs
# or, directly:
FS8_DOC_OUTPUT=build/docs/doxygen doxygen Doxyfile
```

The MkDocs guides site is built with:

```bash
pip install -r requirements-docs.txt
mkdocs build --strict   # writes to site/
mkdocs serve            # local preview at http://127.0.0.1:8000/
```

## Quick sanity check

Once built, list the available CLI actions:

```bash
./foresight --help
```