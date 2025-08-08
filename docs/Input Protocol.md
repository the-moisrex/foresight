# Input Protocol Documentation

from [Linux Event Codes Docs](https://www.kernel.org/doc/Documentation/input/event-codes.txt)

## Overview

The **Input Protocol** is a **stateful protocol** used by the Linux input subsystem to communicate input device values
to userspace. It defines a set of **event types** and **codes** to represent hardware events, such as key presses, mouse
movements, or touch interactions. Each hardware event generates multiple **input events**, each containing a **type**, *
*code**, and **value** representing a single data change.

**Key Features**:

- **Stateful**: Events are emitted only when values change. The Linux input subsystem maintains the state, so drivers do
  not need to track it.
- **Event Separation**: The **EV_SYN** event type groups input events into packets representing changes at the same
  moment.
- **State Access**: Userspace can query the current state using **EVIOCG*** ioctls (`linux/input.h`) or via sysfs at
  `class/input/event*/device/capabilities/` and `class/input/event*/device/properties`.

## Event Types

Event types group related codes under a logical input construct. Each type has specific codes for generating events.

| **Event Type**   | **Description**                                                                |
|------------------|--------------------------------------------------------------------------------|
| **EV_SYN**       | Separates events into packets (e.g., for multitouch or time-based separation). |
| **EV_KEY**       | Represents state changes for keyboards, buttons, or key-like devices.          |
| **EV_REL**       | Reports relative axis changes (e.g., mouse movement).                          |
| **EV_ABS**       | Reports absolute axis changes (e.g., touchscreen coordinates).                 |
| **EV_MSC**       | Handles miscellaneous input/output data not fitting other types.               |
| **EV_SW**        | Represents binary state switches (e.g., laptop lid status).                    |
| **EV_LED**       | Controls LEDs on devices (on/off states).                                      |
| **EV_SND**       | Sends sound commands to simple sound output devices.                           |
| **EV_REP**       | Manages autorepeating for devices.                                             |
| **EV_FF**        | Sends force feedback commands to devices.                                      |
| **EV_PWR**       | Used for power button/switch input (usage TBD).                                |
| **EV_FF_STATUS** | Reports force feedback device status.                                          |

## Event Codes

Event codes define the specific type of event within each event type.

### EV_SYN

- **Purpose**: Marks and separates event packets.
- **Codes**:
    - **SYN_REPORT**: Synchronizes events into packets (e.g., mouse movement sets `REL_X`, `REL_Y`, followed by
      `SYN_REPORT`).
    - **SYN_CONFIG**: TBD.
    - **SYN_MT_REPORT**: Separates multitouch events (see `multi-touch-protocol.txt`).
    - **SYN_DROPPED**: Indicates a buffer overrun in the evdev client’s event queue. Clients should ignore events until
      the next `SYN_REPORT` and query the device state using **EVIOCG*** ioctls.

### EV_KEY

- **Format**: `KEY_<name>` (e.g., `KEY_A` for the 'A' key) or `BTN_<name>` (e.g., `BTN_LEFT` for a mouse button).
- **Values**:
    - `1`: Key pressed.
    - `0`: Key released.
    - `2`: Key repeated (if supported by hardware).
- **Special Codes**:
    - **BTN_TOOL_<name>** (e.g., `BTN_TOOL_FINGER`, `BTN_TOOL_PEN`): Indicates the tool used (finger, pen, etc.) on
      trackpads, tablets, or touchscreens. Set to `1` when the tool is active, `0` when not.
    - **BTN_TOUCH**: Indicates meaningful physical contact (e.g., touch on a touchscreen). May be conditioned by
      implementation-specific thresholds (e.g., pressure). Can combine with `BTN_TOOL_<name>` (e.g., `BTN_TOOL_PEN=1`,
      `BTN_TOUCH=0` for a hovering pen).
    - **BTN_TOOL_FINGER**, **BTN_TOOL_DOUBLETAP**, **BTN_TOOL_TRIPLETAP**, **BTN_TOOL_QUADTAP**: Denote 1, 2, 3, or 4
      finger interactions on trackpads/touchscreens. Only one should be `1` per synchronization frame. Use
      `input_mt_report_finger_count()` for multitouch drivers (see `multi-touch-protocol.txt`).

**Notes**:

- `BTN_TOUCH` must be the first code in a synchronization frame for legacy `mousedev` emulation.
- Historically, devices with `BTN_TOOL_FINGER` and `BTN_TOUCH` were treated as touchpads, while those without
  `BTN_TOOL_FINGER` were touchscreens. For compatibility, follow this convention, but future devices should use *
  *EVIOCGPROP** to convey device type.

### EV_REL

- **Purpose**: Reports relative changes (e.g., mouse movement).
- **Special Codes**:
    - **REL_WHEEL**, **REL_HWHEEL**: Vertical and horizontal scroll wheel events.

### EV_ABS

- **Purpose**: Reports absolute changes (e.g., touchscreen coordinates).
- **Special Codes**:
    - **ABS_DISTANCE**: Distance of a tool from the surface (emitted when `BTN_TOUCH=0`, i.e., hovering). Use `ABS_Z`
      for devices free in 3D space. `BTN_TOOL_<name>` must reflect tool proximity (`1` when in range, `0` when not).
    - **ABS_MT_<name>**: Multitouch events (see `multi-touch-protocol.txt`).

### EV_SW

- **Purpose**: Reports binary switch states (e.g., `SW_LID` for laptop lid status).
- **Requirement**: Drivers must report the current switch state on device binding or resume to ensure synchronization
  with userspace. Duplicate states after resume are filtered by the input subsystem.

### EV_MSC

- **Purpose**: Handles miscellaneous input/output events.
- **Special Code**:
    - **MSC_TIMESTAMP**: Reports microseconds since the last reset (uint32, may wrap). Time differences are reliable
      over hours, but resets to zero may occur. Drivers must not report this if unsupported by hardware.

### EV_LED

- **Purpose**: Sets and queries LED states on devices.

### EV_REP

- **Purpose**: Configures autorepeating behavior.

### EV_SND

- **Purpose**: Sends sound commands to simple sound output devices.

### EV_FF

- **Purpose**: Sends force feedback commands to devices.

### EV_PWR

- **Purpose**: Handles power button/switch input (usage TBD).

## Device Properties

Device properties provide additional context for userspace to configure devices beyond event types.

| **Property**                 | **Description**                                                                                                                                              |
|------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **INPUT_PROP_DIRECT**        | Coordinates map directly to screen coordinates (e.g., touchscreens, tablets). Non-direct devices (e.g., touchpads, mice) require transformations.            |
| **INPUT_PROP_POINTER**       | Device requires an on-screen pointer (e.g., touchpads, mice). Non-pointer devices (e.g., touchscreens) do not.                                               |
| **INPUT_PROP_BUTTONPAD**     | Touchpads with buttons beneath the surface (e.g., clickpads). Historically encoded in the `bcm5974` driver’s version field as “integrated button.”           |
| **INPUT_PROP_SEMI_MT**       | Touchpads detecting multiple contacts without individual positions (e.g., bounding box or recent touches). If unset, the device is a true multitouch device. |
| **INPUT_PROP_TOPBUTTONPAD**  | Laptops with tracksticks and touchpad-based buttons (e.g., Lenovo *40 series). Userspace emulates buttons; the kernel treats these as standard buttonpads.   |
| **INPUT_PROP_ACCELEROMETER** | Device axes (`ABS_X`, `ABS_Y`, `ABS_Z`) represent accelerometer data. Regular and accelerometer axes must not mix on the same event node.                    |

**Note**: If neither `INPUT_PROP_DIRECT` nor `INPUT_PROP_POINTER` is set, the device type is deduced from event types.

## Guidelines for Devices

### Mice

- **Events**: `REL_X`, `REL_Y` for movement; `BTN_LEFT` for primary button; `BTN_MIDDLE`, `BTN_RIGHT`, `BTN_4`, etc.,
  for additional buttons; `REL_WHEEL`, `REL_HWHEEL` for scroll wheels.

### Touchscreens

- **Events**: `ABS_X`, `ABS_Y` for touch location; `BTN_TOUCH` for active touch. Avoid `BTN_MOUSE`, `BTN_LEFT`, etc.,
  for touch events.
- **Property**: Set `INPUT_PROP_DIRECT`.
- **Tool Events**: Use `BTN_TOOL_<name>` where applicable.

### Trackpads

- **Legacy**: Report `REL_X`, `REL_Y` like mice if only relative data is available.
- **Modern**: Report `ABS_X`, `ABS_Y` for touch location; `BTN_TOUCH` for active touch; `BTN_TOOL_<name>` for
  multi-finger counts.
- **Property**: Set `INPUT_PROP_POINTER`.

### Tablets

- **Events**: `BTN_TOOL_<name>` for active tools; `ABS_X`, `ABS_Y` for tool location; `BTN_TOUCH` for contact;
  `BTN_STYLUS`, `BTN_STYLUS2` for tool buttons; `BTN_0`, `BTN_1`, etc., for generic tablet buttons.
- **Properties**: Set `INPUT_PROP_DIRECT` and `INPUT_PROP_POINTER`.