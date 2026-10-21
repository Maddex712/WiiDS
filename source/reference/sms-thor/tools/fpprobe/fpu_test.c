/* Checks src/port_fpu.h against the dolphin-oracle measurement of frsqrte /
 * fres (tools/fpprobe: probe.dol run by scripts/run_oracle.py).
 *   python3 build_dol.py /tmp/x (writes inputs.bin); cc -O2 -o fpu_test fpu_test.c -lm
 *   ./fpu_test $DOLPHIN_ORACLE/runs/fpprobe-interp/trace.txt /tmp/x/inputs.bin */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/port_fpu.h"

static unsigned char* hexdup(const char* h, size_t* n)
{
	size_t len = strcspn(h, " \n");
	unsigned char* b = malloc(len / 2);
	for (size_t i = 0; i < len / 2; i++)
		sscanf(h + 2 * i, "%2hhx", &b[i]);
	*n = len / 2;
	return b;
}

static unsigned long long be64(const unsigned char* p)
{
	unsigned long long v = 0;
	for (int i = 0; i < 8; i++)
		v = v << 8 | p[i];
	return v;
}

int main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: fpu_test TRACE INPUTS\n");
		return 2;
	}
	FILE* f = fopen(argv[1], "r");
	if (!f)
		return 2;
	static char line[4 << 20];
	unsigned char *rsq = 0, *res = 0, *rrsq = 0, *rres = 0;
	size_t n;
	while (fgets(line, sizeof line, f)) {
		char label[16];
		if (sscanf(line, "D %15s", label) != 1)
			continue;
		char* data = strchr(strchr(line + 2, ' ') + 1, ' ') + 1;
		if (!strcmp(label, "rsq"))
			rsq = hexdup(data, &n);
		else if (!strcmp(label, "res"))
			res = hexdup(data, &n);
		else if (!strcmp(label, "rrsq"))
			rrsq = hexdup(data, &n);
		else if (!strcmp(label, "rres"))
			rres = hexdup(data, &n);
	}
	fclose(f);
	FILE* in = fopen(argv[2], "rb");
	unsigned char inputs[4096 * 8];
	if (!in || fread(inputs, 8, 4096, in) != 4096 || !rsq || !res || !rrsq || !rres) {
		fprintf(stderr, "missing data\n");
		return 2;
	}
	fclose(in);
	int bad = 0, checked = 0;
	for (unsigned k = 0; k < 65536; k++) {
		unsigned long long hi = ((unsigned long long)(1023 + (k >> 15)) << 52) | ((unsigned long long)(k & 0x7FFF) << 37);
		unsigned long long got = port_f64_bits(port_gekko_frsqrte(port_f64_from_bits(hi))), want = be64(rsq + 8 * k);
		checked++;
		if (got != want && bad++ < 5)
			printf("frsqrte grid %u: %016llx want %016llx\n", k, got, want);
	}
	for (unsigned k = 0; k < 32768; k++) {
		unsigned long long x = (1023ULL << 52) | ((unsigned long long)k << 37);
		unsigned long long got = port_f64_bits((double)port_gekko_fres(port_f64_from_bits(x))), want = be64(res + 8 * k);
		checked++;
		if (got != want && bad++ < 10)
			printf("fres grid %u: %016llx want %016llx\n", k, got, want);
	}
	for (unsigned k = 0; k < 4096; k++) {
		double x = port_f64_from_bits(be64(inputs + 8 * k));
		unsigned long long g1 = port_f64_bits(port_gekko_frsqrte(x)), w1 = be64(rrsq + 8 * k);
		unsigned long long g2 = port_f64_bits((double)port_gekko_fres(x)), w2 = be64(rres + 8 * k);
		checked += 2;
		/* NaN payloads are not compared */
		if (g1 != w1 && !(isnan(port_f64_from_bits(g1)) && isnan(port_f64_from_bits(w1))) && bad++ < 20)
			printf("frsqrte(%.17g): %016llx want %016llx\n", x, g1, w1);
		if (g2 != w2 && !(isnan(port_f64_from_bits(g2)) && isnan(port_f64_from_bits(w2))) && bad++ < 20)
			printf("fres(%.17g): %016llx want %016llx\n", x, g2, w2);
	}
	/* The oracle value that started this: TMario::mEntryRadius in play-r9 is
	 * 0x4357e16c (215.88055). THitActor::calcEntryRadius with attack radius
	 * 80 and damage height 130: sqrt(rad2) as rad2 * frsqrte(rad2), no refinement. */
	{
		float rad = 80.0f, h = 130.0f, h2 = h * h, rad2 = rad * rad + h2;
		volatile float diag = (float)((double)rad2 * port_gekko_frsqrte(rad2));
		float entry = 1.4142135f * diag;
		unsigned u;
		memcpy(&u, &entry, 4);
		checked++;
		if (u != 0x4357e16c && bad++ < 30)
			printf("mEntryRadius: %08x want 4357e16c\n", u);
	}
	printf("%d values checked, %d mismatches\n", checked, bad);
	return bad != 0;
}
