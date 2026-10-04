#include "gui_forms/audio/audio.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <string>

static void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Pass the prepared runtime asset directory and wav or ogg");
        const std::string format = argv[2];
        require(format == "wav" || format == "ogg", "Supported fixture format");
        const std::filesystem::path audio = std::filesystem::path(argv[1]) / "audio";
        std::ifstream inventory(audio / "verification.tsv");
        unsigned expected = 0;
        require(bool(inventory >> expected) && expected > 0, "Verified audio inventory count");
        std::vector<std::string> names;
        std::vector<std::uint64_t> frames;
        std::string stem;
        std::uint64_t length = 0;
        while (inventory >> stem >> length) {
            require(length > 0, "Positive producer loop length");
            names.push_back(stem);
            frames.push_back(length);
        }
        require(inventory.eof() && !names.empty(), "Complete producer loop inventory");
        for (std::size_t i = 0; i < names.size(); ++i) {
            const std::string name = std::string(names[i]) + "." + format;
            gui_forms::AudioClipResult clip = format == "ogg"
                                                  ? gui_forms::AudioClip::load_ogg(audio / name)
                                                  : gui_forms::AudioClip::load_wav(audio / name);
            if (clip.status != gui_forms::AudioStatus::ok) {
                std::cerr << name << ": status " << static_cast<int>(clip.status) << '\n';
            }
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
                        const std::size_t source = static_cast<std::size_t>(
                            ((position + sample / 2) % frames[i]) * 2 + sample % 2);
                        maximum_error = std::max(
                            maximum_error, std::abs(double(output[sample]) - original[source]));
                    }
                }
                position += output.size() / 2;
            }
            require(maximum_error < 0.00001, "Exact playback across loop seam");
            std::cout << name << ": " << frames[i] << " frames, seam error " << maximum_error
                      << '\n';
        }
        unsigned decoded = 0;
        for (const std::filesystem::directory_entry& file :
             std::filesystem::directory_iterator(audio)) {
            if (file.path().extension() != "." + format) {
                continue;
            }
            gui_forms::AudioClipResult clip = format == "ogg"
                                                  ? gui_forms::AudioClip::load_ogg(file.path())
                                                  : gui_forms::AudioClip::load_wav(file.path());
            if (clip.status != gui_forms::AudioStatus::ok) {
                std::cerr << file.path().filename() << ": audio status "
                          << static_cast<int>(clip.status) << '\n';
            }
            require(clip.status == gui_forms::AudioStatus::ok, "Full prepared audio decode");
            ++decoded;
        }
        require(decoded == expected, "Complete verified runtime audio batch");
        std::cout << "Full audio decode: " << decoded << "/" << expected << " files.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
