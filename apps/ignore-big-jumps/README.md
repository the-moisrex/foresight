# Ignore big mouse jumps

This helps you to ignore big jumps of a faulty mouse.

Usage:

```bash
mouse=/dev/input/event1
foresight intercept $mouse | ignore-big-jumps | foresight redirect $mouse
```
