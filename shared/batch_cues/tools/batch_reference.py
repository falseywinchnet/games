#!/usr/bin/env python3
"""NumPy reference renders for shared/batch_cues (the audio batch 1/2 scripts).

The batch scripts in authoring/audio/work/src do not go through a make_sfx.py write():
sfx.py and sfx2.py pass each cue through finish() and synth.write_wav (24-bit), and
render_music.py masters a song through compose.master, an ffmpeg loudness step and a
soft limiter. So shared/voice/tools/numpy_reference.py does not apply; this does the
same job for them. Writes raw little-endian float64 arrays and a manifest.txt into --out
(read by tests/batch_cues_tests.cpp --reference DIR):

  cues   python3 batch_reference.py cues --src SRC --script sfx2 --out DIR [--masters DIR ...]
         Runs `python3 <script>.py` with write_wav intercepted. Each cue's float master
         (finish() output, before 24-bit quantisation) is saved as `rNNNN cue NAME`.
         Every render_note cache miss is logged with the state synth._rng had then,
         and written to notes_<script>.inc as C++ for the cue tables: the cues are
         rendered alone, so each note is primed from the state the script rendered it at.
         With --masters, the 24-bit quantisation is compared with the shipped WAV masters.

  music  python3 batch_reference.py music --src SRC --song switchbox --out DIR [--master WAV]
         Runs render_music.py --batch2 for that song alone (as `render_music.py --batch2
         switchbox` does) and saves the float loop before quantisation as `rNNNN stereo
         music_<song>_loop` (left then right), the loudness ffmpeg measured, and the event
         list. --after renders the batch's earlier songs first (a whole `--batch2` run).

Needs NumPy 1.26 (the Mini's /usr/bin/python3) and, for music, ffmpeg on PATH.
Deterministic; writes only into --out.
"""
from __future__ import annotations

import argparse
import importlib
import os
import sys
import wave
from pathlib import Path

import numpy as np


def state_words(g):
    s = g.bit_generator.state
    st, inc = s['state']['state'], s['state']['inc']
    m = (1 << 64) - 1
    return [st >> 64, st & m, inc >> 64, inc & m, int(s['has_uint32']), int(s['uinteger'])]


def state_cpp(g):
    w = state_words(g)
    return ('{{0x%016xull, 0x%016xull}, {0x%016xull, 0x%016xull}, %s, 0x%08xu}'
            % (w[0], w[1], w[2], w[3], 'true' if w[4] else 'false', w[5]))


class Writer:
    def __init__(self, out: Path):
        self.out = out
        out.mkdir(parents=True, exist_ok=True)
        self.lines = []
        self.count = 0

    def put(self, kind, args, data):
        name = f'r{self.count:04d}'
        self.count += 1
        np.asarray(data, dtype='<f8').tofile(self.out / (name + '.bin'))
        self.lines.append(' '.join([name, kind] + [str(a) for a in args]))

    def close(self):
        (self.out / 'manifest.txt').write_text('\n'.join(self.lines) + '\n')


def read24(path: Path):
    with wave.open(str(path), 'rb') as f:
        raw = f.readframes(f.getnframes())
        ch = f.getnchannels()
        width = f.getsampwidth()
    if width != 3:
        return None
    b = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32)
    v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
    v = np.where(v >= 1 << 23, v - (1 << 24), v)
    return v.reshape(-1, ch).T


def quant24(x):
    return np.round(np.clip(np.atleast_2d(x), -1, 1) * 8388607).astype(np.int32)


def install_note_log(synth, modules):
    """render_note wrapper: the state of synth._rng at each cache miss, in order."""
    original = synth.render_note
    log = []

    def render_note(inst, m, dur, vel):
        vq = round(vel * 20) / 20
        dq = round(dur * 100) / 100
        if (inst, m, dq, vq) not in synth._cache:
            log.append((inst, m, dur, vel, state_cpp(synth._rng)))
        return original(inst, m, dur, vel)

    synth.render_note = render_note
    for module in modules:
        if getattr(module, 'render_note', None) is original:
            module.render_note = render_note
    return log


def cues(src: Path, script: str, out: Path, masters: list[Path]):
    sys.path.insert(0, str(src.resolve()))
    module = importlib.import_module(script)
    synth = sys.modules['synth']
    related = [sys.modules[k] for k in ('sfx', 'sfx2', 'compose') if k in sys.modules] + [module]
    log = install_note_log(synth, related)
    for m in related:
        if hasattr(m, 'lufs'):
            m.lufs = lambda path: None  # the report's loudness; the cues do not depend on it
    w = Writer(out)
    report = []
    problems = 0

    def write_wav(path, x, bits=24, loop=None):
        nonlocal problems
        name = os.path.basename(path)[:-4]
        x = np.asarray(x, dtype=float)
        mono = x if x.ndim == 1 else x[0]
        if x.ndim == 2 and not np.array_equal(x[0], x[1]):
            raise SystemExit(f'{name}: stereo cue with different channels')
        w.put('cue', [name, 'stereo' if x.ndim == 2 else 'mono'], mono)
        line = f'{name}: {len(mono)} samples'
        for d in masters:
            p = d / (name + '.wav')
            if p.exists():
                theirs = read24(p)
                ours = quant24(x)
                if theirs is None:
                    line += ', master not 24-bit'
                elif theirs.shape != ours.shape:
                    line += f', master LENGTH DIFFERS ({theirs.shape} vs {ours.shape})'
                    problems += 1
                else:
                    diff = int(np.abs(theirs.astype(np.int64) - ours).max())
                    line += ', master identical' if diff == 0 else f', master max {diff} LSB'
                    problems += diff != 0
                break
        else:
            line += ', no master'
        report.append(line)

    module.write_wav = write_wav
    if script == 'sfx2':
        sys.modules['sfx'].write_wav = write_wav
    module.main()
    w.close()
    inc = [f'// render_note cache misses of {script}.py in order: inst, m, dur, vel, synth._rng then.',
           '// Generated by shared/batch_cues/tools/batch_reference.py; do not edit.']
    for inst, m, dur, vel, st in log:
        inc.append(f'{{"{inst}", {float(m)!r}, {float(dur)!r}, {float(vel)!r}, {st}}},')
    (out / f'notes_{script}.inc').write_text('\n'.join(inc) + '\n')
    (out / 'report.txt').write_text('\n'.join(report) + '\n')
    print('\n'.join(report))
    print(f'{w.count} cues, {len(log)} distinct notes in {out}; {problems} problems')


def music(src: Path, song_id: str, out: Path, master: Path | None, after: bool, masters_dir: Path | None = None,
          songs: list[str] | None = None):
    sys.path.insert(0, str(src.resolve()))
    import json
    import subprocess
    import tempfile
    rm = importlib.import_module('render_music')
    synth = sys.modules['synth']
    compose = sys.modules['compose']
    log = install_note_log(synth, [compose, rm])
    w = Writer(out)
    measured = {}
    original_lufs = rm.lufs

    def lufs(path):
        i, tp = original_lufs(path)
        measured.setdefault(os.path.basename(path), []).append(i)
        return i, tp

    rm.lufs = lufs
    captured = {}
    original_write = rm.write_wav

    def write_wav(path, x, bits=24, loop=None):
        base = os.path.basename(path)[:-4]
        if base.startswith('music_'):
            if base == f'music_{song_id}_loop':
                captured[base] = np.array(x, dtype=float)
            if masters_dir is not None and (masters_dir / (base + '.wav')).exists():
                theirs = read24(masters_dir / (base + '.wav'))
                ours = quant24(x)
                same = theirs is not None and theirs.shape == ours.shape
                print('compare', base, 'max LSB', int(np.abs(theirs.astype(np.int64) - ours).max()) if same else 'SHAPE', flush=True)
        original_write(path, x, bits, loop)

    rm.write_wav = write_wav
    tmp = Path(tempfile.mkdtemp(dir=out))
    rm.OUT = str(tmp)
    rm.WORK = str(tmp)
    ids = [s['id'] for s in rm.SONGS2]
    if songs:
        ids = songs
    elif after:
        ids = ids[:ids.index(song_id) + 1]
    if after or songs:
        state_at = {}
        original_arrange = rm.arrange2

        def arrange2(song):
            state_at[song['id']] = state_cpp(synth._rng)
            log_len[song['id']] = len(log)
            return original_arrange(song)
        log_len = {}
        rm.arrange2 = arrange2
        rm.main(['--batch2'] + ids)
        if song_id not in log_len:
            w.close()
            return
        start = log_len[song_id]
        notes = log[start:]
        print('synth._rng at the start of', song_id, '=', state_at[song_id])
        print('notes cached before it:', start)
    else:
        rm.main(['--batch2', song_id])
        notes = log
    name = f'music_{song_id}_loop'
    x = captured[name]
    w.put('stereo', [name, x.shape[1]], np.concatenate([x[0], x[1]]))
    for k, v in measured.items():
        print('loudness', k, v)
    (out / 'loudness.txt').write_text(json.dumps(measured) + '\n')
    if master is not None:
        theirs = read24(master)
        ours = quant24(x)
        if theirs is None or theirs.shape != ours.shape:
            print('master LENGTH or FORMAT DIFFERS', None if theirs is None else theirs.shape, ours.shape)
        else:
            print('master max LSB difference', int(np.abs(theirs.astype(np.int64) - ours).max()))
    print(len(notes), 'notes rendered for', song_id)
    w.close()
    for p in tmp.iterdir():
        p.unlink()
    tmp.rmdir()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='mode', required=True)
    c = sub.add_parser('cues')
    c.add_argument('--src', type=Path, required=True)
    c.add_argument('--script', choices=('sfx', 'sfx2'), required=True)
    c.add_argument('--out', type=Path, required=True)
    c.add_argument('--masters', type=Path, action='append', default=[])
    m = sub.add_parser('music')
    m.add_argument('--src', type=Path, required=True)
    m.add_argument('--song', required=True)
    m.add_argument('--out', type=Path, required=True)
    m.add_argument('--master', type=Path)
    m.add_argument('--after', action='store_true')
    m.add_argument('--masters', type=Path, help='compare every song rendered with the masters here')
    m.add_argument('--songs', nargs='*', help='render these batch-2 songs in this order (one process)')
    args = parser.parse_args()
    if args.mode == 'cues':
        cues(args.src, args.script, args.out, args.masters)
    else:
        music(args.src, args.song, args.out, args.master, args.after, args.masters, args.songs)


if __name__ == '__main__':
    main()
