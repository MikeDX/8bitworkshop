#!/usr/bin/env python3
"""
Render authentic Namco Pac-Man WSG audio (real wave PROM + note tables)
and pack OKI MSM5205 ADPCM for Pocket Gal.

Outputs:
  presets/pcktgal/adpcm.bin
  presets/pcktgal/adpcm_map.inc
  snap/pacman_wsg/*.wav          (debug listens)

WSG math (Pac-Man): 96 kHz, 20-bit phase += frequency, index = (phase>>15)&31,
sample = (wave[w][i]-7.5)*volume.  freq ≈ Hz * 11  (voice-0 units).
"""
from __future__ import annotations

import math
import struct
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WAVE_PROM = ROOT / "roms" / "pacman" / "82s126.1m"
OUT_BIN = ROOT / "presets" / "pcktgal" / "adpcm.bin"
OUT_INC = ROOT / "presets" / "pcktgal" / "adpcm_map.inc"
OUT_WAV = ROOT / "snap" / "pacman_wsg"

NAMCO_HZ = 96000
RENDER_HZ = 48000  # internal render
ADPCM_HZ = 8000
NWAVES, NSAMP = 8, 32

# Music note table from pac sound ROM (tables+0x88) — index 1..15
NOTE_TAB = [
    0x00,
    0x57, 0x5C, 0x61, 0x67, 0x6D, 0x74, 0x7B, 0x82,
    0x8A, 0x92, 0x9A, 0xA3, 0xAD, 0xB8, 0xC3,
]

# OKI MSM5205
STEP = [
    16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
    1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767,
]
INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]


def load_waves(path: Path) -> list[list[float]]:
    data = path.read_bytes()
    waves = []
    for w in range(NWAVES):
        # centered float samples
        waves.append([(data[w * NSAMP + s] & 15) - 7.5 for s in range(NSAMP)])
    return waves


def note_freq(note: int) -> int:
    """20-bit WSG frequency from Namco music note index."""
    if note <= 0:
        return 0
    return NOTE_TAB[note] << 5  # matches Hz≈11*freq mapping


class WSG:
    def __init__(self, waves: list[list[float]]):
        self.waves = waves
        self.voices = [
            {"freq": 0, "vol": 0, "wave": 0, "phase": 0},
            {"freq": 0, "vol": 0, "wave": 0, "phase": 0},
            {"freq": 0, "vol": 0, "wave": 0, "phase": 0},
        ]
        self.inc_scale = NAMCO_HZ / RENDER_HZ  # phase add multiplier per output sample

    def set_voice(self, i: int, freq: int, vol: int, wave: int) -> None:
        v = self.voices[i]
        v["freq"] = int(freq) & 0xFFFFF
        v["vol"] = max(0, min(15, int(vol)))
        v["wave"] = int(wave) & 7

    def render(self, n: int) -> list[float]:
        out = [0.0] * n
        scale = self.inc_scale
        for i in range(n):
            s = 0.0
            for v in self.voices:
                if v["vol"] and v["freq"]:
                    v["phase"] = (v["phase"] + int(v["freq"] * scale)) & 0xFFFFF
                    idx = (v["phase"] >> 15) & 31
                    s += self.waves[v["wave"]][idx] * v["vol"]
            out[i] = s
        return out


def frames(seconds: float) -> int:
    return int(RENDER_HZ * seconds)


def vblank_frames(n: int) -> int:
    """Arcade VBlank ticks → render samples (60 Hz)."""
    return int(RENDER_HZ * (n / 60.0))


def normalize(buf: list[float], peak: float = 0.85) -> list[int]:
    m = max((abs(x) for x in buf), default=1.0) or 1.0
    g = (32767 * peak) / m
    return [max(-32768, min(32767, int(x * g))) for x in buf]


def write_wav(path: Path, pcm: list[int], rate: int = RENDER_HZ) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(rate)
        wf.writeframes(b"".join(struct.pack("<h", s) for s in pcm))


def downsample(pcm: list[int], src: int, dst: int) -> list[int]:
    """Simple lowpass + decimate for ADPCM rate."""
    ratio = src / dst
    # box filter
    out = []
    pos = 0.0
    while int(pos) < len(pcm):
        i0 = int(pos)
        i1 = min(len(pcm) - 1, i0 + int(ratio))
        chunk = pcm[i0:i1] or [pcm[i0]]
        out.append(sum(chunk) // len(chunk))
        pos += ratio
    return out


def encode_oki(pcm: list[int]) -> bytes:
    predsample = 0
    index = 0
    nibbles: list[int] = []
    for sample in pcm:
        step = STEP[index]
        diff = sample - predsample
        sign = 8 if diff < 0 else 0
        if diff < 0:
            diff = -diff
        nibble = 0
        if diff >= step:
            nibble = 4
            diff -= step
        step_h = step >> 1
        if diff >= step_h:
            nibble |= 2
            diff -= step_h
        if diff >= (step_h >> 1):
            nibble |= 1
        nibble |= sign
        diffq = step >> 3
        if nibble & 4:
            diffq += step
        if nibble & 2:
            diffq += step >> 1
        if nibble & 1:
            diffq += step >> 2
        predsample += -diffq if (nibble & 8) else diffq
        predsample = max(-32768, min(32767, predsample))
        index = max(0, min(48, index + INDEX[nibble & 7]))
        nibbles.append(nibble & 0xF)
    if len(nibbles) & 1:
        nibbles.append(0)
    return bytes((nibbles[i] << 4) | nibbles[i + 1] for i in range(0, len(nibbles), 2))


# ---- sequence players (Namco music bytes: high=dur, low=note) ----

def play_music(
    wsg: WSG,
    song_a: bytes,
    song_b: bytes,
    wave_a: int,
    wave_b: int,
    vol_a: int = 13,
    vol_b: int = 9,
) -> list[float]:
    """Play two independent duration tracks locked to VBlank timing."""

    def expand(song: bytes):
        ev = []
        t = 0
        for b in song:
            d, n = b >> 4, b & 0xF
            ev.append((t, d, n))
            t += d
        return ev, t

    ea, ta = expand(song_a)
    eb, tb = expand(song_b)
    total_vb = max(ta, tb)
    out: list[float] = []

    def note_at(ev, t):
        for start, d, n in ev:
            if start <= t < start + d:
                return n
        return 0

    # render per vblank for envelope continuity
    for vb in range(total_vb):
        na, nb = note_at(ea, vb), note_at(eb, vb)
        wsg.set_voice(0, note_freq(na), vol_a if na else 0, wave_a)
        wsg.set_voice(1, note_freq(nb), vol_b if nb else 0, wave_b)
        wsg.set_voice(2, 0, 0, 0)
        out.extend(wsg.render(vblank_frames(1)))
    # short release
    wsg.set_voice(0, 0, 0, 0)
    wsg.set_voice(1, 0, 0, 0)
    out.extend(wsg.render(vblank_frames(4)))
    return out


def sweep(
    wsg: WSG,
    *,
    voice: int,
    wave: int,
    vol: int,
    freq0: int,
    dfreq: int,
    steps: int,
    step_vb: int = 1,
) -> list[float]:
    out: list[float] = []
    f = freq0
    for _ in range(steps):
        wsg.set_voice(voice, max(0, f), vol, wave)
        for i in range(3):
            if i != voice:
                wsg.set_voice(i, 0, 0, 0)
        out.extend(wsg.render(vblank_frames(step_vb)))
        f += dfreq
    wsg.set_voice(voice, 0, 0, 0)
    out.extend(wsg.render(vblank_frames(2)))
    return out


def render_all(waves: list[list[float]]) -> dict[str, list[float]]:
    wsg = WSG(waves)
    clips: dict[str, list[float]] = {}

    # --- music (arcade table bytes) ---
    intro_a = bytes([
        0x82, 0x70, 0x69, 0x82, 0x70, 0x69, 0x83, 0x70, 0x6A, 0x83, 0x70, 0x6A,
        0x82, 0x70, 0x69, 0x82, 0x70, 0x69, 0x89, 0x8B, 0x8D, 0x8E,
    ])
    intro_b = bytes([
        0x67, 0x50, 0x30, 0x47, 0x30, 0x67, 0x50, 0x30, 0x47, 0x30, 0x67, 0x50, 0x30, 0x47, 0x30,
        0x4B, 0x10, 0x4C, 0x10, 0x4D, 0x10, 0x4E, 0x10,
        0x67, 0x50, 0x30, 0x47, 0x30, 0x67, 0x50, 0x30, 0x47, 0x30, 0x67, 0x50, 0x30, 0x47, 0x30,
        0x4B, 0x10, 0x4C, 0x10, 0x4D, 0x10, 0x4E, 0x10,
        0x67, 0x50, 0x30, 0x47, 0x30, 0x67, 0x50, 0x30, 0x47, 0x30, 0x67, 0x50, 0x30, 0x47, 0x30,
        0x4B, 0x10, 0x4C, 0x10, 0x4D, 0x10, 0x4E, 0x10,
        0x77, 0x20, 0x4E, 0x10, 0x4D, 0x10, 0x4C, 0x10, 0x4A, 0x10, 0x47, 0x10, 0x46, 0x10,
        0x65, 0x30, 0x66, 0x30, 0x67, 0x40, 0x70,
    ])
    # Truncate harmony to lead length so jingle ends cleanly
    lead_vb = sum(b >> 4 for b in intro_a)
    # rebuild B only up to lead_vb by playing and cutting
    clips["INTRO"] = play_music(wsg, intro_a, intro_b, wave_a=1, wave_b=0, vol_a=14, vol_b=10)
    # hard cut to lead duration
    clips["INTRO"] = clips["INTRO"][: vblank_frames(lead_vb) + vblank_frames(4)]

    inter_a = bytes([  # deedle melody CH2 @ 0x165
        0x26, 0x67, 0x26, 0x67, 0x26, 0x67, 0x23, 0x44, 0x42, 0x47, 0x30, 0x67, 0x2A, 0x8B, 0x70,
        0x26, 0x67, 0x26, 0x67, 0x26, 0x67, 0x23, 0x44, 0x42, 0x47, 0x30, 0x67, 0x23, 0x84, 0x70,
        0x26, 0x67, 0x26, 0x67, 0x26, 0x67, 0x23, 0x44, 0x42, 0x47, 0x30, 0x67, 0x29, 0x6A, 0x2B, 0x6C, 0x30,
        0x2C, 0x6D, 0x40, 0x2B, 0x6C, 0x29, 0x6A, 0x67, 0x20, 0x29, 0x6A, 0x40, 0x26, 0x87, 0x70,
    ])
    inter_b = bytes([  # harmony CH1 @ 0x128
        0x42, 0x50, 0x4E, 0x50, 0x49, 0x50, 0x46, 0x50, 0x4E, 0x49, 0x70, 0x66, 0x70,
        0x43, 0x50, 0x4F, 0x50, 0x4A, 0x50, 0x47, 0x50, 0x4F, 0x4A, 0x70, 0x67, 0x70,
        0x42, 0x50, 0x4E, 0x50, 0x49, 0x50, 0x46, 0x50, 0x4E, 0x49, 0x70, 0x66, 0x70,
        0x45, 0x46, 0x47, 0x50, 0x47, 0x48, 0x49, 0x50, 0x49, 0x4A, 0x4B, 0x50, 0x6E,
    ])
    clips["INTER"] = play_music(wsg, inter_a, inter_b, wave_a=0, wave_b=1, vol_a=14, vol_b=9)
    # Full intermission is ~4.4s / ~17KB ADPCM — keep first phrase so it fits
    # in the leftover dual-bank budget after intro + ambient.
    inter_lead_vb = sum(b >> 4 for b in inter_a[:15])  # first line of melody
    clips["INTER"] = clips["INTER"][: vblank_frames(inter_lead_vb) + vblank_frames(4)]

    # --- SFX from Namco effect headers (tables @ ROM 0x798) ---
    # Header: [flags, freq, dfreq, ?, ?, ?, duration-ish, ?]
    # Voice freq ≈ header_freq << 8 (16-bit voice style).
    def fx_freq(f8: int) -> int:
        return (f8 & 0xFF) << 8

    # WAKA1 73 20 00 0c 00 0a 1f 00 — flat chirp ~8 frames
    clips["WAKA1"] = sweep(
        wsg, voice=2, wave=3, vol=12, freq0=fx_freq(0x20), dfreq=0, steps=8, step_vb=1
    )
    # WAKA2 72 20 fb 87 00 02 0f 00 — slight downward (-5 per frame on mid byte)
    clips["WAKA2"] = sweep(
        wsg, voice=2, wave=2, vol=12, freq0=fx_freq(0x20), dfreq=-0x500, steps=8, step_vb=1
    )

    # EAT ghost 56 0c ff 8c 00 02 0f 00 — low start, falling then sparkle up
    eat = sweep(wsg, voice=2, wave=1, vol=13, freq0=fx_freq(0x0C), dfreq=-0x100, steps=6, step_vb=1)
    eat += sweep(wsg, voice=2, wave=1, vol=13, freq0=fx_freq(0x10), dfreq=0x300, steps=10, step_vb=1)
    clips["EAT"] = eat

    # DEATH 41 20 ff 86 fe 1c 0f ff — long descending cascade
    death = []
    f = fx_freq(0x28)
    for t in range(52):
        vol = max(4, 14 - (t // 6))
        wave = 0 if t < 24 else (1 if t < 40 else 2)
        wsg.set_voice(0, max(0x80, f), vol, wave)
        wsg.set_voice(1, 0, 0, 0)
        wsg.set_voice(2, 0, 0, 0)
        death.extend(wsg.render(vblank_frames(1)))
        f += -0x100  # ≈ ff on mid-frequency byte
    wsg.set_voice(0, 0, 0, 0)
    clips["DEATH"] = death

    # Coin / credit 05 00 02 20 … — bright ding
    ding = sweep(wsg, voice=0, wave=0, vol=14, freq0=fx_freq(0x30), dfreq=0, steps=4, step_vb=1)
    ding += sweep(wsg, voice=0, wave=0, vol=10, freq0=fx_freq(0x28), dfreq=-0x200, steps=10, step_vb=1)
    clips["COIN"] = ding

    # Fruit bonus
    clips["FRUIT"] = sweep(
        wsg, voice=2, wave=3, vol=12, freq0=fx_freq(0x18), dfreq=0x200, steps=10, step_vb=1
    )

    # Fright warble (arcade voice: +0x180 / frame, wrap every 8)
    fright = []
    f = 0x0180
    for t in range(32):  # shorter loop → room for quality elsewhere
        if (t & 7) == 0:
            f = 0x0180
        else:
            f += 0x0180
        wsg.set_voice(1, f, 11, 4)
        wsg.set_voice(0, 0, 0, 0)
        wsg.set_voice(2, 0, 0, 0)
        fright.extend(wsg.render(vblank_frames(1)))
    wsg.set_voice(1, 0, 0, 0)
    clips["FRIGHT"] = fright

    # Eyes return whoop
    eyes = sweep(wsg, voice=1, wave=5, vol=13, freq0=0x1400, dfreq=-0x100, steps=12, step_vb=1)
    eyes += sweep(wsg, voice=1, wave=5, vol=13, freq0=0x0800, dfreq=0x100, steps=12, step_vb=1)
    clips["EYES"] = eyes

    # Siren (first maze level ≈ effect 0x10 chain) — one weeooh period
    siren = sweep(wsg, voice=1, wave=4, vol=11, freq0=0x0600, dfreq=0x40, steps=16, step_vb=2)
    siren += sweep(wsg, voice=1, wave=4, vol=11, freq0=0x0A00, dfreq=-0x40, steps=16, step_vb=2)
    clips["SIREN"] = siren

    return clips


def main() -> None:
    if not WAVE_PROM.exists():
        raise SystemExit(f"missing {WAVE_PROM}")
    waves = load_waves(WAVE_PROM)
    clips = render_all(waves)
    OUT_WAV.mkdir(parents=True, exist_ok=True)

    # Prefer oneshots + intro + ambient loops. Intermission is large; include
    # only if it fits the 32KB dual-bank budget after the rest.
    order = [
        "WAKA1", "WAKA2", "EAT", "DEATH", "COIN", "FRUIT",
        "INTRO", "FRIGHT", "EYES", "SIREN",
    ]
    optional = ["INTER"]

    for name in order + optional:
        write_wav(OUT_WAV / f"{name.lower()}.wav", normalize(clips[name]))

    packed = bytearray()
    map_lines = [
        "; Auto-generated by scripts/gen_pcktgal_pacman_adpcm.py",
        "; Authentic Namco WSG → OKI MSM5205 @ 8 kHz",
        "; Offsets are linear ROM $0000..; player banks 16KB windows at CPU $4000.",
        "",
    ]
    offs: dict[str, tuple[int, int]] = {}

    def append_clip(name: str) -> bool:
        pcm8 = downsample(normalize(clips[name]), RENDER_HZ, ADPCM_HZ)
        ad = encode_oki(pcm8)
        pad = 1 if (len(packed) & 1) else 0
        if len(packed) + pad + len(ad) > 0x8000:
            return False
        if pad:
            packed.append(0)
        off = len(packed)
        packed.extend(ad)
        offs[name] = (off, len(ad))
        map_lines.extend([
            f"ADPCM_{name}_OFF = ${off:04x}",
            f"ADPCM_{name}_LEN = ${len(ad):04x}",
            "",
        ])
        print(f"{name:8s}  {len(ad):5d} B  ({len(ad) * 2 / ADPCM_HZ:.2f}s)  @ ${off:04x}")
        return True

    for name in order:
        if not append_clip(name):
            raise SystemExit(f"required clip {name} does not fit")
    for name in optional:
        if append_clip(name):
            print(f"(included optional {name})")
        else:
            # Alias to INTRO so the command still plays something valid
            off, ln = offs["INTRO"]
            map_lines.extend([
                f"ADPCM_{name}_OFF = ${off:04x}",
                f"ADPCM_{name}_LEN = ${ln:04x}",
                "",
            ])
            print(f"(optional {name} aliased to INTRO — no room)")

    OUT_BIN.write_bytes(packed)
    OUT_INC.write_text("\n".join(map_lines) + "\n")
    print(f"wrote {OUT_BIN} ({len(packed)} bytes)")
    print(f"wrote {OUT_INC}")
    print(f"debug WAVs in {OUT_WAV}")


if __name__ == "__main__":
    main()
