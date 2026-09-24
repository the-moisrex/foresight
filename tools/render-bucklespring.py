#!/usr/bin/env python3
"""Render bucklespring synthesis to WAV and compare against reference recordings.

Usage:
    python3 tools/render-bucklespring.py                  # render all measured keys
    python3 tools/render-bucklespring.py --key 0x1e       # render key A press only
    python3 tools/render-bucklespring.py --key 0x1e -p 0  # render key A release

Produces:
    build/synth/<hex>-<0|1>.wav   — synthetic rendering
    build/synth/compare.txt       — side-by-side spectral comparison
"""

import argparse
import re
import struct
import sys
import wave
from pathlib import Path

import numpy as np

# ---------------------------------------------------------------------------
# Voice parameters extracted from bucklespring_data.cxx
# Format: (primary_freq, primary_q, secondary_freq, secondary_q, ring_ms, peak_dbfs, contact_ms, snap_ms, snap_bw_ms)
# ---------------------------------------------------------------------------

PRESS_PARAMS = {}
RELEASE_PARAMS = {}

def parse_data_file(path: Path) -> None:
    """Parse bucklespring_data.cxx to extract voice parameters."""
    text = path.read_text()

    # Match press_params array
    press_match = re.search(
        r'const std::array<click_params, 256> fs8::bucklespring_press_params = \{\{\s*\n(.*?)\}\};',
        text, re.DOTALL
    )
    release_match = re.search(
        r'const std::array<click_params, 256> fs8::bucklespring_release_params = \{\{\s*\n(.*?)\}\};',
        text, re.DOTALL
    )

    def parse_entries(block: str) -> dict:
        result = {}
        for line in block.strip().splitlines():
            line = line.strip()
            m = re.match(r'\{\s*([\d.f,\s-]+)\s*\},\s*//\s*(0x[0-9a-fA-F]+)', line)
            if m:
                vals = [float(x.strip().rstrip('f')) for x in m.group(1).split(',')]
                keycode = int(m.group(2), 16)
                result[keycode] = tuple(vals)
        return result

    if press_match:
        PRESS_PARAMS.update(parse_entries(press_match.group(1)))
    if release_match:
        RELEASE_PARAMS.update(parse_entries(release_match.group(1)))


# ---------------------------------------------------------------------------
# PRNG — exact port of C++ xorshift32
# ---------------------------------------------------------------------------

class Xorshift32:
    __slots__ = ('state',)

    def __init__(self, seed: int):
        self.state = seed if seed != 0 else 1

    def next(self) -> int:
        self.state ^= (self.state << 13) & 0xFFFFFFFF
        self.state ^= (self.state >> 17) & 0xFFFFFFFF
        self.state ^= (self.state << 5) & 0xFFFFFFFF
        self.state &= 0xFFFFFFFF
        return self.state

    def uniform(self) -> float:
        return float(self.next() & 0x00FFFFFF) / 8388608.0 - 1.0


# ---------------------------------------------------------------------------
# Synthesis — exact port of C++ render()
# ---------------------------------------------------------------------------

TWO_PI = 2.0 * np.pi
RING_MS_SCALE = 7.0  # ~7 decay time-constants (≈ -60 dB), mirrors C++ duration_frames
MS_TO_SEC = 0.001


def db_to_linear(db: float) -> float:
    return 10.0 ** (db / 20.0)


def render_key(keycode: int, pressed: bool, sample_rate: int = 44100) -> np.ndarray:
    """Render a single key event to a mono float32 array.

    Impulse-driven synthesis: single-sample click excites damped sinusoids at
    per-key resonance frequencies. The click provides the sharp transient
    (high crest factor ~27 dB), the damped sinusoids provide the tonal ring.
    """
    params = PRESS_PARAMS if pressed else RELEASE_PARAMS
    if keycode not in params:
        raise ValueError(f"Key 0x{keycode:02x} not found in data")

    v = params[keycode]
    primary_freq, primary_q, secondary_freq, secondary_q, ring_ms, peak_dbfs, contact_ms, snap_ms, _ = v

    gain = db_to_linear(peak_dbfs)
    sr = float(sample_rate)

    # Duration — mirror of bucklespring_synth::duration_frames
    total_ms = snap_ms + ring_ms * RING_MS_SCALE + contact_ms + 1.0
    capped_ms = min(total_ms, 150.0)
    frames = int(sr * capped_ms / 1000.0)

    # Ring decay — envelope time constant is ring_ms (C++ bucklespring.cxx)
    ring_tau = ring_ms * MS_TO_SEC
    ring_tau_inv = 1.0 / ring_tau if ring_tau > 0.0 else 0.0

    # PRNG
    seed = (keycode * 2654435761) & 0xFFFFFFFF
    if pressed:
        seed = (seed + 0x9E3779B9) & 0xFFFFFFFF
    rng = Xorshift32(seed)

    output = np.zeros(frames, dtype=np.float32)

    # Phase 1: single-sample click (maximum sharpness → high crest factor)
    if frames > 0:
        output[0] = gain

    # Phase 2: damped sinusoids at resonance frequencies
    resonances = [
        (primary_freq, gain * 0.1),
        (secondary_freq, gain * 0.1),
    ]

    for freq, amp in resonances:
        if freq <= 0 or freq >= sr / 2:
            continue
        phase = rng.uniform() * TWO_PI
        for i in range(frames):
            t = i / sr
            output[i] += amp * np.sin(TWO_PI * freq * t + phase) * np.exp(-t * ring_tau_inv)

    # Phase 3: minimal noise floor (-60 dBFS)
    for i in range(frames):
        output[i] += rng.uniform() * 0.001 * gain

    return output


# ---------------------------------------------------------------------------
# WAV I/O
# ---------------------------------------------------------------------------

def write_wav(path: Path, data: np.ndarray, sample_rate: int = 44100) -> None:
    """Write mono float32 audio to a 16-bit WAV file."""
    # Clamp and convert to int16
    clipped = np.clip(data, -1.0, 1.0)
    int16_data = (clipped * 32767.0).astype(np.int16)

    with wave.open(str(path), 'wb') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(int16_data.tobytes())


def read_wav_mono(path: Path) -> tuple[np.ndarray, int]:
    """Read a WAV file and return mono float32 + sample rate."""
    with wave.open(str(path), 'rb') as wf:
        sr = wf.getframerate()
        nch = wf.getnchannels()
        sw = wf.getsampwidth()
        raw = wf.readframes(wf.getnframes())

    if sw == 2:
        data = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
    elif sw == 1:
        data = np.frombuffer(raw, dtype=np.uint8).astype(np.float32) / 128.0 - 1.0
    else:
        raise ValueError(f"Unsupported sample width: {sw}")

    if nch > 1:
        data = data[::nch]  # take first channel

    return data, sr


# ---------------------------------------------------------------------------
# Spectral analysis — lightweight comparison metrics
# ---------------------------------------------------------------------------

def spectral_centroid(frame: np.ndarray, sr: int) -> float:
    """Compute spectral centroid of a short frame."""
    fft = np.abs(np.fft.rfft(frame))
    freqs = np.fft.rfftfreq(len(frame), 1.0 / sr)
    total = fft.sum()
    if total < 1e-12:
        return 0.0
    return float(np.sum(freqs * fft) / total)


def spectral_flatness(frame: np.ndarray) -> float:
    """Compute spectral flatness (geometric mean / arithmetic mean)."""
    fft = np.abs(np.fft.rfft(frame)) + 1e-12
    log_mean = np.mean(np.log(fft))
    mean = np.mean(fft)
    return float(np.exp(log_mean) / mean)


def band_energy(data: np.ndarray, sr: int, low_hz: float, high_hz: float) -> float:
    """Compute average energy in a frequency band (dB)."""
    fft = np.abs(np.fft.rfft(data))
    freqs = np.fft.rfftfreq(len(data), 1.0 / sr)
    mask = (freqs >= low_hz) & (freqs < high_hz)
    if not mask.any():
        return -297.0
    band = fft[mask]
    rms = np.sqrt(np.mean(band ** 2))
    if rms < 1e-12:
        return -297.0
    return float(20.0 * np.log10(rms))


def duration_above_threshold(data: np.ndarray, sr: int, threshold_db: float = -40.0) -> float:
    """Find duration in ms where RMS of 1ms windows exceeds threshold."""
    threshold_linear = 10.0 ** (threshold_db / 20.0)
    window = int(sr * 0.001)  # 1ms windows
    for i in range(0, len(data) - window, window):
        rms = np.sqrt(np.mean(data[i:i + window] ** 2))
        if rms < threshold_linear:
            return i / sr * 1000.0
    return len(data) / sr * 1000.0


def analyze(data: np.ndarray, sr: int) -> dict:
    """Compute a summary of spectral characteristics."""
    peak = float(np.max(np.abs(data)))
    peak_db = 20.0 * np.log10(peak) if peak > 1e-12 else -297.0

    dur = duration_above_threshold(data, sr)

    # Centroid at specific times — use zero-padded FFT for short sounds
    def centroid_at(t_ms):
        idx = int(t_ms * sr / 1000.0)
        n = min(1024, len(data) - idx)
        if n < 64:
            return 0.0
        segment = data[idx:idx + n]
        # Zero-pad to 1024 if needed
        if len(segment) < 1024:
            segment = np.pad(segment, (0, 1024 - len(segment)))
        return spectral_centroid(segment, sr)

    bands = {}
    for name, lo, hi in [
        ("0-500Hz", 0, 500), ("500-1000Hz", 500, 1000),
        ("1000-2000Hz", 1000, 2000), ("2000-4000Hz", 2000, 4000),
        ("4000-8000Hz", 4000, 8000), ("8000-16000Hz", 8000, 16000),
    ]:
        bands[name] = band_energy(data, sr, lo, hi)

    return {
        "peak_dbfs": peak_db,
        "duration_above_m40db": dur,
        "centroid_2ms": centroid_at(2.0),
        "centroid_5ms": centroid_at(5.0),
        "centroid_10ms": centroid_at(10.0),
        "bands": bands,
    }


# ---------------------------------------------------------------------------
# Comparison report
# ---------------------------------------------------------------------------

KEY_NAMES = {
    0x01: "ESC", 0x02: "1_KEY", 0x03: "2", 0x04: "3", 0x05: "4",
    0x06: "5", 0x07: "6", 0x08: "7", 0x09: "8", 0x0a: "9", 0x0b: "0",
    0x0c: "MINUS", 0x0d: "EQUALS", 0x0e: "BACKSPACE", 0x0f: "TAB",
    0x10: "Q", 0x11: "W", 0x12: "E", 0x13: "R", 0x14: "T",
    0x15: "Y", 0x16: "U", 0x17: "I", 0x18: "O", 0x19: "P",
    0x1c: "ENTER", 0x1e: "A", 0x1f: "S", 0x20: "D", 0x21: "F",
    0x22: "G", 0x2c: "Z", 0x2d: "X", 0x2e: "C", 0x2f: "V",
    0x30: "B", 0x31: "N", 0x32: "M", 0x39: "SPACE",
}


def format_comparison(keycode: int, ref_analysis: dict, synth_analysis: dict) -> str:
    name = KEY_NAMES.get(keycode, f"0x{keycode:02x}")
    lines = [
        f"\n{'='*70}",
        f"  KEY: {name} (0x{keycode:02x})",
        f"{'='*70}",
        f"  {'Metric':<30s}  {'Reference':>12s}  {'Synthetic':>12s}  {'Delta':>10s}",
        f"  {'-'*30}  {'-'*12}  {'-'*12}  {'-'*10}",
    ]

    def row(label, ref_val, synth_val, unit=""):
        delta = synth_val - ref_val
        return f"  {label:<30s}  {ref_val:>10.1f}{unit}  {synth_val:>10.1f}{unit}  {delta:>+8.1f}{unit}"

    lines.append(row("Peak dBFS", ref_analysis["peak_dbfs"], synth_analysis["peak_dbfs"]))
    lines.append(row("Duration >-40dB (ms)", ref_analysis["duration_above_m40db"], synth_analysis["duration_above_m40db"]))
    lines.append(row("Centroid @2ms (Hz)", ref_analysis["centroid_2ms"], synth_analysis["centroid_5ms"]))
    lines.append(row("Centroid @5ms (Hz)", ref_analysis["centroid_5ms"], synth_analysis["centroid_5ms"]))
    lines.append(row("Centroid @10ms (Hz)", ref_analysis["centroid_10ms"], synth_analysis["centroid_10ms"]))

    lines.append(f"\n  {'Band Energy (dB)':<30s}")
    for band in ["0-500Hz", "500-1000Hz", "1000-2000Hz", "2000-4000Hz", "4000-8000Hz", "8000-16000Hz"]:
        ref_v = ref_analysis["bands"][band]
        syn_v = synth_analysis["bands"][band]
        lines.append(row(f"  {band}", ref_v, syn_v))

    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Render bucklespring synthesis to WAV and compare")
    parser.add_argument("--key", type=str, default=None, help="Keycode hex (e.g. 0x1e). If omitted, render all measured keys.")
    parser.add_argument("-p", "--pressed", type=int, default=1, choices=[0, 1], help="1=press (default), 0=release")
    parser.add_argument("--sr", type=int, default=44100, help="Sample rate")
    parser.add_argument("--wav-dir", type=str, required=True, help="Reference WAV directory")
    args = parser.parse_args()

    # Parse voice parameters
    data_file = Path(__file__).resolve().parent.parent / "sound" / "bucklespring_data.cxx"
    parse_data_file(data_file)
    print(f"Loaded {len(PRESS_PARAMS)} press + {len(RELEASE_PARAMS)} release voices")

    # Output directory
    out_dir = Path(__file__).resolve().parent.parent / "build" / "synth"
    out_dir.mkdir(parents=True, exist_ok=True)

    wav_dir = Path(args.wav_dir)

    # Determine which keys to render
    if args.key:
        keycodes = [int(args.key, 16)]
    else:
        # All measured keys
        keycodes = sorted(PRESS_PARAMS.keys())

    report_lines = ["Bucklespring Synthesis vs Reference — Comparison Report"]

    for kc in keycodes:
        pressed = args.pressed == 1
        suffix = "1" if pressed else "0"
        name = KEY_NAMES.get(kc, f"{kc:02x}")

        # Render synthetic
        synth = render_key(kc, pressed, args.sr)
        synth_path = out_dir / f"{kc:02x}-{suffix}.wav"
        write_wav(synth_path, synth, args.sr)
        print(f"  Rendered synthetic: {synth_path}  ({len(synth)/args.sr*1000:.1f} ms)")

        # Load reference
        ref_name = f"{kc:02x}-{suffix}.wav"
        ref_path = wav_dir / ref_name
        if not ref_path.exists():
            print(f"  WARNING: Reference not found: {ref_path}")
            report_lines.append(f"\n  {name} (0x{kc:02x}): REFERENCE NOT FOUND")
            continue

        ref_data, ref_sr = read_wav_mono(ref_path)

        # Analyze both
        ref_ana = analyze(ref_data, ref_sr)
        synth_ana = analyze(synth, args.sr)

        # Report
        report_lines.append(format_comparison(kc, ref_ana, synth_ana))
        print(f"  Compared: {name} — duration ref={ref_ana['duration_above_m40db']:.1f}ms synth={synth_ana['duration_above_m40db']:.1f}ms")

    # Write report
    report_path = out_dir / "compare.txt"
    report_path.write_text("\n".join(report_lines))
    print(f"\nReport written to: {report_path}")


if __name__ == "__main__":
    main()
