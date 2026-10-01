#pragma once
#include "gui_forms/audio/audio.hpp"
#include <array>
#include <filesystem>
#include <future>
#include <string>

namespace games {
// Game-owned scheduling only; PCM validation, mixing and device output belong
// to GUI.Forms. File reads run on one bounded background loader.
class PcmPlayer final {
public:
    static constexpr std::size_t slot_count = 32;
    void start(std::size_t slot, const std::string& name, bool loop, double gain, double rate = 1);
    void gain(std::size_t slot, double value);
    void pause(std::size_t slot);
    void resume(std::size_t slot);
    void clear(std::size_t slot);
    void tick();
    void shutdown();
    bool playing(std::size_t slot) const;
private:
    struct Slot final {
        std::string name{};
        std::shared_ptr<const gui_forms::AudioClip> clip{};
        std::future<gui_forms::AudioClipResult> pending{};
        gui_forms::AudioVoice voice{};
        bool loop{}, paused{};
        double gain{1}, rate{1};
    };
    gui_forms::AudioEngine engine_{};
    std::array<Slot, slot_count> slots_{};
    bool attempted_{};
    void create_voice(Slot& slot);
    static void report(gui_forms::AudioStatus status, const std::string& operation);
};
}
