"""Maze 95 effects. Synthesized, no samples.

Soft steps on carpet, a turn's swish, a bump into brick, a locked door's
clunk, a pad's click, a door grinding down into the floor, the flip stone's
great roll, a bulb popping out and humming back, the elevator's motor and
its ding, a portal's shimmer, the marble rumbling, knocking and thudding,
someone appearing and vanishing, the click of a 1995 button, and the paint
snail: the sticky crackle of slime as it crawls, the suck and knock of it pulling into its
shell, and a small rising pip as it peeks out again. And the residents: Ceiling Cat's
meow, the goose's honk, the penny-farthing frog's bell, Mazeball's baby burp, Keyboard
Cat's riff, Monke's hoot, and the Thinking Face's own little glam-pop loop. Writes
48 kHz 16-bit mono WAVs into ../assets/audio.
"""
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'engine'))
from synth import SR, tarr, noise, fft_filter, mtof, ep, bass, vibes  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), '..', 'assets', 'audio')
rng = np.random.default_rng(95)


def write(name, x, peak_db):
    x = np.asarray(x, dtype=float)
    x = x / (np.max(np.abs(x)) + 1e-12) * 10 ** (peak_db / 20)
    fade = min(len(x), int(.005 * SR))
    x[-fade:] *= np.linspace(1, 0, fade)
    x[:8] *= np.linspace(0, 1, 8)
    with wave.open(os.path.join(OUT, name + '.wav'), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((x * 32767).astype('<i2').tobytes())


def sweep(f0, f1, dur, curve=1.0):
    n = int(dur * SR)
    u = np.linspace(0, 1, n) ** curve
    return np.sin(2 * np.pi * np.cumsum(f0 * (f1 / f0) ** u) / SR)


def env(n, att, tau):
    t = tarr(n)
    return np.minimum(1, t / max(att, 1e-6)) * np.exp(-t / tau)


def chime(freqs, dur, tau):
    n = int(dur * SR)
    t = tarr(n)
    return sum(a * np.sin(2 * np.pi * f * t) * np.exp(-t / tau) for f, a in freqs) * np.minimum(1, t / .002)


def main():
    os.makedirs(OUT, exist_ok=True)
    for i in range(3):  # carpet steps: a muffled pad of a footfall
        n = int(.16 * SR)
        t = tarr(n)
        write(f'mz_step_0{i + 1}', fft_filter(noise(n), 80, 700 + 120 * i) * env(n, .008, .035) + .5 * np.sin(2 * np.pi * (70 + 8 * i) * t) * env(n, .004, .04), -19)
    n = int(.22 * SR)
    t = tarr(n)
    write('mz_turn', fft_filter(noise(n), 300, 2500) * np.sin(np.pi * t / .22) ** 2, -26)
    n = int(.25 * SR)
    t = tarr(n)
    write('mz_bump', np.sin(2 * np.pi * (110 + 60 * np.exp(-t / .02)) * t) * env(n, .002, .06) + .3 * fft_filter(noise(n), 200, 2000) * env(n, .001, .02), -15)
    # locked: a heavy clunk and a short buzz
    n = int(.45 * SR)
    t = tarr(n)
    clunk = np.sin(2 * np.pi * 90 * t) * env(n, .002, .08) + .5 * fft_filter(noise(n), 400, 3000) * env(n, .001, .015)
    buzz = np.sign(np.sin(2 * np.pi * 110 * t)) * env(n, .15, .08) * (t > .15)
    write('mz_locked', clunk + fft_filter(buzz, 80, 1600) * .4, -13)
    # a pad pressed: a click and a bright two-note chime
    n = int(.6 * SR)
    t = tarr(n)
    click = fft_filter(noise(n), 2000, 9000) * env(n, .0005, .004)
    write('mz_pad', click + chime([(1318.5, 1), (1975.5, .6)], .6, .25), -14)
    # a door grinding down into the floor
    n = int(1.5 * SR)
    t = tarr(n)
    grind = fft_filter(noise(n), 60, 600) * (1 + .5 * np.sin(2 * np.pi * 11 * t)) * np.minimum(1, t / .1) * np.exp(-np.maximum(0, t - 1.2) / .08)
    motor = np.sign(np.sin(2 * np.pi * np.cumsum(55 - 15 * t / 1.5) / SR)) * .15
    thud = np.zeros(n)
    k = int(1.28 * SR)
    tt = tarr(n - k)
    thud[k:] = np.sin(2 * np.pi * 60 * tt) * np.exp(-tt / .08)
    write('mz_door', grind + fft_filter(motor, 40, 400) + thud, -12)
    # the flip stone: the world rolling over
    n = int(1.3 * SR)
    t = tarr(n)
    whoosh = fft_filter(noise(n), 200, 4000) * np.sin(np.pi * np.clip(t / 1.2, 0, 1)) ** 2
    roll = sweep(80, 320, 1.3, .7) * np.sin(np.pi * np.clip(t / 1.3, 0, 1)) * .5
    write('mz_flip', whoosh + roll + chime([(mtof(85), .4), (mtof(92), .3)], 1.3, .5) * (t > .9), -12)
    # bulbs: pop and fizz out, hum back on
    n = int(.6 * SR)
    t = tarr(n)
    pop = fft_filter(noise(n), 1000, 9000) * env(n, .0005, .01)
    fizz = fft_filter(noise(n), 3000, 9000) * env(n, .01, .12) * (1 + np.sign(np.sin(2 * np.pi * 47 * t))) * .2
    hum = np.sin(2 * np.pi * 120 * t) * np.exp(-t / .2) * .3
    write('mz_bulb_off', pop + fizz + hum * (t < .1), -12)
    n = int(.7 * SR)
    t = tarr(n)
    write('mz_bulb_on', (np.sin(2 * np.pi * 120 * t) + .5 * np.sin(2 * np.pi * 240 * t)) * np.minimum(1, t / .4) * np.exp(-np.maximum(0, t - .5) / .1) * .5
          + fft_filter(noise(n), 2000, 6000) * env(n, .001, .02) * .3, -16)
    # the elevator: a motor winding up, then the ding
    n = int(2.6 * SR)
    t = tarr(n)
    motor = np.sin(2 * np.pi * np.cumsum(90 + 50 * np.minimum(1, t / 2.2)) / SR) * np.minimum(1, t / .3) * np.exp(-np.maximum(0, t - 2.2) / .1)
    rattle = fft_filter(noise(n), 100, 900) * .3 * np.minimum(1, t / .3) * np.exp(-np.maximum(0, t - 2.2) / .1)
    ding = np.zeros(n)
    k = int(2.3 * SR)
    ding[k:] = chime([(mtof(84), 1), (mtof(88), .5)], (n - k) / SR, .4)[:n - k]
    write('mz_elevator', motor * .6 + rattle + ding * .8, -12)
    # a portal: a shimmer that falls and rises
    n = int(.8 * SR)
    t = tarr(n)
    shim = sum(np.sin(2 * np.pi * f * t * (1 + .3 * np.sin(2 * np.pi * 3 * t))) for f in (880, 1320, 1760)) * env(n, .01, .3)
    write('mz_portal', shim + fft_filter(noise(n), 3000, 10000) * env(n, .001, .05) * .5, -13)
    # the marble: a rumble as it rolls a square, a knock off a wall, a thud face to face
    n = int(.75 * SR)
    t = tarr(n)
    write('mz_marble_roll', fft_filter(noise(n), 30, 220) * np.sin(np.pi * t / .75) * (1 + .3 * np.sin(2 * np.pi * 9 * t)), -12)
    n = int(.4 * SR)
    t = tarr(n)
    write('mz_marble_bounce', np.sin(2 * np.pi * (140 + 80 * np.exp(-t / .01)) * t) * env(n, .001, .07) + .3 * fft_filter(noise(n), 500, 4000) * env(n, .0005, .01), -14)
    n = int(.7 * SR)
    t = tarr(n)
    write('mz_marble_bump', np.sin(2 * np.pi * (45 + 40 * np.exp(-t / .03)) * t) * env(n, .002, .25) + .3 * fft_filter(noise(n), 60, 400) * env(n, .002, .1), -10)
    # someone appears (a sparkle), and vanishes (a poof)
    n = int(.6 * SR)
    t = tarr(n)
    sparkle = sum(chime([(mtof(m), .5)], .6, .15) * 0 + np.roll(chime([(mtof(m), .5)], .6, .15), int(i * .05 * SR)) for i, m in enumerate((88, 91, 95, 100)))
    write('mz_appear', sparkle, -16)
    n = int(.4 * SR)
    t = tarr(n)
    write('mz_poof', fft_filter(noise(n), 300, 3000) * env(n, .005, .08) + .3 * sweep(600, 200, .4, 1) * env(n, .001, .1), -17)
    n = int(.05 * SR)
    t = tarr(n)
    write('mz_click', np.sin(2 * np.pi * 2200 * t) * np.exp(-t / .006), -22)
    snail()
    residents()
    print('ok')


def snail():
    """the paint snail, with its own noise so the effects above stay as they were"""
    own = np.random.default_rng(1995)
    n = int(.5 * SR)  # the crawl: sticky crackle, slime pulled off a surface, over a soft wet hush
    t = tarr(n)
    pops = np.zeros(n)
    for at in own.integers(0, n - 400, 130):  # tiny air pockets tearing open: clicks of noise, not tones
        k = int(own.uniform(.0006, .0025) * SR)
        pops[at:at + k] += own.standard_normal(k) * np.exp(-np.arange(k) / (k / 4)) * own.uniform(.2, 1) ** 2
    crackle = fft_filter(pops, 1200, 8000) * np.minimum(1, t / .06) * np.exp(-np.maximum(0, t - .25) / .12)
    hush = fft_filter(own.standard_normal(n), 300, 2500) * np.sin(np.pi * np.clip(t / .5, 0, 1)) ** 2
    write('mz_snail_squelch', crackle / np.max(np.abs(crackle)) + .35 * hush / np.max(np.abs(hush)), -22)
    n = int(.32 * SR)  # into the shell: a suck of air, then the shell's hollow knock on the floor
    t = tarr(n)
    suck = sweep(240, 70, .32, .5) * env(n, .004, .07)
    hiss = fft_filter(own.standard_normal(n), 600, 3500) * env(n, .002, .03)
    knock = np.where(t > .09, np.sin(2 * np.pi * 420 * (t - .09)) * np.exp(-np.maximum(0, t - .09) / .035), 0)
    write('mz_snail_hide', suck + .3 * hiss + .55 * knock, -17)
    n = int(.22 * SR)  # peeking out: a tiny rising pip and a little wet
    t = tarr(n)
    pip = sweep(520, 1150, .22, 1.4) * np.minimum(1, t / .02) * np.exp(-t / .07)
    write('mz_snail_peek', pip + .3 * fft_filter(own.standard_normal(n), 400, 2000) * env(n, .01, .05), -24)


if __name__ == '__main__':
    main()


def place(out, x, at):
    i = int(at * SR)
    n = min(len(x), len(out) - i)
    if n > 0:
        out[i:i + n] += x[:n]


def formant(x, centre, width):
    return fft_filter(x, max(30, centre - width), centre + width)


def residents():
    """the meme residents, each with their own noise so nothing above changes"""
    own = np.random.default_rng(2026)
    n = int(.62 * SR)  # a meow: the pitch rising then falling through an 'ee-ow' of a mouth
    t = tarr(n)
    f0 = 520 + 260 * np.sin(np.pi * np.clip(t / .5, 0, 1)) ** 1.5 - 120 * t
    ph = 2 * np.pi * np.cumsum(f0 * (1 + .012 * np.sin(2 * np.pi * 6 * t))) / SR
    voice = sum(np.sin(ph * k) / k ** 1.2 for k in range(1, 9))
    shape = np.minimum(1, t / .05) * np.exp(-np.maximum(0, t - .32) / .1)
    opening = np.sin(np.pi * np.clip(t / .55, 0, 1))  # the mouth opens on the 'ee' and closes on the 'ow'
    mouth = formant(voice, 900, 500) * (1 - opening) + formant(voice, 2000, 700) * opening + .5 * formant(voice, 2600, 500)
    write('mz_meow', mouth * shape, -15)
    n = int(.36 * SR)  # a honk: harsh, nasal, a little flat at the end
    t = tarr(n)
    f0 = 400 - 70 * t / .36
    ph = 2 * np.pi * np.cumsum(f0) / SR
    saw = sum(np.sin(ph * k) / k for k in range(1, 24))
    honk = formant(saw, 1300, 450) + .6 * formant(saw, 2900, 600) + .25 * fft_filter(own.standard_normal(n), 900, 4000)
    write('mz_honk', np.tanh(honk * 2.5) * np.minimum(1, t / .015) * np.exp(-np.maximum(0, t - .22) / .05), -12)
    n = int(1.0 * SR)  # the bicycle bell: ding-ding, struck twice
    out = np.zeros(n)
    for at in (0, .17):
        m = int(.8 * SR)
        tt = tarr(m)
        ding = sum(a * np.sin(2 * np.pi * 2350 * r * tt) * np.exp(-tt / d) for r, a, d in ((1, 1, .5), (2.76, .5, .25), (5.4, .25, .1), (1.5, .3, .35)))
        place(out, ding * np.minimum(1, tt / .002), at)
    write('mz_bell', out, -14)
    n = int(.42 * SR)  # a baby's burp: a short, wet, low croak
    t = tarr(n)
    f0 = 140 - 50 * t / .42
    pulses = np.sin(2 * np.pi * np.cumsum(f0) / SR)
    pulses = np.sign(pulses) * np.abs(pulses) ** .3
    burp = formant(pulses, 420, 260) + .5 * formant(pulses, 1100, 300) + .15 * fft_filter(own.standard_normal(n), 200, 1600)
    burp *= np.minimum(1, t / .02) * np.exp(-np.maximum(0, t - .18) / .09) * (1 + .4 * np.sin(2 * np.pi * 23 * t))
    write('mz_burp', burp, -13)
    n = int(1.9 * SR)  # Keyboard Cat's riff: a jaunty little run on an electric piano (our own tune)
    out = np.zeros(n)
    beat = .16
    for k, m in enumerate((67, 71, 74, 79, 78, 74, 76, 72, 67)):
        place(out, ep(m, beat * (2 if k == 8 else .9), .75), k * beat)
    for k, m in enumerate((43, 47, 50, 43)):
        place(out, bass(m, beat * 2, .7), k * beat * 2.25)
    write('mz_keys', out, -14)
    n = int(.9 * SR)  # Monke: ooh ooh
    out = np.zeros(n)
    for at, base in ((0, 330), (.32, 380)):
        m = int(.34 * SR)
        tt = tarr(m)
        f0 = base + 160 * np.clip(tt / .2, 0, 1)
        v = np.sin(2 * np.pi * np.cumsum(f0) / SR) + .3 * np.sin(4 * np.pi * np.cumsum(f0) / SR)
        place(out, formant(v, 500, 300) * np.sin(np.pi * tt / .34) ** .8, at)
    write('mz_ooh', out, -15)
    thinker_loop(own)


def thinker_loop(own):
    """the Thinking Face's own music: two bars at 120, glam, peachy and upbeat, looping cleanly"""
    bar = 2.0
    n = int(2 * bar * SR)
    out = np.zeros(n + int(2 * SR))
    q = bar / 4
    chords = ((65, 69, 72, 76), (64, 67, 71, 74))   # Fmaj7, Em7
    for b, chord in enumerate(chords):
        for k in range(4):
            for m in chord:
                place(out, ep(m, q * .45, .55, 1.3) * .5, b * bar + k * q + q * .5)   # off-beat stabs
        for k, m in enumerate((41, 41, 53, 48) if b == 0 else (40, 40, 52, 47)):
            place(out, bass(m, q * .8, .8), b * bar + k * q)
    for k in range(8):  # four on the floor, with handclaps on two and four
        tt = tarr(int(.3 * SR))
        kick = np.sin(2 * np.pi * np.cumsum(50 + 110 * np.exp(-tt / .03)) / SR) * np.exp(-tt / .14)
        place(out, kick * .9, k * q)
        if k % 2:
            clap = fft_filter(own.standard_normal(len(tt)), 900, 6000) * np.exp(-tt / .05)
            place(out, clap * .45, k * q)
    for k in range(16):  # sparkle: a vibraphone arpeggio in sixteenths
        m = (77, 81, 84, 88)[k % 4] if k < 8 else (76, 79, 83, 86)[k % 4]
        place(out, vibes(m, q * .4, .35) * .35, k * q / 2)
    loop = out[:n].copy()
    loop[:len(out) - n] += out[n:]  # the tails wrap round, so the loop has no seam
    write('mz_thinker', loop, -10)
