"""Original numpy synthesis engine for the card-game audio family.

Everything is generated from oscillators, modal resonators and filtered noise.
No samples or third-party audio assets are used.
"""
import numpy as np
import wave, struct

SR = 48000
_rng = np.random.default_rng(12345)


def mtof(m):
    return 440.0 * 2 ** ((m - 69) / 12.0)


def tarr(n):
    return np.arange(n) / SR


def gate(n, dur, att=0.003, rel=0.1):
    """Attack ramp + raised-cosine release starting at `dur` seconds."""
    e = np.ones(n)
    a = max(int(att * SR), 1)
    a = min(a, n)
    e[:a] = np.sin(0.5 * np.pi * np.arange(a) / a) ** 2
    g = int(dur * SR)
    if g < n:
        r = max(int(rel * SR), 1)
        seg = min(r, n - g)
        e[g:g + seg] *= 0.5 * (1 + np.cos(np.pi * np.arange(seg) / r))
        e[g + seg:] = 0
    return e


def fft_filter(x, lo=None, hi=None, order=2, pad=4096, circular=False):
    """Zero-phase Butterworth-magnitude band filter applied in the frequency domain."""
    n = len(x)
    N = n if circular else n + pad
    X = np.fft.rfft(x, N)
    f = np.fft.rfftfreq(N, 1 / SR)
    H = np.ones_like(f)
    if hi:
        H /= np.sqrt(1 + (f / hi) ** (2 * order))
    if lo:
        H /= np.sqrt(1 + (lo / np.maximum(f, 1e-3)) ** (2 * order))
    return np.fft.irfft(X * H, N)[:n]


def noise(n, seed=None):
    r = np.random.default_rng(seed) if seed is not None else _rng
    return r.standard_normal(n)


# --------------------------------------------------------------------- tonal instruments
def _partials(f, n, ratios, amps, taus, phases=None):
    t = tarr(n)
    y = np.zeros(n)
    for i, (r, a, tau) in enumerate(zip(ratios, amps, taus)):
        fr = f * r
        if fr > 18000 or a == 0:
            continue
        ph = 0 if phases is None else phases[i]
        y += a * np.exp(-t / tau) * np.sin(2 * np.pi * fr * t + ph)
    return y


def ep(m, dur, vel=0.7, bright=1.0):
    """FM electric piano (tine-ish): modulator ratio 1, decaying index, bell partial."""
    f = mtof(m)
    rel = 0.18
    n = int((dur + rel) * SR)
    t = tarr(n)
    dec = np.exp(-t / (1.6 * (261.6 / f) ** 0.35))
    idx = bright * (0.9 + 1.8 * vel) * np.exp(-t / 0.3) + 0.3 * bright
    y = np.sin(2 * np.pi * f * t + idx * np.sin(2 * np.pi * f * t)) * dec
    if f * 7 < 16000:
        y += 0.07 * vel * bright * np.sin(2 * np.pi * f * 7.0 * t) * np.exp(-t / 0.05)
    return y * gate(n, dur, 0.002, rel) * (0.35 + 0.65 * vel)


def pluck(m, dur, vel=0.7, pos=0.18, decay=1.0, bright=1.0, body=True):
    """Nylon-string style additive pluck with per-partial damping and pick noise."""
    f = mtof(m)
    rel = 0.1
    n = int((dur + rel) * SR)
    t60 = decay * 2.6 * (196 / f) ** 0.45
    K = int(min(40, 12000 / f))
    ks = np.arange(1, K + 1)
    amps = np.abs(np.sin(np.pi * ks * pos)) / ks ** 1.05
    amps *= np.exp(-(ks * f) / (3600 * bright * (0.55 + vel)))
    taus = (t60 / 6.9) / (1 + 0.00055 * ks * f)
    ratios = ks * np.sqrt(1 + 8e-6 * ks ** 2)
    ph = _rng.uniform(-0.2, 0.2, K)
    y = _partials(f, n, ratios, amps, taus, ph)
    if body:
        k = min(n, int(0.03 * SR))
        nz = fft_filter(noise(k), 700, 4500) * np.exp(-tarr(k) / 0.004)
        y[:k] += 0.25 * vel * nz / (np.abs(nz).max() + 1e-9) * amps.max()
    y *= gate(n, dur, 0.0008, rel)
    return y * (0.3 + 0.7 * vel)


def marimba(m, dur, vel=0.7):
    f = mtof(m)
    n = int(min(2.2, 0.4 + 1.2 * (300 / f) ** 0.5) * SR)
    t1 = 0.55 * (300 / f) ** 0.55
    y = _partials(f, n, [1, 3.93, 9.24], [1, 0.3 * (0.5 + vel), 0.08 * vel], [t1, 0.11, 0.03])
    k = int(0.006 * SR)
    y[:k] += 0.06 * vel * fft_filter(noise(k), 300, 3000)[:k] * np.linspace(1, 0, k)
    return y * gate(n, 10, 0.0007) * (0.3 + 0.7 * vel)


def vibes(m, dur, vel=0.7, trem=5.2):
    f = mtof(m)
    rel = 0.35
    n = int((min(dur, 3.0) + rel) * SR)
    t = tarr(n)
    fund = np.exp(-t / 1.4) * np.sin(2 * np.pi * f * t) * (1 + 0.22 * np.sin(2 * np.pi * trem * t))
    y = fund + _partials(f, n, [4.0, 10.0], [0.10 * (0.5 + vel), 0.025 * vel], [0.25, 0.06])
    return y * gate(n, min(dur, 3.0), 0.001, rel) * (0.3 + 0.7 * vel)


def celesta(m, dur, vel=0.7):
    f = mtof(m)
    n = int(1.8 * SR)
    y = _partials(f, n, [1, 2, 3, 4.02, 6.9], [1, 0.12, 0.22 * vel, 0.06, 0.03 * vel], [0.75, 0.4, 0.12, 0.1, 0.03])
    return y * gate(n, 10, 0.0006) * (0.3 + 0.7 * vel)


def harp(m, dur, vel=0.7):
    return pluck(m, max(dur, 1.4), vel, pos=0.32, decay=1.25, bright=0.8, body=False)


def pizz(m, dur, vel=0.7):
    return pluck(m, min(dur, 0.35), vel, pos=0.22, decay=0.35, bright=0.7)


def bass(m, dur, vel=0.7):
    """Fingered upright-ish bass."""
    f = mtof(m)
    rel = 0.07
    n = int((dur + rel) * SR)
    t = tarr(n)
    fr = f * (1 + 0.012 * np.exp(-t / 0.015))
    ph = 2 * np.pi * np.cumsum(fr) / SR
    y = np.zeros(n)
    for k in range(1, 9):
        if k * f > 4000:
            break
        a = 1 / k ** 1.15 * (1 if k == 1 else 0.6 + 0.6 * vel)
        tau = 1.3 / k ** 0.9
        y += a * np.exp(-t / tau) * np.sin(k * ph)
    kk = min(n, int(0.02 * SR))
    y[:kk] += 0.12 * vel * fft_filter(noise(kk), 80, 900)[:kk] * np.exp(-tarr(kk) / 0.006)
    return y * gate(n, dur, 0.004, rel) * (0.4 + 0.6 * vel)


_tables = {}


def _table(kind, K):
    key = (kind, K)
    if key not in _tables:
        N = 4096
        x = np.arange(N) / N
        tab = np.zeros(N)
        for k in range(1, K + 1):
            if kind == "saw":
                a = 1 / k ** 1.25
            elif kind == "flute":
                a = [1, 0.32, 0.11, 0.05, 0.025, 0.012][k - 1] if k <= 6 else 0
            elif kind == "clar":
                a = (1 / k ** 0.9 if k % 2 else 0.04 / k) * (1 if k < 9 else 0.4)
            elif kind == "reed":  # accordion-ish
                a = 1 / k ** 0.8 * (0.6 if k % 2 == 0 else 1)
            tab += a * np.sin(2 * np.pi * k * x)
        _tables[key] = np.append(tab / np.abs(tab).max(), tab[0] / np.abs(tab).max())
    return _tables[key]


def wt(kind, freq, maxpart=40, phase0=0.0):
    fmax = float(np.max(freq))
    K = int(max(1, min(maxpart, 15000 / fmax)))
    tab = _table(kind, K)
    ph = (phase0 + np.cumsum(freq) / SR) % 1.0
    return np.interp(ph * 4096, np.arange(4097), tab)


def pad(m, dur, vel=0.6, att=0.5, rel=1.0, cutoff=2200):
    f = mtof(m)
    n = int((dur + rel) * SR)
    t = tarr(n)
    y = np.zeros(n)
    for c in (-7, 0, 6):
        fr = f * 2 ** (c / 1200) * (1 + 0.0015 * np.sin(2 * np.pi * 0.3 * t + c))
        y += wt("saw", fr, phase0=_rng.uniform())
    y = fft_filter(y, 90, cutoff, order=2)
    return y * gate(n, dur, att, rel) * vel / 3


def _breathy(kind, m, dur, vel, att, rel, vib_rate, vib_depth, breath, maxpart):
    f = mtof(m)
    n = int((dur + rel) * SR)
    t = tarr(n)
    vib = vib_depth * np.clip((t - 0.18) / 0.35, 0, 1) * np.sin(2 * np.pi * vib_rate * t + _rng.uniform(0, 6))
    scoop = -0.006 * np.exp(-t / 0.04)
    fr = f * (1 + vib + scoop)
    y = wt(kind, fr, maxpart)
    amp_env = gate(n, dur, att, rel) * (1 + 0.5 * vib / max(vib_depth, 1e-9) * 0.04)
    nz = fft_filter(noise(n), f * 0.9, f * 3.5, order=1) * breath
    y = y + nz / (np.std(nz) + 1e-9) * breath
    return y * amp_env * (0.35 + 0.65 * vel)


def flute(m, dur, vel=0.7):
    return _breathy("flute", m, dur, vel, 0.045, 0.12, 5.0, 0.0045, 0.035, 6)


def clarinet(m, dur, vel=0.7):
    y = _breathy("clar", m, dur, vel, 0.03, 0.09, 4.6, 0.003, 0.015, 12)
    return fft_filter(y, None, 3200, order=1)


def reed(m, dur, vel=0.7):
    f = mtof(m)
    n = int((dur + 0.08) * SR)
    t = tarr(n)
    y = wt("reed", f * np.ones(n), 14) + wt("reed", f * 2 ** (9 / 1200) * np.ones(n), 14, 0.3)
    y = fft_filter(y, 120, 2600, order=1) * 0.5
    return y * gate(n, dur, 0.025, 0.08) * (0.35 + 0.65 * vel)


# --------------------------------------------------------------------- percussion
def kick(vel=0.7, seed=None):
    n = int(0.35 * SR)
    t = tarr(n)
    fr = 48 + 62 * np.exp(-t / 0.028)
    y = np.sin(2 * np.pi * np.cumsum(fr) / SR) * np.exp(-t / 0.16)
    y[:int(0.004 * SR)] += 0.2 * fft_filter(noise(int(0.004 * SR), seed), 500, 3000)
    return y * gate(n, 10, 0.0015) * vel


def brush_tap(vel=0.6, seed=None):
    n = int(0.18 * SR)
    t = tarr(n)
    nz = fft_filter(noise(n, seed), 900, 6500) * np.exp(-t / 0.045)
    nz /= np.abs(nz).max()
    body = 0.35 * np.sin(2 * np.pi * 185 * t) * np.exp(-t / 0.035)
    return (nz * 0.8 + body) * gate(n, 10, 0.0015) * vel


def brush_swish(length=0.5, vel=0.4, seed=None):
    n = int(length * SR)
    x = np.linspace(0, 1, n)
    env = np.sin(np.pi * x ** 0.6) ** 2
    nz = fft_filter(noise(n, seed), 2500, 9000)
    return nz / np.abs(nz).max() * env * vel * 0.5


def shaker(vel=0.4, seed=None):
    n = int(0.09 * SR)
    t = tarr(n)
    env = (1 - np.exp(-t / 0.008)) * np.exp(-t / 0.022)
    nz = fft_filter(noise(n, seed), 5000, 13000)
    return nz / np.abs(nz).max() * env * vel


def hat(vel=0.4, seed=None):
    n = int(0.08 * SR)
    t = tarr(n)
    nz = fft_filter(noise(n, seed), 7000, 15000) * np.exp(-t / 0.018)
    return nz / np.abs(nz).max() * vel * gate(n, 10, 0.0005)


def rim(vel=0.5, seed=None):
    n = int(0.08 * SR)
    y = _partials(1, n, [1720, 2480], [1, 0.5], [0.012, 0.008])
    k = int(0.002 * SR)
    y[:k] += 0.6 * fft_filter(noise(k, seed), 2000, 9000)
    return y * gate(n, 10, 0.0003) * vel


def clave(vel=0.5, seed=None):
    n = int(0.12 * SR)
    return _partials(1, n, [2450, 6100], [1, 0.08], [0.03, 0.01]) * gate(n, 10, 0.0003) * vel


def woodblock(vel=0.5, high=True, seed=None):
    n = int(0.12 * SR)
    b = 1180 if high else 820
    return _partials(1, n, [b, b * 2.6], [1, 0.25], [0.025, 0.012]) * gate(n, 10, 0.0004) * vel


def triangle(vel=0.4, seed=None):
    n = int(1.5 * SR)
    return _partials(1, n, [3950, 4720, 6410, 9100], [1, 0.5, 0.35, 0.2], [0.6, 0.5, 0.35, 0.25]) * gate(n, 10, 0.0004) * vel


def slide(m, dur, vel=0.7):
    """Slide-whistle-ish lead: near-sine with a comic upward glide into each note."""
    f = mtof(m)
    rel = 0.08
    n = int((dur + rel) * SR)
    t = tarr(n)
    glide = 2 ** (-3 / 12 * np.exp(-t / 0.035))
    vib = 1 + 0.008 * np.clip((t - 0.15) / 0.2, 0, 1) * np.sin(2 * np.pi * 6.2 * t)
    y = wt('flute', f * glide * vib, 3)
    br = fft_filter(noise(n), f, f * 3, 1)
    y += 0.04 * br / (np.std(br) + 1e-9)
    return y * gate(n, dur, 0.02, rel) * (0.35 + 0.65 * vel)


def tuba(m, dur, vel=0.7):
    """Round brass-ish bass for oom-pah (soft attack, odd-leaning harmonics)."""
    f = mtof(m)
    n = int((dur + 0.06) * SR)
    t = tarr(n)
    y = wt('reed', f * (1 + 0.01 * np.exp(-t / 0.03)) * np.ones(n), 8)
    y = fft_filter(y, 30, 600 + 500 * vel, 1)
    return y * gate(n, dur, 0.025, 0.06) * (0.4 + 0.6 * vel)


INSTR = dict(slide=slide, tuba=tuba, ep=ep, pluck=pluck, marimba=marimba, vibes=vibes, celesta=celesta, harp=harp,
             pizz=pizz, bass=bass, pad=pad, flute=flute, clarinet=clarinet, reed=reed)
DRUMS = dict(kick=kick, tap=brush_tap, shaker=shaker, hat=hat, rim=rim, clave=clave,
             wbhi=lambda vel=0.5, seed=None: woodblock(vel, True, seed),
             wblo=lambda vel=0.5, seed=None: woodblock(vel, False, seed), triangle=triangle)

_cache = {}


def render_note(inst, m, dur, vel):
    """Cached note render; velocity quantised to keep the cache useful."""
    vq = round(vel * 20) / 20
    dq = round(dur * 100) / 100
    key = (inst, m, dq, vq)
    if key not in _cache:
        _cache[key] = INSTR[inst](m, dq, vq)
    return _cache[key]


_dcache = {}


def render_drum(name, vel, rr):
    vq = round(vel * 20) / 20
    key = (name, vq, rr % 4)
    if key not in _dcache:
        _dcache[key] = DRUMS[name](vq, seed=1000 + rr % 4 + sum(map(ord, name)) % 97)
    return _dcache[key]


# --------------------------------------------------------------------- mixing
def pan_gains(p):
    th = (p + 1) * np.pi / 4
    return np.cos(th), np.sin(th)


class LoopMix:
    """Stereo buses of exactly L samples; everything wraps so the loop is seamless."""

    def __init__(self, L, circular=True):
        self.L = L
        self.circular = circular
        self.buses = {}

    def bus(self, name):
        if name not in self.buses:
            self.buses[name] = np.zeros((2, self.L))
        return self.buses[name]

    def add(self, name, y, start, pan=0.0, gain=1.0):
        b = self.bus(name)
        gl, gr = pan_gains(pan)
        n = len(y)
        if not self.circular:
            s = max(start, 0)
            y = y[s - start:]
            e = min(self.L, s + len(y))
            b[0, s:e] += gl * gain * y[:e - s]
            b[1, s:e] += gr * gain * y[:e - s]
            return
        s = start % self.L
        pos = 0
        while pos < n:
            k = min(n - pos, self.L - s)
            b[0, s:s + k] += gl * gain * y[pos:pos + k]
            b[1, s:s + k] += gr * gain * y[pos:pos + k]
            pos += k
            s = 0


def make_ir(length=2.6, rt_low=2.0, rt_mid=1.6, rt_high=0.8, predelay=0.018, seed=7):
    n = int(length * SR)
    t = tarr(n)
    ir = np.zeros((2, n))
    r = np.random.default_rng(seed)
    for ch in range(2):
        base = r.standard_normal(n)
        lo = fft_filter(base, None, 500)
        hi = fft_filter(base, 4000, None)
        mid = base - lo - hi
        y = lo * np.exp(-6.9 * t / rt_low) + mid * np.exp(-6.9 * t / rt_mid) + 0.6 * hi * np.exp(-6.9 * t / rt_high)
        y *= 1 - np.exp(-t / 0.012)  # soften onset
        d = int(predelay * SR)
        y = np.concatenate([np.zeros(d), y])[:n]
        # a few early reflections
        for dt, g in ((0.011, 0.35), (0.019, 0.28), (0.027, 0.22), (0.041, 0.16)):
            k = int((dt + (0.002 if ch else 0)) * SR)
            y[k] += g * np.sqrt(np.sum(y ** 2)) / 30
        ir[ch] = y / np.sqrt(np.sum(y ** 2))
    return ir


def conv_reverb(x, ir, circular=True):
    L = x.shape[1]
    N = L if circular else L + ir.shape[1]
    out = np.zeros((2, L if circular else N))
    for ch in range(2):
        h = np.zeros(N)
        h[:ir.shape[1]] = ir[ch]
        out[ch] = np.fft.irfft(np.fft.rfft(x[ch], N) * np.fft.rfft(h), N)
    return out


def pingpong(x, delay_samples, fb=0.35, n=5, lp=3500):
    """Circular ping-pong echo (np.roll keeps loop seamless)."""
    out = np.zeros_like(x)
    mono = x.mean(0)
    g = 1.0
    for k in range(1, n + 1):
        g *= fb
        ch = (k + 1) % 2
        out[ch] += g * np.roll(mono, k * delay_samples)
    out[0] = fft_filter(out[0], 250, lp, circular=True)
    out[1] = fft_filter(out[1], 250, lp, circular=True)
    return out


def autopan(x, cycles, depth=0.35):
    L = x.shape[1]
    lfo = np.sin(2 * np.pi * cycles * np.arange(L) / L)
    out = x.copy()
    out[0] *= 1 + depth * lfo
    out[1] *= 1 - depth * lfo
    return out


# --------------------------------------------------------------------- file io
def write_wav(path, x, bits=24, loop=None):
    """Write stereo/mono PCM WAV. `loop=(start,end)` adds a smpl chunk (end inclusive)."""
    x = np.atleast_2d(x)
    nch = x.shape[0]
    data = np.clip(x.T, -1, 1)
    if bits == 24:
        q = np.round(data * 8388607).astype(np.int32)
        b = q.astype("<i4").tobytes()
        raw = np.frombuffer(b, np.uint8).reshape(-1, 4)[:, :3].tobytes()
    else:
        tp = (np.random.default_rng(1).random(data.shape) - np.random.default_rng(2).random(data.shape)) / 32767
        q = np.clip(np.round((data + tp) * 32767), -32768, 32767).astype("<i2")
        raw = q.tobytes()
    bps = bits // 8
    fmt = struct.pack("<HHIIHH", 1, nch, SR, SR * nch * bps, nch * bps, bits)
    chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    if loop is not None:
        smpl = struct.pack("<IIIIIIIII", 0, 0, int(1e9 / SR), 60, 0, 0, 0, 1, 0)
        smpl += struct.pack("<IIIIII", 0, 0, loop[0], loop[1], 0, 0)
        chunks += b"smpl" + struct.pack("<I", len(smpl)) + smpl
    chunks += b"data" + struct.pack("<I", len(raw)) + raw
    if len(raw) % 2:
        chunks += b"\0"
    with open(path, "wb") as fh:
        fh.write(b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks)


def read_wav(path):
    with open(path, "rb") as fh:
        d = fh.read()
    pos = 12
    nch = bits = None
    while pos < len(d):
        cid = d[pos:pos + 4]
        sz = struct.unpack("<I", d[pos + 4:pos + 8])[0]
        body = d[pos + 8:pos + 8 + sz]
        if cid == b"fmt ":
            _, nch, sr, _, _, bits = struct.unpack("<HHIIHH", body[:16])
        elif cid == b"data":
            if bits == 24:
                b = np.frombuffer(body, np.uint8).reshape(-1, 3)
                v = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
                v = np.where(v >= 1 << 23, v - (1 << 24), v) / 8388607.0
            else:
                v = np.frombuffer(body, "<i2") / 32767.0
            return v.reshape(-1, nch).T
        pos += 8 + sz + (sz % 2)


# ===================================================================== batch 2 instruments
def felt_piano(m, dur, vel=0.6):
    """Soft felt piano: two detuned inharmonic strings, felt-damped highs, hammer thump."""
    f = mtof(m)
    rel = 0.25
    n = int((min(dur, 4.0) + rel) * SR)
    t = tarr(n)
    B = 0.00015 * (f / 261.6) ** 0.5
    y = np.zeros(n)
    for det in (-0.6, 0.6):
        for k in range(1, 16):
            fk = k * f * np.sqrt(1 + B * k * k) * 2 ** (det / 1200)
            if fk > 9000:
                break
            a = np.exp(-(k - 1) * (0.55 - 0.25 * vel)) / k ** 0.3
            tau = 2.2 * (261.6 / f) ** 0.5 / (1 + 0.35 * k)
            y += a * np.exp(-t / tau) * np.sin(2 * np.pi * fk * t + k)
    k = min(n, int(0.03 * SR))
    y[:k] += 0.5 * fft_filter(noise(k, 3), 60, 600)[:k] * np.exp(-tarr(k) / 0.006)
    y = fft_filter(y, 40, 1800 + 2500 * vel, 1)
    return y * gate(n, min(dur, 4.0), 0.004, rel) * (0.3 + 0.7 * vel) * 0.25


def glass(m, dur, vel=0.6):
    """Glass-harmonica / water-glass tone: soft swell, near-pure partials, slow beating."""
    f = mtof(m)
    rel = 0.6
    n = int((dur + rel) * SR)
    t = tarr(n)
    y = (np.sin(2 * np.pi * f * t) * (1 + 0.15 * np.sin(2 * np.pi * 1.3 * t))
         + 0.18 * np.sin(2 * np.pi * f * 2.0 * t + 1) + 0.06 * np.sin(2 * np.pi * f * 3.01 * t + 2))
    return y * gate(n, dur, 0.09, rel) * (0.3 + 0.7 * vel) * 0.8


def musicbox(m, dur, vel=0.6):
    f = mtof(m)
    n = int(1.6 * SR)
    y = _partials(f, n, [1, 2.0, 5.4, 9.9], [1, 0.2, 0.18 * vel, 0.05], [0.9, 0.35, 0.08, 0.03])
    return y * gate(n, 10, 0.0005) * (0.3 + 0.7 * vel)


def logdrum(m, dur, vel=0.7):
    """Hollow slit/log drum, pitched."""
    f = mtof(m)
    n = int(0.9 * SR)
    t = tarr(n)
    fr = f * (1 + 0.04 * np.exp(-t / 0.01))
    ph = 2 * np.pi * np.cumsum(fr) / SR
    y = np.exp(-t / 0.22) * np.sin(ph) + 0.35 * np.exp(-t / 0.07) * np.sin(2.03 * ph) + 0.12 * np.exp(-t / 0.03) * np.sin(3.9 * ph)
    k = int(0.008 * SR)
    y[:k] += 0.4 * fft_filter(noise(k, 5), 200, 2500)[:k] * np.linspace(1, 0, k)
    return y * gate(n, 10, 0.001) * (0.3 + 0.7 * vel)


def litho(m, dur, vel=0.7):
    """Lithophone / tuned stone: hard click, short inharmonic ring."""
    f = mtof(m)
    n = int(0.7 * SR)
    y = _partials(f, n, [1, 2.76, 5.40, 8.93], [1, 0.45, 0.25 * vel, 0.1 * vel], [0.18, 0.07, 0.03, 0.015])
    k = int(0.003 * SR)
    y[:k] += 0.8 * fft_filter(noise(k, 6), 1500, 9000)[:k]
    return y * gate(n, 10, 0.0003) * (0.3 + 0.7 * vel)


def analog(m, dur, vel=0.6, bright=0.6):
    """Rounded analog-style synth: saw through a decaying low-pass (blend of dark/bright copies)."""
    f = mtof(m)
    rel = 0.12
    n = int((dur + rel) * SR)
    t = tarr(n)
    fr = f * np.ones(n)
    y = wt('saw', fr, 40) + wt('saw', fr * 1.004, 40, 0.37)
    dark = fft_filter(y, 40, f * 2.0, 2)
    br = fft_filter(y, 40, f * 6.0, 2)
    y = dark + bright * vel * (br - dark) * np.exp(-t / 0.12)
    return y * gate(n, dur, 0.006, rel) * (0.35 + 0.65 * vel) * 0.4


def synthbass(m, dur, vel=0.7):
    f = mtof(m)
    rel = 0.06
    n = int((dur + rel) * SR)
    t = tarr(n)
    ph = 2 * np.pi * f * t
    y = np.sin(ph) + 0.35 * np.sin(2 * ph) * np.exp(-t / 0.15) + 0.12 * np.sin(3 * ph) * np.exp(-t / 0.06)
    return y * gate(n, dur, 0.004, rel) * (0.4 + 0.6 * vel)


def ping(m, dur, vel=0.6):
    """Delicate electronic ping (sine blip with tiny downward chirp)."""
    f = mtof(m)
    n = int(0.8 * SR)
    t = tarr(n)
    fr = f * (1 + 0.02 * np.exp(-t / 0.008))
    y = np.sin(2 * np.pi * np.cumsum(fr) / SR) * np.exp(-t / 0.18) + 0.1 * np.sin(2 * np.pi * 3 * f * t) * np.exp(-t / 0.04)
    return y * gate(n, 10, 0.0008) * (0.3 + 0.7 * vel)


def kalimba(m, dur, vel=0.6):
    f = mtof(m)
    n = int(1.3 * SR)
    y = _partials(f, n, [1, 5.9, 13.2], [1, 0.12 * vel, 0.04], [0.5 * (440 / f) ** 0.3, 0.05, 0.02])
    return y * gate(n, 10, 0.0008) * (0.3 + 0.7 * vel)


INSTR.update(felt=felt_piano, glass=glass, musicbox=musicbox, logdrum=logdrum, litho=litho,
             analog=analog, synthbass=synthbass, ping=ping, kalimba=kalimba)


def clap(vel=0.5, seed=None):
    n = int(0.2 * SR)
    t = tarr(n)
    e = np.zeros(n)
    for d in (0, 0.009, 0.017):
        e += np.where(t >= d, np.exp(-(t - d) / (0.006 if d < 0.017 else 0.06)), 0)
    nz = fft_filter(noise(n, seed), 900, 5000)
    return nz / np.abs(nz).max() * e * vel * 0.6


def stone_click(vel=0.5, seed=None):
    n = int(0.08 * SR)
    r = np.random.default_rng(seed)
    y = _partials(1, n, [2100 * r.uniform(.9, 1.1), 3700, 5200], [1, .6, .3], [0.012, 0.007, 0.004])
    return y * gate(n, 10, 0.0002) * vel


def rainstick(vel=0.3, seed=None):
    n = int(1.2 * SR)
    r = np.random.default_rng(seed)
    y = np.zeros(n)
    for _ in range(140):
        s = int(r.uniform(0, n - 400))
        g = r.uniform(.2, 1) * np.exp(-s / n * 2)
        y[s:s + 300] += g * _partials(1, 300, [r.uniform(3000, 8000)], [1], [0.002])
    return y * vel * 0.5


def tick(vel=0.4, seed=None):
    n = int(0.04 * SR)
    return _partials(1, n, [3200, 5100], [1, .4], [0.006, 0.003]) * gate(n, 10, 0.0002) * vel


DRUMS.update(clap=clap, stone=stone_click, rainstick=rainstick, tick=tick,
             logkick=lambda vel=0.5, seed=None: logdrum(38, 0.5, vel) * 0.9,
             loghi=lambda vel=0.5, seed=None: logdrum(50, 0.3, vel) * 0.7)
