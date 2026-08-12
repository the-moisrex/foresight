#include <stdexcept>

import fs8.mods;
import fs8.log;
import fs8.utils;
import fs8.devices.queries;

int main(int const argc, char const* const* argv) try {
    using namespace fs8; // NOLINT(*-using-namespace)

    static constexpr auto args = arguments["USB Keyboard Copied"];

    static constinit auto pipeline =
      context
      | io_manager
      | intercept[keyboard | fail_on_no_match]
      | input_manager
      | search_engine
      | on[typed["@test"], type_string["nice"]]
      | ignore_adjacent_syns
      | uinput;

    pipeline.mod(intercept).add(args(argc, argv) | grab | fail_on_no_match);
    pipeline();

    return 0;
} catch (std::runtime_error const& err) {
    fs8::log("Runtime Error: {}", err.what());
} catch (...) {
    fs8::log("Unknown Error.");
}
