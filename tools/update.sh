#!/bin/bash

curdir=$(dirname "$0")

"$curdir/gen-keys.awk" /usr/include/linux/input-event-codes.h > "$curdir/../evdev/inputs-event-codes.ixx"
