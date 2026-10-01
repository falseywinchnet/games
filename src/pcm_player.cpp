#include "pcm_player.hpp"
#include "runtime_paths.hpp"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>

namespace games {
namespace {
class PcmLoader final {
public:
    PcmLoader() : worker_(&PcmLoader::run, this) {}
    ~PcmLoader() {
        { std::lock_guard<std::mutex> lock(mutex_); closing_ = true; jobs_.clear(); }
        wake_.notify_one();
        worker_.join();
    }
    std::future<gui_forms::AudioClipResult> request(const std::filesystem::path& path) {
        Job job;
        job.path = path;
        std::future<gui_forms::AudioClipResult> result = job.result.get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (jobs_.size() >= 32 || closing_) {
                job.result.set_value({{}, gui_forms::AudioStatus::quota_exceeded});
                return result;
            }
            jobs_.push_back(std::move(job));
        }
        wake_.notify_one();
        return result;
    }
private:
    struct Job final {
        std::filesystem::path path{};
        std::promise<gui_forms::AudioClipResult> result{};
    };
    std::mutex mutex_{};
    std::condition_variable wake_{};
    std::deque<Job> jobs_{};
    bool closing_{};
    std::thread worker_;
    void run() {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                while (jobs_.empty() && !closing_) { wake_.wait(lock); }
                if (closing_) { return; }
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            try { job.result.set_value(gui_forms::AudioClip::load_wav(job.path)); }
            catch (...) { job.result.set_exception(std::current_exception()); }
        }
    }
};
PcmLoader& loader() { static PcmLoader instance; return instance; }
}
void PcmPlayer::report(gui_forms::AudioStatus status, const std::string& operation) {
    if (status != gui_forms::AudioStatus::ok) {
        std::cerr << "Audio " << operation << ": status " << static_cast<int>(status) << '\n';
    }
}
void PcmPlayer::create_voice(Slot& slot) {
    if (!slot.clip || engine_.status() != gui_forms::AudioStatus::ok) { return; }
    report(engine_.voice(slot.clip, slot.loop, slot.voice, !slot.loop), slot.name);
    report(slot.voice.set_gain(slot.gain), "gain");
    report(slot.voice.set_rate(slot.rate), "rate");
    if (!slot.paused) { report(slot.voice.play(), "play"); }
}
void PcmPlayer::start(std::size_t index, const std::string& name, bool loop, double gain, double rate) {
    if (index >= slots_.size() || name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos) { return; }
    if (!attempted_) {
        attempted_ = true;
        report(engine_.open(), "device open");
    }
    if (engine_.status() != gui_forms::AudioStatus::ok) { return; }
    Slot& slot = slots_[index];
    if (slot.name != name) {
        clear(index);
        slot.name = name;
        slot.pending = loader().request(std::filesystem::path(asset_directory()) / "audio" / (name + ".wav"));
    }
    slot.loop = loop;
    slot.gain = gain;
    slot.rate = rate;
    slot.paused = false;
    if (slot.clip) {
        slot.voice = gui_forms::AudioVoice{};
        create_voice(slot);
    }
}
void PcmPlayer::gain(std::size_t index, double value) {
    Slot& slot = slots_.at(index);
    slot.gain = value;
    if (slot.clip) { report(slot.voice.set_gain(value), "gain"); }
}
void PcmPlayer::pause(std::size_t index) {
    Slot& slot = slots_.at(index);
    slot.paused = true;
    if (slot.clip) { report(slot.voice.pause(), "pause"); }
}
void PcmPlayer::resume(std::size_t index) {
    Slot& slot = slots_.at(index);
    slot.paused = false;
    if (slot.clip) { report(slot.voice.play(), "resume"); }
}
void PcmPlayer::clear(std::size_t index) { slots_.at(index) = Slot{}; }
bool PcmPlayer::playing(std::size_t index) const {
    const Slot& slot = slots_.at(index);
    return !slot.paused && (slot.pending.valid() || slot.voice.playing());
}
void PcmPlayer::tick() {
    for (Slot& slot : slots_) {
        if (!slot.pending.valid() || slot.pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { continue; }
        try {
            gui_forms::AudioClipResult result = slot.pending.get();
            report(result.status, slot.name);
            slot.clip = std::move(result.clip);
            create_voice(slot);
        } catch (const std::exception& error) {
            std::cerr << "Audio " << slot.name << ": " << error.what() << '\n';
        }
    }
}
void PcmPlayer::shutdown() {
    for (std::size_t i = 0; i < slots_.size(); ++i) { clear(i); }
    engine_.shutdown();
    attempted_ = false;
}
}
