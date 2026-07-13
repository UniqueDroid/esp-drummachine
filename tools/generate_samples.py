#!/usr/bin/env python3
"""Generates short synthetic 8-bit/11025Hz mono drum WAVs for the sequencer.

Still fully synthetic (no sample library access here) but v2: proper
one-pole filters shape the noise instead of raw white noise, envelopes are
exponential instead of polynomial, and kick/snare get a soft-clipped
transient click for punch. Swap these for real recorded samples any time by
dropping same-format WAVs (mono, 8-bit unsigned PCM, 11025Hz) into a
/Sounds/<Set>/ folder on the SD card - the firmware doesn't care where a
set's files came from.
"""
import math
import os
import random
import wave

SAMPLE_RATE = 11025


def write_wav(path, samples):
    with wave.open(path, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(1)  # 8-bit unsigned
        f.setframerate(SAMPLE_RATE)
        f.writeframes(bytes(samples))


def to_u8(x):
    return max(0, min(255, int(x * 127 + 128)))


def soft_clip(x, drive=1.6):
    return math.tanh(x * drive) / math.tanh(drive)


def lowpass(samples, cutoff_hz):
    rc = 1.0 / (2 * math.pi * cutoff_hz)
    dt = 1.0 / SAMPLE_RATE
    alpha = dt / (rc + dt)
    out = [0.0] * len(samples)
    out[0] = samples[0]
    for i in range(1, len(samples)):
        out[i] = out[i - 1] + alpha * (samples[i] - out[i - 1])
    return out


def highpass(samples, cutoff_hz):
    rc = 1.0 / (2 * math.pi * cutoff_hz)
    dt = 1.0 / SAMPLE_RATE
    alpha = rc / (rc + dt)
    out = [0.0] * len(samples)
    out[0] = samples[0]
    for i in range(1, len(samples)):
        out[i] = alpha * (out[i - 1] + samples[i] - samples[i - 1])
    return out


def bandpass(samples, low_hz, high_hz):
    return highpass(lowpass(samples, high_hz), low_hz)


def white_noise(n):
    return [random.uniform(-1, 1) for _ in range(n)]


def expo_envelope(n, decay_rate):
    return [math.exp(-decay_rate * (i / SAMPLE_RATE)) for i in range(n)]


def finalize(floats, gain=1.0):
    peak = max(1e-9, max(abs(x) for x in floats))
    scale = gain / peak
    return [to_u8(soft_clip(x * scale)) for x in floats]


def kick(duration=0.28, start_freq=145.0, end_freq=42.0):
    n = int(SAMPLE_RATE * duration)
    env = expo_envelope(n, 14)
    body = []
    phase = 0.0
    for i in range(n):
        t = i / n
        freq = end_freq + (start_freq - end_freq) * math.exp(-t * 18)
        phase += 2 * math.pi * freq / SAMPLE_RATE
        body.append(math.sin(phase) * env[i])

    # short high-freq click at the very start for attack/punch
    click_n = int(SAMPLE_RATE * 0.004)
    click = highpass(white_noise(click_n), 2000)
    click_env = expo_envelope(click_n, 400)
    for i in range(click_n):
        body[i] += click[i] * click_env[i] * 0.6

    return finalize(body, gain=0.95)


def snare(duration=0.2):
    n = int(SAMPLE_RATE * duration)
    env = expo_envelope(n, 22)
    phase1 = phase2 = 0.0
    tone = []
    for i in range(n):
        phase1 += 2 * math.pi * 185.0 / SAMPLE_RATE
        phase2 += 2 * math.pi * 330.0 / SAMPLE_RATE
        tone.append((math.sin(phase1) * 0.6 + math.sin(phase2) * 0.4) * env[i])

    noise = bandpass(white_noise(n), 900, 6500)
    noise_env = expo_envelope(n, 16)
    mixed = [tone[i] * 0.5 + noise[i] * noise_env[i] * 1.1 for i in range(n)]
    return finalize(mixed, gain=0.95)


def hihat(open_hat=False):
    duration = 0.32 if open_hat else 0.07
    n = int(SAMPLE_RATE * duration)
    decay_rate = 9 if open_hat else 55
    noise = highpass(white_noise(n), 7000)
    env = expo_envelope(n, decay_rate)
    mixed = [noise[i] * env[i] for i in range(n)]
    return finalize(mixed, gain=0.85)


def tom(duration=0.24, freq=210.0):
    n = int(SAMPLE_RATE * duration)
    env = expo_envelope(n, 12)
    phase = 0.0
    out = []
    for i in range(n):
        t = i / n
        f = freq * (1 - 0.35 * t)
        phase += 2 * math.pi * f / SAMPLE_RATE
        harmonic = math.sin(phase * 2) * 0.15
        out.append((math.sin(phase) + harmonic) * env[i])
    return finalize(out, gain=0.95)


def clap(duration=0.22):
    n = int(SAMPLE_RATE * duration)
    out = [0.0] * n
    burst_starts = [0.0, 0.018, 0.036, 0.09]
    burst_len = int(0.015 * SAMPLE_RATE)
    for bi, bs in enumerate(burst_starts):
        start = int(bs * SAMPLE_RATE)
        decay = 30 if bi < len(burst_starts) - 1 else 10  # last burst rings out longer
        burst = bandpass(white_noise(min(burst_len * 3, n - start)), 1000, 4500)
        env = expo_envelope(len(burst), decay)
        for i, v in enumerate(burst):
            if start + i >= n:
                break
            out[start + i] += v * env[i]
    return finalize(out, gain=0.9)


def cymbal(duration=0.45):
    n = int(SAMPLE_RATE * duration)
    env = expo_envelope(n, 6)
    # a handful of inharmonic partials for a metallic tone, plus filtered noise
    partial_freqs = [3200, 4100, 5300, 6700, 8300]
    phases = [0.0] * len(partial_freqs)
    tonal = []
    for i in range(n):
        s = 0.0
        for pi, f in enumerate(partial_freqs):
            phases[pi] += 2 * math.pi * f / SAMPLE_RATE
            s += math.sin(phases[pi])
        tonal.append(s / len(partial_freqs))
    noise = highpass(white_noise(n), 5000)
    mixed = [(tonal[i] * 0.5 + noise[i] * 0.6) * env[i] for i in range(n)]
    return finalize(mixed, gain=0.85)


samples = {
    "kick.wav": kick(),
    "snare.wav": snare(),
    "hihat.wav": hihat(),
    "tom.wav": tom(),
    "clap.wav": clap(),
    "cymbal.wav": cymbal(),
}

if __name__ == "__main__":
    out_dir = os.path.join(os.path.dirname(__file__), "sd_samples")
    os.makedirs(out_dir, exist_ok=True)
    for name, data in samples.items():
        path = os.path.join(out_dir, name)
        write_wav(path, data)
        print(f"wrote {path} ({len(data)} bytes, {len(data)/SAMPLE_RATE*1000:.0f}ms)")
