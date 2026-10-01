#pragma once
#include "pcm_player.hpp"
#include <string_view>

namespace games {
struct FourPegsTrack final {
    std::string_view name{};
    std::uint64_t bar_frames{}, loop_frames{};
};
FourPegsTrack fourpegs_track(std::string_view name);

// Game policy only. Shared transport owns the sample clock and bar transition;
// PcmPlayer owns the common device and bounded effect voices. UI thread only.
class FourPegsAudio final {
public:
    explicit FourPegsAudio(bool offline = false) : player_(offline) {}
    ~FourPegsAudio();
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
    PcmPlayer player_{}; // Declared first: outlives the transport attached to it.
    gui_forms::AudioLoopTransport transport_{};
    FourPegsTrack wanted_{};
    std::future<gui_forms::AudioClipResult> loading_{};
    std::stop_source cancellation_{};
    std::shared_ptr<const gui_forms::AudioClip> loaded_{};
    std::uint64_t request_id_{}, cancel_id_{};
    std::size_t next_effect_{};
    bool ready_{}, enabled_{}, paused_{}, submitted_{}, load_failed_{};
    bool foreground_{true}, master_music_{true}, master_sound_{true};
    bool playback_failed_{};
    double duck_{}, sent_gain_{-1};
    void discard_load();
    void close_music();
    bool accept(gui_forms::AudioLoopCommand command, const char* operation);
};
}
