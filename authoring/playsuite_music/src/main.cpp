// playsuite_music: renders PlaySuite music loops and stingers.
//   playsuite_music sfx <out_dir> [id ...]        render the card handling sounds
//   playsuite_music render <out_dir> [id ...]     render WAV masters (+ .f32 reference, .json
//   report) playsuite_music list                          list song ids playsuite_music lint
//   harmonic self-check (sustained semitone clashes) playsuite_music analyse <decoded.f32>
//   <master.f32> <frames> <loop 0|1>
#include "mix.hpp"
#include "songs.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

namespace ps {
int renderCardSfx(const std::string& out, const std::set<std::string>& want);
}

static const char* noteName(int m) {
    static const char* n[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    return n[((m % 12) + 12) % 12];
}

// Harmonic self-check: sustained melodic notes a semitone away from a chord tone of the underlying
// chord.
static int lintSong(const ps::Song& s) {
    using namespace ps;
    int total = 0;
    double beats = double(s.bars) * s.bpb;
    for (const Track& t : s.tracks) {
        if (t.p.inst >= Inst::Kick)
            continue;
        int flagged = 0;
        std::string examples;
        for (const Note& n : t.notes) {
            if (n.dur < 0.74 || n.vel <= 0)
                continue;
            double b = n.beat < 0 ? n.beat + beats : n.beat;
            const ChordAt* c = nullptr;
            for (const ChordAt& x : s.chords)
                if (b >= x.beat - 1e-9 && b < x.beat + x.dur - 1e-9) {
                    c = &x;
                    break;
                }
            if (!c)
                continue;
            int pc = ((int(std::lround(n.pitch)) % 12) + 12) % 12;
            bool in = false, clash = false;
            std::vector<int> pcs;
            for (int iv : (*c).c.tones)
                pcs.push_back(((*c).c.root + iv) % 12);
            if ((*c).c.bass >= 0)
                pcs.push_back((*c).c.bass);
            for (int q : pcs)
                if (q == pc)
                    in = true;
            // available tensions: 9th everywhere, 11th on minor chords, 13th on major/dominant
            // chords
            const std::vector<int>& tv = (*c).c.tones;
            bool minor = std::find(tv.begin(), tv.end(), 3) != tv.end();
            int rel = ((pc - (*c).c.root) % 12 + 12) % 12;
            if (rel == 2 || (minor && rel == 5) || (!minor && rel == 9))
                in = true;
            // chromatic approach into the next chord (walking bass, pickups)
            if (!in && b + n.dur >= (*c).beat + (*c).dur - 0.3) {
                for (const ChordAt& x : s.chords)
                    if (std::fabs(x.beat - ((*c).beat + (*c).dur)) < 1e-6) {
                        int d = ((pc - x.c.bassPc()) % 12 + 12) % 12;
                        if (d == 1 || d == 11 || pc == x.c.bassPc())
                            in = true;
                        for (int iv : x.c.tones)
                            if ((x.c.root + iv) % 12 == pc)
                                in = true;
                    }
            }
            for (int q : pcs) {
                int d = ((pc - q) % 12 + 12) % 12;
                if (d == 1 || d == 11)
                    clash = true;
            }
            if (!in && clash) {
                ++flagged;
                if (flagged <= 6) {
                    char buf[96];
                    std::snprintf(buf, sizeof buf, " bar %d beat %.2f %s%d over %s;",
                                  int(b / s.bpb) + 1, std::fmod(b, double(s.bpb)) + 1,
                                  noteName(int(std::lround(n.pitch))),
                                  int(std::lround(n.pitch)) / 12 - 1, (*c).c.name.c_str());
                    examples += buf;
                }
            }
        }
        if (flagged)
            std::printf("  %-18s %3d clash(es):%s\n", t.name.c_str(), flagged, examples.c_str());
        total += flagged;
    }
    return total;
}

int main(int argc, char** argv) {
    using namespace ps;
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: playsuite_music render <out_dir> [id...] | list | analyse ...\n");
        return 2;
    }
    std::string cmd = argv[1];
    try {
        if (cmd == "analyse" && argc >= 6) {
            std::cout << analyseDecoded(argv[2], argv[3], std::atol(argv[4]),
                                        std::atoi(argv[5]) != 0)
                      << "\n";
            return 0;
        }
        if (cmd == "sfx" && argc >= 3) {
            std::filesystem::create_directories(argv[2]);
            std::set<std::string> want(argv + 3, argv + argc);
            return renderCardSfx(argv[2], want) > 0 ? 0 : 1;
        }
        std::vector<SongFactory> songs = allSongs();
        if (cmd == "list") {
            for (const SongFactory& s : songs)
                std::cout << s.id << "\n";
            return 0;
        }
        if (cmd == "lint") {
            for (const SongFactory& f : songs) {
                Song s = f.make();
                if (s.chords.empty())
                    continue;
                std::printf("%s\n", s.id.c_str());
                std::printf("  total %d\n", lintSong(s));
            }
            return 0;
        }
        if (cmd == "render" && argc >= 3) {
            std::string out = argv[2];
            std::filesystem::create_directories(out);
            std::set<std::string> want;
            for (int i = 3; i < argc; ++i)
                want.insert(argv[i]);
            for (const SongFactory& f : songs) {
                if (!want.empty() && !want.count(f.id))
                    continue;
                if (want.empty() && f.id.rfind("instrument_", 0) == 0)
                    continue;
                Song song = f.make();
                std::fprintf(stderr, "rendering %s (%d bars, %.3f bpm)...\n", song.id.c_str(),
                             song.bars, song.bpm());
                Report rep;
                Stereo audio = renderSong(song, rep);
                rep.file = song.id + ".wav";
                writeWav24(out + "/" + song.id + ".wav", audio, 0);
                writeF32(out + "/" + song.id + ".f32", audio);
                std::ofstream(out + "/" + song.id + ".json") << reportJson(rep) << "\n";
                std::cout << reportJson(rep) << "\n";
            }
            return 0;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    std::fprintf(stderr, "bad arguments\n");
    return 2;
}
