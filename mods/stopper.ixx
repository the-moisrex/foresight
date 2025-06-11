// Created by moisrex on 6/11/25.

export module foresight.mods.stopper;
import foresight.mods.context;

export namespace foresight {
    constexpr struct basic_stopper {
      private:
        bool stopped = false;

      public:
        constexpr basic_stopper() noexcept                                 = default;
        constexpr basic_stopper(basic_stopper const &) noexcept            = default;
        constexpr basic_stopper(basic_stopper &&) noexcept                 = default;
        constexpr basic_stopper &operator=(basic_stopper const &) noexcept = default;
        constexpr basic_stopper &operator=(basic_stopper &&) noexcept      = default;
        constexpr ~basic_stopper() noexcept                                = default;

        constexpr void stop() noexcept {
            stopped = true;
        }

        constexpr context_action operator()([[maybe_unused]] Context auto &ctx) const noexcept {
            using enum context_action;
            if (stopped) {
                return exit;
            }
            return next;
        }
    } stopper;
} // namespace foresight
