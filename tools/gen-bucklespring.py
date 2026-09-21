#!/usr/bin/env python3
"""Generate bucklespring_data.ixx and bucklespring_data.cxx from WAV analysis.

The .ixx file contains the struct definition and extern const array declarations.
The .cxx file is the module implementation unit that defines the arrays.
"""

import re
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Known key analysis data extracted from analysis_results.txt
# Format: keycode_hex -> {press_params, release_params}
# Each params dict has the fields matching bucklespring_voice.
# ---------------------------------------------------------------------------

# Resonance summary (top 2 per key, press events)
# Key               : Freq1(Q=Q1), Freq2(Q=Q2)
# Decay tau stats (press): mean=5.872ms, std=2.820ms
# Amplitude stats (press): peak mean=-2.99 dBFS

# Per-key measured data from analysis_results.txt
MEASURED_KEYS = {
    # keycode: (name, press_data, release_data)
    # press_data = (peak_dbfs, decay_tau_ms, res1_freq, res1_q, res2_freq, res2_q, transient_slope, ring_slope)
    0x01: ("ESC",        (-5.68, 3.98,  9076.0, 54.0, 2784.0, 8.0,  3.24, -4.13),  (-9.47, 2.96,  6450.0, 12.0, 2934.0, 8.0, -12.06, -3.53)),
    0x02: ("1_KEY",      (-1.57, 9.76,  1002.0, 5.0,  501.0,  5.0, -6.03, -0.90),  (-2.34, 4.46,  1503.0, 1.7,  1421.0, 3.0, -6.03, -0.90)),
    0x0e: ("BACKSPACE",  (-4.30, 8.80,  1448.0, 9.0,  835.0,  4.0, -8.90, -9.54),  (-2.52, 4.41,  1336.0, 2.4,  2728.0, 9.8, -12.06, -3.53)),
    0x0f: ("TAB",        (-6.01, 6.88,  1058.0, 3.0,  1670.0, 5.0, -8.66, -6.72),  (-6.24, 2.66,  3010.0, 20.0, 2005.0, 5.0, -12.06, -3.53)),
    0x1c: ("ENTER",      (-3.38, 5.22,  1392.0, 3.0,  2394.0, 5.0, 1.04,  -5.80),  (-2.68, 3.65,  1392.0, 3.0,  2394.0, 5.0, -12.06, -3.53)),
    0x1e: ("A",          (-3.22, 4.26,  1169.0, 5.0,  835.0,  5.0, 3.68,  -2.32),  (-3.90, 3.62,  1169.0, 5.0,  835.0,  5.0, 3.68,  -2.32)),
    0x2a: ("LSHIFT",     (-3.60, 7.38,  1169.0, 10.0, 947.0,  8.0, -3.68, -4.27),  (-1.79, 3.70,  1169.0, 10.0, 947.0,  8.0, -3.68, -4.27)),
    0x2c: ("Z",          (0.00,  5.44,  1225.0, 4.0,  6849.0, 31.0, -6.03, -0.90),  (-5.83, 3.40,  2151.0, 20.0, 1225.0, 4.0, -6.03, -0.90)),
    0x33: ("COMMA",      (-0.58, 6.28,  1114.0, 7.0,  11972.0,108.0,-6.03, -0.90),  (-5.52, 4.10,  2545.0, 20.0, 1776.0, 18.0, -6.03, -0.90)),
    0x34: ("DOT",        (-3.25, 4.26,  835.0,  4.0,  1670.0, 7.0, -6.03, -0.90),  (-3.17, 3.40,  1776.0, 18.0, 835.0,  4.0, -6.03, -0.90)),
    0x39: ("SPACE",      (-4.05, 4.46,  2283.0, 4.0,  1169.0, 5.0, 2.68,  -1.93),  (-1.70, 3.50,  6010.0, 12.0, 3007.0, 8.0, -12.06, -3.53)),
    0x3b: ("F1",         (-0.27, 2.03,  1169.0, 5.0,  2116.0, 13.0,-6.03, -0.90),  (-7.43, 2.80,  4242.0, 20.0, 2016.0, 20.0, -6.03, -0.90)),
}

# Key categories for interpolation of unmeasured keys
# Each category has default resonance freqs, Q, decay, and noise character
CATEGORIES = {
    "alpha": {
        "res1_freq": 1100.0, "res1_q": 5.0,
        "res2_freq": 900.0, "res2_q": 5.0,
        "decay_ms": 5.0, "peak_dbfs": -3.0,
        "transient_slope": -2.0, "ring_slope": -3.0,
    },
    "number": {
        "res1_freq": 1000.0, "res1_q": 5.0,
        "res2_freq": 600.0, "res2_q": 5.0,
        "decay_ms": 7.0, "peak_dbfs": -2.0,
        "transient_slope": -5.0, "ring_slope": -2.0,
    },
    "modifier": {
        "res1_freq": 1200.0, "res1_q": 8.0,
        "res2_freq": 1000.0, "res2_q": 6.0,
        "decay_ms": 6.0, "peak_dbfs": -3.5,
        "transient_slope": -4.0, "ring_slope": -4.0,
    },
    "function": {
        "res1_freq": 1500.0, "res1_q": 8.0,
        "res2_freq": 2200.0, "res2_q": 15.0,
        "decay_ms": 3.0, "peak_dbfs": -1.0,
        "transient_slope": -5.0, "ring_slope": -2.0,
    },
    "punctuation": {
        "res1_freq": 1000.0, "res1_q": 5.0,
        "res2_freq": 1500.0, "res2_q": 6.0,
        "decay_ms": 5.0, "peak_dbfs": -2.5,
        "transient_slope": -5.0, "ring_slope": -3.0,
    },
    "numpad": {
        "res1_freq": 1200.0, "res1_q": 5.0,
        "res2_freq": 800.0, "res2_q": 4.0,
        "decay_ms": 4.0, "peak_dbfs": -4.0,
        "transient_slope": -5.0, "ring_slope": -4.0,
    },
}

# Map keycode ranges to categories
def get_category(keycode):
    if keycode in MEASURED_KEYS:
        return None  # use measured data
    if 0x02 <= keycode <= 0x0b:  # 1-0
        return "number"
    if 0x0c <= keycode <= 0x0d:  # MINUS, EQUALS
        return "punctuation"
    if keycode in (0x0e, 0x0f):  # BACKSPACE, TAB
        return "modifier"
    if 0x10 <= keycode <= 0x19:  # Q-P
        return "alpha"
    if 0x1a <= keycode <= 0x1b:  # LBRACKET, RBRACKET
        return "punctuation"
    if keycode in (0x1c, 0x1d):  # ENTER, LCTRL
        return "modifier"
    if 0x1e <= keycode <= 0x26:  # A-L
        return "alpha"
    if 0x27 <= keycode <= 0x28:  # SEMICOLON, APOSTROPHE
        return "punctuation"
    if keycode in (0x29,):  # GRAVE
        return "punctuation"
    if keycode in (0x2a, 0x36):  # LSHIFT, RSHIFT
        return "modifier"
    if 0x2b <= keycode <= 0x2b:  # BACKSLASH
        return "punctuation"
    if 0x2c <= keycode <= 0x32:  # Z-M
        return "alpha"
    if 0x33 <= keycode <= 0x35:  # COMMA, DOT, SLASH
        return "punctuation"
    if keycode in (0x37, 0x38, 0x3a):  # KPASTERISK, LALT, CAPSLOCK
        return "modifier"
    if keycode == 0x39:  # SPACE
        return None  # measured
    if 0x3b <= keycode <= 0x44:  # F1-F10
        return "function"
    if 0x45 <= keycode <= 0x46:  # NUMLOCK, SCROLLLOCK
        return "modifier"
    if 0x47 <= keycode <= 0x52:  # KP7-KP0
        return "numpad"
    if 0x53 <= keycode <= 0x57:  # KPENTER and others
        return "modifier"
    if 0x58 <= keycode <= 0x59:  # unused
        return None
    if 0x5a <= keycode <= 0x5b:  # unused
        return None
    if 0x60 <= keycode <= 0x7d:  # F11, F12, etc
        return "function"
    return "alpha"  # fallback


def lerp(a, b, t):
    return a + (b - a) * t


def find_nearest_measured(keycode):
    """Find the nearest measured keycode by Euclidean-ish distance on keyboard layout."""
    measured = list(MEASURED_KEYS.keys())
    # Use simple keycode distance
    best = min(measured, key=lambda k: abs(k - keycode))
    return best


def get_voice_for_key(keycode, is_press):
    """Get voice parameters for a keycode, using measured data or interpolation."""
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
            "transient_slope": d[6],
            "ring_slope": d[7],
        }

    cat_name = get_category(keycode)
    if cat_name is None:
        # Fallback: use alpha defaults
        cat = CATEGORIES["alpha"]
    else:
        cat = CATEGORIES[cat_name]

    # Use a simple hash to add per-key variation
    h = (keycode * 2654435761) & 0xFFFF
    v = h / 65535.0  # [0, 1]

    # Find nearest measured key for reference
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
    transient_slope = lerp(cat["transient_slope"], ref[6], 0.3)
    ring_slope = lerp(cat["ring_slope"], ref[7], 0.3)

    return {
        "primary_freq": primary_freq,
        "primary_q": primary_q,
        "secondary_freq": secondary_freq,
        "secondary_q": secondary_q,
        "ring_ms": ring_ms,
        "peak_dbfs": peak_dbfs,
        "transient_slope": transient_slope,
        "ring_slope": ring_slope,
    }


def generate_array_rows(is_press):
    """Generate the 256 rows for one array."""
    lines = []
    for kc in range(256):
        v = get_voice_for_key(kc, is_press)
        comment = ""
        if kc in MEASURED_KEYS:
            comment = f"  // {MEASURED_KEYS[kc][0]} (measured)"
        lines.append(
            f"        {{ {v['primary_freq']:.1f}f, {v['primary_q']:.1f}f, "
            f"{v['secondary_freq']:.1f}f, {v['secondary_q']:.1f}f, "
            f"{v['ring_ms']:.1f}f, {v['peak_dbfs']:.1f}f, "
            f"{v['transient_slope']:.1f}f, {v['ring_slope']:.1f}f }},  // 0x{kc:02x}{comment}"
        )
    return lines


def generate_ixx():
    """Generate the module interface file (struct + extern const declarations)."""
    lines = []
    lines.append("// Auto-generated by tools/gen-bucklespring.py -- DO NOT EDIT.")
    lines.append("//")
    lines.append("// Per-keycode synthesis voice struct and extern array declarations")
    lines.append("// for the bucklespring sound profile. Array definitions are in bucklespring_data.cxx.")
    lines.append("")
    lines.append("module;")
    lines.append("#include <array>")
    lines.append("#include <cstdint>")
    lines.append("")
    lines.append("export module fs8.mods:bucklespring_data;")
    lines.append("")
    lines.append("export namespace fs8 {")
    lines.append("")
    lines.append("    /// Per-keycode synthesis voice parameters.")
    lines.append("    struct bucklespring_voice {")
    lines.append("        float primary_freq;       ///< main spring resonance (Hz)")
    lines.append("        float primary_q;          ///< primary bandpass Q")
    lines.append("        float secondary_freq;     ///< secondary resonance (Hz)")
    lines.append("        float secondary_q;        ///< secondary bandpass Q")
    lines.append("        float ring_ms;            ///< resonance decay time (ms)")
    lines.append("        float peak_dbfs;          ///< target peak amplitude (dBFS)")
    lines.append("        float transient_slope;    ///< transient noise slope (dB/oct)")
    lines.append("        float ring_slope;         ///< ring noise slope (dB/oct)")
    lines.append("    };")
    lines.append("")
    lines.append("    extern const std::array<bucklespring_voice, 256> bucklespring_press_params;")
    lines.append("    extern const std::array<bucklespring_voice, 256> bucklespring_release_params;")
    lines.append("")
    lines.append("} // namespace fs8")
    lines.append("")
    return "\n".join(lines)


def generate_cxx():
    """Generate the module implementation file (array definitions)."""
    lines = []
    lines.append("// Auto-generated by tools/gen-bucklespring.py -- DO NOT EDIT.")
    lines.append("// Defines the bucklespring parameter arrays (declared extern in bucklespring_data.ixx).")
    lines.append("")
    lines.append("module;")
    lines.append("#include <array>")
    lines.append("")
    lines.append("module fs8.mods;")
    lines.append("")
    lines.append("import :bucklespring_data;")
    lines.append("")
    lines.append("using fs8::bucklespring_voice;")
    lines.append("")
    lines.append("// clang-format off")
    lines.append("")
    lines.append("const std::array<bucklespring_voice, 256> fs8::bucklespring_press_params = {{")
    lines.extend(generate_array_rows(is_press=True))
    lines.append("}};")
    lines.append("")
    lines.append("const std::array<bucklespring_voice, 256> fs8::bucklespring_release_params = {{")
    lines.extend(generate_array_rows(is_press=False))
    lines.append("}};")
    lines.append("")
    lines.append("// clang-format on")
    lines.append("")
    return "\n".join(lines)


def main():
    sound_dir = Path(__file__).resolve().parent.parent / "sound"

    ixx_path = sound_dir / "bucklespring_data.ixx"
    ixx_path.write_text(generate_ixx())
    print(f"Generated {ixx_path}")

    cxx_path = sound_dir / "bucklespring_data.cxx"
    cxx_path.write_text(generate_cxx())
    print(f"Generated {cxx_path}")

    print(f"  256 press + 256 release voices")
    print(f"  {len(MEASURED_KEYS)} measured keys used as reference")


if __name__ == "__main__":
    main()
