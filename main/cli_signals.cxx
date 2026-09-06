#include "cli_args.hxx"

#include <functional>
#include <vector>

import fs8;

namespace signals {
    // NOLINTBEGIN(*-avoid-non-const-global-variables)
    std::sig_atomic_t volatile sig{};
    std::vector<std::move_only_function<void(std::sig_atomic_t) const>> actions{};
    // NOLINTEND(*-avoid-non-const-global-variables)
} // namespace signals

void handle_signals(int const signal) {
    // let's not care about race conditions here, shall we?
    // I like to live dangerously here in `foresight` land.
    signals::sig = signal;
    for (auto const& func : signals::actions) {
        func(signals::sig);
    }
}
