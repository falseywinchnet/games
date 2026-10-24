#include "voice_bank.hpp"

#include <algorithm>

namespace voice {

Interleaved interleave(std::span<const double> mono) {
    Interleaved result(mono.size() * 2);
    for (std::size_t i = 0; i < mono.size(); ++i) {
        const float value = static_cast<float>(mono[i]);
        result[2 * i] = value;
        result[2 * i + 1] = value;
    }
    return result;
}

Interleaved interleave(const Stereo& signal) {
    const std::size_t frames = std::min(signal.left.size(), signal.right.size());
    Interleaved result(frames * 2);
    for (std::size_t i = 0; i < frames; ++i) {
        result[2 * i] = static_cast<float>(signal.left[i]);
        result[2 * i + 1] = static_cast<float>(signal.right[i]);
    }
    return result;
}

Interleaved render(const Cue& cue) {
    if (cue.mono != nullptr) {
        const Interleaved result = interleave(cue.mono());
        return result;
    }
    if (cue.stereo != nullptr) {
        const Interleaved result = interleave(cue.stereo());
        return result;
    }
    return Interleaved{};
}

Bank& Bank::shared() {
    static Bank bank;
    return bank;
}

void Bank::add(std::span<const Cue> cues) {
    const std::lock_guard<std::mutex> lock(mutex_);
    for (const Cue& cue : cues) {
        bool replaced = false;
        for (Cue& existing : cues_) {
            if (existing.name == cue.name) {
                existing = cue;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            cues_.push_back(cue);
        }
    }
}

std::optional<Cue> Bank::find(std::string_view name) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    for (const Cue& cue : cues_) {
        if (cue.name == name) {
            return cue;
        }
    }
    return std::nullopt;
}

std::vector<std::string> Bank::names() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result{};
    for (const Cue& cue : cues_) {
        result.emplace_back(cue.name);
    }
    return result;
}

}  // namespace voice
