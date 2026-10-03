"""Koi-Koi sound: the music "Twelve Months" and the card effects. Synthesized; no samples.

Music: a calm, consonant piece in the yo pentatonic (D E G A B: no semitones, so nothing
clashes), 66 BPM: a koto (plucked, with the gentle pitch bend after the pluck), a
breathy bamboo flute in long phrases, a soft low koto drone on D and A, and a little room.
One seamless 32-bar loop.

Effects: the sharp "pachi!" of a hanafuda card slapped down (they're thick, and are played
with a snap), a card turned over, a capture (two cards meeting), a shuffle, a set made (a koto
flourish), koi-koi (a taiko and a rising koto run), stopping (wooden clappers), a round won,
a round lost, a click. Writes ../assets/audio: WAVs for effects and AAC (.m4a, via
afconvert) for the music and the longer cues.
"""
import json
import os
import subprocess
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'engine'))
from synth import SR, tarr, noise, fft_filter, make_ir, conv_reverb, LoopMix, render_note  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), '..', 'assets', 'audio')
MASTERS = os.path.join(os.path.dirname(__file__), 'masters')
rng = np.random.default_rng(1868)
YO = [0, 2, 5, 7, 9]  # D E G A B from D
TONIC = 62  # D4


def yo(i, base=TONIC):
    return base + YO[i % 5] + 12 * (i // 5)


def hz(m):
    return 440 * 2 ** ((m - 69) / 12)


def koto(m, dur, vel=.7, bend=True):
    """A plucked silk-string tone: bright attack, quick decay of the upper partials, and the
    slight rise and settle of pitch as the string is pressed after the pluck."""
    n = int(dur * SR)
    t = tarr(n)
    f0 = hz(m) * (1 + (.012 * np.exp(-t / .08) if bend else 0) + .002 * np.sin(2 * np.pi * 5 * t) * np.minimum(1, t / .4))
    ph = 2 * np.pi * np.cumsum(f0) / SR
    y = np.zeros(n)
    for k, a in enumerate((1, .55, .32, .2, .12, .07), start=1):
        y += a * np.sin(k * ph + k * .3) * np.exp(-t / (1.6 / (1 + .7 * (k - 1))))
    att = fft_filter(noise(n), 2000, 8000) * np.exp(-t / .006) * .3
    return (y * np.minimum(1, t / .002) + att) * vel


def flute(m, dur, vel=.6):
    """A breathy bamboo flute: a soft tone that swells, a little breath, a slow vibrato that grows."""
    n = int(dur * SR)
    t = tarr(n)
    vib = .006 * np.sin(2 * np.pi * 4.6 * t) * np.clip((t - .3) / .6, 0, 1)
    f = hz(m) * (1 + vib)
    ph = 2 * np.pi * np.cumsum(f) / SR
    y = np.sin(ph) + .18 * np.sin(2 * ph) + .05 * np.sin(3 * ph)
    breath = fft_filter(noise(n), 1200, 5000) * (.18 + .1 * np.exp(-t / .15))
    env = np.minimum(1, t / .18) * np.exp(-np.maximum(0, t - dur * .7) / (dur * .25))
    return (y + breath) * env * vel


# the flute's phrases: yo steps above D4 (5 = D5), seconds; None breathes
PHRASES = [
    [(3, 1.4), (4, .9), (5, 2.2), (None, .6), (4, 1.0), (3, 2.4)],
    [(5, 1.2), (6, .9), (7, 1.8), (6, 1.0), (5, 2.6)],
    [(2, 1.0), (3, 1.0), (4, 1.6), (None, .5), (3, .9), (2, 1.0), (0, 2.6)],
    [(7, 1.6), (6, .8), (5, .8), (4, 1.4), (None, .5), (5, 2.8)],
]
BPM = 66
BARS = 32


def music():
    spb = SR * 60 / BPM
    L = int(round(BARS * 4 * spb))
    mix = LoopMix(L, circular=True)
    # the koto: a slow, gentle figure, two plucks a beat, rising and settling (with a few rests)
    pattern = [0, 2, 4, 5, 4, 2, 3, 1]
    for bar in range(BARS):
        shift = [0, 0, 1, 0, -1, 0, 2, 0][bar % 8]
        for k, step in enumerate(pattern):
            if (bar % 4 == 3 and k >= 6) or rng.uniform() < .08:
                continue
            at = int((bar * 4 + k * .5) * spb + rng.uniform(-.01, .01) * SR)
            m = yo(step + shift + 2, TONIC - 12)
            mix.add('koto', koto(m, 2.2, .55 + .15 * (k == 0)), at, -.3 + .1 * k)
        # the drone: open D and A, low, every two bars
        if bar % 2 == 0:
            for m, p in ((38, -.2), (45, .2)):
                mix.add('drone', koto(m, 6.0, .5, bend=False), int(bar * 4 * spb), p)
    # the flute, in long phrases with space between
    t = 2.0
    k = 0
    while t < L / SR - 6:
        for step, d in PHRASES[k % len(PHRASES)]:
            if step is not None:
                mix.add('flute', flute(yo(step), d * 1.15, .5), int(t * SR), .15)
            t += d
        t += rng.uniform(4, 7)
        k += 1
    levels = dict(koto=-19, drone=-25, flute=-19)
    out = np.zeros((2, L))
    for name, b in mix.buses.items():
        rms = np.sqrt(np.mean(b.mean(0) ** 2))
        out += b * 10 ** ((levels[name] - 20 * np.log10(max(rms, 1e-9))) / 20)
    out += conv_reverb(out * .4, make_ir(length=2.8, rt_low=2.0, rt_mid=2.2, rt_high=1.4), True) * .4
    out = np.stack([fft_filter(c, 40, 9000, 2, circular=True) for c in out])
    out *= 10 ** ((-21 - 20 * np.log10(np.sqrt(np.mean(out.mean(0) ** 2)))) / 20)
    return np.tanh(out / .7) * .7, L


def write(path, x, stereo):
    y = np.clip(x, -1, 1)
    with wave.open(path, 'wb') as w:
        w.setnchannels(2 if stereo else 1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(((y.T if stereo else y) * 32767).astype('<i2').tobytes())


def norm(x, peak_db):
    x = np.asarray(x, dtype=float)
    x = x / (np.max(np.abs(x)) + 1e-12) * 10 ** (peak_db / 20)
    fade = min(len(x), int(.004 * SR))
    x[-fade:] *= np.linspace(1, 0, fade)
    x[:8] *= np.linspace(0, 1, 8)
    return x


def room(x, wet=.25):
    y = np.stack([x, x])
    y = y + conv_reverb(y * .5, make_ir(length=1.4), False)[:, :len(x)] * wet
    return y.mean(0)


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(MASTERS, exist_ok=True)
    # the card slap: a sharp crack of thick card on felt, and a low thump
    n = int(.22 * SR)
    t = tarr(n)
    crack = fft_filter(noise(n), 1800, 9000) * np.exp(-t / .006)
    thump = np.sin(2 * np.pi * (140 + 120 * np.exp(-t / .01)) * t) * np.exp(-t / .03) * .7
    write(os.path.join(OUT, 'kk_place.wav'), norm(room(crack + thump, .15), -10), False)
    # a capture: two slaps, the second landing on the first
    y = np.zeros(int(.3 * SR))
    y[:n] += crack + thump
    s = int(.06 * SR)
    y[s:s + n] += (crack * .8 + thump * .5)
    write(os.path.join(OUT, 'kk_take.wav'), norm(room(y, .15), -9), False)
    # a card turned over: a soft swish and a light tap
    n = int(.18 * SR)
    t = tarr(n)
    write(os.path.join(OUT, 'kk_flip.wav'), norm(fft_filter(noise(n), 1500, 7000) * np.sin(np.pi * t / t[-1]) ** 2 * .6 + np.sin(2 * np.pi * 400 * t) * np.exp(-np.maximum(0, t - .12) / .01) * (t > .12) * .3, -16), False)
    # the shuffle: a riffle of many little cracks
    n = int(1.1 * SR)
    y = np.zeros(n)
    for k in range(40):
        at = int((.05 + k * .022 + rng.uniform(0, .01)) * SR)
        m = int(.02 * SR)
        y[at:at + m] += fft_filter(noise(m), 2000, 8000) * np.exp(-tarr(m) / .004) * rng.uniform(.4, 1)
    write(os.path.join(OUT, 'kk_shuffle.wav'), norm(y, -15), False)
    # a set made: a koto flourish up the scale
    y = np.zeros(int(2.2 * SR))
    for k, st in enumerate((5, 6, 7, 8, 10)):
        v = koto(yo(st), 1.6, .7)
        at = int(k * .07 * SR)
        y[at:at + len(v)] += v[:len(y) - at]
    write(os.path.join(MASTERS, 'kk_set.wav'), np.stack([norm(room(y, .35), -10)] * 2), True)
    # koi-koi: a taiko and a rising run
    n = int(2.4 * SR)
    t = tarr(n)
    taiko = np.sin(2 * np.pi * (60 + 50 * np.exp(-t / .05)) * t) * np.exp(-t / .35) + fft_filter(noise(n), 100, 900) * np.exp(-t / .06) * .4
    y = taiko.copy()
    for k, st in enumerate((3, 4, 5, 6, 7, 8, 9, 10)):
        v = koto(yo(st), 1.2, .5)
        at = int((.25 + k * .05) * SR)
        y[at:at + len(v)] += v[:n - at]
    write(os.path.join(MASTERS, 'kk_koikoi.wav'), np.stack([norm(room(y, .3), -8)] * 2), True)
    # stopping: hyoshigi, two wooden clappers struck twice
    y = np.zeros(int(.9 * SR))
    for at in (0, .22):
        m = int(.3 * SR)
        tt = tarr(m)
        clap = sum(a * np.sin(2 * np.pi * f * tt) * np.exp(-tt / d) for f, a, d in ((1850, 1, .05), (2960, .5, .03), (720, .4, .04))) + fft_filter(noise(m), 1500, 6000) * np.exp(-tt / .005)
        s = int(at * SR)
        y[s:s + m] += clap
    write(os.path.join(OUT, 'kk_stop.wav'), norm(room(y, .3), -9), False)
    # a round won: a bright koto chord; lost: a falling pair
    y = np.zeros(int(2.6 * SR))
    for k, st in enumerate((0, 2, 4, 5)):
        v = koto(yo(st), 2.2, .6)
        at = int(k * .04 * SR)
        y[at:at + len(v)] += v[:len(y) - at]
    write(os.path.join(MASTERS, 'kk_win.wav'), np.stack([norm(room(y, .35), -10)] * 2), True)
    y = np.zeros(int(2.2 * SR))
    for k, st in enumerate((4, 2)):
        v = koto(yo(st), 1.8, .55)
        at = int(k * .3 * SR)
        y[at:at + len(v)] += v[:len(y) - at]
    write(os.path.join(MASTERS, 'kk_lose.wav'), np.stack([norm(room(y, .35), -12)] * 2), True)
    n = int(.04 * SR)
    t = tarr(n)
    write(os.path.join(OUT, 'kk_click.wav'), norm(np.sin(2 * np.pi * 1500 * t) * np.exp(-t / .006), -22), False)
    # the music
    x, L = music()
    write(os.path.join(MASTERS, 'kk_music.wav'), x, True)
    for name in ('kk_music', 'kk_set', 'kk_koikoi', 'kk_win', 'kk_lose'):
        subprocess.run(['afconvert', '-f', 'm4af', '-d', 'aac', '-b', '192000', os.path.join(MASTERS, name + '.wav'), os.path.join(OUT, name + '.m4a')], check=True)
    json.dump({'music': [{'id': 'kk_music', 'bpm': BPM, 'bars': BARS, 'bar_samples': int(round(4 * SR * 60 / BPM)), 'loop_end_sample_exclusive': L, 'seconds': L / SR}]},
              open(os.path.join(OUT, 'koikoi_audio_manifest.json'), 'w'), indent=1)
    print('ok', L / SR)


if __name__ == '__main__':
    main()
