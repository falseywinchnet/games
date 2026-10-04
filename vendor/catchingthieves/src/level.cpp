#include "level.hpp"

#include <algorithm>
#include <sstream>

namespace ct {

int dir_of(char c) {
    switch (c) {
        case 'u': case 'U': return kUp;
        case 'r': case 'R': return kRight;
        case 'd': case 'D': return kDown;
        case 'l': case 'L': return kLeft;
        default: return -1;
    }
}

int Level::step(int c, int d) const {
    const int x = c % w + kDX[d], y = c / w + kDY[d];
    return in(x, y) ? idx(x, y) : -1;
}

int Level::goals() const {
    int n = 0;
    for (std::uint8_t g : goal) n += g;
    return n;
}

bool Level::parse(const std::string& xsb, Level& out, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    std::vector<std::string> rows;
    std::istringstream in(xsb);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find_first_not_of(" ") == std::string::npos) { if (!rows.empty()) break; continue; }
        rows.push_back(line);
    }
    if (rows.empty()) return fail("empty level");
    Level lv;
    lv.h = static_cast<int>(rows.size());
    for (const std::string& r : rows) lv.w = std::max(lv.w, static_cast<int>(r.size()));
    if (lv.w > 40 || lv.h > 40) return fail("level too large");
    lv.tiles.assign(static_cast<size_t>(lv.w * lv.h), Tile::outside);
    lv.goal.assign(static_cast<size_t>(lv.w * lv.h), 0);
    for (int y = 0; y < lv.h; ++y)
        for (int x = 0; x < static_cast<int>(rows[static_cast<size_t>(y)].size()); ++x) {
            const char c = rows[static_cast<size_t>(y)][static_cast<size_t>(x)];
            const int i = lv.idx(x, y);
            switch (c) {
                case '#': lv.tiles[static_cast<size_t>(i)] = Tile::wall; break;
                case ' ': case '-': case '_': break;  // floor or outside: decided by the flood below
                case '.': lv.goal[static_cast<size_t>(i)] = 1; break;
                case '$': lv.boxes.push_back(i); break;
                case '*': lv.boxes.push_back(i); lv.goal[static_cast<size_t>(i)] = 1; break;
                case '@': if (lv.player >= 0) return fail("two players"); lv.player = i; break;
                case '+': if (lv.player >= 0) return fail("two players"); lv.player = i; lv.goal[static_cast<size_t>(i)] = 1; break;
                default: return fail("unknown character");
            }
        }
    if (lv.player < 0) return fail("no player");
    // the floor is everything the player can reach without crossing a hedge
    std::vector<int> stack{lv.player};
    lv.tiles[static_cast<size_t>(lv.player)] = Tile::floor;
    while (!stack.empty()) {
        const int c = stack.back();
        stack.pop_back();
        for (int d = 0; d < 4; ++d) {
            const int n = lv.step(c, d);
            if (n < 0) return fail("the garden is not closed");
            if (lv.tiles[static_cast<size_t>(n)] == Tile::outside) { lv.tiles[static_cast<size_t>(n)] = Tile::floor; stack.push_back(n); }
        }
    }
    for (int b : lv.boxes)
        if (!lv.floor(b)) return fail("a pumpkin outside the garden");
    for (int i = 0; i < lv.w * lv.h; ++i)
        if (lv.goal[static_cast<size_t>(i)] && !lv.floor(i)) return fail("a burrow outside the garden");
    if (static_cast<int>(lv.boxes.size()) != lv.goals() || lv.boxes.empty()) return fail("pumpkins and burrows differ in number");
    std::sort(lv.boxes.begin(), lv.boxes.end());
    out = std::move(lv);
    return true;
}

std::string Level::xsb() const {
    // cropped to the garden itself: a row of nothing would read as the end of the level
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (tiles[static_cast<size_t>(idx(x, y))] != Tile::outside) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
    std::string s;
    for (int y = y0; y <= y1; ++y) {
        std::string row;
        for (int x = x0; x <= x1; ++x) {
            const int i = idx(x, y);
            const bool g = goal[static_cast<size_t>(i)] != 0;
            const bool b = std::find(boxes.begin(), boxes.end(), i) != boxes.end();
            char c = ' ';
            if (tiles[static_cast<size_t>(i)] == Tile::wall) c = '#';
            else if (tiles[static_cast<size_t>(i)] == Tile::outside) c = ' ';
            else if (i == player) c = g ? '+' : '@';
            else if (b) c = g ? '*' : '$';
            else if (g) c = '.';
            row += c;
        }
        while (!row.empty() && row.back() == ' ') row.pop_back();
        s += row + "\n";
    }
    return s;
}

std::vector<std::uint8_t> live_squares(const Level& lv) {
    // backwards from every burrow: a pumpkin at s can be pushed to t = s + d
    // when the bear can stand at s - d
    std::vector<std::uint8_t> live(static_cast<size_t>(lv.w * lv.h), 0);
    std::vector<int> q;
    for (int i = 0; i < lv.w * lv.h; ++i)
        if (lv.goal[static_cast<size_t>(i)]) { live[static_cast<size_t>(i)] = 1; q.push_back(i); }
    for (size_t k = 0; k < q.size(); ++k) {
        const int t = q[k];
        for (int d = 0; d < 4; ++d) {
            const int s = lv.step(t, (d + 2) % 4);      // where the pumpkin came from
            const int p = s >= 0 ? lv.step(s, (d + 2) % 4) : -1;  // where the bear stood
            if (lv.floor(s) && lv.floor(p) && !live[static_cast<size_t>(s)]) { live[static_cast<size_t>(s)] = 1; q.push_back(s); }
        }
    }
    return live;
}

bool Board::load(const Level& level) {
    if (level.player < 0 || level.boxes.empty()) return false;
    lv_ = level;
    compute_dead();
    restart();
    return true;
}

void Board::compute_dead() {
    const auto live = live_squares(lv_);
    dead_.assign(live.size(), 0);
    for (size_t i = 0; i < live.size(); ++i) dead_[i] = lv_.tiles[i] == Tile::floor && !live[i];
}

void Board::restart() {
    player_ = lv_.player;
    boxes_ = lv_.boxes;
    occ_.assign(static_cast<size_t>(lv_.w * lv_.h), -1);
    for (size_t i = 0; i < boxes_.size(); ++i) occ_[static_cast<size_t>(boxes_[i])] = static_cast<int>(i);
    hist_.clear();
    pushes_ = 0;
}

bool Board::move(int dir, Move* out) {
    if (dir < 0 || dir > 3) return false;
    const int n = lv_.step(player_, dir);
    if (!lv_.floor(n)) return false;
    Move m;
    m.dir = dir;
    const int b = box_at(n);
    if (b >= 0) {
        const int t = lv_.step(n, dir);
        if (!lv_.floor(t) || box_at(t) >= 0) return false;
        occ_[static_cast<size_t>(n)] = -1;
        occ_[static_cast<size_t>(t)] = b;
        boxes_[static_cast<size_t>(b)] = t;
        m.push = true;
        m.box = b;
        ++pushes_;
    }
    player_ = n;
    hist_.push_back(m);
    if (out) *out = m;
    return true;
}

bool Board::undo(Move* out) {
    if (hist_.empty()) return false;
    const Move m = hist_.back();
    hist_.pop_back();
    const int back = (m.dir + 2) % 4;
    if (m.push) {
        const int at = boxes_[static_cast<size_t>(m.box)];
        const int from = lv_.step(at, back);
        occ_[static_cast<size_t>(at)] = -1;
        occ_[static_cast<size_t>(from)] = m.box;
        boxes_[static_cast<size_t>(m.box)] = from;
        --pushes_;
    }
    player_ = lv_.step(player_, back);
    if (out) *out = m;
    return true;
}

bool Board::replay(const std::string& lurd) {
    for (char c : lurd) {
        const int d = dir_of(c);
        if (d < 0) continue;
        Move m;
        if (!move(d, &m)) return false;
        if (m.push != (c >= 'A' && c <= 'Z')) return false;  // a walk written as a push, or the reverse
    }
    return true;
}

bool Board::solved() const {
    for (int b : boxes_)
        if (!lv_.goal[static_cast<size_t>(b)]) return false;
    return true;
}

int Board::on_goal() const {
    int n = 0;
    for (int b : boxes_) n += lv_.goal[static_cast<size_t>(b)];
    return n;
}

std::string Board::history() const {
    std::string s;
    for (const Move& m : hist_) s += m.push ? kPush[m.dir] : kWalk[m.dir];
    return s;
}

bool Board::stuck() const {
    for (int b : boxes_)
        if (!lv_.goal[static_cast<size_t>(b)] && dead_square(b)) return true;
    // four squares, each a hedge or a pumpkin, holding a pumpkin off its burrow: nothing in there can ever move
    auto solid = [&](int x, int y) { return !lv_.in(x, y) || lv_.tiles[static_cast<size_t>(lv_.idx(x, y))] != Tile::floor || box_at(lv_.idx(x, y)) >= 0; };
    for (int b : boxes_) {
        const int bx = b % lv_.w, by = b / lv_.w;
        for (int ox = -1; ox <= 0; ++ox)
            for (int oy = -1; oy <= 0; ++oy) {
                bool all = true, loose = false;
                for (int k = 0; k < 4 && all; ++k) {
                    const int x = bx + ox + k % 2, y = by + oy + k / 2;
                    if (!solid(x, y)) all = false;
                    else if (lv_.in(x, y) && box_at(lv_.idx(x, y)) >= 0 && !lv_.goal[static_cast<size_t>(lv_.idx(x, y))]) loose = true;
                }
                if (all && loose) return true;
            }
    }
    return false;
}

}  // namespace ct
