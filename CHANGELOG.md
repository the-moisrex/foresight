# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Features
- Add `--version` flag to CLI
- Add semi-automated release process with GitHub Actions
- Add changelog generation with git-cliff
- Embed version number in build system

### Bug Fixes
- Fix pressed mod
- Fix left click track position

### Documentation
- Add release process documentation

### Performance
- Benchmark mod improvements

### Refactoring
- Event diagnostics improvements
- Sanitizer enhancements

### Testing
- Add comprehensive test suite

---

## [v1.0.0] - 2024-06-24

Initial release with core functionality:

### Features
- `foresight intercept` - Intercept input devices and print events
- `foresight redirect` - Redirect stdin to input devices
- `foresight systemd` - Install as systemd service
- `foresight list-devices` - List available input devices
- `foresight how-to-type` - Show how to type key combinations
- `foresight new` - Create new apps from templates
- `foresight matches` - Match key combos in evtest streams

### Core Pipeline
- Type-safe, compile-time pipeline architecture
- Modular design with composable mods
- Support for evdev, udev, and xkbcommon
- Real-time input processing

### Documentation
- MkDocs Material documentation site
- Doxygen API reference
- Example applications

---

[Unreleased]: https://github.com/the-moisrex/foresight/compare/v1.0.0...HEAD
[v1.0.0]: https://github.com/the-moisrex/foresight/releases/tag/v1.0.0
