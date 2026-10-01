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
        require(argc == 3, "Pass the prepared runtime asset directory and wav or ogg");
        const std::string format = argv[2];
        require(format == "wav" || format == "ogg", "Supported fixture format");
        const std::filesystem::path audio = std::filesystem::path(argv[1]) / "audio";
        const std::array<const char*, 20> names{
            "music_menu_loop", "music_klondike_loop", "music_spider_loop", "music_freecell_loop",
            "music_hearts_loop", "music_puzzle_main_loop", "music_puzzle_tiptoe_loop", "music_gems_loop",
            "music_sudoku_day_loop", "music_sudoku_night_loop", "music_nature_cube_loop", "music_untangle_loop",
            "music_atom_probe_loop", "music_four_pegs_loop", "music_switchbox_loop", "music_puzzle_solve_loop",
            "music_sticks_stones_loop", "fp_music_t1", "fp_music_t2", "fp_music_t3"};
        const std::array<std::uint64_t, 20> frames{
            4388608, 6582784, 5857680, 6912000, 4838400, 4538144, 4369728, 5076656,
            4850560, 5421184, 5236320, 5632000, 5509504, 5172288, 4680000, 4969360, 5068800,
            3177931, 2880000, 2560000};
        for (std::size_t i = 0; i < names.size(); ++i) {
            const std::string name = std::string(names[i]) + "." + format;
            gui_forms::AudioClipResult clip = format == "ogg" ? gui_forms::AudioClip::load_ogg(audio / name) : gui_forms::AudioClip::load_wav(audio / name);
            if (clip.status != gui_forms::AudioStatus::ok) { std::cerr << name << ": status " << static_cast<int>(clip.status) << '\n'; }
            require(clip.status == gui_forms::AudioStatus::ok, "Production loader opens loop");
            require((*clip.clip).frames() == frames[i], "Exact producer loop frame count");
            gui_forms::AudioEngine engine{};
            const gui_forms::AudioStatus open_status = engine.open(true);
            require(open_status == gui_forms::AudioStatus::ok, "Offline engine starts");
            gui_forms::AudioVoice voice{};
            const gui_forms::AudioStatus admit_status = engine.voice(clip.clip, true, voice);
            require(admit_status == gui_forms::AudioStatus::ok, "Loop voice");
            const gui_forms::AudioStatus play_status = voice.play();
            require(play_status == gui_forms::AudioStatus::ok, "Loop plays");
            std::array<float, 8192> output{};
            std::uint64_t position = 0;
            double maximum_error = 0;
            const std::span<const float> original = (*clip.clip).samples();
            while (position < frames[i] + 4096) {
                const gui_forms::AudioStatus render_status = engine.render(output);
                require(render_status == gui_forms::AudioStatus::ok, "Offline render");
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
            if (file.path().extension() != "." + format) { continue; }
            gui_forms::AudioClipResult clip = format == "ogg" ? gui_forms::AudioClip::load_ogg(file.path()) : gui_forms::AudioClip::load_wav(file.path());
            if (clip.status != gui_forms::AudioStatus::ok) {
                std::cerr << file.path().filename() << ": audio status " << static_cast<int>(clip.status) << '\n';
            }
            require(clip.status == gui_forms::AudioStatus::ok, "Full prepared audio decode");
            ++decoded;
        }
        require(decoded == 275, "Complete runtime batch including Switchbox and Four Pegs");
        std::cout << "Full audio decode: " << decoded << "/275 files.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
