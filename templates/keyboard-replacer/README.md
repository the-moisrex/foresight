# {{name}}

Grabs a keyboard and swaps CapsLock with Escape.

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
