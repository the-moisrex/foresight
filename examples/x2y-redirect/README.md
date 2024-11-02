# X to Y example

This example converts the key `x` on your keyboard to `y` when you press it on your keyboard.


Usage:

```bash
keyboard=/dev/input/event1
foresight intercept $keyboard | x2y | foresight redirect $keyboard 
```


```
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