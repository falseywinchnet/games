#!/usr/bin/env python3
"""NumPy reference renders of synth.py music and bed scripts, for the live voice engine.

shared/voice/tools/numpy_reference.py `cues` handles the effect scripts whose cues go
through write(name, x, peak_db). This handles the other kind: scripts that end in
write(path, x) (a music master, clipped and written as 16-bit) or synth.write_wav(path,
x, bits=16) (clipped, TPDF-dithered and rounded). It runs the script's own main() in a
scratch copy with every output intercepted and, for each file the script writes:

  - saves the master as the script computed it, np.clip(x, -1, 1), float64 channel
    after channel (left then right), as `rNNNN.bin`, listed in manifest.txt as
    `rNNNN music <name> <channels> <frames> <quantiser>`, quantiser `trunc` for
    (x * 32767).astype('<i2') or `dither` for write_wav's 16-bit path;
  - checks that this quantisation reproduces the bytes the script wrote;
  - prints, as C++, the state of synth._rng and of every np.random.Generator in the
    script's globals when the script began that output, and every render_note()
    cache entry made by an earlier output that this one reuses (with the _rng state
    it was rendered from), for voice::Synth::prime_note.

A start state is recorded when the previous output was written, so it is the state at
which the script began an output only if the script computes each output just before
writing it. Rock Stack's make_sfx.py builds zc_rush and zc_reeds before writing either:
the printed start of zc_reeds is the state after both, and its true start is zc_rush's.

    /usr/bin/python3 music_reference.py --script <copy of audio_src>/meadow_music.py --out DIR
        [--masters DIR]   also compare with the shipped 16-bit WAV masters there

Needs NumPy 1.26 (the Mini's /usr/bin/python3). Writes only into --out and the scratch
copy's own folders.
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


def state_cpp(g: np.random.Generator) -> str:
    s = g.bit_generator.state
    st, inc = s['state']['state'], s['state']['inc']
    m = (1 << 64) - 1
    return ('voice::GeneratorState{{0x%016xull, 0x%016xull}, {0x%016xull, 0x%016xull}, %s, 0x%08xu}'
            % (st >> 64, st & m, inc >> 64, inc & m, 'true' if s['has_uint32'] else 'false', s['uinteger']))


def dither16(data: np.ndarray) -> np.ndarray:
    """synth.write_wav's 16-bit path on frames x channels data already clipped."""
    tp = (np.random.default_rng(1).random(data.shape) - np.random.default_rng(2).random(data.shape)) / 32767
    return np.clip(np.round((data + tp) * 32767), -32768, 32767).astype('<i2')


class StubJson:
    """json for the script: manifests read as empty and are not written."""
    @staticmethod
    def load(_f):
        return {'music': [], 'stingers': []}

    @staticmethod
    def dump(*_a, **_k):
        return None


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--script', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--masters', type=Path)
    args = parser.parse_args()
    script = args.script.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    scratch = out / 'script_wavs'
    scratch.mkdir(exist_ok=True)
    sys.path.insert(0, str(script.parent / 'engine'))
    sys.path.insert(0, str(script.parent))
    spec = importlib.util.spec_from_file_location('music_script', script)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    synth = sys.modules['synth']
    for attr in ('OUT', 'MASTERS', 'ASSETS'):
        if hasattr(module, attr):
            setattr(module, attr, str(scratch))
    module.json = StubJson
    if hasattr(module, 'encode'):
        module.encode = lambda *a, **k: None
    real_open = open

    def guarded_open(path, mode='r', *a, **k):  # manifest reads see an empty file
        if 'r' in mode and str(path).endswith('.json') and not os.path.exists(path):
            return real_open(os.devnull, mode, *a, **k)
        return real_open(path, mode, *a, **k)
    module.open = guarded_open

    def generators() -> dict[str, np.random.Generator]:
        found = {'synth._rng': synth._rng}
        for key, value in vars(module).items():
            if isinstance(value, np.random.Generator):
                found[key] = value
        return found

    # render_note: which output made each cache entry, from which _rng state
    made: dict[tuple, tuple[int, str, tuple]] = {}
    primes: list[str] = []
    index = [0]
    original_render_note = synth.render_note

    def render_note(inst, m, dur, vel):
        vq = round(vel * 20) / 20
        dq = round(dur * 100) / 100
        key = (inst, m, dq, vq)
        if key not in synth._cache:
            made[key] = (index[0], state_cpp(synth._rng), (inst, m, dq, vq))
        else:
            first, state, a = made[key]
            line = f'synth.prime_note("{a[0]}", {a[1]}, {a[2]!r}, {a[3]!r}, {state});'
            if first < index[0] and line not in primes:
                primes.append(line)
        return original_render_note(inst, m, dur, vel)
    synth.render_note = render_note
    for key, value in list(vars(module).items()):
        if value is original_render_note:
            setattr(module, key, render_note)

    lines: list[str] = []
    report: list[str] = []
    problems = 0
    starts = {k: state_cpp(g) for k, g in generators().items()}

    def capture(path, x, quantiser, written):
        nonlocal starts, problems
        name = Path(path).stem
        x = np.atleast_2d(np.asarray(x, dtype=float))
        y = np.clip(x, -1, 1)
        file = f'r{len(lines):04d}'
        y.astype('<f8').tofile(out / (file + '.bin'))
        lines.append(f'{file} music {name} {y.shape[0]} {y.shape[1]} {quantiser}')
        ours = ((y.T * 32767).astype('<i2') if quantiser == 'trunc' else dither16(y.T)).tobytes()
        with wave.open(str(written), 'rb') as f:
            theirs = f.readframes(f.getnframes())
        status = 'quantisation matches the script' if ours == theirs else 'QUANTISATION DIFFERS from the script'
        problems += ours != theirs
        if args.masters is not None:
            master = args.masters / (name + '.wav')
            if master.exists():
                with wave.open(str(master), 'rb') as f:
                    shipped = f.readframes(f.getnframes())
                same = shipped == ours
                status += ', master ' + ('identical' if same else 'DIFFERS')
                problems += not same
            else:
                status += ', no master'
        line = f'// {name} ({y.shape[0]} ch, {y.shape[1]} frames): starts at ' + '; '.join(f'{k} = {v}' for k, v in starts.items())
        if primes:
            line += '\n//   reuses notes cached by earlier outputs:\n//     ' + '\n//     '.join(primes)
        report.append(line + '\n//   ' + status)
        primes.clear()
        index[0] += 1
        starts = {k: state_cpp(g) for k, g in generators().items()}

    if hasattr(module, 'write'):
        original_write = module.write

        def write(path, x):
            target = scratch / Path(path).name
            original_write(str(target), x)
            capture(path, x, 'trunc', target)
        module.write = write
        # a script that imported write from another (angel_music from meadow_music)
        for key, value in list(vars(module).items()):
            if isinstance(value, types.ModuleType) and getattr(value, 'write', None) is original_write:
                value.write = write
    original_write_wav = synth.write_wav

    def write_wav(path, x, bits=24, loop=None):
        target = scratch / Path(path).name
        original_write_wav(str(target), x, bits=bits, loop=loop)
        capture(path, x, 'dither' if bits == 16 else 'round24', target)
    synth.write_wav = write_wav
    if getattr(module, 'write_wav', None) is original_write_wav:
        module.write_wav = write_wav

    module.main()
    (out / 'manifest.txt').write_text('\n'.join(lines) + '\n')
    (out / 'states.txt').write_text('\n'.join(report) + '\n')
    print('\n'.join(report))
    print(f'{len(lines)} outputs in {out}; {problems} problems')


if __name__ == '__main__':
    main()
