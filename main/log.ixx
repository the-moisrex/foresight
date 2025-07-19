module;
#include <print>
export module foresight.main.log;

export namespace foresight {

    template <typename... Args>
    void log(std::format_string<Args...> fmt, Args&&... args) {
        std::println(stderr, std::move(fmt), std::forward<Args>(args)...);
    }

} // namespace foresight
