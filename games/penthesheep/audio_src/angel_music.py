"""Pen the Sheep score: two heavenly tracks beside "Pasture Haze" and "Long Grass".

The player asked for more of the angelic, ethereal side of the meadow (the classic
hex-pen puzzle's wash still sounds the more heavenly of the two), so these stay
in that world, with no beat and no tune to follow, and add voices to it:

  - "Cloud Choir" (sh_music_choir): a wordless choir singing "ah", every part a
    small section of detuned voices with a slow, shared vibrato, moving through
    Cmaj9, Am9, Fmaj9#11 (the lydian lift) and G6/9sus, with high glass tones
    floating above. A cathedral of a reverb.
  - "Morning Bells" (sh_music_bells): glass bells in G pentatonic, sparse and
    unhurried, over a hushed string pad (Gmaj9, Em9, Cmaj9#11, Dsus), with a
    soft echo carrying each bell away.

Each is one seamless loop. Everything is synthesized here: no samples. Writes 48 kHz
16-bit WAV masters into masters/, adds the tracks to ../assets/audio's manifest
and, where afconvert exists (macOS), encodes the AAC the game ships.
"""
import json
import os
import shutil
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'engine'))
sys.path.insert(0, HERE)
from synth import SR, make_ir, conv_reverb, fft_filter, LoopMix, tarr  # noqa: E402
from meadow_music import bus_rms_db, finish, write  # noqa: E402

ASSETS = os.path.join(HERE, '..', '..', '..', 'assets', 'audio')
MASTERS = os.path.join(HERE, 'masters')
rng = np.random.default_rng(777)


def hz(m, cents=0.0):
    return 440 * 2 ** ((m - 69 + cents / 100) / 12)


# An "ah": the formants of an open vowel, sung softly (gaussians on the harmonic amplitudes).
AH = [(750, 90, 1.0), (1150, 110, .55), (2700, 170, .18), (3400, 220, .1)]


def formant_gain(f):
    g = .05
    for centre, width, amp in AH:
        g += amp * np.exp(-.5 * ((f - centre) / width) ** 2)
    return g


def choir_voice(m, dur, attack, release, voices=6, vel=1.0):
    """A small section singing one note: additive "ah" voices, each a little off pitch."""
    n = int(dur * SR)
    t = tarr(n)
    env = np.minimum(1, t / attack) ** 2 * np.minimum(1, np.maximum(0, dur - t) / release) ** 1.5
    L = np.zeros(n)
    R = np.zeros(n)
    f0 = hz(m)
    for v in range(voices):
        detune = rng.uniform(-9, 9)
        rate = rng.uniform(4.6, 5.4)
        vib = 1 + .0045 * np.sin(2 * np.pi * rate * t + rng.uniform(0, 6.28)) * np.minimum(1, t / 1.5)
        f = f0 * 2 ** (detune / 1200) * vib
        phase = 2 * np.pi * np.cumsum(f) / SR + rng.uniform(0, 6.28)
        y = np.zeros(n)
        for k in range(1, 24):
            fk = f0 * k
            if fk > 5200:
                break
            y += formant_gain(fk) / k ** .7 * np.sin(k * phase)
        # breath: a little filtered noise in the voice
        y += .015 * fft_filter(rng.standard_normal(n), 600, 3000)
        p = rng.uniform(-.8, .8)
        L += y * np.cos((p + 1) * np.pi / 4)
        R += y * np.sin((p + 1) * np.pi / 4)
    return np.stack([L, R]) * env * vel / voices


def glass(m, dur, vel=1.0):
    """A pure high tone with its octave, swelling and fading: light through glass."""
    n = int(dur * SR)
    t = tarr(n)
    env = np.sin(np.pi * np.clip(t / dur, 0, 1)) ** 2
    f = hz(m) * (1 + .002 * np.sin(2 * np.pi * .3 * t))
    ph = 2 * np.pi * np.cumsum(f) / SR
    y = (np.sin(ph) + .25 * np.sin(2 * ph)) * env * vel
    return y


def bell(m, vel=1.0):
    """A glass bell: a struck fundamental with inharmonic partials, the upper ones dying first."""
    dur = 6.0
    n = int(dur * SR)
    t = tarr(n)
    f = hz(m)
    y = np.zeros(n)
    for ratio, amp, tau in ((1, 1.0, 2.6), (2.0, .35, 1.6), (2.76, .3, 1.1), (5.4, .12, .5), (8.93, .05, .25)):
        y += amp * np.sin(2 * np.pi * f * ratio * t + rng.uniform(0, 6.28)) * np.exp(-t / tau)
    y *= 1 - np.exp(-t / .002)
    return y * vel


def pad_string(m, dur, attack, release, vel=1.0):
    """A hushed string section: detuned saws, darkly filtered, slow bow."""
    n = int(dur * SR)
    t = tarr(n)
    env = np.minimum(1, t / attack) ** 2 * np.minimum(1, np.maximum(0, dur - t) / release) ** 1.5
    y = np.zeros(n)
    for v in range(5):
        f = hz(m, rng.uniform(-8, 8)) * (1 + .002 * np.sin(2 * np.pi * rng.uniform(.2, .5) * t))
        ph = np.cumsum(f) / SR + rng.uniform(0, 1)
        y += 2 * (ph - np.floor(ph + .5))
    y = fft_filter(y, 80, 1400, 2)
    return y * env * vel / 5


def place(mix, bus, y, start, pan=0.0, gain=1.0):
    if y.ndim == 1:
        mix.add(bus, y, start, pan, gain)
        return
    L = mix.L
    b = mix.bus(bus)
    s = start % L
    pos, n = 0, y.shape[1]
    while pos < n:
        k = min(n - pos, L - s)
        b[:, s:s + k] += gain * y[:, pos:pos + k]
        pos += k
        s = 0


def cloud_choir():
    LOOP = 64.0
    L = int(LOOP * SR)
    mix = LoopMix(L, circular=True)
    # (bass, choir notes, glass notes) for each 16-second stretch
    chords = [
        (36, [55, 60, 64, 67, 71, 74], [79, 83, 86, 88]),           # Cmaj9
        (33, [57, 60, 64, 67, 71, 72], [76, 79, 83, 84]),           # Am9
        (29, [57, 60, 64, 67, 69, 71], [76, 79, 81, 83]),           # Fmaj9#11
        (31, [55, 60, 62, 64, 67, 69], [74, 76, 79, 81]),           # G6/9sus
    ]
    for i, (bass, notes, high) in enumerate(chords):
        at = i * 16.0
        # the parts come in one after another and sing over the change into the next chord
        for j, m in enumerate(notes):
            start = at - 1.5 + j * rng.uniform(.4, 1.1)
            y = choir_voice(m, 20.0, 3.5, 5.0, vel=rng.uniform(.7, 1.0) * (1.1 if m < 60 else 1.0))
            place(mix, 'choir', y, int(start * SR))
        y = choir_voice(bass + 12, 20.0, 4.0, 5.0, voices=4, vel=.8)
        place(mix, 'choir', y, int((at - 2) * SR))
        tt = tarr(int(19 * SR))
        low = np.sin(2 * np.pi * hz(bass) * tt) + .4 * np.sin(2 * np.pi * (hz(bass) + .3) * tt)
        low *= np.minimum(1, tt / 3) * np.minimum(1, (19 - tt) / 4)
        mix.add('bass', low, int((at - 1.5) * SR), 0)
        # glass tones drifting above
        t = at + rng.uniform(1, 3)
        while t < at + 16:
            m = int(rng.choice(high))
            mix.add('glass', glass(m, rng.uniform(3, 6), rng.uniform(.4, .9)), int(t * SR), rng.uniform(-.9, .9))
            t += rng.uniform(2.0, 4.5)
    return mix, L, dict(choir=-17, bass=-25, glass=-26), make_ir(length=6.0, rt_low=4.2, rt_mid=4.8, rt_high=3.6, predelay=.03)


def morning_bells():
    LOOP = 56.0
    L = int(LOOP * SR)
    mix = LoopMix(L, circular=True)
    chords = [
        (43, [55, 59, 62, 66, 69]),   # Gmaj9
        (40, [55, 59, 62, 66, 67]),   # Em9
        (36, [55, 59, 62, 64, 66]),   # Cmaj9#11
        (38, [57, 62, 64, 67, 69]),   # Dsus, 6/9
    ]
    for i, (bass, notes) in enumerate(chords):
        at = i * 14.0
        for m in notes:
            place_y = pad_string(m, 17.0, 3.0, 4.0, vel=rng.uniform(.7, 1.0))
            mix.add('pad', place_y, int((at - 1.2 + rng.uniform(0, .8)) * SR), rng.uniform(-.6, .6))
        tt = tarr(int(16 * SR))
        low = np.sin(2 * np.pi * hz(bass) * tt) * np.minimum(1, tt / 2) * np.minimum(1, (16 - tt) / 3)
        mix.add('bass', low, int((at - 1) * SR), 0)
    pentatonic = [67, 69, 71, 74, 76, 79, 81, 83, 86, 88]
    t = 0.3
    previous = 74
    while t < LOOP:
        # small steps mostly, now and then a leap: a slow, wandering line of bells
        options = [m for m in pentatonic if abs(m - previous) <= (12 if rng.uniform() < .2 else 5) and m != previous]
        m = int(rng.choice(options))
        previous = m
        mix.add('bells', bell(m, rng.uniform(.5, 1.0)), int(t * SR), rng.uniform(-.7, .7))
        if rng.uniform() < .25:  # a soft answer an octave up
            mix.add('bells', bell(m + 12, rng.uniform(.25, .45)), int((t + .18) * SR), rng.uniform(-.8, .8))
        t += float(rng.choice([1.0, 1.5, 2.0, 2.5, 3.0], p=[.25, .3, .25, .12, .08]))
    return mix, L, dict(pad=-20, bass=-27, bells=-19), make_ir(length=4.5, rt_low=3.0, rt_mid=3.4, rt_high=2.6, predelay=.025)


def master(mix, L, levels, ir):
    out = np.zeros((2, L))
    for name, b in mix.buses.items():
        out += b * 10 ** ((levels.get(name, -22) - bus_rms_db(b)) / 20)
    out = out * .55 + conv_reverb(out * .5, ir, True) * .65
    if 'bells' in mix.buses:  # an echo carrying each bell away (seamless: rolled)
        echo = np.roll(out[::-1], int(.42 * SR), axis=1) * .22  # from the other side
        out = out + np.stack([fft_filter(c, 300, 5000, circular=True) for c in echo])
    out = np.stack([fft_filter(c, 40, 9000, 2, circular=True) for c in out])
    mid, side = (out[0] + out[1]) / 2, (out[0] - out[1]) / 2
    return np.stack([mid + side * 1.6, mid - side * 1.6])


def encode(wav, m4a):
    if shutil.which('afconvert') is None:
        print('afconvert not found: encode', wav, 'to', m4a, 'on macOS')
        return
    subprocess.run(['afconvert', '-f', 'm4af', '-d', 'aac', '-b', '192000', wav, m4a], check=True)


def main():
    os.makedirs(MASTERS, exist_ok=True)
    manifest_path = os.path.join(ASSETS, 'sheep_audio_manifest.json')
    manifest = json.load(open(manifest_path))
    for name, build in (('sh_music_choir', cloud_choir), ('sh_music_bells', morning_bells)):
        mix, L, levels, ir = build()
        wav = os.path.join(MASTERS, name + '.wav')
        write(wav, finish(master(mix, L, levels, ir), -21.0, -3.0))
        encode(wav, os.path.join(ASSETS, name + '.m4a'))
        manifest['music'] = [e for e in manifest['music'] if e['id'] != name]
        manifest['music'].append({'id': name, 'loop_end_sample_exclusive': L, 'seconds': L / SR})
        print('ok', name, L / SR)
    with open(manifest_path, 'w') as out:
        json.dump(manifest, out, indent=1)


if __name__ == '__main__':
    main()
