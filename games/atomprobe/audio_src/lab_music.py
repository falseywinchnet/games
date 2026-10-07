"""Atom Probe score: "Black Box".

A cool, dark synth theme in E minor at 100 BPM, for a sealed chamber of fog
and light: a pulsing eighth-note bass, a glassy sixteenth-note arpeggio
through a ping-pong delay, wide pads that breathe with the kick, a gated
snare, and a slow searching lead in the second half. One seamless 32-bar loop
(an 8-bar intro over A, then B, then A and B again under the lead).

Plus stingers: every atom found, some atoms missed, and a top-score fanfare.
Everything is synthesized (engine/synth.py and the timbres below); no samples.
Writes 48 kHz 16-bit WAV masters into masters/ and the manifest (exact loop
and bar lengths) into ../assets/audio. The game ships AAC encodings of the
masters (afconvert -f m4af -d aac -b 192000); their decoded lengths match the
manifest's loop points exactly.
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'engine'))
from synth import SR, mtof, tarr, gate, noise, fft_filter, wt, pad, kick, hat, make_ir, conv_reverb, pingpong, LoopMix  # noqa: E402

OUT = os.path.join(os.path.dirname(__file__), '..', 'assets', 'audio')
MASTERS = os.path.join(os.path.dirname(__file__), 'masters')
rng = np.random.default_rng(1964)

BPM = 100
SPB = SR * 60 // BPM          # samples per beat (28800, exact)
BARS = 32
L = BARS * 4 * SPB


# ------------------------------------------------------------------ timbres
def pluck_syn(m, dur, vel=.6, bright=1.0):
    """A glassy synth pluck: detuned saw and square, a fast low-pass sweep, a short tail."""
    f = mtof(m)
    n = int((dur + .25) * SR)
    t = tarr(n)
    fr = f * np.ones(n)
    y = wt('saw', fr, 30, rng.uniform()) * .6 + wt('saw', fr * 1.006, 30, rng.uniform()) * .6
    y += np.sign(np.sin(2 * np.pi * f * t + rng.uniform(0, 6))) * .25
    dark = fft_filter(y, 120, f * 1.5, 2)
    br = fft_filter(y, 120, f * 7 * bright, 2)
    y = dark + (br - dark) * np.exp(-t / .05)
    return y * np.exp(-t / .22) * gate(n, 10, .002) * (.3 + .7 * vel) * .5


def bass_syn(m, dur, vel=.7):
    """Rounded saw bass with a plucky filter envelope and a sine sub."""
    f = mtof(m)
    rel = .04
    n = int((dur + rel) * SR)
    t = tarr(n)
    fr = f * np.ones(n)
    y = wt('saw', fr, 40) + wt('saw', fr * 1.003, 40, .3)
    dark = fft_filter(y, 30, f * 2.2, 2)
    br = fft_filter(y, 30, f * 9, 2)
    y = dark + (br - dark) * np.exp(-t / .07) * .8
    y = y * .45 + np.sin(2 * np.pi * f * t) * .8
    return y * gate(n, dur, .003, rel) * (.4 + .6 * vel)


def lead_syn(m, dur, vel=.6):
    """A searching lead: two detuned saws, slow vibrato, soft attack, low-passed."""
    f = mtof(m)
    rel = .35
    n = int((dur + rel) * SR)
    t = tarr(n)
    vib = .006 * np.clip((t - .25) / .4, 0, 1) * np.sin(2 * np.pi * 5.2 * t)
    glide = -.012 * np.exp(-t / .05)
    fr = f * (1 + vib + glide)
    y = wt('saw', fr, 30) + wt('saw', fr * 1.005, 30, .41) + .5 * np.sin(2 * np.pi * np.cumsum(fr * .5) / SR)
    y = fft_filter(y, 150, 2600, 2)
    return y * gate(n, dur, .06, rel) * (.35 + .65 * vel) * .35


def snare(vel=.7):
    """A gated electronic snare: a tuned body and a bright noise burst."""
    n = int(.45 * SR)
    t = tarr(n)
    body = np.sin(2 * np.pi * (185 + 60 * np.exp(-t / .01)) * t) * np.exp(-t / .07)
    nz = fft_filter(noise(n), 1200, 9000) * np.exp(-t / .12)
    y = body * .6 + nz / (np.std(nz[:2000]) + 1e-9) * .25
    y *= np.where(t < .2, 1, np.exp(-(t - .2) / .02))  # the gate
    return y * vel


def open_hat(vel=.4):
    n = int(.4 * SR)
    t = tarr(n)
    y = fft_filter(noise(n), 7000, 16000) * np.exp(-t / .12)
    return y / (np.std(y[:3000]) + 1e-9) * .2 * vel


def sonar(m, vel=.5):
    """A distant sonar ping: a pure tone with a long shimmering tail."""
    f = mtof(m)
    n = int(2.4 * SR)
    t = tarr(n)
    y = np.sin(2 * np.pi * f * t) * np.exp(-t / .6) + .2 * np.sin(2 * np.pi * f * 2.01 * t) * np.exp(-t / .3)
    return y * gate(n, 10, .004) * vel


def swell(dur, vel=.4):
    """Filtered noise rising into a bar line."""
    n = int(dur * SR)
    t = tarr(n)
    y = fft_filter(noise(n), 400, 5000) * (t / dur) ** 2
    return y / (np.std(y) + 1e-9) * vel * .2


# ------------------------------------------------------------------ the tune
CH = {
    'Em9': (40, [52, 55, 59, 62, 66]),
    'Cmaj7': (36, [48, 52, 55, 59, 62]),
    'Am9': (33, [45, 48, 52, 55, 59]),
    'B7sus4': (35, [47, 52, 54, 57]),
    'B7': (35, [47, 51, 54, 57]),
    'D6': (38, [50, 54, 57, 59]),
    'Bm7': (35, [47, 50, 54, 57]),
}
A = ['Em9', 'Cmaj7', 'Am9', 'B7sus4', 'Em9', 'Cmaj7', 'Am9', 'B7']
B = ['Cmaj7', 'D6', 'Em9', 'Em9', 'Am9', 'Bm7', 'Cmaj7', 'B7']
FORM = A + B + A + B  # the intro (A, sparse), B, then A and B again under the lead

LEAD = {  # bar -> [(beat, midi, beats)]
    16: [(0, 71, 1.5), (1.5, 74, .5), (2, 76, 2)],
    17: [(0, 79, 1), (1, 76, 1), (2, 74, 1.5), (3.5, 71, .5)],
    18: [(0, 72, 3), (3, 71, 1)],
    19: [(0, 69, 2), (2, 71, 2)],
    20: [(0, 71, 1.5), (1.5, 74, .5), (2, 76, 1), (3, 79, 1)],
    21: [(0, 81, 2), (2, 79, 1), (3, 76, 1)],
    22: [(0, 74, 2), (2, 72, 1), (3, 74, 1)],
    23: [(0, 71, 3.5)],
    24: [(0, 76, 2), (2, 79, 2)],
    25: [(0, 78, 2), (2, 74, 2)],
    26: [(0, 79, 3), (3, 78, 1)],
    27: [(0, 76, 4)],
    28: [(0, 72, 2), (2, 76, 2)],
    29: [(0, 74, 2), (2, 78, 2)],
    30: [(0, 79, 2), (2, 76, 1), (3, 74, 1)],
    31: [(0, 75, 2), (2, 78, 2)],
}


def at(bar, beat=0.0):
    return int(round((bar * 4 + beat) * SPB))


def arrange():
    mix = LoopMix(L, circular=True)
    pump = np.ones(L)  # the kick's sidechain: everything sustained ducks and breathes
    for bar, name in enumerate(FORM):
        root, tones = CH[name]
        intro = bar < 8
        # pads: the whole bar, a little behind the beat
        for k, m in enumerate(tones):
            mix.add('pad', pad(m, 4 * SPB / SR * .98, .5, att=.35, rel=1.2, cutoff=1600 if intro else 2200), at(bar, .02), (k - 2) * .25)
        # the arpeggio: up through the chord and over the octave, sixteenth notes
        seq = tones + [tones[1] + 12, tones[2] + 12]
        order = [0, 2, 1, 3, 2, 4, 3, 5, 4, 6, 5, 3, 4, 2, 3, 1]
        for i in range(16):
            m = seq[order[i] % len(seq)] + 12
            v = .55 + (.25 if i % 4 == 0 else 0) + rng.uniform(-.05, .05)
            mix.add('arp', pluck_syn(m, .16, v, .7 if intro else 1.0), at(bar, i * .25), .35 * np.sin(i * .9))
        # bass from bar 4: eighths on the root, an octave leap at the end of the bar
        if bar >= 4:
            pat = [0, 0, 12, 0, 0, 0, 12, 7]
            for i, iv in enumerate(pat):
                mix.add('bass', bass_syn(root + iv, .22, .75 if i % 2 == 0 else .6), at(bar, i * .5), 0)
        # drums: a hint in the intro, the full kit from bar 8
        if bar >= 8:
            for b in (0, 2):
                mix.add('drums', kick(.9), at(bar, b), 0)
            if bar % 4 == 3:
                mix.add('drums', kick(.7), at(bar, 2.75), 0)
            for b in (1, 3):
                mix.add('snare', snare(.8), at(bar, b), 0)
            for i in range(8):
                mix.add('hats', hat(.5 if i % 2 else .3), at(bar, i * .5 + (.04 if i % 2 else 0)), .3)
            mix.add('hats', open_hat(.6), at(bar, 3.5), -.3)
            for b in (0, 2):
                s = at(bar, b)
                tt = tarr(SPB)
                env = 1 - .45 * np.exp(-tt / .1)
                idx = (s + np.arange(SPB)) % L
                pump[idx] = np.minimum(pump[idx], env)
        elif bar >= 4:
            mix.add('drums', kick(.6), at(bar, 0), 0)
            for i in range(4):
                mix.add('hats', hat(.25), at(bar, i + .5), .3)
        # a sonar ping now and then, far away
        if bar % 8 == 0:
            mix.add('fx', sonar(88, .4), at(bar, 0), -.5)
        if bar % 8 == 4:
            mix.add('fx', sonar(83, .3), at(bar, 2), .5)
        if bar % 8 == 7:
            mix.add('fx', swell(2 * SPB / SR, .6), at(bar, 2), 0)
        # the lead
        for (beat, m, d) in LEAD.get(bar, []):
            mix.add('lead', lead_syn(m, d * SPB / SR * .95, .7), at(bar, beat), .05)
    return mix, pump


LEVELS = dict(pad=-21, arp=-20, bass=-15, drums=-15, snare=-19, hats=-26, fx=-27, lead=-17)


def bus_rms_db(b):
    m = b.mean(0)
    r = np.sqrt(np.mean(m ** 2))
    return 20 * np.log10(max(r, 1e-9))


def master(mix, pump, wet=.26):
    out = np.zeros((2, L))
    send = np.zeros((2, L))
    for name, b in mix.buses.items():
        g = 10 ** ((LEVELS.get(name, -20) - bus_rms_db(b)) / 20)
        b = b * g
        if name in ('pad', 'arp', 'bass', 'lead'):
            b = b * pump
        if name == 'arp':
            b = b + pingpong(b, int(SPB * .75), fb=.42, n=5, lp=4200) * .7
        if name == 'lead':
            b = b + pingpong(b, int(SPB * 1.5), fb=.35, n=4, lp=3000) * .45
        out += b
        send += b * {'snare': .6, 'lead': .4, 'pad': .3, 'arp': .25, 'fx': .5}.get(name, .08)
    ir = make_ir(length=2.8, rt_low=2.2, rt_mid=1.8, rt_high=.9)
    out += conv_reverb(send, ir, True) * wet
    out = np.stack([fft_filter(c, 28, None, 2, circular=True) for c in out])
    return out


def finish(x, target_rms_db=-19.0, ceiling_db=-3.0):
    x = x * 10 ** ((target_rms_db - bus_rms_db(x)) / 20)
    c = 10 ** (ceiling_db / 20)
    return np.tanh(x / c) * c


def write(path, x):
    import wave
    y = np.clip(x, -1, 1)
    y16 = (y.T * 32767).astype('<i2')
    with wave.open(path, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(y16.tobytes())


def stinger(kind):
    n = int(4.5 * SR)
    buses = {}

    def put(bus, y, beat, pan=0.0):
        s = int(beat * SPB)
        b = buses.setdefault(bus, np.zeros((2, n)))
        e = min(n, s + len(y))
        gl, gr = np.cos((pan + 1) * np.pi / 4), np.sin((pan + 1) * np.pi / 4)
        b[0, s:e] += gl * y[:e - s]
        b[1, s:e] += gr * y[:e - s]

    if kind == 'win':  # every atom found: the arpeggio climbs into E major and blooms
        seq = [52, 56, 59, 63, 64, 68, 71, 75, 76, 80, 83, 88]
        for i, m in enumerate(seq):
            put('arp', pluck_syn(m, .2, .7), i * .125, .4 * np.sin(i))
        for m in (40, 52, 56, 59, 63, 66):
            put('pad', pad(m, 3.0, .6, att=.08, rel=1.4, cutoff=3200), 1.5)
        put('lead', lead_syn(83, 2.2, .8), 1.5)
        put('fx', sonar(88, .5), 1.5)
        put('drums', kick(.9), 1.5)
    elif kind == 'miss':  # atoms missed: the light drains out, down into the low E
        for i, m in enumerate([71, 67, 64, 59, 55, 52]):
            put('arp', pluck_syn(m, .3, .6, .6), i * .25, -.3 + .12 * i)
        for m in (40, 47, 52, 55):
            put('pad', pad(m, 2.6, .55, att=.2, rel=1.4, cutoff=1100), 1.5)
        put('lead', lead_syn(64, .8, .6), 1.5)
        put('lead', lead_syn(63, 1.6, .5), 2.25)
        put('drums', kick(.7), 1.5)
    else:  # top score: a fanfare of the theme, in the major
        for i, (m, d) in enumerate([(71, .5), (76, .5), (80, .5), (83, 2.2)]):
            put('lead', lead_syn(m, d * SPB / SR, .85), [0, .5, 1, 1.5][i])
        for m in (40, 52, 56, 59, 64, 68):
            put('pad', pad(m, 3.0, .6, att=.05, rel=1.4, cutoff=3500), 1.5)
        for i in range(16):
            put('arp', pluck_syn([64, 68, 71, 76][i % 4] + 12, .15, .6), 1.5 + i * .125, .4 * np.sin(i))
        put('drums', kick(.9), 1.5)
        put('snare', snare(.8), 2.5)
    out = np.zeros((2, n))
    send = np.zeros((2, n))
    for name, b in buses.items():
        g = {'pad': .5, 'arp': .9, 'lead': 1.0, 'fx': .5, 'drums': .9, 'snare': .5}.get(name, .7)
        out += b * g
        send += b * g * .3
    ir = make_ir(length=2.4)
    out += conv_reverb(send, ir, False)[:, :n] * .35
    fade = int(.4 * SR)
    out[:, -fade:] *= np.linspace(1, 0, fade)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(MASTERS, exist_ok=True)
    manifest = {'music': [], 'stingers': []}
    mix, pump = arrange()
    x = finish(master(mix, pump))
    write(os.path.join(MASTERS, 'ap_music.wav'), x)
    manifest['music'].append({'id': 'ap_music', 'bpm': BPM, 'bars': BARS, 'bar_samples': 4 * SPB,
                              'loop_end_sample_exclusive': L, 'seconds': round(L / SR, 3)})
    print('ap_music', BPM, 'BPM', round(L / SR, 2), 's')
    for kind in ('win', 'miss', 'topscore'):
        y = finish(stinger(kind), -18, -2.5)
        write(os.path.join(MASTERS, f'ap_stinger_{kind}.wav'), y)
        manifest['stingers'].append(f'ap_stinger_{kind}')
    json.dump(manifest, open(os.path.join(OUT, 'atomprobe_audio_manifest.json'), 'w'), indent=1)


if __name__ == '__main__':
    main()
