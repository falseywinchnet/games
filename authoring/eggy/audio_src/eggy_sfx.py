"""Eggy's sound effects and ambience beds (all synthesised).
python3 eggy_sfx.py -> ../assets/audio/eggy_*.m4a (effects) and eggy_amb_*.wav (gapless beds)
"""
import json, os, subprocess
import numpy as np
import eggy_synth  # noqa: F401
import synth as S
from synth import SR, tarr, fft_filter, noise, write_wav, render_note
from sfx import env, place_at, verb, finish
from sfx2 import seq, mix, swoosh, glide

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'assets', 'audio')
WORK = os.path.join(HERE, 'out')
rng = np.random.default_rng(2026)


def nzb(n, lo, hi, seed):
    x = fft_filter(noise(n, seed), lo, hi)
    return x / (np.abs(x).max() + 1e-12)


def peep(f0, dur, up=1.25, down=.9, amp=1.0):
    """One duckling peep: a quick rise then fall in pitch, soft and round."""
    n = int(dur * SR)
    t = tarr(n) / dur
    f = f0 * np.where(t < .35, 1 + (up - 1) * (t / .35), up + (down - up) * ((t - .35) / .65))
    ph = 2 * np.pi * np.cumsum(f) / SR
    e = np.sin(np.pi * np.clip(t, 0, 1)) ** 1.5
    return (np.sin(ph) + .18 * np.sin(2 * ph) + .05 * np.sin(3 * ph)) * e * amp


def chirps():
    out = {}
    specs = [[(3000, .09, 0)], [(3300, .07, 0), (3500, .06, .1)], [(2800, .12, 0)], [(3600, .05, 0), (3800, .05, .07), (4000, .05, .14)],
             [(3100, .1, 0)], [(2600, .08, 0), (3400, .08, .11)]]
    for i, sp in enumerate(specs):
        y = np.zeros(int(.4 * SR))
        for f, d, t0 in sp:
            place_at(y, peep(f * rng.uniform(.95, 1.05), d, rng.uniform(1.15, 1.35), rng.uniform(.82, .95)), t0)
        out[f'eggy_chirp_{i + 1:02d}'] = (verb(y, .08, .15), -17)
    return out


def steps():
    out = {}
    n = int(.08 * SR)
    t = tarr(n)
    out['eggy_step_grass'] = (nzb(n, 300, 2500, 1) * env(n, .002, .015) * .8 + nzb(n, 4000, 9000, 2) * env(n, .001, .008) * .3, -27)
    y = np.zeros(int(.09 * SR))
    for k in range(6):
        g = int(.006 * SR)
        place_at(y, nzb(g, 800, 5000, 10 + k) * env(g, .0005, .002) * rng.uniform(.4, 1), k * .012)
    out['eggy_step_snow'] = (y, -25)
    out['eggy_step_rock'] = (render_note('litho', 96, .1, .3) * .4 + np.pad(nzb(int(.01 * SR), 2000, 9000, 3) * env(int(.01 * SR), .0003, .002), (0, int(.69 * SR))), -27)
    out['eggy_step_ice'] = (np.sin(2 * np.pi * 3100 * t) * env(n, .0005, .02) + .3 * np.sin(2 * np.pi * 4700 * t) * env(n, .0005, .01), -28)
    y = np.zeros(int(.25 * SR))
    place_at(y, nzb(int(.06 * SR), 500, 4000, 4) * env(int(.06 * SR), .002, .015), 0)
    place_at(y, glide(600, 1300, .06, amp=.4, tau=.03), .03)
    out['eggy_step_water'] = (y, -23)
    return out


def bigger():
    out = {}
    out['eggy_hop'] = (mix((glide(520, 950, .1, amp=.5, tau=.06), 0, 1), (swoosh(.12, 1500, 6000, 21), 0, .25)), -19)
    n = int(.2 * SR)
    t = tarr(n)
    out['eggy_land'] = (np.sin(2 * np.pi * 120 * t) * env(n, .002, .03) + .4 * nzb(n, 200, 1500, 5) * env(n, .001, .02), -19)
    y = np.zeros(int(1.0 * SR))
    place_at(y, nzb(int(.3 * SR), 400, 6000, 6) * env(int(.3 * SR), .003, .07), 0)
    for k in range(6):
        place_at(y, glide(rng.uniform(500, 900), rng.uniform(1100, 1800), .05, amp=.25, tau=.025), .05 + k * .07 + rng.uniform(0, .03))
    out['eggy_splash'] = (verb(y, .1, .3), -15)
    y = np.zeros(int(1.3 * SR))
    for k in range(3):
        place_at(y, glide(720, 340, .12, amp=.6, tau=.06), .1 + k * .32)
    out['eggy_drink'] = (verb(y, .1, .3), -17)
    out['eggy_refresh'] = (verb(mix((seq([84, 88, 91, 96], 'celesta', .07, .55, .6), 0, .5), (render_note('glass', 84, .8, .4), .25, .25)), .35, .9), -12)
    y = np.zeros(int(1.6 * SR))
    for k in range(4):
        hn = int(.12 * SR)
        place_at(y, nzb(hn, 900, 2200, 30 + k) * np.sin(np.pi * np.linspace(0, 1, hn)) ** 2 * (.9 - .12 * k), .05 + k * .33)
    out['eggy_pant'] = (y, -21)
    whirr_n = int(.8 * SR)
    wt_ = tarr(whirr_n)
    whirr = np.sin(2 * np.pi * np.cumsum(1400 + 400 * np.sin(2 * np.pi * 9 * wt_)) / SR) * env(whirr_n, .02, .25) * .25
    out['eggy_bonk'] = (verb(mix((S.woodblock(.6, False), 0, .7), (glide(300, 160, .15, amp=.7, tau=.05), 0, 1), (whirr, .12, 1)), .15, .4), -12)
    sh = np.zeros(int(2.2 * SR))
    for k in range(10):
        place_at(sh, render_note('ping', int(rng.choice([88, 91, 93, 95, 98, 100])), .1, .35) * .3, .2 + k * .06)
    out['eggy_star'] = (verb(mix((seq([79, 83, 86, 91, 95], 'celesta', .06, .6, .5), 0, .6), (sh, 0, 1)), .4, 1.2), -10)
    gn = int(2.2 * SR)
    gt = tarr(gn) / 2.2
    gust = fft_filter(noise(gn, 40), 300, 3000) * np.sin(np.pi * gt) ** 1.5
    gust = fft_filter(gust, 200, 2500)
    out['eggy_gust'] = (gust / np.abs(gust).max(), -15)
    sn = int(.9 * SR)
    out['eggy_slide'] = (mix((nzb(sn, 3000, 11000, 41) * np.sin(np.pi * np.linspace(0, 1, sn)) * .4, 0, 1), (glide(1600, 700, .6, amp=.25, tau=.4), .1, 1)), -17)
    y = np.zeros(int(.8 * SR))
    for k in range(5):
        bn = int(.07 * SR)
        place_at(y, nzb(bn, 2000, 7000, 50 + k) * env(bn, .005, .02) * rng.uniform(.5, 1), k * .13 + rng.uniform(0, .04))
    out['eggy_preen'] = (y, -23)
    y = np.zeros(int(.7 * SR))
    for k in range(4):
        place_at(y, swoosh(.09, 800, 4000, 60 + k, True, .5), k * .1)
    out['eggy_flap'] = (y, -19)
    out['eggy_milestone'] = (verb(seq([(74, 0), (78, .14), (81, .28), (86, .42)], 'brass', .14, .6, .3), .3, .8), -12)
    out['eggy_biome'] = (verb(mix((render_note('vibes', 81, 1.2, .4), 0, .5), (render_note('bowl', 69, 1.5, .4), 0, .4)), .4, 1.2), -16)
    y = np.zeros(int(.8 * SR))
    for k in range(18):
        tn = int(.012 * SR)
        place_at(y, nzb(tn, 1500, 6000, 70 + k) * env(tn, .0005, .004), .05 + k * .03)
    out['eggy_tent'] = (verb(y, .1, .3), -15)
    out['eggy_salute'] = (verb(mix((S.DRUMS['snare'](.6, 1), 0, .6), (render_note('brass', 81, .25, .55), .02, .4)), .2, .5), -12)
    out['eggy_medal'] = (verb(mix((render_note('vibes', 93, .6, .6), 0, .6), (S.DRUMS['anvil'](.3, 2), 0, .25), (seq([96, 100, 103], 'celesta', .05, .4), .1, .3)), .35, 1.0), -10)
    out['eggy_ui_click'] = (S.woodblock(.4, True) * .6, -19)
    out['eggy_ui_key'] = (S.tick(.5, 3) * .7, -21)
    return out


def thunder(near):
    n = int((5.5 if near else 7.0) * SR)
    t = tarr(n)
    rumble = fft_filter(noise(n, 300 + near), 25, 260 if near else 160)
    swell = np.exp(-t / (1.6 if near else 2.6)) * (1 - np.exp(-t / (.02 if near else .35)))
    roll = 1 + .5 * np.sin(2 * np.pi * .7 * t) * np.sin(2 * np.pi * 1.9 * t + 1)
    y = rumble / np.abs(rumble).max() * swell * roll
    if near:  # the sharp crack of a close strike
        k = int(.25 * SR)
        y[:k] += 1.2 * nzb(k, 1500, 9000, 310) * env(k, .001, .04)
    return verb(y, .2, 1.0)


def tree_fall():
    y = np.zeros(int(4.0 * SR))
    for k in range(10):  # creaks
        cn = int(.15 * SR)
        f0 = 140 + 30 * k
        place_at(y, glide(f0, f0 * 1.4, .15, 'reed', .12, .08) * (.4 + k * .06), .1 + k * .2)
    cn = int(1.4 * SR)
    ct = tarr(cn)
    crash = fft_filter(noise(cn, 320), 60, 1800) * np.exp(-ct / .3) + np.sin(2 * np.pi * 55 * ct) * np.exp(-ct / .25) * .8
    place_at(y, crash / np.abs(crash).max(), 2.35)
    for k in range(8):  # leaves and branches settling
        place_at(y, nzb(int(.06 * SR), 1500, 7000, 330 + k) * env(int(.06 * SR), .003, .02) * .3, 2.5 + k * .09)
    return verb(y, .3, 1.0)


def ribbit():
    y = np.zeros(int(.6 * SR))
    for k in range(2):
        n = int(.11 * SR)
        t = tarr(n)
        f = 220 * (1 + .25 * np.sin(2 * np.pi * 30 * t))
        place_at(y, np.sign(np.sin(2 * np.pi * np.cumsum(f) / SR)) * .3 * np.sin(np.pi * np.linspace(0, 1, n)) ** 2, k * .16)
    return fft_filter(verb(y, .1, .2), 120, 2500)


def fanfare():
    y = np.zeros(int(6.5 * SR))
    notes = [(62, 0, .2), (62, .25, .2), (62, .5, .2), (69, .75, .6), (66, 1.4, .25), (69, 1.65, .25), (74, 1.9, 1.6)]
    for m, t, d in notes:
        place_at(y, render_note('brass', m, d, .7) * .45, t)
        place_at(y, render_note('flute', m + 12, d, .5) * .2, t)
    for m in (50, 57, 62, 66, 69):
        place_at(y, render_note('brass', m, 2.6, .5) * .14, 1.9)
    place_at(y, render_note('tuba', 38, 2.6, .7) * .5, 1.9)
    for k in range(24):
        place_at(y, S.DRUMS['ghost'](.12 + .02 * k, k) * .8, k * .075)
    place_at(y, S.DRUMS['snare'](.6, 3), 1.9)
    place_at(y, S.triangle(.25, 4), 1.9)
    place_at(y, render_note('bowl', 62, 3, .5) * .4, 1.9)
    return verb(y, .35, 2.0)


def ambience():
    """Seamless 20 s stereo beds rendered circularly."""
    L = 20 * SR
    beds = {}
    t = np.arange(L) / SR
    # wind: filtered noise, slow swells with whole cycles in the loop
    w = np.stack([fft_filter(noise(L, 81 + c), 150, 1800, circular=True) for c in range(2)])
    sw = .55 + .45 * np.sin(2 * np.pi * 3 * t / 20) * np.sin(2 * np.pi * 2 * t / 20 + 1)
    beds['eggy_amb_wind'] = w * sw
    # brook: babble = band noise + many bubble blips placed circularly
    b = np.stack([fft_filter(noise(L, 91 + c), 600, 5000, circular=True) * .35 for c in range(2)])
    for k in range(900):
        s0 = int(rng.uniform(0, L))
        f0 = rng.uniform(500, 1600)
        d = rng.uniform(.015, .05)
        bl = glide(f0, f0 * rng.uniform(1.2, 1.8), d, amp=rng.uniform(.1, .35), tau=d / 2)
        ch = k % 2
        idx = (s0 + np.arange(len(bl))) % L
        b[ch, idx] += bl
    beds['eggy_amb_brook'] = b
    # forest: soft leaf hush with slow movement (no creatures)
    f = np.stack([fft_filter(noise(L, 101 + c), 1500, 7000, circular=True) for c in range(2)])
    fs = .5 + .5 * np.sin(2 * np.pi * 5 * t / 20 + np.array([[0], [1.3]]))
    beds['eggy_amb_forest'] = f * fs * .6
    # heavy rain: dense hiss plus a patter of drops
    rn = np.stack([fft_filter(noise(L, 111 + c), 400, 9000, circular=True) * .5 for c in range(2)])
    for k in range(6000):
        s0 = int(rng.uniform(0, L))
        dn = int(.004 * SR)
        drop = fft_filter(noise(dn, 2000 + k % 50), 1500, 8000) * np.linspace(1, 0, dn) * rng.uniform(.2, .8)
        idx = (s0 + np.arange(dn)) % L
        rn[k % 2, idx] += drop
    beds['eggy_amb_rain'] = rn
    # campfire: soft roar and random crackles
    fr = np.stack([fft_filter(noise(L, 121 + c), 80, 900, circular=True) * .25 for c in range(2)])
    for k in range(700):
        s0 = int(rng.uniform(0, L))
        cn = int(rng.uniform(.002, .012) * SR)
        cr = fft_filter(noise(cn, 4000 + k % 60), 1000, 7000) * np.exp(-np.arange(cn) / (cn / 3)) * rng.uniform(.3, 1.2)
        idx = (s0 + np.arange(cn)) % L
        fr[k % 2, idx] += cr
    beds['eggy_amb_fire'] = fr
    return beds


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)
    rep = {}
    effects = {}
    for part in (chirps(), steps(), bigger()):
        effects.update(part)
    effects['eggy_fanfare'] = (fanfare(), -6)
    effects['eggy_thunder_near'] = (thunder(True), -5)
    effects['eggy_thunder_far'] = (thunder(False), -9)
    effects['eggy_tree_fall'] = (tree_fall(), -10)
    effects['eggy_ribbit'] = (ribbit(), -20)
    fn = int(.3 * SR)
    effects['eggy_fish'] = (verb(mix((nzb(fn, 800, 6000, 340) * env(fn, .002, .03) * .6, 0, 1), (glide(900, 1700, .05, amp=.3, tau=.02), .02, 1)), .1, .2), -22)
    for name, (x, peak) in effects.items():
        x = finish(np.asarray(x, dtype=float), peak)
        wav = os.path.join(WORK, name + '.wav')
        write_wav(wav, x)
        m4a = os.path.join(OUT, name + '.m4a')
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', wav, '-c:a', 'aac_at', '-b:a', '96k', m4a], check=True)
        rep[name] = dict(file=os.path.basename(m4a), seconds=round(len(x) / SR, 3), peak_dbfs=peak)
    for name, x in ambience().items():
        x = x / np.abs(x).max() * 10 ** (-12 / 20)
        path = os.path.join(OUT, name + '.wav')
        write_wav(path, x, 16)
        rep[name] = dict(file=os.path.basename(path), seconds=20.0, loop='whole file, seamless')
    mpath = os.path.join(OUT, 'eggy_audio_manifest.json')
    manifest = json.load(open(mpath)) if os.path.exists(mpath) else {'music': []}
    manifest['effects'] = rep
    json.dump(manifest, open(mpath, 'w'), indent=1)
    print(len(rep), 'effects and beds')


if __name__ == '__main__':
    main()
