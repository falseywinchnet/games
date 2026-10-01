#include "audio.hpp"
#include "audio_pcm.hpp"
#include "storage.hpp"
#import <Foundation/Foundation.h>
#include <map>
namespace games {
static AVAudioEngine* engine = nil;
static AVAudioPlayerNode* music = nil;
static AVAudioPCMBuffer* loop = nil;
static NSMutableDictionary* effects = nil;
static std::string current;
static bool music_enabled = false;
static std::map<std::string, unsigned> variants;
static AVAudioPlayer* stinger = nil;
std::string asset_directory() {
    NSString* resources = [[NSBundle mainBundle] resourcePath];
    NSString* assets = [resources stringByAppendingPathComponent:@"assets"];
    if ([[NSFileManager defaultManager] fileExistsAtPath:assets])
        return std::string([assets UTF8String]);
    return std::string(GAMES_ASSET_DIR);
}
static NSString* audio_path(const std::string& name) {
    std::string path = asset_directory() + "/audio/" + name + ".m4a";
    return [NSString stringWithUTF8String:path.c_str()];
}
static std::string track_name(const std::string& name) {
    return name == "solitaire" ? "klondike" : name;
}
AVAudioPCMBuffer* load_loop_pcm(const std::string& name) {
    NSString* manifest_path =
        [NSString stringWithUTF8String:(asset_directory() + "/audio/audio_manifest.json").c_str()];
    NSData* data = [NSData dataWithContentsOfFile:manifest_path];
    if (!data)
        return nil;
    NSDictionary* manifest = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
    if (![manifest isKindOfClass:[NSDictionary class]])
        return nil;
    for (NSDictionary* entry in [manifest objectForKey:@"music"]) {
        if ([[entry objectForKey:@"id"]
                isEqualToString:[NSString stringWithUTF8String:track_name(name).c_str()]]) {
            AVAudioFrameCount count =
                [[entry objectForKey:@"loop_end_sample_exclusive"] unsignedIntValue];
            if ([[entry objectForKey:@"loop_start_sample"] unsignedIntValue] != 0 || count == 0 ||
                count > 48000 * 600)
                return nil;
            NSError* error = nil;
            AVAudioFile* file = [[AVAudioFile alloc]
                initForReading:[NSURL fileURLWithPath:audio_path("music_" + track_name(name) +
                                                                 "_loop")]
                         error:&error];
            if (!file || file.length < count || file.processingFormat.sampleRate != 48000 ||
                file.processingFormat.channelCount != 2) {
                [file release];
                return nil;
            }
            AVAudioPCMBuffer* buffer =
                [[AVAudioPCMBuffer alloc] initWithPCMFormat:file.processingFormat
                                              frameCapacity:count];
            bool ok = [file readIntoBuffer:buffer frameCount:count error:&error] &&
                      buffer.frameLength == count;
            [file release];
            if (!ok) {
                [buffer release];
                return nil;
            }
            return buffer;
        }
    }
    return nil;
}
void music_play(const std::string& game, bool enabled) {
    music_enabled = enabled;
    if (game.empty()) {
        [music stop];
        [engine stop];
        [stinger stop];
        [loop release];
        loop = nil;
        current.clear();
        return;
    }
    if (!enabled) {
        [music pause];
        return;
    }
    if (engine == nil) {
        engine = [[AVAudioEngine alloc] init];
        music = [[AVAudioPlayerNode alloc] init];
        [engine attachNode:music];
    }
    if (current != game || loop == nil) {
        [music stop];
        [stinger stop];
        [engine stop];
        [loop release];
        loop = load_loop_pcm(game);
        current = game;
        if (!loop)
            return;
        [engine connect:music to:engine.mainMixerNode format:loop.format];
        [music scheduleBuffer:loop
                       atTime:nil
                      options:AVAudioPlayerNodeBufferLoops
            completionHandler:nil];
    }
    music.volume = 0.4f;
    NSError* error = nil;
    if (!engine.running && ![engine startAndReturnError:&error])
        return;
    [music play];
}
static std::string effect_name(const std::string& name) {
    std::string base = name;
    unsigned count = 1;
    if (name == "deal") {
        base = "card_shuffle";
        count = 2;
    }
    if (name == "flip") {
        base = "card_flip";
        count = 4;
    }
    if (name == "select") {
        base = "card_pickup";
        count = 4;
    }
    if (name == "place") {
        base = "card_place";
        count = 4;
    }
    if (name == "foundation") {
        base = "card_foundation";
        count = 4;
    }
    if (name == "undo") {
        base = "ui_undo";
        count = 2;
    }
    if (name == "hint") {
        base = "ui_hint";
        count = 2;
    }
    if (name == "invalid") {
        base = "ui_invalid";
        count = 2;
    }
    if (name == "win")
        return "stinger_win_" + track_name(current);
    if (name == "sudoku_digit_place" || name == "sudoku_note_place") {
        base = name;
        count = 3;
        return base + "_0" + std::to_string(1 + variants[base]++ % count);
    }
    if (base != name)
        return base + "_0" + std::to_string(1 + variants[base]++ % count);
    static std::map<std::string, int> variant_counts;
    std::map<std::string, int>::iterator known = variant_counts.find(base);
    if (known == variant_counts.end()) {
        int available = 0;
        while (available < 9 &&
               [[NSFileManager defaultManager]
                   fileExistsAtPath:audio_path(base + "_0" + std::to_string(available + 1))])
            ++available;
        variant_counts[base] = available;
        count = available;
    } else
        count = (*known).second;
    if (count)
        return base + "_0" + std::to_string(1 + variants[base]++ % count);
    return base;
}
void sound_play(const std::string& name, bool enabled) {
    if (!enabled)
        return;
    if (effects == nil)
        effects = [[NSMutableDictionary alloc] init];
    std::string mapped = effect_name(name);
    NSString* key = [NSString stringWithUTF8String:mapped.c_str()];
    AVAudioPlayer* player = [effects objectForKey:key];
    if (player == nil) {
        NSString* path = audio_path(mapped);
        player = [[AVAudioPlayer alloc] initWithContentsOfURL:[NSURL fileURLWithPath:path]
                                                        error:nil];
        if (player == nil)
            return;
        player.volume = 1.0f;
        [player prepareToPlay];
        [effects setObject:player forKey:key];
        [player release];
    }
    if (name == "win" || mapped.rfind("stinger_", 0) == 0) {
        // One game-specific stinger; stop the loop rather than stack two victory cues.
        [music pause];
        [stinger stop];
        stinger = player;
    } else if (name == "deal") {
        [stinger stop];
        if (music_enabled)
            [music play];
    }
    player.currentTime = 0;
    [player play];
}
} // namespace games
