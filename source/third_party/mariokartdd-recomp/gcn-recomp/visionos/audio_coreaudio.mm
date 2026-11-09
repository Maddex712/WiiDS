// Core Audio output for the portable mix in audio.cpp, on visionOS: a RemoteIO unit pulling
// 48 kHz stereo 16-bit, which is what the mix produces. The desktop uses audio_sdl.cpp and
// Android audio_aaudio.cpp.
#include "runtime.h"
#include <AVFAudio/AVFAudio.h>
#include <AudioToolbox/AudioToolbox.h>
#include <cstdio>

void audio_render(int16_t* out, int frames);
void audio_open_wav();

static constexpr double OUT_RATE = 48000.0;
static AudioComponentInstance g_unit;

static OSStatus on_render(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames,
                          AudioBufferList* io) {
    audio_render((int16_t*)io->mBuffers[0].mData, (int)frames);
    // That the device is still pulling: the frontend's stats cannot tell a stopped unit
    // from a ring that is refilling.
    static uint64_t pulled;
    if ((pulled + frames) / (uint64_t)(OUT_RATE * 30) != pulled / (uint64_t)(OUT_RATE * 30))
        fprintf(stderr, "audio: %llu s pulled\n", (unsigned long long)((pulled + frames) / (uint64_t)OUT_RATE));
    pulled += frames;
    return noErr;
}

// Reactivates the session and restarts the unit, every second until that works: the
// system does not always say when an interruption has ended.
static void resume_after_interruption(int tries) {
    NSError* err = nil;
    if ([AVAudioSession.sharedInstance setActive:YES error:&err] && AudioOutputUnitStart(g_unit) == noErr) {
        fprintf(stderr, "audio: resumed\n");
        return;
    }
    if (tries == 0) fprintf(stderr, "audio: cannot resume yet: %s\n", err.localizedDescription.UTF8String);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        resume_after_interruption(tries + 1);
    });
}

// The app's audio session: playback, which an immersive game is, rather than the default
// session the output unit otherwise runs under.
static void start_session() {
    AVAudioSession* s = AVAudioSession.sharedInstance;
    NSError* err = nil;
    if (![s setCategory:AVAudioSessionCategoryPlayback error:&err])
        fprintf(stderr, "audio: session category: %s\n", err.localizedDescription.UTF8String);
    [s setPreferredSampleRate:OUT_RATE error:nil];
    if (![s setActive:YES error:&err])
        fprintf(stderr, "audio: session activation: %s\n", err.localizedDescription.UTF8String);
    [NSNotificationCenter.defaultCenter addObserverForName:AVAudioSessionInterruptionNotification object:s queue:nil
                                                usingBlock:^(NSNotification* n) {
        const auto type = (AVAudioSessionInterruptionType)[n.userInfo[AVAudioSessionInterruptionTypeKey] integerValue];
        fprintf(stderr, "audio: interruption %s (reason %ld)\n",
                type == AVAudioSessionInterruptionTypeBegan ? "began" : "ended",
                (long)[n.userInfo[AVAudioSessionInterruptionReasonKey] integerValue]);
        if (type == AVAudioSessionInterruptionTypeBegan) {
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
                resume_after_interruption(0);
            });
        } else {
            resume_after_interruption(0);
        }
    }];
}

bool audio_open() {
    audio_open_wav();
    start_session();
    AudioComponentDescription desc{};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_RemoteIO;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (!comp || AudioComponentInstanceNew(comp, &g_unit) != noErr) {
        fprintf(stderr, "audio: no output unit\n");
        return false;
    }
    AudioStreamBasicDescription fmt{};
    fmt.mSampleRate = OUT_RATE;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel = 16;
    fmt.mBytesPerFrame = 4;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerPacket = 4;
    OSStatus r = AudioUnitSetProperty(g_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt,
                                      sizeof(fmt));
    AURenderCallbackStruct cb{on_render, nullptr};
    if (r == noErr)
        r = AudioUnitSetProperty(g_unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb,
                                 sizeof(cb));
    if (r == noErr) r = AudioUnitInitialize(g_unit);
    if (r == noErr) r = AudioOutputUnitStart(g_unit);
    if (r != noErr) {
        fprintf(stderr, "audio: cannot start the output unit (%d)\n", (int)r);
        return false;
    }
    fprintf(stderr, "audio: playing, 48 kHz stereo\n");
    return true;
}
