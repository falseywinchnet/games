// Renders the game's mower sounds from the live voice (src/mower_voice.*) as 48 kHz
// stereo WAV masters. The mower in the game is always at mowing speed with its
// blades turning, so its running sound is one long loop; starting and stopping are
// one-shots that hand over to and from that loop.
//
//   mm_mower_loops <directory>
//     mm_mower_run.wav    20 s of mowing; the end is folded into the start, so it loops
//     mm_mower_start.wav  key on, straight up to mowing speed, blades in, then running
//     mm_mower_stop.wav   running, key off, everything spinning down to silence
//     mm_chipper.wav      a second of the deck chewing through a flower bed, on its own
//     mm_granny_*.wav     the old lady's four lines: shout, scold, shriek, wail
//
// It prints the loop's length in samples for the audio manifest.
#include "chipper_voice.hpp"
#include "granny_voice.hpp"
#include "mower_voice.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr int rate = mm::MowerVoice::sample_rate;
constexpr int frame = rate / 60;
constexpr double pi = 3.14159265358979323846;

void put(std::FILE* file, std::uint32_t value, int bytes) {
    for (int index = 0; index < bytes; ++index)
        std::fputc(static_cast<int>((value >> (8 * index)) & 0xFF), file);
}

bool write_wav(const std::string& path, const std::vector<float>& sound) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr)
        return false;
    const std::uint32_t bytes = static_cast<std::uint32_t>(sound.size() * 2);
    std::fputs("RIFF", file);
    put(file, 36 + bytes, 4);
    std::fputs("WAVEfmt ", file);
    put(file, 16, 4);
    put(file, 1, 2);
    put(file, 2, 2);
    put(file, rate, 4);
    put(file, rate * 4, 4);
    put(file, 4, 2);
    put(file, 16, 2);
    std::fputs("data", file);
    put(file, bytes, 4);
    for (const float sample : sound)
        put(file, static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(sample * 32767.0F))) & 0xFFFF, 2);
    std::fclose(file);
    return true;
}

// The grass the deck meets while mowing: a steady cut with a slow swell, repeating with the loop.
double grass(double seconds, double period) {
    return 0.46 + 0.10 * std::sin(2 * pi * seconds / period * 3) + 0.05 * std::sin(2 * pi * seconds / period * 7 + 1.3);
}

// Runs the voice for `seconds`, appending what it plays.
void run(mm::MowerVoice& voice, std::vector<float>& sound, double from, double seconds, bool ignition, bool blades, double period, bool keep) {
    std::vector<float> block(static_cast<std::size_t>(frame) * 2);
    const int frames = static_cast<int>(std::lround(seconds * 60));
    for (int index = 0; index < frames; ++index) {
        mm::MowerControls controls{};
        controls.ignition = ignition;
        controls.governed_rpm = 3600;
        controls.blades = blades;
        controls.load = blades ? grass(from + index / 60.0, period) : 0;
        voice.control(controls);
        voice.render(block);
        if (keep)
            sound.insert(sound.end(), block.begin(), block.end());
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: mm_mower_loops <directory>\n");
        return 2;
    }
    const std::string directory = argv[1];
    constexpr double loop_seconds = 20;
    constexpr double fold_seconds = 1.0;
    {
        // Mowing: settle first, then play a loop and a little more; the extra is folded over the start.
        mm::MowerVoice voice(1);
        std::vector<float> sound;
        run(voice, sound, 0, 3, true, false, loop_seconds, false);
        run(voice, sound, 0, 9, true, true, loop_seconds, false);
        run(voice, sound, 0, loop_seconds + fold_seconds, true, true, loop_seconds, true);
        const std::size_t loop = static_cast<std::size_t>(loop_seconds * rate), fold = static_cast<std::size_t>(fold_seconds * rate);
        for (std::size_t index = 0; index < fold; ++index) {
            // equal power: the engine's roar is noise-like enough that amplitudes do not simply add
            const double t = (index + 0.5) / static_cast<double>(fold);
            const float in = static_cast<float>(std::sin(t * pi / 2)), out = static_cast<float>(std::cos(t * pi / 2));
            for (std::size_t channel = 0; channel < 2; ++channel)
                sound[index * 2 + channel] = sound[index * 2 + channel] * in + sound[(loop + index) * 2 + channel] * out;
        }
        sound.resize(loop * 2);
        if (!write_wav(directory + "/mm_mower_run.wav", sound))
            return 1;
        std::printf("mm_mower_run %zu samples\n", loop);
    }
    {
        // Starting: the key, straight up to speed, the blades, then two seconds of mowing to hand over in.
        mm::MowerVoice voice(2);
        std::vector<float> sound;
        run(voice, sound, 0, 1.9, true, false, loop_seconds, true);
        run(voice, sound, 0, 3.6, true, true, loop_seconds, true);
        const std::size_t fade = static_cast<std::size_t>(0.9 * rate), frames = sound.size() / 2;
        for (std::size_t index = 0; index < fade; ++index) {
            const float gain = static_cast<float>(std::cos((index + 0.5) / static_cast<double>(fade) * pi / 2));
            sound[(frames - fade + index) * 2] *= gain;
            sound[(frames - fade + index) * 2 + 1] *= gain;
        }
        if (!write_wav(directory + "/mm_mower_start.wav", sound))
            return 1;
        std::printf("mm_mower_start %.2f s\n", frames / static_cast<double>(rate));
    }
    {
        // Stopping: half a second of mowing to hand over from, then the key off until nothing rings.
        mm::MowerVoice voice(3);
        std::vector<float> sound;
        run(voice, sound, 0, 3, true, false, loop_seconds, false);
        run(voice, sound, 0, 8, true, true, loop_seconds, false);
        run(voice, sound, 0, 0.5, true, true, loop_seconds, true);
        const std::size_t fade = static_cast<std::size_t>(0.35 * rate);
        for (std::size_t index = 0; index < fade; ++index) {
            const float gain = static_cast<float>(std::sin((index + 0.5) / static_cast<double>(fade) * pi / 2));
            sound[index * 2] *= gain;
            sound[index * 2 + 1] *= gain;
        }
        for (int guard = 0; guard < 20 && !voice.silent(); ++guard)
            run(voice, sound, 0, 0.5, false, false, loop_seconds, true);
        if (!write_wav(directory + "/mm_mower_stop.wav", sound))
            return 1;
        std::printf("mm_mower_stop %.2f s\n", sound.size() / 2 / static_cast<double>(rate));
    }
    {
        // Chewing: the grinding alone, as a short one-shot to lay over the running loop.
        mm::ChipperVoice chipper(3);
        std::vector<float> sound(static_cast<std::size_t>(rate) * 2 * 12 / 10, 0.0F);
        chipper.render_add(sound, 1.0);
        const std::size_t frames = sound.size() / 2, fade = static_cast<std::size_t>(0.25 * rate);
        for (std::size_t index = 0; index < fade; ++index) {
            const float gain = static_cast<float>(std::cos((index + 0.5) / static_cast<double>(fade) * pi / 2));
            sound[(frames - fade + index) * 2] *= gain;
            sound[(frames - fade + index) * 2 + 1] *= gain;
        }
        if (!write_wav(directory + "/mm_chipper.wav", sound))
            return 1;
        std::printf("mm_chipper %.2f s\n", frames / static_cast<double>(rate));
    }
    {
        const char* names[4] = {"mm_granny_shout", "mm_granny_scold", "mm_granny_shriek", "mm_granny_wail"};
        for (int k = 0; k < 4; ++k) {
            mm::GrannyVoice voice(5);
            std::vector<float> sound(static_cast<std::size_t>(rate) * 2 * 4, 0.0F);
            const std::size_t frames = voice.render_line(static_cast<mm::GrannyLine>(k), sound);
            sound.resize(frames * 2);
            if (!write_wav(directory + "/" + names[k] + ".wav", sound))
                return 1;
            std::printf("%s %.2f s\n", names[k], frames / static_cast<double>(rate));
        }
    }
    return 0;
}
