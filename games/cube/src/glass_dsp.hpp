#pragma once
// Nature Cube's sound: the cues the game sends its music and effects, and the building
// blocks the effects share with the suite's music engine. Nothing here allocates once
// constructed.
#include "music_core.hpp"
#include "styles/glasshouse.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace ps_cube {

using games::music::pi;
using games::music::sample_rate;

// What just happened in the game. Posted from the interface thread, taken on the audio
// thread.
enum class Cue : int { none, pick, step, erase, connect, blocked, turn, level, win, portal };

// The cue for a sound name the puzzle view sends, or Cue::none when it is not one of the
// cube's: those still play from the shared sound files.
inline Cue cue_for(std::string_view name) {
    if (name == "nature_cube_pick")
        return Cue::pick;
    if (name == "nature_cube_trace")
        return Cue::step;
    if (name == "nature_cube_erase")
        return Cue::erase;
    if (name == "nature_cube_connect")
        return Cue::connect;
    if (name == "nature_cube_blocked")
        return Cue::blocked;
    if (name == "nature_cube_portal")
        return Cue::portal;
    if (name == "nature_cube_turn")
        return Cue::turn;
    if (name == "ui_new_game")
        return Cue::level;
    if (name == "stinger_win_nature_cube" || name == "stinger_topscore_nature_cube")
        return Cue::win;
    return Cue::none;
}

// A single-producer, single-consumer ring of cues. A full ring drops the newest cue.
class CueRing final {
  public:
    void push(Cue cue) noexcept {
        const unsigned head = head_.load(std::memory_order_relaxed);
        const unsigned next = (head + 1) % size;
        if (next == tail_.load(std::memory_order_acquire))
            return;
        cues_[head] = cue;
        head_.store(next, std::memory_order_release);
    }
    bool pop(Cue& cue) noexcept {
        const unsigned tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return false;
        cue = cues_[tail];
        tail_.store((tail + 1) % size, std::memory_order_release);
        return true;
    }

  private:
    static constexpr unsigned size = 64;
    std::array<Cue, size> cues_{};
    std::atomic<unsigned> head_{0};
    std::atomic<unsigned> tail_{0};
};

// The music is the suite's glasshouse (src/music/styles/glasshouse.hpp); the effects
// tune to the harmony it publishes and are built from the same blocks.
using games::music::approach;
using games::music::decay_for;
using games::music::Harmony;
using games::music::rate_for;
using games::music::Room;
using games::music::saw;
using games::music::SineTable;
using games::music::Svf;
using games::music::wrap;
inline double hz_of(double midi) { return games::music::mtof(midi); }

// A small deterministic generator, so a seed always plays the same piece.
class Dice final : public games::music::Random {
  public:
    explicit Dice(std::uint32_t seed) : Random(seed * 2654435761U + 0x6D2B79F5U) {}
};

// The cue the music answers (Composer::cue) for one of the game's, 0 for none.
inline int music_cue(Cue cue) {
    if (cue == Cue::connect) return games::music::Glasshouse::cue_join;
    if (cue == Cue::level) return games::music::Glasshouse::cue_level;
    if (cue == Cue::win) return games::music::Glasshouse::cue_win;
    return 0;
}

} // namespace ps_cube
