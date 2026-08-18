# Command Line Interface

The `foresight` binary is a small CLI built around two complementary ideas:
**intercept** (turn kernel input events into a stream on stdout) and **redirect**
(turn a stream on stdin back into kernel input events). Between the two you can
chain any program that transforms `struct input_event`s — including apps written
with the Foresight library.

## Usage

```
Usage: foresight [options] [action]
  arguments:
    -h | --help          Print help.

  actions:
    intercept [files...] Intercept the files and print everything to stdout.
       -g | --grab       Grab the input.
                         Stops everyone else from using the input.
                         Only use this if you know what you're doing!

    redirect [files...]  Redirect stdin to the specified files.

    new [name] [template]
                         Create a new app from a template (interchangeable).
                         Run 'foresight new --list-templates' to see the
                         available templates.

    help                 Print help.
```

## Example usages

The classic pipeline — intercept a keyboard, transform it, and write it back:

```bash
keyboard=/dev/input/event1
foresight intercept -g $keyboard | x2y | foresight redirect $keyboard
```

The three stages in the pipeline are:

1. **`foresight intercept`** — reads the device and prints `input_event`s to stdout.
2. **the middle program** — reads from stdin, transforms the events, writes to stdout.
3. **`foresight redirect`** — reads stdin and writes the events back to the device.

The middle program can be written in any language; it just has to pass
`struct input_event` records through unmodified pipes. A minimal example in C:

```c
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

## Creating a new app from a template

`foresight new` scaffolds a new app (project) from an interchangeable template:

```bash
foresight new my-app
foresight new --list-templates   # see the available templates
```