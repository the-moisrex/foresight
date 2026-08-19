# Debounce Mouse Clicks

Fixes a faulty mouse that occasionally double clicks. A press (and its matching
release) that lands within the debounce window of the previous press of the same
button is dropped, while real clicks pass through untouched.

## Usage

```bash
./debounce-mouse-clicks [mouse_device] [-t <time>]
./debounce-mouse-clicks -h | --help
```

### Arguments
- `mouse_device`: The mouse device query (defaults to `Mouse`)
- `-t | --time <time>`: The debounce window, e.g. `50ms`, `1s`, `500us` (default: `30ms`)
- `-h | --help`: Print help