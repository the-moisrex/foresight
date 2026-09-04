// Created by moisrex on 10/27/25.

module;
#include <span>
#include <string>
#include <string_view>
export module fs8.bash;
import fs8.pimpl;

namespace fs8 {

    export struct [[nodiscard]] bash_runner : plain_pimpl_idiom<bash_runner> {
        bash_runner();
        bash_runner(bash_runner const&)                = delete;
        bash_runner(bash_runner&&) noexcept            = default;
        bash_runner& operator=(bash_runner const&)     = delete;
        bash_runner& operator=(bash_runner&&) noexcept = default;
        ~bash_runner();

        void                      start();
        [[nodiscard]] std::string load(std::string_view file);
        [[nodiscard]] std::string exec(std::string_view command);
        void                      set_variable(std::string_view name, std::string_view value);
        [[nodiscard]] std::string get_variable(std::string_view name);
        [[nodiscard]] std::string call_function(std::string_view func_name, std::span<std::string_view const> args);
        [[nodiscard]] std::string get_variables();
        [[nodiscard]] std::string get_functions();
    };
} // namespace fs8
