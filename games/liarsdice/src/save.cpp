#include "save.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ld {

namespace {
const char* kMagic = "LIARSDICE1";
std::uint64_t fnv(const std::string& s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}
std::string clean(std::string s) {
    for (char& c : s) if (c == '\n' || c == '\r' || c == '|') c = ' ';
    return s;
}
std::string join(const std::vector<std::string>& v) {
    std::string o;
    for (size_t i = 0; i < v.size(); ++i) o += (i ? "|" : "") + clean(v[i]);
    return o;
}
std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    if (s.empty()) return out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}
std::string ints(const std::vector<int>& v) {
    std::string o;
    for (size_t i = 0; i < v.size(); ++i) o += (i ? "," : "") + std::to_string(v[i]);
    return o;
}
std::vector<int> parse_ints(const std::string& s) {
    std::vector<int> out;
    for (const std::string& p : split(s, ',')) if (!p.empty()) out.push_back(std::stoi(p));
    return out;
}
}  // namespace

std::string serialize(const Ledger& l) {
    std::ostringstream o;
    o << "owed=" << l.owed << "\njar=" << join(l.jar) << "\ntrophies=" << join(l.trophies) << "\nwon=" << l.won << "\nlost=" << l.lost
      << "\nserved=" << l.served << "\nstruck=" << l.struck << "\nfreed=" << l.freed << "\nrec=" << l.rec.bids << "," << l.rec.bluffs
      << "\noffer=" << l.offer_seed << "\nsound=" << l.sound << "\nmusic=" << l.music << "\n";
    for (int i = 0; i < 32; ++i) {
        const CrewNotes& n = l.notes[static_cast<size_t>(i)];
        if (n.games || n.bids_seen) o << "note" << i << "=" << n.games << "," << n.beaten << "," << n.bids_seen << "," << n.bluffs_seen << "," << n.tells_caught << "\n";
    }
    if (l.in_match) {
        const Wager& w = l.wager;
        const Match& m = l.match;
        o << "w_title=" << clean(w.title) << "\nw_tier=" << w.tier << "\nw_seats=" << ints(w.seats) << "\nw_lose=" << w.years_lose << "\nw_win=" << w.years_win
          << "\nw_forfeit=" << clean(w.forfeit) << "\nw_prize=" << clean(w.prize) << "\nw_pitch=" << clean(w.pitch) << "\nm_players=" << m.players
          << "\nm_count=" << ints(m.count) << "\nm_turn=" << m.turn << "\nm_bid=" << m.bid.qty << "," << m.bid.face << "\nm_bidder=" << m.bidder
          << "\nm_round=" << m.round << "\nm_rng=" << l.rng << "\n";
        for (int p = 0; p < m.players; ++p) o << "m_dice" << p << "=" << ints(m.dice[static_cast<size_t>(p)]) << "\n";
        o << "m_history=";
        for (size_t i = 0; i < m.history.size(); ++i) o << (i ? ";" : "") << m.history[i].who << "," << m.history[i].bid.qty << "," << m.history[i].bid.face;
        o << "\n";
    }
    std::string body = o.str();
    return std::string(kMagic) + "\n" + body + "check=" + std::to_string(fnv(body)) + "\n";
}

bool parse(const std::string& all, Ledger& out) {
    const std::string head = std::string(kMagic) + "\n";
    if (all.size() > 65536 || all.rfind(head, 0) != 0) return false;
    const size_t ck = all.rfind("check=");
    if (ck == std::string::npos || ck < head.size()) return false;
    const std::string body = all.substr(head.size(), ck - head.size());
    if (std::to_string(fnv(body)) + "\n" != all.substr(ck + 6)) return false;
    Ledger l;
    try {
        std::istringstream in(body);
        std::string line;
        bool has_match = false;
        while (std::getline(in, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "owed") l.owed = std::max(0, std::stoi(v));
            else if (k == "jar") l.jar = split(v, '|');
            else if (k == "trophies") l.trophies = split(v, '|');
            else if (k == "won") l.won = std::stoi(v);
            else if (k == "lost") l.lost = std::stoi(v);
            else if (k == "served") l.served = std::stoi(v);
            else if (k == "struck") l.struck = std::stoi(v);
            else if (k == "freed") l.freed = v == "1";
            else if (k == "rec") { const auto p = split(v, ','); if (p.size() == 2) { l.rec.bids = std::stod(p[0]); l.rec.bluffs = std::stod(p[1]); } }
            else if (k == "offer") l.offer_seed = std::stoull(v);
            else if (k == "sound") l.sound = v == "1";
            else if (k == "music") l.music = v == "1";
            else if (k.rfind("note", 0) == 0) {
                const int i = std::stoi(k.substr(4));
                const auto p = parse_ints(v);
                if (i >= 0 && i < 32 && p.size() == 5) l.notes[static_cast<size_t>(i)] = {p[0], p[1], p[2], p[3], p[4]};
            }
            else if (k == "w_title") { l.wager.title = v; has_match = true; }
            else if (k == "w_tier") l.wager.tier = std::stoi(v);
            else if (k == "w_seats") l.wager.seats = parse_ints(v);
            else if (k == "w_lose") l.wager.years_lose = std::stoi(v);
            else if (k == "w_win") l.wager.years_win = std::stoi(v);
            else if (k == "w_forfeit") l.wager.forfeit = v;
            else if (k == "w_prize") l.wager.prize = v;
            else if (k == "w_pitch") l.wager.pitch = v;
            else if (k == "m_players") { l.match.players = std::stoi(v); l.match.dice.assign(static_cast<size_t>(std::clamp(l.match.players, 0, 4)), {}); }
            else if (k == "m_count") l.match.count = parse_ints(v);
            else if (k == "m_turn") l.match.turn = std::stoi(v);
            else if (k == "m_bid") { const auto p = parse_ints(v); if (p.size() == 2) l.match.bid = {p[0], p[1]}; }
            else if (k == "m_bidder") l.match.bidder = std::stoi(v);
            else if (k == "m_round") l.match.round = std::stoi(v);
            else if (k == "m_rng") l.rng = std::stoull(v);
            else if (k.rfind("m_dice", 0) == 0) {
                const int p = std::stoi(k.substr(6));
                if (p >= 0 && p < static_cast<int>(l.match.dice.size())) l.match.dice[static_cast<size_t>(p)] = parse_ints(v);
            }
            else if (k == "m_history") {
                for (const std::string& h : split(v, ';')) {
                    const auto p = parse_ints(h);
                    if (p.size() == 3) l.match.history.push_back({p[0], {p[1], p[2]}});
                }
            }
        }
        if (has_match) {
            // a game in progress must hang together, or it is dropped (the ledger is kept)
            const Match& m = l.match;
            bool ok = m.players >= 3 && m.players <= 4 && static_cast<int>(m.count.size()) == m.players && static_cast<int>(l.wager.seats.size()) == m.players - 1 &&
                      m.turn >= 0 && m.turn < m.players && m.alive_count() >= 2;
            for (int p = 0; ok && p < m.players; ++p) {
                ok = static_cast<int>(m.dice[static_cast<size_t>(p)].size()) == m.count[static_cast<size_t>(p)] && m.count[static_cast<size_t>(p)] >= 0 && m.count[static_cast<size_t>(p)] <= kStartDice;
                for (int v : m.dice[static_cast<size_t>(p)]) ok = ok && v >= 1 && v <= 6;
            }
            for (int s : l.wager.seats) ok = ok && s >= 0 && s < 32;
            l.in_match = ok;
        }
    } catch (...) {
        return false;
    }
    out = l;
    return true;
}

bool save_ledger(const std::string& path, const Ledger& l) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    {
        std::ofstream f(path + ".tmp", std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << serialize(l);
        if (!f) return false;
    }
    std::filesystem::rename(path + ".tmp", path, ec);
    return !ec;
}

bool load_ledger(const std::string& path, Ledger& l) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    return parse(ss.str(), l);
}

}  // namespace ld
