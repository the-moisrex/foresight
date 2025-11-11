#include <linux/input-event-codes.h>
#include <stdexcept>
import foresight.mods;
import foresight.main.log;
import foresight.main.utils;

int main(int const argc, char const* const* argv) try {
    using namespace foresight; // NOLINT(*-using-namespace)
    using namespace std::chrono_literals;

    static constexpr auto args = arguments["pen", "usb keyboard"];
    static constinit auto pipeline =
      context
      | intercept
      | scheduled_emitter
      | search_engine
      | keys_status
      | mice_quantifier
      | swipe_detector
      | ignore_adjacent_syns
      | uinput;

    pipeline.mod(intercept).add_devs(args(argc, argv) | find_devices, grab_inputs);
    pipeline();

    return 0;
} catch (std::runtime_error const& err) {
    foresight::log("Runtime Error: {}", err.what());
} catch (...) {
    foresight::log("Unknown Error.");
}
