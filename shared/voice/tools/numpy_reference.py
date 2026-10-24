#!/usr/bin/env python3
"""NumPy reference renders for the live voice engine (shared/voice).

Two modes, both writing raw little-endian arrays and a `manifest.txt` into --out,
which the C++ side reads (voice_tests --reference DIR; a game's cue test likewise):

  engine  Every synth.py instrument and drum at several notes, velocities and
          lengths, the mixing helpers, fft_filter and NumPy's random streams.
          python3 numpy_reference.py engine --synth authoring/audio/work/src/synth.py --out DIR

  cues    Runs a game's cue script (a make_sfx.py whose cues go through
          write(name, x, peak_db)) with write() intercepted: each cue's float master
          before 16-bit quantisation is saved, the generator states at the start of
          every cue are printed as C++ (voice::GeneratorState) for the cue table,
          and, with --masters, the quantised result is compared with the WAV masters.
          python3 numpy_reference.py cues --script games/penthesheep/audio_src/make_sfx.py --out DIR \
              --masters assets/audio

Needs NumPy 1.26 (the Mini's /usr/bin/python3). Deterministic; writes only into --out.
"""
from __future__ import annotations

import argparse
import importlib.util
import os
import sys
import types
import wave
from pathlib import Path

import numpy as np

# ---------------------------------------------------------------------------- output

class Writer:
    def __init__(self, out: Path) -> None:
        self.out = out
        out.mkdir(parents=True, exist_ok=True)
        self.lines: list[str] = []
        self.count = 0

    def put(self, kind: str, args: list, data: np.ndarray, dtype: str = '<f8') -> None:
        name = f'r{self.count:04d}'
        self.count += 1
        np.asarray(data).astype(dtype).tofile(self.out / (name + '.bin'))
        self.lines.append(' '.join([name, kind] + [repr(a) if isinstance(a, float) else str(a) for a in args]))

    def close(self) -> None:
        (self.out / 'manifest.txt').write_text('\n'.join(self.lines) + '\n')


def load_synth(path: Path) -> types.ModuleType:
    spec = importlib.util.spec_from_file_location('synth', path)
    module = importlib.util.module_from_spec(spec)
    sys.modules['synth'] = module
    spec.loader.exec_module(module)
    return module


def fresh(synth: types.ModuleType) -> None:
    """The module as freshly imported: the shared generator and the caches reset."""
    synth._rng = np.random.default_rng(12345)
    synth._cache.clear()
    synth._dcache.clear()
    synth._tables.clear()


def state_words(g: np.random.Generator) -> list[int]:
    s = g.bit_generator.state
    st, inc = s['state']['state'], s['state']['inc']
    m = (1 << 64) - 1
    return [st >> 64, st & m, inc >> 64, inc & m, int(s['has_uint32']), int(s['uinteger'])]


def state_cpp(g: np.random.Generator) -> str:
    w = state_words(g)
    return ('voice::GeneratorState{{0x%016xull, 0x%016xull}, {0x%016xull, 0x%016xull}, %s, 0x%08xu}'
            % (w[0], w[1], w[2], w[3], 'true' if w[4] else 'false', w[5]))


# ---------------------------------------------------------------------------- engine cases

def mixed_sequence(seed: int) -> np.ndarray:
    """Interleaved draws of every kind; voice_tests replays the same script."""
    g = np.random.default_rng(seed)
    out = []
    for i in range(300):
        out.append(g.random())
        out.append(float(g.integers(0, 10)))
        out.append(float(g.integers(0, 7)))
        out.append(g.standard_normal())
        out.append(float(g.integers(0, 2 ** 40)))
        out.append(g.uniform(-1, 1))
        out.append(g.exponential(1.5))
        out.append(float(g.integers(3, 4)))
        out.append(float(g.choice([88, 91, 93, 95])))
        out.append(float(g.choice(5, p=[.25, .3, .25, .12, .08])))
        out.append(g.normal(.4, .2))
        out.append(float(g.poisson(2.5)))
    return np.array(out)


def sequence_case(synth: types.ModuleType) -> np.ndarray:
    """Shared-module order and the render caches; voice_tests replays the same script."""
    fresh(synth)
    parts = [synth.render_note('pluck', 60, .5, .7), synth.render_note('pad', 64, 1.0, .52),
             synth.render_note('pluck', 60, .5, .7), synth.render_note('flute', 72, .3, .62),
             synth.noise(1000), synth.render_drum('kick', .8, 5), synth.render_drum('tap', .44, -3),
             synth.render_note('slide', 67, .41, .9), synth.kick(.7), synth.render_drum('kick', .8, 1),
             synth.render_note('marimba', 55, .2, .33), synth.render_note('bass', 40, .6, .8),
             synth.render_note('clarinet', 62, .7, .5), synth.render_note('pluck', 60.0, .5, .7)]
    return np.concatenate(parts)


def mix_case(synth: types.ModuleType) -> np.ndarray:
    """LoopMix, make_ir, conv_reverb, pingpong and autopan; voice_tests replays it."""
    fresh(synth)
    L = 48000
    mix = synth.LoopMix(L)
    mix.add('keys', synth.render_note('ep', 60, .4, .7), 1000, -.4, .8)
    mix.add('keys', synth.render_note('ep', 67, .9, .5), 40000, .3, .7)  # wraps round the loop
    mix.add('drums', synth.render_drum('hat', .5, 2), -500, .5, 1.0)
    straight = synth.LoopMix(L, circular=False)
    straight.add('a', synth.render_note('celesta', 76, .3, .6), -2000, .2, .9)
    straight.add('a', synth.render_note('vibes', 64, .5, .6), 30000, -.7, .5)
    ir = synth.make_ir(length=0.7, seed=7)
    wet = synth.conv_reverb(mix.bus('keys'), ir)
    tail = synth.conv_reverb(straight.bus('a'), ir, circular=False)
    echo = synth.pingpong(mix.bus('keys') + mix.bus('drums'), 7000, .4, 4, 3000)
    moved = synth.autopan(mix.bus('drums'), 2, .3)
    return np.concatenate([ir.ravel(), mix.bus('keys').ravel(), mix.bus('drums').ravel(), straight.bus('a').ravel(),
                           wet.ravel(), tail.ravel(), echo.ravel(), moved.ravel()])


def engine(synth_path: Path, out: Path, quick: bool) -> None:
    synth = load_synth(synth_path)
    w = Writer(out)
    # NumPy's generators
    for seed in (0, 1, 7, 12345, 1977, 2 ** 40 + 7):
        g = np.random.default_rng(seed)
        w.put('raw', [seed, 64], g.bit_generator.random_raw(64), '<u8')
        w.put('normal', [seed, 200000], np.random.default_rng(seed).standard_normal(200000))
        w.put('random', [seed, 2000], np.random.default_rng(seed).random(2000))
        w.put('uniform', [seed, -0.2, 0.2, 2000], np.random.default_rng(seed).uniform(-0.2, 0.2, 2000))
        w.put('exponential', [seed, 0.16, 100000], np.random.default_rng(seed).exponential(0.16, 100000))
        for low, high in ((0, 6), (0, 1000000), (2, 7), (-5, 5), (0, 2 ** 33), (0, 2 ** 63 - 1), (0, 1)):
            w.put('integers', [seed, low, high, 3000], np.random.default_rng(seed).integers(low, high, 3000), '<i8')
        for lam in (0.0, 0.7, 4.0, 9.5, 12.0, 55.0):
            w.put('poisson', [seed, lam, 3000], np.random.default_rng(seed).poisson(lam, 3000), '<i8')
        w.put('mixed', [seed], mixed_sequence(seed))
        g = np.random.default_rng(seed)
        g.standard_normal(1001)
        g.integers(0, 10)
        w.put('state', [seed], np.array(state_words(g), dtype=object).astype(np.uint64), '<u8')
        x = np.random.default_rng(seed).standard_normal(100003)
        w.put('sum', [seed, 100003], np.array([np.sum(x), np.std(x), np.sum(x[:77]), np.std(x[:5000])]))
    # fft_filter, including prime and odd lengths (Bluestein) and circular mode
    for seed, n, lo, hi, order, pad, circular in ((3, 1000, 700, 4500, 2, 4096, 0), (4, 4093, 0, 3200, 1, 4096, 0),
                                                  (5, 9973, 250, 3500, 2, 4096, 1), (6, 12000, 60, 0, 2, 4096, 0),
                                                  (7, 48000, 90, 2200, 2, 4096, 0), (8, 777, 1000, 4000, 1, 0, 0),
                                                  (9, 101, 300, 3000, 2, 4096, 1)):
        x = np.random.default_rng(seed).standard_normal(n)
        y = synth.fft_filter(x, lo or None, hi or None, order=order, pad=pad, circular=bool(circular))
        w.put('fftfilter', [seed, n, float(lo), float(hi), order, pad, circular], y)
    # wavetables
    for kind, maxpart, phase0, f0, f1, n in (('saw', 40, 0.0, 110.0, 880.0, 24000), ('flute', 6, 0.25, 523.0, 530.0, 9000),
                                             ('clar', 12, 0.7, 196.0, 190.0, 9000), ('reed', 14, 0.3, 2000.0, 4000.0, 9000)):
        fresh(synth)
        w.put('wt', [kind, maxpart, phase0, f0, f1, n], synth.wt(kind, np.linspace(f0, f1, n), maxpart, phase0))
    # every instrument
    notes = (40, 64, 88) if not quick else (60,)
    vels = (0.4, 0.95) if not quick else (0.7,)
    durs = (0.15, 1.2) if not quick else (0.5,)
    for name in synth.INSTR:
        for m in notes:
            for vel in vels:
                for dur in durs:
                    fresh(synth)
                    w.put('instr', [name, float(m), dur, vel], synth.INSTR[name](m, dur, vel))
    fresh(synth)
    w.put('instr', ['pad', 57.5, 0.8, 0.6], synth.INSTR['pad'](57.5, 0.8, 0.6))  # a fractional note
    # every drum: seeded, from the shared generator, and through render_drum
    for name in synth.DRUMS:
        for vel in (0.3, 0.8):
            for seed in (11, -1):
                if seed == -1 and name in ('stone', 'rainstick'):
                    continue
                fresh(synth)
                w.put('drum', [name, vel, seed], synth.DRUMS[name](vel, seed=None if seed == -1 else seed))
        for rr in range(4):
            fresh(synth)
            w.put('renderdrum', [name, 0.62, rr], synth.render_drum(name, 0.62, rr))
    for length, vel, seed in ((0.5, 0.4, 3), (1.3, 0.7, -1)):
        fresh(synth)
        w.put('swish', [length, vel, seed], synth.brush_swish(length, vel, None if seed == -1 else seed))
    w.put('sequence', [], sequence_case(synth))
    w.put('mix', [], mix_case(synth))
    w.close()
    print(f'{w.count} reference renders in {out}')


# ---------------------------------------------------------------------------- cue scripts

def cues(script: Path, out: Path, fade: float, fade_in: int, masters: Path | None) -> None:
    script = script.resolve()
    sys.path.insert(0, str(script.parent / 'engine'))
    sys.path.insert(0, str(script.parent))
    spec = importlib.util.spec_from_file_location('cue_script', script)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    synth = sys.modules['synth']
    w = Writer(out)
    rendered = out / 'script_wavs'
    rendered.mkdir(parents=True, exist_ok=True)
    module.OUT = str(rendered)

    def generators() -> dict[str, np.random.Generator]:
        found = {'synth._rng': synth._rng}
        for key, value in vars(module).items():
            if isinstance(value, np.random.Generator):
                found[key] = value
        return found

    starts = {k: state_cpp(g) for k, g in generators().items()}
    misses: list[str] = []
    original_render_note = synth.render_note

    def render_note(inst, m, dur, vel):  # logs the cache misses a later cue would need primed
        vq = round(vel * 20) / 20
        dq = round(dur * 100) / 100
        if (inst, m, dq, vq) not in synth._cache:
            misses.append(f'prime_note("{inst}", {m}, {dur}, {vel}, {state_cpp(synth._rng)})')
        return original_render_note(inst, m, dur, vel)
    synth.render_note = render_note
    if getattr(module, 'render_note', None) is original_render_note:  # from synth import render_note
        module.render_note = render_note

    original_write = module.write
    report: list[str] = []
    failures = 0

    def write(name, x, peak_db):
        nonlocal starts, failures
        x = np.asarray(x, dtype=float)
        y = x / (np.max(np.abs(x)) + 1e-12) * 10 ** (peak_db / 20)
        k = min(len(y), int(fade * module.SR))
        y[-k:] *= np.linspace(1, 0, k)
        if fade_in:
            y[:fade_in] *= np.linspace(0, 1, fade_in)
        w.put('cue', [name], y)
        original_write(name, x.copy(), peak_db)
        ours = (y * 32767).astype('<i2').tobytes()
        with wave.open(str(rendered / (name + '.wav')), 'rb') as f:
            theirs = f.readframes(f.getnframes())
        same_script = ours == theirs
        line = f'// {name}: starts at ' + '; '.join(f'{k} = {v}' for k, v in starts.items())
        if misses:
            line += '\n//   notes cached before it: ' + '; '.join(misses)
        status = 'normalisation matches the script' if same_script else 'NORMALISATION DIFFERS from the script write()'
        if masters is not None:
            path = masters / (name + '.wav')
            if path.exists():
                with wave.open(str(path), 'rb') as f:
                    master = f.readframes(f.getnframes())
                diff = np.abs(np.frombuffer(ours, '<i2').astype(int) - np.frombuffer(master, '<i2').astype(int)) if len(master) == len(ours) else None
                status += ', master ' + ('identical' if master == ours else ('max %d LSB' % diff.max() if diff is not None else 'LENGTH DIFFERS'))
                failures += master != ours
            else:
                status += ', no master'
        failures += not same_script
        report.append(line + '\n//   ' + status)
        starts = {k: state_cpp(g) for k, g in generators().items()}

    module.write = write
    module.main()
    w.close()
    (out / 'states.txt').write_text('\n'.join(report) + '\n')
    print('\n'.join(report))
    print(f'{w.count} cues in {out}; {failures} problems')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='mode', required=True)
    e = sub.add_parser('engine')
    e.add_argument('--synth', type=Path, required=True)
    e.add_argument('--out', type=Path, required=True)
    e.add_argument('--quick', action='store_true')
    c = sub.add_parser('cues')
    c.add_argument('--script', type=Path, required=True)
    c.add_argument('--out', type=Path, required=True)
    c.add_argument('--fade', type=float, default=.004)
    c.add_argument('--fade-in', type=int, default=8)
    c.add_argument('--masters', type=Path)
    args = parser.parse_args()
    if args.mode == 'engine':
        engine(args.synth, args.out, args.quick)
    else:
        cues(args.script, args.out, args.fade, args.fade_in, args.masters)


if __name__ == '__main__':
    main()
