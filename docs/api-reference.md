# API Reference

The complete C++ API reference is generated from the source with
[Doxygen](https://www.doxygen.nl/) and published alongside this site.

## Published docs

**<https://the-moisrex.github.io/foresight/api/>**

The reference is organized by:

- **Modules** — the `fs8.*` C++ module pages (e.g. `fs8.mods`, `fs8.event`, `fs8.context`).
- **Classes** — `basic_context`, `event_type`, the `basic_*` mods, and more.
- **Files** — the `.ixx` module sources.

## Building it locally

Via CMake (requires Doxygen):

```bash
cmake --build build --target docs
```

The HTML is written to `build/docs/doxygen/html`. Or run Doxygen directly:

```bash
FS8_DOC_OUTPUT=build/docs/doxygen doxygen Doxyfile
```

## Contributing

The reference is generated straight from the `///` doc comments in the
`.ixx` module files. When you add or change library code, update the
doc comments next to the declarations — the reference updates automatically
on the next build.