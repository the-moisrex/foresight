// Created by moisrex on 6/18/24.

module;
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <libevdev/libevdev.h>
#include <memory_resource>
#include <numeric>
#include <optional>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>
module foresight.evdev;
import foresight.mods.caps;
import foresight.main.log;

using foresight::evdev;

std::string_view foresight::to_string(evdev_status const status) noexcept {
    using enum evdev_status;
    switch (status) {
        case unknown: return {"Unknown state."};
        case success: return {"Success."};
        case grab_failure: return {"Grabbing the input failed."};
        case invalid_file_descriptor: return {"The file descriptor specified is not valid."};
        case invalid_device: return {"The device is not valid."};
        case failed_setting_file_descriptor: return {"Failed to set the file descriptor."};
        case failed_to_open_file: return {"Failed to open the file."};
        default: return {"Invalid state."};
    }
}

evdev::evdev(std::filesystem::path const& file) noexcept {
    set_file(file);
}

evdev::evdev(evdev&& inp) noexcept
  : dev{std::exchange(inp.dev, nullptr)},
    status{std::exchange(inp.status, evdev_status::unknown)} {}

evdev& evdev::operator=(evdev&& other) noexcept {
    if (&other != this) {
        dev    = std::exchange(other.dev, nullptr);
        status = std::exchange(other.status, evdev_status::unknown);
    }
    return *this;
}

evdev::~evdev() noexcept {
    this->close();
}

void evdev::close() noexcept {
    if (is_fd_initialized()) {
        libevdev_grab(dev, LIBEVDEV_UNGRAB);
    }
    auto const file_descriptor = native_handle();
    if (dev != nullptr) {
        libevdev_free(dev);
        dev = nullptr;
    }
    if (file_descriptor >= 0) {
        ::close(file_descriptor);
    }
    status = evdev_status::unknown;
}

void evdev::set_file(std::filesystem::path const& file) noexcept {
    auto const new_fd = ::open(file.c_str(), O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (new_fd < 0) [[unlikely]] {
        this->close();
        status = evdev_status::failed_to_open_file;
        return;
    }
    set_file(new_fd);
}

void evdev::set_file(int const file) noexcept {
    this->close();
    if (file < 0) [[unlikely]] {
        status = evdev_status::invalid_file_descriptor;
        return;
    }
    int const res_rc = libevdev_new_from_fd(file, &dev);
    if (dev == nullptr) [[unlikely]] {
        this->close();
        status = evdev_status::invalid_device;
        return;
    }
    if (res_rc < 0) [[unlikely]] {
        // res_rc is now -errno
        ::close(file);
        this->close();
        status = evdev_status::failed_setting_file_descriptor;
        return;
    }
    status = evdev_status::success;
}

void evdev::init_new() noexcept {
    this->close();
    dev = libevdev_new();
}

int evdev::native_handle() const noexcept {
    if (dev == nullptr) [[unlikely]] {
        return -1;
    }
    return libevdev_get_fd(dev);
}

libevdev* evdev::device_ptr() const noexcept {
    return dev;
}

bool evdev::is_fd_initialized() const noexcept {
    return native_handle() != -1;
}

void evdev::grab_input(bool const grab) noexcept {
    if (!is_fd_initialized()) [[unlikely]] {
        return;
    }
    if (libevdev_grab(dev, grab ? LIBEVDEV_GRAB : LIBEVDEV_UNGRAB) < 0) {
        status = evdev_status::grab_failure;
    }
}

std::string_view evdev::device_name() const noexcept {
    if (dev == nullptr) [[unlikely]] {
        return invalid_device_name;
    }
    return libevdev_get_name(dev);
}

void evdev::device_name(std::string_view const new_name) noexcept {
    if (dev == nullptr) [[unlikely]] {
        return;
    }
    libevdev_set_name(dev, new_name.data());
}

std::string_view evdev::physical_location() const noexcept {
    if (dev == nullptr) [[unlikely]] {
        return invalid_device_location;
    }
    auto const* res = libevdev_get_phys(dev);
    if (res == nullptr) [[unlikely]] {
        return invalid_device_location;
    }
    return {res};
}

std::string_view evdev::unique_identifier() const noexcept {
    if (dev == nullptr) [[unlikely]] {
        return invalid_unique_identifier;
    }
    auto const* res = libevdev_get_uniq(dev);
    if (res == nullptr) [[unlikely]] {
        return invalid_unique_identifier;
    }
    return {res};
}

void evdev::physical_location(std::string_view const new_name) noexcept {
    if (dev == nullptr) [[unlikely]] {
        return;
    }
    libevdev_set_phys(dev, new_name.data());
}

void evdev::unique_identifier(std::string_view const new_name) noexcept {
    if (dev == nullptr) [[unlikely]] {
        return;
    }
    libevdev_set_uniq(dev, new_name.data());
}

void evdev::enable_event_type(ev_type const type) noexcept {
    if (dev == nullptr) [[unlikely]] {
        return;
    }
    if (libevdev_enable_event_type(dev, type) != 0) [[unlikely]] {
        status = evdev_status::failed_to_set_options;
    }
}

void evdev::enable_event_code(ev_type const type, code_type const code) noexcept {
    enable_event_code(type, code, nullptr);
}

void evdev::enable_event_code(ev_type const type, code_type const code, void const* const value) noexcept {
    if (dev == nullptr) [[unlikely]] {
        return;
    }
    if (libevdev_enable_event_code(dev, type, code, value) != 0) [[unlikely]] {
        status = evdev_status::failed_to_set_options;
    }
}

void evdev::enable_caps(dev_caps_view const inp_caps) noexcept {
    for (auto const& [type, codes, _] : inp_caps) {
        for (auto const code : codes) {
            enable_event_code(type, code);
        }
    }
}

void evdev::disable_event_type(ev_type const type) noexcept {
    if (dev == nullptr) [[unlikely]] {
        return;
    }
    if (libevdev_disable_event_type(dev, type) != 0) [[unlikely]] {
        status = evdev_status::failed_to_set_options;
    }
}

void evdev::disable_event_code(ev_type const type, code_type const code) noexcept {
    if (dev == nullptr) [[unlikely]] {
        return;
    }
    if (libevdev_disable_event_code(dev, type, code) != 0) [[unlikely]] {
        status = evdev_status::failed_to_set_options;
    }
}

void evdev::disable_caps(dev_caps_view const inp_caps) noexcept {
    for (auto const& [type, codes, _] : inp_caps) {
        for (auto const code : codes) {
            disable_event_code(type, code);
        }
    }
}

void evdev::apply_caps(dev_caps_view const inp_caps) noexcept {
    using enum caps_action;
    for (auto const& [type, codes, action] : inp_caps) {
        switch (action) {
            case append:
                for (auto const code : codes) {
                    enable_event_code(type, code);
                }
                break;
            case remove_codes:
                for (auto const code : codes) {
                    disable_event_code(type, code);
                }
                break;
            case remove_type: disable_event_type(type); break;
        }
    }
}

bool evdev::has_event_type(ev_type const type) const noexcept {
    if (dev == nullptr) [[unlikely]] {
        return false;
    }
    return libevdev_has_event_type(dev, type) == 1;
}

bool evdev::has_event_code(ev_type const type, code_type const code) const noexcept {
    if (dev == nullptr) [[unlikely]] {
        return false;
    }
    return libevdev_has_event_code(dev, type, code) == 1;
}

bool evdev::has_cap(dev_cap_view const& inp_cap) const noexcept {
    return std::ranges::all_of(inp_cap.codes, [this, type = inp_cap.type](auto const code) {
        return has_event_code(type, code);
    });
}

// returns percentage
std::uint8_t evdev::match_cap(dev_cap_view const& inp_cap) const noexcept {
    double count = 0;
    for (code_type const code : inp_cap.codes) {
        if (has_event_code(inp_cap.type, code)) {
            ++count;
        }
    }
    return static_cast<std::uint8_t>(count / static_cast<double>(inp_cap.codes.size()) * 100);
}

bool evdev::has_caps(dev_caps_view const inp_caps) const noexcept {
    return std::ranges::all_of(inp_caps, [this](auto const& inp_cap) noexcept {
        return has_cap(inp_cap);
    });
}

std::uint8_t evdev::match_caps(dev_caps_view const inp_caps) const noexcept {
    using enum caps_action;
    double count = 0;
    double all   = 0;
    for (auto const& [type, codes, action] : inp_caps) {
        switch (action) {
            case append:
                for (code_type const code : codes) {
                    if (has_event_code(type, code)) {
                        ++count;
                    }
                }
                break;
            case remove_codes:
                for (code_type const code : codes) {
                    if (has_event_code(type, code)) {
                        --count;
                    }
                }
                break;
            case remove_type:
                if (has_event_type(type)) {
                    --count;
                }
                break;
        }
        all += static_cast<double>(codes.size());
    }
    return static_cast<std::uint8_t>(std::max(0.0, count) / all * 100.0); // NOLINT(*-magic-numbers)
}

input_absinfo const* evdev::abs_info(code_type const code) const noexcept {
    if (dev == nullptr) [[unlikely]] {
        return nullptr;
    }
    return libevdev_get_abs_info(dev, code);
}

bool evdev::has_abs_info(code_type const code) const noexcept {
    return this->abs_info(code) != nullptr;
}

void evdev::abs_info(code_type const abs_code, input_absinfo const& abs_info) noexcept {
    if (is_fd_initialized()) {
        return;
    }
    // Decide if this should implicitly enable the code if not present,
    // or require it to be enabled first.
    if (!has_event_code(EV_ABS, abs_code)) {
        enable_event_code(EV_ABS, abs_code, &abs_info);
    }

    // Code exists, update its info
    libevdev_set_abs_info(dev, abs_code, &abs_info);
}

std::optional<input_event> evdev::next() noexcept {
    input_event input{};

    if (dev == nullptr) [[unlikely]] {
        return std::nullopt;
    }

    switch (libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &input)) {
        [[likely]] case LIBEVDEV_READ_STATUS_SUCCESS: { return input; }
        [[unlikely]] case LIBEVDEV_READ_STATUS_SYNC: {
            // handling 'SYN_DROPPED's:
            int rc = LIBEVDEV_READ_STATUS_SYNC;
            while (rc == LIBEVDEV_READ_STATUS_SYNC) {
                rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_SYNC, &input);
            }
        }
        case -EAGAIN: break;
        default: return std::nullopt;
    }

    return std::nullopt;
}

foresight::evdev_rank foresight::device(dev_caps_view const inp_caps) {
    evdev_rank res;

    for (evdev_rank&& rank : foresight::rank_devices(inp_caps)) {
        if (rank.score > res.score) {
            res = std::move(rank);
        }
    }
    return res;
}

namespace {
    void trim(std::string_view& str) noexcept {
        auto const start = str.find_first_not_of(' ');
        auto const end   = str.find_last_not_of(' ');
        str              = str.substr(start, end - start + 1);
    }

    /// Function to calculate Levenshtein distance (case-insensitive: both strings to lowercase)
    /// Returns the minimum number of edits (insertions, deletions, substitutions)
    /// required to transform s1 into s2.
    [[nodiscard]] std::uint32_t levenshtein_distance(std::string_view const lhs,
                                                     std::string_view const rhs) noexcept {
        try {
            auto const m = static_cast<std::uint32_t>(lhs.size());
            auto const n = static_cast<std::uint32_t>(rhs.size());
            if (m == 0) {
                return n;
            }
            if (n == 0) {
                return m;
            }
            std::array<std::byte, 2048>                          buffer; // NOLINT(*-init)
            std::pmr::monotonic_buffer_resource                  mbr{buffer.data(), buffer.size()};
            std::pmr::polymorphic_allocator<std::uint32_t> const pa{&mbr};
            std::pmr::vector<std::pmr::vector<std::uint32_t>>    matrix(m + 1, pa);
            for (std::uint32_t i = 0; i <= m; ++i) {
                matrix[i].resize(n + 1);
                matrix[i][0] = i;
            }
            for (std::uint32_t i = 0; i <= n; ++i) {
                matrix[0][i] = i;
            }
            std::uint32_t above_cell    = 0;
            std::uint32_t left_cell     = 0;
            std::uint32_t diagonal_cell = 0;
            std::uint32_t cost          = 0;
            for (std::uint32_t i = 1; i <= m; ++i) {
                for (std::uint32_t j = 1; j <= n; ++j) {
                    auto const l  = static_cast<char>(std::tolower(static_cast<unsigned char>(lhs[i - 1])));
                    auto const r  = static_cast<char>(std::tolower(static_cast<unsigned char>(rhs[j - 1])));
                    cost          = l == r ? 0 : 1;
                    above_cell    = matrix[i - 1][j];
                    left_cell     = matrix[i][j - 1];
                    diagonal_cell = matrix[i - 1][j - 1];
                    matrix[i][j]  = std::min({above_cell + 1, left_cell + 1, diagonal_cell + cost});
                }
            }
            return matrix[m][n];
        } catch (...) {
            return 0;
        }
    }

    /// Function to perform a fuzzy search score between two strings (case-insensitive).
    /// Returns a score (0-100) indicating how well lhs matches rhs, using a simple
    /// subsequence and character match approach.
    [[nodiscard]] std::uint16_t fuzzy_search(std::string_view const lhs,
                                             std::string_view const rhs) noexcept {
        if (lhs.empty() && rhs.empty()) {
            return 100;
        }
        if (lhs.empty() || rhs.empty()) {
            return 0;
        }

        auto const lhs_len = lhs.size();
        auto const rhs_len = rhs.size();

        // Simple fuzzy: count matching characters in order (subsequence match)
        std::size_t match_count = 0;
        std::size_t rhs_pos     = 0;
        for (std::size_t i = 0; i < lhs_len; ++i) {
            auto const c = static_cast<char>(std::tolower(lhs[i]));
            while (rhs_pos < rhs_len) {
                auto const rc = static_cast<char>(std::tolower(rhs[rhs_pos]));
                if (rc == c) {
                    ++match_count;
                    ++rhs_pos;
                    break;
                }
                ++rhs_pos;
            }
            if (rhs_pos == rhs_len) {
                break;
            }
        }

        // Score: percent of lhs characters found in order in rhs
        return static_cast<std::uint16_t>(
          (static_cast<double>(match_count) / static_cast<double>(lhs_len)) * 100);
    }

    [[nodiscard]] std::uint16_t subset_score(std::string_view const lhs,
                                             std::string_view const rhs) noexcept {
        std::uint16_t score   = 0;
        auto const    lhs_len = lhs.size();
        auto const    rhs_len = rhs.size();
        if (rhs_len >= lhs_len) {
            for (std::size_t i = 0; i <= rhs_len - lhs_len; ++i) {
                bool is_substring = true;
                for (std::size_t j = 0; j < lhs_len; ++j) {
                    auto const rc = static_cast<char>(std::tolower(rhs[i + j]));
                    auto const lc = static_cast<char>(std::tolower(lhs[j]));
                    if (rc != lc) {
                        is_substring = false;
                        break;
                    }
                }
                if (is_substring) {
                    score += 100;
                }
            }
        }
        return score;
    }

    [[nodiscard]] std::uint16_t levenshtein_score(std::string_view const lhs,
                                                  std::string_view const rhs) noexcept {
        auto const   distance = static_cast<double>(levenshtein_distance(lhs, rhs));
        double const max_len  = static_cast<double>(std::max(lhs.length(), rhs.length()));

        // Calculate similarity: (max_len - distance) / max_len * 100
        // Cast to double for accurate division
        double const similarity = (max_len - distance) / max_len;
        return static_cast<std::uint16_t>(similarity * 100);
    }

    /// Calculate a percent score of matching between the two strings
    [[nodiscard]] std::uint16_t calc_score(std::string_view const lhs, std::string_view const rhs) noexcept {
        if (lhs.empty() && rhs.empty()) {
            return 100; // Both empty, considered 100% match
        }
        if (lhs.empty() || rhs.empty()) {
            return 0;   // One is empty, no match (or adjust as per a specific requirement)
        }

        return static_cast<std::uint16_t>(
          (levenshtein_score(lhs, rhs)
           + fuzzy_search(lhs, rhs)
           + subset_score(lhs, rhs)
           + subset_score(rhs, lhs))
          / 4);
    }

    [[nodiscard]] std::pair<foresight::dev_caps_view, std::uint16_t> find_caps(
      std::string_view const query) noexcept {
        foresight::dev_caps_view caps{};
        std::uint16_t            score = 0;
        for (auto const& [name, cap_view] : foresight::caps::cap_maps) {
            if (query == name) {
                return {cap_view, 100};
            }
            auto const cur_score = calc_score(query, name);
            if (cur_score > score) {
                score = cur_score;
                caps  = cap_view;
            }
        }
        return {caps, score};
    }

} // namespace

foresight::evdev_rank foresight::device(std::string_view query) {
    trim(query);

    // check if it's a path
    if (query.starts_with('/')) {
        std::filesystem::path const path{query};
        if (exists(path)) {
            return {.score = 100, .dev = evdev{path}};
        }
    }

    auto const [caps, caps_score] = find_caps(query);

    evdev_rank best{};
    for (evdev&& dev : all_input_devices()) {
        auto const name = dev.device_name();
        auto const loc  = dev.physical_location();
        auto const id   = dev.unique_identifier();

        if (!dev.is_fd_initialized() || (name == invalid_device_name) || (loc == invalid_device_location)) {
            continue;
        }

        std::uint16_t const name_score = calc_score(query, name);
        auto const          loc_score  = static_cast<std::uint16_t>(calc_score(query, loc) / 2);
        auto const          id_score   = static_cast<std::uint16_t>(calc_score(query, id) * 1.5);
        auto const          cur_caps_score =
          static_cast<std::uint16_t>(dev.match_caps(caps) * (static_cast<double>(caps_score) / 100 + 1));
        auto const score =
          static_cast<std::uint8_t>((name_score + loc_score + id_score + cur_caps_score) / 4);
        if (score > best.score) {
            best.score = score;
            best.dev   = std::move(dev);
        }
        log("  - Score {}% ({}/{}/{}/{}): {} {} {}",
            score,
            name_score,
            loc_score,
            id_score,
            cur_caps_score,
            name,
            loc,
            id);
    }
    log("  + Best device with score {}%: {} {}",
        best.score,
        best.dev.device_name(),
        best.dev.physical_location());
    return best;
}
