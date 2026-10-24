// Reference renders for the live studio cues, made by the original program's own code.
// Built from authoring/playsuite_music/src unchanged (card_sfx.cpp is included so its clip
// functions, internal to that file, can be called without its 24-bit file writer):
//
//   c++ -std=c++20 -O2 -ffp-contract=off -I authoring/playsuite_music/src \
//       shared/studio/tools/studio_reference.cpp \
//       $(ls authoring/playsuite_music/src/*.cpp | grep -v -e main.cpp -e card_sfx.cpp) \
//       -o studio_reference
//   studio_reference <out_dir> [id ...]
//
// For every song, stinger and card sound (or just the ids given) it writes <id>.bin, the
// finished master as interleaved little-endian float32 before quantisation, and appends
// "<id> stereo <frames> <seconds to render>" to <out_dir>/manifest.txt.
#include "card_sfx.cpp"
#include "mix.hpp"
#include "songs.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

namespace {

void write(const std::string& dir, const std::string& id, const ps::Stereo& s, double seconds) {
    std::vector<float> inter(s.size() * 2);
    for (std::size_t i = 0; i < s.size(); ++i) {
        inter[2 * i] = s.L[i];
        inter[2 * i + 1] = s.R[i];
    }
    std::ofstream(dir + "/" + id + ".bin", std::ios::binary)
        .write(reinterpret_cast<const char*>(inter.data()), std::streamsize(inter.size() * 4));
    std::ofstream(dir + "/manifest.txt", std::ios::app)
        << id << " stereo " << s.size() << " " << seconds << "\n";
    std::fprintf(stderr, "%-34s %9zu frames %7.2f s\n", id.c_str(), s.size(), seconds);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: studio_reference <out_dir> [id ...]\n");
        return 2;
    }
    const std::string dir = argv[1];
    std::filesystem::create_directories(dir);
    const std::set<std::string> want(argv + 2, argv + argc);
    using clock = std::chrono::steady_clock;
    for (const ps::SongFactory& f : ps::allSongs()) {
        if (f.id.rfind("instrument_", 0) == 0 || (!want.empty() && !want.count(f.id)))
            continue;
        const clock::time_point t0 = clock::now();
        ps::Song song = f.make();
        ps::Report rep;
        ps::Stereo audio = ps::renderSong(song, rep);
        write(dir, f.id, audio, std::chrono::duration<double>(clock::now() - t0).count());
    }
    const struct {
        const char* stem;
        ps::Clip (*make)(int);
        int count;
        double peak_db;
    } kinds[] = {{"card_pickup", ps::pickup, 4, -28},         {"card_place", ps::place, 4, -22},
                 {"card_flip", ps::flip, 4, -20.5},           {"card_foundation", ps::foundation, 4, -15.5},
                 {"card_deal", ps::deal, 4, -20},             {"card_shuffle", ps::shuffle, 2, -19.5}};
    for (const auto& kind : kinds)
        for (int i = 1; i <= kind.count; ++i) {
            const std::string id = std::string(kind.stem) + (i < 10 ? "_0" : "_") + std::to_string(i);
            if (!want.empty() && !want.count(id))
                continue;
            const clock::time_point t0 = clock::now();
            ps::Clip clip = kind.make(i);
            ps::finish(clip, kind.peak_db);
            write(dir, id, clip.audio, std::chrono::duration<double>(clock::now() - t0).count());
        }
    return 0;
}
