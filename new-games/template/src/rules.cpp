#include "rules.hpp"

#include <algorithm>
#include <cstddef>
#include <sstream>

namespace tg {
namespace {

void flip(Board& board, int row, int column) {
    if (row < 0 || column < 0 || row >= board.side || column >= board.side) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(row * board.side + column);
    board.lit[index] = board.lit[index] == 0 ? 1 : 0;
}

void flip_cross(Board& board, int cell) {
    const int row = cell / board.side;
    const int column = cell % board.side;
    flip(board, row, column);
    flip(board, row - 1, column);
    flip(board, row + 1, column);
    flip(board, row, column - 1);
    flip(board, row, column + 1);
}

// Reads a non-negative decimal integer no larger than `limit`. Rejects anything else.
bool read_number(const std::string& text, std::uint64_t limit, std::uint64_t& value) {
    if (text.empty() || text.size() > 20) {
        return false;
    }
    std::uint64_t total = 0;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char character = text[index];
        if (character < '0' || character > '9') {
            return false;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
        if (digit > limit || total > (limit - digit) / 10) {
            return false;
        }
        total = total * 10 + digit;
    }
    value = total;
    return true;
}

}  // namespace

std::uint64_t next_random(std::uint64_t& state) {
    state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t mixed = state;
    mixed = (mixed ^ (mixed >> 30)) * 0xBF58476D1CE4E5B9ULL;
    mixed = (mixed ^ (mixed >> 27)) * 0x94D049BB133111EBULL;
    mixed = mixed ^ (mixed >> 31);
    return mixed;
}

Board new_game(int side, std::uint64_t seed) {
    Board board;
    board.side = std::clamp(side, minimum_side, maximum_side);
    board.seed = seed;
    const int cells = board.side * board.side;
    board.lit.assign(static_cast<std::size_t>(cells), 1);
    std::uint64_t state = seed;
    // Every press is its own inverse, so a board reached by presses can be solved by
    // repeating them. Keep pressing until the board is not already solved.
    const int presses = cells;
    for (int count = 0; count < presses || solved(board); ++count) {
        const std::uint64_t pick = next_random(state) % static_cast<std::uint64_t>(cells);
        flip_cross(board, static_cast<int>(pick));
    }
    return board;
}

bool valid_cell(const Board& board, int cell) {
    return cell >= 0 && cell < board.side * board.side;
}

bool press(Board& board, int cell) {
    if (!valid_cell(board, cell) || solved(board)) {
        return false;
    }
    flip_cross(board, cell);
    board.moves.push_back(cell);
    if (solved(board)) {
        const int count = static_cast<int>(board.moves.size());
        if (board.best == 0 || count < board.best) {
            board.best = count;
        }
    }
    return true;
}

bool undo(Board& board) {
    if (board.moves.empty()) {
        return false;
    }
    const int cell = board.moves.back();
    board.moves.pop_back();
    flip_cross(board, cell);
    return true;
}

bool solved(const Board& board) {
    return lit_count(board) == board.side * board.side;
}

int lit_count(const Board& board) {
    int count = 0;
    for (std::size_t index = 0; index < board.lit.size(); ++index) {
        count += board.lit[index] != 0 ? 1 : 0;
    }
    return count;
}

std::string encode(const Board& board) {
    std::ostringstream out;
    out << "side=" << board.side << "\n";
    out << "seed=" << board.seed << "\n";
    out << "best=" << board.best << "\n";
    out << "moves=";
    for (std::size_t index = 0; index < board.moves.size(); ++index) {
        if (index > 0) {
            out << ",";
        }
        out << board.moves[index];
    }
    out << "\n";
    return out.str();
}

bool decode(const std::string& text, Board& destination) {
    std::uint64_t side = 0;
    std::uint64_t seed = 0;
    std::uint64_t best = 0;
    std::string moves_text;
    bool have_side = false;
    bool have_seed = false;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        if (key == "side") {
            have_side = read_number(value, static_cast<std::uint64_t>(maximum_side), side);
            if (!have_side || side < static_cast<std::uint64_t>(minimum_side)) {
                return false;
            }
        } else if (key == "seed") {
            have_seed = read_number(value, UINT64_MAX, seed);
            if (!have_seed) {
                return false;
            }
        } else if (key == "best") {
            if (!read_number(value, 100000, best)) {
                return false;
            }
        } else if (key == "moves") {
            moves_text = value;
        }
    }
    if (!have_side || !have_seed) {
        return false;
    }
    // Rebuild the position by replaying the presses on the seeded board, so a save can
    // never describe a position the rules could not reach.
    Board rebuilt = new_game(static_cast<int>(side), seed);
    rebuilt.best = static_cast<int>(best);
    std::istringstream cells(moves_text);
    std::string item;
    int replayed = 0;
    while (std::getline(cells, item, ',')) {
        std::uint64_t cell = 0;
        if (!read_number(item, side * side - 1, cell)) {
            return false;
        }
        if (replayed >= 10000 || !press(rebuilt, static_cast<int>(cell))) {
            return false;
        }
        ++replayed;
    }
    destination = rebuilt;
    return true;
}

std::string encode_session(const Session& session) {
    std::string text = encode(session.board);
    text += "next_side=" + std::to_string(session.next_side) + "\n";
    return text;
}

bool decode_session(const std::string& text, Session& destination) {
    Session loaded;
    if (!decode(text, loaded.board)) {
        return false;
    }
    loaded.next_side = loaded.board.side;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.rfind("next_side=", 0) != 0) {
            continue;
        }
        std::uint64_t side = 0;
        if (!read_number(line.substr(10), static_cast<std::uint64_t>(maximum_side), side) ||
            side < static_cast<std::uint64_t>(minimum_side)) {
            return false;
        }
        loaded.next_side = static_cast<int>(side);
    }
    destination = loaded;
    return true;
}

}  // namespace tg
