#include "audio_loader.hpp"
#include "runtime_paths.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace games {
namespace {
class PcmLoader final {
public:
    PcmLoader() : worker_(&PcmLoader::run, this) {}
    ~PcmLoader() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closing_ = true;
            current_.request_stop();
            for (Job& job : jobs_) { job.cancellation.request_stop(); }
        }
        wake_.notify_one();
        worker_.join();
    }
    std::future<gui_forms::AudioClipResult> request(const std::filesystem::path& path, std::stop_source cancellation) {
        Job job{};
        job.path = path;
        job.cancellation = std::move(cancellation);
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
        std::stop_source cancellation{};
    };
    std::mutex mutex_{};
    std::condition_variable wake_{};
    std::deque<Job> jobs_{};
    bool closing_{};
    std::stop_source current_{};
    std::thread worker_;
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
                const std::stop_token cancellation = job.cancellation.get_token();
                gui_forms::AudioClipResult decoded{{}, gui_forms::AudioStatus::cancelled};
                if (!cancellation.stop_requested()) {
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
                if (cancellation.stop_requested()) { decoded = {{}, gui_forms::AudioStatus::cancelled}; }
                job.result.set_value(std::move(decoded));
            }
            catch (...) { job.result.set_exception(std::current_exception()); }
        }
    }
};
}
std::future<gui_forms::AudioClipResult> load_audio_clip(const std::string& name, std::stop_source cancellation) {
    static PcmLoader decoder;
    const std::filesystem::path path = std::filesystem::path(asset_directory()) / "audio" / name;
    std::future<gui_forms::AudioClipResult> result = decoder.request(path, std::move(cancellation));
    return result;
}
}
