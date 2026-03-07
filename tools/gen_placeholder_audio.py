#!/usr/bin/env python3
"""Generate placeholder WAV audio files for the N64 dev engine.

Creates simple synthesized sound effects and a short BGM loop.
These are meant to be replaced with proper audio content later.

Usage: python3 tools/gen_placeholder_audio.py
"""

import struct
import math
import os
import random

SAMPLE_RATE = 22050  # 22 kHz — good enough for N64 SFX, saves space
CHANNELS = 1
SAMPLE_WIDTH = 2  # 16-bit

def write_wav(filename, samples):
    """Write a list of float samples [-1.0, 1.0] to a 16-bit mono WAV file."""
    os.makedirs(os.path.dirname(filename), exist_ok=True)
    num_samples = len(samples)
    data_size = num_samples * SAMPLE_WIDTH
    file_size = 36 + data_size

    with open(filename, 'wb') as f:
        # RIFF header
        f.write(b'RIFF')
        f.write(struct.pack('<I', file_size))
        f.write(b'WAVE')
        # fmt chunk
        f.write(b'fmt ')
        f.write(struct.pack('<I', 16))  # chunk size
        f.write(struct.pack('<H', 1))   # PCM format
        f.write(struct.pack('<H', CHANNELS))
        f.write(struct.pack('<I', SAMPLE_RATE))
        f.write(struct.pack('<I', SAMPLE_RATE * CHANNELS * SAMPLE_WIDTH))  # byte rate
        f.write(struct.pack('<H', CHANNELS * SAMPLE_WIDTH))  # block align
        f.write(struct.pack('<H', SAMPLE_WIDTH * 8))  # bits per sample
        # data chunk
        f.write(b'data')
        f.write(struct.pack('<I', data_size))
        for s in samples:
            clamped = max(-1.0, min(1.0, s))
            f.write(struct.pack('<h', int(clamped * 32767)))

    print(f"  Generated: {filename} ({num_samples} samples, {num_samples/SAMPLE_RATE:.2f}s)")

def sine_tone(freq, duration, volume=0.8):
    """Generate a sine wave tone."""
    n = int(SAMPLE_RATE * duration)
    return [volume * math.sin(2 * math.pi * freq * i / SAMPLE_RATE) for i in range(n)]

def fade_in(samples, duration):
    """Apply a linear fade-in."""
    n = min(int(SAMPLE_RATE * duration), len(samples))
    for i in range(n):
        samples[i] *= i / n
    return samples

def fade_out(samples, duration):
    """Apply a linear fade-out."""
    n = min(int(SAMPLE_RATE * duration), len(samples))
    start = len(samples) - n
    for i in range(n):
        samples[start + i] *= (n - i) / n
    return samples

def chirp(freq_start, freq_end, duration, volume=0.8):
    """Generate a frequency sweep (chirp)."""
    n = int(SAMPLE_RATE * duration)
    samples = []
    phase = 0
    for i in range(n):
        t = i / n
        freq = freq_start + (freq_end - freq_start) * t
        phase += 2 * math.pi * freq / SAMPLE_RATE
        samples.append(volume * math.sin(phase))
    return samples

def noise_burst(duration, volume=0.5):
    """Generate a noise burst with envelope."""
    n = int(SAMPLE_RATE * duration)
    samples = [volume * (random.random() * 2 - 1) for _ in range(n)]
    return fade_out(samples, duration * 0.8)

def square_tone(freq, duration, volume=0.5):
    """Generate a square wave tone."""
    n = int(SAMPLE_RATE * duration)
    period = SAMPLE_RATE / freq
    return [volume * (1.0 if (i % period) < (period / 2) else -1.0) for i in range(n)]

def generate_bgm_loop():
    """Generate a simple ambient BGM loop (~4 seconds).
    Uses a chord progression with sine waves for a mellow ambient feel.
    """
    loop_duration = 4.0
    n = int(SAMPLE_RATE * loop_duration)
    samples = [0.0] * n

    # Simple chord progression: Am - F - C - G (i - VI - III - VII)
    chords = [
        [220.0, 261.63, 329.63],   # Am: A3, C4, E4
        [174.61, 220.0, 261.63],   # F:  F3, A3, C4
        [261.63, 329.63, 392.0],   # C:  C4, E4, G4
        [196.0, 246.94, 293.66],   # G:  G3, B3, D4
    ]

    beats_per_chord = loop_duration / len(chords)
    chord_samples = int(SAMPLE_RATE * beats_per_chord)

    for ci, chord in enumerate(chords):
        start = ci * chord_samples
        for freq in chord:
            vol = 0.15  # Quiet, ambient
            for i in range(chord_samples):
                if start + i < n:
                    t = i / chord_samples
                    # Soft envelope per chord
                    env = math.sin(math.pi * t)  # Fade in and out
                    samples[start + i] += vol * env * math.sin(2 * math.pi * freq * i / SAMPLE_RATE)

    # Add a subtle bass note
    for ci, chord in enumerate(chords):
        bass_freq = chord[0] / 2  # One octave below root
        start = ci * chord_samples
        for i in range(chord_samples):
            if start + i < n:
                t = i / chord_samples
                env = math.sin(math.pi * t) * 0.7
                samples[start + i] += 0.12 * env * math.sin(2 * math.pi * bass_freq * i / SAMPLE_RATE)

    # Normalize
    peak = max(abs(s) for s in samples)
    if peak > 0:
        samples = [s / peak * 0.7 for s in samples]

    # Crossfade last 200ms into first 200ms for seamless loop
    crossfade = int(SAMPLE_RATE * 0.2)
    for i in range(crossfade):
        t = i / crossfade
        samples[i] = samples[i] * t + samples[n - crossfade + i] * (1 - t)

    return samples

def main():
    base = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    sfx_dir = os.path.join(base, "assets", "audio", "sfx")
    music_dir = os.path.join(base, "assets", "audio", "music")

    print("Generating placeholder audio files...")
    print()

    # --- SFX ---
    print("Sound effects:")

    # Menu open: rising tone
    s = chirp(330, 660, 0.1, 0.7)
    s = fade_in(s, 0.01)
    s = fade_out(s, 0.03)
    write_wav(os.path.join(sfx_dir, "menu_open.wav"), s)

    # Menu close: falling tone
    s = chirp(660, 330, 0.1, 0.7)
    s = fade_in(s, 0.01)
    s = fade_out(s, 0.03)
    write_wav(os.path.join(sfx_dir, "menu_close.wav"), s)

    # Menu navigate: short click
    s = sine_tone(880, 0.04, 0.6)
    s = fade_in(s, 0.005)
    s = fade_out(s, 0.02)
    write_wav(os.path.join(sfx_dir, "menu_nav.wav"), s)

    # Menu select: confirmation beep
    s = sine_tone(660, 0.05, 0.7) + sine_tone(880, 0.1, 0.7)
    s = fade_out(s, 0.04)
    write_wav(os.path.join(sfx_dir, "menu_select.wav"), s)

    # Object select: rising chirp
    s = chirp(440, 880, 0.08, 0.7)
    s = fade_out(s, 0.02)
    write_wav(os.path.join(sfx_dir, "obj_select.wav"), s)

    # Object deselect: falling chirp
    s = chirp(660, 330, 0.08, 0.6)
    s = fade_out(s, 0.02)
    write_wav(os.path.join(sfx_dir, "obj_deselect.wav"), s)

    # Mode change: two-tone blip
    s = sine_tone(550, 0.04, 0.6) + sine_tone(730, 0.06, 0.6)
    s = fade_out(s, 0.03)
    write_wav(os.path.join(sfx_dir, "mode_change.wav"), s)

    # Collision: noise burst
    random.seed(42)  # Reproducible
    s = noise_burst(0.12, 0.6)
    s = fade_in(s, 0.005)
    write_wav(os.path.join(sfx_dir, "collision.wav"), s)

    # --- BGM ---
    print()
    print("Background music:")

    s = generate_bgm_loop()
    write_wav(os.path.join(music_dir, "demo.wav"), s)

    print()
    print("Done! All placeholder audio files generated.")
    print()
    print("To replace with better audio:")
    print("  SFX:  Use jsfxr (https://sfxr.me/) to design sounds, export as WAV")
    print("  BGM:  Download a free XM from The Mod Archive (https://modarchive.org/)")
    print("        or compose in MilkyTracker/OpenMPT")
    print()
    print("Place source files in assets/audio/sfx/ and assets/audio/music/")
    print("The build system converts them automatically.")

if __name__ == "__main__":
    main()
