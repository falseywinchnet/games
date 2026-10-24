#!/usr/bin/env python3
"""NumPy reference renders of a whole sound script (effects, music loops, stingers).

numpy_reference.py `cues` covers scripts whose every cue goes through
write(name, x, peak_db). Music scripts (lab_music.py, garden_music.py,
villain_music.py, koikoi_audio.py ...) write through write(path, x) or
write(path, x, stereo) after their own normalisation, and share synth.py's
render_note cache from one loop to the next. This runs such a script with its
write() intercepted and saves, for every file it writes, the float master it
handed to write() (before clipping and 16-bit quantisation; stereo as the left
channel then the right), in numpy_reference.py's manifest format:

    rNNNN cue <name> <channels> <frames>

and prints, for every cue, the generator states at the moment the script began
it (synth._rng and every np.random.Generator in the script's globals) and the
render_note cache entries made before it that it reuses, as C++ for the cue
table: `{"inst", m, dur, vel, voice::GeneratorState{...}}` (the state _rng had
when the script rendered that note), ready for voice::Synth::prime_note.
Scripts whose write(name, x, peak_db) normalises are handled as `cues` does
(--fade, --fade-in). With --masters, each quantised master is compared with the
WAV of the same name there.

    /usr/bin/python3 score_reference.py --script games/koikoi/audio_src/koikoi_audio.py --out DIR

Needs NumPy 1.26 (the Mini's /usr/bin/python3). Writes only into --out.
"""
from __future__ import annotations

import argparse
import importlib.util
import os
import sys
import time
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


def pcm16(y: np.ndarray) -> bytes:
    y = np.clip(y, -1, 1)
    return ((y.T if y.ndim == 2 else y) * 32767).astype('<i2').tobytes()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--script', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--fade', type=float, default=.004)
    parser.add_argument('--fade-in', type=int, default=8)
    parser.add_argument('--masters', type=Path, action='append', default=[])
    args = parser.parse_args()
    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    rendered = out / 'script_wavs'
    rendered.mkdir(exist_ok=True)

    script = args.script.resolve()
    sys.path.insert(0, str(script.parent / 'engine'))
    sys.path.insert(0, str(script.parent))
    spec = importlib.util.spec_from_file_location('cue_script', script)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    synth = sys.modules['synth']
    for key in ('OUT', 'MASTERS'):
        if hasattr(module, key):
            setattr(module, key, str(rendered))
    if hasattr(module, 'subprocess'):  # afconvert and friends: not needed for the masters
        module.subprocess = types.SimpleNamespace(run=lambda *a, **k: None)

    def generators() -> dict[str, np.random.Generator]:
        found = {'synth._rng': synth._rng}
        for key, value in vars(module).items():
            if isinstance(value, np.random.Generator):
                found[key] = value
        return found

    # render_note: remember the _rng state each cache entry was made from, and which
    # earlier entries each cue reuses.
    made: dict[tuple, str] = {}
    cue_index = [0]
    made_in: dict[tuple, int] = {}
    reused: list[tuple] = []
    original_render_note = synth.render_note

    def render_note(inst, m, dur, vel):
        vq = round(vel * 20) / 20
        dq = round(dur * 100) / 100
        key = (inst, m, dq, vq)
        if key not in synth._cache:
            made[key] = state_cpp(synth._rng)
            made_in[key] = cue_index[0]
        elif made_in.get(key, -1) < cue_index[0] and key not in reused:
            reused.append(key)
        return original_render_note(inst, m, dur, vel)

    synth.render_note = render_note
    if getattr(module, 'render_note', None) is original_render_note:
        module.render_note = render_note

    lines: list[str] = []
    count = [0]
    problems = [0]
    starts = {k: state_cpp(g) for k, g in generators().items()}
    began = [time.perf_counter()]
    original_write = module.write

    def write(*a):
        nonlocal starts
        if isinstance(a[0], str) and os.sep not in a[0] and len(a) == 3 and not isinstance(a[2], bool):
            name, x, peak_db = a
            x = np.asarray(x, dtype=float)
            y = x / (np.max(np.abs(x)) + 1e-12) * 10 ** (peak_db / 20)
            k = min(len(y), int(args.fade * module.SR))
            y[-k:] *= np.linspace(1, 0, k)
            if args.fade_in:
                y[:args.fade_in] *= np.linspace(0, 1, args.fade_in)
            original_write(name, x.copy(), peak_db)
            with wave.open(str(rendered / (name + '.wav')), 'rb') as f:
                script_bytes = f.readframes(f.getnframes())
        else:
            path = a[0]
            name = os.path.splitext(os.path.basename(path))[0]
            y = np.array(a[1], dtype=float)
            original_write(*a)
            with wave.open(path, 'rb') as f:
                script_bytes = f.readframes(f.getnframes())
        seconds = time.perf_counter() - began[0]
        channels = 2 if y.ndim == 2 else 1
        frames = y.shape[-1]
        file = f'r{count[0]:04d}'
        count[0] += 1
        np.ascontiguousarray(y).astype('<f8').tofile(out / (file + '.bin'))
        lines.append(f'{file} cue {name} {channels} {frames}')
        ours = pcm16(y)
        status = 'normalisation matches the script' if ours == script_bytes else 'NORMALISATION DIFFERS from the script write()'
        problems[0] += ours != script_bytes
        for folder in args.masters:
            p = folder / (name + '.wav')
            if p.exists():
                with wave.open(str(p), 'rb') as f:
                    master = f.readframes(f.getnframes())
                if master == ours:
                    status += f', master {p.parent.name}/{p.name} identical'
                elif len(master) == len(ours):
                    d = np.abs(np.frombuffer(ours, '<i2').astype(int) - np.frombuffer(master, '<i2').astype(int))
                    status += f', master {p.parent.name}/{p.name} max {d.max()} LSB ({np.count_nonzero(d)} samples)'
                    problems[0] += 1
                else:
                    status += f', master {p.parent.name}/{p.name} LENGTH DIFFERS'
                    problems[0] += 1
        text = f'// {name} ({channels} ch, {frames} frames, NumPy {seconds:.2f} s): starts at\n'
        text += ''.join(f'//   {k} = {v}\n' for k, v in starts.items())
        if reused:
            text += f'//   reuses {len(reused)} notes cached before it:\n'
            text += ''.join(f'    {{"{k[0]}", {k[1]!r}, {k[2]!r}, {k[3]!r}, {made[k]}}},\n' for k in reused)
        text += '//   ' + status
        print(text, flush=True)
        (out / 'states.txt').open('a').write(text + '\n')
        starts = {k: state_cpp(g) for k, g in generators().items()}
        reused.clear()
        cue_index[0] += 1
        began[0] = time.perf_counter()

    (out / 'states.txt').write_text('')
    module.write = write
    module.main()
    (out / 'manifest.txt').write_text('\n'.join(lines) + '\n')
    print(f'{count[0]} cues in {out}; {problems[0]} problems')


if __name__ == '__main__':
    main()
