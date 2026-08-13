// Created by moisrex on 9/9/22.

module;
#include <cstddef>
#include <linux/input.h>
#include <string_view>
export module fs8.keyboard;
import fs8.devices.evdev;
import fs8.translate;
import fs8.pimpl;

namespace fs8 {
    export constexpr std::size_t give_up_limit = 5;

    export struct [[nodiscard]] keyboard_runner : plain_pimpl_idiom<keyboard_runner> {
        keyboard_runner();
        keyboard_runner(keyboard_runner const &)            = delete;
        keyboard_runner(keyboard_runner &&)                 = delete;
        keyboard_runner &operator=(keyboard_runner const &) = delete;
        keyboard_runner &operator=(keyboard_runner &&)      = delete;
        ~keyboard_runner() noexcept;

        void to_string();

        void check();

        void replace(std::string_view replace_it, std::string_view replace_with);
        void remove(std::string_view text);

        void backspace(std::size_t count = 1);
        void put(std::string_view text);
        void put(input_event ev);

        void buffer(input_event &ev);

        // this loop is the main loop
        int loop() noexcept;
    };
} // namespace fs8
