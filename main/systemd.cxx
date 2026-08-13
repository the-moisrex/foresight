module;
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
module fs8.systemd;
import fs8.pimpl;

using fs8::systemd_service;

template <>
struct fs8::plain_pimpl_idiom<systemd_service>::impl {
    std::vector<std::string> args; // owning copy; `execStart` used to store a dangling span
    std::string              description;
};

systemd_service::systemd_service() noexcept = default;

systemd_service::~systemd_service() noexcept = default;

void systemd_service::execStart(std::span<std::string_view const> const args) {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    pimpl->args.assign(args.begin(), args.end());
}

void systemd_service::description(std::string_view const desc) {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    pimpl->description = desc;
}

namespace {
    std::string escape_command(std::span<std::string_view const> const cmd) {
        std::string result;
        for (auto const& arg : cmd) {
            if (!result.empty()) {
                result += ' ';
            }

            std::error_code             ec;
            std::filesystem::path const arg_path{arg};
            if (exists(arg_path)) {
                result += absolute(arg_path, ec).string();
                continue;
            }

            bool const needs_quotes = arg.find_first_of(" \t\"'\\") != std::string_view::npos;
            if (needs_quotes) {
                result += '"';
            }

            for (char const cur : arg) {
                switch (cur) {
                    case '"': result += "\\\""; break;
                    case '\\': result += "\\\\"; break;
                    default: result += cur; break;
                }
            }

            if (needs_quotes) {
                result += '"';
            }
        }
        return result;
    }
} // namespace

bool systemd_service::check_systemd_support() {
    return std::filesystem::exists("/run/systemd/system");
}

void systemd_service::install() const {
    if (!check_systemd_support()) {
        throw std::runtime_error("Systemd not supported on this system");
    }

    if (pimpl.get() == nullptr || pimpl->args.empty()) {
        throw std::runtime_error("No executable specified");
    }

    // Get home directory
    char const* home = std::getenv("HOME");
    if (!home) {
        throw std::runtime_error("HOME environment variable not set");
    }

    // Create user systemd directory
    std::filesystem::path user_systemd_dir = std::filesystem::path(home) / ".config/systemd/user";
    std::filesystem::create_directories(user_systemd_dir);

    // Generate service name from executable
    std::filesystem::path       exec_path(pimpl->args[0]);
    std::string                 service_name = exec_path.filename().string() + ".service";
    std::filesystem::path       service_file = user_systemd_dir / service_name;
    std::filesystem::path const exec_file{pimpl->args[0]};
    if (!exists(exec_file)) {
        throw std::runtime_error("Executable not found");
    }
    std::vector<std::string_view> args_view;
    args_view.reserve(pimpl->args.size());
    for (auto const& arg : pimpl->args) {
        args_view.emplace_back(arg);
    }
    auto const cmd_str = escape_command(args_view);

    std::println("Name: {}\nService File: {}\nExec: {}", service_name, service_file.string(), cmd_str);

    // Write service unit file
    std::ofstream out(service_file);
    if (!out) {
        throw std::runtime_error("Failed to create service file");
    }

    out
      << "[Unit]\n"
      << "Description="
      << pimpl->description
      << "\n\n"
      << "[Service]\n"
      // << "ExecStart=/bin/bash -c '"
      // << cmd_str
      // << " & pid=$!; inotifywait -e modify "
      // << absolute(exec_file).string()
      // << " && kill $pid && wait $pid'"
      << "ExecStart=/bin/bash -c '"
      << cmd_str
      << "'"
      << "\n"
      << "Restart=always\n"
      << "RestartSec=1\n"
      // << "Environment=DISPLAY=:0\n"
      // << "Environment=XAUTHORITY=%h/.Xauthority\n"
      << "\n[Install]\n"
      << "WantedBy=default.target\n";
}

void systemd_service::enable(bool const start_now) const {
    if (pimpl.get() == nullptr || pimpl->args.empty()) {
        throw std::runtime_error("No executable specified");
    }

    // Generate service name from executable
    std::filesystem::path exec_path(pimpl->args[0]);
    std::string           service_name = exec_path.filename().string() + ".service";

    // Enable service
    std::string cmd = "systemctl --user enable " + service_name;
    if (std::system(cmd.c_str()) != 0) {
        throw std::runtime_error("Failed to enable service");
    }

    // Start service if requested
    if (start_now) {
        cmd = "systemctl --user start " + service_name;
        if (std::system(cmd.c_str()) != 0) {
            throw std::runtime_error("Failed to start service");
        }
    }
}
