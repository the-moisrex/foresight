#!/usr/bin/env python3
"""Generate <profile>_data.cxx parameter tables for click-engine profiles.

Every click-style profile (modelf, and the realistic batch: linear, topre,
typewriter, mx-blue, alps) shares fs8::detail::render_click; this tool owns
their 256-row press/release parameter arrays.  Tables are built from:

  * MEASURED rows -- per-keycode values extracted from reference recordings
    by tools/analyze-clicks.py (provenance/license noted next to each table),
    and/or
  * CATEGORIES -- key-category defaults plus a deterministic per-key jitter
    for every other keycode (same interpolation scheme as
    tools/gen-bucklespring.py).

The .cxx definition files are emitted here; the matching <profile>_data.ixx
extern headers are hand-written.

Usage:
    python3 tools/gen-click-profile.py --profile modelf
    python3 tools/gen-click-profile.py --profile all
"""

import argparse
from dataclasses import dataclass, field
from pathlib import Path

# ---------------------------------------------------------------------------
# Key categories (shared keycode ranges, mirroring gen-bucklespring.py)
# ---------------------------------------------------------------------------

DEFAULT_CATEGORY = "alpha"


def get_category(keycode: int) -> str:
    if 0x02 <= keycode <= 0x0B:
        return "number"
    if 0x0C <= keycode <= 0x0D:
        return "punctuation"
    if keycode in (0x0E, 0x0F):
        return "modifier"
    if 0x10 <= keycode <= 0x19:
        return "alpha"
    if 0x1A <= keycode <= 0x1B:
        return "punctuation"
    if keycode in (0x1C, 0x1D):
        return "modifier"
    if 0x1E <= keycode <= 0x26:
        return "alpha"
    if 0x27 <= keycode <= 0x29:
        return "punctuation"
    if keycode in (0x2A, 0x36):
        return "modifier"
    if keycode == 0x2B:
        return "punctuation"
    if 0x2C <= keycode <= 0x32:
        return "alpha"
    if 0x33 <= keycode <= 0x35:
        return "punctuation"
    if keycode in (0x37, 0x38, 0x3A, 0x45, 0x46):
        return "modifier"
    if keycode == 0x39:
        return "modifier"  # space bar: big spring, low thump
    if 0x3B <= keycode <= 0x44:
        return "function"
    if 0x47 <= keycode <= 0x52:
        return "numpad"
    if 0x53 <= keycode <= 0x57:
        return "modifier"
    if 0x60 <= keycode <= 0x7D:
        return "function"
    return DEFAULT_CATEGORY


def jitter(keycode: int) -> float:
    """Deterministic per-key value in [0, 1) (same FNV-ish hash as gen-bucklespring)."""
    return ((keycode * 2654435761) & 0xFFFF) / 65535.0


# ---------------------------------------------------------------------------
# Profile description
#
# Category row layout (9 floats, matches fs8::click_params field order):
#   (res1_f, res1_q, res2_f, res2_q, ring_ms, peak_dbfs, contact_ms,
#    snap_ms, snap_bw_ms)
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Jitter:
    """Per-key deterministic spread applied to category-derived rows."""

    mode_spread: float = 0.06  # +/- fraction on resonant frequencies
    ring_spread: float = 0.25
    peak_spread: float = 1.5  # dB
    snap_spread: float = 0.4


@dataclass(frozen=True)
class ReleaseRule:
    """How to derive the release row from the press row (unmeasured keys)."""

    ring_mul: float = 0.65
    peak_delta: float = -1.5  # dB
    snap_mul: float = 0.55
    snap_bw_mul: float = 0.85


@dataclass
class Profile:
    title: str  # e.g. "Model F" -> header comment + generated banner
    categories: dict[str, tuple[float, ...]]
    measured: dict[int, tuple[tuple[float, ...] | None, tuple[float, ...] | None]] = field(
        default_factory=dict
    )  # keycode -> (press row, release row); wins over categories, None = derive
    default_category: str = DEFAULT_CATEGORY
    jitter: Jitter = Jitter()
    release: ReleaseRule = ReleaseRule()

    @staticmethod
    def finalize(row: tuple[float, ...]) -> tuple[float, ...]:
        """Sanitize a row, then guarantee the slot budget.

        - Resonances are capped at 9 kHz: measured upstroke ticks can sit at
          10-13 kHz, and the engine derives two more modes above them
          (primary*1.35, secondary*1.7) which must stay well below Nyquist.
        - A secondary inside the 0.75-1.35x guard band of the primary would
          beat as near-unison; mirror it below instead.
        - snap + 7*ring + contact + 1 <= 149 ms (the player caps slots at
          150 ms; an overlong measured ring is shortened so the ring is
          never truncated mid-air).  The budget leaves 0.5 ms of headroom
          and the cut value is floored to one decimal, because the emitted
          table prints ring_ms with %.1f — rounding a budget-fit ring up
          would push the duration back over the 150 ms slot.
        """
        f1 = min(row[0], 9000.0)
        f2 = min(row[2], 9000.0)
        if 0.75 * f1 <= f2 <= 1.35 * f1:
            f2 = 0.55 * f1
        out = [f1, row[1], f2, row[3], row[4], row[5], row[6], row[7], row[8]]
        budget = (148.5 - out[7] - out[6]) / 7.0
        if out[4] > budget:
            out[4] = max(0.5, int(budget * 10.0) / 10.0)
        return tuple(out)

    def press_row(self, keycode: int) -> tuple[float, ...]:
        m = self.measured.get(keycode)
        if m is not None and m[0] is not None:
            return self.finalize(m[0])
        row = self.category_row(keycode)
        return self.finalize(row)

    def category_row(self, keycode: int) -> tuple[float, ...]:
        row = self.categories.get(get_category(keycode))
        if row is None:
            row = self.categories[self.default_category]
        res1_f, res1_q, res2_f, res2_q, ring, peak, contact, snap, snap_bw = row
        v = jitter(keycode)
        spread = 1.0 + (v - 0.5) * self.jitter.mode_spread
        return (
            res1_f * spread,
            res1_q,
            res2_f * spread,
            res2_q,
            ring * (1.0 + (v - 0.5) * self.jitter.ring_spread),
            peak + (v - 0.5) * self.jitter.peak_spread,
            contact,
            snap * (1.0 + (v - 0.5) * self.jitter.snap_spread),
            snap_bw,
        )

    def release_row(self, keycode: int) -> tuple[float, ...]:
        m = self.measured.get(keycode)
        if m is not None and m[1] is not None:
            return self.finalize(m[1])
        p = list(self.press_row(keycode))
        p[4] *= self.release.ring_mul
        p[5] += self.release.peak_delta
        p[7] *= self.release.snap_mul
        p[8] *= self.release.snap_bw_mul
        return self.finalize(tuple(p))


# ---------------------------------------------------------------------------
# Profiles
# ---------------------------------------------------------------------------

PROFILES: dict[str, Profile] = {
    # IBM Model F capacitive buckling spring: lighter and more metallic than
    # the Model M — higher modes, longer ringing, sharper snap, less
    # bottom-out thump.  No recordings analysed; category-interpolated.
    "modelf": Profile(
        title="Model F",
        categories={
            #                  res1_f  res1_q  res2_f  res2_q  ring   peak   contact snap   snap_bw
            "alpha":        (2100.0,  8.0,    950.0,  6.0,    6.5,  -3.0,   0.10,  1.60,  0.9),
            "number":       (1950.0,  8.0,    800.0,  5.0,    7.0,  -2.5,   0.15,  2.00,  1.0),
            "modifier":     (2350.0,  10.0,   1150.0, 7.0,    6.0,  -3.5,   0.08,  1.30,  0.8),
            "function":     (2900.0,  12.0,   2100.0, 12.0,   5.0,  -2.0,   0.15,  1.50,  0.8),
            "punctuation":  (2000.0,  8.0,    1050.0, 6.0,    6.0,  -2.8,   0.08,  1.40,  0.9),
            "numpad":       (2200.0,  8.0,    900.0,  5.0,    5.5,  -4.0,   0.12,  1.60,  0.9),
        },
    ),
    # Linear switches (MX Red-class): no click jacket, smooth travel — the
    # sound is the bottom-out thud.  Seed: median of 168 detected keystrokes
    # in mx_red_commons.wav (tools/analyze-clicks.py).  Measured ring (24 ms)
    # includes room/desk resonance of the typing recording; the dry synth only
    # needs the switch's own decay, so it is rebased down to ~8 ms.  peak_dbfs
    # is rebased from event-relative (-14.4 dB) to loudness parity with the
    # existing profiles.
    "linear": Profile(
        title="Linear",
        categories={
            #                  res1_f  res1_q  res2_f  res2_q  ring   peak   contact snap   snap_bw
            "alpha":        (1300.0,  25.0,   2814.0, 45.0,   8.0,  -8.0,   0.000, 3.80,  2.0),
            "number":       (1260.0,  24.0,   2700.0, 42.0,   8.0,  -8.3,   0.000, 3.60,  2.0),
            "modifier":     (1150.0,  22.0,   2450.0, 40.0,   9.5,  -7.5,   0.000, 4.20,  2.2),
            "function":     (1450.0,  28.0,   3100.0, 50.0,   7.0,  -9.0,   0.000, 3.40,  1.8),
            "punctuation":  (1300.0,  25.0,   2800.0, 45.0,   8.0,  -8.2,   0.000, 3.70,  2.0),
            "numpad":       (1350.0,  26.0,   2900.0, 46.0,   7.5,  -8.8,   0.000, 3.60,  1.9),
        },
    ),
    # Topre electrostatic capacitive (rubber dome): the pooled median of 774
    # keystrokes across realforce_87u, hhkb_type_s, fc660c_silenced and
    # novatouch (tools/analyze-clicks.py).  Soft dome thock — rebased like
    # `linear` (room-included ring 24 ms -> dry 8 ms; peak to loudness parity).
    "topre": Profile(
        title="Topre",
        categories={
            #                  res1_f  res1_q  res2_f  res2_q  ring   peak   contact snap   snap_bw
            "alpha":        (1023.0,  18.0,   1686.0, 26.0,   8.0,  -8.5,   0.000, 4.00,  1.5),
            "number":       (1000.0,  17.0,   1650.0, 25.0,   8.0,  -8.8,   0.000, 3.90,  1.5),
            "modifier":     (880.0,   16.0,   1450.0, 24.0,   9.5,  -7.8,   0.000, 4.60,  1.7),
            "function":     (1250.0,  20.0,   2100.0, 30.0,   6.5,  -9.2,   0.000, 3.40,  1.3),
            "punctuation":  (1050.0,  18.0,   1720.0, 26.0,   7.8,  -8.6,   0.000, 3.90,  1.5),
            "numpad":       (1100.0,  19.0,   1800.0, 28.0,   7.2,  -9.0,   0.000, 3.70,  1.4),
        },
    ),
    # Manual typewriter: 50 typebar strikes in typewriter_bigsoundbank.wav —
    # instant sharp attack (snap 0.6 ms), short tight ring (3.4 ms), loud
    # metallic pings at 3.1–7.0 kHz with very narrow modes (median Q ~155).
    # The measured secondary sits near-unison with the primary (typebars ring
    # as one cluster), so the category secondary is placed a guard-band below.
    # Wide mode jitter mirrors the measured q25/q75 spread (per-bar pings).
    "typewriter": Profile(
        title="Typewriter",
        categories={
            #                  res1_f  res1_q  res2_f  res2_q  ring   peak   contact snap   snap_bw
            "alpha":        (4600.0,  150.0,  2600.0, 120.0,  3.4,  -6.0,   0.000, 0.60,  1.0),
            "number":       (4800.0,  160.0,  2700.0, 125.0,  3.3,  -6.2,   0.000, 0.58,  1.0),
            "modifier":     (2400.0,  100.0,  1300.0, 80.0,   5.0,  -5.0,   0.000, 1.20,  1.6),
            "function":     (5200.0,  170.0,  2900.0, 130.0,  3.0,  -7.0,   0.000, 0.55,  0.9),
            "punctuation":  (4400.0,  150.0,  2500.0, 115.0,  3.5,  -6.1,   0.000, 0.62,  1.1),
            "numpad":       (4700.0,  155.0,  2650.0, 120.0,  3.2,  -6.8,   0.000, 0.58,  1.0),
        },
        jitter=Jitter(mode_spread=0.30, ring_spread=0.15, peak_spread=3.0, snap_spread=0.5),
    ),
    # Cherry MX Blue (click jacket): 104 keycodes measured per-key from the
    # GPL-2.0 cherrybuckle reference set (105 press/104 release WAVs, analysis
    # only — no audio in this repo).  Measured keys win over the category
    # table; categories fill the ~152 unmeasured keycodes (nav, extra F-keys)
    # with medians of the measured keys in the same category.  Keycode 0x77
    # is excluded: its capture measured a 13 kHz Q=300 beep, not a click.
    # Releases of unmeasured keys follow the measured upstroke style
    # (faster snap, slightly louder, same ring).
    "mx_blue": Profile(
        title="MX Blue",
        categories={
            #                  res1_f  res1_q  res2_f  res2_q  ring   peak   contact snap   snap_bw
            "alpha":        (1423.9,  38.7,   854.3,  37.4,   6.0,  -4.0,   0.000, 2.53,  1.6),
            "number":       (1292.0,  37.9,   775.2,  32.5,   5.1,  -6.8,   0.000, 5.08,  1.2),
            "modifier":     (1394.3,  42.8,   716.0,  24.0,   6.4,  -2.9,   0.000, 1.79,  1.6),
            "function":     (1427.9,  41.4,   856.7,  38.5,   6.0,  -6.0,   0.000, 1.37,  1.2),
            "punctuation":  (1146.6,  32.8,   705.2,  26.2,   6.0,  -3.1,   0.000, 1.68,  1.1),
            "numpad":       (744.2,   27.2,   446.5,  22.4,   5.8,  -4.0,   0.000, 1.88,  1.4),
        },
        measured={
            0x01: ((1313.5, 37.5, 2818.2, 55.1, 24.0, -9.5, 0.000, 8.277, 2.5), (9113.9, 161.2, 2912.4, 36.1, 7.0, -5.7, 0.000, 1.723, 1.9)),
            0x02: ((931.3, 28.8, 1286.6, 20.8, 5.5, -2.3, 0.000, 3.719, 2.1), (3111.5, 115.6, 1176.3, 36.4, 9.2, -1.6, 0.000, 2.562, 1.8)),
            0x03: ((2226.0, 75.2, 740.2, 19.6, 24.0, -7.6, 0.000, 6.893, 0.8), (1114.3, 17.2, 2689.0, 76.8, 16.3, -0.3, 0.000, 1.043, 1.9)),
            0x04: ((1388.9, 46.9, 559.9, 18.9, 6.3, -5.8, 0.000, 1.451, 1.3), (1184.3, 44.0, 2500.5, 84.5, 11.2, -2.2, 0.000, 1.429, 1.8)),
            0x05: ((1195.1, 21.1, 691.8, 16.1, 2.4, -8.7, 0.000, 7.211, 1.0), (1469.6, 54.6, 2780.5, 49.2, 10.5, -0.6, 0.000, 1.156, 2.5)),
            0x06: ((707.9, 26.3, 2858.5, 88.5, 4.5, -13.0, 0.000, 4.512, 1.1), (1485.8, 42.5, 1103.6, 34.2, 7.0, -4.6, 0.000, 1.383, 2.5)),
            0x07: ((713.3, 18.9, 1488.5, 46.1, 8.0, -13.7, 0.000, 6.417, 2.5), (1200.5, 26.2, 2872.0, 106.7, 17.0, -0.9, 0.000, 1.315, 2.4)),
            0x08: ((1515.4, 51.2, 702.5, 26.1, 5.5, -6.1, 0.000, 4.512, 1.1), (1160.1, 43.1, 707.9, 29.2, 6.1, -2.4, 0.000, 2.063, 2.2)),
            0x09: ((734.8, 27.3, 2217.9, 82.4, 3.7, -13.1, 0.000, 7.914, 1.4), (2476.3, 92.0, 1442.7, 67.0, 5.1, -0.9, 0.000, 1.179, 1.7)),
            0x0a: ((2126.4, 71.8, 939.4, 38.8, 4.7, -1.5, 0.000, 5.646, 0.9), (1442.7, 53.6, 670.2, 24.9, 6.3, -2.0, 0.000, 1.270, 2.4)),
            0x0b: ((1445.4, 53.7, 2549.0, 59.2, 4.6, -2.6, 0.000, 1.882, 1.5), (559.9, 23.1, 1103.6, 22.8, 7.5, -2.6, 0.000, 2.971, 1.6)),
            0x0c: ((2589.4, 87.5, 699.8, 26.0, 23.2, -7.2, 0.000, 3.560, 1.0), (1227.4, 16.9, 2882.8, 119.0, 6.3, -3.1, 0.000, 1.088, 1.2)),
            0x0d: ((1426.6, 53.0, 519.5, 17.5, 7.1, -3.1, 0.000, 1.406, 1.1), (842.5, 15.7, 1391.6, 36.9, 7.9, -4.2, 0.000, 0.862, 1.6)),
            0x0e: ((1405.0, 27.5, 1017.4, 25.2, 10.8, -2.5, 0.000, 3.923, 2.3), (10252.5, 300.0, 1429.3, 48.3, 10.9, -4.3, 0.000, 1.882, 1.1)),
            0x0f: ((686.4, 28.3, 1391.6, 36.9, 6.4, -6.2, 0.000, 2.948, 2.5), (1388.9, 43.0, 7577.0, 234.6, 6.0, -6.0, 0.000, 1.202, 2.5)),
            0x10: ((2164.1, 80.4, 718.7, 33.4, 3.1, 0.0, 0.000, 1.474, 1.5), (3057.7, 87.4, 1041.7, 32.2, 5.3, -4.7, 0.000, 1.950, 1.0)),
            0x11: ((1200.5, 21.2, 2802.0, 94.6, 8.6, -0.0, 0.000, 1.043, 1.1), (1109.0, 41.2, 2853.1, 117.8, 4.9, -0.9, 0.000, 1.315, 2.4)),
            0x12: ((1432.0, 53.2, 511.4, 21.1, 5.0, -1.8, 0.000, 3.923, 2.5), (6118.1, 174.8, 1421.2, 48.0, 5.6, -0.6, 0.000, 0.952, 1.0)),
            0x13: ((1456.2, 67.6, 837.1, 18.3, 5.9, -0.6, 0.000, 2.630, 1.0), (594.9, 20.1, 1146.6, 47.3, 6.0, -2.0, 0.000, 1.111, 2.1)),
            0x14: ((716.0, 24.2, 1203.2, 49.7, 5.1, -1.5, 0.000, 0.748, 2.5), (1200.5, 34.3, 2904.3, 119.9, 9.5, -4.6, 0.000, 1.383, 1.4)),
            0x15: ((710.6, 26.4, 966.3, 44.9, 6.0, -2.8, 0.000, 2.313, 1.0), (1197.8, 14.8, 2686.3, 110.9, 9.6, 0.0, 0.000, 1.315, 1.2)),
            0x16: ((1442.7, 59.6, 936.7, 38.7, 6.8, -5.8, 0.000, 5.147, 1.3), (2137.2, 49.6, 1429.3, 66.4, 9.1, -4.2, 0.000, 1.927, 2.5)),
            0x17: ((1434.6, 44.4, 2150.6, 66.6, 5.2, -10.0, 0.000, 8.118, 2.1), (1138.6, 35.2, 2869.3, 82.0, 5.1, -0.9, 0.000, 1.156, 1.1)),
            0x18: ((1472.3, 54.7, 939.4, 38.8, 5.8, -5.8, 0.000, 2.494, 0.8), (1300.1, 48.3, 2616.3, 64.8, 7.8, -4.8, 0.000, 3.447, 1.7)),
            0x19: ((1437.3, 48.5, 713.3, 26.5, 4.9, -3.5, 0.000, 2.426, 1.2), (1184.3, 16.9, 5151.8, 191.4, 12.0, -0.0, 0.000, 0.680, 1.2)),
            0x1a: ((567.9, 12.4, 1127.8, 26.2, 4.4, -1.9, 0.000, 0.839, 1.4), (1310.8, 40.6, 562.6, 17.4, 8.9, -1.3, 0.000, 1.066, 1.4)),
            0x1b: ((1434.6, 44.4, 516.8, 19.2, 4.0, 0.0, 0.000, 3.197, 1.2), (1318.9, 54.4, 6591.9, 272.1, 3.8, -0.1, 0.000, 1.111, 1.0)),
            0x1c: ((1429.3, 53.1, 716.0, 29.6, 6.2, -2.7, 0.000, 1.791, 1.6), (890.9, 30.1, 2570.5, 43.4, 14.3, -3.4, 0.000, 1.497, 2.0)),
            0x1d: ((1380.8, 42.8, 694.4, 19.8, 5.8, 0.0, 0.000, 1.134, 1.3), (12914.5, 300.0, 1141.3, 38.5, 6.4, -3.7, 0.000, 2.993, 2.5)),
            0x1e: ((1415.8, 47.8, 777.9, 20.6, 7.3, -3.9, 0.000, 3.016, 0.8), (1391.6, 27.2, 5141.1, 159.2, 9.1, -3.2, 0.000, 0.839, 1.8)),
            0x1f: ((729.4, 30.1, 1453.5, 36.0, 4.7, -2.9, 0.000, 2.381, 1.7), (928.6, 34.5, 1475.0, 60.9, 13.2, -3.1, 0.000, 0.862, 2.5)),
            0x20: ((1472.3, 60.8, 2912.4, 98.4, 12.6, -4.1, 0.000, 2.630, 2.5), (1200.5, 44.6, 2540.9, 94.4, 4.3, -2.7, 0.000, 1.633, 2.5)),
            0x21: ((2853.1, 81.5, 1722.7, 64.0, 7.5, -8.9, 0.000, 6.100, 1.6), (1060.5, 35.8, 2422.5, 69.2, 4.9, 0.0, 0.000, 1.565, 1.4)),
            0x22: ((1170.9, 33.5, 699.8, 26.0, 6.3, -4.7, 0.000, 3.061, 2.5), (1106.3, 17.9, 2099.5, 70.9, 24.0, -5.2, 0.000, 1.383, 2.5)),
            0x23: ((689.1, 25.6, 1079.4, 22.3, 5.2, -1.9, 0.000, 1.655, 1.7), (2094.1, 64.8, 942.1, 35.0, 5.4, -0.6, 0.000, 1.723, 0.8)),
            0x24: ((1144.0, 8.5, 780.6, 26.4, 10.0, -0.7, 0.000, 2.562, 1.9), (1154.7, 25.2, 12893.0, 300.0, 6.5, -5.6, 0.000, 0.907, 2.5)),
            0x25: ((936.7, 38.7, 1418.5, 47.9, 4.6, 0.0, 0.000, 2.132, 2.0), (936.7, 31.6, 1292.0, 24.0, 7.5, -1.5, 0.000, 0.975, 1.9)),
            0x26: ((2153.3, 88.9, 1426.6, 44.2, 6.9, -2.6, 0.000, 1.995, 2.2), (936.7, 31.6, 2177.5, 53.9, 9.0, -1.6, 0.000, 2.313, 2.5)),
            0x27: ((681.0, 31.6, 1375.4, 51.1, 6.0, -2.7, 0.000, 1.587, 2.5), (1203.2, 27.9, 2217.9, 63.4, 7.3, -1.4, 0.000, 1.383, 0.8)),
            0x28: ((1388.9, 43.0, 705.2, 26.2, 4.0, -0.0, 0.000, 1.927, 1.8), (6906.8, 150.9, 2438.6, 90.6, 9.0, -3.5, 0.000, 4.104, 1.1)),
            0x29: ((1146.6, 32.8, 705.2, 29.1, 14.7, -11.9, 0.000, 0.340, 0.6), (1181.6, 54.9, 6602.6, 223.0, 5.7, -9.5, 0.000, 0.635, 2.5)),
            0x2a: ((1461.6, 54.3, 742.9, 25.1, 3.5, -1.8, 0.000, 1.338, 1.2), (9797.6, 300.0, 4131.7, 139.5, 6.9, -3.6, 0.000, 2.109, 2.5)),
            0x2b: ((1429.3, 44.2, 761.7, 9.1, 5.4, -1.1, 0.000, 1.859, 1.1), (1184.3, 29.3, 2624.4, 81.2, 3.0, -1.4, 0.000, 1.043, 1.8)),
            0x2c: ((1510.0, 51.0, 2263.7, 105.1, 5.6, -5.8, 0.000, 7.710, 2.1), (6780.3, 300.0, 1200.5, 63.7, 2.8, 0.0, 0.000, 1.315, 1.7)),
            0x2d: ((1141.3, 18.4, 3604.1, 148.8, 5.2, -4.8, 0.000, 5.782, 1.4), (1200.5, 49.6, 2174.9, 89.8, 4.5, -2.6, 0.000, 1.134, 2.0)),
            0x2e: ((729.4, 19.4, 1440.0, 48.6, 9.6, -3.4, 0.000, 4.830, 0.8), (1055.1, 30.2, 2855.8, 88.4, 5.3, -5.9, 0.000, 1.746, 2.1)),
            0x2f: ((681.0, 28.1, 1240.9, 28.8, 11.9, -7.0, 0.000, 6.531, 2.5), (9175.8, 227.3, 1986.4, 82.0, 3.1, 0.0, 0.000, 1.678, 1.4)),
            0x30: ((683.7, 25.4, 1383.5, 46.7, 4.6, -6.5, 0.000, 2.336, 2.3), (611.0, 22.7, 7280.9, 245.9, 7.9, -4.4, 0.000, 1.020, 1.9)),
            0x31: ((1499.2, 39.8, 821.0, 25.4, 9.4, -4.2, 0.000, 1.701, 1.5), (2457.5, 83.0, 1168.2, 28.9, 3.8, -0.6, 0.000, 1.429, 2.3)),
            0x32: ((1415.8, 52.6, 936.7, 20.5, 7.0, -8.7, 0.000, 1.451, 2.5), (1160.1, 35.9, 6955.2, 215.3, 6.2, -2.3, 0.000, 1.655, 1.4)),
            0x33: ((939.4, 29.1, 1483.1, 55.1, 9.6, -5.5, 0.000, 1.678, 0.8), (1335.1, 35.4, 2656.7, 109.7, 5.8, -0.6, 0.000, 1.655, 2.0)),
            0x34: ((559.9, 17.3, 1423.9, 27.8, 3.4, -3.2, 0.000, 0.635, 1.1), (1483.1, 55.1, 2156.0, 47.1, 7.2, -3.3, 0.000, 1.338, 2.1)),
            0x35: ((697.1, 25.9, 514.1, 11.9, 6.1, -5.0, 0.000, 2.200, 1.6), (7202.9, 243.3, 10260.6, 300.0, 8.0, -2.7, 0.000, 1.020, 1.4)),
            0x36: ((1456.2, 60.1, 713.3, 22.1, 11.6, -2.9, 0.000, 1.746, 2.3), (1052.4, 32.6, 721.4, 33.5, 7.5, -1.7, 0.000, 1.655, 2.5)),
            0x37: ((271.9, 8.4, 686.4, 25.5, 11.8, -4.0, 0.000, 2.290, 1.4), (10344.0, 300.0, 271.9, 9.2, 8.5, -4.0, 0.000, 0.476, 2.5)),
            0x38: ((1133.2, 17.5, 1924.5, 71.5, 17.6, 0.0, 0.000, 0.884, 1.4), (12898.4, 299.5, 6696.8, 276.4, 8.1, -1.9, 0.000, 1.791, 2.5)),
            0x39: ((1036.3, 38.5, 4134.4, 153.6, 12.2, -1.7, 0.000, 2.721, 1.7), (1157.4, 28.7, 2069.9, 33.4, 24.0, -4.0, 0.000, 0.998, 0.8)),
            0x3a: ((1394.3, 57.6, 901.7, 16.8, 5.1, -3.2, 0.000, 5.601, 1.7), (802.1, 19.9, 5065.7, 188.2, 5.7, -4.5, 0.000, 1.338, 2.5)),
            0x3b: ((2724.0, 92.0, 1485.8, 55.2, 3.3, -7.4, 0.000, 0.703, 0.9), (1176.3, 48.6, 13374.8, 300.0, 3.4, -0.3, 0.000, 1.043, 1.5)),
            0x3c: ((1162.8, 28.8, 702.5, 23.7, 24.0, -15.8, 0.000, 2.630, 2.5), (12855.3, 300.0, 1178.9, 24.3, 3.6, -0.6, 0.000, 0.680, 1.0)),
            0x3d: ((2559.8, 95.1, 1434.6, 44.4, 8.1, -7.2, 0.000, 0.862, 1.2), (1065.9, 22.0, 13401.7, 300.0, 5.5, -7.4, 0.000, 1.224, 2.5)),
            0x3e: ((1087.4, 22.4, 557.2, 14.8, 9.8, -18.5, 0.000, 0.794, 1.2), (2514.0, 103.8, 1181.6, 29.3, 4.9, -4.1, 0.000, 0.794, 2.5)),
            0x3f: ((702.5, 29.0, 1421.2, 52.8, 2.1, -3.8, 0.000, 0.839, 0.9), (2312.1, 95.4, 1144.0, 47.2, 8.2, -5.2, 0.000, 0.499, 2.4)),
            0x40: ((2724.0, 72.3, 1510.0, 62.3, 3.3, -0.0, 0.000, 1.655, 0.8), (7208.2, 206.0, 1477.7, 39.2, 6.8, -2.2, 0.000, 1.111, 1.8)),
            0x41: ((1485.8, 55.2, 737.5, 17.1, 4.5, -17.4, 0.000, 0.635, 0.9), (1173.6, 29.1, 3461.5, 107.2, 3.3, -1.5, 0.000, 1.927, 1.8)),
            0x42: ((705.2, 23.8, 2123.7, 78.9, 7.3, -4.8, 0.000, 0.385, 0.9), (549.1, 20.4, 2061.8, 95.8, 6.5, -7.9, 0.000, 0.408, 2.5)),
            0x43: ((1429.3, 48.3, 549.1, 17.0, 11.0, -9.8, 0.000, 0.249, 1.7), (974.4, 25.9, 8244.5, 255.2, 4.5, -5.3, 0.000, 1.315, 2.5)),
            0x44: ((1493.9, 61.7, 543.7, 10.6, 3.3, -11.6, 0.000, 0.249, 1.1), (1413.1, 43.8, 2473.6, 46.0, 3.0, -5.4, 0.000, 0.567, 1.2)),
            0x45: ((707.9, 29.2, 463.0, 17.2, 6.9, -4.0, 0.000, 1.859, 1.2), (681.0, 16.9, 5038.8, 124.8, 5.8, -4.0, 0.000, 1.156, 1.3)),
            0x46: ((1504.6, 46.6, 909.8, 14.7, 9.8, -14.8, 0.000, 0.249, 2.2), (6931.0, 171.7, 2398.3, 99.0, 5.8, -4.9, 0.000, 1.361, 1.7)),
            0x47: ((1413.1, 30.9, 855.9, 22.7, 3.3, -4.0, 0.000, 1.791, 1.7), (2355.2, 79.5, 1197.8, 37.1, 5.0, -4.0, 0.000, 1.293, 1.2)),
            0x48: ((1421.2, 48.0, 694.4, 23.5, 5.7, -4.0, 0.000, 2.018, 0.8), (2026.8, 83.7, 998.6, 30.9, 6.2, -4.0, 0.000, 2.177, 2.5)),
            0x49: ((271.9, 9.2, 697.1, 18.5, 6.7, -4.0, 0.000, 1.338, 1.4), (7243.2, 224.2, 511.4, 15.8, 4.5, -4.0, 0.000, 1.338, 1.5)),
            0x4a: ((463.0, 21.5, 716.0, 22.2, 5.4, -4.0, 0.000, 2.698, 1.7), (697.1, 25.9, 7167.9, 106.5, 4.9, -4.0, 0.000, 1.497, 1.6)),
            0x4b: ((694.4, 21.5, 506.0, 17.1, 8.2, -4.0, 0.000, 0.680, 0.9), (2008.0, 74.6, 1004.0, 24.9, 4.5, -4.0, 0.000, 1.134, 2.4)),
            0x4c: ((6855.6, 195.9, 1004.0, 20.7, 5.5, -4.0, 0.000, 1.247, 1.3), (1938.0, 72.0, 6947.1, 286.8, 3.1, -4.0, 0.000, 1.995, 1.5)),
            0x4d: ((834.4, 28.2, 1402.3, 47.4, 5.7, -4.0, 0.000, 1.633, 1.5), (266.5, 9.0, 2032.2, 83.9, 6.4, -4.0, 0.000, 1.156, 2.5)),
            0x4e: ((740.2, 34.4, 1413.1, 40.4, 7.9, -4.0, 0.000, 3.492, 2.5), (269.2, 9.1, 683.7, 12.7, 6.6, -4.0, 0.000, 1.338, 2.3)),
            0x4f: ((702.5, 23.7, 6852.9, 231.5, 5.1, -4.0, 0.000, 3.583, 1.1), (1353.9, 50.3, 12801.5, 300.0, 6.8, -4.0, 0.000, 1.542, 2.2)),
            0x50: ((748.3, 8.7, 269.2, 8.3, 5.9, -4.0, 0.000, 2.086, 1.2), (10327.9, 300.0, 2102.2, 65.1, 5.1, -4.0, 0.000, 1.156, 2.0)),
            0x51: ((1399.7, 28.9, 977.1, 27.9, 5.9, -4.0, 0.000, 1.247, 1.5), (4142.4, 139.9, 6091.2, 226.3, 5.6, -4.0, 0.000, 1.701, 2.2)),
            0x52: ((705.2, 26.2, 498.0, 15.4, 9.4, -4.0, 0.000, 1.973, 1.5), (468.3, 15.8, 5695.5, 176.3, 8.8, -4.0, 0.000, 0.794, 2.2)),
            0x53: ((686.4, 23.2, 465.7, 19.2, 4.9, -4.0, 0.000, 0.658, 1.0), (2145.2, 79.7, 263.8, 8.9, 10.8, -4.0, 0.000, 1.247, 1.4)),
            0x56: ((2121.0, 71.6, 710.6, 24.0, 2.3, -0.0, 0.000, 1.066, 0.8), (10572.8, 300.0, 2904.3, 107.9, 7.1, -4.0, 0.000, 1.247, 2.5)),
            0x57: ((1475.0, 78.3, 514.1, 21.2, 5.2, -6.7, 0.000, 2.358, 2.5), (925.9, 28.7, 1421.2, 40.6, 7.6, -8.5, 0.000, 2.041, 2.5)),
            0x58: ((1469.6, 36.4, 923.2, 20.2, 14.8, -12.4, 0.000, 11.406, 1.0), (8720.9, 300.0, 2707.8, 100.6, 5.6, -5.7, 0.000, 1.791, 2.2)),
            0x5b: ((1456.2, 38.6, 721.4, 19.1, 5.0, 0.0, 0.000, 1.111, 0.9), (753.7, 23.3, 13226.8, 300.0, 4.0, 0.0, 0.000, 1.361, 2.5)),
            0x60: ((707.9, 20.2, 3426.5, 97.9, 4.4, -4.0, 0.000, 0.703, 0.9), (707.9, 23.9, 5472.1, 112.9, 8.0, -4.0, 0.000, 3.084, 1.5)),
            0x61: ((1426.6, 44.2, 702.5, 32.6, 2.9, 0.0, 0.000, 1.383, 1.3), (691.8, 25.7, 987.8, 36.7, 7.9, -5.8, 0.000, 0.249, 2.3)),
            0x62: ((8346.8, 155.1, 2091.4, 97.1, 6.9, -4.0, 0.000, 2.721, 2.2), (691.8, 23.4, 2896.2, 76.9, 5.0, -4.0, 0.000, 1.179, 2.3)),
            0x63: ((1060.5, 35.8, 1434.6, 59.2, 1.7, -8.1, 0.000, 3.129, 2.5), (1938.0, 72.0, 2907.0, 60.0, 3.7, -4.4, 0.000, 0.862, 1.0)),
            0x64: ((1456.2, 38.6, 721.4, 19.1, 5.0, 0.0, 0.000, 1.111, 0.9), (753.7, 23.3, 13226.8, 300.0, 4.0, 0.0, 0.000, 1.361, 2.5)),
            0x66: ((2549.0, 72.8, 1418.5, 52.7, 8.2, -11.0, 0.000, 1.361, 0.9), (1095.5, 45.2, 9103.2, 300.0, 5.4, -5.1, 0.000, 1.927, 2.5)),
            0x67: ((1388.9, 51.6, 576.0, 23.8, 24.0, -10.2, 0.000, 6.032, 1.8), (3259.6, 46.6, 5251.4, 139.4, 7.2, -0.8, 0.000, 1.497, 1.8)),
            0x68: ((705.2, 26.2, 1122.4, 27.8, 8.4, -11.8, 0.000, 3.741, 2.0), (1127.8, 46.6, 12874.2, 300.0, 7.0, -3.1, 0.000, 0.522, 2.5)),
            0x69: ((721.4, 24.4, 1079.4, 23.6, 11.3, -1.9, 0.000, 1.927, 2.4), (1248.9, 58.0, 2449.4, 82.7, 2.5, 0.0, 0.000, 3.628, 1.0)),
            0x6a: ((1466.9, 60.6, 543.7, 9.6, 10.8, -0.1, 0.000, 1.995, 0.9), (1141.3, 38.5, 705.2, 26.2, 4.2, -1.8, 0.000, 1.247, 2.3)),
            0x6b: ((1429.3, 53.1, 2137.2, 79.4, 4.3, -4.2, 0.000, 1.814, 1.5), (2519.4, 85.1, 1429.3, 75.9, 4.3, -4.2, 0.000, 0.635, 1.3)),
            0x6c: ((716.0, 24.2, 1146.6, 16.4, 19.4, -2.5, 0.000, 0.862, 1.1), (759.0, 23.5, 1423.9, 66.1, 3.1, -2.1, 0.000, 4.921, 1.9)),
            0x6d: ((756.4, 25.5, 1520.8, 56.5, 4.8, -6.0, 0.000, 5.102, 1.5), (1055.1, 32.7, 8844.8, 300.0, 5.9, -5.4, 0.000, 0.884, 2.5)),
            0x6e: ((1434.6, 48.5, 2632.4, 108.7, 10.5, -9.2, 0.000, 4.739, 2.5), (1074.0, 30.7, 2220.6, 82.5, 5.4, -5.5, 0.000, 0.612, 0.8)),
            0x6f: ((1127.8, 32.2, 2199.1, 81.7, 4.6, -5.9, 0.000, 6.485, 1.5), (2142.6, 66.3, 1413.1, 40.4, 8.0, -3.3, 0.000, 2.585, 1.0)),
            0x7d: ((1456.2, 38.6, 721.4, 19.1, 5.0, 0.0, 0.000, 1.111, 0.9), (753.7, 23.3, 13226.8, 300.0, 4.0, 0.0, 0.000, 1.361, 2.5)),
            0xff: ((6105.5, 34.7, 2250.0, 21.3, 2.8, -4.3, 0.000, 0.542, 1.0), (5730.5, 61.1, 703.1, 8.6, 5.3, -8.0, 0.000, 0.812, 1.7)),
        },
        release=ReleaseRule(ring_mul=1.0, peak_delta=0.4, snap_mul=0.3, snap_bw_mul=1.4),
    ),
    # Alps SKCM Orange: 114 keystrokes in the freesound #680714 preview
    # (analysis only — no audio in this repo).  Crisp leaf click with a fast
    # sharp attack (snap ~1.1 ms) and a hollow housing ping; the measured
    # secondary sits inside the guard band of the primary (median ratio 1.29),
    # so the category secondary is placed above it instead.  ring/peak rebased
    # like the other recording-derived profiles.
    "alps": Profile(
        title="Alps",
        categories={
            #                  res1_f  res1_q  res2_f  res2_q  ring   peak   contact snap   snap_bw
            "alpha":        (1190.0,  24.0,   1730.0, 28.0,   7.0,  -7.0,   0.000, 1.10,  1.3),
            "number":       (1160.0,  23.0,   1690.0, 27.0,   7.0,  -7.3,   0.000, 1.05,  1.3),
            "modifier":     (980.0,   21.0,   1420.0, 25.0,   8.5,  -6.4,   0.000, 1.40,  1.5),
            "function":     (1350.0,  27.0,   1960.0, 31.0,   6.0,  -8.0,   0.000, 0.95,  1.1),
            "punctuation":  (1200.0,  24.0,   1740.0, 28.0,   6.8,  -7.1,   0.000, 1.10,  1.3),
            "numpad":       (1250.0,  25.0,   1810.0, 29.0,   6.5,  -7.6,   0.000, 1.05,  1.2),
        },
    ),
}


# ---------------------------------------------------------------------------
# Row emission
# ---------------------------------------------------------------------------


def generate_rows(row_fn) -> list[str]:
    lines = []
    for kc in range(256):
        p = row_fn(kc)
        lines.append(
            "        { "
            f"{p[0]:.1f}f, {p[1]:.1f}f, {p[2]:.1f}f, {p[3]:.1f}f, "
            f"{p[4]:.1f}f, {p[5]:.1f}f, {p[6]:.3f}f, {p[7]:.3f}f, {p[8]:.1f}f"
            + f" }},  // 0x{kc:02x}"
        )
    return lines


def generate_cxx(name: str, profile: Profile) -> str:
    lines = [
        "// Auto-generated by tools/gen-click-profile.py -- DO NOT EDIT.",
        f"// Defines the {profile.title} parameter arrays "
        f"(declared extern in {name}_data.ixx).",
        "",
        "module;",
        "#include <array>",
        "",
        "module fs8.sound;",
        "",
        "import :bucklespring_data;",
        f"import :{name}_data;",
        "",
        "using fs8::click_params;",
        "",
        "// clang-format off",
        "",
        f"const std::array<click_params, 256> fs8::{name}_press_params = {{{{",
    ]
    lines += generate_rows(profile.press_row)
    lines += ["}};", "", f"const std::array<click_params, 256> fs8::{name}_release_params = {{{{"]
    lines += generate_rows(profile.release_row)
    lines += ["}};", "", "// clang-format on", ""]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--profile",
        required=True,
        choices=[*PROFILES, "all"],
        help="which profile's parameter table to (re)generate",
    )
    args = parser.parse_args()

    names = list(PROFILES) if args.profile == "all" else [args.profile]
    sound_dir = Path(__file__).resolve().parent.parent / "sound"

    for name in names:
        profile = PROFILES[name]
        out = sound_dir / f"{name}_data.cxx"
        out.write_text(generate_cxx(name, profile))
        measured = len(profile.measured)
        print(f"Generated {out}")
        print(
            f"  256 press + 256 release parameter sets "
            f"({measured} measured keys, rest category-interpolated)"
        )


if __name__ == "__main__":
    main()
