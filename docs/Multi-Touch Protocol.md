# Multi-Touch (MT) Protocol Documentation

From [Linux Kernel Documentation](https://www.kernel.org/doc/Documentation/input/multi-touch-protocol.txt)

## Overview

The **Multi-Touch (MT) Protocol** enables kernel drivers to report detailed data from multiple contacts (e.g., fingers or objects) on touch-sensitive devices. This protocol supports both **multi-touch** and **multi-user** devices, providing a standardized way to communicate touch data to userspace. The protocol is divided into two types based on hardware capabilities:

- **Type A**: Handles **anonymous contacts** by sending raw data for all contacts.
- **Type B**: Supports **identifiable contacts** using event slots to track and update individual contacts.

**Key Purpose**: To allow precise reporting of multiple simultaneous contacts, enhancing support for advanced touch interactions.

## Protocol Usage

### General Structure
- **ABS_MT Events**: Contact details are sent as packets of **ABS_MT events** (e.g., position, size, pressure). Only these events are part of a contact packet.
- **Compatibility**: The MT protocol is built on top of the single-touch (ST) protocol, ensuring compatibility with existing ST applications, which ignore ABS_MT events.
- **Synchronization**:
  - **input_sync()**: Marks the end of a multi-touch transfer with a **SYN_REPORT** event, signaling the receiver to process accumulated events and prepare for new ones.
  - **Type A**: Uses **input_mt_sync()** to generate a **SYN_MT_REPORT** event, separating individual contact packets.
  - **Type B**: Uses **input_mt_slot(slot)** to generate an **ABS_MT_SLOT** event, indicating updates for a specific contact slot.

### Type A: Anonymous Contacts
- **Description**: For devices that cannot track individual contacts, the driver sends raw data for all contacts in arbitrary order.
- **Process**:
  - Send ABS_MT events for each contact (e.g., `ABS_MT_POSITION_X`, `ABS_MT_POSITION_Y`).
  - Call `input_mt_sync()` after each contact packet to generate a **SYN_MT_REPORT** event.
  - End the transfer with `input_sync()` to generate a **SYN_REPORT** event.
- **Key Note**: Event filtering and finger tracking are handled in **userspace** [3].

### Type B: Identifiable Contacts
- **Description**: For devices that track identifiable contacts, each contact is associated with a **slot** and updated using **ABS_MT_TRACKING_ID**.
- **Process**:
  - Start a packet with `input_mt_slot(slot)` to select a slot.
  - Update contact attributes (e.g., `ABS_MT_POSITION_X`, `ABS_MT_POSITION_Y`).
  - Use **ABS_MT_TRACKING_ID** to manage contact lifecycle:
    - **Non-negative ID**: Indicates an active contact.
    - **-1**: Marks an unused slot.
    - **New ID**: Signals a new contact.
    - **Removed ID**: Signals a contact has been lifted.
  - End the transfer with `input_sync()` to generate a **SYN_REPORT** event.
- **Key Note**: The receiver maintains the full state of each contact, as only changes are sent.

### Handling Limited Reporting
- Some devices track more contacts than they can report. Drivers should:
  - Assign one **Type B slot** per reported contact.
  - Update **ABS_MT_TRACKING_ID** when a contact’s identity changes.
  - Use **BTN_TOOL_*TAP** events to indicate the total number of tracked contacts, setting `use_count` to `false` in `input_mt_report_pointer_emulation()`.

## Protocol Examples

### Type A Example
For a two-contact touch:
```plaintext
ABS_MT_POSITION_X x[0]
ABS_MT_POSITION_Y y[0]
SYN_MT_REPORT
ABS_MT_POSITION_X x[1]
ABS_MT_POSITION_Y y[1]
SYN_MT_REPORT
SYN_REPORT
```
- **After moving a contact**: Same sequence, with updated coordinates.
- **After lifting the first contact**:
```plaintext
ABS_MT_POSITION_X x[1]
ABS_MT_POSITION_Y y[1]
SYN_MT_REPORT
SYN_REPORT
```
- **After lifting the second contact**:
```plaintext
SYN_MT_REPORT
SYN_REPORT
```
- **Note**: If `BTN_TOUCH` or `ABS_PRESSURE` is reported, the final `SYN_MT_REPORT` may be omitted to avoid dropped events.

### Type B Example
For a two-contact touch:
```plaintext
ABS_MT_SLOT 0
ABS_MT_TRACKING_ID 45
ABS_MT_POSITION_X x[0]
ABS_MT_POSITION_Y y[0]
ABS_MT_SLOT 1
ABS_MT_TRACKING_ID 46
ABS_MT_POSITION_X x[1]
ABS_MT_POSITION_Y y[1]
SYN_REPORT
```
- **After moving contact 45**:
```plaintext
ABS_MT_SLOT 0
ABS_MT_POSITION_X x[0]
SYN_REPORT
```
- **After lifting contact in slot 0**:
```plaintext
ABS_MT_TRACKING_ID -1
SYN_REPORT
```
- **After lifting the second contact**:
```plaintext
ABS_MT_SLOT 1
ABS_MT_TRACKING_ID -1
SYN_REPORT
```

## Event Semantics

### Core Events
- **ABS_MT_POSITION_X/Y**: Surface coordinates of the touch center.
- **ABS_MT_TOUCH_MAJOR**: Length of the major axis of the contact area (in surface units, max = √(X² + Y²)).
- **ABS_MT_TOUCH_MINOR**: Length of the minor axis (omit for circular contacts).

### Additional Events
- **ABS_MT_WIDTH_MAJOR/MINOR**: Size of the approaching tool (e.g., finger or pen). Omit MINOR for circular tools.
- **ABS_MT_PRESSURE**: Pressure on the contact area (alternative to TOUCH/WIDTH for pressure-based devices).
- **ABS_MT_DISTANCE**: Distance between the contact and surface (0 = touching, >0 = hovering).
- **ABS_MT_ORIENTATION**: Orientation of the touch ellipse (signed quarter-revolution, 0 = aligned with Y-axis, max = aligned with X-axis).
- **ABS_MT_TOOL_X/Y**: Coordinates of the approaching tool (omit if indistinguishable from touch point).
- **ABS_MT_TOOL_TYPE**: Type of tool (e.g., `MT_TOOL_FINGER`, `MT_TOOL_PEN`, `MT_TOOL_PALM`). Handled by input core for Type B devices.
- **ABS_MT_BLOB_ID**: Groups points into an arbitrary contact shape (Type A only, low-level grouping).
- **ABS_MT_TRACKING_ID**: Tracks a contact’s lifecycle (Type B only, handled by input core).

### Geometrical Interpretation
- **TOUCH vs. WIDTH**: 
  - **TOUCH**: Represents the contact area (e.g., finger touching glass).
  - **WIDTH**: Represents the tool’s size (e.g., finger’s perimeter).
  - **Ratio**: `ABS_MT_TOUCH_MAJOR / ABS_MT_WIDTH_MAJOR` approximates pressure.
- **Orientation**: Describes the touch ellipse’s rotation. Range [0, 1] for partial orientation support.
- **Tool vs. Touch Position**: If both are provided, the tool’s major axis points toward the touch point.

## Event Computation
For devices with limited data:
- **Rectangular Contacts**:
  ```plaintext
  ABS_MT_TOUCH_MAJOR = max(X, Y)
  ABS_MT_TOUCH_MINOR = min(X, Y)
  ABS_MT_ORIENTATION = bool(X > Y)  # Range [0, 1]
  ```
- **Win8 Devices** (with touch (T) and centroid (C) coordinates):
  ```plaintext
  ABS_MT_POSITION_X = T_X
  ABS_MT_POSITION_Y = T_Y
  ABS_MT_TOOL_X = C_X
  ABS_MT_TOOL_Y = C_Y
  ABS_MT_TOUCH_MAJOR = min(X, Y)
  ABS_MT_WIDTH_MAJOR = min(X, Y) + distance(T, C)
  ABS_MT_WIDTH_MINOR = min(X, Y)
  ```

## Finger Tracking
- **Type A**: Userspace handles tracking [3].
- **Type B**: The kernel assigns **ABS_MT_TRACKING_ID** to each contact, solving a **Euclidean Bipartite Matching** problem to track contacts across events [5].

## Gestures
- **TOUCH/WIDTH**: Can approximate pressure or distinguish finger types (e.g., index vs. thumb).
- **MINOR/ORIENTATION**: Enable detection of sweeping vs. pointing fingers or twisting motions.

## Notes
- **Compatibility**: Finger packets must not be interpreted as single-touch events.
- **Type A Filtering**: All finger data bypasses input filtering, as subsequent events refer to different fingers.
- **Example Drivers**:
  - **Type A**: `bcm5974`
  - **Type B**: `hid-egalax`
- **References**:
  - [1] Tilt modeling using `TOOL_X - POSITION_X`.
  - [3] `mtdev` project: http://bitmath.org/code/mtdev/.
  - [4] Event computation section.
  - [5] Finger tracking section.