// Rules remain independent of presentation, cancellation and save corruption.
#include "gen.hpp"
#include "garden.hpp"
#include "save.hpp"
#include "show.hpp"
#include "solver.hpp"
#include "tiers.hpp"
#include <cstdlib>
#include <map>
#include <set>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include "cancellation.hpp"
#include <string>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
std::string contents(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream data;
    data << input.rdbuf();
    return data.str();
}
void write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}
std::string envelope(const std::string& body) {
    std::uint64_t checksum = 14695981039346656037ULL;
    for (const unsigned char byte : body) { checksum ^= byte; checksum *= 1099511628211ULL; }
    return "THIEVES1\n" + body + "check=" + std::to_string(checksum) + "\n";
}
void save_checks() {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "playsuite-ct-core-contract";
    std::filesystem::create_directories(directory);
    const std::filesystem::path path = directory / "state.txt";
    ct::SaveData source;
    source.level = 4;
    source.history = "rRU";
    source.records[2] = {20, 8};
    require(ct::write_save(path, source), "first save");
    source.history = "rRUD";
    require(ct::write_save(path, source), "replace prior save");
    ct::SaveData restored;
    require(ct::load_save(path, restored) && restored.history == source.history && restored.level == 4,
            "latest committed save round-trips");
    const std::string valid = contents(path);
    const std::string invalid[] = {"FOREIGN1\n", valid.substr(0, valid.size() - 3),
        std::string(256 * 1024 + 1, 'X'), envelope("level=243\n"), envelope("level=-2\n"),
        envelope("endless_tier=5\n"), envelope("history=xyz\n"), envelope("level=1trailing\n"),
        envelope("level=-1\nendless=#####|#@$.#|#####\nhistory=U\n")};
    for (const std::string& corrupt : invalid) {
        write(path, corrupt);
        restored.level = 41; restored.history = "marker";
        require(!ct::load_save(path, restored), "refuse invalid save");
        require(restored.level == 41 && restored.history == "marker", "rejected save leaves destination unchanged");
    }
    std::filesystem::remove_all(directory);
}
void motion_checks() {
    ct::Level level;
    require(ct::Level::parse("########\n#@ $  .#\n########\n", level), "parse scene fixture");
    ct::Board board;
    require(board.load(level), "load fixture");
    ct::Garden garden;
    garden.set_level(level); garden.resize(300, 160, 88);
    ct::Show show(7);
    show.set_level(board, garden, ct::Season::spring);
    ct::Move move;
    require(board.move(ct::kRight, &move), "walk");
    show.moved(board, move, false);
    require(show.busy(), "ordinary move animates");
    for (int frame = 0; frame < 30; ++frame) show.update(1.0 / 60.0, board);
    require(!show.busy(), "move lands in bounded time");
    require(board.move(ct::kRight, &move) && move.push, "push");
    show.moved(board, move, false); show.settle(board);
    const ct::V3 bear = garden.cell_pos(board.player());
    const ct::V3 pumpkin = garden.cell_pos(board.boxes()[0]);
    require(!show.busy() && show.state().bear.pos.x == bear.x && show.state().bear.pos.y == bear.y,
            "reduced motion immediately lands bear");
    require(show.state().pumpkins[0].pos.x == pumpkin.x && show.state().pumpkins[0].pos.y == pumpkin.y &&
            show.state().pumpkins[0].roll_x == 0 && show.state().pumpkins[0].roll_y == 0 && show.state().puffs.empty(),
            "reduced motion lands pumpkin without transient effects");
    require(board.undo(&move), "undo push");
    show.moved(board, move, true); show.settle(board);
    require(!show.busy() && board.pushes() == 0, "reduced motion undo preserves exact rules");
    const std::string history = board.history();
    show.settle(board);
    require(board.history() == history, "presentation never changes rules");
    const int widths[] = {300, 400, 590, 960};
    const int heights[] = {160, 185, 400, 540};
    for (int width : widths) for (int height : heights) {
        garden.resize(width, height, 88); garden.render(show.state(), 0);
        double x = 0, y = 0;
        garden.to_screen(garden.cell_pos(board.player()), x, y);
        require(x >= 0 && x < width - 88 && y >= 0 && y < height, "board remains visible at minimum and large sizes");
    }
}
void cancellation_checks() {
    ct::Level level;
    require(ct::Level::parse("#######\n#@ $ .#\n#######\n", level), "parse cancellation fixture");
    ct::CancellationSource cancellation;
    const ct::CancellationToken token=cancellation.get_token();
    require(!token.stop_requested(), "fresh cancellation token permits search");
    cancellation.request_stop();
    require(token.stop_requested(), "existing token observes source cancellation");
    const ct::SolveResult cancelled = ct::solve(level, 2000000, cancellation.get_token());
    require(!cancelled.solved && cancelled.exhausted, "cancelled hint is unknown, never a false unsolvable result");
    ct::GenParams parameters;
    const ct::GenResult generated = ct::generate(parameters, cancellation.get_token());
    require(!generated.ok, "hidden generation cancels before doing work");
    const ct::SolveResult normal = ct::solve(level);
    require(normal.solved && normal.lurd == "rRR", "ordinary solver remains unchanged");
}
// A long session of play, simulated: the bear wanders, pushes, undoes and
// starts over across several gardens and seasons while the raccoons perform.
// Nobody may say the same line twice.
void session_checks() {
    ct::Show show(11);
    ct::Garden garden;
    garden.resize(400, 260, 88);
    std::vector<std::string> said;
    std::uint64_t h = 99;
    double minutes = 0;
    for (int g = 0; g < 6; ++g) {
        const ct::Grown grown = ct::grow(g % 2 ? ct::kMedium : ct::kEasy, 500 + static_cast<std::uint64_t>(g));
        require(grown.ok, "session garden grows");
        ct::Board board;
        board.load(grown.entry.level);
        garden.set_level(board.level());
        show.set_level(board, garden, static_cast<ct::Season>(g % 5));
        for (int frame = 0; frame < 15 * 60 * 3; ++frame) {  // three minutes a garden at 15 frames a second
            const double dt = 1.0 / 15;
            if (frame % 9 == 0 && (frame / 450) % 3 != 2) {  // walking for a while, then standing still
                h ^= h << 13; h ^= h >> 7; h ^= h << 17;
                ct::Move move;
                const int roll = static_cast<int>(h % 100);
                if (roll == 0) { board.restart(); show.restart(board); show.stuck(false); }
                else if (roll < 12 && board.undo(&move)) { show.moved(board, move, true); show.stuck(board.stuck()); }
                else if (board.move(static_cast<int>((h >> 8) % 4), &move)) { show.moved(board, move, false); show.stuck(board.stuck()); }
                else show.blocked(static_cast<int>((h >> 8) % 4));
                if (board.solved()) { show.won(board.pushes() <= grown.entry.par); }
                if (roll == 99) show.hinted();
            }
            show.update(dt, board);
            for (const ct::Cue& cue : show.cues)
                if (cue.kind == ct::Cue::say && cue.text != "Pbbbt!") said.push_back(cue.text);
            show.cues.clear();
            if (board.solved()) break;
        }
        minutes += 3;
    }
    std::map<std::string, int> seen;
    int repeats = 0;
    for (const std::string& line : said) repeats += seen[line]++ > 0;
    if (std::getenv("CT_SESSION_DUMP"))
        for (const std::pair<const std::string, int>& line : seen) std::cout << line.second << "  " << line.first << "\n";
    std::cout << "Session: " << said.size() << " lines in about " << minutes << " minutes, " << repeats << " repeated.\n";
    require(said.size() >= 60, "the raccoons and the bear talk through a session");
    require(repeats == 0, "no line is said twice in a session");
}
bool said_from(const ct::Show& show, ct::Line kind, int who) {
    for (const ct::Bubble& bubble : show.bubbles) {
        if (bubble.who != who) continue;
        for (int i = 0; i < ct::Lines::size(kind); ++i)
            if (bubble.text == ct::Lines::line(kind, i)) return true;
    }
    return false;
}

// Wedging pumpkins again and again in one garden: the raccoons laugh, then
// cheer, then turn kind and point at Undo, without laughter in the sound.
void escalation_checks() {
    ct::Level level;
    require(ct::Level::parse("#########\n#       #\n#  $ .  #\n#  @    #\n#########\n", level), "parse escalation fixture");
    ct::Board board;
    board.load(level);
    ct::Garden garden;
    garden.set_level(level); garden.resize(400, 260, 88);
    ct::Show show(3);
    show.set_level(board, garden, ct::Season::autumn);
    const ct::Line bear[] = {ct::Line::bear_stuck, ct::Line::bear_stuck_again, ct::Line::bear_stuck_calm, ct::Line::bear_stuck_calm};
    const ct::Line coon[] = {ct::Line::laugh, ct::Line::laugh_again, ct::Line::laugh_soft, ct::Line::laugh_soft};
    for (int time = 0; time < 4; ++time) {
        show.bubbles.clear(); show.cues.clear();
        show.stuck(true);
        require(said_from(show, bear[time], -1), "the bear's reaction follows how often it has happened");
        require(said_from(show, coon[time], 0), "the raccoons' reaction follows how often it has happened");
        bool laugh = false;
        for (const ct::Cue& cue : show.cues) laugh = laugh || cue.text == "ct_laugh";
        require(laugh == (time < 2), "gentle words come without laughter");
        show.bubbles.clear();
        show.stuck(false);
        for (int frame = 0; frame < 15 * 12; ++frame) show.update(1.0 / 15, board);
    }
    show.set_level(board, garden, ct::Season::autumn);
    show.bubbles.clear();
    show.stuck(true);
    require(said_from(show, ct::Line::bear_stuck, -1), "a new garden starts the escalation afresh");
}
}
int main() {
    save_checks(); motion_checks(); cancellation_checks(); session_checks(); escalation_checks();
    std::cout << "Catching Thieves: save corruption/replacement, motion, layout, cancellation, session lines and escalation pass.\n";
}
