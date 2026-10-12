#include "dealer.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <string>
#include <tuple>
#include <utility>

namespace games {
namespace {

bool cancelled(const void* flag) {
    return (*static_cast<const gui_forms::CancellationFlag*>(flag)).requested();
}

std::uint64_t fresh_luck() {
    return static_cast<std::uint64_t>(
               std::chrono::system_clock::now().time_since_epoch().count()) *
               0x9e3779b97f4a7c15ULL +
           0x2545f4914f6cdd1dULL;
}

} // namespace

bool Dealer::Key::operator<(const Key& other) const {
    return std::tie(kind, option, level) < std::tie(other.kind, other.option, other.level);
}
bool Dealer::Key::operator==(const Key& other) const {
    return kind == other.kind && option == other.option && level == other.level;
}

Dealer::Dealer(std::filesystem::path file) : file_(std::move(file)), luck_(fresh_luck()) {
    load();
}

Dealer::~Dealer() {
    stop_job();
}

Dealer::Key Dealer::key_of(Kind kind, int option, Difficulty level) {
    return {static_cast<int>(kind), normalized_option(kind, option), static_cast<int>(level)};
}

std::uint32_t Dealer::take(Kind kind, int option, Difficulty level) {
    const std::map<Key, std::uint32_t>::iterator found = ready_.find(key_of(kind, option, level));
    if (found == ready_.end())
        return 0;
    const std::uint32_t seed = (*found).second;
    ready_.erase(found);
    save();
    return seed;
}

void Dealer::want(Kind kind, int option, Difficulty level) {
    const Key key = key_of(kind, option, level);
    if (ready_.count(key) || std::find(wanted_.begin(), wanted_.end(), key) != wanted_.end() ||
        (job_ && (*job_).key == key))
        return;
    wanted_.push_back(key);
    start_next();
}

void Dealer::hurry(Kind kind, int option, Difficulty level) {
    const Key key = key_of(kind, option, level);
    if (ready_.count(key) || (job_ && (*job_).key == key))
        return;
    wanted_.erase(std::remove(wanted_.begin(), wanted_.end(), key), wanted_.end());
    if (job_) {
        const Key aside = (*job_).key;
        stop_job();
        wanted_.insert(wanted_.begin(), aside);
    }
    wanted_.insert(wanted_.begin(), key);
    start_next();
}

bool Dealer::collect() {
    if (!job_ || !(*job_).done.load(std::memory_order_acquire))
        return false;
    (*worker_).join();
    worker_.reset();
    const Key key = (*job_).key;
    const DealPick pick = (*job_).pick;
    job_.reset();
    if (pick.found) {
        ready_[key] = pick.seed;
        save();
    } else
        wanted_.insert(wanted_.begin(), key); // cancelled: made again later
    start_next();
    return pick.found;
}

bool Dealer::busy() const {
    return job_ != nullptr;
}

void Dealer::pause() {
    paused_ = true;
    if (!job_)
        return;
    const Key key = (*job_).key;
    stop_job();
    wanted_.insert(wanted_.begin(), key);
}

void Dealer::resume() {
    paused_ = false;
    start_next();
}

std::uint32_t Dealer::make_now(Kind kind, int option, Difficulty level) {
    return generate_deal(kind, option, level, fresh_luck()).seed;
}

void Dealer::run(const gui_forms::CancellationFlag& cancellation, void* context) {
    Job& job = *static_cast<Job*>(context);
    job.pick = generate_deal(static_cast<Kind>(job.key.kind), job.key.option,
                             static_cast<Difficulty>(job.key.level), job.luck, cancelled,
                             &cancellation);
    job.done.store(true, std::memory_order_release);
}

void Dealer::start_next() {
    if (paused_ || job_ || wanted_.empty())
        return;
    job_ = std::make_unique<Job>();
    (*job_).key = wanted_.front();
    (*job_).luck = luck_++;
    wanted_.erase(wanted_.begin());
    worker_ = std::make_unique<gui_forms::Worker>(&Dealer::run, job_.get());
}

void Dealer::stop_job() {
    if (worker_) {
        (*worker_).request_cancel();
        (*worker_).join();
    }
    worker_.reset();
    job_.reset();
}

// "playsuite-deals 1", then one line a ready deal: kind option level seed.
void Dealer::load() {
    std::ifstream in(file_);
    std::string tag;
    int version = 0;
    if (!(in >> tag >> version) || tag != "playsuite-deals" || version != 1)
        return;
    int kind = 0, option = 0, level = 0;
    std::uint32_t seed = 0;
    while (in >> kind >> option >> level >> seed)
        if (kind >= 0 && kind <= 2 && level >= 0 && level <= 2 && seed != 0)
            ready_[key_of(static_cast<Kind>(kind), option, static_cast<Difficulty>(level))] =
                seed;
}

void Dealer::save() const {
    std::error_code error;
    std::filesystem::create_directories(file_.parent_path(), error);
    std::filesystem::path temporary = file_;
    temporary += ".tmp";
    {
        std::ofstream out(temporary);
        out << "playsuite-deals 1\n";
        for (const std::pair<const Key, std::uint32_t>& entry : ready_)
            out << entry.first.kind << ' ' << entry.first.option << ' ' << entry.first.level
                << ' ' << entry.second << '\n';
        if (!out)
            return;
    }
    std::filesystem::rename(temporary, file_, error);
}

} // namespace games
