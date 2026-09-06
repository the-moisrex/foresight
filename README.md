# Foresight (Input Manager)

![Foresight Icon](./assets/icon.svg)

Smart Input, which intercepts your keystrokes and will help you type less, faster, and adds foresight.

Even though we're trying to start from making the keyboard smarter, but the goal of the project is to help
the computer know what the user wants and just help them do it and also give the user a situational awareness
of the system they're using.

**Currently, only Linux is supported**; the project is still **a work in progress**.


## Goals

- Programming input devices to match your needs and wants.
- Automation
- Quick Actions
- Creating models of your habits to predict the correct actions
- A personal assistance that lets you focus and gives you the right information at the right time
- Connecting related tools to create useful pipelines of actions
- Adding features to software that don't support them yet
- Do all of these things while considering "Privacy"
- To be used as a library as well

I understand these are far-fetched goals, but we can't get there if we don't try.

## Features

| Feature Name                | Description                                                              | Status |
|-----------------------------|--------------------------------------------------------------------------|--------|
| Intercept                   | Intercept the input events and print them                                | ✅      |
| \> Redirect                 | Read the input events from stdin, and write them to the event files      | ✅      |
| \> Common interceptors      | Common interceptors for use with `intercept \| ... \| redirect`          | ✅      |
| \> Ignore big jumps         | Ignore big mouse jumps                                                   | ✅      |
| \> Ignore Starting moves    | Ignore starting moves of a mouse                                         | ✅      |
| \> Ignore fast repeats      | Ignore fast repeats of input events                                      | ✅      |
| \> Ignore events completely | Ignore some events completely (mostly used conditionally)                | ✅      |
| \> Ignore adjacent repeats  | Ignore duplicated events (mostly happen by mistake)                      | ✅      |
| \> On Held Down             | Fire actions while a key is held down (ignoring its auto-repeats)        | ✅      |
| \> Ignore repeats of keys   | Ignore the auto-repeat events (value 2) of specific keys                 | ✅      |
| \> Gate keys while held     | Let a decider swallow a held key/chord or emit it normally (`held[key, decider]`) | ✅      |
| \> ABS to REL               | Convert absolute events (drawing tablets) to relative events (mouse)     | ✅      |
| \> Add Scrollbar            | Convert mouse movements into scroll wheel (conditionally)                | ✅      |
| \> Replace events           | Convert events into other events.                                        | ✅      |
| \> Route events             | Route events into different output devices                               | ✅      |
| Device Info                 | List kernel event devices (like evtest)                                  | ❌      |
| String Matching             | Figure out what the using is typing/editing right now                    | ❌      |
| Regular Expression          | Use RegExp to find and replace selected/typing strings                   | ❌      |
| Auto-complete               | Auto complete the user input                                             | ❌      |
| Auto-correct                | Auto correct                                                             | ❌      |
| Number Scroll               | Shortcut + Scroll-wheel to update the number/date/color/...              | ❌      |
| Shell cmds                  | Run shell commands (for eg: `$ whoami <ctrl-enter>`)                     | ✅      |
| Software Detection          | Detect which app the user's in, so we can use custom commands            | ❌      |
| Clang Tools                 | Use clang tools to rename variables, show assembly, ...                  | ❌      |
| Config File                 | Custom config file to customize by the user                              | ❌      |
| GUI                         | Graphical User Interface for ease of use                                 | ❌      |
| Remote Input                | Remotely control the keyboard and mouse (sharing keyboard & mouse)       | ❌      |
| Auto translate              | Translate the input while typing                                         | ❌      |
| Keyboard re-mapping         | Remap any input to another or a combinations of others                   | ✅      |
| Unicode Support             | Emojis, ...                                                              | ❌      |
| Macros                      | Register a sequence of keys, and re-run them as needed                   | ❌      |
| Modes and Layers            | Like vim modes                                                           | ❌      |
| Audio                       | Add audio support for when events happen, we can configure special audio | ❌      |
| Network Packet Matching     | Fire events on network packets (use case: beep on loading ads)           | ❌      |
| Habits                      | Machine-Learning based event-habit calculator                            | ❌      |
| \> Anomaly Notifier         | Machine-Learning based anomaly notifier based on habits                  | ❌      |

## Documentation

- **Guides site**: [the-moisrex.github.io/foresight](https://the-moisrex.github.io/foresight/) — getting started, CLI usage, and how to write your own apps.
- **API reference**: [the-moisrex.github.io/foresight/api](https://the-moisrex.github.io/foresight/api/) — generated from the source with Doxygen.
- **Sources**: the guides live in [`docs/`](./docs/); the API reference is generated from the `///` comments in the `.ixx` modules.

## Help & Usage

Examples like pen2mice will convert Drawing Tablet events into Mouse Events plus some other features like adding scroll wheel to it.
Foresight is designed in a way so ***it can be used as a library***.

But this foresight binary itself can be used like this:

```
Usage: foresight [options] [action]
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

```

The same x2y result can also be achieved with the legacy pipe-based approach:

````bash
    $ keyboard=/dev/input/event1
    $ foresight intercept -g $keyboard | x2y | foresight redirect $keyboard
      -----------------------------   ---   ----------------------------
        |                              |      |
        |                              |      |
        |                              |      |
        |                              |      |
        `----> Intercept the input     |      `---> put the modified input back
                                      /
                                     /
                                    /
             -----------------------
            /
    $ cat x2y.c  # you can do it with any programming language you like
      #include <stdio.h>
      #include <stdlib.h>
      #include <linux/input.h>

      int main(void) {
          setbuf(stdin, NULL);   // disable stdin buffer
          setbuf(stdout, NULL);  // disable stdout buffer

          struct input_event event;

          // read from the input
          while (fread(&event, sizeof(event), 1, stdin) == 1) {

              // modify the input however you like
              // here, we change "x" to "y"
              if (event.type == EV_KEY && event.code == KEY_X)
                  event.code = KEY_Y;

              // write it to stdout
              fwrite(&event, sizeof(event), 1, stdout);
          }
      }
````
---
Partially inspired by these projects:

- [Interception Tools](https://gitlab.com/interception/linux/tools)
- [keydogger](https://github.com/jarusll/keydogger/)
- [Google Teller](https://github.com/berthubert/googerteller)
- libevdev
- [libxkbcommon](https://github.com/xkbcommon/libxkbcommon)
