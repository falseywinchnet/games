#pragma once
#include "pcm_player.hpp"
#include <span>
#include <string_view>

namespace games {
// One of a game's music loops, cut on its bar lines: the bar's and the loop's length in
// frames at 48 kHz, from the game's audio manifest (the runtime files are trimmed to them by
// prepare_portable_assets.py).
struct BarTrack final {
    std::string_view name{};
    std::uint64_t bar_frames{}, loop_frames{};
};

// Music that changes on a bar line, and the game's effects beside it, for any game: Four
// Pegs and Liar's Dice. The game names its tracks (kept for the life of the player) and asks
// for one by name; the change lands on the next bar with a short fade. Game policy only:
// the shared transport owns the sample clock and the bar transition, PcmPlayer the device
// and bounded effect voices. UI thread only.
class BarMusic final {
public:
    explicit BarMusic(std::span<const BarTrack> tracks, bool offline = false)
        : tracks_(tracks), player_(offline) {}
    ~BarMusic();
    void music(const std::string& name, bool enabled);
    void effects(const std::string& name, double gain, double rate, bool enabled);
    void duck(double amount);
    void cabinet(bool foreground, bool music, bool sound);
    void tick(double dt);
    void stop();
    bool pending() const;
    gui_forms::AudioStatus status() const;
    gui_forms::AudioLoopReceipt music_receipt();
    gui_forms::AudioStatus render(std::span<float> samples);
private:
    std::span<const BarTrack> tracks_;
    PcmPlayer player_{}; // Declared first: outlives the transport attached to it.
    gui_forms::AudioLoopTransport transport_{};
    BarTrack wanted_{};
    std::future<gui_forms::AudioClipResult> loading_{};
    std::shared_ptr<gui_forms::CancellationFlag> cancellation_{};
    std::shared_ptr<const gui_forms::AudioClip> loaded_{};
    std::uint64_t request_id_{}, cancel_id_{};
    std::size_t next_effect_{};
    bool ready_{}, enabled_{}, paused_{}, submitted_{}, load_failed_{};
    bool foreground_{true}, master_music_{true}, master_sound_{true};
    bool playback_failed_{};
    double duck_{}, sent_gain_{-1};
    [[nodiscard]] BarTrack track(std::string_view name) const;
    void discard_load();
    void close_music();
    bool accept(gui_forms::AudioLoopCommand command, const char* operation);
};
}
