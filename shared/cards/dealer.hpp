#pragma once
// The next deal of each game, option and level, made in the background (games::generate_deal
// on a GUI.Forms Worker) and kept between runs, so that a new game is ready at once. A deal
// is made only while the table is shown: pause() stops the work, resume() goes on with it.
#include "solitaire_solver.hpp"
#include "gui_forms/threading.hpp"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <vector>

namespace games {

class Dealer {
  public:
    // Ready deals are kept in `file`.
    explicit Dealer(std::filesystem::path file);
    ~Dealer();
    Dealer(const Dealer&) = delete;
    Dealer& operator=(const Dealer&) = delete;

    // A ready deal's seed for game.deal(kind, seed, option), taken; 0 when none is ready.
    [[nodiscard]] std::uint32_t take(Kind kind, int option, Difficulty level);
    // Asks for a deal to be made ready, unless one is. First asked, first made.
    void want(Kind kind, int option, Difficulty level);
    // A deal someone is waiting for: made before anything else, setting aside the one in hand.
    void hurry(Kind kind, int option, Difficulty level);
    // On the UI thread: takes in a finished deal and starts the next. True when a deal
    // became ready.
    bool collect();
    [[nodiscard]] bool busy() const;
    // On the UI thread, when nothing on the table moves: writes the ready deals if they
    // changed (a deal taken or made). Writing at once could hold up a card in flight.
    void save_if_due();
    void pause();
    void resume();
    // A deal made here and now (for the first deal, when nothing is ready yet).
    [[nodiscard]] static std::uint32_t make_now(Kind kind, int option, Difficulty level);

  private:
    struct Key {
        int kind, option, level;
        bool operator<(const Key& other) const;
        bool operator==(const Key& other) const;
    };
    struct Job {
        Key key;
        std::uint64_t luck = 0;
        DealPick pick;
        std::atomic<bool> done{false};
    };
    static void run(const gui_forms::CancellationFlag& cancellation, void* context);
    static Key key_of(Kind kind, int option, Difficulty level);
    void start_next();
    void stop_job();
    void load();
    void save() const;

    std::filesystem::path file_;
    std::map<Key, std::uint32_t> ready_;
    bool save_due_ = false;
    std::vector<Key> wanted_;
    std::uint64_t luck_ = 0;
    bool paused_ = false;
    std::unique_ptr<Job> job_;                // declared before the worker that borrows it
    std::unique_ptr<gui_forms::Worker> worker_;
};

} // namespace games
