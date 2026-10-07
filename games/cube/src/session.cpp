#include "session.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace ps_cube {
namespace {

bool parse_int(const std::string& text, int minimum, int maximum, int& value) {
    int parsed = 0;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(first, last, parsed);
    if (result.ec != std::errc() || result.ptr != last || parsed < minimum || parsed > maximum) {
        return false;
    }
    value = parsed;
    return true;
}

bool parse_u64(const std::string& text, std::uint64_t& value) {
    std::uint64_t parsed = 0;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(first, last, parsed);
    if (result.ec != std::errc() || result.ptr != last || text.empty()) {
        return false;
    }
    value = parsed;
    return true;
}

// Splits on a separator, keeping empty fields.
std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> fields;
    std::string field;
    for (char c : text) {
        if (c == separator) {
            fields.push_back(field);
            field.clear();
        } else {
            field.push_back(c);
        }
    }
    fields.push_back(field);
    return fields;
}

bool parse_cells(const std::string& text, int cells, std::vector<int>& out) {
    out.clear();
    if (text.empty()) {
        return true;
    }
    const std::vector<std::string> fields = split(text, ' ');
    for (const std::string& field : fields) {
        int cell = 0;
        if (!parse_int(field, 0, cells - 1, cell)) {
            return false;
        }
        out.push_back(cell);
    }
    return true;
}

bool parse_lines(const std::string& text, int cells, std::size_t count,
                 std::vector<std::vector<int>>& out) {
    out.clear();
    const std::vector<std::string> fields = split(text, ';');
    if (fields.size() != count) {
        return false;
    }
    for (const std::string& field : fields) {
        std::vector<int> line;
        if (!parse_cells(field, cells, line)) {
            return false;
        }
        out.push_back(line);
    }
    return true;
}

std::string cells_text(const std::vector<int>& cells) {
    std::string text;
    for (std::size_t index = 0; index < cells.size(); ++index) {
        if (index > 0) {
            text += ' ';
        }
        text += std::to_string(cells[index]);
    }
    return text;
}

std::string lines_text(const std::vector<std::vector<int>>& lines) {
    std::string text;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index > 0) {
            text += ';';
        }
        text += cells_text(lines[index]);
    }
    return text;
}

struct ScoreOrder {
    bool operator()(const TopScore& a, const TopScore& b) const {
        return a.strokes < b.strokes;
    }
};

// The shared puzzle engine numbered sixteen cells on each of six faces; the playable
// faces were 0 (front), 3 (left) and 4 (top). Returns -1 for any other cell.
int legacy_cell(int old_cell) {
    if (old_cell < 0 || old_cell >= 96) {
        return -1;
    }
    const int face = old_cell / 16;
    const int slot = face == 0 ? 0 : face == 3 ? 1 : face == 4 ? 2 : -1;
    if (slot < 0) {
        return -1;
    }
    const int cell = slot * 16 + old_cell % 16;
    return cell;
}

bool legacy_line(const std::vector<int>& old_line, std::vector<int>& line) {
    line.clear();
    for (int old_cell : old_line) {
        const int cell = legacy_cell(old_cell);
        if (cell < 0) {
            return false;
        }
        line.push_back(cell);
    }
    return true;
}

}  // namespace

bool valid_name(const std::string& name) {
    if (name.empty() || name.size() > 96) {
        return false;
    }
    bool visible = false;
    for (unsigned char c : name) {
        if (c < 32 || c == 127) {
            return false;
        }
        visible = visible || c != ' ';
    }
    return visible;
}

bool qualifies(const std::vector<TopScore>& scores, int strokes) {
    if (strokes <= 0) {
        return false;
    }
    const bool fits = scores.size() < score_table_size || strokes < scores.back().strokes;
    return fits;
}

bool record(std::vector<TopScore>& scores, const std::string& name, int strokes) {
    if (!valid_name(name) || !qualifies(scores, strokes)) {
        return false;
    }
    std::vector<TopScore>::iterator position = scores.begin();
    while (position != scores.end() && (*position).strokes <= strokes) {
        ++position;
    }
    scores.insert(position, TopScore{name, strokes});
    if (scores.size() > score_table_size) {
        scores.pop_back();
    }
    return true;
}

bool replay(const Puzzle& puzzle, const std::vector<std::vector<int>>& lines, int strokes,
            Play& destination) {
    if (lines.size() != puzzle.ends.size() || strokes < 0) {
        return false;
    }
    Play play = fresh_play(puzzle);
    for (std::size_t pair = 0; pair < lines.size(); ++pair) {
        const std::vector<int>& line = lines[pair];
        if (line.empty()) {
            continue;
        }
        if (puzzle.pair_of[static_cast<std::size_t>(line.front())] != static_cast<int>(pair) ||
            !press(puzzle, play, line.front())) {
            return false;
        }
        for (std::size_t index = 1; index < line.size(); ++index) {
            if (play.paths[pair].size() > index) {
                // The far side of a portal arrives with the hop.
                if (play.paths[pair][index] != line[index]) {
                    return false;
                }
                continue;
            }
            if (!extend(puzzle, play, line[index])) {
                return false;
            }
        }
        if (play.paths[pair] != line) {
            return false;
        }
        release(play);
    }
    play.strokes = strokes;
    play.won = all_connected(puzzle, play);
    play.active = -1;
    destination = play;
    return true;
}

std::string encode_session(const Session& session) {
    const Puzzle& puzzle = session.puzzle;
    std::ostringstream out;
    out << "level=" << puzzle.level << '\n';
    out << "next_level=" << session.next_level << '\n';
    out << "portals_met=" << (session.portals_met ? 1 : 0) << '\n';
    out << "seed=" << puzzle.seed << '\n';
    out << "side=" << puzzle.side << '\n';
    std::vector<int> stones;
    for (int cell = 0; cell < puzzle.geometry.cells; ++cell) {
        if (puzzle.tiles[static_cast<std::size_t>(cell)] == Tile::stone) {
            stones.push_back(cell);
        }
    }
    out << "stones=" << cells_text(stones) << '\n';
    out << "pairs=" << puzzle.ends.size() << '\n';
    std::vector<std::vector<int>> ends;
    for (const std::array<int, 2>& pair : puzzle.ends) {
        ends.push_back({pair[0], pair[1]});
    }
    out << "ends=" << lines_text(ends) << '\n';
    std::vector<std::vector<int>> portals;
    for (const std::array<int, 2>& portal : puzzle.portals) {
        portals.push_back({portal[0], portal[1]});
    }
    out << "portal_pairs=" << puzzle.portals.size() << '\n';
    out << "portals=" << lines_text(portals) << '\n';
    out << "witness=" << lines_text(puzzle.witness) << '\n';
    out << "paths=" << lines_text(session.play.paths) << '\n';
    out << "strokes=" << session.play.strokes << '\n';
    out << "pending=" << session.pending << '\n';
    out << "player=" << session.player << '\n';
    for (const TopScore& score : session.scores) {
        out << "score=" << score.strokes << ' ' << score.name << '\n';
    }
    return out.str();
}

bool decode_session(const std::string& text, Session& destination) {
    std::vector<std::pair<std::string, std::string>> fields;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            return false;
        }
        fields.push_back({line.substr(0, equals), line.substr(equals + 1)});
    }
    Session session;
    int level = -1;
    int side = 0;
    int pairs = -1;
    int portal_pairs = -1;
    int strokes = -1;
    int flag = 0;
    bool have_seed = false;
    bool have_player = false;
    std::string stones_text;
    std::string ends_text;
    std::string portals_text;
    std::string witness_text;
    std::string paths_text;
    for (const std::pair<std::string, std::string>& field : fields) {
        const std::string& key = field.first;
        const std::string& value = field.second;
        bool ok = true;
        if (key == "level") {
            ok = parse_int(value, 0, 2, level);
        } else if (key == "next_level") {
            ok = parse_int(value, 0, 2, session.next_level);
        } else if (key == "portals_met") {
            ok = parse_int(value, 0, 1, flag);
            session.portals_met = flag == 1;
        } else if (key == "seed") {
            ok = parse_u64(value, session.puzzle.seed);
            have_seed = true;
        } else if (key == "side") {
            ok = parse_int(value, minimum_side, maximum_side, side);
        } else if (key == "pairs") {
            ok = parse_int(value, 1, maximum_pairs, pairs);
        } else if (key == "portal_pairs") {
            ok = parse_int(value, 0, maximum_portals, portal_pairs);
        } else if (key == "stones") {
            stones_text = value;
        } else if (key == "ends") {
            ends_text = value;
        } else if (key == "portals") {
            portals_text = value;
        } else if (key == "witness") {
            witness_text = value;
        } else if (key == "paths") {
            paths_text = value;
        } else if (key == "strokes") {
            ok = parse_int(value, 0, 1000000, strokes);
        } else if (key == "pending") {
            ok = parse_int(value, -1, 1000000, session.pending);
        } else if (key == "player") {
            ok = valid_name(value);
            session.player = value;
            have_player = true;
        } else if (key == "score") {
            const std::size_t space = value.find(' ');
            TopScore score;
            ok = space != std::string::npos &&
                 parse_int(value.substr(0, space), 1, 1000000, score.strokes);
            score.name = space != std::string::npos ? value.substr(space + 1) : "";
            ok = ok && valid_name(score.name) && session.scores.size() < score_table_size;
            session.scores.push_back(score);
        } else {
            ok = false;
        }
        if (!ok) {
            return false;
        }
    }
    if (level < 0 || side == 0 || pairs < 0 || portal_pairs < 0 || strokes < 0 || !have_seed ||
        !have_player) {
        return false;
    }
    if (!std::is_sorted(session.scores.begin(), session.scores.end(), ScoreOrder{})) {
        return false;
    }
    Puzzle& puzzle = session.puzzle;
    puzzle.side = side;
    puzzle.level = level;
    const int cells = faces * side * side;
    std::vector<int> stones;
    std::vector<std::vector<int>> ends;
    std::vector<std::vector<int>> portals;
    std::vector<std::vector<int>> paths;
    if (!parse_cells(stones_text, cells, stones) ||
        !parse_lines(ends_text, cells, static_cast<std::size_t>(pairs), ends) ||
        !parse_lines(witness_text, cells, static_cast<std::size_t>(pairs), puzzle.witness) ||
        !parse_lines(paths_text, cells, static_cast<std::size_t>(pairs), paths)) {
        return false;
    }
    if (portal_pairs > 0 &&
        !parse_lines(portals_text, cells, static_cast<std::size_t>(portal_pairs), portals)) {
        return false;
    }
    if (portal_pairs == 0 && !portals_text.empty()) {
        return false;
    }
    for (const std::vector<int>& pair : ends) {
        if (pair.size() != 2) {
            return false;
        }
        puzzle.ends.push_back({pair[0], pair[1]});
    }
    for (const std::vector<int>& portal : portals) {
        if (portal.size() != 2) {
            return false;
        }
        puzzle.portals.push_back({portal[0], portal[1]});
    }
    if (!assemble(puzzle, stones) || !witness_valid(puzzle) ||
        !replay(puzzle, paths, strokes, session.play)) {
        return false;
    }
    destination = session;
    return true;
}

std::uint64_t checksum(const std::string& body) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t index = 0; index < body.size(); ++index) {
        hash ^= static_cast<unsigned char>(body[index]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string seal(const std::string& body) {
    std::string sealed = std::string(save_magic) + "\n" + body;
    sealed += "check=" + std::to_string(checksum(body)) + "\n";
    return sealed;
}

bool unseal(const std::string& sealed, std::string& body) {
    const std::string header = std::string(save_magic) + "\n";
    if (sealed.size() > save_limit_bytes || sealed.rfind(header, 0) != 0) {
        return false;
    }
    const std::size_t check_at = sealed.rfind("check=");
    if (check_at == std::string::npos || check_at < header.size()) {
        return false;
    }
    const std::string candidate = sealed.substr(header.size(), check_at - header.size());
    const std::string expected = std::to_string(checksum(candidate)) + "\n";
    if (sealed.substr(check_at + 6) != expected) {
        return false;
    }
    body = candidate;
    return true;
}

bool write_save(const std::filesystem::path& path, const std::string& body) {
    const std::string sealed = seal(body);
    if (sealed.size() > save_limit_bytes) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file << sealed;
        file.flush();
        if (!file) {
            return false;
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

bool read_file(const std::filesystem::path& path, std::string& text) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::string contents(save_limit_bytes + 1, '\0');
    file.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    contents.resize(static_cast<std::size_t>(file.gcount()));
    if (contents.size() > save_limit_bytes) {
        return false;
    }
    text = contents;
    return true;
}

bool read_save(const std::filesystem::path& path, std::string& body) {
    std::string sealed;
    if (!read_file(path, sealed)) {
        return false;
    }
    const bool ok = unseal(sealed, body);
    return ok;
}

bool import_legacy(const std::string& file_text, bool board, Session& destination) {
    std::istringstream file(file_text);
    std::string magic;
    int version = 0;
    std::uint64_t hash = 0;
    file >> magic >> version >> hash;
    if (!file || magic != "RAINSTAR_PUZZLE" || version != 1) {
        return false;
    }
    file.get();
    std::ostringstream raw;
    raw << file.rdbuf();
    if (checksum(raw.str()) != hash) {
        return false;
    }
    std::istringstream in(raw.str());
    int kind = -1;
    std::uint32_t seed = 0;
    std::uint32_t random_state = 0;
    int moves = 0;
    int score = 0;
    int stage = 0;
    int progress = 0;
    int over = 0;
    int won = 0;
    int recorded = 0;
    std::string player;
    in >> kind >> seed >> random_state >> moves >> score >> stage >> progress >> over >> won >>
        recorded >> std::quoted(player);
    // Kind 1 was Nature Cube in the shared engine.
    if (!in || kind != 1 || !valid_name(player) || moves < 0 || moves > 10000000) {
        return false;
    }
    std::array<int, 96> grid{};
    std::array<int, 96> secret{};
    std::array<int, 96> marks{};
    std::array<int, 96> aux{};
    for (int index = 0; index < 96; ++index) {
        in >> grid[static_cast<std::size_t>(index)] >> secret[static_cast<std::size_t>(index)] >>
            marks[static_cast<std::size_t>(index)] >> aux[static_cast<std::size_t>(index)];
    }
    // Untangle's nodes, embedding and edges; empty for the cube but present in the file.
    for (int block = 0; block < 3; ++block) {
        int count = 0;
        in >> count;
        if (!in || count < 0 || count > 300) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            double a = 0;
            double b = 0;
            in >> a >> b;
        }
    }
    std::vector<std::vector<int>> old_lines[2];
    for (int which = 0; which < 2; ++which) {
        int count = 0;
        in >> count;
        if (!in || count < 0 || count > 9) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            int size = 0;
            in >> size;
            if (!in || size < 0 || size > 96) {
                return false;
            }
            std::vector<int> line;
            for (int step = 0; step < size; ++step) {
                int cell = 0;
                in >> cell;
                line.push_back(cell);
            }
            old_lines[which].push_back(line);
        }
    }
    int score_count = 0;
    in >> score_count;
    if (!in || score_count < 0 || score_count > 10) {
        return false;
    }
    Session session = destination;
    session.scores.clear();
    for (int index = 0; index < score_count; ++index) {
        TopScore entry;
        in >> std::quoted(entry.name) >> entry.strokes;
        if (!in || !valid_name(entry.name) || entry.strokes < 1 || entry.strokes > 100000000) {
            return false;
        }
        session.scores.push_back(entry);
    }
    std::stable_sort(session.scores.begin(), session.scores.end(), ScoreOrder{});
    session.player = player;
    if (!board) {
        destination.scores = session.scores;
        destination.player = session.player;
        return true;
    }
    // The board: four cells to a face, pairs marked 1..9 on their endpoints, stones as 1.
    Puzzle puzzle;
    puzzle.side = 4;
    puzzle.level = std::clamp(aux[94], 0, 2);
    puzzle.seed = seed;
    std::vector<int> stones;
    for (int old_cell = 0; old_cell < 96; ++old_cell) {
        if (grid[static_cast<std::size_t>(old_cell)] == 1) {
            const int cell = legacy_cell(old_cell);
            if (cell < 0) {
                return false;
            }
            stones.push_back(cell);
        }
    }
    const std::vector<std::vector<int>>& old_paths = old_lines[0];
    const std::vector<std::vector<int>>& old_witness = old_lines[1];
    if (old_witness.size() < 3 || old_paths.size() != old_witness.size()) {
        return false;
    }
    for (std::size_t pair = 0; pair < old_witness.size(); ++pair) {
        std::vector<int> line;
        if (!legacy_line(old_witness[pair], line) || line.size() < 2) {
            return false;
        }
        const int mark = static_cast<int>(pair) + 1;
        const int old_front = old_witness[pair].front();
        const int old_back = old_witness[pair].back();
        if (marks[static_cast<std::size_t>(old_front)] != mark ||
            marks[static_cast<std::size_t>(old_back)] != mark) {
            return false;
        }
        puzzle.ends.push_back({line.front(), line.back()});
        puzzle.witness.push_back(line);
    }
    if (!assemble(puzzle, stones) || !witness_valid(puzzle)) {
        return false;
    }
    std::vector<std::vector<int>> lines;
    for (const std::vector<int>& old_line : old_paths) {
        std::vector<int> line;
        if (!legacy_line(old_line, line)) {
            return false;
        }
        lines.push_back(line);
    }
    Play play;
    if (!replay(puzzle, lines, moves, play)) {
        return false;
    }
    session.puzzle = puzzle;
    session.play = play;
    session.next_level = puzzle.level;
    // A board finished under the old rules (or now finished because filling no longer
    // counts) offers its result once, unless it was already entered.
    session.pending = play.won && recorded == 0 && qualifies(session.scores, moves) ? moves : -1;
    destination = session;
    return true;
}

}  // namespace ps_cube
