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

        // NOLINTBEGIN(*-avoid-c-arrays)

// GCC 16 recognises #embed from C23 but warns under -Wpedantic because it
// is not yet a standard C++26 feature.  Silence the warning until GCC catches up.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wc23-extensions"

        // Embedded template files.  Paths resolve relative to this source file.

        // basic
        constexpr char basic_cmake_data[] = {
#embed "../templates/basic/CMakeLists.txt"
        };
        constexpr char basic_app_data[] = {
#embed "../templates/basic/{{name}}.cpp"
        };
        constexpr char basic_readme_data[] = {
#embed "../templates/basic/README.md"
        };
        constexpr char basic_clang_format_data[] = {
#embed "../templates/basic/.clang-format"
        };
        constexpr char basic_presets_data[] = {
#embed "../templates/basic/CMakePresets.json"
        };

        // auto-typer
        constexpr char auto_typer_cmake_data[] = {
#embed "../templates/auto-typer/CMakeLists.txt"
        };
        constexpr char auto_typer_app_data[] = {
#embed "../templates/auto-typer/{{name}}.cpp"
        };
        constexpr char auto_typer_readme_data[] = {
#embed "../templates/auto-typer/README.md"
        };
        constexpr char auto_typer_clang_format_data[] = {
#embed "../templates/auto-typer/.clang-format"
        };
        constexpr char auto_typer_presets_data[] = {
#embed "../templates/auto-typer/CMakePresets.json"
        };

        // x2y
        constexpr char x2y_cmake_data[] = {
#embed "../templates/x2y/CMakeLists.txt"
        };
        constexpr char x2y_app_data[] = {
#embed "../templates/x2y/{{name}}.c"
        };
        constexpr char x2y_readme_data[] = {
#embed "../templates/x2y/README.md"
        };
        constexpr char x2y_clang_format_data[] = {
#embed "../templates/x2y/.clang-format"
        };
        constexpr char x2y_presets_data[] = {
#embed "../templates/x2y/CMakePresets.json"
        };

        // keyboard-replacer
        constexpr char keyboard_replacer_cmake_data[] = {
#embed "../templates/keyboard-replacer/CMakeLists.txt"
        };
        constexpr char keyboard_replacer_app_data[] = {
#embed "../templates/keyboard-replacer/{{name}}.cpp"
        };
        constexpr char keyboard_replacer_readme_data[] = {
#embed "../templates/keyboard-replacer/README.md"
        };
        constexpr char keyboard_replacer_clang_format_data[] = {
#embed "../templates/keyboard-replacer/.clang-format"
        };
        constexpr char keyboard_replacer_presets_data[] = {
#embed "../templates/keyboard-replacer/CMakePresets.json"
        };

        // tablet
        constexpr char tablet_cmake_data[] = {
#embed "../templates/tablet/CMakeLists.txt"
        };
        constexpr char tablet_app_data[] = {
#embed "../templates/tablet/{{name}}.cpp"
        };
        constexpr char tablet_readme_data[] = {
#embed "../templates/tablet/README.md"
        };
        constexpr char tablet_clang_format_data[] = {
#embed "../templates/tablet/.clang-format"
        };
        constexpr char tablet_presets_data[] = {
#embed "../templates/tablet/CMakePresets.json"
        };

        // mouse
        constexpr char mouse_cmake_data[] = {
#embed "../templates/mouse/CMakeLists.txt"
        };
        constexpr char mouse_app_data[] = {
#embed "../templates/mouse/{{name}}.cpp"
        };
        constexpr char mouse_readme_data[] = {
#embed "../templates/mouse/README.md"
        };
        constexpr char mouse_clang_format_data[] = {
#embed "../templates/mouse/.clang-format"
        };
        constexpr char mouse_presets_data[] = {
#embed "../templates/mouse/CMakePresets.json"
        };

#pragma GCC diagnostic pop

        // NOLINTEND(*-avoid-c-arrays)

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
          {"CMakeLists.txt", {basic_cmake_data, sizeof(basic_cmake_data)}},
          {"{{name}}.cpp", {basic_app_data, sizeof(basic_app_data)}},
          {"README.md", {basic_readme_data, sizeof(basic_readme_data)}},
          {".clang-format", {basic_clang_format_data, sizeof(basic_clang_format_data)}},
          {"CMakePresets.json", {basic_presets_data, sizeof(basic_presets_data)}},
        };

        constexpr template_file auto_typer_files[] = {
          {"CMakeLists.txt", {auto_typer_cmake_data, sizeof(auto_typer_cmake_data)}},
          {"{{name}}.cpp", {auto_typer_app_data, sizeof(auto_typer_app_data)}},
          {"README.md", {auto_typer_readme_data, sizeof(auto_typer_readme_data)}},
          {".clang-format", {auto_typer_clang_format_data, sizeof(auto_typer_clang_format_data)}},
          {"CMakePresets.json", {auto_typer_presets_data, sizeof(auto_typer_presets_data)}},
        };

        constexpr template_file x2y_files[] = {
          {"CMakeLists.txt", {x2y_cmake_data, sizeof(x2y_cmake_data)}},
          {"{{name}}.c", {x2y_app_data, sizeof(x2y_app_data)}},
          {"README.md", {x2y_readme_data, sizeof(x2y_readme_data)}},
          {".clang-format", {x2y_clang_format_data, sizeof(x2y_clang_format_data)}},
          {"CMakePresets.json", {x2y_presets_data, sizeof(x2y_presets_data)}},
        };

        constexpr template_file keyboard_replacer_files[] = {
          {"CMakeLists.txt", {keyboard_replacer_cmake_data, sizeof(keyboard_replacer_cmake_data)}},
          {"{{name}}.cpp", {keyboard_replacer_app_data, sizeof(keyboard_replacer_app_data)}},
          {"README.md", {keyboard_replacer_readme_data, sizeof(keyboard_replacer_readme_data)}},
          {".clang-format", {keyboard_replacer_clang_format_data, sizeof(keyboard_replacer_clang_format_data)}},
          {"CMakePresets.json", {keyboard_replacer_presets_data, sizeof(keyboard_replacer_presets_data)}},
        };

        constexpr template_file tablet_files[] = {
          {"CMakeLists.txt", {tablet_cmake_data, sizeof(tablet_cmake_data)}},
          {"{{name}}.cpp", {tablet_app_data, sizeof(tablet_app_data)}},
          {"README.md", {tablet_readme_data, sizeof(tablet_readme_data)}},
          {".clang-format", {tablet_clang_format_data, sizeof(tablet_clang_format_data)}},
          {"CMakePresets.json", {tablet_presets_data, sizeof(tablet_presets_data)}},
        };

        constexpr template_file mouse_files[] = {
          {"CMakeLists.txt", {mouse_cmake_data, sizeof(mouse_cmake_data)}},
          {"{{name}}.cpp", {mouse_app_data, sizeof(mouse_app_data)}},
          {"README.md", {mouse_readme_data, sizeof(mouse_readme_data)}},
          {".clang-format", {mouse_clang_format_data, sizeof(mouse_clang_format_data)}},
          {"CMakePresets.json", {mouse_presets_data, sizeof(mouse_presets_data)}},
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
          app_template{
                       .name        = "keyboard-replacer",
                       .description = "Swap keyboard keys (e.g. CapsLock to Escape).",
                       .files       = keyboard_replacer_files,
                       },
          app_template{
                       .name        = "tablet",
                       .description = "Convert drawing tablet input to relative mouse movements.",
                       .files       = tablet_files,
                       },
          app_template{
                       .name        = "mouse",
                       .description = "Amplify mouse movements.",
                       .files       = mouse_files,
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
