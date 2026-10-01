#include "audio_pcm.hpp"
#include "storage.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main() {
    @autoreleasepool {
        const char* names[] = {"menu",         "solitaire",    "spider",        "freecell",
                               "hearts",       "puzzle_main",  "puzzle_tiptoe", "gems",
                               "sudoku_day",   "sudoku_night", "nature_cube",   "untangle",
                               "atom_probe",   "four_pegs",    "switchbox",     "puzzle_solve",
                               "sticks_stones"};
        const unsigned frames[] = {4388608, 6582784, 5857680, 6912000, 4838400, 4538144,
                                   4369728, 5076656, 4850560, 5421184, 5236320, 5632000,
                                   5509504, 5172288, 4680000, 4969360, 5068800};
        for (int i = 0; i < 17; ++i) {
            AVAudioPCMBuffer* loop = games::load_loop_pcm(names[i]);
            require(loop && loop.frameLength == frames[i],
                    "production decoder uses exact manifest bounds");
            AVAudioEngine* engine = [[AVAudioEngine alloc] init];
            AVAudioPlayerNode* node = [[AVAudioPlayerNode alloc] init];
            [engine attachNode:node];
            [engine connect:node to:engine.mainMixerNode format:loop.format];
            NSError* error = nil;
            require([engine enableManualRenderingMode:AVAudioEngineManualRenderingModeOffline
                                               format:loop.format
                                    maximumFrameCount:4096
                                                error:&error],
                    "offline render setup");
            [node scheduleBuffer:loop
                           atTime:nil
                          options:AVAudioPlayerNodeBufferLoops
                completionHandler:nil];
            require([engine startAndReturnError:&error], "offline render engine start");
            [node play];
            AVAudioPCMBuffer* output = [[AVAudioPCMBuffer alloc] initWithPCMFormat:loop.format
                                                                     frameCapacity:4096];
            unsigned position = 0;
            double max_error = 0;
            while (position < frames[i] + 4096) {
                AVAudioEngineManualRenderingStatus status = [engine renderOffline:4096
                                                                         toBuffer:output
                                                                            error:&error];
                require(status == AVAudioEngineManualRenderingStatusSuccess,
                        "offline render succeeds without starvation");
                for (unsigned j = 0; j < output.frameLength; ++j)
                    if (position + j >= frames[i] - 128)
                        for (unsigned channel = 0; channel < 2; ++channel) {
                            double difference =
                                output.floatChannelData[channel][j] -
                                loop.floatChannelData[channel][(position + j) % frames[i]];
                            max_error = std::max(max_error, std::abs(difference));
                        }
                position += output.frameLength;
            }
            require(max_error < 0.00001,
                    "native playback crosses seam exactly without padding or gap");
            std::cout << names[i] << ": " << loop.frameLength << " PCM frames; seam error "
                      << max_error << '\n';
            [node stop];
            [engine stop];
            [output release];
            [node release];
            [engine release];
            [loop release];
        }
        int decoded = 0;
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(games::asset_directory() + "/audio")) {
            if (entry.path().extension() != ".m4a" && entry.path().extension() != ".wav")
                continue;
            NSString* path = [NSString stringWithUTF8String:entry.path().c_str()];
            AVAudioFile* file = [[AVAudioFile alloc] initForReading:[NSURL fileURLWithPath:path]
                                                              error:nil];
            require(file && file.length > 0, "native decoder opens all packaged files");
            AVAudioPCMBuffer* buffer =
                [[AVAudioPCMBuffer alloc] initWithPCMFormat:file.processingFormat
                                              frameCapacity:8192];
            while (file.framePosition < file.length) {
                require([file readIntoBuffer:buffer error:nil] && buffer.frameLength > 0,
                        "full native decode");
                for (unsigned c = 0; c < buffer.format.channelCount; ++c)
                    for (unsigned j = 0; j < buffer.frameLength; ++j)
                        require(std::isfinite(buffer.floatChannelData[c][j]), "finite PCM output");
            }
            ++decoded;
            [buffer release];
            [file release];
        }
        require(decoded == 236, "complete delivered runtime batch");
        std::cout << "Native full decode: " << decoded << "/236 files.\n";
    }
}
