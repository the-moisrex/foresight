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

template <fs8::capture_format FormatT>
context_action fs8::basic_replay<FormatT>::operator()(special_event const& tag) noexcept {
    using enum context_action;
    if (tag.code != fs8::start.code) {
        return drop_event;
    }
    ensure_state();
    if (st_->file_path.empty()) {
        fs8::log("replay: no file set");
        return exit;
    }
    if (st_->fd >= 0) {
        ::close(st_->fd);
        st_->fd = -1;
    }
    st_->fd = ::open(st_->file_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (st_->fd < 0) {
        fs8::log("replay: failed to open {}", st_->file_path);
        return exit;
    }
    // Read the header bytes to detect format.
    std::array<char, fs8::detail::format_header_size> header{};
    auto const n = ::read(st_->fd, header.data(), header.size());
    if (n < fs8::detail::format_header_size) {
        fs8::log("replay: file too short");
        ::close(st_->fd);
        st_->fd = -1;
        return exit;
    }
    // Check binary magic: FFS8 (0x38534646) + version (u16)
    constexpr std::uint32_t binary_magic = 0x38534646u;
    std::uint32_t           file_magic{};
    std::memcpy(&file_magic, header.data(), sizeof(file_magic));
    if (file_magic == binary_magic) {
        st_->is_binary = true;
        return next; // header consumed
    }
    // Not binary — assume evtest text format.
    st_->is_binary     = false;
    st_->linebuf.clear();
    st_->linebuf.append(header.data(), static_cast<std::size_t>(n));
    return next;
}

template <fs8::capture_format FormatT>
context_action fs8::basic_replay<FormatT>::operator()(event_type& event, special_event const& tag) noexcept {
    using enum context_action;
    if (tag.code != fs8::load_event.code) {
        return drop_event;
    }
    if (!static_cast<bool>(st_) || st_->fd < 0) {
        return exit;
    }
    if (st_->is_binary) {
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
        std::string_view const line{st_->linebuf.data(), newline};
        fs8::parsed_evtest_event parsed;
        if (fs8::parse_evtest_line(line, parsed)) {
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
                fs8::parsed_evtest_event parsed;
                if (fs8::parse_evtest_line(st_->linebuf, parsed)) {
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
            std::string_view const line{st_->linebuf.data(), nl};
            fs8::parsed_evtest_event parsed;
            if (fs8::parse_evtest_line(line, parsed)) {
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

// ── Explicit instantiations ──────────────────────────────────────────────────

template struct fs8::basic_replay<fs8::capture_binary_format>;
template struct fs8::basic_replay<fs8::capture_evtest_format>;
