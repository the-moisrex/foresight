# New Scrolling

Use `Meta` + `Ctrl` + `Mouse move` as scrollbars


Usage:

```bash
mouse=/dev/input/event29
foresight intercept $mouse | add-scroll | foresight redirect $mouse
```
