#include "audio.hpp"

#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <chrono>
#include <map>
#include <vector>

namespace eggy {

namespace {
NSString* g_assets = nil;
AVAudioEngine* g_engine = nil;
struct Track {
    AVAudioPlayerNode* node = nil;
    AVAudioPCMBuffer* buffer = nil;
    std::string name;
    float vol = 0, target = 0;
};
Track g_tracks[2];
int g_front = 0;
std::string g_want;
bool g_music_on = true;
bool g_foreground = false, g_master_music = true, g_master_sound = true;
float g_duck = 0;
std::map<std::string, std::vector<AVAudioPlayer*>> g_sfx;
std::map<std::string, unsigned> g_sfx_next;
constexpr int kBeds = 5;
AVAudioPlayer* g_amb[kBeds] = {nil, nil, nil, nil, nil};
float g_amb_target[kBeds] = {}, g_amb_vol[kBeds] = {};

NSString* path_for(const std::string& rel) {
    return [g_assets stringByAppendingPathComponent:[NSString stringWithUTF8String:rel.c_str()]];
}

AVAudioPCMBuffer* load_pcm(const std::string& name) {
    NSString* p = path_for("audio/" + name + ".m4a");
    if (![[NSFileManager defaultManager] fileExistsAtPath:p]) p = path_for("audio/" + name + ".wav");
    NSError* err = nil;
    AVAudioFile* f = [[AVAudioFile alloc] initForReading:[NSURL fileURLWithPath:p] error:&err];
    if (!f) return nil;
    // loop length from the manifest if present (trims encoder padding)
    AVAudioFrameCount count = static_cast<AVAudioFrameCount>(f.length);
    NSData* md = [NSData dataWithContentsOfFile:path_for("audio/eggy_audio_manifest.json")];
    if (md) {
        NSDictionary* m = [NSJSONSerialization JSONObjectWithData:md options:0 error:nil];
        for (NSDictionary* e in m[@"music"])
            if ([e[@"id"] isEqualToString:[NSString stringWithUTF8String:name.c_str()]]) {
                const AVAudioFrameCount c = [e[@"loop_end_sample_exclusive"] unsignedIntValue];
                if (c > 0 && c <= f.length) count = c;
            }
    }
    AVAudioPCMBuffer* b = [[AVAudioPCMBuffer alloc] initWithPCMFormat:f.processingFormat frameCapacity:count];
    const bool ok = [f readIntoBuffer:b frameCount:count error:&err];
    [f release];
    if (!ok) { [b release]; return nil; }
    return b;
}

void start_track(Track& t, const std::string& name) {
    if (t.node) { [t.node stop]; [g_engine detachNode:t.node]; [t.node release]; t.node = nil; }
    if (t.buffer) { [t.buffer release]; t.buffer = nil; }
    t.name = name;
    t.vol = 0;
    if (name.empty()) return;
    t.buffer = load_pcm(name);
    if (!t.buffer) return;
    t.node = [[AVAudioPlayerNode alloc] init];
    [g_engine attachNode:t.node];
    [g_engine connect:t.node to:g_engine.mainMixerNode format:t.buffer.format];
    t.node.volume = 0;
    [t.node scheduleBuffer:t.buffer atTime:nil options:AVAudioPlayerNodeBufferLoops completionHandler:nil];
    if (!g_engine.running) [g_engine startAndReturnError:nil];
    [t.node play];
}

AVAudioPlayer* make_player(const std::string& name) {
    NSString* p = path_for("audio/" + name + ".m4a");
    if (![[NSFileManager defaultManager] fileExistsAtPath:p]) p = path_for("audio/" + name + ".wav");
    if (![[NSFileManager defaultManager] fileExistsAtPath:p]) return nil;
    AVAudioPlayer* pl = [[AVAudioPlayer alloc] initWithContentsOfURL:[NSURL fileURLWithPath:p] error:nil];
    if (!pl) return nil;
    pl.enableRate = YES;
    [pl prepareToPlay];
    return pl;
}
}  // namespace

std::string asset_dir() {
    NSString* res = [[NSBundle mainBundle] resourcePath];
    NSString* a = [res stringByAppendingPathComponent:@"assets"];
    if ([[NSFileManager defaultManager] fileExistsAtPath:a]) return [a UTF8String];
#ifdef EGGY_ASSET_DIR
    return EGGY_ASSET_DIR;
#else
    return "assets";
#endif
}

double backing_scale() {
    NSScreen* s = [NSScreen mainScreen];
    return s ? s.backingScaleFactor : 2.0;
}

double wall_clock() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void audio_start(const std::string& dir) {
    if (g_assets) return;
    g_assets = [[NSString alloc] initWithUTF8String:dir.c_str()];
    g_engine = [[AVAudioEngine alloc] init];
    const char* amb[kBeds] = {"eggy_amb_wind", "eggy_amb_brook", "eggy_amb_forest", "eggy_amb_rain", "eggy_amb_fire"};
    for (int i = 0; i < kBeds; ++i) {
        g_amb[i] = make_player(amb[i]);
        if (g_amb[i]) { g_amb[i].numberOfLoops = -1; g_amb[i].volume = 0; [g_amb[i] play]; }
    }
}

void audio_music(const std::string& track, bool enabled) {
    enabled = enabled && g_foreground && g_master_music;
    g_music_on = enabled;
    if (!g_foreground) return;
    if (!g_engine) return;
    if (track == g_want) return;
    g_want = track;
    // the back slot takes the new track and fades in while the front fades out
    const int back = 1 - g_front;
    start_track(g_tracks[back], track);
    g_front = back;
}

void audio_ambience(float wind, float water, float forest, float rain, float fire, bool enabled) {
    enabled = enabled && g_foreground && g_master_sound;
    const float v[kBeds] = {wind, water, forest, rain, fire};
    for (int i = 0; i < kBeds; ++i) g_amb_target[i] = enabled ? v[i] : 0;
}

void audio_duck_music(float amount) { g_duck = std::max(g_duck, amount); }

void audio_sfx(const std::string& name, float gain, float rate, bool enabled) {
    if (!enabled || !g_assets || !g_foreground || !g_master_sound) return;
    auto& pool = g_sfx[name];
    if (pool.empty()) {
        for (int i = 0; i < 3; ++i) {
            AVAudioPlayer* p = make_player(name);
            if (!p) break;
            pool.push_back(p);
        }
        if (pool.empty()) { pool.push_back(nil); }
    }
    AVAudioPlayer* p = pool[g_sfx_next[name]++ % pool.size()];
    if (!p) return;
    p.volume = std::min(1.f, gain);
    p.rate = std::clamp(rate, .5f, 2.f);
    p.currentTime = 0;
    [p play];
}

void audio_tick(double dt) {
    if (!g_engine || !g_foreground) return;
    g_duck = std::max(0.f, g_duck - static_cast<float>(dt) * .25f);
    const float music_gain = .42f * (1 - .75f * std::min(1.f, g_duck));
    for (int i = 0; i < 2; ++i) {
        Track& t = g_tracks[i];
        t.target = (i == g_front && g_music_on) ? music_gain : 0.f;
        const float step = static_cast<float>(dt) / 3.5f;  // ~3.5 s crossfade
        if (t.vol < t.target) t.vol = std::min(t.target, t.vol + step);
        else t.vol = std::max(t.target, t.vol - step);
        if (t.node) t.node.volume = t.vol;
        if (i != g_front && t.vol <= 0 && t.node) start_track(t, "");
    }
    for (int i = 0; i < kBeds; ++i) {
        g_amb_vol[i] += (g_amb_target[i] - g_amb_vol[i]) * std::min(1.f, static_cast<float>(dt) * 1.2f);
        if (g_amb[i]) {
            g_amb[i].volume = g_amb_vol[i];
            // silent beds are paused, not mixed at zero volume
            if (g_amb_vol[i] < .005f && g_amb_target[i] < .005f) { if (g_amb[i].playing) [g_amb[i] pause]; }
            else if (!g_amb[i].playing) [g_amb[i] play];
        }
    }
}

void audio_cabinet(bool foreground, bool music, bool sound) {
    g_foreground = foreground; g_master_music = music; g_master_sound = sound;
    if (!foreground || !music) {
        g_music_on = false;
        for (Track& t : g_tracks) { t.vol = t.target = 0; if(t.node)t.node.volume=0; }
    }
    if (!foreground || !sound) {
        for(int i=0;i<kBeds;++i) { g_amb_vol[i]=g_amb_target[i]=0; [g_amb[i] pause]; }
        for(auto& [name,pool]:g_sfx) for(AVAudioPlayer* p:pool) [p stop];
    }
}

void audio_stop() {
    for (Track& t : g_tracks) if (t.node) [t.node stop];
    for (AVAudioPlayer* a : g_amb) [a stop];
    [g_engine stop];
}

}  // namespace eggy
