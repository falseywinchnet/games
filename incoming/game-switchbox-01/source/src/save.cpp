#include "save.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace sbx {

namespace {
constexpr size_t kMaxScores = 10;

std::uint64_t fnv(const std::string& s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}
std::string clean_name(const std::string& n) {
    std::string out;
    for (char c : n)
        if (c >= 32 && c < 127 && c != '\t') out += c;
    if (out.size() > 14) out.resize(14);
    return out;
}
}  // namespace

// v2: switchbox-v1.txt belongs to the earlier, removed Switchbox and is kept untouched.
// GAMES_STATE_DIR redirects saves for isolated tests, as for the rest of the cabinet.
std::filesystem::path save_path(bool dev) {
    const char* name = dev ? "switchbox-dev-v2.txt" : "switchbox-v2.txt";
    if (const char* dir = std::getenv("GAMES_STATE_DIR")) return std::filesystem::path(dir) / name;
    const char* home = std::getenv("HOME");
    std::filesystem::path base = home ? home : ".";
    base /= "Library/Application Support/Rainstar/Games";
    return base / name;
}

bool write_save(const std::filesystem::path& path, const SaveData& d) {
    std::ostringstream o;
    const PuzzleState& p = d.puzzle;
    if (d.has_puzzle) {
        o << "rng=" << p.rng << "\ncombo=";
        for (int i = 0; i < kSwitches; ++i) o << (i ? "," : "") << p.combo[static_cast<size_t>(i)];
        o << "\nprogress=" << p.progress << "\nsteps=" << p.steps << "\nzero=" << p.zero_streak << "\nbest_depth=" << p.best_depth
          << "\nstall=" << p.stall << "\nlost=" << p.lost_level << "\nstuck=" << p.stuck_scolds << "\nripped=" << p.ripped << "\nlast_wrong=" << p.last_wrong << "\n";
    }
    const Settings& s = d.settings;
    o << "cracked=" << d.cracked << "\nsound=" << s.sound << "\nmusic=" << s.music << "\nreduced=" << s.reduced_motion
      << "\nname=" << clean_name(s.player_name) << "\n";
    for (const TopScore& t : d.scores) o << "score=" << clean_name(t.name) << '\t' << t.steps << '\t' << std::setprecision(15) << t.when << "\n";
    std::string body = o.str();
    body = "SWITCHBOX1\n" + body + "check=" + std::to_string(fnv(body)) + "\n";
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
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string all = ss.str();
    if (all.size() > 64 * 1024 || all.rfind("SWITCHBOX1\n", 0) != 0) return false;
    const size_t ck = all.rfind("check=");
    if (ck == std::string::npos) return false;
    const std::string body = all.substr(11, ck - 11);
    if (std::to_string(fnv(body)) + "\n" != all.substr(ck + 6)) return false;
    SaveData d;
    std::istringstream in(body);
    std::string line;
    bool have_combo = false;
    try {
        while (std::getline(in, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            PuzzleState& p = d.puzzle;
            if (k == "rng") p.rng = std::stoull(v);
            else if (k == "combo") {
                std::istringstream cs(v);
                std::string n;
                for (int i = 0; i < kSwitches && std::getline(cs, n, ','); ++i) p.combo[static_cast<size_t>(i)] = std::stoi(n);
                have_combo = true;
            }
            else if (k == "progress") p.progress = std::stoi(v);
            else if (k == "steps") p.steps = std::stoi(v);
            else if (k == "zero") p.zero_streak = std::stoi(v);
            else if (k == "best_depth") p.best_depth = std::stoi(v);
            else if (k == "stall") p.stall = std::stoi(v);
            else if (k == "lost") p.lost_level = std::stoi(v);
            else if (k == "stuck") p.stuck_scolds = std::stoi(v);
            else if (k == "ripped") p.ripped = v == "1";
            else if (k == "last_wrong") p.last_wrong = std::stoi(v);
            else if (k == "cracked") d.cracked = std::stoi(v);
            else if (k == "sound") d.settings.sound = v == "1";
            else if (k == "music") d.settings.music = v == "1";
            else if (k == "reduced") d.settings.reduced_motion = v == "1";
            else if (k == "name") d.settings.player_name = clean_name(v);
            else if (k == "score" && d.scores.size() < kMaxScores) {
                std::istringstream sc(v);
                TopScore t;
                std::string a, b, c;
                std::getline(sc, a, '\t'); std::getline(sc, b, '\t'); std::getline(sc, c, '\t');
                t.name = clean_name(a);
                t.steps = std::stoi(b);
                t.when = c.empty() ? 0 : std::stod(c);
                if (t.steps > 0) d.scores.push_back(t);
            }
        }
    } catch (...) {
        return false;
    }
    d.has_puzzle = have_combo;
    out = d;
    return true;
}

bool qualifies(const std::vector<TopScore>& scores, int steps) {
    return scores.size() < kMaxScores || steps < scores.back().steps;
}

int add_score(std::vector<TopScore>& scores, const TopScore& s) {
    if (!qualifies(scores, s.steps)) return -1;
    auto it = std::upper_bound(scores.begin(), scores.end(), s, [](const TopScore& a, const TopScore& b) { return a.steps < b.steps; });
    const int place = static_cast<int>(it - scores.begin());
    scores.insert(it, s);
    if (scores.size() > kMaxScores) scores.pop_back();
    return place;
}

}  // namespace sbx
