#!/usr/bin/env python3
"""Generate bucklespring_data.ixx and bucklespring_data.cxx from WAV analysis.

The .ixx file contains the struct definition and extern const array declarations.
The .cxx file is the module implementation unit that defines the arrays.
"""

import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Known key analysis data extracted from analysis_results.txt
# Format: keycode_hex -> (name, press_data, release_data)
#
# press_data / release_data =
#   (peak_dbfs, decay_tau_ms, res1_freq, res1_q, res2_freq, res2_q,
#    contact_ms, snap_ms)
#
# contact_ms = time of first energy peak (keycap contact)
# snap_ms    = time of buckle snap (the loud event)
# ---------------------------------------------------------------------------

MEASURED_KEYS = {
    #                     peak   tau   res1_f  res1_q  res2_f  res2_q  cont   snap
    0x01: ("ESC",       (-5.68, 3.98,  9076.0, 54.0,  2784.0, 8.0,   0.045, 0.998),
                        (-9.47, 2.96,  6450.0, 12.0,  2934.0, 8.0,   0.045, 0.500)),
    0x02: ("1_KEY",     (-1.57, 9.76,  1002.0, 5.0,   501.0,  5.0,   0.272, 2.834),
                        (-2.34, 4.46,  1503.0, 1.7,   1421.0, 3.0,   0.272, 1.000)),
    0x0e: ("BACKSPACE", (-4.30, 8.80,  1448.0, 9.0,   835.0,  4.0,   0.204, 1.769),
                        (-2.52, 4.41,  1336.0, 2.4,   2728.0, 9.8,   0.204, 0.800)),
    0x0f: ("TAB",       (-6.01, 6.88,  1058.0, 3.0,   1670.0, 5.0,   0.227, 1.882),
                        (-6.24, 2.66,  3010.0, 20.0,  2005.0, 5.0,   0.227, 0.800)),
    0x1c: ("ENTER",     (-3.38, 5.22,  1392.0, 3.0,   2394.0, 5.0,   0.045, 1.066),
                        (-2.68, 3.65,  1392.0, 3.0,   2394.0, 5.0,   0.045, 0.600)),
    0x1e: ("A",         (-3.22, 4.26,  1169.0, 5.0,   835.0,  5.0,   0.272, 1.610),
                        (-3.90, 3.62,  1169.0, 5.0,   835.0,  5.0,   0.272, 0.700)),
    0x2a: ("LSHIFT",    (-3.60, 7.38,  1169.0, 10.0,  947.0,  8.0,   0.045, 1.519),
                        (-1.79, 3.70,  1169.0, 10.0,  947.0,  8.0,   0.045, 0.700)),
    0x2c: ("Z",         ( 0.00, 5.44,  1225.0, 4.0,   6849.0, 31.0,  0.272, 1.519),
                        (-5.83, 3.40,  2151.0, 20.0,  1225.0, 4.0,   0.272, 0.800)),
    0x33: ("COMMA",     (-0.58, 6.28,  1114.0, 7.0,   11972.0,108.0, 0.045, 1.315),
                        (-5.52, 4.10,  2545.0, 20.0,  1776.0, 18.0,  0.045, 0.800)),
    0x34: ("DOT",       (-3.25, 4.26,  835.0,  4.0,   1670.0, 7.0,   0.181, 1.406),
                        (-3.17, 3.40,  1776.0, 18.0,  835.0,  4.0,   0.181, 0.800)),
    0x39: ("SPACE",     (-4.05, 4.46,  2283.0, 4.0,   1169.0, 5.0,   0.045, 0.998),
                        (-1.70, 3.50,  6010.0, 12.0,  3007.0, 8.0,   0.045, 0.600)),
    0x3b: ("F1",        (-0.27, 2.03,  1169.0, 5.0,   2116.0, 13.0,  0.272, 1.678),
                        (-7.43, 2.80,  4242.0, 20.0,  2016.0, 20.0,  0.272, 0.800)),
}

# Default snap_bw_ms for all keys
DEFAULT_SNAP_BW_MS = 0.8

# Key categories for interpolation of unmeasured keys
CATEGORIES = {
    "alpha": {
        "res1_freq": 1100.0, "res1_q": 5.0,
        "res2_freq": 900.0, "res2_q": 5.0,
        "decay_ms": 5.0, "peak_dbfs": -3.0,
        "contact_ms": 0.15, "snap_ms": 1.5,
    },
    "number": {
        "res1_freq": 1000.0, "res1_q": 5.0,
        "res2_freq": 600.0, "res2_q": 5.0,
        "decay_ms": 7.0, "peak_dbfs": -2.0,
        "contact_ms": 0.20, "snap_ms": 2.0,
    },
    "modifier": {
        "res1_freq": 1200.0, "res1_q": 8.0,
        "res2_freq": 1000.0, "res2_q": 6.0,
        "decay_ms": 6.0, "peak_dbfs": -3.5,
        "contact_ms": 0.10, "snap_ms": 1.2,
    },
    "function": {
        "res1_freq": 1500.0, "res1_q": 8.0,
        "res2_freq": 2200.0, "res2_q": 15.0,
        "decay_ms": 3.0, "peak_dbfs": -1.0,
        "contact_ms": 0.20, "snap_ms": 1.5,
    },
    "punctuation": {
        "res1_freq": 1000.0, "res1_q": 5.0,
        "res2_freq": 1500.0, "res2_q": 6.0,
        "decay_ms": 5.0, "peak_dbfs": -2.5,
        "contact_ms": 0.10, "snap_ms": 1.3,
    },
    "numpad": {
        "res1_freq": 1200.0, "res1_q": 5.0,
        "res2_freq": 800.0, "res2_q": 4.0,
        "decay_ms": 4.0, "peak_dbfs": -4.0,
        "contact_ms": 0.15, "snap_ms": 1.5,
    },
}


def get_category(keycode):
    if keycode in MEASURED_KEYS:
        return None  # use measured data
    if 0x02 <= keycode <= 0x0b:
        return "number"
    if 0x0c <= keycode <= 0x0d:
        return "punctuation"
    if keycode in (0x0e, 0x0f):
        return "modifier"
    if 0x10 <= keycode <= 0x19:
        return "alpha"
    if 0x1a <= keycode <= 0x1b:
        return "punctuation"
    if keycode in (0x1c, 0x1d):
        return "modifier"
    if 0x1e <= keycode <= 0x26:
        return "alpha"
    if 0x27 <= keycode <= 0x28:
        return "punctuation"
    if keycode in (0x29,):
        return "punctuation"
    if keycode in (0x2a, 0x36):
        return "modifier"
    if 0x2b <= keycode <= 0x2b:
        return "punctuation"
    if 0x2c <= keycode <= 0x32:
        return "alpha"
    if 0x33 <= keycode <= 0x35:
        return "punctuation"
    if keycode in (0x37, 0x38, 0x3a):
        return "modifier"
    if keycode == 0x39:
        return None  # measured
    if 0x3b <= keycode <= 0x44:
        return "function"
    if 0x45 <= keycode <= 0x46:
        return "modifier"
    if 0x47 <= keycode <= 0x52:
        return "numpad"
    if 0x53 <= keycode <= 0x57:
        return "modifier"
    if 0x58 <= keycode <= 0x59:
        return None
    if 0x5a <= keycode <= 0x5b:
        return None
    if 0x60 <= keycode <= 0x7d:
        return "function"
    return "alpha"


def lerp(a, b, t):
    return a + (b - a) * t


def find_nearest_measured(keycode):
    measured = list(MEASURED_KEYS.keys())
    return min(measured, key=lambda k: abs(k - keycode))


def get_params_for_key(keycode, is_press):
    """Get synthesis parameters for a keycode, using measured data or interpolation."""
    if keycode in MEASURED_KEYS:
        name, press_data, release_data = MEASURED_KEYS[keycode]
        d = press_data if is_press else release_data
        return {
            "primary_freq": d[2],
            "primary_q": d[3],
            "secondary_freq": d[4],
            "secondary_q": d[5],
            "ring_ms": d[1],
            "peak_dbfs": d[0],
            "contact_ms": d[6],
            "snap_ms": d[7],
        }

    cat_name = get_category(keycode)
    cat = CATEGORIES[cat_name] if cat_name else CATEGORIES["alpha"]

    h = (keycode * 2654435761) & 0xFFFF
    v = h / 65535.0  # [0, 1]

    nearest = find_nearest_measured(keycode)
    _, press_ref, release_ref = MEASURED_KEYS[nearest]
    ref = press_ref if is_press else release_ref

    # Blend category defaults with nearest measured key (70% category, 30% nearest)
    primary_freq = lerp(cat["res1_freq"], ref[2], 0.3) * (1.0 + (v - 0.5) * 0.05)
    primary_q = lerp(cat["res1_q"], ref[3], 0.3)
    secondary_freq = lerp(cat["res2_freq"], ref[4], 0.3) * (1.0 + (v - 0.5) * 0.05)
    secondary_q = lerp(cat["res2_q"], ref[5], 0.3)
    ring_ms = lerp(cat["decay_ms"], ref[1], 0.3) * (1.0 + (v - 0.5) * 0.2)
    peak_dbfs = lerp(cat["peak_dbfs"], ref[0], 0.3)
    contact_ms = lerp(cat["contact_ms"], ref[6], 0.3)
    snap_ms = lerp(cat["snap_ms"], ref[7], 0.3)

    return {
        "primary_freq": primary_freq,
        "primary_q": primary_q,
        "secondary_freq": secondary_freq,
        "secondary_q": secondary_q,
        "ring_ms": ring_ms,
        "peak_dbfs": peak_dbfs,
        "contact_ms": contact_ms,
        "snap_ms": snap_ms,
    }


def generate_array_rows(is_press):
    """Generate the 256 rows for one array."""
    lines = []
    for kc in range(256):
        p = get_params_for_key(kc, is_press)
        comment = ""
        if kc in MEASURED_KEYS:
            comment = f"  // {MEASURED_KEYS[kc][0]} (measured)"
        lines.append(
            f"        {{ {p['primary_freq']:.1f}f, {p['primary_q']:.1f}f, "
            f"{p['secondary_freq']:.1f}f, {p['secondary_q']:.1f}f, "
            f"{p['ring_ms']:.1f}f, {p['peak_dbfs']:.1f}f, "
            f"{p['contact_ms']:.3f}f, {p['snap_ms']:.3f}f, "
            f"{DEFAULT_SNAP_BW_MS:.1f}f }},  // 0x{kc:02x}{comment}"
        )
    return lines


def main():
    sound_dir = Path(__file__).resolve().parent.parent / "sound"

    cxx_path = sound_dir / "bucklespring_data.cxx"
    cxx_path.write_text(generate_cxx())
    print(f"Generated {cxx_path}")

    print(f"  256 press + 256 release parameter sets")
    print(f"  {len(MEASURED_KEYS)} measured keys used as reference")


def generate_cxx():
    """Generate the module implementation file (array definitions)."""
    lines = []
    lines.append("// Auto-generated by tools/gen-bucklespring.py -- DO NOT EDIT.")
    lines.append("// Defines the bucklespring parameter arrays (declared extern in bucklespring_data.ixx).")
    lines.append("")
    lines.append("module;")
    lines.append("#include <array>")
    lines.append("")
    lines.append("module fs8.sound;")
    lines.append("")
    lines.append("import :bucklespring_data;")
    lines.append("")
    lines.append("using fs8::bucklespring_params;")
    lines.append("")
    lines.append("// clang-format off")
    lines.append("")
    lines.append("const std::array<bucklespring_params, 256> fs8::bucklespring_press_params = {{")
    lines.extend(generate_array_rows(is_press=True))
    lines.append("}};")
    lines.append("")
    lines.append("const std::array<bucklespring_params, 256> fs8::bucklespring_release_params = {{")
    lines.extend(generate_array_rows(is_press=False))
    lines.append("}};")
    lines.append("")
    lines.append("// clang-format on")
    lines.append("")
    return "\n".join(lines)


if __name__ == "__main__":
    main()
