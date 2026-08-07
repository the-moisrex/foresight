// Created by moisrex on 6/11/25.

export module fs8.mods.stopper;
import fs8.context;
import fs8.traits;

export namespace fs8 {

    constexpr struct [[nodiscard]] basic_stopper : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        bool stopped = false;

      public:
        constexpr void stop() noexcept {
            stopped = true;
        }

        context_action operator()() const noexcept {
            using enum context_action;
            return stopped ? exit : next;
        }
    } stopper;

} // namespace fs8
