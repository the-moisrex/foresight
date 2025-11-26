// Created by moisrex on 10/27/25.

module;
#include <array>
#include <span>
#include <string>
#include <string_view>
export module foresight.bash;

namespace fs8 {

    export struct [[nodiscard]] bash_runner {
      private:
        std::array<int, 2> to_child{};
        std::array<int, 2> from_child{};
        pid_t              pid{};

      public:
        constexpr bash_runner()                        = default;
        bash_runner(bash_runner const&)                = delete;
        bash_runner(bash_runner&&) noexcept            = default;
        bash_runner& operator=(bash_runner const&)     = delete;
        bash_runner& operator=(bash_runner&&) noexcept = default;
        ~bash_runner();

        void        start();
        std::string load(std::string_view file);
        std::string exec(std::string_view command);
        void        set_variable(std::string_view name, std::string_view value);
        std::string get_variable(std::string_view name);
        std::string call_function(std::string_view func_name, std::span<std::string_view const> args);
        [[nodiscard]] std::string get_variables();
        [[nodiscard]] std::string get_functions();
    };
} // namespace fs8
