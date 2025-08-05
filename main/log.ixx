module;
#include <print>
export module foresight.main.log;
import foresight.mods.event;

export namespace foresight {

    constexpr struct [[nodiscard]] basic_log {
      private:
        std::string_view msg{};

      public:
        constexpr explicit basic_log(std::string_view const& inp_msg) noexcept : msg{inp_msg} {}

        constexpr basic_log() noexcept                            = default;
        consteval basic_log(basic_log const&) noexcept            = default;
        constexpr basic_log(basic_log&&) noexcept                 = default;
        consteval basic_log& operator=(basic_log const&) noexcept = default;
        constexpr basic_log& operator=(basic_log&&) noexcept      = default;
        constexpr ~basic_log() noexcept                           = default;

        template <typename... Args>
        void operator()(std::format_string<Args...> fmt, Args&&... args) const noexcept {
            try {
                std::println(stderr, std::move(fmt), std::forward<Args>(args)...);
            } catch (...) {
                // do nothing
            }
        }

        consteval basic_log operator[](std::string_view const str) const noexcept {
            return basic_log{str};
        }

        void operator()(event_type const& event) const noexcept {
            operator()("{}{} {} {}", msg, event.type_name(), event.code_name(), event.value());
        }
    } log;


} // namespace foresight
