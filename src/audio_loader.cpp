#include "audio_loader.hpp"
#include "runtime_paths.hpp"
#include "voice_bank.hpp"
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <optional>

namespace games {
namespace {
// The decoding thread is a GUI.Forms Worker; each clip carries its own
// CancellationFlag, which GUI.Forms' Ogg decoder observes. A name a game registered
// as a live cue (voice::Bank, shared/voice) is rendered on the same Worker instead of
// read from a file, and the clip is kept for every later request.
class PcmLoader final {
public:
    PcmLoader() : worker_(&PcmLoader::work, this) {}
    ~PcmLoader() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closing_ = true;
            if (current_) { (*current_).request(); }
            for (Job& job : jobs_) { (*job.cancellation).request(); }
        }
        wake_.notify_one();
        worker_.request_cancel();
        worker_.join();
    }
    std::future<gui_forms::AudioClipResult> request(const std::filesystem::path& path, const std::string& name,
                                                    std::shared_ptr<gui_forms::CancellationFlag> cancellation) {
        Job job{};
        job.path = path;
        job.name = name;
        job.cue = voice::Bank::shared().find(name);
        job.cancellation = std::move(cancellation);
        std::future<gui_forms::AudioClipResult> result = job.result.get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (job.cue.has_value()) {
                const std::map<std::string, std::shared_ptr<const gui_forms::AudioClip>>::const_iterator kept =
                    live_clips_.find(name);
                if (kept != live_clips_.end()) {
                    job.result.set_value({(*kept).second, gui_forms::AudioStatus::ok});
                    return result;
                }
            }
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
        std::string name{};
        std::optional<voice::Cue> cue{};
        std::promise<gui_forms::AudioClipResult> result{};
        std::shared_ptr<gui_forms::CancellationFlag> cancellation{};
    };
    std::mutex mutex_{};
    std::condition_variable wake_{};
    std::deque<Job> jobs_{};
    bool closing_{};
    std::shared_ptr<gui_forms::CancellationFlag> current_{};
    // Rendered live cues by name, kept for the life of the process.
    std::map<std::string, std::shared_ptr<const gui_forms::AudioClip>> live_clips_{};
    gui_forms::Worker worker_; // declared after everything it uses
    static void work(const gui_forms::CancellationFlag&, void* context) {
        (*static_cast<PcmLoader*>(context)).run();
    }
    // A live cue's clip: the kept one, or rendered now and kept.
    gui_forms::AudioClipResult live(const std::string& name, const voice::Cue& cue) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const std::map<std::string, std::shared_ptr<const gui_forms::AudioClip>>::const_iterator kept =
                live_clips_.find(name);
            if (kept != live_clips_.end()) {
                const gui_forms::AudioClipResult result{(*kept).second, gui_forms::AudioStatus::ok};
                return result;
            }
        }
        const voice::Interleaved samples = voice::render(cue);
        gui_forms::AudioClipResult rendered = gui_forms::AudioClip::copy(samples);
        if (rendered.clip) {
            std::lock_guard<std::mutex> lock(mutex_);
            live_clips_[name] = rendered.clip;
        }
        return rendered;
    }
    void run() {
        for (;;) {
            Job job{};
            {
                std::unique_lock<std::mutex> lock(mutex_);
                while (jobs_.empty() && !closing_) { wake_.wait(lock); }
                if (jobs_.empty() && closing_) { return; }
                job = std::move(jobs_.front());
                jobs_.pop_front();
                current_ = job.cancellation;
            }
            try {
                const gui_forms::CancellationFlag& cancellation = *job.cancellation;
                gui_forms::AudioClipResult decoded{{}, gui_forms::AudioStatus::cancelled};
                if (job.cue.has_value()) {
                    decoded = live(job.name, *job.cue);
                } else if (!cancellation.requested()) {
                    std::filesystem::path compressed = job.path;
                    compressed += ".ogg";
                    const bool has_compressed = std::filesystem::exists(compressed);
                    if (has_compressed) { decoded = gui_forms::AudioClip::load_ogg(compressed, cancellation); }
                    else {
                        std::filesystem::path pcm = job.path;
                        pcm += ".wav";
                        decoded = gui_forms::AudioClip::load_wav(pcm);
                    }
                }
                if (cancellation.requested()) { decoded = {{}, gui_forms::AudioStatus::cancelled}; }
                job.result.set_value(std::move(decoded));
            }
            catch (...) { job.result.set_exception(std::current_exception()); }
        }
    }
};
}
std::future<gui_forms::AudioClipResult> load_audio_clip(
    const std::string& name, std::shared_ptr<gui_forms::CancellationFlag> cancellation) {
    static PcmLoader decoder;
    if (!cancellation) { cancellation = std::make_shared<gui_forms::CancellationFlag>(); }
    const std::filesystem::path path = std::filesystem::path(asset_directory()) / "audio" / name;
    std::future<gui_forms::AudioClipResult> result = decoder.request(path, name, std::move(cancellation));
    return result;
}
}
