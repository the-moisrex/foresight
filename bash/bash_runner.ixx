// Created by moisrex on 10/27/25.

module;
#include <string_view>
export module foresight.bash;

namespace foresight {

    struct [[nodiscard]] bash_runner {
        void load(std::string_view);
        void exec(std::string_view);
    };
} // namespace foresight
