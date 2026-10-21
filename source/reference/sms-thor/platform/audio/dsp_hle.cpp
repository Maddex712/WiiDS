// High-level emulation of the JAudio DSP task's mail protocol (PROTOCOL.md).
//
// The CPU side is decomp code (JSystem/dsptask.c, dspproc.c, osdsp_task.c,
// JASAudioThread.cpp): commands go out as a word count, DSPAssertInt, then the
// words; the DSP answers through the mailbox plus the DSP interrupt, whose
// handler (__DSPHandler) takes 0xDCD1000x task mails and hands 0xDCD10004
// "request" mails to the audio task's req_cb (syncDSP), which reads a second
// mail 0xF355xxxx: 0xF355FF00 = subframe rendered, anything else = command
// acknowledged (DspFinishWork(low 16 bits)).
//
// This file always builds the port_audio_dsp_* entry points. The SDK-named
// DSP functions (DSPSendMailToDSP, __DSP_boot_task, ...) are only defined
// with SMS_AUDIO_DSP_HLE, because the bring-up fake DSP in
// platform/misc/sdk_data.cpp defines them today; README.md has the switch-over.
#include "port_compat.h"
#include "port_os.h"
#include "port_platform.h"
#include <dolphin/dsp.h>

#include "dsp_mixer.h"

#include <deque>
#include <string>
#include <vector>

extern "C" u8* port_aram_ptr(u32 addr);
extern "C" void __DSPHandler(int interrupt, void* context); // JSystem/osdsp_task.c

namespace {

const u32 kPrefix = 0xF3550000u; // JASystem::DSPInterface::JAS_DSP_PREFIX << 16

struct Hle {
	std::deque<u32> fromDsp; // mailbox DSP -> CPU
	// command assembly
	u32 lastMail;
	bool inCmd;
	bool release; // header word 0: DSPReleaseHalt's two map words
	u32 want;
	std::vector<u32> words;
	// frame state from the 0x82 command
	int16_t* outA;
	int16_t* outB;
	u32 subframes, sub, subSamples;
	u16 master;
	u64 subframesRendered;
	bool traced;
} g;

const uint8_t* aram_lookup(uint32_t a) { return port_aram_ptr(a); }

void post(u32 m) { g.fromDsp.push_back(m); }

// Raise the DSP interrupt: runs the decomp's __DSPHandler in interrupt
// context at the next check point (the sender still has interrupts off).
void raise_irq(u32 first, u32 second)
{
	port_irq_defer([first, second]() {
		post(first);
		post(second);
		__DSPHandler(0, NULL);
	});
}

void reply(u32 low16) { raise_irq(0xDCD10004u, kPrefix | low16); }

// Debug: SMS_AUDIO_ARAM_DUMP=path[,subframe] writes the 16 MiB ARAM image once
// (default after 40 s of audio) for offline mixer runs (tests/, PROTOCOL.md).
void maybe_dump_aram()
{
	static int state; // 0 unchecked, 1 armed, 2 done
	static u64 at;
	static const char* path;
	if (state == 0) {
		path  = getenv("SMS_AUDIO_ARAM_DUMP");
		state = path && *path ? 1 : 2;
		const char* c = path ? strchr(path, ',') : NULL;
		at            = c ? strtoull(c + 1, NULL, 10) : 16000;
	}
	if (state != 1 || g.subframesRendered < at)
		return;
	state = 2;
	std::string fn(path, strchr(path, ',') ? strchr(path, ',') - path : strlen(path));
	if (FILE* f = fopen(fn.c_str(), "wb")) {
		fwrite(port_aram_ptr(0), 1, 16u << 20, f);
		fclose(f);
		port_log("[audio] ARAM image written to %s\n", fn.c_str());
	}
}

void render_subframe()
{
	maybe_dump_aram();
	u32 n = g.subSamples;
	port_dspmix_render(g.outA + g.sub * n, g.outB + g.sub * n, (int)n, g.master);
	g.sub++;
	g.subframesRendered++;
	reply(0xFF00);
}

void execute()
{
	const std::vector<u32>& w = g.words;
	u32 op = w[0] >> 24;
	if (g.release) {
		// DSPReleaseHalt: the 64-bit active-voice map. The microcode renders
		// the next subframe of the current frame and reports 0xFF00.
		if (g.outA && g.sub < g.subframes)
			render_subframe();
		return;
	}
	switch (op) {
	case 0x81: // DsetupTable(n, CH_BUF, DSPRES_FILTER, DSPADPCM_FILTER, FX_BUF)
		if (w.size() >= 5) {
			port_dspmix_set_aram(aram_lookup);
			port_dspmix_setup(w[0] & 0xFFFF, (void*)(uintptr_t)w[1], (const uint32_t*)(uintptr_t)w[2],
			                  (const uint32_t*)(uintptr_t)w[3], (void*)(uintptr_t)w[4]);
			port_log("[audio] DSP setup: %u voices at 0x%08x, FX lines at 0x%08x\n", (unsigned)(w[0] & 0xFFFF),
			         (unsigned)w[1], (unsigned)w[4]);
		}
		reply(w[0] >> 16);
		break;
	case 0x82: // DsyncFrame(subframes, out A, out B) | mixer level
		if (w.size() >= 3) {
			g.subframes = (w[0] >> 16) & 0xFF;
			g.master    = (u16)(w[0] & 0xFFFF);
			g.outA      = (int16_t*)(uintptr_t)w[1];
			g.outB      = (int16_t*)(uintptr_t)w[2];
			// The two outputs are the halves of one frame buffer.
			u32 frame    = (w[2] - w[1]) / 2;
			g.subSamples = g.subframes ? frame / g.subframes : 0;
			g.sub        = 0;
			if (!g.traced) {
				g.traced = true;
				port_log("[audio] first DSP frame: %u subframes x %u samples, mixer level 0x%04x\n",
				         (unsigned)g.subframes, (unsigned)g.subSamples, (unsigned)g.master);
			}
		}
		break;
	default:
		port_log("[audio] DSP: unknown command 0x%08x (%zu words)\n", (unsigned)w[0], w.size());
		reply(w[0] >> 16);
		break;
	}
}

} // namespace

extern "C" void port_audio_dsp_boot(DSPTaskInfo* task)
{
	// Task start: the microcode mails 0xDCD10000 (the handler then runs the
	// task's init_cb, DspHandShake, which waits for one more mail).
	(void)task;
	raise_irq(0xDCD10000u, kPrefix);
	port_log("[audio] DSP HLE: audio task booted (software mixer)\n");
}

extern "C" u32 port_audio_dsp_check_mail_from(void)
{
	return g.fromDsp.empty() ? 0 : 0x80000000u | (g.fromDsp.front() >> 16);
}

extern "C" u32 port_audio_dsp_read_mail_from(void)
{
	if (g.fromDsp.empty())
		return 0;
	u32 m = g.fromDsp.front();
	g.fromDsp.pop_front();
	return m;
}

extern "C" void port_audio_dsp_mail_to(u32 mail)
{
	g.lastMail = mail;
	if (!g.inCmd)
		return;
	g.words.push_back(mail);
	if (g.words.size() == g.want) {
		g.inCmd = false;
		execute();
	}
}

extern "C" void port_audio_dsp_assert_int(void)
{
	// DSPSendCommands2 sends the word count, then asserts; 0 means 2 words.
	g.release = g.lastMail == 0;
	g.want    = g.lastMail ? g.lastMail : 2;
	g.inCmd   = true;
	g.words.clear();
}

extern "C" u64 port_audio_dsp_subframes(void) { return g.subframesRendered; }

// True while a frame started by 0x82 still has subframes to render. The AI
// layer does not start another DMA block until it is done: on hardware the
// DSP always finishes a frame within one block period, and consuming blocks
// faster than JAudio produces frames makes DSPBuf repeat stale samples (gaps)
// while the sequencer falls behind.
extern "C" int port_audio_dsp_frame_pending(void) { return g.outA && g.sub < g.subframes; }

#ifdef SMS_AUDIO_DSP_HLE
// SDK surface (replaces the handshake-only fake in platform/misc/sdk_data.cpp).
DSPTaskInfo* __DSP_curr_task;
DSPTaskInfo* __DSP_first_task;
DSPTaskInfo* __DSP_last_task;
extern "C" void DSPInit(void) {}
extern "C" u32 DSPCheckMailToDSP(void) { return 0; } // always drained at once
extern "C" u32 DSPCheckMailFromDSP(void) { return port_audio_dsp_check_mail_from(); }
extern "C" u32 DSPReadMailFromDSP(void) { return port_audio_dsp_read_mail_from(); }
extern "C" void DSPSendMailToDSP(u32 mail) { port_audio_dsp_mail_to(mail); }
extern "C" void DSPAssertInt(void) { port_audio_dsp_assert_int(); }
extern "C" void __DSP_boot_task(DSPTaskInfo* task)
{
	__DSP_curr_task = task;
	port_audio_dsp_boot(task);
}
// SMS runs only the audio task: no task switching to emulate.
extern "C" void __DSP_insert_task(DSPTaskInfo*) {}
extern "C" void __DSP_exec_task(DSPTaskInfo*, DSPTaskInfo*) {}
extern "C" void __DSP_remove_task(DSPTaskInfo*) {}
#endif
