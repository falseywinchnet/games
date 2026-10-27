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
    if (w <= 0 || c < 0 || c >= w*h || d < 0 || d > 3) return -1;
    const int x = c % w + kDX[d], y = c / w + kDY[d];
    return in(x, y) ? idx(x, y) : -1;
}

int Level::goals() const {
    int n = 0;
    for (std::uint8_t g : goal) n += g;
    return n;
}

bool Level::special() const {
    return !raccoons.empty() || std::any_of(pit.begin(), pit.end(), [](auto x){ return x != 0; }) ||
           std::any_of(gate.begin(), gate.end(), [](auto x){ return x != 0; });
}

bool Level::edge(int from, int dir) const {
    const int to = step(from, dir);
    return floor(from) && floor(to) && (!gate_at(from) || gate_at(from) == dir+1) &&
           (!gate_at(to) || gate_at(to) == dir+1);
}

bool Level::valid() const {
    if (w < 1 || h < 1 || w > 40 || h > 40 || tiles.size() != static_cast<size_t>(w*h) || goal.size() != tiles.size()) return false;
    if ((!gate.empty() && gate.size() != tiles.size()) || (!pit.empty() && pit.size() != tiles.size())) return false;
    if (!floor(player) || pit_at(player) || boxes.empty() || goals() < 1) return false;
    std::vector<int> occupied(tiles.size());
    occupied[player] = 1;
    for (size_t i=0; i<boxes.size(); ++i) {
        int b=boxes[i];
        if (!floor(b) || pit_at(b) || occupied[b]++) return false;
    }
    for (int r : raccoons) if (!floor(r) || pit_at(r) || goal[r] || gate_at(r) || occupied[r]++) return false;
    // Only boundary cells can step outside a rectangular grid. Check that
    // boundary once, instead of four coordinate divisions for every floor.
    for (int x=0;x<w;++x) if (floor(x)||floor((h-1)*w+x)) return false;
    for (int y=1;y+1<h;++y) if (floor(y*w)||floor(y*w+w-1)) return false;
    int pits = 0;
    for (int c=0; c<w*h; ++c) {
        if (goal[c] > 1 || gate_at(c) > 4 || (!pit.empty() && pit[c] > 1)) return false;
        if ((goal[c] || gate_at(c) || pit_at(c)) && !floor(c)) return false;
        if (pit_at(c) && goal[c]) return false;
        pits += pit_at(c);
    }
    // Any pumpkin can fill a pit or be taken by a squirrel. Each pit and
    // each squirrel can remove at most one; no special pumpkin type is involved.
    return static_cast<int>(boxes.size()) >= goals() &&
        boxes.size() <= static_cast<std::size_t>(goals()+pits)+raccoons.size();
}

bool Level::parse(const std::string& xsb, Level& out, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    std::vector<std::string> rows;
    std::istringstream in(xsb);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find_first_not_of(" ") == std::string::npos) { if (!rows.empty()) break; continue; }
        if (line.rfind("!ct2 ", 0) == 0) break;
        rows.push_back(line);
    }
    if (rows.empty()) return fail("empty level");
    Level lv;
    lv.h = static_cast<int>(rows.size());
    for (const std::string& r : rows) lv.w = std::max(lv.w, static_cast<int>(r.size()));
    if (lv.w > 40 || lv.h > 40) return fail("level too large");
    lv.tiles.assign(static_cast<size_t>(lv.w * lv.h), Tile::outside);
    lv.goal.assign(static_cast<size_t>(lv.w * lv.h), 0);
    lv.gate.assign(lv.tiles.size(), 0);
    lv.pit.assign(lv.tiles.size(), 0);
    for (int y = 0; y < lv.h; ++y)
        for (int x = 0; x < static_cast<int>(rows[static_cast<size_t>(y)].size()); ++x) {
            const char c = rows[static_cast<size_t>(y)][static_cast<size_t>(x)];
            const int i = lv.idx(x, y);
            switch (c) {
                case '#': lv.tiles[static_cast<size_t>(i)] = Tile::wall; break;
                case ' ': case '-': case '_': break;  // floor or outside: decided by the flood below
                case '.': lv.goal[static_cast<size_t>(i)] = 1; break;
                case '$': case 'B': lv.boxes.push_back(i); break; // B reads old development saves as an ordinary pumpkin
                case 'O': lv.pit[i] = 1; break;
                case 'X': lv.raccoons.push_back(i); break;
                case '^': case '>': case 'v': case '<': lv.gate[i] = static_cast<std::uint8_t>(std::string("^>v<").find(c)+1); break;
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

    // Optional !ct2 metadata follows the grid. This keeps
    // overlays under actors and stable pumpkin identities lossless on save.
    do {
        if (line.rfind("!ct2 ", 0) != 0) continue;
        std::istringstream meta(line.substr(5));
        std::string kind; int count;
        if (!(meta >> kind >> count) || count < 0 || count > lv.w*lv.h) return fail("bad overlay metadata");
        std::vector<int> values(static_cast<size_t>(count));
        for (int& v : values) if (!(meta >> v)) return fail("short overlay metadata");
        if (kind == "boxes") lv.boxes = values;
        else if (kind == "raccoons") lv.raccoons = values;
        else if (kind == "bridge") continue; // obsolete development metadata; no pumpkin types
        else {
            std::vector<std::uint8_t>* dst = kind=="gate" ? &lv.gate : kind=="pit" ? &lv.pit : nullptr;
            if (!dst) return fail("unknown overlay metadata");
            dst->clear();
            for (int v : values) { if (v < 0 || v > 4) return fail("bad overlay value"); dst->push_back(static_cast<std::uint8_t>(v)); }
        }
    } while (std::getline(in, line));
    if (!lv.valid()) return fail("invalid overlays");
    out = std::move(lv);
    return true;
}

std::string Level::xsb() const {
    const bool overlays = special();
    // cropped to the garden itself: a row of nothing would read as the end of the level
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (tiles[static_cast<size_t>(idx(x, y))] != Tile::outside) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
    if (overlays) { x0=0; y0=0; x1=w-1; y1=h-1; }
    std::string s;
    for (int y = y0; y <= y1; ++y) {
        std::string row;
        for (int x = x0; x <= x1; ++x) {
            const int i = idx(x, y);
            const bool g = goal[static_cast<size_t>(i)] != 0;
            const bool b = std::find(boxes.begin(), boxes.end(), i) != boxes.end();
            char c = ' ';
            if (tiles[static_cast<size_t>(i)] == Tile::wall) c = '#';
            // Explicit outside cells preserve full canvas dimensions for indexed
            // overlays. Blank rows and trailing spaces are trimmed by XSB readers.
            else if (tiles[static_cast<size_t>(i)] == Tile::outside) c = overlays ? '_' : ' ';
            else if (i == player) c = g ? '+' : '@';
            else if (b) c = g ? '*' : '$';
            else if (g) c = '.';
            else if (pit_at(i)) c = 'O';
            else if (gate_at(i)) c = "^>v<"[gate_at(i)-1];
            else if (std::find(raccoons.begin(), raccoons.end(), i) != raccoons.end()) c = 'X';
            row += c;
        }
        while (!row.empty() && row.back() == ' ') row.pop_back();
        s += row + "\n";
    }
    if (overlays) {
        auto emit = [&](const char* key, const auto& v) {
            s += std::string("!ct2 ")+key+" "+std::to_string(v.size());
            for (auto x : v) s += " "+std::to_string(x);
            s += "\n";
        };
        emit("boxes", boxes); emit("gate", gate); emit("pit", pit); emit("raccoons", raccoons);
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
    if (!level.valid()) return false;
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
    filled_.assign(lv_.tiles.size(), 0);
    departed_.assign(lv_.raccoons.size(), 0);
    occ_.assign(static_cast<size_t>(lv_.w * lv_.h), -1);
    for (size_t i = 0; i < boxes_.size(); ++i) occ_[static_cast<size_t>(boxes_[i])] = static_cast<int>(i);
    hist_.clear();
    pushes_ = 0;
}

bool Board::move(int dir, Move* out) {
    if (dir < 0 || dir > 3) return false;
    const int n = lv_.step(player_, dir);
    if (!lv_.edge(player_, dir) || raccoon_at(n) || (lv_.pit_at(n) && !pit_filled(n))) return false;
    Move m;
    m.dir = dir;
    const int b = box_at(n);
    if (b >= 0) {
        const int t = lv_.step(n, dir);
        if (!lv_.edge(n, dir) || box_at(t) >= 0 || raccoon_at(t)) return false;
        const bool fill = lv_.pit_at(t) && !pit_filled(t);
        occ_[static_cast<size_t>(n)] = -1;
        occ_[static_cast<size_t>(t)] = b;
        boxes_[static_cast<size_t>(b)] = t;
        if (fill) {
            filled_[t] = 1; occ_[t] = -1; boxes_[b] = -1; m.filled = t;
        }
        m.push = true;
        m.box = b;
        ++pushes_;
    }
    player_ = n;
    // A squirrel takes at most one adjacent unguarded pumpkin, then leaves.
    // Covered burrows are safe. Bear guarding includes diagonal neighbours.
    for (size_t r=0; r<lv_.raccoons.size(); ++r) {
        if (departed_[r]) continue;
        for (int d=0; d<4; ++d) {
            int c=lv_.step(lv_.raccoons[r], d), b=box_at(c);
            if (b<0 || lv_.goal[c]) continue;
            if (std::max(std::abs(c%lv_.w-player_%lv_.w), std::abs(c/lv_.w-player_/lv_.w)) <= 1) continue;
            m.stolen.push_back(b); m.thieves.push_back(static_cast<int>(r)); m.stolen_from.push_back(c);
            boxes_[b] = -2; occ_[c] = -1; departed_[r] = 1;
            break;
        }
    }
    hist_.push_back(m);
    if (out) *out = m;
    return true;
}

bool Board::undo(Move* out) {
    if (hist_.empty()) return false;
    const Move m = hist_.back();
    hist_.pop_back();
    const int back = (m.dir + 2) % 4;
    for (size_t i=0; i<m.stolen.size(); ++i) {
        boxes_[m.stolen[i]]=m.stolen_from[i]; occ_[m.stolen_from[i]]=m.stolen[i]; departed_[m.thieves[i]]=0;
    }
    if (m.push) {
        const int at = boxes_[static_cast<size_t>(m.box)];
        if (at >= 0) occ_[at] = -1;
        if (m.filled >= 0) filled_[m.filled] = 0;
        boxes_[m.box] = player_;
        occ_[player_] = m.box;
        --pushes_;
    }
    player_ = lv_.step(player_, back);
    if (out) *out = m;
    return true;
}

bool Board::replay(const std::string& lurd) {
    for (char c : lurd) {
        const int d = dir_of(c);
        if (d < 0) return false;
        Move m;
        if (!move(d, &m)) return false;
        if (m.push != (c >= 'A' && c <= 'Z')) { undo(); return false; }  // a walk written as a push, or the reverse
    }
    return true;
}

bool Board::solved() const { return on_goal() == lv_.goals(); }

int Board::on_goal() const {
    int n = 0;
    for (int b : boxes_) if (b >= 0) n += lv_.goal[b];
    return n;
}

bool Board::raccoon_at(int c) const {
    for (size_t i=0; i<lv_.raccoons.size(); ++i) if (!departed_[i] && lv_.raccoons[i] == c) return true;
    return false;
}

bool Board::can_walk(int from, int dir) const {
    int c=lv_.step(from, dir);
    return lv_.edge(from, dir) && box_at(c)<0 && !raccoon_at(c) && (!lv_.pit_at(c) || pit_filled(c));
}

std::string Board::state_key() const {
    std::string s;
    auto add=[&](int v){ unsigned n=static_cast<unsigned>(v+2); s+=static_cast<char>(n&255); s+=static_cast<char>(n>>8); };
    add(player_);
    for (int b : boxes_) add(b);
    s.append(filled_.begin(),filled_.end()); s.append(departed_.begin(),departed_.end());
    return s;
}

bool Board::restore_state(const std::string& key) {
    const size_t offset=2+2*boxes_.size();
    if (key.size()!=offset+filled_.size()+departed_.size()) return false;
    auto get=[&](size_t at){ return static_cast<int>(static_cast<unsigned char>(key[at])) +
        256*static_cast<int>(static_cast<unsigned char>(key[at+1]))-2; };
    if (!lv_.floor(get(0))) return false;
    std::vector<int> occ(occ_.size(),-1), boxes(boxes_.size());
    for (size_t i=0;i<boxes.size();++i) {
        int c=get(2+2*i);
        if (c < -2 || (c>=0 && (!lv_.floor(c) || c==get(0) || occ[c]>=0))) return false;
        boxes[i]=c; if(c>=0) occ[c]=static_cast<int>(i);
    }
    for (size_t i=offset;i<key.size();++i) if (key[i]!=0 && key[i]!=1) return false;
    player_=get(0); boxes_=std::move(boxes); occ_=std::move(occ);
    std::copy(key.begin()+offset,key.begin()+offset+filled_.size(),filled_.begin());
    std::copy(key.begin()+offset+filled_.size(),key.end(),departed_.begin());
    clear_history();
    return true;
}

std::string Board::history() const {
    std::string s;
    for (const Move& m : hist_) s += m.push ? kPush[m.dir] : kWalk[m.dir];
    return s;
}

bool Board::stuck() const {
    int remaining=0;
    for (int b : boxes_) remaining += b>=0;
    if (remaining < lv_.goals()) return true;
    // Classic static deadlock rules are unsound for consumable pumpkins.
    // On extended maps report only a proven shortage; the exact solver handles the rest.
    if (lv_.special()) return false;
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
