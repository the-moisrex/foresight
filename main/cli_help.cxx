#include "cli_args.hxx"

#include <algorithm>
#include <coroutine>
#include <print>
#include <string>
#include <vector>

import fs8;
import fs8.devices.udev;
import fs8.lib.evtest;

void print_version() {
#ifdef FORESIGHT_VERSION
    std::println("foresight {}", FORESIGHT_VERSION);
#else
    std::println("foresight (unknown version)");
#endif
}

void print_help() {
    std::println("{}", R"TEXT(Usage: foresight [options] [action]
  Arguments:
    -h | --help                   Print help.
    -v | --version                Print version.

  Actions:
    intercept [queries...]        Intercept the devices matching the queries and
                                  print everything to stdout.
       -g | --grab                Grab the input.
                                  Stops everyone else from using the input.
                                  Only use this if you know what you're doing!

    redirect [query]              Redirect stdin to the device matching the query.
    to       [query]              Alias for 'redirect'
    systemd  exec-file [args...]  Install exec-file as a user service to systemd.
    list-devices                  List input devices
    how-to-type [--evtest] [str]  How to type the specified input?
    new       [name] [template]   Create a new app from a template.
                                  Use 'foresight new --list-templates' to see them.
    matches   [pattern...]        Match key combos in an evtest-format stream
                                   read from stdin, printing 'Matched <pattern>'
                                   when one is detected.
       --echo-events              Also echo the triggering event line.

    evtest   [device]             Like the evtest command: list devices and let
                                    the user select one, or open the specified
                                    device. Print device info and events in
                                    evtest text format to stdout.
       -g | --grab                Grab the input exclusively.

    live     [device]             Live view: compact, aligned event display with
                                     mouse accumulation, keyboard text, and hold
                                     durations. Uses terminal colors when interactive.
       -g | --grab                Grab the input exclusively.

    capture  [queries...]         Capture input events to a file. Events are
                                     buffered in memory and flushed to disk on idle.
       -g | --grab                Grab the input exclusively.
       --format <fmt>             Output format: "binary" (default) or "evtest".
       --naming <strategy>        File naming: "daily" (default), "hourly",
                                     "weekly", "monthly", "uptime", "system-uptime",
                                     "single-file", or "manual".
       --name <filename>          Custom output filename (requires --naming manual).

    replay   <file>               Replay captured events from a file to stdout.
                                     Auto-detects format (binary or evtest).

    help                 Print help.

  Queries are device names, paths (e.g. /dev/input/event1), or udev terms
  (e.g. "name=event0", "attr:device/name=My Mouse", "keyboard").

  How-to-type queries use modifier tags:
    <key>          Press keys together  (e.g. <ctrl+alt+x>)
    [key]          Release keys together (e.g. [ctrl+shift+left])
    <<key>>        Press keys in order
    [[key]]        Release keys in order
  Tags use -, +, or space as separators; plain text is typed literally.

  Example Usages:
    A Foresight pipeline app can remap keys directly, e.g. an x2y remapper:

      #include <linux/input-event-codes.h>
      import fs8;
      import fs8.mods;

      int main() {
          using namespace fs8;

          static constinit auto pipeline =
            context
            | io_manager
            | input_manager
            | intercept[keyboard | required | grab]
            | replace[KEY_X, KEY_Y]
            | output;

          pipeline();
      }

    $ foresight how-to-type --evtest "[Ctrl+Shift+Left]" \
      | foresight matches "[ctrl+shift+left]"
      Matched [ctrl+shift+left] at 0.000000

)TEXT");
}

void print_new_help() {
    std::println("{}", R"TEXT(Usage: foresight new [name] [template]
       foresight new --list-templates

Creates a new Foresight app from a template, in the current directory or at
the given path (whose filename becomes the app name).

Positionals (interchangeable):
    name                  The app name/path; e.g. "my-app" or "subdir/my-app".
    template              The template to use; e.g. "basic", "x2y", "auto-typer".
                          Omitted, defaults to "basic". An argument matching a
                          known template is treated as the template.

Options:
    -h | --help           Print this help.
    --list-templates      List the available templates.

)TEXT");
}

void print_input_devices_table() {
    struct Entry {
        std::string name;
        std::string location;
        std::string id;
    };

    constexpr std::size_t kInitialReserve = 16;

    std::vector<Entry> devices;
    devices.reserve(kInitialReserve);

    // NOLINTBEGIN(*-magic-numbers)
    constexpr std::size_t kDefaultNameWidth = 6;  // "Device"
    constexpr std::size_t kDefaultLocWidth  = 17; // "Physical Location"
    constexpr std::size_t kDefaultIdWidth   = 9;  // "Unique ID"
    // NOLINTEND(*-magic-numbers)

    // Minimum column widths (length of header texts)
    // NOLINTBEGIN(*-avoid-non-const-global-variables)
    std::size_t w_name = kDefaultNameWidth;
    std::size_t w_loc  = kDefaultLocWidth;
    std::size_t w_id   = kDefaultIdWidth;
    // NOLINTEND(*-avoid-non-const-global-variables)

    // Single pass: measure + store owned strings
    // Enumerate input devices through the query system (udev), then open
    // each evdev to read name/location/unique-id fields.
    for (auto pick : fs8::filter_devices(fs8::input)) {
        auto dev = fs8::to_evdev(pick);
        if (!dev.is_ok()) [[unlikely]] {
            continue;
        }
        auto const name_sv = dev.device_name();
        auto const loc_sv  = dev.physical_location();
        auto const id_sv   = dev.unique_identifier();

        w_name = std::max(w_name, name_sv.size());
        w_loc  = std::max(w_loc, loc_sv.size());
        w_id   = std::max(w_id, id_sv.size());

        devices.emplace_back(std::string{name_sv}, std::string{loc_sv}, std::string{id_sv});
    }

    if (devices.empty()) [[unlikely]] {
        std::println("No input devices found.");
        return;
    }

    // Header (still uses println; widths are constant here, so it compiles)
    std::println("{: <{}}  {: <{}}  {: <{}}", "Device", w_name, "Physical Location", w_loc, "Unique ID", w_id);

    // Separator
    std::println("{:-<{}}  {:-<{}}  {:-<{}}", "", w_name, "", w_loc, "", w_id);

    // Rows
    for (auto const& [name, location, id] : devices) {
        std::println("{: <{}}  {: <{}}  {: <{}}", name, w_name, location, w_loc, id, w_id);
    }

    // Footer
    std::println("\n{} device{} detected.", devices.size(), devices.size() == 1 ? "" : "s");
}
