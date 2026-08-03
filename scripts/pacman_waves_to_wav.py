#!/usr/bin/env python3
"""
Render Namco Pac-Man WSG wavetable PROM → WAV files.

Source: roms/pacman/82s126.1m (8 waves × 32 samples, values 0..15).
Optional second PROM 82s126.3m is also rendered if present.

Usage:
  python3 scripts/pacman_waves_to_wav.py
  python3 scripts/pacman_waves_to_wav.py -i roms/pacman/82s126.1m -o /tmp/pacwaves
  python3 scripts/pacman_waves_to_wav.py --hz 220 --seconds 1.5

Each wave becomes waveNN.wav (looped tone) plus a waves_preview.wav
that plays all 8 in sequence.
"""
from __future__ import annotations

import argparse
import math
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PROM = ROOT / "roms" / "pacman" / "82s126.1m"
DEFAULT_OUT = ROOT / "snap" / "pacman_waves"

NWAVES = 8
NSAMP = 32


def load_waves(path: Path) -> list[list[int]]:
    data = path.read_bytes()
    if len(data) < NWAVES * NSAMP:
        raise SystemExit(f"{path}: expected {NWAVES * NSAMP} bytes, got {len(data)}")
    waves = []
    for w in range(NWAVES):
        row = [data[w * NSAMP + s] & 0x0F for s in range(NSAMP)]
        waves.append(row)
    return waves


def render_tone(
    table: list[int],
    *,
    hz: float,
    seconds: float,
    rate: int,
    amp: float = 0.7,
) -> bytes:
    """Phase-step through 32-entry 4-bit table → int16 PCM mono."""
    n = int(rate * seconds)
    # Midpoint of 0..15 is 7.5; center as signed.
    out = bytearray()
    phase = 0.0
    step = hz * NSAMP / rate  # table indices per output sample
    peak = int(32767 * amp)
    for _ in range(n):
        idx = int(phase) % NSAMP
        # 0..15 → roughly -1..+1
        x = (table[idx] - 7.5) / 7.5
        s = max(-32768, min(32767, int(x * peak)))
        out += s.to_bytes(2, "little", signed=True)
        phase += step
    return bytes(out)


def write_wav(path: Path, pcm: bytes, rate: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(rate)
        wf.writeframes(pcm)
    print(f"wrote {path} ({len(pcm) // 2} samples @ {rate} Hz)")


def render_prom(
    prom: Path,
    out_dir: Path,
    *,
    hz: float,
    seconds: float,
    rate: int,
    prefix: str,
) -> None:
    waves = load_waves(prom)
    print(f"{prom.name}: {len(waves)} waves × {NSAMP} samples")
    for i, table in enumerate(waves):
        print(f"  wave{i}: {table}")
        pcm = render_tone(table, hz=hz, seconds=seconds, rate=rate)
        write_wav(out_dir / f"{prefix}wave{i}.wav", pcm, rate)

    # Preview: each wave ~0.35s with a short gap
    gap = render_tone([7] * NSAMP, hz=hz, seconds=0.08, rate=rate, amp=0.0)
    parts = []
    for table in waves:
        parts.append(render_tone(table, hz=hz, seconds=0.35, rate=rate))
        parts.append(gap)
    write_wav(out_dir / f"{prefix}waves_preview.wav", b"".join(parts), rate)

    # Also dump raw nibbles as text for inspection
    txt = out_dir / f"{prefix}waves.txt"
    lines = [f"# from {prom}", f"# {NWAVES} x {NSAMP}, values 0..15", ""]
    for i, table in enumerate(waves):
        lines.append(f"wave{i}: " + " ".join(f"{v:x}" for v in table))
    txt.write_text("\n".join(lines) + "\n")
    print(f"wrote {txt}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "-i",
        "--input",
        type=Path,
        default=DEFAULT_PROM,
        help=f"wave PROM (default {DEFAULT_PROM})",
    )
    ap.add_argument(
        "-o",
        "--outdir",
        type=Path,
        default=DEFAULT_OUT,
        help=f"output directory (default {DEFAULT_OUT})",
    )
    ap.add_argument("--hz", type=float, default=440.0, help="preview tone frequency")
    ap.add_argument("--seconds", type=float, default=1.0, help="length of each wave WAV")
    ap.add_argument("--rate", type=int, default=22050, help="WAV sample rate")
    ap.add_argument(
        "--both",
        action="store_true",
        help="also render roms/pacman/82s126.3m if present",
    )
    args = ap.parse_args()

    if not args.input.exists():
        raise SystemExit(f"missing wave PROM: {args.input}")

    render_prom(
        args.input,
        args.outdir,
        hz=args.hz,
        seconds=args.seconds,
        rate=args.rate,
        prefix="",
    )

    other = args.input.with_name("82s126.3m")
    if args.both and other.exists() and other != args.input:
        render_prom(
            other,
            args.outdir,
            hz=args.hz,
            seconds=args.seconds,
            rate=args.rate,
            prefix="3m_",
        )


if __name__ == "__main__":
    main()
