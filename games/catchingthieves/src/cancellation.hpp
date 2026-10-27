#pragma once
#include <atomic>

namespace ct {
// Non-owning cooperative cancellation for synchronous searches. The worker owns
// the source and is joined before it is destroyed. An atomic flag also works
// with the Apple C++ library used by the independent core CI profile, which does
// not yet expose std::stop_token. No rule or platform dependency is introduced.
class CancellationToken {
  public:
    CancellationToken() = default;
    explicit CancellationToken(const std::atomic_bool& requested) : requested_(&requested) {}
    bool stop_requested() const {
        return requested_ && (*requested_).load(std::memory_order_relaxed);
    }
  private:
    const std::atomic_bool* requested_ = nullptr;
};
class CancellationSource {
  public:
    void request_stop() { requested_.store(true,std::memory_order_relaxed); }
    bool stop_requested() const { return requested_.load(std::memory_order_relaxed); }
    CancellationToken get_token() const { return CancellationToken(requested_); }
  private:
    std::atomic_bool requested_{false};
};
} // namespace ct
