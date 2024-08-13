# Foresight Smart Keyboard

Smart Keyboard which intercepts your keystrokes and will help you type less, faster, and adds foresight.

## Features

| Feature Name        | Description                                                         | Status |
|---------------------|---------------------------------------------------------------------|--------|
| Intercept           | Intercept the input events and print them                           | ✅      |
| Redirect            | Read the input events from stdin, and write them to the event files | ❌      |
| Device Info         | List kernel event devices (like evtest)                             | ❌      |
| String Matching     | Figure out what the using is typing/editing right now               | ❌      |
| Regular Expression  | Use RegExp to find and replace selected/typing strings              | ❌      |
| Auto-complete       | Auto complete the user input                                        | ❌      |
| Auto-correct        | Auto correct                                                        | ❌      |
| Number Scroll       | Shortcut + Scroll-wheel to update the number/date/color/...         | ❌      |
| Shell cmds          | Run shell commands                                                  | ❌      |
| Software Detection  | Detect which app the user's in, so we can use custom commands       | ❌      |
| Clang Tools         | Use clang tools to rename variables, show assembly, ...             | ❌      |
| Config File         | Custom config file to customize by the user                         | ❌      |
| GUI                 | Graphical User Interface for ease of use                            | ❌      |
| Remote Input        | Remotely control the keyboard and mouse (sharing keyboard & mouse)  | ❌      |
| Auto translate      | Translate the input while typing                                    | ❌      |
| Keyboard re-mapping | Remap any input to another or a combinations of others              | ❌      |
| Unicode Support     | Emojis, ...                                                         | ❌      |
| Macros              | Register a sequence of keys, and re-run them as needed              | ❌      |
| Modes and Layers    | Like vim modes                                                      | ❌      |


## Help & Usage

```c++
Usage: foresight [options] [action]
  arguments:
    -h | --help          Print help.

  actions:
    intercept [files...] Intercept the files and print everything to stdout.
       -g | --grab       Grab the input.
                         Stops everyone else from using the input.
                         Only use this if you know what you're doing!

    redirect [files...]  Redirect stdin to the specified files.

    help                 Print help.

  Example Usages:
    $ keyboard=/dev/input/event1
    $ foresight intercept $keyboard | x2y | foresight redirect $keyboard
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
```

---
Inspired by these projects:

- [Interception Tools](https://gitlab.com/interception/linux/tools)
- [keydogger](https://github.com/jarusll/keydogger/)
- libevdev
