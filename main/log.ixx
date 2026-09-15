module;
#include <format>
#include <print>
export module fs8.log;
import fs8.event;

export namespace fs8 {

    constexpr struct [[nodiscard]] basic_log {
      private:
        std::string_view msg;

      public:
        constexpr explicit basic_log(std::string_view const& inp_msg) noexcept : msg{inp_msg} {}

        constexpr basic_log() noexcept                            = default;
        consteval basic_log(basic_log const&) noexcept            = default;
        constexpr basic_log(basic_log&&) noexcept                 = default;
        consteval basic_log& operator=(basic_log const&) noexcept = default;
        constexpr basic_log& operator=(basic_log&&) noexcept      = default;
        constexpr ~basic_log() noexcept                           = default;

        template <typename... Args>
        void operator()(std::format_string<Args...> fmt, Args&&... args) const noexcept try {
            std::println(stderr, std::move(fmt), std::forward<Args>(args)...);
        } catch (...) {
            std::terminate();
        }

        consteval basic_log operator[](std::string_view const str) const noexcept {
            return basic_log{str};
        }

        /// Format the stored message with one runtime string argument, e.g.
        /// `log["monitor changed to: {}"](monitor_name)`.
        void operator()(std::string_view const arg) const noexcept try {
            if (msg.empty()) {
                std::println(stderr, "{}", arg);
            } else {
                std::println(stderr, "{}", std::vformat(msg, std::make_format_args(arg)));
            }
        } catch (...) {
            std::terminate();
        }

        void operator()(event_type const& event) const noexcept {
            operator()("{}{} {} {}", msg, event.type_name(), event.code_name(), event.value());
        }
    } log;


} // namespace fs8
