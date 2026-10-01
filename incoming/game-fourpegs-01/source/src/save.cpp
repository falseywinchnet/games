#include "save.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fp {

namespace {
constexpr size_t kMaxScores = 10;

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
std::string code_str(const Code& c) {
    std::string s;
    for (int v : c) s += static_cast<char>('0' + (v < 0 ? 9 : v));
    return s;
}
bool parse_code(const std::string& s, Code& c) {
    if (s.size() != static_cast<size_t>(kPegs)) return false;
    for (int i = 0; i < kPegs; ++i) {
        const int v = s[static_cast<size_t>(i)] - '0';
        if (v == 9) c[static_cast<size_t>(i)] = -1;
        else if (v >= 0 && v < kColors) c[static_cast<size_t>(i)] = v;
        else return false;
    }
    return true;
}
}  // namespace

// v2: four_pegs-v1.txt belongs to the earlier Four Pegs and is never touched.
std::filesystem::path save_path(bool dev) {
    const char* name = dev ? "four_pegs-dev-v2.txt" : "four_pegs-v2.txt";
    if (const char* dir = std::getenv("GAMES_STATE_DIR")) return std::filesystem::path(dir) / name;
    const char* home = std::getenv("HOME");
    std::filesystem::path base = home ? home : ".";
    return base / "Library/Application Support/Rainstar/Games" / name;
}

bool write_save(const std::filesystem::path& path, const SaveData& d) {
    std::ostringstream o;
    if (d.has_board) {
        o << "rng=" << d.board.rng << "\nsecret=" << code_str(d.board.secret) << "\ndraft=" << code_str(d.board.draft) << "\n";
        for (const Row& r : d.board.rows) o << "row=" << code_str(r.guess) << "\n";
        o << "plan=" << clean(d.plan, 120) << "\n";
    }
    o << "foiled=" << d.foiled << "\ntriumphs=" << d.triumphs << "\nsound=" << d.settings.sound << "\nmusic=" << d.settings.music
      << "\nname=" << clean(d.settings.player_name, 14) << "\n";
    for (const TopScore& t : d.scores) o << "score=" << clean(t.name, 14) << '\t' << t.guesses << '\t' << std::setprecision(15) << t.when << "\n";
    std::string body = o.str();
    body = "FOURPEGS2\n" + body + "check=" + std::to_string(fnv(body)) + "\n";
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
    if (all.size() > 64 * 1024 || all.rfind("FOURPEGS2\n", 0) != 0) return false;
    const size_t ck = all.rfind("check=");
    if (ck == std::string::npos) return false;
    const std::string body = all.substr(10, ck - 10);
    if (std::to_string(fnv(body)) + "\n" != all.substr(ck + 6)) return false;
    SaveData d;
    std::istringstream in(body);
    std::string line;
    bool have_secret = false;
    try {
        while (std::getline(in, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "rng") d.board.rng = std::stoull(v);
            else if (k == "secret") have_secret = parse_code(v, d.board.secret);
            else if (k == "draft") parse_code(v, d.board.draft);
            else if (k == "row") {
                Row r;
                if (!parse_code(v, r.guess)) return false;
                d.board.rows.push_back(r);
            }
            else if (k == "plan") d.plan = clean(v, 120);
            else if (k == "foiled") d.foiled = std::stoi(v);
            else if (k == "triumphs") d.triumphs = std::stoi(v);
            else if (k == "sound") d.settings.sound = v == "1";
            else if (k == "music") d.settings.music = v == "1";
            else if (k == "name") d.settings.player_name = clean(v, 14);
            else if (k == "score" && d.scores.size() < kMaxScores) {
                std::istringstream sc(v);
                TopScore t;
                std::string a, b, c;
                std::getline(sc, a, '\t'); std::getline(sc, b, '\t'); std::getline(sc, c, '\t');
                t.name = clean(a, 14);
                t.guesses = std::stoi(b);
                t.when = c.empty() ? 0 : std::stod(c);
                if (t.guesses > 0 && t.guesses <= kTurns) d.scores.push_back(t);
            }
        }
    } catch (...) {
        return false;
    }
    // scores are recomputed from the secret, so a saved board cannot carry doctored feedback
    if (have_secret)
        for (Row& r : d.board.rows) r.score = evaluate(d.board.secret, r.guess);
    d.has_board = have_secret;
    out = d;
    return true;
}

bool qualifies(const std::vector<TopScore>& scores, int guesses) { return scores.size() < kMaxScores || guesses < scores.back().guesses; }

int add_score(std::vector<TopScore>& scores, const TopScore& s) {
    if (!qualifies(scores, s.guesses)) return -1;
    auto it = std::upper_bound(scores.begin(), scores.end(), s, [](const TopScore& a, const TopScore& b) { return a.guesses < b.guesses; });
    const int place = static_cast<int>(it - scores.begin());
    scores.insert(it, s);
    if (scores.size() > kMaxScores) scores.pop_back();
    return place;
}

}  // namespace fp
