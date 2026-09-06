# X to Y example

This example converts the key `x` on your keyboard to `y` when you press it on your keyboard.


Usage:

```bash
./x2y
```


```
  A Foresight pipeline app that remaps x to y:

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
```

### Legacy: the intercept–transform–redirect pipeline

The same result can also be achieved by piping through a standalone filter:

```bash
keyboard=/dev/input/event1
foresight intercept $keyboard | x2y | foresight redirect $keyboard
```