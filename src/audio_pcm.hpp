#pragma once
#import <AVFoundation/AVFoundation.h>
#include <string>
namespace games {
// Returns an owned PCM buffer trimmed to the producer's [0,N) loop bounds.
// Native AudioToolbox decoding removes AAC priming; remaining tail padding is excluded.
AVAudioPCMBuffer* load_loop_pcm(const std::string& name);
} // namespace games
