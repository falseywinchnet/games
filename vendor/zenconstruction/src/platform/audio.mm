#include "audio.hpp"

#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <mach/mach_time.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

namespace zc {

namespace {
NSString* g_assets = nil;
AVAudioEngine* g_engine = nil;
struct Track {
    AVAudioPlayerNode* node = nil;
    AVAudioPCMBuffer* buffer = nil;
    std::string name;
    float vol = 0, target = 0;
    double fade_at = -1;   // host seconds: hold full volume until then, then fade fast (a bar-line cut)
    bool instant = false;  // comes in at full volume when its scheduled start arrives
};
std::map<std::string, AVAudioFramePosition> g_bar;  // bar length in frames, per music id

double host_now() { return [AVAudioTime secondsForHostTime:mach_absolute_time()]; }
Track g_tracks[2];
int g_front = 0;
std::string g_want;
bool g_music_on = true;
float g_duck = 0;
std::map<std::string, std::vector<AVAudioPlayer*>> g_sfx;
// looping beds: a player through a varispeed (for pitch) into the mixer
struct Bed {
    AVAudioPlayerNode* node = nil;
    AVAudioUnitVarispeed* speed = nil;
    AVAudioPCMBuffer* buffer = nil;
    float gain = 0, target_gain = 0;
    float rate = 1, target_rate = 1;
    bool missing = false;
};
std::map<std::string, Bed> g_beds;
std::map<std::string, unsigned> g_sfx_next;

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
    NSData* md = [NSData dataWithContentsOfFile:path_for("audio/zen_audio_manifest.json")];
    if (md) {
        NSDictionary* m = [NSJSONSerialization JSONObjectWithData:md options:0 error:nil];
        for (NSDictionary* e in m[@"music"])
            if ([e[@"id"] isEqualToString:[NSString stringWithUTF8String:name.c_str()]]) {
                const AVAudioFrameCount c = [e[@"loop_end_sample_exclusive"] unsignedIntValue];
                if (c > 0 && c <= f.length) count = c;
                if (e[@"bar_samples"]) g_bar[name] = [e[@"bar_samples"] longLongValue];
            }
    }
    AVAudioPCMBuffer* b = [[AVAudioPCMBuffer alloc] initWithPCMFormat:f.processingFormat frameCapacity:count];
    const bool ok = [f readIntoBuffer:b frameCount:count error:&err];
    [f release];
    if (!ok) { [b release]; return nil; }
    return b;
}

void start_track(Track& t, const std::string& name, std::uint64_t host_start = 0) {
    if (t.node) { [t.node stop]; [g_engine detachNode:t.node]; [t.node release]; t.node = nil; }
    if (t.buffer) { [t.buffer release]; t.buffer = nil; }
    t.name = name;
    t.vol = 0;
    t.fade_at = -1;
    t.instant = host_start != 0;
    if (name.empty()) return;
    t.buffer = load_pcm(name);
    if (!t.buffer) return;
    t.node = [[AVAudioPlayerNode alloc] init];
    [g_engine attachNode:t.node];
    [g_engine connect:t.node to:g_engine.mainMixerNode format:t.buffer.format];
    t.node.volume = 0;
    [t.node scheduleBuffer:t.buffer atTime:nil options:AVAudioPlayerNodeBufferLoops completionHandler:nil];
    if (!g_engine.running) [g_engine startAndReturnError:nil];
    if (host_start) [t.node playAtTime:[AVAudioTime timeWithHostTime:host_start]];
    else [t.node play];
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
#ifdef ZC_ASSET_DIR
    return ZC_ASSET_DIR;
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
}

void audio_music(const std::string& track, bool enabled) {
    g_music_on = enabled;
    if (!g_engine) return;
    if (track == g_want) return;
    g_want = track;
    // the back slot takes the new track and fades in while the front fades out
    const int back = 1 - g_front;
    start_track(g_tracks[back], track);
    g_front = back;
}

void audio_music_on_bar(const std::string& track, bool enabled) {
    g_music_on = enabled;
    if (!g_engine || track == g_want) return;
    Track& cur = g_tracks[g_front];
    AVAudioTime* nt = cur.node ? cur.node.lastRenderTime : nil;
    AVAudioTime* pt = nt ? [cur.node playerTimeForNodeTime:nt] : nil;
    if (!cur.node || !cur.buffer || !pt || !nt.hostTimeValid || pt.sampleTime < 0 || cur.vol < .05f || !enabled) {
        audio_music(track, enabled);  // nothing audible to stay in time with
        return;
    }
    const double sr = pt.sampleRate;
    const AVAudioFramePosition loop = cur.buffer.frameLength;
    const AVAudioFramePosition pos = pt.sampleTime % loop;
    const AVAudioFramePosition bar = g_bar.count(cur.name) && g_bar[cur.name] > 0 ? g_bar[cur.name] : loop;
    AVAudioFramePosition next = (pos / bar + 1) * bar;
    if (next - pos < static_cast<AVAudioFramePosition>(sr * .08)) next += bar;  // too close to schedule safely
    if (next > loop) next = loop;  // the loop's end is a bar line too
    const double delay = static_cast<double>(next - pos) / sr;
    if (std::getenv("ZC_AUDIO_LOG"))
        std::fprintf(stderr, "music %s -> %s on the bar: pos %lld bar %lld, in %.3f s\n", cur.name.c_str(), track.c_str(),
                     static_cast<long long>(pos), static_cast<long long>(bar), delay);
    g_want = track;
    const int back = 1 - g_front;
    start_track(g_tracks[back], track, nt.hostTime + [AVAudioTime hostTimeForSeconds:delay]);
    cur.fade_at = host_now() + delay;
    g_front = back;
}

void audio_duck_music(float amount) { g_duck = std::max(g_duck, amount); }

void audio_bed(const std::string& name, float gain, float rate, bool enabled) {
    if (!g_engine) return;
    Bed& bed = g_beds[name];
    bed.target_gain = enabled ? std::max(0.f, gain) : 0.f;
    bed.target_rate = std::clamp(rate, .5f, 2.f);
    if (bed.node != nil || bed.missing || bed.target_gain <= 0) return;
    bed.buffer = load_pcm(name);
    if (bed.buffer == nil) {
        bed.missing = true;
        if (std::getenv("ZC_AUDIO_LOG")) std::fprintf(stderr, "bed %s: missing\n", name.c_str());
        return;
    }
    if (std::getenv("ZC_AUDIO_LOG"))
        std::fprintf(stderr, "bed %s: %u frames at %.0f Hz\n", name.c_str(), bed.buffer.frameLength, bed.buffer.format.sampleRate);
    bed.node = [[AVAudioPlayerNode alloc] init];
    bed.speed = [[AVAudioUnitVarispeed alloc] init];
    [g_engine attachNode:bed.node];
    [g_engine attachNode:bed.speed];
    [g_engine connect:bed.node to:bed.speed format:bed.buffer.format];
    [g_engine connect:bed.speed to:g_engine.mainMixerNode format:bed.buffer.format];
    bed.node.volume = 0;
    bed.gain = 0;
    bed.rate = bed.target_rate;
    bed.speed.rate = bed.rate;
    [bed.node scheduleBuffer:bed.buffer atTime:nil options:AVAudioPlayerNodeBufferLoops completionHandler:nil];
    if (!g_engine.running) [g_engine startAndReturnError:nil];
    [bed.node play];
}

void audio_sfx(const std::string& name, float gain, float rate, bool enabled, float pan) {
    if (!enabled || !g_assets) return;
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
    if (std::getenv("ZC_AUDIO_LOG")) std::fprintf(stderr, "sfx %s gain %.2f rate %.2f pan %.2f%s\n", name.c_str(), gain, rate, pan, p ? "" : " (missing)");
    if (!p) return;
    p.volume = std::min(1.f, gain);
    p.rate = std::clamp(rate, .5f, 2.f);
    p.pan = std::clamp(pan, -1.f, 1.f);
    p.currentTime = 0;
    [p play];
}

void audio_tick(double dt) {
    if (!g_engine) return;
    g_duck = std::max(0.f, g_duck - static_cast<float>(dt) * .25f);
    const float music_gain = .42f * (1 - .75f * std::min(1.f, g_duck));
    for (int i = 0; i < 2; ++i) {
        Track& t = g_tracks[i];
        t.target = (i == g_front && g_music_on) ? music_gain : 0.f;
        float step = static_cast<float>(dt) / 3.5f;  // ~3.5 s crossfade
        if (t.instant && i == g_front) step = 1;      // a bar-line cut: in at full strength (it is silent until its start time)
        if (i != g_front && t.fade_at > 0) {
            if (host_now() < t.fade_at) step = 0;      // keep playing to the bar line
            else step = static_cast<float>(dt) / .12f; // then out, quickly
        }
        if (t.vol < t.target) t.vol = std::min(t.target, t.vol + step);
        else t.vol = std::max(t.target, t.vol - step);
        if (t.node) t.node.volume = t.vol;
        if (i != g_front && t.vol <= 0 && t.node) start_track(t, "");
    }
    // beds: quick to swell, slower to die away (a motor spinning down), the
    // pitch gliding after
    const float swell = std::min(1.f, static_cast<float>(dt) / .08f);
    const float fall = std::min(1.f, static_cast<float>(dt) / .35f);
    const float glide = std::min(1.f, static_cast<float>(dt) / .22f);
    for (std::map<std::string, Bed>::iterator it = g_beds.begin(); it != g_beds.end(); ++it) {
        Bed& bed = (*it).second;
        if (bed.node == nil) continue;
        bed.gain += (bed.target_gain - bed.gain) * (bed.target_gain > bed.gain ? swell : fall);
        if (bed.gain < 1e-4f && bed.target_gain <= 0) bed.gain = 0;
        bed.rate += (bed.target_rate - bed.rate) * glide;
        bed.node.volume = bed.gain;
        bed.speed.rate = bed.rate;
    }
}

void audio_stop() {
    if (!g_engine) return;
    // Fade everything out before stopping: a player cut off mid-wave, or one
    // still playing while the engine is torn down, pops or buzzes the speaker.
    if (g_engine.running) {
        AVAudioMixerNode* mixer = g_engine.mainMixerNode;
        const float start = mixer.outputVolume;
        for (std::map<std::string, std::vector<AVAudioPlayer*>>::iterator it = g_sfx.begin(); it != g_sfx.end(); ++it) {
            for (size_t k = 0; k < (*it).second.size(); k += 1) {
                AVAudioPlayer* p = (*it).second[k];
                if (p != nil && p.playing) [p setVolume:0 fadeDuration:0.1];
            }
        }
        for (int step = 1; step <= 12; ++step) {
            const float k = 1.f - static_cast<float>(step) / 12.f;
            mixer.outputVolume = start * k * k;
            [NSThread sleepForTimeInterval:0.01];
        }
        mixer.outputVolume = 0;
        [NSThread sleepForTimeInterval:0.03];
    }
    for (std::map<std::string, std::vector<AVAudioPlayer*>>::iterator it = g_sfx.begin(); it != g_sfx.end(); ++it) {
        for (size_t k = 0; k < (*it).second.size(); k += 1) {
            if ((*it).second[k] != nil) [(*it).second[k] stop];
        }
    }
    for (std::map<std::string, Bed>::iterator it = g_beds.begin(); it != g_beds.end(); ++it) {
        if ((*it).second.node != nil) [(*it).second.node stop];
    }
    for (int i = 0; i < 2; ++i) {
        if (g_tracks[i].node) [g_tracks[i].node stop];
    }
    [g_engine pause];
    [g_engine stop];
}

}  // namespace zc
