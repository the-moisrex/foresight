#include <linux/input-event-codes.h>
#include <stdexcept>
import foresight.mods;
import foresight.main.log;
import foresight.main.utils;

int main(int const argc, char const* const* argv) try {
    using namespace foresight; // NOLINT(*-using-namespace)

    static constexpr auto args = arguments["USB Keyboard"];

    static constinit auto pipeline =
      context
      | intercept
      | search_engine
      | on(typed("@test"), type_string(U"nice"))
      | ignore_adjacent_syns
      | uinput;

    pipeline.mod(intercept).add_devs(args(argc, argv) | find_devices /* , grab_inputs */);
    pipeline();

    return 0;
} catch (std::runtime_error const& err) {
    foresight::log("Runtime Error: {}", err.what());
} catch (...) {
    foresight::log("Unknown Error.");
}
