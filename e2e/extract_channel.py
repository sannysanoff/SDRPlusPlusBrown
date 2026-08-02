#!/usr/bin/env python3
"""Extract a narrow subband (default 16 kHz) from an I/Q baseband WAV.

The requested channel is mixed to DC (0 Hz) and a short, small I/Q WAV is
written so it can be committed next to the e2e tests.

Usage:
    extract_channel.py SRC OUT --rec-center Hz --chan-center Hz \
        --start SEC --stop SEC [--fs-out 16000] [--band 16000]

Example (DMR, signal at absolute 144300000, 5 s from the 144553405 Hz baseband):
    extract_channel.py \\
        /Users/san/recordings/baseband_144553405Hz_17-40-40_15-05-2024---tarlink--dmr---.wav \\
        e2e/recordings/baseband_144300000Hz_16k.wav \\
        --rec-center 144553405 --chan-center 144300000 --start 10 --stop 15
"""
import argparse
import os
import wave

import numpy as np
from scipy import signal


def read_iq(path, start_s, stop_s):
    with wave.open(path, "rb") as w:
        assert w.getnchannels() == 2 and w.getsampwidth() == 2, "expect 16-bit stereo I/Q"
        fs = w.getframerate()
        i0 = max(0, int(start_s * fs))
        i1 = min(int(stop_s * fs), w.getnframes())
        assert i1 > i0, "empty time window"
        w.setpos(i0)
        raw = np.frombuffer(w.readframes(i1 - i0), dtype=np.int16).astype(np.float32)
    iq = (raw[0::2] + 1j * raw[1::2]) / 32768.0
    return fs, iq


def write_iq(path, iq, fs):
    iq = np.asarray(iq, dtype=np.complex64)
    peak = np.max(np.abs(iq)) or 1.0
    xq = np.clip(iq / peak * 0.95, -1, 1)
    iq16 = np.empty(2 * len(xq), dtype=np.int16)
    iq16[0::2] = (xq.real * 32767).astype(np.int16)
    iq16[1::2] = (xq.imag * 32767).astype(np.int16)
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(int(fs))
        w.writeframes(iq16.tobytes())
    return peak


def main():
    ap = argparse.ArgumentParser(description="Extract a narrow subband from an I/Q baseband WAV, channel at DC.")
    ap.add_argument("src")
    ap.add_argument("out")
    ap.add_argument("--rec-center", required=True, type=float, help="recording center (0 Hz of the source file)")
    ap.add_argument("--chan-center", required=True, type=float, help="absolute channel center to extract")
    ap.add_argument("--start", required=True, type=float, help="start time in seconds")
    ap.add_argument("--stop", required=True, type=float, help="stop time in seconds")
    ap.add_argument("--fs-out", type=float, default=16000.0)
    ap.add_argument("--band", type=float, default=16000.0)
    args = ap.parse_args()

    fs_in, iq = read_iq(args.src, args.start, args.stop)
    print(f"in : fs={fs_in} n={len(iq)} dur={len(iq) / fs_in:.2f}s")

    off = args.chan_center - args.rec_center
    t = np.arange(len(iq), dtype=np.float64) / fs_in
    mix = np.exp(1j * 2 * np.pi * (-off) * t).astype(np.complex64)
    iq = iq * mix
    del t, mix

    dec = fs_in / args.fs_out
    assert abs(dec - round(dec)) < 1e-6, f"fs_out must divide fs_in ({fs_in})"
    iq = signal.resample_poly(iq, 1, int(round(dec)))
    print(f"decimated: fs={args.fs_out:.0f} n={len(iq)}")

    half = args.band / 2.0
    ntaps = 193
    b = signal.firwin(ntaps, half - 500, width=1000, fs=args.fs_out, window=("kaiser", 8.6))
    iq = signal.lfilter(b, 1.0, iq)
    iq = iq[(ntaps - 1) // 2:]

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    peak = write_iq(args.out, iq, args.fs_out)
    print(f"out: {args.out} fs={args.fs_out:.0f} n={len(iq)} dur={len(iq) / args.fs_out:.2f}s peak={peak:.3f}")

    nperseg = min(4096, len(iq))
    f, P = signal.welch(iq, fs=args.fs_out, nperseg=nperseg)
    imax = int(np.argmax(P))
    print(f"verify: max PSD at {f[imax]:+.1f} Hz ({10 * np.log10(P[imax]):.1f} dB)")
    inb = P[np.abs(f) < half].sum() / P.sum()
    print(f"verify: {100 * inb:.1f}% of output power inside +-{half:.0f} Hz")


if __name__ == "__main__":
    main()
