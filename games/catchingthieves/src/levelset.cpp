#include "levelset.hpp"

#include <cstdlib>
#include <sstream>

namespace ct {

namespace {
// The board read so far becomes a level, carrying the keys read before it.
bool flush(LevelEntry& cur, std::string& board, std::vector<LevelEntry>& out, std::string* error) {
    if (board.empty()) return true;
    std::string err;
    const std::string title = cur.level.title, solution = cur.level.solution;  // parse replaces the level
    if (!Level::parse(board, cur.level, &err)) {
        if (error) *error = "level " + std::to_string(out.size() + 1) + ": " + err;
        return false;
    }
    cur.level.title = title;
    cur.level.solution = solution;
    out.push_back(cur);
    cur = LevelEntry{};
    board.clear();
    return true;
}

void trim(std::string& s) {
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
}
}  // namespace

bool load_levels(const std::string& text, std::vector<LevelEntry>& out, std::string* error) {
    std::istringstream in(text);
    std::string line, board;
    LevelEntry cur;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] == ';') {
            if (!board.empty() && !flush(cur, board, out, error)) return false;
            const size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string k = line.substr(1, colon - 1), v = line.substr(colon + 1);
            trim(k);
            trim(v);
            if (k == "title") cur.level.title = v;
            else if (k == "section") cur.section = v;
            else if (k == "par") cur.par = std::atoi(v.c_str());
            else if (k == "switches") cur.switches = std::atoi(v.c_str());
            else if (k == "solution") cur.level.solution = v;
            else if (k == "seed") cur.seed = v;
            else if (k == "id") cur.id = std::atoi(v.c_str());
            else if (k == "tier") cur.tier = v;
            else if (k == "lesson") cur.lesson = v;
            else if (k == "nodes") cur.nodes = std::atoll(v.c_str());
            continue;
        }
        if (line.find_first_not_of(' ') == std::string::npos) {
            if (!flush(cur, board, out, error)) return false;
            continue;
        }
        board += line + "\n";
    }
    return flush(cur, board, out, error);
}

std::string save_levels(const std::vector<LevelEntry>& levels) {
    std::ostringstream o;
    for (const LevelEntry& e : levels) {
        if (e.id >= 0) o << "; id: " << e.id << "\n";
        if (!e.tier.empty()) o << "; tier: " << e.tier << "\n";
        if (!e.lesson.empty()) o << "; lesson: " << e.lesson << "\n";
        if (!e.level.title.empty()) o << "; title: " << e.level.title << "\n";
        if (!e.section.empty()) o << "; section: " << e.section << "\n";
        if (e.par) o << "; par: " << e.par << "\n";
        if (e.switches) o << "; switches: " << e.switches << "\n";
        if (!e.seed.empty()) o << "; seed: " << e.seed << "\n";
        if (e.nodes) o << "; nodes: " << e.nodes << "\n";
        if (!e.level.solution.empty()) o << "; solution: " << e.level.solution << "\n";
        o << e.level.xsb() << "\n";
    }
    return o.str();
}

}  // namespace ct
