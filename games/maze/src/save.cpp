#include "save.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace mz {

namespace {
const char kHeader[] = "MAZE1\n";
std::uint64_t fnv(const std::string& s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}
}  // namespace

bool write_save(const std::filesystem::path& path, const SaveData& d) {
    std::ostringstream o;
    o << "level=" << d.level << "\nseed=" << d.run_seed << "\ncollected=" << d.collected << "\ncleared=" << d.cleared << "\nsteps=" << d.steps
      << "\nperfect=" << d.perfect << "\nsound=" << d.sound << "\nmusic=" << d.music << "\n";
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
    if (all.size() > 16 * 1024 || all.rfind(kHeader, 0) != 0) return false;
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
            if (k == "level") d.level = std::max(1, std::min(100000, std::stoi(v)));
            else if (k == "seed") d.run_seed = std::stoull(v);
            else if (k == "collected") d.collected = std::stoull(v);
            else if (k == "cleared") d.cleared = std::stoi(v);
            else if (k == "steps") d.steps = std::stoll(v);
            else if (k == "perfect") d.perfect = std::stoi(v);
            else if (k == "sound") d.sound = v == "1";
            else if (k == "music") d.music = v == "1";
        }
    } catch (...) {
        return false;
    }
    out = d;
    return true;
}

}  // namespace mz
