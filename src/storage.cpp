#include "storage.hpp"
#include "runtime_paths.hpp"
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
namespace games {
namespace {
std::uint64_t checksum(const std::string& text) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}
void write_state(std::ostream& out, const State& s) {
    out << static_cast<int>(s.kind) << ' ' << s.seed << ' ' << s.moves << ' ' << s.score << ' '
        << s.completed << ' ' << s.draw_count << ' ' << s.spider_suits << ' ' << s.turn << ' '
        << s.leader << ' ' << s.trick_number << ' ' << s.round << ' ' << s.hearts_broken << ' '
        << s.passing << ' ' << s.over << '\n';
    for (int n : s.points)
        out << n << ' ';
    for (int n : s.totals)
        out << n << ' ';
    out << '\n';
    for (const Pile& pile : s.piles) {
        out << pile.size();
        for (const Card& c : pile)
            out << ' ' << c.rank << ' ' << c.suit << ' ' << c.id << ' ' << c.up;
        out << '\n';
    }
}
bool read_state(std::istream& in, State& s, int expected) {
    int kind = 0;
    in >> kind >> s.seed >> s.moves >> s.score >> s.completed >> s.draw_count >> s.spider_suits >>
        s.turn >> s.leader >> s.trick_number >> s.round >> s.hearts_broken >> s.passing >> s.over;
    if (!in || kind != expected || s.moves < 0 || s.completed < 0 || s.completed > 8 ||
        s.turn < 0 || s.turn > 3 || s.leader < 0 || s.leader > 3 || s.trick_number < 0 ||
        s.trick_number > 13 || s.round < 0 || s.round > 100000 ||
        (s.draw_count != 1 && s.draw_count != 3) ||
        (s.spider_suits != 1 && s.spider_suits != 2 && s.spider_suits != 4))
        return false;
    s.kind = static_cast<Kind>(kind);
    for (int& n : s.points) {
        in >> n;
        if (n < 0 || n > 26)
            return false;
    }
    for (int& n : s.totals) {
        in >> n;
        if (n < 0 || n > 1000000)
            return false;
    }
    int total = 0;
    for (Pile& pile : s.piles) {
        int count = 0;
        in >> count;
        if (!in || count < 0 || count > 104 || total + count > 104)
            return false;
        total += count;
        for (int i = 0; i < count; ++i) {
            Card c;
            in >> c.rank >> c.suit >> c.id >> c.up;
            if (!in)
                return false;
            pile.push_back(c);
        }
    }
    Game validator;
    validator.state = s;
    return validator.invariant();
}
} // namespace
bool save_cabinet(const std::filesystem::path& path, const Cabinet& cabinet) {
    std::ostringstream payload;
    payload << cabinet.active << ' ' << cabinet.back << ' ' << cabinet.reduced << ' '
            << cabinet.sound << ' ' << cabinet.music << '\n';
    for (int i = 0; i < 4; ++i) {
        payload << cabinet.started[i] << '\n';
        if (cabinet.started[i])
            write_state(payload, cabinet.games[i].state);
    }
    for (bool recorded : cabinet.result_recorded)
        payload << recorded << ' ';
    payload << '\n' << std::quoted(cabinet.player_name) << '\n';
    for (const std::vector<TopScore>& list : cabinet.top_scores) {
        payload << list.size() << '\n';
        for (const TopScore& score : list)
            payload << std::quoted(score.name) << ' ' << score.value << '\n';
    }
    std::string data = payload.str();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        return false;
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out << "RAINSTAR_GAMES 2 " << checksum(data) << '\n' << data;
    out.close();
    if (!out)
        return false;
    std::filesystem::rename(temporary, path, error);
    return !error;
}
bool load_cabinet(const std::filesystem::path& path, Cabinet& destination) {
    std::error_code error;
    std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > 65536)
        return false;
    std::ifstream file(path, std::ios::binary);
    std::string magic;
    int version = 0;
    std::uint64_t expected = 0;
    file >> magic >> version >> expected;
    if (!file || magic != "RAINSTAR_GAMES" || (version != 1 && version != 2))
        return false;
    file.get();
    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string data = buffer.str();
    if (checksum(data) != expected)
        return false;
    std::istringstream in(data);
    Cabinet candidate;
    in >> candidate.active >> candidate.back >> candidate.reduced >> candidate.sound >>
        candidate.music;
    if (!in || candidate.active < 0 || candidate.active > 3 || candidate.back < 0 ||
        candidate.back > 3)
        return false;
    for (int i = 0; i < 4; ++i) {
        in >> candidate.started[i];
        if (!in)
            return false;
        if (candidate.started[i]) {
            if (!read_state(in, candidate.games[i].state, i))
                return false;
            candidate.games[i].message = "Welcome back. Your table was saved.";
        }
    }
    if (!candidate.started[candidate.active])
        return false;
    if (version >= 2) {
        for (bool& recorded : candidate.result_recorded)
            in >> recorded;
        in >> std::quoted(candidate.player_name);
        if (!in || !valid_score_name(candidate.player_name))
            return false;
        for (int profile = 0; profile < 7; ++profile) {
            int count = 0;
            in >> count;
            if (!in || count < 0 || count > 10)
                return false;
            for (int i = 0; i < count; ++i) {
                TopScore score;
                in >> std::quoted(score.name) >> score.value;
                if (!in || !valid_score_name(score.name) || (profile >= 2 && score.value < 0))
                    return false;
                const std::vector<TopScore>& list = candidate.top_scores[profile];
                if (!list.empty() && score_is_better(profile, score.value, list.back().value))
                    return false;
                candidate.top_scores[profile].push_back(score);
            }
        }
    }
    in >> std::ws;
    if (!in.eof())
        return false;
    destination = std::move(candidate);
    return true;
}
std::filesystem::path cabinet_path() {
    return state_directory() / "cabinet-v1.txt";
}
} // namespace games
