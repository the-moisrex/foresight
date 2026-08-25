# {{name}}

A Foresight app created from the `auto-typer` template: it watches a keyboard
for typed patterns and auto-completes them into longer strings.

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

The template completes the pattern `@test` into `nice`; edit `{{name}}.cpp` to
change the pattern and the completion.
