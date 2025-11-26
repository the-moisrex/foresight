module;
#include <span>
#include <string_view>
export module foresight.main.systemd;

namespace fs8 {

    export struct systemd_service {
        void        execStart(std::span<std::string_view const> args);
        void        description(std::string_view desc);
        static bool check_systemd_support();
        void        install() const;
        void        enable(bool start_now = true) const;

      private:
        std::span<std::string_view const> args_;
        std::string_view                  description_;
    };
} // namespace fs8
