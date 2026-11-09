#pragma once
#include <cstdint>

void dsp_hle_reset();
void dsp_hle_mail(uint32_t mail);
void dsp_hle_cpu_interrupt();
void dsp_send_mail(uint32_t mail, bool interrupt);
void dsp_raise(uint16_t bit);
void dsp_mail_clear();
