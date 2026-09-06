#include "cli_args.hxx"

#include <format>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

import fs8;
import fs8.scaffold;

int create_new_app(std::span<char const* const> const args) {
    using enum options::action_type;

    bool             list_templates = false;
    bool             help_requested = false;
    std::string_view tpl;
    std::string_view name;

    for (auto const arg : args | std::views::drop(2)) { // remove "foresight new"
        std::string_view const cur{arg};
        if (cur == "--list-templates") {
            list_templates = true;
            continue;
        }
        if (cur == "--help" || cur == "-h") {
            help_requested = true;
            continue;
        }
        if (fs8::is_valid_template(cur)) {
            if (!tpl.empty()) {
                throw std::invalid_argument(
                  std::format("'{}' and '{}' are both templates; pass one template and one app name.", tpl, cur));
            }
            tpl = cur;
            continue;
        }
        if (!name.empty()) {
            throw std::invalid_argument(std::format("Unknown argument '{}'.", cur));
        }
        name = cur;
    }

    if (list_templates) {
        std::println("Available templates:");
        for (auto const& templ : fs8::available_templates()) {
            std::println("  {:<12} {}", templ.name, templ.description);
        }
        return EXIT_SUCCESS;
    }
    if (help_requested) {
        print_new_help();
        return EXIT_SUCCESS;
    }
    if (name.empty()) {
        if (tpl.empty()) {
            throw std::invalid_argument("Please provide a name for the app.");
        }
        name = tpl; // e.g. `foresight new x2y` -> an app named after the template
    }
    if (tpl.empty()) {
        tpl = "basic";
    }

    fs8::create_app(name, tpl);

    std::println();
    std::println("Next steps:");
    std::println("  cd {}", name);
    std::println("  cmake --preset release");
    std::println("  cmake --build --preset release");
    return EXIT_SUCCESS;
}
