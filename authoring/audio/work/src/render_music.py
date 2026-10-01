"""Render the music loops: python3 render_music.py [song_id ...]"""
import sys, json, subprocess, re, time, os
import numpy as np
from synth import SR, write_wav
from compose import render, master
from songs import SONGS, arrange
from songs2 import SONGS2, arrange2

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'outputs', 'music')
WORK = os.path.join(ROOT, 'work')
TARGET_LUFS = -20.0
CEIL_DB = -3.0


def lufs(path):
    r = subprocess.run(['ffmpeg', '-hide_banner', '-nostats', '-i', path, '-af', 'ebur128=peak=true', '-f', 'null', '-'],
                       capture_output=True, text=True).stderr
    i = float(re.findall(r'I:\s+(-?[\d.]+) LUFS', r)[-1])
    tp = float(re.findall(r'Peak:\s+(-?[\d.]+) dBFS', r)[-1])
    return i, tp


def soft_limit(x, ceil):
    """Gentle tanh knee above 70 % of the ceiling; leaves most material untouched."""
    k = 0.7 * ceil
    a = np.abs(x)
    over = a > k
    y = x.copy()
    y[over] = np.sign(x[over]) * (k + (ceil - k) * np.tanh((a[over] - k) / (ceil - k)))
    return y


def main(ids):
    batch2 = '--batch2' in ids
    ids = [i for i in ids if i != '--batch2']
    songs, arr = (SONGS2, arrange2) if batch2 else (SONGS, arrange)
    os.makedirs(OUT, exist_ok=True)
    report = {}
    for song in songs:
        if ids and song['id'] not in ids:
            continue
        t0 = time.time()
        ctx = arr(song)
        mix, spb = render(song, ctx)
        out, stats = master(song, mix, spb)
        tmp = os.path.join(WORK, f"tmp_{song['id']}.wav")
        out = out / np.abs(out).max() * 0.5
        write_wav(tmp, out)
        i, _ = lufs(tmp)
        out = out * 10 ** ((TARGET_LUFS - i) / 20)
        ceil = 10 ** (CEIL_DB / 20)
        if np.abs(out).max() > 0.7 * ceil:
            out = soft_limit(out, ceil)
        path = os.path.join(OUT, f"music_{song['id']}_loop.wav")
        write_wav(path, out, 24, loop=(0, out.shape[1] - 1))
        i, tp = lufs(path)
        with open(os.path.join(WORK, f"score_{song['id']}.json"), 'w') as fh:
            json.dump(dict(song={k: v for k, v in song.items() if k != 'bus'}, samples_per_beat=spb,
                           events=[list(e) for e in sorted(ctx.ev, key=lambda e: e[1])]), fh, default=list)
        report[song['id']] = dict(file=os.path.relpath(path, ROOT), samples=out.shape[1], seconds=out.shape[1] / SR,
                                  bpm_exact=SR * 60 / spb, samples_per_beat=spb, bars=int(ctx.total / song['bpb']),
                                  beats_per_bar=song['bpb'], lufs=i, true_peak_dbtp=tp, bus_rms_db=stats,
                                  notes=len(ctx.ev), render_s=round(time.time() - t0, 1))
        os.remove(tmp)
        print(song['id'], json.dumps(report[song['id']]), flush=True)
    rp = os.path.join(WORK, 'music_render_report.json')
    old = json.load(open(rp)) if os.path.exists(rp) else {}
    old.update(report)
    json.dump(old, open(rp, 'w'), indent=1)


if __name__ == '__main__':
    main(sys.argv[1:])
