# {{name}}

A Foresight app created from the `x2y` template: a tiny C filter that reads
`input_event`s from stdin, modifies them, and writes them to stdout.

## Build

```bash
cmake --preset release
cmake --build --preset release
```

## Usage

Put it in the middle of a Foresight pipeline:

```bash
foresight intercept $keyboard | ./{{name}} | foresight redirect $keyboard
```

The filter here changes every `x` into `y`; edit `{{name}}.c` to do anything
you like.
