#!/bin/bash

curdir=$(dirname "$0")

"$curdir/gen-keys.awk" /usr/include/linux/input-event-codes.h | clang-format - > "$curdir/../devices/inputs-event-codes.ixx"

