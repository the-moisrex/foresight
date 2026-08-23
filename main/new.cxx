// Created by moisrex on 8/17/26.

module;
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
module fs8.scaffold;

namespace fs = std::filesystem;

namespace fs8 {

    namespace {

        /// Replace every `{{name}}` placeholder in `text` with `name`.
        [[nodiscard]] std::string substitute(std::string_view const text, std::string_view const name) {
            constexpr std::string_view placeholder = "{{name}}";
            std::string                result;
            result.reserve(text.size());
            std::size_t pos = 0;
            while (true) {
                auto const found = text.find(placeholder, pos);
                if (found == std::string_view::npos) {
                    result.append(text.substr(pos));
                    break;
                }
                result.append(text.substr(pos, found - pos));
                result.append(name);
                pos = found + placeholder.size();
            }
            return result;
        }

        /// Whether `name` is a safe app name (usable as a file/folder/target name).
        [[nodiscard]] bool is_valid_app_name(std::string_view const name) noexcept {
            if (name.empty()) {
                return false;
            }
            for (char const cur : name) {
                bool const ok =
                  (cur >= 'a' && cur <= 'z')
                  || (cur >= 'A' && cur <= 'Z')
                  || (cur >= '0' && cur <= '9')
                  || cur
                  == '-'
                  || cur
                  == '_'
                  || cur
                  == '.';
                if (!ok) {
                    return false;
                }
            }
            return true;
        }

        // clang-format off
        constexpr template_file basic_files[] = {
          {"CMakeLists.txt", R"TEMPLATE(
cmake_minimum_required(VERSION 3.23...4.1.2)
project({{name}} LANGUAGES CXX C)

add_executable({{name}})
target_sources({{name}} PRIVATE {{name}}.cpp)
set_target_properties({{name}} PROPERTIES OUTPUT_NAME {{name}})
set_target_properties({{name}} PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties({{name}} PROPERTIES COMPILER_LANGUAGE CXX)
set_target_properties({{name}} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Builds against the Foresight library. Either drop this folder into Foresight's
# apps/ directory, or point FORESIGHT_SOURCE_DIR at a Foresight checkout:
#   cmake -G Ninja -DFORESIGHT_SOURCE_DIR=/path/to/foresight -B build
#   cmake --build build
if (NOT TARGET foresight::foresight)
    if (NOT DEFINED FORESIGHT_SOURCE_DIR)
        message(FATAL_ERROR "foresight::foresight not found. Add this folder to "
                            "Foresight's apps/ directory, or set "
                            "-DFORESIGHT_SOURCE_DIR=<foresight checkout>.")
    endif ()
    add_subdirectory(${FORESIGHT_SOURCE_DIR} foresight-build)
endif ()

target_link_libraries({{name}} PRIVATE foresight::foresight)
target_compile_features({{name}} PUBLIC cxx_std_26)
set_target_properties({{name}} PROPERTIES
        CXX_STANDARD 26
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON)
)TEMPLATE"},
          {"{{name}}.cpp", R"TEMPLATE(
#include <stdexcept>
#include <linux/input-event-codes.h>

import fs8.mods;
import fs8.log;
import fs8.cli;
import fs8.devices.queries;

static constexpr auto args =
  fs8::arguments["USB Keyboard"]
    .positional("keyboard_device")
    .help(R"TEXT(
Usage: {{name}} [keyboard_device]

A Foresight app created from the 'basic' template: it grabs the keyboard and
replaces 'x' with 'y' as you type.

Arguments:
    -h | --help           Print help.

Positionals:
    keyboard_device       The USB keyboard device query.
)TEXT");

int main(int const argc, char const* const* argv) try {
    using namespace fs8; // NOLINT(*-using-namespace)

    auto const parsed = args(argc, argv);
    parsed.exit_if_needed();

    static constinit auto pipeline =
      context
      | io_manager
      | intercept[keyboard | required]
      | input_manager
      | keys_status
      | on[pressed[KEY_X], replace[KEY_X, KEY_Y]]
      | update_mod[keys_status]
      | uinput;

    pipeline.mod(intercept).add(parsed | required);
    pipeline();

    return 0;
} catch (std::runtime_error const& err) {
    fs8::log("Runtime Error: {}", err.what());
    throw;
} catch (...) {
    fs8::log("Unknown Error.");
    throw;
}
)TEMPLATE"},
          {"README.md", R"TEMPLATE(
# {{name}}

A Foresight app created from the `basic` template: it grabs your keyboard and
replaces every `x` with `y` as you type.

## Build

This app links against the Foresight library (`foresight::foresight`). Either:

- Drop this folder into Foresight's `apps/` directory and add
  `add_subdirectory({{name}})` to `apps/CMakeLists.txt`, then build Foresight.
- Or build standalone against a Foresight checkout:

  ```bash
  cmake -G Ninja -DFORESIGHT_SOURCE_DIR=/path/to/foresight -B build
  cmake --build build
  ```

## Usage

```bash
./{{name}} [keyboard_device]
./{{name}} -h | --help
```

Positionals:

- `keyboard_device`: The USB keyboard device query (defaults to `USB Keyboard`).
- `-h | --help`: Print help.
)TEMPLATE"},
        };

        constexpr template_file x2y_files[] = {
          {"CMakeLists.txt", R"TEMPLATE(
cmake_minimum_required(VERSION 3.23)
project({{name}} LANGUAGES C CXX)

set(name {{name}})
add_executable(${name})
target_sources(${name} PRIVATE {{name}}.c)
set_target_properties(${name} PROPERTIES OUTPUT_NAME ${name})
set_target_properties(${name} PROPERTIES LINKER_LANGUAGE C)
set_target_properties(${name} PROPERTIES COMPILER_LANGUAGE C)
set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
)TEMPLATE"},
          {"{{name}}.c", R"TEMPLATE(
#include <stdio.h>
#include <stdlib.h>
#include <linux/input.h>

int main(void) {
    setbuf(stdin, NULL);   // disable stdin buffer
    setbuf(stdout, NULL);  // disable stdout buffer

    struct input_event event;

    // read from the input
    while (fread(&event, sizeof(event), 1, stdin) == 1) {

        // modify the input however you like
        // here, we change "x" to "y"
        if (event.type == EV_KEY && event.code == KEY_X)
            event.code = KEY_Y;

        // write it to stdout
        fwrite(&event, sizeof(event), 1, stdout);
    }
}
)TEMPLATE"},
          {"README.md", R"TEMPLATE(
# {{name}}

A Foresight app created from the `x2y` template: a tiny C filter that reads
`input_event`s from stdin, modifies them, and writes them to stdout.

## Build

```bash
cmake -B build
cmake --build build
```

## Usage

Put it in the middle of a Foresight pipeline:

```bash
foresight intercept $keyboard | ./{{name}} | foresight redirect $keyboard
```

The filter here changes every `x` into `y`; edit `{{name}}.c` to do anything
you like.
)TEMPLATE"},
        };

        constexpr template_file auto_typer_files[] = {
          {"CMakeLists.txt", R"TEMPLATE(
cmake_minimum_required(VERSION 3.23...4.1.2)
project({{name}} LANGUAGES CXX C)

add_executable({{name}})
target_sources({{name}} PRIVATE {{name}}.cpp)
set_target_properties({{name}} PROPERTIES OUTPUT_NAME {{name}})
set_target_properties({{name}} PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties({{name}} PROPERTIES COMPILER_LANGUAGE CXX)
set_target_properties({{name}} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Builds against the Foresight library. Either drop this folder into Foresight's
# apps/ directory, or point FORESIGHT_SOURCE_DIR at a Foresight checkout:
#   cmake -G Ninja -DFORESIGHT_SOURCE_DIR=/path/to/foresight -B build
#   cmake --build build
if (NOT TARGET foresight::foresight)
    if (NOT DEFINED FORESIGHT_SOURCE_DIR)
        message(FATAL_ERROR "foresight::foresight not found. Add this folder to "
                            "Foresight's apps/ directory, or set "
                            "-DFORESIGHT_SOURCE_DIR=<foresight checkout>.")
    endif ()
    add_subdirectory(${FORESIGHT_SOURCE_DIR} foresight-build)
endif ()

target_link_libraries({{name}} PRIVATE foresight::foresight)
target_compile_features({{name}} PUBLIC cxx_std_26)
set_target_properties({{name}} PROPERTIES
        CXX_STANDARD 26
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON)
)TEMPLATE"},
          {"{{name}}.cpp", R"TEMPLATE(
#include <stdexcept>

import fs8.mods;
import fs8.log;
import fs8.cli;
import fs8.devices.queries;

static constexpr auto args =
  fs8::arguments["USB Keyboard"]
    .positional("keyboard_device")
    .help(R"TEXT(
Usage: {{name}} [keyboard_device]

A Foresight app created from the 'auto-typer' template: it watches your typing
and completes the pattern "@test" into the string "nice".

Arguments:
    -h | --help           Print help.

Positionals:
    keyboard_device       The USB keyboard device query.
)TEXT");

int main(int const argc, char const* const* argv) try {
    using namespace fs8; // NOLINT(*-using-namespace)

    auto const parsed = args(argc, argv);
    parsed.exit_if_needed();

    static constinit auto pipeline =
      context
      | io_manager
      | intercept[keyboard | required | matches_limit(10)]
      | input_manager
      | search_engine
      | on[typed["@test"], type_string("nice")]
      | ignore_adjacent_syns
      | uinput;

    pipeline.mod(intercept).add(parsed | required);
    pipeline();

    return 0;
} catch (std::runtime_error const& err) {
    fs8::log("Runtime Error: {}", err.what());
    throw;
} catch (...) {
    fs8::log("Unknown Error.");
    throw;
}
)TEMPLATE"},
          {"README.md", R"TEMPLATE(
# {{name}}

A Foresight app created from the `auto-typer` template: it watches a keyboard
for typed patterns and auto-completes them into longer strings.

## Build

This app links against the Foresight library (`foresight::foresight`). Either:

- Drop this folder into Foresight's `apps/` directory and add
  `add_subdirectory({{name}})` to `apps/CMakeLists.txt`, then build Foresight.
- Or build standalone against a Foresight checkout:

  ```bash
  cmake -G Ninja -DFORESIGHT_SOURCE_DIR=/path/to/foresight -B build
  cmake --build build
  ```

## Usage

```bash
./{{name}} [keyboard_device]
./{{name}} -h | --help
```

The template completes the pattern `@test` into `nice`; edit `{{name}}.cpp` to
change the pattern and the completion.
)TEMPLATE"},
        };
        // clang-format on

        constexpr std::array app_templates = {
          app_template{
                       .name        = "basic",
                       .description = "Minimal C++ pipeline: replace a key as you type.",
                       .files       = basic_files,
                       },
          app_template{
                       .name        = "x2y",
                       .description = "Tiny raw-C stdin/stdout event filter.",
                       .files       = x2y_files,
                       },
          app_template{
                       .name        = "auto-typer",
                       .description = "C++ pipeline: complete a typed pattern into a string.",
                       .files       = auto_typer_files,
                       },
        };

    } // namespace

    std::span<app_template const> available_templates() noexcept {
        return app_templates;
    }

    bool is_valid_template(std::string_view const name) noexcept {
        for (app_template const& tpl : app_templates) {
            if (tpl.name == name) {
                return true;
            }
        }
        return false;
    }

    std::string_view template_description(std::string_view const name) noexcept {
        for (app_template const& tpl : app_templates) {
            if (tpl.name == name) {
                return tpl.description;
            }
        }
        return {};
    }

    void create_app(std::string_view const target, std::string_view const template_name) {
        app_template const* tpl = nullptr;
        for (app_template const& cur : app_templates) {
            if (cur.name == template_name) {
                tpl = &cur;
                break;
            }
        }
        if (tpl == nullptr) {
            throw std::invalid_argument(std::format("Unknown template '{}'.", template_name));
        }
        if (target.empty()) {
            throw std::invalid_argument("Please provide a name for the app.");
        }

        fs::path const    target_path{target};
        std::string const name = target_path.filename().string();
        if (!is_valid_app_name(name)) {
            throw std::invalid_argument(std::format("Invalid app name '{}'. Use letters, digits, '-', '_', or '.'.", name));
        }

        std::error_code ec;
        bool const      already_exists = fs::exists(target_path, ec);
        if (ec || already_exists) {
            throw fs::filesystem_error(std::format("'{}' already exists; pick a different name or remove it first.", target),
                                       target_path,
                                       ec);
        }

        fs::create_directories(target_path, ec);
        if (ec) {
            throw fs::filesystem_error("Failed to create the app directory.", target_path, ec);
        }

        std::println("Creating '{}' from the '{}' template...", name, template_name);

        for (template_file const& file : tpl->files) {
            std::string const rel  = substitute(file.path, name);
            std::string const data = substitute(file.content, name);
            auto const        out  = target_path / rel;
            fs::create_directories(out.parent_path(), ec);
            if (ec) {
                throw fs::filesystem_error("Failed to create a sub-directory.", out, ec);
            }
            std::ofstream stream{out};
            if (!stream) {
                throw fs::filesystem_error("Failed to open file for writing.", out, ec);
            }
            stream << data;
            std::println("  {}", (target_path / rel).lexically_normal().string());
        }
    }

} // namespace fs8
