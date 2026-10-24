#pragma once
// Helper threads for a render. The engine starts no threads of its own: whoever runs it
// may lend some (the shell lends GUI.Forms Workers, ui/studio_workers.cpp). Every job a
// render hands out writes its own memory, and everything summed is summed in a fixed
// order afterwards, so a render is the same sample for sample with any number of helpers,
// none included.
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <mutex>

namespace studio {

struct Crew {
    // Runs entry(context) on up to `count` other threads at once and returns a handle at
    // once (fewer threads, or none, is fine: the render's own thread does any work left).
    void* (*start)(int count, void (*entry)(void* context), void* context) = nullptr;
    // Waits for every thread `start` began.
    void (*join)(void* handle) = nullptr;
    int helpers = 0;
};

// Batches of jobs shared by the calling thread and the crew's helpers.
class Team {
  public:
    explicit Team(const Crew& crew);
    ~Team();
    Team(const Team&) = delete;
    Team& operator=(const Team&) = delete;
    // Runs job(context, i) for every i < count and returns when all are done, rethrowing the
    // first exception a job threw.
    void run(std::size_t count, void (*job)(void* context, std::size_t index), void* context);

  private:
    static void helper(void* self);
    void take(std::size_t generation);

    std::mutex mutex_{};
    std::condition_variable wake_{};
    std::condition_variable done_{};
    std::size_t generation_ = 0;
    bool stopping_ = false;
    void (*job_)(void*, std::size_t) = nullptr;
    void* context_ = nullptr;
    std::size_t count_ = 0;
    std::size_t next_ = 0;
    std::size_t finished_ = 0;
    std::exception_ptr failure_{};
    Crew crew_{};
    void* handle_ = nullptr;
};

}  // namespace studio
