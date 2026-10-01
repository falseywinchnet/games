#include "gui_forms/audio/audio.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

static void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Pass the prepared runtime asset directory");
        const std::filesystem::path audio = std::filesystem::path(argv[1]) / "audio";
        const std::array<const char*, 17> names{
            "menu", "klondike", "spider", "freecell", "hearts", "puzzle_main", "puzzle_tiptoe",
            "gems", "sudoku_day", "sudoku_night", "nature_cube", "untangle", "atom_probe",
            "four_pegs", "switchbox", "puzzle_solve", "sticks_stones"};
        const std::array<std::uint64_t, 17> frames{
            4388608, 6582784, 5857680, 6912000, 4838400, 4538144, 4369728, 5076656,
            4850560, 5421184, 5236320, 5632000, 5509504, 5172288, 4680000, 4969360, 5068800};
        for (std::size_t i = 0; i < names.size(); ++i) {
            const std::string name = std::string("music_") + names[i] + "_loop.wav";
            gui_forms::AudioClipResult clip = gui_forms::AudioClip::load_wav(audio / name);
            require(clip.status == gui_forms::AudioStatus::ok, "Production loader opens loop");
            require((*clip.clip).frames() == frames[i], "Exact producer loop frame count");
            gui_forms::AudioEngine engine;
            require(engine.open(true) == gui_forms::AudioStatus::ok, "Offline engine starts");
            gui_forms::AudioVoice voice;
            require(engine.voice(clip.clip, true, voice) == gui_forms::AudioStatus::ok, "Loop voice");
            require(voice.play() == gui_forms::AudioStatus::ok, "Loop plays");
            std::array<float, 8192> output{};
            std::uint64_t position = 0;
            double maximum_error = 0;
            const std::span<const float> original = (*clip.clip).samples();
            while (position < frames[i] + 4096) {
                require(engine.render(output) == gui_forms::AudioStatus::ok, "Offline render");
                for (std::size_t sample = 0; sample < output.size(); ++sample) {
                    if (position + sample / 2 + 128 >= frames[i]) {
                        const std::size_t source = static_cast<std::size_t>(((position + sample / 2) % frames[i]) * 2 + sample % 2);
                        maximum_error = std::max(maximum_error, std::abs(double(output[sample]) - original[source]));
                    }
                }
                position += output.size() / 2;
            }
            require(maximum_error < 0.00001, "Exact playback across loop seam");
            std::cout << name << ": " << frames[i] << " frames, seam error " << maximum_error << '\n';
        }
        unsigned decoded = 0;
        for (const std::filesystem::directory_entry& file : std::filesystem::directory_iterator(audio)) {
            if (file.path().extension() != ".wav") { continue; }
            gui_forms::AudioClipResult clip = gui_forms::AudioClip::load_wav(file.path());
            if (clip.status != gui_forms::AudioStatus::ok) {
                std::cerr << file.path().filename() << ": audio status " << static_cast<int>(clip.status) << '\n';
            }
            require(clip.status == gui_forms::AudioStatus::ok, "Full prepared PCM decode");
            ++decoded;
        }
        require(decoded == 243, "Complete runtime batch including Switchbox");
        std::cout << "Full PCM decode: " << decoded << "/243 files.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
