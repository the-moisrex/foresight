module;
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <print>
#include <stdexcept>
#include <string>
module foresight.main.systemd;

using foresight::systemd_service;

void systemd_service::execStart(std::span<std::string_view const> const args) {
    args_ = args;
}

void systemd_service::description(std::string_view const desc) {
    description_ = desc;
}

namespace {
    std::string escape_command(std::span<std::string_view const> const cmd) {
        std::string result;
        for (auto const& arg : cmd) {
            if (!result.empty()) {
                result += ' ';
            }

            std::error_code ec;
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

    if (args_.empty()) {
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
    std::filesystem::path exec_path(args_[0]);
    std::string           service_name = exec_path.filename().string() + ".service";
    std::filesystem::path service_file = user_systemd_dir / service_name;
    std::filesystem::path const exec_file{args_[0]};
    if (!exists(exec_file)) {
        throw std::runtime_error("Executable not found");
    }
    auto const            cmd_str      = escape_command(args_);

    std::println("Name: {}\nService File: {}\nExec: {}", service_name, service_file.string(), cmd_str);

    // Write service unit file
    std::ofstream out(service_file);
    if (!out) {
        throw std::runtime_error("Failed to create service file");
    }

    out
      << "[Unit]\n"
      << "Description="
      << description_
      << "\n\n"
      << "[Service]\n"
      << "ExecStart="
      << cmd_str
      << "\n"
      << "Restart=always\n"
      << "RestartSec=5\n"
      // << "Environment=DISPLAY=:0\n"
      // << "Environment=XAUTHORITY=%h/.Xauthority\n\n"
      << "[Install]\n"
      << "WantedBy=default.target\n";
}

void systemd_service::enable(bool const start_now) const {
    if (args_.empty()) {
        throw std::runtime_error("No executable specified");
    }

    // Generate service name from executable
    std::filesystem::path exec_path(args_[0]);
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
