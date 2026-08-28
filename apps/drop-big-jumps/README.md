# Drop big mouse jumps

This helps you to drop big jumps of a faulty mouse.

Usage:

```bash
mouse=/dev/input/event1
foresight intercept $mouse | drop-big-jumps | foresight redirect $mouse
```
