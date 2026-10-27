#include "save.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace ap {

namespace {
constexpr size_t kMaxScores = 10;
const char kHeader[] = "ATOMPROBE2\n";

std::uint64_t fnv(const std::string& s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}
std::string clean(const std::string& n, size_t max) {
    std::string out;
    for (char c : n)
        if (c >= 32 && c < 127 && c != '\t') out += c;
    if (out.size() > max) out.resize(max);
    return out;
}
}  // namespace

// v2: atom_probe-v1.txt belongs to the earlier 4x4 Atom Probe and is never touched.
std::filesystem::path save_path(bool dev) {
    const char* name = dev ? "atom_probe-dev-v2.txt" : "atom_probe-v2.txt";
    if (const char* dir = std::getenv("GAMES_STATE_DIR")) return std::filesystem::path(dir) / name;
    const char* home = std::getenv("HOME");
    std::filesystem::path base = home ? home : ".";
    return base / "Library/Application Support/Rainstar/Games" / name;
}

bool write_save(const std::filesystem::path& path, const SaveData& d) {
    std::ostringstream o;
    if (d.has_box) {
        o << "atoms=" << d.box.atoms << "\nmarks=" << d.box.marks << "\nempties=" << d.box.empties << "\nopen=" << d.box.opened << "\nfired=";
        for (size_t i = 0; i < d.box.fired.size(); ++i) o << (i ? "," : "") << d.box.fired[i];
        o << "\n";
    }
    o << "solved=" << d.solved << "\nopened=" << d.opened << "\nsound=" << d.settings.sound << "\nmusic=" << d.settings.music
      << "\nname=" << clean(d.settings.player_name, 14) << "\n";
    for (const TopScore& t : d.scores) o << "score=" << clean(t.name, 14) << '\t' << t.points << '\t' << std::setprecision(15) << t.when << "\n";
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
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string all = ss.str();
    const size_t hl = sizeof kHeader - 1;
    if (all.size() > 64 * 1024 || all.rfind(kHeader, 0) != 0) return false;
    const size_t ck = all.rfind("check=");
    if (ck == std::string::npos || ck < hl) return false;
    const std::string body = all.substr(hl, ck - hl);
    if (std::to_string(fnv(body)) + "\n" != all.substr(ck + 6)) return false;
    SaveData d;
    std::istringstream in(body);
    std::string line;
    bool have_atoms = false;
    try {
        while (std::getline(in, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "atoms") { d.box.atoms = std::stoull(v); have_atoms = true; }
            else if (k == "marks") d.box.marks = std::stoull(v);
            else if (k == "empties") d.box.empties = std::stoull(v);
            else if (k == "open") d.box.opened = v == "1";
            else if (k == "fired") {
                std::istringstream fs(v);
                std::string p;
                while (std::getline(fs, p, ','))
                    if (!p.empty() && d.box.fired.size() < kPorts) d.box.fired.push_back(std::stoi(p));
            }
            else if (k == "solved") d.solved = std::stoi(v);
            else if (k == "opened") d.opened = std::stoi(v);
            else if (k == "sound") d.settings.sound = v == "1";
            else if (k == "music") d.settings.music = v == "1";
            else if (k == "name") d.settings.player_name = clean(v, 14);
            else if (k == "score" && d.scores.size() < kMaxScores) {
                std::istringstream sc(v);
                TopScore t;
                std::string a, b, c;
                std::getline(sc, a, '\t'); std::getline(sc, b, '\t'); std::getline(sc, c, '\t');
                t.name = clean(a, 14);
                t.points = std::stoi(b);
                t.when = c.empty() ? 0 : std::stod(c);
                if (t.points > 0 && t.points <= 2 * kPorts) d.scores.push_back(t);
            }
        }
    } catch (...) {
        return false;
    }
    std::sort(d.scores.begin(), d.scores.end(), [](const TopScore& a, const TopScore& b) { return a.points < b.points; });
    d.has_box = have_atoms;
    out = d;
    return true;
}

bool qualifies(const std::vector<TopScore>& scores, int points) { return scores.size() < kMaxScores || points < scores.back().points; }

int add_score(std::vector<TopScore>& scores, const TopScore& s) {
    if (!qualifies(scores, s.points)) return -1;
    auto it = std::upper_bound(scores.begin(), scores.end(), s, [](const TopScore& a, const TopScore& b) { return a.points < b.points; });
    const int place = static_cast<int>(it - scores.begin());
    scores.insert(it, s);
    if (scores.size() > kMaxScores) scores.pop_back();
    return place;
}

}  // namespace ap
