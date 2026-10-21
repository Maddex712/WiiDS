// Storage for SDK globals the game references directly. Weak where another
// platform module (GX/GD, audio) is expected to own the real definition.
#include "port_compat.h"
#include <dolphin/gx.h>
#include <dolphin/gd.h>
#include <dolphin/dsp.h>
#include "port_os.h"

// GD display-list builder state (J3D/GD writers use it inline).
__attribute__((weak)) GDLObj* __GDCurrentDL;
extern "C" __attribute__((weak)) void GDOverflowed(void)
{
	fprintf(stderr, "[gd] display list overflow\n");
}

// Standard NTSC 640x480 interlaced render mode (values from the public SDK
// documentation of GXNtsc480Int).
#ifdef _WIN32
GXRenderModeObj GXNtsc480Int = {
#else
__attribute__((weak)) GXRenderModeObj GXNtsc480Int = {
#endif
	VI_TVMODE_NTSC_INT, 640, 480, 480, 40, 0, 640, 480, VI_XFBMODE_DF, GX_FALSE, GX_FALSE,
	{ { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 } },
	{ 0, 0, 21, 22, 21, 0, 0 },
};

#ifndef SMS_AUDIO_DSP_HLE // platform/audio/dsp_hle.cpp provides the real DSP HLE
// DSP task list (the task manager in JSystem/osdsp_task.c drives it). There is
// no DSP on the host; the audio layer will replace this.
__attribute__((weak)) DSPTaskInfo* __DSP_curr_task;
__attribute__((weak)) DSPTaskInfo* __DSP_first_task;
__attribute__((weak)) DSPTaskInfo* __DSP_last_task;
// Minimal fake DSP (weak: platform/audio's DSP HLE replaces it when linked): the boot handshake mail is delivered and the task's init
// callback runs; mail to the DSP is accepted and dropped. No audio is mixed.
static u32 s_mail_from_dsp[16];
static int s_mail_head, s_mail_count;
static void dsp_post_mail(u32 m)
{
	if (s_mail_count < 16) {
		s_mail_from_dsp[(s_mail_head + s_mail_count) % 16] = m;
		s_mail_count++;
	}
}
extern "C" __attribute__((weak)) u32 DSPCheckMailFromDSP(void) { return s_mail_count ? 0x80000000u | (s_mail_from_dsp[s_mail_head] >> 16) : 0; }
extern "C" __attribute__((weak)) u32 DSPReadMailFromDSP(void)
{
	if (!s_mail_count)
		return 0;
	u32 m      = s_mail_from_dsp[s_mail_head];
	s_mail_head = (s_mail_head + 1) % 16;
	s_mail_count--;
	return m;
}
extern "C" __attribute__((weak)) u32 DSPCheckMailToDSP(void) { return 0; }
// Command framing used by JSystem/dsptask.c DSPSendCommands2: a word count,
// DSPAssertInt, then the words. The real microcode acknowledges each command
// by mailing back, which ends in DspFinishWork(first word >> 16); the fake
// acknowledges as soon as the last word arrives.
void DspFinishWork(u16 id);
static u32 s_last_mail, s_cmd_words, s_cmd_seen, s_cmd_first;
static bool s_in_cmd;
extern "C" __attribute__((weak)) void DSPSendMailToDSP(u32 mail)
{
	s_last_mail = mail;
	if (!s_in_cmd)
		return;
	if (s_cmd_seen++ == 0)
		s_cmd_first = mail;
	if (s_cmd_seen == s_cmd_words) {
		s_in_cmd = false;
		DspFinishWork((u16)(s_cmd_first >> 16));
	}
}
extern "C" __attribute__((weak)) void DSPAssertInt(void)
{
	s_cmd_words = s_last_mail ? s_last_mail : 2;
	s_cmd_seen  = 0;
	s_in_cmd    = true;
}
extern "C" __attribute__((weak)) void DSPInit(void) {}
extern "C" __attribute__((weak)) void __DSP_boot_task(DSPTaskInfo* task)
{
	fprintf(stderr, "[dsp] boot task %p (fake DSP: handshake only, no mixing)\n", task);
	__DSP_curr_task = task;
	dsp_post_mail(0xDCD10000u);
	// The boot interrupt would run init_cb asynchronously; run it now, since
	// the caller spins on its effect without any OS call.
	if (task->init_cb)
		task->init_cb(task);
}
extern "C" __attribute__((weak)) void __DSP_insert_task(DSPTaskInfo* task) {}
extern "C" __attribute__((weak)) void __DSP_exec_task(DSPTaskInfo* curr, DSPTaskInfo* next) {}
extern "C" __attribute__((weak)) void __DSP_remove_task(DSPTaskInfo* task) {}
#endif
