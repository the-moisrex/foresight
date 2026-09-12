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
import fs8.pimpl;
import fs8.log;
import fs8.lib.evtest;

using fs8::basic_replay;
using fs8::context_action;
using fs8::event_type;
using fs8::special_event;

template <>
struct fs8::pimpl_idiom<basic_replay>::impl {
    std::string                                file_path;
    int                                        fd        = -1;
    bool                                       owns_fd   = true;
    bool                                       is_binary = false;
    std::string                                linebuf;
    std::array<char, detail::input_event_size> header_buf{}; // leftover bytes from format detection (pipe only)
    std::size_t                                header_len = 0;

    // Binary read buffer — batches syscalls (~170 events per 4 KiB read).
    static constexpr std::size_t    read_buf_size = 4096;
    std::array<char, read_buf_size> read_buf{};
    std::size_t                     read_buf_len = 0; // bytes valid in read_buf
    std::size_t                     read_buf_pos = 0; // next byte to consume

    // Text line cursor — avoids O(n²) erase on every parsed line.
    std::size_t linebuf_pos = 0; // start of unprocessed data in linebuf

    // ── Helpers ──────────────────────────────────────────────────────────────

    context_action handle_start() noexcept {
        using enum context_action;
        if (file_path.empty()) {
            log("replay: no file set");
            return exit;
        }
        if (fd >= 0 && owns_fd) {
            ::close(fd);
        }
        fd           = -1;
        owns_fd      = true;
        read_buf_pos = 0;
        read_buf_len = 0;
        linebuf_pos  = 0;
        if (file_path == "-") {
            fd      = STDIN_FILENO;
            owns_fd = false;
        } else {
            fd = ::open(file_path.c_str(), O_RDONLY | O_CLOEXEC);
            if (fd < 0) {
                log("replay: failed to open {}", file_path);
                return exit;
            }
        }
        // Read the header bytes to detect format.
        std::array<char, detail::format_header_size> header{};
        auto const                                   n = ::read(fd, header.data(), header.size());
        if (n < static_cast<ssize_t>(detail::format_header_size)) {
            log("replay: file too short");
            if (owns_fd) {
                ::close(fd);
            }
            fd = -1;
            return exit;
        }
        // Check binary magic: FFS8 (0x38534646) + version (u16)
        constexpr std::uint32_t binary_magic = 0x3853'4646u;
        std::uint32_t           file_magic{};
        std::memcpy(&file_magic, header.data(), sizeof(file_magic));
        if (file_magic == binary_magic) {
            is_binary = true;
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
            is_binary = false;
            linebuf.clear();
            linebuf.append(header.data(), static_cast<std::size_t>(n));
            return next;
        }
        // Raw binary input_event (no header) — e.g. piped from `intercept`.
        // Try to seek back so the existing binary reader works unchanged.
        if (owns_fd && ::lseek(fd, -n, SEEK_CUR) != -1) {
            is_binary = true;
            return next;
        }
        // Cannot seek (pipe): stash the bytes we already read for the first event.
        is_binary = true;
        std::memcpy(header_buf.data(), header.data(), static_cast<std::size_t>(n));
        header_len = static_cast<std::size_t>(n);
        return next;
    }

    context_action handle_load_binary(event_type& event) noexcept {
        using enum context_action;
        // Finish assembling the first event if we stashed bytes during format detection.
        if (header_len > 0) {
            std::memcpy(&event.native(), header_buf.data(), header_len);
            auto total = header_len;
            header_len = 0;
            while (total < detail::input_event_size) {
                auto const nread = ::read(fd, reinterpret_cast<char*>(&event.native()) + total, detail::input_event_size - total);
                if (nread <= 0) {
                    return exit;
                }
                total += static_cast<std::size_t>(nread);
            }
            return next;
        }
        // Serve from read buffer if data is available.
        if (read_buf_pos + detail::input_event_size <= read_buf_len) {
            std::memcpy(&event.native(), read_buf.data() + read_buf_pos, detail::input_event_size);
            read_buf_pos += detail::input_event_size;
            return next;
        }
        // Refill the read buffer.
        auto const nread = ::read(fd, read_buf.data(), read_buf_size);
        if (nread <= 0) {
            return exit;
        }
        read_buf_len = static_cast<std::size_t>(nread);
        read_buf_pos = 0;
        if (read_buf_len < detail::input_event_size) {
            return exit; // partial trailing event
        }
        std::memcpy(&event.native(), read_buf.data(), detail::input_event_size);
        read_buf_pos = detail::input_event_size;
        return next;
    }

    /// Parse complete lines already in the line buffer; returns `next` on the
    /// first successfully parsed event, or `drop_event` if nothing was parsed.
    context_action parse_buffered_lines(event_type& event) noexcept {
        using enum context_action;
        while (linebuf_pos < linebuf.size()) {
            auto const nl = linebuf.find('\n', linebuf_pos);
            if (nl == std::string::npos) {
                break;
            }
            std::string_view const line{linebuf.data() + linebuf_pos, nl - linebuf_pos};
            parsed_evtest_event    parsed;
            if (parse_evtest_line(line, parsed)) {
                linebuf_pos = nl + 1;
                event       = event_type{parsed.event};
                return next;
            }
            // Not an event line — skip.
            linebuf_pos = nl + 1;
        }
        return drop_event;
    }

    context_action handle_load_text(event_type& event) noexcept {
        using enum context_action;
        // Try to parse existing lines in the buffer first.
        if (auto r = parse_buffered_lines(event); r == next) {
            return next;
        }
        // Compact before reading more data.
        linebuf.erase(0, linebuf_pos);
        linebuf_pos = 0;
        // Read more data from the file.
        while (true) {
            auto const buf_size = linebuf.size();
            linebuf.resize(buf_size + 4096);
            auto const nread = ::read(fd, linebuf.data() + buf_size, 4096);
            linebuf.resize(buf_size + static_cast<std::size_t>(nread));
            if (nread == 0) {
                // EOF — try to flush any remaining partial line.
                if (linebuf_pos < linebuf.size()) {
                    parsed_evtest_event parsed;
                    if (parse_evtest_line(std::string_view{linebuf}.substr(linebuf_pos), parsed)) {
                        event = event_type{parsed.event};
                        return next;
                    }
                }
                return exit;
            }
            if (auto r = parse_buffered_lines(event); r == next) {
                return next;
            }
            // Compact and continue reading.
            linebuf.erase(0, linebuf_pos);
            linebuf_pos = 0;
        }
    }
};

// ── basic_replay members ─────────────────────────────────────────────────────

void basic_replay::set_file(std::string_view path) noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    pimpl->file_path = std::string{path};
}

context_action basic_replay::operator()(event_type& event, special_event const& tag) noexcept {
    using enum context_action;
    if (tag.code == fs8::start.code) {
        if (pimpl.get() == nullptr) [[unlikely]] {
            init_impl();
        }
        return pimpl->handle_start();
    }
    if (tag.code != load_event.code) {
        return drop_event;
    }
    if (pimpl.get() == nullptr || pimpl->fd < 0) {
        return exit;
    }
    return pimpl->is_binary ? pimpl->handle_load_binary(event) : pimpl->handle_load_text(event);
}
