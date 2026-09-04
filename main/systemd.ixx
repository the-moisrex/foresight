module;
#include <span>
#include <string_view>
export module fs8.systemd;
import fs8.pimpl;

namespace fs8 {

    export struct [[nodiscard]] systemd_service : plain_pimpl_idiom<systemd_service> {
        systemd_service() noexcept;
        ~systemd_service() noexcept;

        void        execStart(std::span<std::string_view const> args);
        void        description(std::string_view desc);
        [[nodiscard]] static bool check_systemd_support();
        void        install() const;
        void        enable(bool start_now = true) const;
    };
} // namespace fs8
