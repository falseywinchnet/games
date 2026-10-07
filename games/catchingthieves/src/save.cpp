#include "save.hpp"
#include "level.hpp"
#include "tiers.hpp"
#include <algorithm>
#include <map>
#include <stdexcept>

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace ct {

namespace {
const char kHeader[] = "THIEVES1\n";

std::uint64_t fnv(const std::string& s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}
int integer(const std::string& text) {
    std::size_t consumed = 0;
    const int value = std::stoi(text, &consumed);
    if (consumed != text.size()) throw std::invalid_argument("Invalid save integer");
    return value;
}
bool lurd_only(const std::string& s) {
    for (char c : s)
        if (std::string("udlrUDLR").find(c) == std::string::npos) return false;
    return true;
}
bool board_chars(const std::string& s) {
    for (char c : s)
        if (std::string("#@+$*. |-_").find(c) == std::string::npos) return false;
    return true;
}
bool title_chars(const std::string& s) {
    if (s.size() > 60) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '\'' || c == '-')) return false;
    return true;
}
}  // namespace


bool write_save(const std::filesystem::path& path, const SaveData& d) {
    std::ostringstream o;
    o << "level=" << d.level << "\nhistory=" << (lurd_only(d.history) ? d.history : "") << "\n";
    for (const std::pair<const int, LevelRecord>& record : d.records)
        o << "record=" << record.first << " " << record.second.best_moves << " " << record.second.best_pushes << "\n";
    if (!d.endless_xsb.empty() && board_chars(d.endless_xsb)) {
        o << "endless=" << d.endless_xsb << "\nendless_solution=" << (lurd_only(d.endless_solution) ? d.endless_solution : "") << "\n";
    }
    o << "endless_season=" << d.endless_season << "\nendless_tier=" << d.endless_tier << "\nendless_cleared=" << d.endless_cleared
      << "\nsound=" << d.settings.sound << "\nmusic=" << d.settings.music << "\n";
    if (d.format >= 2) {
        o << "format=2\ndifficulty=" << d.difficulty << "\ngarden_tier=" << d.garden_tier << "\nseason=" << d.season
          << "\ngarden_par=" << d.garden_par << "\n";
        if (!d.garden_title.empty() && title_chars(d.garden_title)) o << "garden_title=" << d.garden_title << "\n";
        for (int t = 0; t < static_cast<int>(d.tiers.size()); ++t)
            o << "tier=" << t << " " << d.tiers[static_cast<size_t>(t)].cleared << " " << d.tiers[static_cast<size_t>(t)].perfect << "\n";
        o << "played=";
        bool first = true;
        for (int id : d.played) { o << (first ? "" : " ") << id; first = false; }
        o << "\n";
    }
    std::string body = o.str();
    body = kHeader + body + "check=" + std::to_string(fnv(body)) + "\n";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << body;
        if (!f.good()) return false;
    }
    std::filesystem::rename(tmp, path, ec);
    return !ec;
}

bool load_save(const std::filesystem::path& path, SaveData& out) {
    std::error_code size_error;
    const std::uintmax_t bytes = std::filesystem::file_size(path, size_error);
    if (size_error || bytes > 256 * 1024) return false;
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string all = ss.str();
    const size_t hl = sizeof kHeader - 1;
    if (all.size() > 256 * 1024 || all.rfind(kHeader, 0) != 0) return false;
    const size_t ck = all.rfind("check=");
    if (ck == std::string::npos || ck < hl) return false;
    const std::string body = all.substr(hl, ck - hl);
    if (std::to_string(fnv(body)) + "\n" != all.substr(ck + 6)) return false;
    SaveData d;
    std::istringstream in(body);
    std::string line;
    try {
        while (std::getline(in, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "level") d.level = integer(v);
            else if (k == "history") { if (!lurd_only(v) || v.size() >= 100000) return false; d.history = v; }
            else if (k == "record") {
                std::istringstream rs(v);
                int idx = -1;
                LevelRecord r;
                rs >> idx >> r.best_moves >> r.best_pushes;
                if (idx >= 0 && idx < 100000 && r.best_moves > 0 && r.best_pushes > 0 && r.best_pushes <= r.best_moves) d.records[idx] = r;
            }
            else if (k == "endless") { if (!board_chars(v) || v.size() >= 2000) return false; d.endless_xsb = v; }
            else if (k == "endless_solution") { if (!lurd_only(v)) return false; d.endless_solution = v; }
            else if (k == "endless_season") d.endless_season = integer(v);
            else if (k == "endless_tier") d.endless_tier = integer(v);
            else if (k == "endless_cleared") d.endless_cleared = integer(v);
            else if (k == "sound") d.settings.sound = v == "1";
            else if (k == "music") d.settings.music = v == "1";
            else if (k == "format") d.format = integer(v);
            else if (k == "difficulty") d.difficulty = integer(v);
            else if (k == "garden_tier") d.garden_tier = integer(v);
            else if (k == "season") d.season = integer(v);
            else if (k == "garden_par") d.garden_par = integer(v);
            else if (k == "garden_title") { if (!title_chars(v)) return false; d.garden_title = v; }
            else if (k == "tier") {
                std::istringstream ts(v);
                int t = -1;
                TierRecord r;
                if (!(ts >> t >> r.cleared >> r.perfect) || t < 0 || t >= static_cast<int>(d.tiers.size()) || r.cleared < 0 || r.perfect < 0 ||
                    r.perfect > r.cleared) return false;
                d.tiers[static_cast<size_t>(t)] = r;
            }
            else if (k == "played") {
                std::istringstream ps(v);
                int id = -1;
                while (ps >> id) {
                    if (id < 0 || id > kMaxGardenId) return false;
                    d.played.insert(id);
                }
            }
        }
    } catch (...) {
        return false;
    }
    if (d.level < -1 || d.level > kMaxGardenId || d.endless_tier < 0 || d.endless_tier >= 5 ||
        d.endless_season < 0 || d.endless_season >= 5 || d.endless_cleared < 0) return false;
    if (d.format < 1 || d.format > 2 || d.difficulty < 0 || d.difficulty >= kDifficulties || d.garden_tier < 0 ||
        d.garden_tier >= kDifficulties || d.season < 0 || d.season >= 5 || d.garden_par < 0 || d.garden_par > 100000) return false;
    // the old book had 243 gardens; only an upgraded save may point past them
    if (d.format == 1 && d.level >= 243) return false;
    if (d.level == -1 && !d.endless_xsb.empty()) {
        std::string board_text = d.endless_xsb;
        std::replace(board_text.begin(), board_text.end(), '|', '\n');
        Level level;
        Board board;
        if (!Level::parse(board_text, level) || !board.load(level) || !board.replay(d.history)) return false;
        if (!d.endless_solution.empty()) {
            board.restart();
            if (!board.replay(d.endless_solution) || !board.solved()) return false;
        }
    }
    out = d;
    return true;
}

int old_book_season(int id) {
    // First Steps and both spring sections, then summer, autumn and winter, and Winter Night
    if (id < 63) return 0;
    if (id < 123) return 1;
    if (id < 183) return 2;
    if (id < 213) return 3;
    return 4;
}

int old_endless_difficulty(int endless_tier) {
    static const int kMap[5] = {kEasy, kMedium, kMedium, kHard, kHard};
    return kMap[endless_tier >= 0 && endless_tier < 5 ? endless_tier : 0];
}

bool migrate(SaveData& d, const std::vector<LevelEntry>& gardens) {
    if (d.format >= 2) return false;
    std::map<int, const LevelEntry*> by_id;
    for (const LevelEntry& e : gardens) by_id[e.id] = &e;
    d.format = 2;
    d.tiers = {};
    d.played.clear();
    for (const std::pair<const int, LevelRecord>& record : d.records) {
        const std::map<int, const LevelEntry*>::const_iterator it = by_id.find(record.first);
        if (it == by_id.end()) continue;
        const LevelEntry& e = *(*it).second;
        const int tier = std::max(0, difficulty_from_key(e.tier));
        TierRecord& t = d.tiers[static_cast<size_t>(tier)];
        ++t.cleared;
        if (e.par > 0 && record.second.best_pushes <= e.par) ++t.perfect;
        d.played.insert(record.first);
    }
    d.tiers[static_cast<size_t>(old_endless_difficulty(d.endless_tier))].cleared += d.endless_cleared;
    if (d.level >= 0) {
        const std::map<int, const LevelEntry*>::const_iterator it = by_id.find(d.level);
        if (it == by_id.end()) {
            // not in the tables: deal afresh at the start
            d.level = -1;
            d.history.clear();
            d.endless_xsb.clear();
            d.difficulty = d.garden_tier = kTutorial;
            return true;
        }
        d.garden_tier = std::max(0, difficulty_from_key((*(*it).second).tier));
        d.season = old_book_season(d.level);
        d.played.insert(d.level);
    } else if (!d.endless_xsb.empty()) {
        d.garden_tier = old_endless_difficulty(d.endless_tier);
        d.season = d.endless_tier;
        d.garden_par = 0;
        for (char c : d.endless_solution) d.garden_par += c >= 'A' && c <= 'Z';
        static const char* kNames[5] = {"Spring garden", "Summer garden", "Autumn garden", "Winter garden", "Night garden"};
        d.garden_title = kNames[d.endless_tier];
    } else {
        // the old endless garden was still growing: carry on at its tier
        d.garden_tier = old_endless_difficulty(d.endless_tier);
        d.history.clear();
    }
    d.difficulty = d.garden_tier;
    return true;
}

}  // namespace ct
