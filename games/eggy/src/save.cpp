#include "save.hpp"
#include "runtime_paths.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace eggy {

namespace {
std::uint64_t fnv(const std::string& s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}
std::string clean_name(std::string n) {
    std::string out;
    for (char c : n)
        if (c >= 32 && c < 127 && c != '\t' && c != '\n') out += c;
    if (out.size() > 16) out.resize(16);
    return out.empty() ? "EGGY FAN" : out;
}
}  // namespace

std::filesystem::path save_path(bool dev) {
    std::filesystem::path base = games::state_directory();
    return base / (dev ? "eggy-dev-v1.txt" : "eggy-v1.txt");
}

bool write_save(const std::filesystem::path& path, const SaveData& d) {
    std::ostringstream o;
    o << std::setprecision(17);
    o << "seed=" << d.seed << "\nu=" << d.u << "\nv=" << d.v << "\nz=" << d.z << "\nbreath=" << d.breath
      << "\nelapsed=" << d.elapsed << "\nday=" << d.day_offset << "\nbest=" << d.best_v << "\nstart=" << d.start_wall
      << "\nlast=" << d.last_wall << "\nfinish=" << d.finish_seconds << "\nfinished=" << d.finished << "\nrecorded=" << d.recorded << "\nintro=" << d.intro_done
      << "\nmilestone=" << d.last_milestone << "\ncollected=";
    for (size_t i = 0; i < d.collected.size(); ++i) o << (i ? "," : "") << d.collected[i];
    const Settings& s = d.settings;
    o << "\nsound=" << s.sound << "\nmusic=" << s.music << "\nreduced=" << s.reduced_motion << "\nzoom=" << s.zoom
      << "\npixel=" << s.pixel << "\noffline=" << s.offline_climbing << "\nname=" << clean_name(s.player_name) << "\n";
    for (const TopScore& t : d.scores)
        o << "score=" << clean_name(t.name) << '\t' << t.seconds << '\t' << t.stars << '\t' << t.stars_total << "\n";
    std::string body = o.str();
    body = "EGGY1\n" + body + "check=" + std::to_string(fnv(body)) + "\n";
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
    std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (all.size() > 200000 || all.rfind("EGGY1\n", 0) != 0) return false;
    const size_t cpos = all.rfind("check=");
    if (cpos == std::string::npos) return false;
    const std::string body = all.substr(6, cpos - 6);
    if (std::to_string(fnv(body)) + "\n" != all.substr(cpos + 6)) return false;
    SaveData d;
    std::istringstream in(body);
    std::string line;
    try {
        while (std::getline(in, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "seed") d.seed = std::stoull(v);
            else if (k == "u") d.u = std::stod(v);
            else if (k == "v") d.v = std::stod(v);
            else if (k == "z") d.z = std::stod(v);
            else if (k == "breath") d.breath = std::clamp(std::stod(v), 0.0, 1.0);
            else if (k == "elapsed") d.elapsed = std::stod(v);
            else if (k == "day") d.day_offset = std::stod(v);
            else if (k == "best") d.best_v = std::stod(v);
            else if (k == "start") d.start_wall = std::stod(v);
            else if (k == "last") d.last_wall = std::stod(v);
            else if (k == "finish") d.finish_seconds = std::stod(v);
            else if (k == "finished") d.finished = v == "1";
            else if (k == "recorded") d.recorded = v == "1";
            else if (k == "intro") d.intro_done = v == "1";
            else if (k == "milestone") d.last_milestone = std::stoi(v);
            else if (k == "collected") {
                std::istringstream cs(v);
                std::string tok;
                while (std::getline(cs, tok, ',')) if (!tok.empty()) d.collected.push_back(std::stoi(tok));
            } else if (k == "sound") d.settings.sound = v == "1";
            else if (k == "music") d.settings.music = v == "1";
            else if (k == "reduced") d.settings.reduced_motion = v == "1";
            else if (k == "zoom") d.settings.zoom = std::clamp(std::stod(v), .6, 1.8);
            else if (k == "pixel") d.settings.pixel = std::clamp(std::stoi(v), 2, 4);
            else if (k == "offline") d.settings.offline_climbing = v == "1";
            else if (k == "name") d.settings.player_name = clean_name(v);
            else if (k == "score") {
                std::istringstream ss(v);
                TopScore t;
                std::string a, b, c, e;
                std::getline(ss, a, '\t'); std::getline(ss, b, '\t'); std::getline(ss, c, '\t'); std::getline(ss, e, '\t');
                t.name = clean_name(a); t.seconds = std::stod(b); t.stars = std::stoi(c); t.stars_total = std::stoi(e);
                if (d.scores.size() < 10) d.scores.push_back(t);
            }
        }
    } catch (...) {
        return false;
    }
    if (!std::isfinite(d.v) || d.v < 0 || !std::isfinite(d.u) || d.collected.size() > 5000) return false;
    out = d;
    return true;
}

void capture(const Sim& s, SaveData& d) {
    d.seed = s.world.seed();
    d.u = s.d.u; d.v = s.d.v; d.z = s.d.z; d.breath = s.d.breath;
    d.elapsed = s.elapsed; d.day_offset = s.day_offset; d.best_v = s.best_v;
    d.finished = s.finished; d.last_milestone = s.last_milestone;
    d.collected.clear();
    for (size_t i = 0; i < s.collected.size(); ++i) if (s.collected[i]) d.collected.push_back(static_cast<int>(i));
}

void restore(Sim& s, const SaveData& d) {
    s.place_at(d.v);
    if (d.u > 0 && d.u < kWidth) { s.d.u = d.u; s.d.z = s.world.ground(s.d.u, s.d.v); }
    s.d.breath = d.breath;
    s.elapsed = d.elapsed;
    s.day_offset = d.day_offset;
    s.best_v = std::max(d.best_v, s.d.v);
    s.finished = d.finished;
    s.last_milestone = d.last_milestone;
    s.last_biome = s.world.row(static_cast<std::int64_t>(s.d.v)).biome;
    for (int i : d.collected)
        if (i >= 0 && static_cast<size_t>(i) < s.collected.size() && !s.collected[static_cast<size_t>(i)]) { s.collected[static_cast<size_t>(i)] = 1; ++s.stars_collected; }
}

bool add_score(std::vector<TopScore>& scores, const TopScore& t) {
    auto better = [](const TopScore& a, const TopScore& b) {
        if (a.seconds != b.seconds) return a.seconds < b.seconds;
        return a.stars > b.stars;
    };
    size_t rank = 0;
    while (rank < scores.size() && !better(t, scores[rank])) ++rank;
    if (rank >= 10) return false;
    scores.insert(scores.begin() + static_cast<long>(rank), t);
    if (scores.size() > 10) scores.resize(10);
    return true;
}

std::string format_duration(double s) {
    s = std::max(0.0, s);
    const long long t = static_cast<long long>(s);
    const long long y = t / 31557600, d = (t % 31557600) / 86400, h = (t % 86400) / 3600, m = (t % 3600) / 60, sec = t % 60;
    std::ostringstream o;
    if (y) o << y << "y ";
    if (y || d) o << d << "d ";
    o << std::setw(2) << std::setfill('0') << h << ":" << std::setw(2) << m << ":" << std::setw(2) << sec;
    return o.str();
}

}  // namespace eggy
