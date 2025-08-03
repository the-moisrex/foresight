// Created by moisrex on 6/11/25.

export module foresight.mods.stopper;
import foresight.mods.context;

export namespace foresight {

    constexpr struct [[nodiscard]] basic_stopper {
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

        constexpr context_action operator()() const noexcept {
            using enum context_action;
            return stopped ? exit : next;
        }
    } stopper;

} // namespace foresight
