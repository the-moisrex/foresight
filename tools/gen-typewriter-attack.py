#!/usr/bin/env python3
"""Cut typebar-attack transients from the CC0 typewriter reference.

Reads the CC0 BigSoundBank typewriter recording (48 kHz, stereo/24-bit),
detects isolated typebar strikes, cuts the first ATTACK_MS of each,
peak-normalizes them, and writes:
  - sound/typewriter_attack.ixx  (module partition: struct + extern decls)
  - sound/typewriter_attack.cxx  (module impl: PCM blob + offset table)

Source (CC0 / public domain, license verified per file on
https://bigsoundbank.com on 2026-09-25):
  #2836 "Typewriter 3" — 22 s of manual typewriter typing

Presses pick their cut round-robin (kc % n_cuts); releases use no attack
(the key-return tick is pure synthesis, issue #338: "press = typebar
strike; release = quiet key-return tick").

Usage:
    python3 tools/gen-typewriter-attack.py
    python3 tools/gen-typewriter-attack.py --ref /path/to/typewriter.wav
"""

import argparse
import struct
import sys
import wave
from pathlib import Path

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------

ATTACK_MS = 12.0  # transient length; the engine crossfades into the tail
N_CUTS = 8  # distinct strikes; keys cycle through them round-robin
ISOLATION_GAPS_MS = (250, 200, 150)  # retry with less silence if too few hits
PEAK_NORM = 0.95  # peak-normalize each cut to this level


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


def pick_isolated(onsets: list[int], sr: int, count: int) -> list[int]:
    """Take up to `count` onsets with >= gap of silence around, trying
    progressively smaller gaps until enough strikes qualify."""
    for gap_ms in ISOLATION_GAPS_MS:
        gap = int(sr * gap_ms / 1000.0)
        picked: list[int] = []
        for i, o in enumerate(onsets):
            prev_end = onsets[i - 1] if i > 0 else o - gap
            next_start = onsets[i + 1] if i + 1 < len(onsets) else o + gap
            if o - prev_end >= gap and next_start - o >= gap:
                picked.append(o)
        if len(picked) >= count:
            return picked[:count]
    return picked  # best effort — the caller errors if it is still short


def cut_attack(samples: list[float], sr: int, onset: int, attack_ms: float) -> list[float]:
    """Cut ATTACK_MS from the onset and peak-normalize (no pre-roll: the
    engine aligns the attack sample with the synthetic envelope at t=0)."""
    end = min(len(samples), onset + int(sr * attack_ms / 1000.0))
    cut = samples[onset:end]
    if not cut:
        raise ValueError("empty cut")
    peak = max(abs(s) for s in cut)
    if peak <= 1e-9:
        raise ValueError("silent cut")
    scale = PEAK_NORM / peak
    return [s * scale for s in cut]


def float_to_int16(data: list[float]) -> bytes:
    """Convert float samples in [-1, 1] to int16 PCM bytes."""
    return struct.pack(f"<{len(data)}h", *(max(-32768, min(32767, int(s * 32767.0))) for s in data))


def generate_attack_data(ref: Path, attack_ms: float, n_cuts: int) -> tuple[bytes, list[tuple[int, int]], int]:
    """Cut isolated strikes and build the per-keycode table.

    Returns (blob_bytes, table[(offset, frames) per keycode], sample_rate).
    """
    samples, sr = read_wav_mono_f32(ref)
    onsets = detect_onsets(samples, sr)
    if not onsets:
        raise SystemExit(f"{ref.name}: no onsets detected")
    picked = pick_isolated(onsets, sr, n_cuts)
    if not picked:
        raise SystemExit(f"{ref.name}: no isolated strikes found")
    print(
        f"{ref.name}: {len(onsets)} strikes detected, "
        f"cutting {len(picked)} isolated ones ({attack_ms:g} ms each)",
        file=sys.stderr,
    )

    cuts = [cut_attack(samples, sr, o, attack_ms) for o in picked]

    # Each distinct cut is stored once; the per-keycode table round-robins
    # through the shared (offset, frames) entries.
    cut_pcm = [float_to_int16(cut) for cut in cuts]
    shared: list[tuple[int, int]] = []
    offset = 0
    for pcm in cut_pcm:
        shared.append((offset, len(pcm) // 2))  # frames = bytes / 2 (int16)
        offset += len(pcm)

    table = [shared[kc % len(shared)] for kc in range(256)]
    return b"".join(cut_pcm), table, sr


def generate_ixx(sample_rate: int) -> str:
    rate = f"{sample_rate:,}".replace(",", "'")
    return f"""\
// Auto-generated by tools/gen-typewriter-attack.py -- DO NOT EDIT.
//
// Short attack samples cut from the CC0 typewriter reference recording.
// Each entry holds the first few milliseconds of a real typebar strike —
// the sharp multi-burst transient that is hard to synthesize.  The
// synthetic tail (in typewriter.cxx) crossfades in after this window.
// Releases use no attack: the key-return tick is pure synthesis.

module;
#include <array>
#include <cstdint>

export module fs8.mods:typewriter_attack;

export namespace fs8 {{

    /// Offset + length into the attack PCM blob for one keycode (press only).
    struct [[nodiscard]] typewriter_attack_entry {{
        uint32_t offset;  ///< byte offset into typewriter_attack_blob
        uint16_t frames;  ///< number of mono int16 samples
    }};

    /// Concatenated int16 PCM attack samples (mono, peak-normalized).
    extern uint8_t const typewriter_attack_blob[];
    extern uint32_t const typewriter_attack_blob_size;

    /// Lookup table: [keycode] -> attack_entry (presses; releases are empty).
    /// Keys cycle round-robin through the isolated reference strikes.
    extern std::array<typewriter_attack_entry, 256> const typewriter_attack_table;

    /// Attack sample rate (resampled at render time if needed).
    inline constexpr uint32_t typewriter_attack_rate = {rate};

}} // namespace fs8
"""


def generate_cxx(blob: bytes, table: list[tuple[int, int]]) -> str:
    lines = []
    lines.append("// Auto-generated by tools/gen-typewriter-attack.py -- DO NOT EDIT.")
    lines.append("// Attack samples from the CC0 BigSoundBank typewriter reference (#2836).")
    lines.append("// license verified per file on bigsoundbank.com (2026-09-25): CC0")
    lines.append("//")
    lines.append("// To regenerate:")
    lines.append("//   python3 tools/gen-typewriter-attack.py")
    lines.append("")
    lines.append("module;")
    lines.append("#include <array>")
    lines.append("#include <cstdint>")
    lines.append("")
    lines.append("module fs8.mods;")
    lines.append("")
    lines.append("import :typewriter_attack;")
    lines.append("")
    lines.append("using fs8::typewriter_attack_entry;")
    lines.append("")
    lines.append("// clang-format off")
    lines.append("")

    lines.append("uint8_t const fs8::typewriter_attack_blob[] = {")
    for i in range(0, len(blob), 16):
        chunk = blob[i : i + 16]
        hex_vals = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"    {hex_vals},")
    lines.append("};")
    lines.append(f"uint32_t const fs8::typewriter_attack_blob_size = {len(blob)};")
    lines.append("")

    lines.append("const std::array<typewriter_attack_entry, 256> fs8::typewriter_attack_table = {{")
    for kc, (off, frames) in enumerate(table):
        lines.append(f"    {{ {off}u, {frames} }},  // 0x{kc:02x}")
    lines.append("}};")
    lines.append("")
    lines.append("// clang-format on")
    lines.append("")
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--ref",
        type=Path,
        default=Path.home() / ".cache" / "foresight-sound-refs" / "typewriter_bigsoundbank.wav",
        help="typewriter reference recording (default: the cached BigSoundBank file)",
    )
    parser.add_argument("--attack-ms", type=float, default=ATTACK_MS, help="transient length per cut")
    parser.add_argument("--cuts", type=int, default=N_CUTS, help="number of isolated strikes to cut")
    args = parser.parse_args()

    blob, table, sr = generate_attack_data(args.ref, args.attack_ms, args.cuts)
    sound_dir = Path(__file__).resolve().parent.parent / "sound"

    ixx = sound_dir / "typewriter_attack.ixx"
    ixx.write_text(generate_ixx(sr))
    print(f"Generated {ixx}")

    cxx = sound_dir / "typewriter_attack.cxx"
    cxx.write_text(generate_cxx(blob, table))
    print(f"Generated {cxx}  ({len(blob)} bytes blob, {args.cuts} cuts @ {sr} Hz)")


if __name__ == "__main__":
    main()
