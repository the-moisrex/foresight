// Created by moisrex on 9/4/26.

module;
#include <array>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <linux/uinput.h>
#include <string>
#include <string_view>
#include <unistd.h>
module fs8.mods;

using fs8::context_action;
using fs8::event_type;
using fs8::special_event;

// ── basic_replay members ─────────────────────────────────────────────────────

void fs8::basic_replay::set_file(std::string_view path) noexcept {
    ensure_state();
    st_->file_path = std::string{path};
}

context_action fs8::basic_replay::operator()(event_type& event, special_event const& tag) noexcept {
    using enum context_action;
    if (tag.code == fs8::start.code) {
        ensure_state();
        if (st_->file_path.empty()) {
            log("replay: no file set");
            return exit;
        }
        if (st_->fd >= 0 && st_->owns_fd) {
            ::close(st_->fd);
        }
        st_->fd = -1;
        st_->owns_fd = true;
        if (st_->file_path == "-") {
            st_->fd      = STDIN_FILENO;
            st_->owns_fd = false;
        } else {
            st_->fd = ::open(st_->file_path.c_str(), O_RDONLY | O_CLOEXEC);
            if (st_->fd < 0) {
                log("replay: failed to open {}", st_->file_path);
                return exit;
            }
        }
        // Read the header bytes to detect format.
        std::array<char, detail::format_header_size> header{};
        auto const                                   n = ::read(st_->fd, header.data(), header.size());
        if (n < static_cast<ssize_t>(detail::format_header_size)) {
            log("replay: file too short");
            if (st_->owns_fd) {
                ::close(st_->fd);
            }
            st_->fd = -1;
            return exit;
        }
        // Check binary magic: FFS8 (0x38534646) + version (u16)
        constexpr std::uint32_t binary_magic = 0x3853'4646u;
        std::uint32_t           file_magic{};
        std::memcpy(&file_magic, header.data(), sizeof(file_magic));
        if (file_magic == binary_magic) {
            st_->is_binary = true;
            return next; // header consumed
        }
        // Check if it looks like evtest text (all printable ASCII / tab / newline).
        bool looks_like_text = true;
        for (char c : header) {
            auto const u = static_cast<unsigned char>(c);
            if (u < 0x20 && u != 0x09 && u != 0x0A) {
                looks_like_text = false;
                break;
            }
        }
        if (looks_like_text) {
            // Evtest text format.
            st_->is_binary = false;
            st_->linebuf.clear();
            st_->linebuf.append(header.data(), static_cast<std::size_t>(n));
            return next;
        }
        // Raw binary input_event (no header) — e.g. piped from `intercept`.
        // Try to seek back so the existing binary reader works unchanged.
        if (st_->owns_fd && ::lseek(st_->fd, -n, SEEK_CUR) != -1) {
            st_->is_binary = true;
            return next;
        }
        // Cannot seek (pipe): stash the bytes we already read for the first event.
        st_->is_binary = true;
        std::memcpy(st_->header_buf.data(), header.data(), static_cast<std::size_t>(n));
        st_->header_len = static_cast<std::size_t>(n);
        return next;
    }
    if (tag.code != load_event.code) {
        return drop_event;
    }
    if (!static_cast<bool>(st_) || st_->fd < 0) {
        return exit;
    }
    if (st_->is_binary) {
        // Finish assembling the first event if we stashed bytes during format detection.
        if (st_->header_len > 0) {
            std::memcpy(&event.native(), st_->header_buf.data(), st_->header_len);
            auto       total = st_->header_len;
            st_->header_len = 0;
            while (total < detail::input_event_size) {
                auto const nread = ::read(st_->fd, reinterpret_cast<char*>(&event.native()) + total, detail::input_event_size - total);
                if (nread <= 0) {
                    return exit;
                }
                total += static_cast<std::size_t>(nread);
            }
            return next;
        }
        auto const result = ::read(st_->fd, &event.native(), sizeof(input_event));
        if (result == 0) {
            return exit; // EOF
        }
        if (result != sizeof(input_event)) {
            return exit; // error or partial read
        }
        return next;
    }
    // Evtest text format.
    // Try to parse existing lines in the buffer first.
    while (true) {
        auto const newline = st_->linebuf.find('\n');
        if (newline == std::string::npos) {
            break;
        }
        std::string_view const   line{st_->linebuf.data(), newline};
        parsed_evtest_event parsed;
        if (parse_evtest_line(line, parsed)) {
            st_->linebuf.erase(0, newline + 1);
            event = event_type{parsed.event};
            return next;
        }
        // Not an event line — skip.
        st_->linebuf.erase(0, newline + 1);
    }
    // Read more data from the file.
    while (true) {
        auto const buf_size = st_->linebuf.size();
        auto const cap      = buf_size + 4096;
        st_->linebuf.resize(cap);
        auto const nread = ::read(st_->fd, st_->linebuf.data() + buf_size, 4096);
        st_->linebuf.resize(buf_size + static_cast<std::size_t>(nread));
        if (nread == 0) {
            // EOF — try to flush any remaining partial line.
            if (!st_->linebuf.empty()) {
                parsed_evtest_event parsed;
                if (parse_evtest_line(st_->linebuf, parsed)) {
                    event = event_type{parsed.event};
                    return next;
                }
            }
            return exit;
        }
        // Parse every complete line in the buffer.
        while (true) {
            auto const nl = st_->linebuf.find('\n');
            if (nl == std::string::npos) {
                break;
            }
            std::string_view const   line{st_->linebuf.data(), nl};
            parsed_evtest_event parsed;
            if (parse_evtest_line(line, parsed)) {
                st_->linebuf.erase(0, nl + 1);
                event = event_type{parsed.event};
                return next;
            }
            st_->linebuf.erase(0, nl + 1);
        }
        if (nread > 0) {
            continue;
        }
        return drop_event;
    }
}
