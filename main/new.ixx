// Created by moisrex on 8/17/26.

module;
#include <span>
#include <string_view>
export module fs8.scaffold;

namespace fs8 {

    /// A single file of a template. `path` is relative to the app directory
    /// and may contain `{{name}}` placeholders; so does `content`.
    export struct template_file {
        std::string_view path;    // e.g. "{{name}}.cpp"
        std::string_view content; // file contents
    };

    /// A named app template: a set of files scaffolded by `foresight new`.
    export struct app_template {
        std::string_view               name;        // e.g. "basic"
        std::string_view               description; // one-liner for --list-templates
        std::span<template_file const> files;
    };

    /// All the templates embedded into this binary.
    export std::span<app_template const> available_templates() noexcept;

    /// Whether `name` is a known template.
    export bool is_valid_template(std::string_view name) noexcept;

    /// The description of a known template, empty otherwise.
    export std::string_view template_description(std::string_view name) noexcept;

    /// Scaffold the given template into `target` (a path, relative or
    /// absolute, whose filename becomes the app name). Throws
    /// `std::invalid_argument` for unknown templates or bad names, and
    /// `std::filesystem::filesystem_error` when it can't be created (e.g. the
    /// directory already exists).
    export void create_app(std::string_view target, std::string_view template_name);

} // namespace fs8
