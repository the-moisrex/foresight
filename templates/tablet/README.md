# {{name}}

Converts a drawing tablet's absolute input into relative mouse movements.

## Build

```bash
cmake --preset release
cmake --build --preset release
```

## Usage

```bash
./{{name}} [tablet_device] [keyboard_device]
./{{name}} -h | --help
```

Positionals:

- `tablet_device`: The drawing tablet device query.
- `keyboard_device`: Optional keyboard for mode switching.
