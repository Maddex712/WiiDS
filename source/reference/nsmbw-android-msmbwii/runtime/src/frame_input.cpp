#include "frame_input.h"

#include "det_clock.h"
#include "input_bindings.h"
#include "netplay_ui.h"

namespace FrameInput {
namespace {

Frame g_frame{};
Source g_source = nullptr;

bool IsKpad(WiiRemoteInput::Kind kind) {
    return kind == WiiRemoteInput::Kind::Remote || kind == WiiRemoteInput::Kind::RemoteWithNunchuk ||
           kind == WiiRemoteInput::Kind::RemoteWithClassic;
}

} // namespace

bool Active() {
    return DetClock::Enabled();
}

Frame SampleLocal() {
    Frame frame{};
    const bool blocked = InputBindings::InputBlocked();
    for (uint32_t chan = 0; chan < frame.size(); ++chan) {
        Remote& remote = frame[chan];
        remote.kind = WiiRemoteInput::EffectiveKind(chan);
        remote.connected = IsKpad(remote.kind) && WiiRemoteInput::ReadKpadSample(chan, remote.sample);
        if (!remote.connected) {
            remote = {};
            continue;
        }
        NetplayUi::FilterGameInput(chan, remote.sample);
        if (blocked) {
            // The settings overlay owns input: buttons and sticks at rest, the remote still there.
            const WiiRemoteInput::KpadSample held = remote.sample;
            remote.sample = {};
            for (int i = 0; i < 3; ++i) {
                remote.sample.acc[i] = held.acc[i];
                remote.sample.nunchukAcc[i] = held.nunchukAcc[i];
            }
            remote.sample.hasNunchuk = held.hasNunchuk;
            remote.sample.hasClassic = held.hasClassic;
        }
    }
    return frame;
}

void Latch(uint32_t retraceCount) {
    if (g_source != nullptr) {
        g_source(retraceCount, g_frame);
    } else {
        g_frame = SampleLocal();
    }
}

const Remote& Get(uint32_t chan) {
    static const Remote kNone{};
    return chan < g_frame.size() ? g_frame[chan] : kNone;
}

void SetSource(Source source) {
    g_source = source;
}

} // namespace FrameInput
