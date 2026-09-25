#!/usr/bin/env python3
"""Compare dumped profile WAVs (tests/sound_test.cxx, FS8_DUMP_DIR).

Two modes:

  matrix DIR           pairwise similarity matrix of the profiles in DIR.
                       Each profile is summarised by median features over
                       its dumped key events (attack time, decay time,
                       spectral centroid, band shares, level); the matrix
                       shows normalised feature distances (larger = more
                       distinct by ear-relevant measures).

  --diff DIRA DIRB     per-profile feature deltas between two dumps, used
                       to prove which profiles an engine change touched.

WAVs are mono 16-bit, produced by the DumpProfilesWhenRequested test.

Examples:
    python3 tools/profile-matrix.py /tmp/sound-new
    python3 tools/profile-matrix.py --diff /tmp/sound-base /tmp/sound-new
"""

import argparse
import sys
import wave
from pathlib import Path

import numpy as np

WIN_MS = 1.0


def load(path: Path) -> tuple[np.ndarray, int]:
    with wave.open(str(path), "rb") as w:
        sr = w.getframerate()
        raw = w.readframes(w.getnframes())
    return np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0, sr


def envelope(x: np.ndarray, sr: int) -> np.ndarray:
    n = max(1, int(sr * WIN_MS / 1000.0))
    return np.sqrt(np.convolve(x * x, np.ones(n) / n, mode="same"))


def features(path: Path) -> dict[str, float] | None:
    x, sr = load(path)
    if x.size < 64 or float(np.abs(x).max()) <= 1e-6:
        return None
    env = envelope(x, sr)
    pk = float(env.max())
    peak_i = int(np.argmax(env))

    # attack: 10% -> 90% of the envelope peak
    pre = env[: peak_i + 1]
    above10 = np.flatnonzero(pre >= 0.1 * pk)
    above90 = np.flatnonzero(pre >= 0.9 * pk)
    attack_ms = (int(above90[0]) - int(above10[0])) * 1000.0 / sr if above10.size and above90.size else 0.0

    # decay: envelope fall from peak to -20 dB (or window end)
    post = env[peak_i:]
    below = np.flatnonzero(post <= pk * 0.1)
    decay_ms = (int(below[0]) * 1000.0 / sr) if below.size else (post.size * 1000.0 / sr)

    # spectrum
    spec = np.abs(np.fft.rfft(x))
    freqs = np.fft.rfftfreq(x.size, 1.0 / sr)
    band = (freqs >= 50.0) & (freqs <= 16000.0)
    spec, freqs = spec[band], freqs[band]
    total = float(spec.sum())
    if total <= 0.0:
        return None
    centroid = float((freqs * spec).sum() / total)
    lf = float(spec[freqs < 600.0].sum() / total)
    hf = float(spec[freqs > 2500.0].sum() / total)

    return {
        "attack_ms": attack_ms,
        "decay_ms": decay_ms,
        "centroid_hz": centroid,
        "lf_share": lf,
        "hf_share": hf,
        "rms_db": 20.0 * np.log10(max(float(np.sqrt(np.mean(x * x))), 1e-9)),
        "dur_ms": x.size * 1000.0 / sr,
    }


FEATURES = ["attack_ms", "decay_ms", "centroid_hz", "lf_share", "hf_share", "rms_db", "dur_ms"]


def profile_features(dump: Path) -> dict[str, dict[str, float]]:
    per_profile: dict[str, list[dict[str, float]]] = {}
    for p in sorted(dump.glob("*.wav")):
        f = features(p)
        if f is not None:
            per_profile.setdefault(p.stem.rsplit("_", 2)[0], []).append(f)
    return {name: {k: float(np.median([f[k] for f in rows])) for k in FEATURES} for name, rows in per_profile.items()}


def matrix(dump: Path) -> None:
    prof = profile_features(dump)
    names = sorted(prof)
    if len(names) < 2:
        raise SystemExit("need at least two profiles")
    mat = np.array([[prof[n][k] for k in FEATURES] for n in names])
    std = mat.std(axis=0)
    std[std < 1e-9] = 1.0
    z = (mat - mat.mean(axis=0)) / std

    print(f"# features (median over events): {' '.join(FEATURES)}", file=sys.stderr)
    print(f"# {'profile':<13} " + " ".join(f"{k:>11}" for k in FEATURES), file=sys.stderr)
    for i, n in enumerate(names):
        print(f"# {n:<13} " + " ".join(f"{mat[i, j]:>11.1f}" for j in range(len(FEATURES))), file=sys.stderr)

    width = max(len(n) for n in names) + 1
    colw = max(max(len(n) for n in names), 4)
    print(" " * width + " ".join(f"{n:>{colw}}" for n in names))
    for i, n in enumerate(names):
        cells = []
        for j, m in enumerate(names):
            d = float(np.linalg.norm(z[i] - z[j]))
            cells.append("   -  " if i == j else f"{d:5.2f} ")
        print(f"{n:<{width}}" + " ".join(cells))
    print("# normalised feature distance (0 = identical signature, larger = more distinct)", file=sys.stderr)


def diff(dir_a: Path, dir_b: Path) -> None:
    a, b = profile_features(dir_a), profile_features(dir_b)
    print(f"# {'profile':<13} " + " ".join(f"{k:>11}" for k in FEATURES))
    for name in sorted(set(a) | set(b)):
        if name not in a or name not in b:
            print(f"{name:<13} missing in one dump")
            continue
        cells = " ".join(f"{b[name][k] - a[name][k]:>+11.1f}" for k in FEATURES)
        print(f"{name:<13} {cells}")
    print("# deltas (dir_b - dir_a); 0.0 columns = unchanged", file=sys.stderr)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dump", nargs="?", type=Path, help="dump directory (matrix mode)")
    parser.add_argument("--diff", nargs=2, type=Path, metavar=("DIRA", "DIRB"), help="compare two dumps")
    args = parser.parse_args()
    if args.diff:
        diff(*args.diff)
    elif args.dump:
        matrix(args.dump)
    else:
        parser.error("pass a dump directory or --diff DIRA DIRB")


if __name__ == "__main__":
    main()
