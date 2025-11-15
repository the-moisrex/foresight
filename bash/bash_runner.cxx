// Created by moisrex on 10/27/25.

module;
#include <array>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <fstream>
#include <iterator>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
module foresight.bash;

using foresight::bash_runner;

namespace {
    constexpr std::size_t      buffer_size = 4096U;
    constexpr std::string_view marker      = "----FORESIGHT-BASH-RUNNER-MARKER----";

    std::string read_file_and_remove_shebang(std::string_view const file) {
        std::ifstream ifs(file.data(), std::ios::binary);
        if (!ifs) {
            throw std::runtime_error(std::format("Cannot open file: {}", file));
        }
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        if (content.starts_with("#!")) {
            if (auto const pos = content.find('\n'); pos != std::string::npos) {
                content = content.substr(pos + 1);
            } else {
                content.clear();
            }
        }
        return content;
    }

} // namespace

std::string bash_runner::exec(std::string_view const command) {
    std::string const cmd = std::format(
      "{};\n"          // the command itself
      "echo '{}'$?\n", // send the marker
      command,
      marker);

    if (write(to_child[1], cmd.data(), cmd.size()) != static_cast<ssize_t>(cmd.size())) {
        throw std::runtime_error("Failed to write to bash process");
    }

    std::string                   output;
    std::array<char, buffer_size> buf{};
    std::size_t                   n = 0;
    while ((n = static_cast<std::size_t>(read(from_child[0], buf.data(), buf.size()))) > 0) {
        output.append(buf.data(), n);
        auto const found = output.find(marker);
        if (found != std::string::npos) {
            std::string result       = output.substr(0, found);
            auto const  status_start = found + marker.size();
            auto const  status_end   = output.find('\n', status_start);
            std::string status_str;
            if (status_end != std::string::npos) {
                status_str = output.substr(status_start, status_end - status_start);
            } else {
                status_str = output.substr(status_start);
            }
            int status = 0;
            try {
                status = std::stoi(status_str);
            } catch (...) {
                // Ignore parse error
            }
            if (!result.empty() && result.back() == '\n') {
                result.pop_back();
            }
            if (status != 0) {
                throw std::runtime_error(std::format("Command failed with status {}", status));
            }
            return result;
        }
    }
    if (n < 0) {
        throw std::runtime_error("Error reading from bash process");
    }
    throw std::runtime_error("Unexpected end of output from bash process");
}

void bash_runner::start() {
    if (pipe2(to_child.data(), O_CLOEXEC) == -1 || pipe2(from_child.data(), O_CLOEXEC) == -1) {
        throw std::runtime_error("Failed to create pipes");
    }

    pid = fork();
    if (pid == -1) {
        throw std::runtime_error("Failed to fork");
    }

    if (pid == 0) { // Child process
        close(to_child[1]);
        close(from_child[0]);
        if (dup2(to_child[0], STDIN_FILENO) == -1 || dup2(from_child[1], STDOUT_FILENO) == -1) {
            _exit(1);
        }
        close(to_child[0]);
        close(from_child[1]);

        // Redirect stderr to stdout
        dup2(STDOUT_FILENO, STDERR_FILENO);

        execl("/bin/bash", "bash", "--noprofile", "--norc", static_cast<char*>(nullptr));
        _exit(1);
    }

    // Parent process
    close(to_child[0]);
    close(from_child[1]);
}

bash_runner::~bash_runner() {
    close(to_child[1]);
    close(from_child[0]);
    waitpid(pid, nullptr, 0);
}

std::string bash_runner::load(std::string_view const file) {
    return exec(read_file_and_remove_shebang(file));
}

void bash_runner::set_variable(std::string_view const name, std::string_view const value) {
    std::string escaped;
    escaped.reserve(value.size() + 4); // Rough estimate
    for (char const c : value) {
        if (c == '\'') {
            escaped += "'\\''";
        } else {
            escaped += c;
        }
    }
    exec(std::format("{}='{}'", name, escaped));
}

std::string bash_runner::get_variable(std::string_view const name) {
    return exec(std::format("echo \"${{{}}}\"", name));
}

std::string bash_runner::call_function(std::string_view const                  func_name,
                                       std::span<std::string_view const> const args) {
    std::string cmd(func_name);
    for (std::string_view const arg : args) {
        cmd += " '";
        for (char const c : arg) {
            if (c == '\'') {
                cmd += "'\\''";
            } else {
                cmd += c;
            }
        }
        cmd += "'";
    }
    return exec(cmd);
}

std::string bash_runner::get_variables() {
    return exec("compgen -v");
}

std::string bash_runner::get_functions() {
    return exec("compgen -A function");
}
