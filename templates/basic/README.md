# {{name}}

A Foresight app created from the `basic` template: it grabs your keyboard and
replaces every `x` with `y` as you type.

## Build

```bash
cmake --preset release
cmake --build --preset release
```

## Usage

```bash
./{{name}} [keyboard_device]
./{{name}} -h | --help
```

Positionals:

- `keyboard_device`: The USB keyboard device query (defaults to `USB Keyboard`).
- `-h | --help`: Print help.
