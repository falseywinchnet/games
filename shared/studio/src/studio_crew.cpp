#include "studio_crew.hpp"

namespace studio {

Team::Team(const Crew& crew) : crew_(crew) {
    if (crew_.start != nullptr && crew_.join != nullptr && crew_.helpers > 0) {
        handle_ = crew_.start(crew_.helpers, &Team::helper, this);
    }
}

Team::~Team() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (handle_ != nullptr) {
        crew_.join(handle_);
    }
}

void Team::take(std::size_t generation) {
    for (;;) {
        std::size_t index = 0;
        void (*job)(void*, std::size_t) = nullptr;
        void* context = nullptr;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (generation_ != generation || next_ >= count_) {
                return;
            }
            index = next_++;
            job = job_;
            context = context_;
        }
        std::exception_ptr failed{};
        try {
            job(context, index);
        } catch (...) {
            failed = std::current_exception();
        }
        const std::lock_guard<std::mutex> lock(mutex_);
        if (failed && !failure_) {
            failure_ = failed;
        }
        if (++finished_ == count_) {
            done_.notify_all();
        }
    }
}

void Team::helper(void* self) {
    Team& team = *static_cast<Team*>(self);
    std::size_t seen = 0;
    for (;;) {
        std::size_t generation = 0;
        {
            std::unique_lock<std::mutex> lock(team.mutex_);
            team.wake_.wait(lock, [&] { return team.stopping_ || team.generation_ != seen; });
            if (team.stopping_) {
                return;
            }
            generation = seen = team.generation_;
        }
        team.take(generation);
    }
}

void Team::run(std::size_t count, void (*job)(void*, std::size_t), void* context) {
    if (count == 0) {
        return;
    }
    std::size_t generation = 0;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        job_ = job;
        context_ = context;
        count_ = count;
        finished_ = 0;
        failure_ = nullptr;
        next_ = 0;
        generation = ++generation_;
    }
    if (handle_ != nullptr) {
        wake_.notify_all();
    }
    take(generation);
    std::exception_ptr failed{};
    {
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock, [&] { return finished_ == count_; });
        failed = failure_;
    }
    if (failed) {
        std::rethrow_exception(failed);
    }
}

}  // namespace studio
