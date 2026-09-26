#!/usr/bin/env python3
"""Measure click-profile parameters from reference recordings.

Produces fs8::click_params rows

    (res1_f, res1_q, res2_f, res2_q, ring_ms, peak_dbfs, contact_ms,
     snap_ms, snap_bw_ms)

for the shared click engine, either per key or as aggregate statistics:

  per-key mode    a directory of per-key WAVs named <hex>-<state>.wav
                  (cherrybuckle convention: 0=press, 1=release).  Emits a
                  Python `measured={...}` literal ready to paste into
                  tools/gen-click-profile.py.

  aggregate mode  one or more continuous recordings (typing samples).
                  Detects individual keystrokes and reports median/quartiles
                  per field, to seed a profile's CATEGORIES table.

  structure mode  (--structure) reports the *temporal/spectral structure* of
                  each keystroke instead of click_params rows: how many
                  envelope bumps a press has (strike + bottom-out), their
                  delay and level ratio, plus band-limited decay times
                  (HF "chime" vs body).  Used to decide whether a profile's
                  double-strike / chime / contact layers are actually
                  present in the reference recordings.

  envelope mode   (--envelope) measures the FULL-band envelope decay of each
                  keystroke: the slow-stage time constant (polyfit of the
                  tail between -10 and -45 dB) plus the -20/-40/-60 dB
                  crossing times.  This is what a profile's ring_ms must
                  reproduce -- ring_ms derived from hf/lf *band* decay (the
                  --structure numbers) under-measures the audible tail by an
                  order of magnitude (the typewriter bug: 3.4 ms measured,
                  ~120 ms audible).

WAV/mp3 inputs are converted to 44.1 kHz mono s16 via ffmpeg when needed.

Examples:
    python3 tools/analyze-clicks.py --per-key ~/.cache/foresight-sound-refs/cherrybuckle/wav
    python3 tools/analyze-clicks.py ~/.cache/foresight-sound-refs/realforce_87u.wav
    python3 tools/analyze-clicks.py --structure ~/.cache/foresight-sound-refs/typewriter_bigsoundbank.wav
    python3 tools/analyze-clicks.py --envelope ~/.cache/foresight-sound-refs/typewriter_bigsoundbank.wav
"""

import argparse
import shutil
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

import numpy as np

# Measurement window limits (ms)
SYNTH_RING_MS = 24.0  # measure up to here; the generator's fit_budget cuts to the slot
SYNTH_SNAP_MS = 100.0
SPECTRUM_WINDOW_MS = 60.0
SNAP_BW_CLAMP = (0.2, 2.5)
Q_CLAMP = (1.0, 300.0)
SPECTRUM_BAND = (120.0, 14000.0)
PEAK_PROMINENCE = 2.0  # candidate resonance must stand out from its local skirt
PEAK_MIN_DB = -25.0  # ... and be audible at all


# ---------------------------------------------------------------------------
# Loading
# ---------------------------------------------------------------------------


def load_wav(path: Path) -> tuple[np.ndarray, int]:
    """Load `path` as float32 mono at 44100 Hz (ffmpeg fallback)."""
    try:
        with wave.open(str(path), "rb") as w:
            sr = w.getframerate()
            sw = w.getsampwidth()
            n = w.getnframes()
            raw = w.readframes(n)
        if sw == 2:
            x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
            if w.getnchannels() > 1:
                x = x.reshape(-1, w.getnchannels()).mean(axis=1)
            return x, sr
    except (wave.Error, EOFError):
        pass
    if shutil.which("ffmpeg") is None:
        raise SystemExit(f"{path}: cannot read (no ffmpeg to convert)")
    with tempfile.NamedTemporaryFile(suffix=".wav") as tmp:
        subprocess.run(
            ["ffmpeg", "-v", "error", "-y", "-i", str(path),
             "-ar", "44100", "-ac", "1", "-f", "wav", "-acodec", "pcm_s16le", tmp.name],
            check=True,
        )
        with wave.open(tmp.name, "rb") as w:
            raw = w.readframes(w.getnframes())
        x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
        return x, 44100


def rms_envelope(x: np.ndarray, sr: int, win_ms: float = 0.5) -> np.ndarray:
    n = max(1, int(sr * win_ms / 1000.0))
    kernel = np.ones(n) / n
    return np.sqrt(np.convolve(x * x, kernel, mode="same"))


# ---------------------------------------------------------------------------
# Measurement core
# ---------------------------------------------------------------------------


def measure_window(x: np.ndarray, sr: int, ref_peak: float | None = None) -> tuple[float, ...] | None:
    """Measure one click window (starts at the event onset).

    ref_peak: full-scale reference of the source recording.  Aggregate mode
    passes the file peak so peak_dbfs becomes event-relative (recording gain
    differences between files cancel out); per-key mode omits it and keeps
    absolute dBFS, which stays comparable within one recording rig.
    """
    if x.size < 16:
        return None
    env = rms_envelope(x, sr)
    peak_env = float(env.max())
    if peak_env <= 1e-7:
        return None

    window_peak = float(np.abs(x).max())
    peak_dbfs = 20.0 * np.log10(max(window_peak / (ref_peak or 1.0), 1e-9))

    # The engine treats contact_ms as a pre-delay (silence before the attack)
    # and snap_ms as absolute from t=0, so every measurement is re-based to the
    # event's own onset: the start of the final rise into the envelope peak.
    # Aggregate windows carry the previous keystroke's tail; without the
    # re-base that tail would turn into an arbitrary attack delay.
    snap_idx = int(np.argmax(env))
    below = np.flatnonzero(env[:snap_idx] < 0.10 * env[snap_idx])
    onset_idx = int(below[-1]) + 1 if below.size else 0
    contact_ms = 0.0
    snap_ms = min((snap_idx - onset_idx) * 1000.0 / sr, SYNTH_SNAP_MS)

    # snap burst width: full width at half maximum of the envelope peak
    half = 0.5 * env[snap_idx]
    left = snap_idx
    while left > 0 and env[left] > half:
        left -= 1
    right = snap_idx
    while right + 1 < env.size and env[right] > half:
        right += 1
    snap_bw_ms = float(np.clip((right - left) * 1000.0 / sr, *SNAP_BW_CLAMP))

    # ring: exp fit of the envelope decay after the snap.  Walk the contiguous
    # decay segment only (the whole tail would drag in the noise floor and
    # flatten the slope); the floor itself is estimated as the file median.
    seg = env[snap_idx:]
    t = np.arange(seg.size) / sr
    floor = float(np.median(env))
    lower = max(peak_env * 0.05, floor * 3.0)
    ring_ms = SYNTH_RING_MS
    first = np.flatnonzero(seg <= peak_env / np.e)
    if first.size:
        pts = []
        i = int(first[0])
        while i < seg.size and seg[i] >= lower and t[i] <= 0.080:
            pts.append(i)
            i += 1
        if len(pts) >= 4:
            pts = np.asarray(pts)
            slope = float(np.polyfit(t[pts], np.log(seg[pts]), 1)[0])
            if slope < -1e-9:
                ring_ms = float(np.clip(-1000.0 / slope, 0.5, SYNTH_RING_MS))

    # spectrum of the attack + early ring: two strongest separated peaks
    start = onset_idx
    end = min(x.size, start + int(SPECTRUM_WINDOW_MS * sr / 1000.0))
    seg = x[start:end] * np.hanning(end - start)
    # next power of two, then 4x zero-padding for fine frequency resolution
    nfft = (1 << int(np.ceil(np.log2(max(seg.size, 64))))) * 4
    spec = np.abs(np.fft.rfft(seg, nfft))
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)
    band = (freqs >= SPECTRUM_BAND[0]) & (freqs <= SPECTRUM_BAND[1])
    spec_b, freqs_b = spec[band], freqs[band]
    if spec_b.size < 8 or spec_b.max() <= 0.0:
        return None

    def bandwidth(idx: int) -> float:
        half_peak = spec_b[idx] / np.sqrt(2.0)
        lo = idx
        while lo > 0 and spec_b[lo] > half_peak:
            lo -= 1
        hi = idx
        while hi + 1 < spec_b.size and spec_b[hi] > half_peak:
            hi += 1
        bw = freqs_b[hi] - freqs_b[lo]
        return bw if bw > 0.0 else freqs_b[1] - freqs_b[0]

    # candidate resonances: local maxima that stand out from their local skirt
    # (prominence) and are actually audible (absolute level).  Ordering by
    # magnitude keeps the loudest partial first; the skirt test stops the
    # 1/f noise floor from winning on absolute level alone.  Silenced boards
    # have smooth spectra, so prominence requirements relax before giving up.
    is_peak = np.zeros(spec_b.size, dtype=bool)
    is_peak[1:-1] = (spec_b[1:-1] > spec_b[:-2]) & (spec_b[1:-1] >= spec_b[2:])
    min_mag = spec_b.max() * 10.0 ** (PEAK_MIN_DB / 20.0)

    def candidates(min_prom: float) -> list[tuple[float, float, int]]:
        out: list[tuple[float, float, int]] = []  # (magnitude, prominence, index)
        for i in np.flatnonzero(is_peak & (spec_b >= min_mag)):
            skirt = (freqs_b > freqs_b[i] * 0.8) & (freqs_b[i] * 1.2 > freqs_b) & (~is_peak)
            if int(skirt.sum()) < 4:
                continue
            mean = float(spec_b[skirt].mean())
            if mean <= 0.0:
                continue
            prom = float(spec_b[i]) / mean
            if prom >= min_prom:
                out.append((float(spec_b[i]), prom, int(i)))
        out.sort(key=lambda r: (-r[0], -r[1]))
        return out

    scored: list[tuple[float, float, int]] = []
    for min_prom in (PEAK_PROMINENCE, 1.5, 1.2, 1.0):
        scored = candidates(min_prom)
        if scored:
            break
    if not scored:  # completely smooth spectrum: strongest in-band interior bin
        i1 = int(np.argmax(spec_b[1:-1])) + 1
        scored = [(float(spec_b[i1]), 1.0, i1)]

    f1 = q1 = f2 = q2 = float("nan")
    i1 = scored[0][2]
    f1 = float(freqs_b[i1])
    q1 = float(np.clip(f1 / bandwidth(i1), *Q_CLAMP))
    guard = (0.75 * f1, 1.35 * f1)
    for _, _, i in scored[1:]:
        f = float(freqs_b[i])
        if not (guard[0] <= f <= guard[1]):
            f2 = f
            q2 = float(np.clip(f2 / bandwidth(i), *Q_CLAMP))
            break
    if np.isnan(f2):  # no separated second mode: mirror below f1
        f2 = f1 * 0.55
        q2 = q1 * 0.6

    return (
        round(f1, 1), round(q1, 1), round(f2, 1), round(q2, 1),
        round(ring_ms, 1), round(peak_dbfs, 1),
        round(contact_ms, 3), round(snap_ms, 3), round(snap_bw_ms, 1),
    )


# ---------------------------------------------------------------------------
# Structure mode: envelope multi-peak + band-decay analysis
# ---------------------------------------------------------------------------

STRUCT_WIN_MS = 2.0  # envelope smoothing (tames 1-2 kHz ripple, keeps >=5 ms bumps)
STRUCT_MIN_SEP_MS = 3.0  # two bumps closer than this are one bump
STRUCT_REL_HEIGHT = 0.08  # bump must reach 8% of the event peak
STRUCT_REL_PROM = 0.12  # ... and stand >=12% of the event peak above its basin
STRUCT_HF_BAND = (3500.0, 9500.0)
STRUCT_LF_BAND = (150.0, 1200.0)
STRUCT_TAIL_MS = 45.0  # spectrum window after strike 1 for the HF tone search
STRUCT_HF_MIN_RATIO = 0.01  # HF tone must carry >=1% of the window spectrum


def envelope_peaks(env: np.ndarray, sr: int) -> list[int]:
    """Local maxima of a smoothed envelope that look like separate bumps."""
    if env.size < 5:
        return []
    idx = np.flatnonzero((env[1:-1] > env[:-2]) & (env[1:-1] >= env[2:])) + 1
    peak = float(env.max())
    if peak <= 1e-9:
        return []
    min_sep = int(STRUCT_MIN_SEP_MS * sr / 1000.0)
    kept: list[int] = []
    for p in sorted((int(i) for i in idx if env[i] >= STRUCT_REL_HEIGHT * peak), key=lambda i: -env[i]):
        h = float(env[p])
        l = p
        while l > 0 and env[l - 1] <= h:
            l -= 1
        r = p
        while r + 1 < env.size and env[r + 1] <= h:
            r += 1
        basin = max(float(env[l : p + 1].min()), float(env[p : r + 1].min()))
        if h - basin >= STRUCT_REL_PROM * peak and all(abs(p - q) >= min_sep for q in kept):
            kept.append(p)
    return sorted(kept)


def band_envelope(x: np.ndarray, sr: int, lo: float, hi: float) -> np.ndarray:
    spec = np.fft.rfft(x)
    freqs = np.fft.rfftfreq(x.size, 1.0 / sr)
    spec[(freqs < lo) | (freqs > hi)] = 0
    return rms_envelope(np.fft.irfft(spec, x.size), sr, win_ms=STRUCT_WIN_MS)


def decay_tau_ms(env: np.ndarray, sr: int) -> float:
    """Exp decay time constant of `env` after its peak (contiguous walk)."""
    pk = float(env.max())
    floor = float(np.median(env))
    lower = max(pk * 0.05, floor * 3.0)
    p = int(np.argmax(env))
    seg = env[p:]
    t = np.arange(seg.size) / sr
    first = np.flatnonzero(seg <= pk / np.e)
    if not first.size:
        return float("nan")
    pts: list[int] = []
    i = int(first[0])
    while i < seg.size and seg[i] >= lower and t[i] <= 0.060:
        pts.append(i)
        i += 1
    if len(pts) < 4:
        return float("nan")
    pts_a = np.asarray(pts)
    slope = float(np.polyfit(t[pts_a], np.log(seg[pts_a]), 1)[0])
    if slope >= -1e-9:
        return float("nan")
    return float(np.clip(-1000.0 / slope, 0.5, 200.0))


def hf_tone_hz(x: np.ndarray, sr: int, start: int) -> tuple[float, float]:
    """Dominant HF tone after strike 1: (freq, energy ratio)."""
    end = min(x.size, start + int(STRUCT_TAIL_MS * sr / 1000.0))
    if end - start < 64:
        return float("nan"), 0.0
    seg = x[start:end] * np.hanning(end - start)
    nfft = (1 << int(np.ceil(np.log2(seg.size)))) * 4
    spec = np.abs(np.fft.rfft(seg, nfft))
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)
    total = float(spec[(freqs >= 100.0) & (freqs <= 14000.0)].sum())
    hf = (freqs >= STRUCT_HF_BAND[0]) & (freqs <= STRUCT_HF_BAND[1])
    if total <= 0.0 or not hf.any():
        return float("nan"), 0.0
    i = int(np.argmax(spec[hf]))
    hf_freqs = freqs[hf]
    hf_spec = spec[hf]
    ratio = float(hf_spec.sum() / total)
    return float(hf_freqs[i]), ratio


def measure_structure(x: np.ndarray, sr: int) -> dict[str, float] | None:
    if x.size < 64:
        return None
    env = rms_envelope(x, sr, win_ms=STRUCT_WIN_MS)
    pk = float(env.max())
    if pk <= 1e-7:
        return None
    peaks = envelope_peaks(env, sr)
    if not peaks:
        return None

    p1 = peaks[0]
    # onset: walk back from strike 1 to its own 5% base
    base = 0.05 * float(env[p1])
    a = p1
    while a > 0 and env[a - 1] >= base:
        a -= 1
    # ... but the window may open on the previous keystroke's tail: if the
    # leading floor is already loud, keep only the final descent.
    out: dict[str, float] = {"n_peaks": float(len(peaks)), "strike1_ms": (p1 - a) * 1000.0 / sr}
    if len(peaks) >= 2:
        p2 = peaks[1]
        out["strike2_delay_ms"] = (p2 - p1) * 1000.0 / sr
        out["strike2_ratio"] = float(env[p2]) / float(env[p1])

    out["hf_tau_ms"] = decay_tau_ms(band_envelope(x, sr, *STRUCT_HF_BAND), sr)
    out["lf_tau_ms"] = decay_tau_ms(band_envelope(x, sr, *STRUCT_LF_BAND), sr)
    hf_freq, hf_ratio = hf_tone_hz(x, sr, p1)
    out["hf_freq_hz"] = hf_freq if hf_ratio >= STRUCT_HF_MIN_RATIO else float("nan")
    out["hf_ratio"] = hf_ratio
    return out


def report_structure(events: list[dict[str, float]]) -> None:
    multi = sum(1 for e in events if e.get("n_peaks", 0) >= 2)
    print(
        f"# structure: {len(events)} events, "
        f"{multi} multi-peak ({100.0 * multi / max(len(events), 1):.0f}%)",
        file=sys.stderr,
    )
    fields = ["strike1_ms", "strike2_delay_ms", "strike2_ratio", "hf_tau_ms", "lf_tau_ms", "hf_freq_hz", "hf_ratio"]
    print(f"# {'field':<18} {'median':>10} {'q25':>10} {'q75':>10} {'n':>5}", file=sys.stderr)
    for name in fields:
        vals = np.array([e[name] for e in events if name in e and np.isfinite(e[name])])
        if not vals.size:
            print(f"# {name:<18} {'-':>10}", file=sys.stderr)
            continue
        print(
            f"# {name:<18} {np.median(vals):>10.3f} {np.percentile(vals, 25):>10.3f} "
            f"{np.percentile(vals, 75):>10.3f} {vals.size:>5d}",
            file=sys.stderr,
        )


def run_structure(paths: list[Path], per_key: Path | None) -> None:
    events: list[dict[str, float]] = []
    if per_key is not None:
        # press files only: each file is one keystroke
        files = sorted(p for p in per_key.glob("*-0.wav"))
        for p in files:
            x, sr = load_wav(p)
            ev = measure_structure(x, sr)
            if ev is not None:
                events.append(ev)
        print(f"# {per_key}: {len(events)} press events", file=sys.stderr)
    for path in paths:
        x, sr = load_wav(path)
        env = rms_envelope(x, sr, win_ms=1.0)
        file_peak = float(env.max())
        if file_peak <= 1e-7:
            print(f"# {path.name}: silent", file=sys.stderr)
            continue
        kept = 0
        for a, b in detect_events(env, sr, file_peak):
            ev = measure_structure(x[a:b], sr)
            if ev is not None:
                events.append(ev)
                kept += 1
        print(f"# {path.name}: {kept} events", file=sys.stderr)
    if not events:
        raise SystemExit("no events measured")
    report_structure(events)


# ---------------------------------------------------------------------------
# Envelope mode (--envelope): full-band decay -> ring_ms source of truth
# ---------------------------------------------------------------------------


def slow_tau_ms(env: np.ndarray, sr: int) -> float:
    """Slow-stage exp decay constant of `env` after its peak (ms).

    Fits log(env) between the -10 dB and -45 dB crossings, skipping the fast
    modal die-off (first ~10 dB) and the attack; falls back to the deepest
    part of the tail when -45 dB is never reached inside the window.
    """
    pk = float(env.max())
    if pk <= 1e-9:
        return float("nan")
    seg = env[int(np.argmax(env)):].astype(np.float64)
    if seg.size < 8:
        return float("nan")
    db = 20.0 * np.log10(np.maximum(seg / pk, 1e-6))

    below10 = np.flatnonzero(db <= -10.0)
    if not below10.size:
        return float("nan")
    i1 = int(below10[0])
    below45 = np.flatnonzero(db <= -45.0)
    if below45.size:
        i2 = int(below45[0])
    elif float(db.min()) <= -20.0:
        i2 = int(db.size - 1)  # window cut the tail short: fit what we have
    else:
        return float("nan")
    if i2 - i1 < 4:
        return float("nan")

    t = np.arange(i1, i2 + 1, dtype=np.float64) / sr
    y = np.log(np.maximum(seg[i1 : i2 + 1], pk * 1e-6))
    slope = float(np.polyfit(t, y, 1)[0])
    if slope >= -1e-9:
        return float("nan")
    return float(np.clip(-1000.0 / slope, 0.5, 400.0))


def measure_envelope(env: np.ndarray, sr: int) -> dict[str, float] | None:
    """Envelope-decay stats of one keystroke window (peak as t=0 reference)."""
    if env.size < 64:
        return None
    pk = float(env.max())
    if pk <= 1e-7:
        return None
    seg = env[int(np.argmax(env)):].astype(np.float64)
    db = 20.0 * np.log10(np.maximum(seg / pk, 1e-6))

    def cross(level: float) -> float:
        idx = np.flatnonzero(db <= level)
        return float(idx[0]) * 1000.0 / sr if idx.size else float("nan")

    return {
        "env_tau_ms": slow_tau_ms(env, sr),
        "t20_ms": cross(-20.0),
        "t40_ms": cross(-40.0),
        "t60_ms": cross(-60.0),
    }


def report_envelope(events: list[dict[str, float]]) -> None:
    fields = ["env_tau_ms", "t20_ms", "t40_ms", "t60_ms"]
    print(f"# envelope: {len(events)} events", file=sys.stderr)
    print(f"# {'field':<12} {'median':>10} {'q25':>10} {'q75':>10} {'n':>5}", file=sys.stderr)
    for name in fields:
        vals = np.array([e[name] for e in events if name in e and np.isfinite(e[name])])
        if not vals.size:
            print(f"# {name:<12} {'-':>10}", file=sys.stderr)
            continue
        print(
            f"# {name:<12} {np.median(vals):>10.1f} {np.percentile(vals, 25):>10.1f} "
            f"{np.percentile(vals, 75):>10.1f} {vals.size:>5d}",
            file=sys.stderr,
        )
    tau = np.array([e["env_tau_ms"] for e in events if np.isfinite(e["env_tau_ms"])])
    if tau.size:
        med = float(np.median(tau))
        print(
            f"# ring_ms target: median slow-stage tau {med:.1f} ms "
            f"(generator budget caps ring near 21 ms)",
            file=sys.stderr,
        )


def run_envelope(paths: list[Path], per_key: Path | None) -> None:
    events: list[dict[str, float]] = []
    if per_key is not None:
        files = sorted(p for p in per_key.glob("*-0.wav"))
        for p in files:
            x, sr = load_wav(p)
            ev = measure_envelope(rms_envelope(x, sr, win_ms=1.0), sr)
            if ev is not None:
                events.append(ev)
        print(f"# {per_key}: {len(events)} press events", file=sys.stderr)
    for path in paths:
        x, sr = load_wav(path)
        env = rms_envelope(x, sr, win_ms=1.0)
        file_peak = float(env.max())
        if file_peak <= 1e-7:
            print(f"# {path.name}: silent", file=sys.stderr)
            continue
        kept = 0
        for a, b in detect_events(env, sr, file_peak):
            ev = measure_envelope(env[a:b], sr)
            if ev is not None:
                events.append(ev)
                kept += 1
        print(f"# {path.name}: {kept} events", file=sys.stderr)
    if not events:
        raise SystemExit("no events measured")
    report_envelope(events)


# ---------------------------------------------------------------------------
# Per-key mode
# ---------------------------------------------------------------------------


def run_per_key(dirpath: Path) -> None:
    files = sorted(p for p in dirpath.glob("*.wav") if p.stem.endswith(("-0", "-1")))
    if not files:
        raise SystemExit(f"{dirpath}: no <hex>-<state>.wav files found")

    rows: dict[int, dict[int, tuple[float, ...]]] = {}
    failures: list[str] = []
    for p in files:
        code_s, state_s = p.stem.rsplit("-", 1)
        try:
            keycode = int(code_s, 16)
        except ValueError:
            failures.append(f"{p.name}: bad keycode")
            continue
        x, sr = load_wav(p)
        onset = int(np.argmax(rms_envelope(x, sr) > 0.02))
        row = measure_window(x[onset:], sr)
        if row is None:
            failures.append(f"{p.name}: unmeasurable")
            continue
        rows.setdefault(keycode, {})[int(state_s)] = row

    print(f"# measured {len(rows)} keycodes from {dirpath}", file=sys.stderr)
    for f in failures:
        print(f"# skipped {f}", file=sys.stderr)

    def fmt(row: tuple[float, ...]) -> str:
        body = ", ".join(
            f"{v:.3f}" if i in (6, 7) else f"{v:.1f}" for i, v in enumerate(row)
        )
        return f"({body})"

    print("measured={")
    for kc in sorted(rows):
        press = rows[kc].get(0)
        release = rows[kc].get(1)
        if press is None:
            continue
        press_s = fmt(press)
        release_s = fmt(release) if release is not None else "None"
        print(f"    0x{kc:02x}: ({press_s}, {release_s}),")
    print("}")


# ---------------------------------------------------------------------------
# Aggregate mode
# ---------------------------------------------------------------------------


def detect_events(env: np.ndarray, sr: int, file_peak: float) -> list[tuple[int, int]]:
    """Return (start, end) windows for individual keystrokes.

    Peaks are found by threshold crossings; each event's window is bounded by
    the midpoints to the neighbouring peaks, so a window can never contain the
    next keystroke (which would otherwise move the envelope argmax to it).
    """
    thr = 0.06 * file_peak
    min_gap = int(0.030 * sr)
    peaks: list[int] = []
    i = 0
    while i < env.size:
        if env[i] > thr:
            j = i + int(np.argmax(env[i:min(env.size, i + min_gap)]))
            peaks.append(j)
            i = j + min_gap
            while i < env.size and env[i] > thr:  # skip the rest of this plateau
                i += 1
        else:
            i += 1

    windows: list[tuple[int, int]] = []
    for k, p in enumerate(peaks):
        if env[p] < 0.10 * file_peak:  # drop taps far below the recording's level
            continue
        a = 0 if k == 0 else (peaks[k - 1] + p) // 2
        b = env.size if k == len(peaks) - 1 else (p + peaks[k + 1]) // 2
        windows.append((a, b))
    return windows


def run_aggregate(paths: list[Path]) -> None:
    all_rows: list[tuple[float, ...]] = []
    for path in paths:
        x, sr = load_wav(path)
        env = rms_envelope(x, sr, win_ms=1.0)
        file_peak = float(env.max())
        if file_peak <= 1e-7:
            print(f"# {path.name}: silent", file=sys.stderr)
            continue
        windows = detect_events(env, sr, file_peak)
        ref = float(np.abs(x).max())
        kept = 0
        for a, b in windows:
            row = measure_window(x[a:b], sr, ref_peak=ref)
            if row is not None:
                all_rows.append(row)
                kept += 1
        print(f"# {path.name}: {kept}/{len(windows)} events measured", file=sys.stderr)

    if not all_rows:
        raise SystemExit("no events measured")

    arr = np.array(all_rows)
    fields = [
        "res1_f", "res1_q", "res2_f", "res2_q",
        "ring_ms", "peak_dbfs", "contact_ms", "snap_ms", "snap_bw_ms",
    ]
    med = np.median(arr, axis=0)
    q25 = np.percentile(arr, 25, axis=0)
    q75 = np.percentile(arr, 75, axis=0)
    print(f"# {len(all_rows)} events from {len(paths)} recording(s)", file=sys.stderr)
    print(f"# {'field':<12} {'median':>10} {'q25':>10} {'q75':>10}", file=sys.stderr)
    for i, name in enumerate(fields):
        print(f"# {name:<12} {med[i]:>10.3f} {q25[i]:>10.3f} {q75[i]:>10.3f}", file=sys.stderr)

    body = ", ".join(f"{v:.1f}" if i not in (6, 7) else f"{v:.3f}" for i, v in enumerate(med))
    print(f"    # median row -> paste as a category value:")
    print(f"    ({body}),")


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--per-key", type=Path, metavar="DIR",
                        help="directory of <hex>-<state>.wav files (cherrybuckle convention)")
    parser.add_argument("--structure", action="store_true",
                        help="report envelope-structure statistics (strikes, band decays) instead of click_params")
    parser.add_argument("--envelope", action="store_true",
                        help="report full-envelope decay statistics (slow-stage tau, dB crossings) for ring_ms")
    parser.add_argument("recordings", nargs="*", type=Path,
                        help="continuous recordings for aggregate statistics")
    args = parser.parse_args()

    if args.envelope:
        run_envelope(args.recordings, args.per_key)
    elif args.structure:
        run_structure(args.recordings, args.per_key)
    elif args.per_key:
        run_per_key(args.per_key)
    elif args.recordings:
        run_aggregate(args.recordings)
    else:
        parser.error("pass --per-key DIR or one or more recordings")


if __name__ == "__main__":
    main()
