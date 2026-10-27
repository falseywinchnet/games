#include "band.hpp"

#include <limits>

namespace games::music {

void Band::note(long origin, double seconds, Instrument kind, double midi, double vel, double length_seconds, double pan) noexcept {
    if (queued_ >= static_cast<int>(queue_.size())) return;
    Note& e = queue_[static_cast<std::size_t>(queued_++)];
    // a player's hands, not a clock: the drums close to the beat, the bass sitting back a
    // touch, the comping later still, melodies a few milliseconds either way
    double slop = 0;
    switch (kind) {
    case Instrument::kick: case Instrument::rim: case Instrument::hat: case Instrument::shaker:
    case Instrument::brush_sweep: case Instrument::brush_tap: case Instrument::snare: case Instrument::ride: case Instrument::clave:
    case Instrument::cowbell: case Instrument::guiro: case Instrument::timbale: case Instrument::clap:
    case Instrument::open_hat: case Instrument::crash: case Instrument::tom: slop = (random_.uniform() - .5) * .008; break;
    case Instrument::bass_marimba: case Instrument::upright: case Instrument::electric_bass: case Instrument::synth_bass: slop = .006 + (random_.uniform() - .5) * .012; break;
    case Instrument::comp_marimba: case Instrument::guitar: case Instrument::ukulele: slop = .006 + (random_.uniform() - .5) * .016; break;
    default: slop = (random_.uniform() - .5) * .022; break;
    }
    e.at = origin + std::max(0L, static_cast<long>((seconds + slop) * sample_rate));
    next_due_ = std::min(next_due_, e.at);
    e.kind = kind;
    e.midi = midi;
    e.vel = vel * (.9 + .2 * random_.uniform());
    e.length = length_seconds;
    e.pan = pan;
}

void Band::start(const Note& e) noexcept {
    // a free voice, or the oldest
    Voice* chosen = nullptr;
    for (Voice& v : voices_)
        if (!v.on) { chosen = &v; break; }
    if (!chosen)
        for (Voice& v : voices_)
            if (!chosen || v.age > (*chosen).age) chosen = &v;
    Voice& v = *chosen;
    v = Voice{};
    v.on = true;
    v.kind = e.kind;
    v.hz = mtof(e.midi);
    v.vel = e.vel;
    v.length = e.length;
    v.pan = e.pan;
    ++notes_;
}

void Band::render_add(std::span<float> stereo, double gain) noexcept {
    const double dt = 1.0 / sample_rate;
    for (std::size_t i = 0; i + 1 < stereo.size(); i += 2) {
        while (clock_ >= next_plan_) plan();
        if (clock_ >= next_due_) {
            // start what is due, keep the rest, and note when the next is due
            int kept = 0;
            // Nothing queued is due until a note says so. long is 32 bits on Windows, so the
            // sentinel is the type's own maximum, not a shifted constant.
            next_due_ = std::numeric_limits<long>::max();
            for (int k = 0; k < queued_; ++k) {
                Note& e = queue_[static_cast<std::size_t>(k)];
                if (e.at <= clock_) start(e);
                else {
                    next_due_ = std::min(next_due_, e.at);
                    queue_[static_cast<std::size_t>(kept++)] = e;
                }
            }
            queued_ = kept;
        }
        double dry_l = 0, dry_r = 0, wet_l = 0, wet_r = 0;
        for (Voice& v : voices_) {
            if (!v.on) continue;
            const std::size_t k = static_cast<std::size_t>(v.kind);
            const double s = play(v, random_, dt) * (solo_ < 0 || solo_ == static_cast<int>(k) ? level_[k] : 0.0);
            double gl, gr;
            pan_gains(v.pan, gl, gr);
            dry_l += s * gl;
            dry_r += s * gr;
            const double w = s * send_[k];
            wet_l += w * gl;
            wet_r += w * gr;
        }
        double room_l = 0, room_r = 0;
        room_.process(wet_l * .3, wet_r * .3, room_l, room_r);
        const double g = gain * fade_.next();
        stereo[i] += static_cast<float>(std::tanh((dry_l + room_l * .5) * .8) * g);
        stereo[i + 1] += static_cast<float>(std::tanh((dry_r + room_r * .5) * .8) * g);
        ++clock_;
    }
}

}  // namespace games::music
