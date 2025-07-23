module;
#include <print>
export module foresight.main.log;
import foresight.mods.event;

export namespace foresight {

    constexpr struct [[nodiscard]] basic_log {
        template <typename... Args>
        void operator()(std::format_string<Args...> fmt, Args&&... args) const noexcept {
            try {
                std::println(stderr, std::move(fmt), std::forward<Args>(args)...);
            } catch (...) {
                // do nothing
            }
        }

        void operator()(event_type const& event) const noexcept {
            operator()("{} {} {}", event.type_name(), event.code_name(), event.value());
        }
    } log;


} // namespace foresight
