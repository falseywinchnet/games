"""Validate masters, encode runtime files, write outputs/audio_manifest.json + .md"""
import os, json, subprocess, re, glob
import numpy as np
from synth import read_wav, SR

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
O = os.path.join(ROOT, 'outputs')
RT = os.path.join(O, 'runtime')


def check(x, loop):
    r = {}
    r['peak_dbfs'] = round(20 * np.log10(np.abs(x).max()), 2)
    r['clipped_samples'] = int((np.abs(x) >= 0.999).sum())
    r['dc_offset_max'] = float(f"{np.abs(x.mean(1)).max():.2e}")
    if loop:
        d = np.abs(np.diff(x, axis=1))
        seam = np.abs(x[:, 0] - x[:, -1]).max()
        r['loop_seam_jump'] = float(f'{seam:.2e}')
        r['loop_seam_jump_percentile'] = round(float((d.max(0) < seam).mean() * 100), 1)
        w = 2400
        a = np.sqrt(np.mean(x[:, -w:] ** 2))
        b = np.sqrt(np.mean(x[:, :w] ** 2))
        r['loop_seam_rms_step_db'] = round(20 * np.log10(b / a), 2)
        # high-frequency click energy at seam vs median elsewhere
        hp = np.diff(np.diff(x.mean(0)))
        seg = np.concatenate([x.mean(0)[-64:], x.mean(0)[:64]])
        hs = np.abs(np.diff(np.diff(seg))).max()
        r['loop_seam_click_ratio'] = round(float(hs / np.percentile(np.abs(hp), 99.9)), 3)
    else:
        r['start_abs'] = float(f'{np.abs(x[:, 0]).max():.1e}')
        r['end_abs'] = float(f'{np.abs(x[:, -1]).max():.1e}')
    return r


def lufs(path):
    e = subprocess.run(['ffmpeg', '-hide_banner', '-nostats', '-i', path, '-af', 'ebur128=peak=true', '-f', 'null', '-'],
                       capture_output=True, text=True).stderr
    i = float(re.findall(r'I:\s+(-?[\d.]+) LUFS', e)[-1])
    tp = float(re.findall(r'Peak:\s+(-?[\d.]+) dBFS', e)[-1])
    return (i if i > -69 else None), tp


def enc(src, base, music):
    os.makedirs(RT, exist_ok=True)
    br_o, br_a = ('112k', '160k') if music else ('64k', '96k')
    ogg = os.path.join(RT, base + '.ogg')
    m4a = os.path.join(RT, base + '.m4a')
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', src, '-c:a', 'libopus', '-b:a', br_o, ogg], check=True)
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', src, '-c:a', 'aac_at', '-b:a', br_a, m4a], check=True)
    return [os.path.relpath(ogg, ROOT), os.path.relpath(m4a, ROOT)]


GAME_EVENTS = {
    'card_pickup': 'all card games: card or stack lifted', 'card_place': 'all: card dropped on tableau/free cell',
    'card_deal': 'all: one card dealt (fire per card, ~40-70 ms apart)', 'card_flip': 'Klondike/Spider: face-down card turned up; Klondike stock draw',
    'card_foundation': 'valid foundation move (Klondike/FreeCell), completed run removed (Spider), trick taken (Hearts); use 01..04 ascending for consecutive moves',
    'card_shuffle': 'new game / redeal', 'ui_undo': 'undo', 'ui_hint': 'hint shown', 'ui_invalid': 'gentle invalid move / rejected drop',
    'ui_victory': 'generic victory SFX (layer with game stinger or use alone)',
    'puzzle_walk': 'puzzle: player step (round-robin 01-04)', 'puzzle_push': 'puzzle: block pushed one tile (01-03)',
    'puzzle_blocked': 'puzzle: walk/push into wall', 'puzzle_on_target': 'puzzle: block lands on goal tile',
    'puzzle_undo': 'puzzle: undo', 'puzzle_win': 'puzzle: level solved', 'puzzle_fail': 'puzzle: stuck/restart/fail',
    # ---- batch 2
    'gems_hover': 'gem under cursor (hover/rotate start); very quiet, rate-limit to ~8/s', 'gems_swap': 'drag-swap accepted',
    'gems_swap_return': 'invalid swap animating back', 'gems_match': 'match cleared; 01..05 = cascade depth 1..5+ (clamp at 05)',
    'gems_powerup_created': 'star/bomb/hypercube gem formed', 'gems_star_line': 'star gem fires (row/column clear)',
    'gems_bomb': 'four-bomb detonates', 'gems_hypercube': 'hypercube used (colour clear)',
    'gems_level_up': 'difficulty step / new colour introduced', 'gems_no_moves_shuffle': 'board reshuffled (no moves)',
    'sudoku_note_place': 'right-click pencil note added', 'sudoku_note_remove': 'pencil note removed',
    'sudoku_digit_place': 'left-click digit placed (no sound on hover)', 'sudoku_erase': 'cell cleared',
    'sudoku_error': 'conflicting digit (error marker persists after undo; play once when the error appears)',
    'sudoku_row_complete': 'row completion animation', 'sudoku_column_complete': 'column completion animation',
    'sudoku_box_complete': 'box completion animation (if several complete at once play only the box cue)',
    'nature_cube_trace': 'path extended by one cell (round-robin; no stereo pan tied to rotation)',
    'nature_cube_connect': 'source joined to matching target', 'nature_cube_disconnect': 'path broken/retracted',
    'nature_cube_path_full': 'all paths connected, before win stinger (optional)',
    'untangle_grab': 'point picked up', 'untangle_release': 'point dropped', 'untangle_edges_clear': 'optional: a move reduces crossings to fewer (only if the game shows that count)',
    'untangle_resolve': 'no crossings remain (final resolution)',
    'atom_probe_fire': 'probe launched', 'atom_probe_result_pass': 'play only after the exit marker is shown',
    'atom_probe_result_absorb': 'play only after absorb marker is shown', 'atom_probe_result_reflect': 'play only after reflect marker is shown',
    'atom_probe_result_deflect': 'play only after the deflection exit marker is shown', 'atom_probe_mark': 'player marks suspected atom',
    'atom_probe_unmark': 'player removes mark', 'atom_probe_check_correct': 'final check: all correct', 'atom_probe_check_wrong': 'final check: errors',
    'four_pegs_place': 'peg placed in slot 1-4 (variant = slot, not colour)', 'four_pegs_remove': 'peg removed',
    'four_pegs_submit': 'guess submitted', 'four_pegs_feedback_exact': 'per revealed black/exact marker, after display',
    'four_pegs_feedback_partial': 'per revealed white/partial marker, after display', 'four_pegs_feedback_none': 'guess revealed with no markers',
    'four_pegs_out_of_turns': 'game lost',
    'switchbox_switch_on': 'player flips a switch on', 'switchbox_switch_off': 'switch flips off (player or the girl)',
    'switchbox_reach': 'the girl reaches out of the box', 'switchbox_reset': 'board reset', 'switchbox_unlock': 'sequence solved / box opens',
    'switchbox_giggle_boing': 'optional comic accent on her reaction (non-vocal)',
    'puzzle_solve_pickup': 'piece picked up', 'puzzle_solve_rotate': 'piece rotated 90 degrees', 'puzzle_solve_flip': 'piece mirrored',
    'puzzle_solve_place': 'piece set down', 'puzzle_solve_fit': 'piece snaps into a valid position', 'puzzle_solve_no_fit': 'drop rejected',
    'sticks_stones_stick_place': 'stick placed', 'sticks_stones_stone_place': 'stone placed', 'sticks_stones_remove': 'piece removed',
    'sticks_stones_constraint_ok': 'a line constraint becomes satisfied', 'sticks_stones_constraint_broken': 'a satisfied constraint becomes unsatisfied',
    'ui_name_key': 'top-score name entry: character typed', 'ui_name_backspace': 'top-score name entry: delete',
    'ui_name_confirm': 'top-score name saved', 'ui_new_game': 'New Game action (not on autosave/restore)',
}


def main():
    rep = json.load(open(os.path.join(ROOT, 'work', 'music_render_report.json')))
    from songs import SONGS
    from songs2 import SONGS2
    man = dict(generated_by='work/src (numpy synthesis; no samples)', sample_rate=SR, music=[], stingers=[], sfx=[])
    for s in SONGS + SONGS2:
        r = rep[s['id']]
        path = os.path.join(ROOT, r['file'])
        x = read_wav(path)
        i, tp = lufs(path)
        man['music'].append(dict(
            id=s['id'], title=s['title'], batch=2 if s in SONGS2 else 1, file=r['file'], format='WAV PCM 24-bit', sample_rate=SR, channels=2,
            samples=x.shape[1], seconds=round(x.shape[1] / SR, 3), bpm=round(r['bpm_exact'], 4),
            meter=f"{s['bpb']}/4", bars=r['bars'], samples_per_beat=r['samples_per_beat'],
            loop_start_sample=0, loop_end_sample_exclusive=x.shape[1], wav_smpl_chunk=True,
            sections=[(sec['name'], len(sec['bars'])) for sec in s['sections']],
            lufs_integrated=i, true_peak_dbtp=tp, validation=check(x, True),
            runtime=enc(path, os.path.basename(path)[:-4], True)))
        print(s['id'], man['music'][-1]['validation'], flush=True)
    s2 = json.load(open(os.path.join(ROOT, 'work', 'sfx2_render_report.json')))
    for path in sorted(glob.glob(os.path.join(O, 'music', 'stinger_*.wav'))):
        x = read_wav(path)
        i, tp = lufs(path)
        nm = os.path.basename(path)[:-4]
        man['stingers'].append(dict(file=os.path.relpath(path, ROOT), batch=2 if nm in s2 else 1,
                                    event=nm.replace('stinger_win_', 'win: ').replace('stinger_topscore_', 'top score / name-entry window: '),
                                    channels=2, seconds=round(x.shape[1] / SR, 3), lufs_integrated=i, true_peak_dbtp=tp,
                                    validation=check(x, False), runtime=enc(path, os.path.basename(path)[:-4], False)))
    srep = json.load(open(os.path.join(ROOT, 'work', 'sfx_render_report.json')))
    s2 = json.load(open(os.path.join(ROOT, 'work', 'sfx2_render_report.json')))
    srep.update(s2)
    for path in sorted(glob.glob(os.path.join(O, 'sfx', '*.wav'))):
        name = os.path.basename(path)[:-4]
        x = read_wav(path)
        ev = re.sub(r'_\d\d$', '', name)
        man['sfx'].append(dict(file=os.path.relpath(path, ROOT), event=ev, batch=2 if name in s2 else 1, use=GAME_EVENTS.get(ev, ''), channels=1,
                               seconds=round(x.shape[1] / SR, 3), peak_dbfs=srep[name]['peak_dbfs'],
                               max_rms50ms_dbfs=srep[name]['max_rms50ms_dbfs'], validation=check(x, False),
                               runtime=enc(path, name, False)))
    bad = [m['file'] for m in man['sfx'] + man['stingers'] + man['music'] if m['validation']['clipped_samples']]
    man['validation_summary'] = dict(files_with_clipping=bad)
    json.dump(man, open(os.path.join(O, 'audio_manifest.json'), 'w'), indent=1)
    print('clipping in:', bad)


if __name__ == '__main__':
    main()
