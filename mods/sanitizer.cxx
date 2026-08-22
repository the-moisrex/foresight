// Created by moisrex on 8/21/26.

module;
#include <cmath>
#include <linux/input-event-codes.h>
module fs8.mods;

// The check_* member functions of basic_event_sanitizer are defined inline
// in the .ixx file because they are part of a class template.
//
// This translation unit exists so the module partition is registered in the
// build system. Any future non-template helpers can go here.
