# Pen to Mice

A utility that converts pen tablet input into mouse-like actions, enabling drawing tablets to function as mice with extended capabilities. This application intercepts input events from a pen tablet and transforms them into mouse movements and clicks, with additional features for gesture control and mode switching.

## Features

- **Pen to Mouse Conversion**: Converts absolute pen tablet movements into relative mouse movements
- **Gesture Detection**: Detects swipes for navigation commands
- **Multi-Mode Operation**: Supports normal and express modes
- **Custom Key Bindings**: Maps specific keys to different functions in express mode
- **Mouse Enhancement**: Adds functionality like preventing accidental clicks
- **Caps Lock Integration**: Uses Caps Lock as a modifier key for special functions

## Usage

```bash
./pen2mice [pen_device] [usb_keyboard_device]
```

### Device Arguments
- `pen`: Specifies the drawing tablet/pen device
- `usb keyboard`: Specifies the USB keyboard device to use

### Controls

#### Basic Mouse Operations
- **Left Click**: Normal left click on tablet
- **Right Click**: Hold pen with right mouse button
- **Middle Click**: Pen middle button click

#### Special Functions
- **Caps Lock + Pen Left Click**: Triggers right click (for long press actions)
- **Caps Lock + Pen Middle Click + Triple Click**: Emits `Cmd+Tab` (Mac) or `Ctrl+Tab` (Linux)
- **Caps Lock + Pen Middle Click + Left Click + Swipe**: Directional navigation
  - Swipe Right: `Ctrl+Cmd+Right`
  - Swipe Left: `Ctrl+Cmd+Left`
  - Swipe Up: `Ctrl+Cmd+Up`
  - Swipe Down: `Ctrl+Cmd+Down`

#### Express Mode
Activate Express Mode by holding **Caps Lock**. In this mode:
- `D` key → `Cmd+Ctrl+Right` (move right)
- `A` key → `Cmd+Ctrl+Left` (move left)
- `W` key → `Cmd+Ctrl+Up` (move up)
- `S` key → `Cmd+Ctrl+Down` (move down)
- `E` key → `Cmd+Tab` (switch applications)
- `ESC` key → Exit Express Mode

#### Mode Switching
- **Caps Lock**: Toggle between normal and express mode
- **Caps Lock + Left Click**: Ignore subsequent key presses during the action
- **Caps Lock + Left Shift** or **ESC**: Exit and restart the application

#### Additional Features
- **Ignore Fast Left Clicks**: Prevents unintended rapid clicks
- **Right Click Jump Fix**: Prevents cursor jumps during right-click drag
- **Scroll Enhancement**: When holding middle button or Caps Lock, button up emits scroll events
- **Mouse Travel Limit**: With Caps Lock, limits mouse travel distance; long release triggers button up

