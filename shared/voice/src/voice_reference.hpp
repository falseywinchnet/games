#pragma once
// Reading the NumPy reference renders that tools/numpy_reference.py writes (a
// manifest.txt of cases and one raw little-endian array per case) and measuring how
// far a C++ render is from one. Used by voice_tests and by each game's cue test;
// header-only, standard library only, for tests and tools rather than the game.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace voice::reference {

struct Case {
    std::string file{};  // without the .bin extension
    std::string kind{};
    std::vector<std::string> args{};
};

// The cases in `directory`/manifest.txt, in order; empty when it is missing.
inline std::vector<Case> read_manifest(const std::string& directory) {
    std::vector<Case> cases{};
    std::ifstream in(directory + "/manifest.txt");
    std::string line{};
    while (std::getline(in, line)) {
        std::istringstream words(line);
        Case entry{};
        words >> entry.file >> entry.kind;
        std::string word{};
        while (words >> word) {
            entry.args.push_back(word);
        }
        if (!entry.kind.empty()) {
            cases.push_back(entry);
        }
    }
    return cases;
}

// A raw array of `T` (little-endian, as on every PlaySuite target).
template <class T>
std::vector<T> read_array(const std::string& directory, const std::string& file) {
    std::ifstream in(directory + "/" + file + ".bin", std::ios::binary);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<T> values(bytes.size() / sizeof(T));
    for (std::size_t i = 0; i < values.size(); ++i) {
        T value{};
        char* destination = reinterpret_cast<char*>(&value);
        for (std::size_t b = 0; b < sizeof(T); ++b) {
            destination[b] = bytes[i * sizeof(T) + b];
        }
        values[i] = value;
    }
    return values;
}

struct Difference {
    std::size_t ours_length{};
    std::size_t reference_length{};
    double max_abs{};         // largest |ours - reference|
    double peak{};            // largest |reference|
    double relative{};        // max_abs / peak (0 for a silent reference that matches)
    double error_db{};        // RMS of the difference against RMS of the reference, in dB
    std::size_t identical{};  // samples equal bit for bit
    [[nodiscard]] bool same_length() const {
        return ours_length == reference_length;
    }
};

inline Difference compare(std::span<const double> ours, std::span<const double> reference) {
    Difference d{};
    d.ours_length = ours.size();
    d.reference_length = reference.size();
    const std::size_t n = std::min(ours.size(), reference.size());
    double error_energy = 0;
    double energy = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double e = std::fabs(ours[i] - reference[i]);
        d.max_abs = std::max(d.max_abs, e);
        d.peak = std::max(d.peak, std::fabs(reference[i]));
        error_energy += e * e;
        energy += reference[i] * reference[i];
        if (ours[i] == reference[i]) {
            ++d.identical;
        }
    }
    d.relative = d.peak > 0 ? d.max_abs / d.peak : d.max_abs;
    d.error_db = error_energy == 0 ? -400 : (energy > 0 ? 10 * std::log10(error_energy / energy) : 400);
    return d;
}

}  // namespace voice::reference
