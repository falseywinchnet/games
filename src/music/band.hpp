#pragma once
// A band: the instruments a score plays, a queue of notes waiting for their moment, the
// voices sounding, and the mix (each instrument's level, its place left to right, how much
// of it goes to the room). A score derives from it, plans its music a bar at a time in
// plan(), and leaves the playing to render_add(). Notes land with a player's looseness: the
// drums close to the beat, the bass a touch behind, melodies a few milliseconds either way.
#include "instruments.hpp"
#include "music_core.hpp"

#include <array>
#include <span>

namespace games::music {

class Band : public Composer {
  public:
    void render_add(std::span<float> stereo, double gain) noexcept override;
    void fade_in(double seconds) noexcept override { fade_.start(seconds); }

    [[nodiscard]] long notes() const { return notes_; }

  protected:
    // `room`: the band's room (its size, tail and darkness).
    Band(std::uint32_t seed, const Room& room) : random_(seed), room_(room) {}
    // Plans more music: called whenever the clock reaches next_plan_ (the score moves it on).
    virtual void plan() noexcept = 0;
    // Queues a note `seconds` after the sample `origin` (a bar's start, say), loosened by the
    // player's hand.
    void note(long origin, double seconds, Instrument kind, double midi, double vel, double length_seconds = 0, double pan = 0) noexcept;
    // How an instrument sits in the mix.
    void mix(Instrument kind, double level, double room_send) noexcept {
        level_[static_cast<std::size_t>(kind)] = level;
        send_[static_cast<std::size_t>(kind)] = room_send;
    }

    Random random_;
    long clock_ = 0;        // samples rendered
    long next_plan_ = 0;    // the sample plan() is next due at

  private:
    void start(const Note& e) noexcept;
    Room room_;
    Fade fade_{};
    std::array<Voice, 64> voices_{};
    std::array<Note, 512> queue_{};
    int queued_ = 0;
    long next_due_ = 0;
    long notes_ = 0;
    std::array<double, instrument_count> level_{};
    std::array<double, instrument_count> send_{};
};

}  // namespace games::music
