#!/usr/bin/env python3
"""Build the sampled profile's PCM data from CC0 reference recordings.

Reads three CC0 BigSoundBank WAVs (48 kHz mono), detects keystroke onsets,
cuts isolated hits, peak-normalizes them, and writes:
  - sound/sampled_data.ixx  (module partition: slices + extern decls)
  - sound/sampled_data.cxx  (module impl: PCM blob + slice table)

Sources (all CC0 / public-domain, license verified per file on
https://bigsoundbank.com on 2026-09-25):
  #1733 "Slow Keyboard"      - isolated computer-keyboard keystrokes
  #2842 "Typewriter, Key"    - a single Hermes Precisa typewriter key
  #2843 "Typewriter, space"  - typewriter spacebar

The emitted layout (constants exported alongside the blob):
  slices[0 .. normal_count)                 keyboard cuts (per-row select)
  slices[normal_count .. normal_count+type)  typewriter-key cuts
  slices[total - 1]                          typewriter spacebar

Usage:
    python3 tools/gen-sampled.py --ref-dir ~/.cache/foresight-sound-refs-cc0
"""

import argparse
import math
import struct
import sys
import wave
from pathlib import Path

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------

KEYBOARD_ID = "1733"
TYPEKEY_ID = "2842"
SPACE_ID = "2843"

NORMAL_CUTS = 6        # isolated keystrokes to take from the keyboard recording
PREROLL_MS = 4.0       # silence kept before the detected onset
MIN_CUT_MS = 45.0
MAX_CUT_MS = 130.0     # must stay well under the 150 ms player slot
END_SILENCE = 0.06     # envelope level (rel. to cut peak) that ends a cut
FADE_MS = 3.0          # fade-out applied to every cut
PEAK_NORM = 0.50
ISOLATION_GAP_MS = 220  # required silence before/after a "clean" keystroke


def read_wav_mono_f32(path: Path) -> tuple[list[float], int]:
    """Read a PCM WAV (16/24/32-bit, mono/stereo) as mono float32."""
    with wave.open(str(path), "rb") as wf:
        sr = wf.getframerate()
        nch = wf.getnchannels()
        sw = wf.getsampwidth()
        raw = wf.readframes(wf.getnframes())

    if sw == 3:
        import numpy as np

        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
        v = b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16)
        v = np.where(v & 0x800000, v - 0x1000000, v).astype(np.float32) / 8388608.0
        samples = v.tolist()
    elif sw == 2:
        samples = [s / 32768.0 for s in struct.unpack(f"<{len(raw) // 2}h", raw)]
    elif sw == 1:
        samples = [(s / 128.0) - 1.0 for s in raw]
    else:
        raise ValueError(f"Unsupported sample width: {sw}")

    if nch > 1:
        samples = samples[::nch]

    return samples, sr


def detect_onsets(samples: list[float], sr: int) -> list[int]:
    """Sample indices where the amplitude envelope crosses upward."""
    hop = sr // 1000  # 1 ms envelope
    n = len(samples) // hop
    env = [max((abs(s) for s in samples[i * hop : (i + 1) * hop]), default=0.0) for i in range(n)]
    peak = max(env)
    if peak <= 1e-9:
        return []
    thr = peak * 0.06
    onsets: list[int] = []
    i = 1
    while i < n - 1:
        if env[i] > thr and env[i - 1] <= thr:
            onsets.append(i * hop)
            i += 50  # debounce 50 ms
        else:
            i += 1
    return onsets


def cut_hit(samples: list[float], sr: int, onset: int) -> list[float]:
    """Cut pre-roll .. decay point, normalize, and fade the tail out."""
    start = max(0, onset - int(sr * PREROLL_MS / 1000.0))
    window = samples[start : start + int(sr * MAX_CUT_MS / 1000.0)]

    peak = max(abs(s) for s in window)
    if peak <= 1e-9:
        raise ValueError("empty cut")

    # Find the decay point: first ms after MIN_CUT_MS below END_SILENCE.
    hop = sr // 1000
    min_ms = int(MIN_CUT_MS)
    end = len(window)
    for ms in range(min_ms, len(window) // hop):
        if max((abs(s) for s in window[ms * hop : (ms + 1) * hop]), default=0.0) < peak * END_SILENCE:
            end = min(len(window), (ms + 5) * hop)  # +5 ms tail
            break

    cut = window[:end]

    # Peak-normalize + fade-out.
    scale = PEAK_NORM / max(abs(s) for s in cut)
    cut = [s * scale for s in cut]
    fade = min(len(cut), int(sr * FADE_MS / 1000.0))
    for i in range(fade):
        x = (fade - i) / fade
        cut[len(cut) - fade + i] *= 0.5 * (1.0 - math.cos(math.pi * x))
    return cut


def pick_isolated(onsets: list[int], sr: int, count: int) -> list[int]:
    """Take up to `count` onsets with >= ISOLATION_GAP_MS of silence around."""
    gap = int(sr * ISOLATION_GAP_MS / 1000.0)
    picked: list[int] = []
    for i, o in enumerate(onsets):
        prev_end = onsets[i - 1] if i > 0 else o - gap
        next_start = onsets[i + 1] if i + 1 < len(onsets) else o + gap
        if o - prev_end >= gap and next_start - o >= gap:
            picked.append(o)
            if len(picked) == count:
                break
    return picked


def float_to_int16(data: list[float]) -> bytes:
    return struct.pack(f"<{len(data)}h", *(max(-32768, min(32767, int(s * 32767.0))) for s in data))


def generate(ref_dir: Path):
    kb, sr_kb = read_wav_mono_f32(ref_dir / f"{KEYBOARD_ID}.wav")
    ty, sr_ty = read_wav_mono_f32(ref_dir / f"{TYPEKEY_ID}.wav")
    sp, sr_sp = read_wav_mono_f32(ref_dir / f"{SPACE_ID}.wav")
    for name, sr in (("keyboard", sr_kb), ("typewriter key", sr_ty), ("space", sr_sp)):
        if sr != 48000:
            raise SystemExit(f"{name}: expected 48000 Hz, got {sr}")

    cuts: list[tuple[str, list[float]]] = []

    isolated = pick_isolated(detect_onsets(kb, sr_kb), sr_kb, NORMAL_CUTS)
    if len(isolated) < NORMAL_CUTS:
        raise SystemExit(f"only {len(isolated)} isolated keystrokes found (want {NORMAL_CUTS})")
    for o in isolated:
        cuts.append((f"keyboard keystroke @{o / sr_kb:.2f}s", cut_hit(kb, sr_kb, o)))
    normal_count = len(cuts)

    ty_onsets = detect_onsets(ty, sr_ty)
    if not ty_onsets:
        raise SystemExit("no onset in typewriter key recording")
    cuts.append(("typewriter key", cut_hit(ty, sr_ty, ty_onsets[0])))
    type_first = normal_count

    sp_onsets = detect_onsets(sp, sr_sp)
    if not sp_onsets:
        raise SystemExit("no onset in typewriter space recording")
    cuts.append(("typewriter space", cut_hit(sp, sr_sp, sp_onsets[0])))

    blob_parts = [float_to_int16(c) for _, c in cuts]
    slices: list[tuple[int, int]] = []
    offset = 0
    for pcm in blob_parts:
        slices.append((offset, len(pcm) // 2))
        offset += len(pcm)

    blob = b"".join(blob_parts)
    type_count = len(cuts) - normal_count - 1
    space_index = len(cuts) - 1
    return blob, slices, cuts, normal_count, type_first, type_count, space_index


def generate_ixx(normal_count: int, type_first: int, type_count: int, space_index: int) -> str:
    total = normal_count + type_count + 1
    # Pad names so the output byte-matches clang-format's declaration
    # alignment (width = longest constant name + 1 space).
    col = len("sampled_normal_count") + 1
    return f"""\
// Auto-generated by tools/gen-sampled.py -- DO NOT EDIT.
//
// Keystroke samples cut from CC0 reference recordings (no attribution
// required; sources listed in sampled_data.cxx).
//
// To regenerate:
//   python3 tools/gen-sampled.py --ref-dir ~/.cache/foresight-sound-refs-cc0

module;
#include <array>
#include <cstddef>
#include <cstdint>

export module fs8.sound:sampled_data;

export namespace fs8 {{

    /// Byte offset + length (mono int16 frames) of one sample in the blob.
    struct [[nodiscard]] sampled_slice {{
        uint32_t offset;
        uint16_t frames;
    }};

    /// Concatenated int16 PCM samples (mono, 48 kHz, peak-normalized).
    extern uint8_t const  sampled_blob[];
    extern uint32_t const sampled_blob_size;

    /// Slice table; layout documented by the *_count constants below.
    extern std::array<sampled_slice, {total}> const sampled_slices;

    /// Sample rate of every embedded slice.
    inline constexpr uint32_t sampled_rate = 48'000;

    inline constexpr std::size_t {"sampled_total".ljust(col)}= {total};
    /// slices[0 .. sampled_normal_count) -- computer-keyboard keystrokes.
    inline constexpr std::size_t {"sampled_normal_count".ljust(col)}= {normal_count};
    /// slices[sampled_type_first .. + sampled_type_count) -- typewriter keys.
    inline constexpr std::size_t {"sampled_type_first".ljust(col)}= {type_first};
    inline constexpr std::size_t {"sampled_type_count".ljust(col)}= {type_count};
    /// slices[sampled_space_index] -- typewriter spacebar.
    inline constexpr std::size_t {"sampled_space_index".ljust(col)}= {space_index};

}} // namespace fs8
"""


def generate_cxx(blob: bytes, slices, cuts) -> str:
    lines = []
    lines.append("// Auto-generated by tools/gen-sampled.py -- DO NOT EDIT.")
    lines.append("//")
    lines.append("// Keystroke samples cut from CC0 / public-domain recordings,")
    lines.append("// license verified per file on bigsoundbank.com (2026-09-25):")
    lines.append("//   #1733 \"Slow Keyboard\"     - Joseph SARDIN - isolated keystrokes")
    lines.append("//   #2842 \"Typewriter, Key\"   - Joseph SARDIN - Hermes Precisa key")
    lines.append("//   #2843 \"Typewriter, space\" - Joseph SARDIN - typewriter spacebar")
    lines.append("")
    lines.append("module;")
    lines.append("#include <array>")
    lines.append("#include <cstdint>")
    lines.append("")
    lines.append("module fs8.sound;")
    lines.append("")
    lines.append("import :sampled_data;")
    lines.append("")
    lines.append("using fs8::sampled_slice;")
    lines.append("")
    lines.append("// clang-format off")
    lines.append("")
    lines.append("uint8_t const fs8::sampled_blob[] = {")
    for i in range(0, len(blob), 16):
        chunk = blob[i : i + 16]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("};")
    lines.append(f"uint32_t const fs8::sampled_blob_size = {len(blob)};")
    lines.append("")
    lines.append(f"const std::array<sampled_slice, {len(slices)}> fs8::sampled_slices = {{{{")
    for (offset, frames), (label, _) in zip(slices, cuts):
        lines.append(f"    {{ {offset}u, {frames} }},  // {label}")
    lines.append("}};")
    lines.append("")
    lines.append("// clang-format on")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--ref-dir", type=Path, default=Path.home() / ".cache" / "foresight-sound-refs-cc0")
    ap.add_argument("--out-dir", type=Path, default=Path(__file__).resolve().parent.parent / "sound")
    args = ap.parse_args()

    blob, slices, cuts, normal_count, type_first, type_count, space_index = generate(args.ref_dir)

    total_ms = sum(frames for _, frames in slices) / 48.0
    print(f"{len(slices)} slices, blob {len(blob)} bytes ({total_ms:.0f} ms total)")
    for (offset, frames), (label, _) in zip(slices, cuts):
        print(f"  {label:<32} {frames:5d} frames  {frames / 48.0:6.1f} ms  @ {offset}")

    (args.out_dir / "sampled_data.ixx").write_text(
        generate_ixx(normal_count, type_first, type_count, space_index)
    )
    (args.out_dir / "sampled_data.cxx").write_text(generate_cxx(blob, slices, cuts))
    print(f"wrote {args.out_dir / 'sampled_data.ixx'} and sampled_data.cxx")
    return 0


if __name__ == "__main__":
    sys.exit(main())
