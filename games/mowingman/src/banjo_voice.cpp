#include "banjo_voice.hpp"

#include <algorithm>
#include <cmath>

namespace mm {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr int line_size = 4096;

// The banjo's open strings, first to fifth: D4 B3 G3 D3 and the short g4 drone.
constexpr int banjo_open[5] = {62, 59, 55, 50, 67};
// The guitar's, low to high: E2 A2 D3 G3 B3 E4.
constexpr int guitar_open[6] = {40, 45, 50, 55, 59, 64};

double hz_of(double midi) {
    return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
}

// Rolls: which string each eighth of a bar is picked on (1 first .. 5 drone).
constexpr int rolls[7][8] = {
    {2, 1, 5, 2, 1, 5, 2, 1},  // forward
    {3, 2, 5, 1, 5, 2, 3, 1},  // forward-reverse
    {3, 2, 5, 1, 4, 2, 5, 1},  // alternating thumb
    {1, 2, 5, 1, 2, 5, 1, 2},  // backward
    {3, 2, 1, 5, 3, 2, 1, 5},  // forward, from the thumb
    {3, 1, 5, 1, 4, 1, 5, 1},  // thumb and middle
    {4, 2, 1, 5, 3, 2, 1, 5},  // foggy forward
};

// The acts. Chords are numbered from the key: 0 I, 5 IV, 7 V, 9 vi, 2 ii, 10 bVII.
typedef BanjoVoice::Chord C;
const BanjoVoice::Act acts[] = {
    {"breakdown", {{0, 0}, {0, 0}, {5, 0}, {0, 0}, {0, 0}, {0, 0}, {7, 0}, {0, 0}}, 8, 128, 0.95, 0, 1, 1},
    {"B part", {{5, 0}, {5, 0}, {0, 0}, {0, 0}, {7, 0}, {7, 2}, {0, 0}, {0, 0}}, 8, 125, 0.9, 1, 1, 1},
    {"minor bridge", {{9, 1}, {9, 1}, {5, 0}, {5, 0}, {0, 0}, {0, 0}, {7, 0}, {7, 2}}, 8, 116, 0.78, 2, 2, 1},
    {"porch vamp", {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {5, 0}, {5, 0}, {7, 0}, {7, 0}}, 8, 112, 0.62, 3, 1, 0},
    {"creek", {{0, 0}, {0, 0}, {0, 0}, {7, 0}, {0, 0}, {0, 0}, {7, 2}, {0, 0}}, 8, 132, 1.0, 0, 1, 1},
    {"long road",
     {{0, 0}, {0, 0}, {5, 0}, {5, 0}, {0, 0}, {0, 0}, {7, 0}, {7, 0}, {0, 0}, {0, 0}, {5, 0}, {5, 0}, {0, 0}, {7, 0}, {0, 0}, {0, 0}},
     16, 122, 0.88, 1, 2, 1},
    {"old time", {{0, 0}, {0, 0}, {10, 0}, {10, 0}, {0, 0}, {0, 0}, {7, 0}, {0, 0}}, 8, 120, 0.85, 1, 1, 1},
    {"sunday", {{0, 0}, {9, 1}, {2, 1}, {7, 2}, {0, 0}, {9, 1}, {2, 1}, {7, 2}}, 8, 110, 0.7, 2, 2, 1},
};
constexpr int act_count = static_cast<int>(sizeof(acts) / sizeof(acts[0]));

// The G lick, in G: (string, fret, 0 pick / 1 hammer-on / 2 slide), one per eighth.
constexpr int g_lick[8][3] = {{3, 2, 0}, {3, 4, 1}, {1, 0, 0}, {5, 0, 0}, {2, 0, 0}, {3, 2, 2}, {3, 0, 0}, {4, 0, 0}};
// A pull-off run down from the top of the chord.
constexpr int run_lick[8][3] = {{1, 5, 0}, {1, 3, 1}, {1, 0, 1}, {2, 3, 0}, {2, 0, 1}, {5, 0, 0}, {3, 2, 0}, {3, 0, 1}};

} // namespace

BanjoVoice::BanjoVoice(std::uint32_t seed) : state_(seed * 2654435761U + 1U) {
    const float pans[5] = {0.28F, 0.22F, 0.18F, 0.12F, 0.32F};
    for (int k = 0; k < 5; ++k) {
        banjo_[k].line.assign(line_size, 0.0F);
        banjo_[k].pan = pans[k];
    }
    bass_.line.assign(line_size, 0.0F);
    bass_.pan = -0.05F;
    for (int k = 0; k < 6; ++k) {
        guitar_[k].line.assign(line_size, 0.0F);
        guitar_[k].pan = -0.42F + 0.03F * k;
    }
    // The head: a tight drum under the bridge rings in three broad bands.
    const double freq[3] = {390, 1050, 2600};
    const double q[3] = {5.5, 4.0, 2.5};
    const double g[3] = {0.55, 0.45, 0.3};
    for (int k = 0; k < 3; ++k) {
        const double w = 2 * pi * freq[k] / sample_rate;
        const double r = 1 - w / (2 * q[k]);
        head_a1_[k] = 2 * r * std::cos(w);
        head_a2_[k] = -r * r;
        head_g_[k] = g[k] * (1 - r);
    }
    burst_.assign(line_size, 0.0F);
    plan_act();
}

double BanjoVoice::uniform() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return (state_ & 0xFFFFFF) / 16777216.0;
}

int BanjoVoice::pick(int n) {
    return std::min(n - 1, static_cast<int>(uniform() * n));
}

// A pluck: the period's worth of the line is filled with a burst of noise, softened by
// how hard the pick is, and combed by where along the string it is struck.
void BanjoVoice::pluck(String& s, double midi, double strength, double hardness) {
    const double period = sample_rate / hz_of(midi);
    s.delay = std::clamp(period - 0.5, 4.0, line_size - 8.0);
    s.delay_to = s.delay;
    s.glide = 0;
    s.mute_in = -1;
    if (&s >= banjo_ && &s < banjo_ + 5) {
        // a banjo string rings a second or two, less the higher it is
        s.loss = static_cast<float>(std::pow(10.0, -3.0 / (1.5 * hz_of(midi))));
        s.bright = 0.62F;
    }
    const int n = static_cast<int>(s.delay);
    const int comb = std::max(1, static_cast<int>(n * (0.12 + 0.06 * uniform())));
    std::vector<float>& burst = burst_;
    double low = 0;
    for (int i = 0; i < n; ++i) {
        low += ((uniform() * 2 - 1) - low) * hardness;
        burst[static_cast<std::size_t>(i)] = static_cast<float>(low);
    }
    double mean = 0;
    for (int i = 0; i < n; ++i)
        mean += burst[static_cast<std::size_t>(i)];
    mean /= n;
    for (int i = 0; i < n; ++i) {
        const float v = burst[static_cast<std::size_t>(i)] - static_cast<float>(mean) - (i >= comb ? burst[static_cast<std::size_t>(i - comb)] - static_cast<float>(mean) : 0.0F);
        const int at = (s.write - n + i + line_size * 2) % line_size;
        s.line[static_cast<std::size_t>(at)] = s.line[static_cast<std::size_t>(at)] * 0.25F + v * static_cast<float>(strength);
    }
}

void BanjoVoice::slide_to(String& s, double midi, double seconds) {
    s.delay_to = std::clamp(sample_rate / hz_of(midi) - 0.5, 4.0, line_size - 8.0);
    s.glide = 1.0 / std::max(1.0, seconds * sample_rate);
}

float BanjoVoice::run(String& s) {
    if (s.glide > 0) {
        s.delay += (s.delay_to - s.delay) * std::min(1.0, s.glide * 6);
        if (std::abs(s.delay_to - s.delay) < 0.01)
            s.glide = 0;
    }
    const double at = s.write - s.delay;
    const double floor_at = std::floor(at);
    const double frac = at - floor_at;
    const int i0 = (static_cast<int>(floor_at) + line_size * 4) % line_size;
    const int i1 = (i0 + 1) % line_size;
    const float out = static_cast<float>(s.line[static_cast<std::size_t>(i0)] * (1 - frac) + s.line[static_cast<std::size_t>(i1)] * frac);
    const float filtered = s.loss * (s.bright * out + (1 - s.bright) * s.last);
    s.last = out;
    s.line[static_cast<std::size_t>(s.write)] = filtered;
    s.write = (s.write + 1) % line_size;
    return out;
}

bool BanjoVoice::tone_in(int midi, const Chord& chord) const {
    const int root = (key_ + chord.root) % 12;
    const int rel = ((midi - 55 - root) % 12 + 12) % 12;
    if (rel == 0 || rel == 7)
        return true;
    if (rel == (chord.kind == 1 ? 3 : 4))
        return true;
    return chord.kind == 2 && rel == 10;
}

// The note a string gives for this chord with the hand at `pos`: open if open rings in
// the chord and the hand is low, otherwise the first chord tone under the hand.
int BanjoVoice::voice_on(int string, const Chord& chord, int pos) const {
    const int open = banjo_open[string - 1];
    if (string == 5)
        return open + (key_ == 7 ? 2 : 0);
    if (pos <= 2 && tone_in(open, chord))
        return open;
    for (int fret = std::max(0, pos); fret <= pos + 4; ++fret)
        if (tone_in(open + fret, chord))
            return open + fret;
    return open + pos;
}

// The next melody note: a step or a skip from the last, a chord tone on a strong beat.
int BanjoVoice::scale_degree_near(int midi, int dir, const Chord& chord, bool strong) {
    const int scale[7] = {0, 2, 4, 5, 7, 9, 11};
    int best = midi;
    double best_score = -1e9;
    for (int cand = 57; cand <= 76; ++cand) {
        const int rel = ((cand - 55 - key_) % 12 + 12) % 12;
        bool in_scale = false;
        for (int d : scale)
            in_scale = in_scale || rel == d;
        // the flat seventh, for the old-time tunes
        in_scale = in_scale || (chord.root == 10 && rel == 10);
        if (!in_scale)
            continue;
        const int step = cand - midi;
        double score = -std::abs(step - dir * 2) * 0.6 - (step == 0 ? 1.5 : 0.0);
        if (tone_in(cand, chord))
            score += strong ? 3.0 : 0.8;
        else if (strong)
            score -= 2.0;
        score += uniform() * 1.6;
        if (cand > 72)
            score -= (cand - 72) * 0.5;
        if (score > best_score) {
            best_score = score;
            best = cand;
        }
    }
    return best;
}

void BanjoVoice::plan_act() {
    int next = act_index_;
    for (int tries = 0; tries < 12; ++tries) {
        next = pick(act_count);
        if (next != act_index_ && acts[next].style != last_style_)
            break;
    }
    // Most acts open in G; now and then the band turns to C or D for one.
    const double roll = uniform();
    const int old_key = key_;
    key_ = acts_played_ < 2 ? 0 : roll < 0.62 ? 0 : roll < 0.82 ? 5 : 7;
    if (key_ != old_key)
        hand_ = 0;
    act_index_ = next;
    act_ = acts[next];
    // a little life in each telling
    act_.tempo += (uniform() * 2 - 1) * 4;
    last_style_ = act_.style;
    bar_ = 0;
    ++acts_played_;
    for (int k = 0; k < 8; ++k)
        roll_[k] = rolls[pick(7)][k];
}

void BanjoVoice::step_eighth() {
    const Chord chord = act_.bars[bar_];
    const bool last_bar = bar_ == act_.length - 1;
    const bool penult = bar_ == act_.length - 2;
    const int e = eighth_;
    // New rolls every couple of bars, from the act's style.
    if (e == 0 && (bar_ % 2 == 0 || uniform() < 0.25)) {
        int which = pick(7);
        if (act_.style == 0)
            which = pick(3) == 0 ? 4 : pick(2) == 0 ? 0 : 6;
        else if (act_.style == 3)
            which = pick(2) == 0 ? 2 : 5;
        for (int k = 0; k < 8; ++k)
            roll_[k] = rolls[which][k];
    }
    if (e == 0) {
        // where the hand sits for this chord
        const int pc = (key_ + chord.root) % 12;
        hand_ = (pc == 0 || pc == 7 || pc == 4 || pc == 9) ? (uniform() < 0.25 && act_.style != 3 ? 5 : 0) : 2;
        if (act_.style == 2 && uniform() < 0.4)
            hand_ = 5;
        // a lick to close the act, or now and then mid-act
        lick_ = -1;
        if ((penult && act_.style != 3) || (uniform() < 0.12 && act_.style < 2 && !last_bar)) {
            lick_ = 0;
            lick_kind_ = uniform() < 0.65 ? 0 : 1;
        }
    }
    const double accent = 0.75 + 0.15 * uniform();
    // The banjo.
    bool played = false;
    if (lick_ >= 0 && lick_ < 8) {
        const int(*lick)[3] = lick_kind_ == 0 ? g_lick : run_lick;
        const int string = lick[lick_][0];
        const int shift = string == 5 ? 0 : key_ % 12;
        const int note = (string == 5 ? voice_on(5, chord, 0) : banjo_open[string - 1] + lick[lick_][1] + shift);
        String& s = banjo_[string - 1];
        if (lick[lick_][2] == 1 && lick_ > 0 && lick[lick_ - 1][0] == string) {
            slide_to(s, note, 0.012);
            // the hammer itself makes a little sound of its own
            const int n = static_cast<int>(s.delay);
            const int at = (s.write - n / 2 + line_size) % line_size;
            s.line[static_cast<std::size_t>(at)] += 0.25F;
        } else if (lick[lick_][2] == 2) {
            pluck(s, note - 1, 0.85 * accent, 0.6);
            slide_to(s, note, 0.07);
        } else {
            pluck(s, note, 0.9 * accent, 0.62);
        }
        played = true;
        ++lick_;
    }
    if (!played) {
        int string = roll_[e];
        const bool sparse = act_.style == 3;
        // The porch vamp leaves holes; the others fill every eighth.
        if (sparse && (e % 2 == 1) && uniform() < 0.55)
            string = 0;
        if (string != 0) {
            const bool melody_beat = e == 0 || e == 3 || e == 6 || (act_.style == 2 && e % 2 == 0);
            int note = voice_on(string, chord, hand_);
            double strength = 0.62 * accent;
            int technique = 0;
            if (melody_beat && string != 5) {
                const int dir = melody_ > 68 ? -1 : melody_ < 61 ? 1 : (uniform() < 0.5 ? -1 : 1);
                const int want = scale_degree_near(melody_, dir, chord, e == 0 || e == 6);
                // Find a string that can fret it under a reasonable hand.
                int best_string = string;
                int best_fret = 99;
                for (int k = 1; k <= 4; ++k) {
                    const int fret = want - banjo_open[k - 1];
                    if (fret < 0 || fret > 12)
                        continue;
                    const int cost = std::abs(fret - hand_) + (k == string ? 0 : 2);
                    if (cost < best_fret) {
                        best_fret = cost;
                        best_string = k;
                    }
                }
                if (best_fret < 99) {
                    string = best_string;
                    note = want;
                    melody_ = want;
                    strength = 0.95 * accent;
                    // Ornaments: a hammer-on from a step below, or a slide into it.
                    const double o = uniform();
                    if (note - banjo_open[string - 1] >= 2 && o < 0.18)
                        technique = 1;
                    else if (note - banjo_open[string - 1] >= 2 && o < 0.3)
                        technique = 2;
                }
            }
            String& s = banjo_[string - 1];
            if (technique == 1) {
                pluck(s, note - 2, strength, 0.6);
                slide_to(s, note, 0.004);
                s.glide = 0;  // the hammer lands half an eighth later
                s.delay_to = std::clamp(sample_rate / hz_of(note) - 0.5, 4.0, line_size - 8.0);
                s.mute_in = -2;  // marks a pending hammer
            } else if (technique == 2) {
                pluck(s, note - 2, strength, 0.6);
                slide_to(s, note, 0.06);
            } else {
                pluck(s, note, string == 5 ? strength * 0.7 : strength, string == 5 ? 0.7 : 0.6);
            }
        }
    }
    // The bass: two-beat or walking; walking up into the next act.
    if (act_.bass != 0) {
        const int pc = (key_ + chord.root) % 12;
        int root = 43 + pc;
        while (root > 50)
            root -= 12;
        const int fifth = root + 7 > 52 ? root - 5 : root + 7;
        int note = -1;
        if (act_.bass == 1 || act_.style == 3) {
            if (e == 0)
                note = root;
            else if (e == 4)
                note = fifth;
        } else if (e % 2 == 0) {
            const int third = root + (chord.kind == 1 ? 3 : 4);
            const int line[4] = {root, third, fifth > root ? fifth : root + 7, root + 5};
            note = line[e / 2];
        }
        if (last_bar && e % 2 == 0) {
            // walk up to where the next act starts (it starts on its I)
            int target = 43;
            while (target < root)
                target += 12;
            const int walk[4] = {root, root + 2, root + 4, target - 1};
            note = std::min(walk[e / 2], 52);
        }
        if (note > 0) {
            bass_.loss = static_cast<float>(std::pow(10.0, -3.0 / (1.6 * hz_of(note))));
            bass_.bright = 0.18F;
            pluck(bass_, note, 0.9, 0.12);
            bass_.mute_in = 60.0 / tempo_ * (act_.bass == 2 ? 0.45 : 0.85);
        }
    }
    // The guitar: a chop on the off-beats.
    if (act_.guitar != 0 && (e == 2 || e == 6)) {
        const int pc = (key_ + chord.root) % 12;
        for (int k = 0; k < 6; ++k) {
            int note = guitar_open[k];
            for (int fret = 0; fret <= 4; ++fret) {
                if (tone_in(guitar_open[k] + fret, chord)) {
                    note = guitar_open[k] + fret;
                    break;
                }
            }
            // the bass strings below the root are left out
            if (k < 2 && ((note - 43 - pc) % 12 + 12) % 12 != 0)
                continue;
            String& s = guitar_[k];
            s.loss = static_cast<float>(std::pow(10.0, -3.0 / (1.2 * hz_of(note))));
            s.bright = 0.45F;
            pluck(s, note, 0.32 + 0.05 * k, 0.4);
            s.mute_in = 0.09 + 0.01 * k;
        }
    }
    // On through the bar, and the act.
    eighth_ = (eighth_ + 1) % 8;
    if (eighth_ == 0) {
        ++bar_;
        if (bar_ >= act_.length)
            plan_act();
    }
}

void BanjoVoice::render_add(std::span<float> stereo, double gain) {
    const double dt = 1.0 / sample_rate;
    for (std::size_t frame = 0; frame + 1 < stereo.size(); frame += 2) {
        gain_ += (gain - gain_) * 0.00005;
        clock_ -= dt;
        if (clock_ <= 0) {
            tempo_ += (act_.tempo - tempo_) * 0.06;
            loud_ += (act_.loud - loud_) * 0.05;
            // a light swing: the on-beat eighth a little longer than the off-beat
            const double beat = 60.0 / tempo_;
            clock_ += beat * (eighth_ % 2 == 0 ? 0.53 : 0.47);
            step_eighth();
        }
        if (gain_ < 1e-5 && gain < 1e-5)
            continue;
        double banjo = 0;
        double left = 0;
        double right = 0;
        for (int k = 0; k < 5; ++k) {
            String& s = banjo_[k];
            if (s.mute_in == -2 && clock_ < 60.0 / tempo_ * 0.24) {
                s.glide = 1.0 / (0.004 * sample_rate);
                s.mute_in = -1;
                const int n = static_cast<int>(s.delay);
                const int at = (s.write - n / 2 + line_size) % line_size;
                s.line[static_cast<std::size_t>(at)] += 0.2F;
            }
            banjo += run(s);
        }
        // strings through the head
        double head = 0;
        for (int k = 0; k < 3; ++k) {
            const double y = head_g_[k] * banjo + head_a1_[k] * head_y1_[k] + head_a2_[k] * head_y2_[k];
            head_y2_[k] = head_y1_[k];
            head_y1_[k] = y;
            head += y;
        }
        const double banjo_out = (banjo * 0.35 + head * 2.2) * 0.55;
        left += banjo_out * 0.8;
        right += banjo_out * 1.0;
        // bass and guitar, damped when their time is up
        if (bass_.mute_in > 0) {
            bass_.mute_in -= dt;
            if (bass_.mute_in <= 0)
                bass_.loss = 0.985F;
        }
        const double b = run(bass_) * 1.5;
        left += b;
        right += b;
        double g = 0;
        for (String& s : guitar_) {
            if (s.mute_in > 0) {
                s.mute_in -= dt;
                if (s.mute_in <= 0)
                    s.loss = 0.93F;
            }
            g += run(s);
        }
        left += g * 0.42;
        right += g * 0.26;
        // a soft top and no DC
        lp_left_ += (left - lp_left_) * 0.55;
        lp_right_ += (right - lp_right_) * 0.55;
        dc_left_ += (lp_left_ - dc_left_) * 0.0015;
        dc_right_ += (lp_right_ - dc_right_) * 0.0015;
        const double scale = 0.5 * loud_ * gain_;
        stereo[frame] += static_cast<float>(std::tanh((lp_left_ - dc_left_) * scale * 1.5) / 1.5);
        stereo[frame + 1] += static_cast<float>(std::tanh((lp_right_ - dc_right_) * scale * 1.5) / 1.5);
    }
}

} // namespace mm
