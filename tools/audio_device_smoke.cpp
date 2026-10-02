#include "fourpegs_audio.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
}
// An explicitly invoked, audible check. Never register this as an automatic test.
int main(int argc, char** argv) {
    try {
        require(argc == 2, "usage: audio_device_smoke <prepared-asset-root>");
#ifdef _WIN32
        const int environment = _putenv_s("GAMES_ASSET_DIR", argv[1]);
#else
        const int environment = setenv("GAMES_ASSET_DIR", argv[1], 1);
#endif
        require(environment == 0, "asset environment");
        games::FourPegsAudio audio{};
        audio.music("fp_music_t1", true);
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point previous = start;
        unsigned stage{};
        std::uint64_t last_applied{};
        unsigned applied_count{};
        for (;;) {
            const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - start).count();
            const double dt = std::chrono::duration<double>(now - previous).count();
            previous = now;
            if (elapsed >= 14) { break; }
            if (stage == 0 && elapsed >= 3) { audio.music("fp_music_t2", true); stage = 1; }
            if (stage == 1 && elapsed >= 6) { audio.music("fp_music_t3", true); stage = 2; }
            if (stage == 2 && elapsed >= 9) { audio.duck(1); stage = 3; }
            if (stage == 3 && elapsed >= 11) { audio.cabinet(true, false, false); stage = 4; }
            if (stage == 4 && elapsed >= 12) {
                audio.cabinet(true, true, true);
                audio.music("fp_music_t3", true);
                stage = 5;
            }
            audio.tick(dt);
            require(audio.status() == gui_forms::AudioStatus::ok, "native device unavailable or stopped");
            const gui_forms::AudioLoopReceipt receipt = audio.music_receipt();
            if (receipt.phase == gui_forms::AudioLoopPhase::applied && receipt.id != last_applied) {
                require(receipt.status == gui_forms::AudioLoopStatus::ok, "applied music receipt failed");
                last_applied = receipt.id;
                ++applied_count;
                std::cout << "Music applied: request " << receipt.id << ", admitted " << receipt.admission_frame
                          << ", applied " << receipt.application_frame << '\n';
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        audio.cabinet(false, true, true);
        require(audio.status() == gui_forms::AudioStatus::closed && !audio.pending(), "device and pending reads close");
        require(applied_count == 3, "all three score tiers reached the native render callback");
        std::cout << "Native device processed all tiers, duck/mute/resume controls and teardown. Listening needs a human check.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
