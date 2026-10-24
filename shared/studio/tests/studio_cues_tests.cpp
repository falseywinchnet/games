// The PlaySuite studio's live cues (CTest studio_cues).
//   studio_cues_tests                    every cue is registered; the card sounds, stingers
//                                        and one loop render with their masters' shapes
//   studio_cues_tests --all              ... and every loop too (slow)
//   studio_cues_tests --reference DIR    every cue (or the ids after DIR) equals the original
//                                        program's render (shared/studio/tools/studio_reference.cpp)
//                                        to 1e-9 of its peak, and quantises to the same 24-bit master
//   studio_cues_tests --time ID...       render time and peak heap of each cue
//   studio_cues_tests --stages ID...     where each song's render spends its time
// --workers before any of these renders with GUI.Forms Workers (when built with them).
#include "studio_cues.hpp"
#include "studio_mix.hpp"
#include "studio_songs.hpp"
#include "voice_reference.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <set>
#include <string>
#include <vector>

// Heap accounting for --time: the bytes held now and the most held at once.
namespace {
std::atomic<std::size_t> heap_now{0};
std::atomic<std::size_t> heap_peak{0};
void note_allocation(std::size_t bytes) {
    const std::size_t now = heap_now.fetch_add(bytes) + bytes;
    std::size_t peak = heap_peak.load();
    while (now > peak && !heap_peak.compare_exchange_weak(peak, now)) {
    }
}
}  // namespace

void* operator new(std::size_t bytes) {
    void* block = std::malloc(bytes + 16);
    if (block == nullptr) {
        throw std::bad_alloc();
    }
    *static_cast<std::size_t*>(block) = bytes;
    note_allocation(bytes);
    return static_cast<char*>(block) + 16;
}
void operator delete(void* pointer) noexcept {
    if (pointer != nullptr) {
        void* block = static_cast<char*>(pointer) - 16;
        heap_now.fetch_sub(*static_cast<std::size_t*>(block));
        std::free(block);
    }
}
void operator delete(void* pointer, std::size_t) noexcept {
    operator delete(pointer);
}

namespace {

// --workers: renders share GUI.Forms Workers, as in the shell.
studio::Crew crew{};

int checks = 0;
int failures = 0;

void require(bool condition, const std::string& what) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAILED: %s\n", what.c_str());
    }
}

const voice::Cue* cue_named(const std::string& name) {
    for (const voice::Cue& cue : studio::sound_cues()) {
        if (cue.name == name) {
            return &cue;
        }
    }
    return nullptr;
}

// What the original program writes for each loop: whole bars at an integer samples-per-beat.
struct Loop {
    const char* name;
    std::size_t frames;
};
constexpr Loop loops[] = {
    {"music_menu_loop", 3896384},         {"music_klondike_loop", 4608000},   {"music_spider_loop", 4608000},
    {"music_freecell_loop", 4388608},     {"music_hearts_loop", 3770208},     {"music_sudoku_day_loop", 4850560},
    {"music_sudoku_night_loop", 5421184}, {"music_gems_loop", 4459392},       {"music_nature_cube_loop", 5120000},
    {"music_untangle_loop", 4873792},     {"music_puzzle_solve_loop", 5280000},
};

// The 24-bit sample the original wrote (writeWav24).
std::int32_t pcm24(double v) {
    v = std::min(std::max(v, -1.0), 1.0 - 1.0 / 8388608.0);
    std::int32_t q = static_cast<std::int32_t>(std::lround(v * 8388608.0));
    return std::max(std::min(q, 8388607), -8388608);
}

double peak_of(const voice::Stereo& s) {
    double peak = 0;
    for (std::size_t i = 0; i < s.left.size(); ++i) {
        peak = std::max({peak, std::fabs(s.left[i]), std::fabs(s.right[i])});
    }
    return peak;
}

void check_shape(const std::string& name, const voice::Stereo& s) {
    bool finite = s.left.size() == s.right.size();
    for (std::size_t i = 0; finite && i < s.left.size(); ++i) {
        finite = std::isfinite(s.left[i]) && std::isfinite(s.right[i]);
    }
    require(finite && !s.left.empty(), name + " renders finite stereo");
    const double peak = peak_of(s);
    if (name.starts_with("music_")) {
        for (const Loop& loop : loops) {
            if (name == loop.name) {
                require(s.left.size() == loop.frames, name + " is its whole bars long");
            }
        }
        require(peak > 0.1 && peak < std::pow(10.0, -1.0 / 20), name + " is mastered under -1 dBFS");
    } else if (name.starts_with("stinger_")) {
        require(s.left.size() <= 235200 && s.left.size() > 96000, name + " lasts 2 to 4.9 s");
        require(s.left.front() == 0 && s.right.front() == 0, name + " starts at zero");
        require(peak > 0.05 && peak < 1, name + " is audible and unclipped");
    } else {
        require(s.left.front() == 0 && s.right.front() == 0, name + " starts at zero");
        require(peak > 0.02 && peak < 0.2, name + " peaks between -34 and -14 dBFS");
    }
}

void self_tests(bool all) {
    require(studio::sound_cues().size() == 47, "the studio has 47 cues");
    std::set<std::string> seen{};
    for (const voice::Cue& cue : studio::sound_cues()) {
        seen.insert(std::string(cue.name));
        require(cue.stereo != nullptr && cue.mono == nullptr, std::string(cue.name) + " is a stereo cue");
    }
    require(seen.size() == 47, "every cue has its own name");
    for (const voice::Cue& cue : studio::sound_cues()) {
        const std::string name(cue.name);
        if (name.starts_with("music_") && !all && name != "music_hearts_loop") {
            continue;
        }
        const voice::Stereo s = cue.stereo();
        check_shape(name, s);
        if (name.starts_with("card_")) {
            require(cue.stereo().left == s.left, name + " renders the same every time");
        }
    }
#ifdef STUDIO_WORKERS
    {
        // the same samples with any crew
        studio::set_render_crew(studio::worker_crew());
        const voice::Stereo shared = (*cue_named("music_hearts_loop")).stereo();
        studio::set_render_crew({});
        const voice::Stereo alone = (*cue_named("music_hearts_loop")).stereo();
        require(shared.left == alone.left && shared.right == alone.right,
                "a loop rendered by Workers equals one rendered alone");
    }
#endif
    studio::add_studio_cues();
    require(voice::Bank::shared().find("music_menu_loop").has_value(), "the cues reach the shell's bank");
    const voice::Interleaved clip = voice::render(*voice::Bank::shared().find("card_place_02"));
    require(clip.size() == 2 * 18240, "a clip is interleaved stereo");
}

void reference_tests(const std::string& directory, const std::set<std::string>& only) {
    const std::vector<voice::reference::Case> cases = voice::reference::read_manifest(directory);
    require(!cases.empty(), "the reference has cases");
    std::printf("%-30s %9s %13s %10s %9s %8s %8s %8s\n", "cue", "frames", "max err/peak", "error dB", "identical",
                "pcm24", "ours s", "orig s");
    for (const voice::reference::Case& c : cases) {
        if (!only.empty() && !only.count(c.file)) {
            continue;
        }
        const voice::Cue* cue = cue_named(c.file);
        require(cue != nullptr, "a cue for " + c.file);
        if (cue == nullptr) {
            continue;
        }
        const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        const voice::Stereo ours = (*cue).stereo();
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const std::vector<float> inter = voice::reference::read_array<float>(directory, c.file);
        std::vector<double> reference(inter.begin(), inter.end());
        std::vector<double> mine(ours.left.size() * 2);
        for (std::size_t i = 0; i < ours.left.size(); ++i) {
            mine[2 * i] = ours.left[i];
            mine[2 * i + 1] = ours.right[i];
        }
        const voice::reference::Difference d = voice::reference::compare(mine, reference);
        std::size_t pcm_differences = mine.size() == reference.size() ? 0 : reference.size();
        for (std::size_t i = 0; i < mine.size() && i < reference.size(); ++i) {
            pcm_differences += pcm24(mine[i]) != pcm24(reference[i]) ? 1 : 0;
        }
        const double share = 100.0 * static_cast<double>(d.identical) / static_cast<double>(d.reference_length);
        std::printf("%-30s %9zu %13.3g %10.1f %8.4f%% %8s %8.2f %8s\n", c.file.c_str(), d.reference_length / 2,
                    d.relative, d.error_db, share, pcm_differences == 0 ? "same" : std::to_string(pcm_differences).c_str(),
                    seconds, c.args.size() > 1 ? c.args[1].c_str() : "?");
        std::fflush(stdout);
        require(d.same_length() && d.relative <= 1e-9, c.file + " equals the original render");
    }
}

void time_tests(const std::vector<std::string>& ids) {
    std::printf("%-30s %9s %9s %12s %12s\n", "cue", "frames", "seconds", "peak heap MB", "clip MB");
    for (const std::string& id : ids) {
        const voice::Cue* cue = cue_named(id);
        require(cue != nullptr, "a cue for " + id);
        if (cue == nullptr) {
            continue;
        }
        heap_peak.store(heap_now.load());
        const std::size_t before = heap_now.load();
        const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        const voice::Interleaved clip = voice::render(*cue);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("%-30s %9zu %9.2f %12.1f %12.1f\n", id.c_str(), clip.size() / 2, seconds,
                    static_cast<double>(heap_peak.load() - before) / 1e6, static_cast<double>(clip.size() * 4) / 1e6);
        std::fflush(stdout);
    }
}

// Where each song's render spends its time.
void stage_tests(const std::vector<std::string>& ids) {
    std::printf("%-30s %8s %8s %8s %8s\n", "song", "tracks", "buses", "master", "total");
    for (const std::string& id : ids) {
        for (const studio::SongFactory& factory : studio::allSongs()) {
            if (factory.id != id) {
                continue;
            }
            studio::RenderTimings t{};
            const studio::Song song = factory.make();
            (void)studio::renderSong(song, &t, crew);
            std::printf("%-30s %8.2f %8.2f %8.2f %8.2f\n", id.c_str(), t.tracks, t.buses, t.master,
                        t.tracks + t.buses + t.master);
            std::fflush(stdout);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (!args.empty() && args[0] == "--workers") {
#ifdef STUDIO_WORKERS
        crew = studio::worker_crew();
        studio::set_render_crew(crew);
        std::printf("rendering with %d helper Workers\n", crew.helpers);
#else
        std::printf("built without GUI.Forms: rendering alone\n");
#endif
        args.erase(args.begin());
    }
    if (args.size() >= 2 && args[0] == "--reference") {
        reference_tests(args[1], std::set<std::string>(args.begin() + 2, args.end()));
    } else if (!args.empty() && args[0] == "--stages") {
        stage_tests(std::vector<std::string>(args.begin() + 1, args.end()));
    } else if (!args.empty() && args[0] == "--time") {
        time_tests(std::vector<std::string>(args.begin() + 1, args.end()));
    } else {
        self_tests(!args.empty() && args[0] == "--all");
    }
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
