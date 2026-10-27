// The gnome's voice, performed on the monk choir's HDR voice: a physical glottis
// driving a vocal-tract tube with a nasal port, turbulence at the glottis and the
// narrowest point of the mouth, and closure impacts. Nothing is recorded or copied.
//
// He is a small man: a tenor's tract scaled to 72% (his formants sit about a third
// above a grown man's), speaking around 300 Hz and laughing an octave above that. His
// laugh is a slow, breathy chuckle on a rounded "hoo": a syllable every third of a second,
// each hooking down four or five semitones, fading into breath. Each sound is a
// performance: timed gestures (breath, voice, vowel, pitch, tongue tip, velum) played
// through the voice and written as a clip.
//
//   node audio_src/gnome_voice.mjs [engine-dir] [out-dir]
//     engine-dir  the monk choir's engine/ (default ~/visualizations/monk-choir/engine)
//     out-dir     where the clips go (default assets/audio next to this folder)
import { writeFileSync } from "node:fs";
import { homedir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const engineDir = resolve(process.env.MONK_ENGINE ?? process.argv[2] ?? join(homedir(), "visualizations/monk-choir/engine"));
const outDir = resolve(process.argv[3] ?? join(here, "../assets/audio"));
const { HdrVoice, CONTROL_BLOCK } = await import(pathToFileURL(join(engineDir, "hdr.js")).href);
const { makeAnatomy } = await import(pathToFileURL(join(engineDir, "anatomy.js")).href);
const { REGISTERS } = await import(pathToFileURL(join(engineDir, "glottis.js")).href);

const SR = 48000;
// Settings the performances share; tools can change them before rendering.
export const STYLE = { laughVowel: 0.15, tuning: "none", chuckle: null, epilarynx: 0.45, speak: null, nyahVowel: 0.6 };

// Glottal settings for the parts of a syllable.
const BREATH = { ...REGISTERS.breathy, oq: 0.97, leak: 0.9, breath: 0.55, skew: 0.6, qa: 0.3 };     // "h": the folds apart
const VOICE = { ...REGISTERS.head, oq: 0.52, leak: 0.03, breath: 0.06, qa: 0.07 };                  // a bright, pinched head voice
const CACKLE = { ...REGISTERS.head, oq: 0.45, leak: 0.02, breath: 0.08, qa: 0.05, jitter: 0.012, shimmer: 0.08 };
// The chuckle: a soft, breathy, nearly falsetto voice from the first moment, no
// separate "h": long open phase and a slow closure, so the fundamental dominates and
// the upper harmonics fall away (matched to within about 2 dB, harmonic by harmonic,
// against the meme's laugh).
// His speaking voice: breathy, with the fundamental strongest and the harmonics falling
// away steadily, as the meme's speech does.
const SPEAK = { ...REGISTERS.breathy, qa: 0.15, breath: REGISTERS.breathy.breath + 0.04 };
const CHUCKLE = { ...REGISTERS.falsetto, oq: 0.8, qa: 0.3, breath: 0.12, leak: 0.15, jitter: 0.01, shimmer: 0.05 };

// The folds move between breath and voice quickly but not instantly: a jump in the
// glottal opening is a click (the HDR voice's impact layer hears the slam).
function setSource(v, src) {
    v.goal = { ...src };
    Object.assign(v.sourceTarget, src);
    // With the folds apart for an "h" there is nothing to collide; the impact layer
    // otherwise hears the first closure out of the breath as a slam.
    v.amount.impact = src.leak > 0.5 ? 0.0 : 0.3;
}

function followSource(v, dt) {
    if (v.goal === undefined)
        return;
    const k = 1 - Math.exp(-dt / 0.009);
    for (const key of ["oq", "skew", "qa", "leak", "breath", "vent"])
        v.source[key] += (v.goal[key] - v.source[key]) * k;
    v.source.jitter = v.goal.jitter;
    v.source.shimmer = v.goal.shimmer;
}

export function gnome(seed) {
    const v = new HdrVoice(SR, makeAnatomy("tenor", 0.72), "head", seed);
    v.goal = { ...VOICE };
    v.vibratoDepth = 0.12;
    v.vibratoRate = 7.0;
    v.driftCents = 6.0;
    v.attack = 0.012;
    v.release = 0.011;
    v.glideRate = 90.0;
    v.vowelGlide = 0.025;
    v.effort = 0.8;
    v.epilarynx = STYLE.epilarynx ?? 0.65; // a narrow epilarynx: the twangy ring
    v.tuning = STYLE.tuning;
    v.amount.noise = STYLE.noise ?? 0.75;
    v.aspirationLevel = STYLE.aspiration ?? 0.2;
    if (STYLE.crossmodes === false)
        v.layers.crossmodes = false;
    return v;
}

// A performance: gestures at times, rendered for `seconds`.
function perform(v, seconds, gestures) {
    gestures.sort((a, b) => a[0] - b[0]);
    const n = Math.round(seconds * SR);
    const out = new Float64Array(n);
    const l = new Float32Array(CONTROL_BLOCK);
    const r = new Float32Array(CONTROL_BLOCK);
    let next = 0;
    for (let off = 0; off < n; off += CONTROL_BLOCK) {
        const now = off / SR;
        while (next < gestures.length && gestures[next][0] <= now) {
            gestures[next][1](v);
            next += 1;
        }
        followSource(v, CONTROL_BLOCK / SR);
        l.fill(0);
        r.fill(0);
        v.render(l, r, 0, CONTROL_BLOCK);
        for (let i = 0; i < CONTROL_BLOCK && off + i < n; i++) out[off + i] = 0.5 * (l[i] + r[i]);
    }
    return out;
}

// One laughing syllable: an "h" of breath, then the voice on a vowel, its pitch falling.
function laugh(g, t, note, vowel, length, fall, breathy = 0.035, src = CACKLE) {
    g.push([t, (v) => { setSource(v, BREATH); v.vowel = v.vowelTarget = vowel; v.noteOn(note, 0.55); }]);
    g.push([t + breathy, (v) => { setSource(v, src); v.velocity = 1.0; }]);
    g.push([t + breathy + 0.01, (v) => { v.noteTarget = note - fall; }]);
    g.push([t + breathy + length, (v) => { v.noteOff(); }]);
}

function hz(note) {
    return 69 + 12 * Math.log2(note / 440);
}

// One syllable of his chuckle, shaped like the meme's: it starts as breath about 20 dB
// down, swells as the voice comes in to a peak near its end while the pitch hooks from
// `from` to `to` Hz, then fades over a few tens of milliseconds. `air` (0..1) keeps it
// breathier throughout.
function chuckle(g, t, from, to, length, vowel, level, air = 0) {
    const a = hz(from);
    const b = hz(to);
    const base = STYLE.chuckle ?? CHUCKLE;
    const breathy = (amount) => ({ ...base, breath: (base.breath + 0.35 * amount) * (STYLE.glottalBreath ?? 1), leak: base.leak + 0.4 * amount, oq: Math.min(0.95, base.oq + 0.2 * amount) });
    g.push([t, (v) => {
        setSource(v, breathy(Math.min(1, air + 0.6)));
        v.vowel = v.vowelTarget = vowel;
        v.glideRate = Math.abs(b - a) / (length * 0.8) + 1;
        v.release = 0.026;
        v.noteOn(a, level * 0.12);
        v.note = a;
    }]);
    g.push([t + 0.012, (v) => { v.noteTarget = b; }]);
    // the swell: louder and less breathy, step by step, to its peak
    const steps = Math.max(2, Math.round(length * 0.85 / 0.01));
    for (let k = 1; k <= steps; k++) {
        const u = k / steps;
        g.push([t + u * length * 0.85, (v) => {
            v.velocity = level * (0.12 + 0.88 * Math.pow(u, 1.6));
            setSource(v, breathy(air + 0.6 * (1 - Math.min(1, u * 1.6))));
        }]);
    }
    g.push([t + length, (v) => { v.noteOff(); }]);
}

// "hoo... hoo... hoo-hoo... hoo": his chuckle. Strong and evenly spaced at first, then
// quicker, fainter and breathier until the last is only breath.
export function giggle(seed) {
    const g = [];
    const vowel = STYLE.laughVowel;
    chuckle(g, 0.03, 470, 615, 0.22, vowel, 0.85);
    chuckle(g, 0.37, 700, 570, 0.18, vowel, 0.9);
    chuckle(g, 0.69, 720, 545, 0.18, vowel, 0.85);
    chuckle(g, 1.01, 700, 520, 0.2, vowel, 0.8, 0.15);
    chuckle(g, 1.43, 565, 505, 0.11, vowel, 0.5, 0.35);
    chuckle(g, 1.72, 600, 520, 0.08, vowel, 0.3, 1.0);
    chuckle(g, 1.91, 540, 520, 0.2, vowel, 0.75, 0.2);
    chuckle(g, 2.21, 550, 445, 0.11, vowel, 0.45, 0.4);
    chuckle(g, 2.42, 260, 245, 0.08, 0.5, 0.25, 0.6);
    return perform(gnome(seed), 2.75, g);
}

// "hoo-hoo-hoo": the same chuckle on "oo", rounder, a little quicker, mocking.
function hoots(seed) {
    const g = [];
    const vowel = 0.06;
    chuckle(g, 0.03, 520, 660, 0.18, vowel, 0.85);
    chuckle(g, 0.31, 720, 560, 0.16, vowel, 0.9);
    chuckle(g, 0.57, 740, 540, 0.16, vowel, 0.85, 0.1);
    chuckle(g, 0.85, 700, 500, 0.22, vowel, 0.8, 0.2);
    chuckle(g, 1.2, 560, 480, 0.1, vowel, 0.35, 0.8);
    return perform(gnome(seed), 1.5, g);
}

// "nyah-nyah, nya-nyah-nyah": the playground jeer in his speaking voice, around 300 Hz,
// each syllable bending as he sneers it. Each starts with the tongue against the palate
// and the nose open (ny) and springs open into a bright, nasal "ah".
export function nyah(seed) {
    const g = [];
    // sol mi la sol mi, on F: C4.. no: F4 D4 G4 F4 D4
    const tune = [[0.0, 0.22, 350, 340], [0.27, 0.22, 300, 285], [0.53, 0.15, 395, 385], [0.72, 0.22, 352, 335], [0.99, 0.5, 300, 255]];
    for (const [start, length, from, to] of tune) {
        const t = start + 0.03;
        g.push([t - 0.03, (v) => { v.stopTipTarget = 1.0; v.velumOpenTarget = 1.0; v.vowel = v.vowelTarget = 1.0; }]);
        g.push([t, (v) => { setSource(v, STYLE.speak ?? SPEAK); v.glideRate = 20; v.noteOn(hz(from) - 0.8, 0.95); v.noteTarget = hz(from); }]);
        g.push([t + 0.045, (v) => { v.stopTipTarget = 0.0; v.vowelGlide = 0.05; v.vowelTarget = STYLE.nyahVowel; v.velumOpenTarget = 0.3; }]);
        g.push([t + length * 0.45, (v) => { v.glideRate = 12; v.noteTarget = hz(to); }]);
        g.push([t + length, (v) => { v.noteOff(); v.vowelGlide = 0.025; }]);
    }
    // and his chuckle at the end
    chuckle(g, 1.6, 690, 560, 0.16, 0.72, 0.8);
    chuckle(g, 1.9, 700, 530, 0.17, 0.72, 0.7, 0.2);
    return perform(gnome(seed), 2.25, g);
}

// "hoo!": popping up out of the grass, the scoop up and the fall of his first chuckle.
function hoo(seed, twice) {
    const g = [];
    if (twice) {
        chuckle(g, 0.03, 480, 640, 0.2, 0.08, 0.9);
        chuckle(g, 0.33, 720, 540, 0.22, 0.08, 0.9, 0.1);
        return perform(gnome(seed), 0.75, g);
    }
    g.push([0.03, (v) => { setSource(v, CHUCKLE); v.vowel = v.vowelTarget = 0.08; v.glideRate = 30; v.noteOn(hz(460), 0.9); v.note = hz(460); v.noteTarget = hz(690); }]);
    g.push([0.17, (v) => { v.glideRate = 22; v.noteTarget = hz(540); v.vowelTarget = 0.2; }]);
    g.push([0.36, (v) => { v.noteOff(); }]);
    return perform(gnome(seed), 0.6, g);
}

function writeClip(name, mono) {
    // Level by loudness, not by the odd sharp peak: the voiced part at -17 dBFS RMS,
    // then a soft limit on anything over half scale.
    let sum = 0;
    let count = 0;
    for (const x of mono) {
        if (Math.abs(x) > 1e-4) {
            sum += x * x;
            count += 1;
        }
    }
    let peak = 0;
    for (const x of mono) peak = Math.max(peak, Math.abs(x));
    const gain = 0.141 / Math.sqrt(sum / Math.max(1, count));
    const n = mono.length;
    const data = Buffer.alloc(44 + n * 4);
    data.write("RIFF", 0);
    data.writeUInt32LE(36 + n * 4, 4);
    data.write("WAVE", 8);
    data.write("fmt ", 12);
    data.writeUInt32LE(16, 16);
    data.writeUInt16LE(1, 20);
    data.writeUInt16LE(2, 22);
    data.writeUInt32LE(SR, 24);
    data.writeUInt32LE(SR * 4, 28);
    data.writeUInt16LE(4, 32);
    data.writeUInt16LE(16, 34);
    data.write("data", 36);
    data.writeUInt32LE(n * 4, 40);
    for (let i = 0; i < n; i++) {
        const fade = Math.min(1, (n - 1 - i) / 480);
        const y = mono[i] * gain * fade;
        const a = Math.abs(y);
        const s = Math.sign(y) * (a < 0.5 ? a : 0.5 + 0.45 * Math.tanh((a - 0.5) / 0.45));
        const v = Math.round(s * 32767);
        data.writeInt16LE(v, 44 + i * 4);
        data.writeInt16LE(v, 46 + i * 4);
    }
    writeFileSync(join(process.env.GNOME_OUT ?? outDir, `${name}.wav`), data);
    let nan = 0;
    for (const x of mono) if (!Number.isFinite(x)) nan++;
    console.log(`${name}: ${(n / SR).toFixed(2)} s, peak ${peak.toExponential(2)}${nan ? `, ${nan} non-finite samples` : ""}`);
}

export function writeAll() {
writeClip("mm_gnome_hoo", hoo(11, false));
writeClip("mm_gnome_hoo2", hoo(12, true));
writeClip("mm_gnome_giggle", giggle(13));
writeClip("mm_gnome_giggle2", hoots(14));
writeClip("mm_gnome_nyah", nyah(15));
}

export { writeClip, SR };

if (import.meta.url === pathToFileURL(process.argv[1]).href)
    writeAll();
