// Rules remain independent of presentation, cancellation and save corruption.
#include "gen.hpp"
#include "garden.hpp"
#include "save.hpp"
#include "show.hpp"
#include "solver.hpp"
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
}
int main() {
    save_checks(); motion_checks(); cancellation_checks();
    std::cout << "Catching Thieves: save corruption/replacement, motion, layout and cancellation pass.\n";
}
