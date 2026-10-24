#include "batch_cues.hpp"

#include "batch_parts.hpp"

namespace batch_cues {

std::vector<voice::Cue> all_cues() {
    std::vector<voice::Cue> cues{};
    const std::span<const voice::Cue> parts[] = {sfx_cues(), sfx2_cues(), music_cues()};
    for (const std::span<const voice::Cue> part : parts) {
        cues.insert(cues.end(), part.begin(), part.end());
    }
    return cues;
}

void register_batch_cues() {
    voice::Bank::shared().add(sfx_cues());
    voice::Bank::shared().add(sfx2_cues());
    voice::Bank::shared().add(music_cues());
}

}  // namespace batch_cues
