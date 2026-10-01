#include "sudoku.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
namespace games {
bool Sudoku::set(int cell, int digit, bool note) {
    if (over || cell < 0 || cell >= 81 || digit < 0 || digit > 9 || puzzle[cell])
        return false;
    if ((!note && grid.values[cell] == digit) || (note && (digit == 0 || grid.values[cell])))
        return false;
    if (history.size() == 512)
        history.erase(history.begin());
    history.push_back(grid);
    if (note)
        grid.notes[cell] ^= 1 << (digit - 1);
    else {
        grid.values[cell] = digit;
        grid.notes[cell] = 0;
        if (digit && digit != solution[cell])
            ++errors;
    }
    over = grid.values == solution;
    message = over ? "Beautifully solved." : "Left click to set · right click to note";
    return true;
}
bool Sudoku::undo() {
    if (history.empty())
        return false;
    grid = history.back();
    history.pop_back();
    over = false;
    message = "Move undone. Mistakes remain counted.";
    return true;
}
bool Sudoku::complete_unit(int unit) const {
    for (int i = 0; i < 9; ++i) {
        int cell = unit < 9 ? unit * 9 + i
                   : unit < 18
                       ? (unit - 9) + i * 9
                       : ((unit - 18) / 3) * 27 + ((unit - 18) % 3) * 3 + (i / 3) * 9 + i % 3;
        if (grid.values[cell] != solution[cell])
            return false;
    }
    return true;
}
bool Sudoku::invariant() const {
    if (difficulty < 0 || difficulty > 2 || errors < 0 || errors > 1000000 || seed.empty() ||
        seed.size() > 128)
        return false;
    for (int i = 0; i < 81; ++i)
        if (solution[i] < 1 || solution[i] > 9 || puzzle[i] < 0 || puzzle[i] > 9 ||
            (puzzle[i] && (puzzle[i] != solution[i] || grid.values[i] != puzzle[i])) ||
            grid.values[i] < 0 || grid.values[i] > 9 || grid.notes[i] < 0 || grid.notes[i] > 511)
            return false;
    for (int unit = 0; unit < 27; ++unit) {
        int mask = 0;
        for (int i = 0; i < 9; ++i) {
            int cell = unit < 9 ? unit * 9 + i
                       : unit < 18
                           ? unit - 9 + i * 9
                           : ((unit - 18) / 3) * 27 + ((unit - 18) % 3) * 3 + (i / 3) * 9 + i % 3;
            mask |= 1 << (solution[cell] - 1);
        }
        if (mask != 511)
            return false;
    }
    return over == (grid.values == solution);
}
bool Sudoku::qualifies() const {
    return over && !recorded &&
           (scores[difficulty].size() < 10 || errors < scores[difficulty].back().value);
}
bool Sudoku::record(const std::string& name) {
    if (!qualifies() || !valid_score_name(name))
        return false;
    std::vector<TopScore>& list = scores[difficulty];
    std::vector<TopScore>::iterator position = list.begin();
    while (position != list.end() && (*position).value <= errors)
        ++position;
    list.insert(position, {name, errors});
    if (list.size() > 10)
        list.pop_back();
    recorded = true;
    player_name = name;
    return true;
}
static std::uint64_t hash_text(const std::string& data) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : data) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}
bool Sudoku::save(const std::filesystem::path& path) const {
    if (!invariant())
        return false;
    std::ostringstream body;
    body << std::quoted(seed) << ' ' << difficulty << ' ' << errors << ' ' << dark << ' ' << over
         << ' ' << recorded << '\n';
    body << std::quoted(player_name) << '\n';
    for (int i = 0; i < 81; ++i)
        body << puzzle[i] << ' ' << solution[i] << ' ' << grid.values[i] << ' ' << grid.notes[i]
             << '\n';
    for (const std::vector<TopScore>& list : scores) {
        body << list.size() << '\n';
        for (const TopScore& entry : list)
            body << std::quoted(entry.name) << ' ' << entry.value << '\n';
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        return false;
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::ofstream out(temporary);
    out << "RAINSTAR_SUDOKU 1 " << hash_text(body.str()) << '\n' << body.str();
    out.close();
    if (!out)
        return false;
    std::filesystem::rename(temporary, path, error);
    return !error;
}
bool Sudoku::load(const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::file_size(path, error) > 16384 || error)
        return false;
    std::ifstream in(path);
    std::string magic;
    int version = 0;
    std::uint64_t expected = 0;
    in >> magic >> version >> expected;
    if (!in || magic != "RAINSTAR_SUDOKU" || version != 1)
        return false;
    in.get();
    std::ostringstream data;
    data << in.rdbuf();
    if (hash_text(data.str()) != expected)
        return false;
    std::istringstream body(data.str());
    Sudoku candidate;
    body >> std::quoted(candidate.seed) >> candidate.difficulty >> candidate.errors >>
        candidate.dark >> candidate.over >> candidate.recorded;
    body >> std::quoted(candidate.player_name);
    if (!valid_score_name(candidate.player_name))
        return false;
    for (int i = 0; i < 81; ++i)
        body >> candidate.puzzle[i] >> candidate.solution[i] >> candidate.grid.values[i] >>
            candidate.grid.notes[i];
    for (std::vector<TopScore>& list : candidate.scores) {
        int count = 0;
        body >> count;
        if (!body || count < 0 || count > 10)
            return false;
        for (int i = 0; i < count; ++i) {
            TopScore entry;
            body >> std::quoted(entry.name) >> entry.value;
            if (!body || !valid_score_name(entry.name) || entry.value < 0 ||
                entry.value > 1000000 || (!list.empty() && entry.value < list.back().value))
                return false;
            list.push_back(entry);
        }
    }
    if (!body || !candidate.invariant())
        return false;
    body >> std::ws;
    if (!body.eof())
        return false;
    *this = std::move(candidate);
    message = "Welcome back. Your puzzle was saved.";
    return true;
}
} // namespace games
