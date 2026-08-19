# Debounce

Drops events that arrive within a debounce window of a previous event of the
same code. By default it debounces the mouse buttons (fixing faulty mice that
occasionally double click), but it can be pointed at any event code and mode.

## Usage

```bash
./debounce [device] [-t <time>] [-m <mode>] [-c <codes>]
./debounce -h | --help
```

### Arguments
- `device`: The input device query (defaults to `Mouse`)
- `-t | --time <time>`: The debounce window, e.g. `50ms`, `1s`, `500us` (default: `30ms`)
- `-m | --mode <mode>`: `click` (default) drops a fast second press and its
  release; `event` drops any event within the window
- `-c | --codes <codes>`: Comma-separated event codes, e.g. `BTN_LEFT,BTN_RIGHT`
  or `EV_ABS:ABS_X` (default: `BTN_LEFT,BTN_RIGHT,BTN_MIDDLE`)
- `-h | --help`: Print help

## Examples

Debounce a faulty mouse's double clicks:

```bash
./debounce my-mouse --time 50ms
```

Debounce bouncing keyboard keys:

```bash
./debounce keyboard --mode click --codes KEY_A,KEY_D,KEY_F,KEY_J
```

Quiet a noisy tablet axis (only settled changes pass):

```bash
./debounce tablet --mode event --codes EV_ABS:ABS_X,EV_ABS:ABS_Y
```

Drop double-fired scroll-wheel steps:

```bash
./debounce mouse --mode event --codes EV_REL:REL_WHEEL
```
