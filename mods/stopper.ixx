// Created by moisrex on 6/11/25.

export module fs8.mods.stopper;
import fs8.context;

export namespace fs8 {

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

        context_action operator()() const noexcept {
            using enum context_action;
            return stopped ? exit : next;
        }
    } stopper;

} // namespace fs8
