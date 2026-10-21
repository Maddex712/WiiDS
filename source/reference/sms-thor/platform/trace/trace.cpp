// Native side of native-vs-retail lockstep testing: plays a dolphin-oracle
// .dtm movie into PADRead and writes a per-field memory trace in
// dolphin-oracle's format (big-endian data, so it compares with retail's).
// See README.md for the environment variables and the two hook calls.
#include "trace.h"

#include <dolphin/pad.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <link.h>
#include <unistd.h>

#include <string>
#include <vector>

volatile uint32_t port_trace_anchor = 0x54524143; // "TRAC" (declared extern "C" in trace.h)

extern "C" char __executable_start[];
extern "C" char _end[];

namespace {

const uintptr_t kMem1Lo = 0x80000000u, kMem1Hi = 0x81800000u;

struct Run {
	uint32_t width;
	char kind; // i f p b
	uint32_t count;
};

struct Copy {
	uint32_t gc, nat, width, count;
	char kind;
};

struct Range {
	std::string label, loc, layout_text;
	std::vector<Copy> map; // GameCube offset <- native offset, per member run
	uint32_t native_len;   // bytes read from the native object
	bool host;
	uintptr_t addr;             // host: static address (relocated at init); mem: absolute
	std::vector<uint32_t> hops; // per dereference: read the pointer, add this
	uint32_t len;
	std::vector<Run> layout;
};

struct State {
	bool inited;
	// movie
	std::vector<uint8_t> movie; // 8-byte records
	uint32_t movie_polls;
	std::vector<int64_t> pollmap; // retail field -> polls
	int bias;
	uint32_t cur_poll;
	bool movie_on;
	// trace
	FILE* out;
	std::vector<Range> ranges;
	uint32_t start, every, frames, written;
	// alignment
	bool synced;
	int64_t field_offset;         // aligned field = retrace + field_offset
	struct Sync {
		std::string label; // condition on a traced range (big-endian bytes)
		uint32_t off, width;
		uint64_t value;
		int64_t field;     // retail field at which it first holds
	};
	std::vector<Sync> syncs; // applied in order: the first aligns, later ones re-align
	size_t next_sync;
	intptr_t reloc;               // runtime - static for host symbols
} g;

bool readable(uintptr_t a, uint32_t n)
{
	if (a >= kMem1Lo && a < kMem1Hi && n <= kMem1Hi - a)
		return true;
	uintptr_t lo = (uintptr_t)__executable_start, hi = (uintptr_t)_end;
	return a >= lo && a < hi && n <= hi - a;
}

bool resolve(const Range& r, uintptr_t* where)
{
	uintptr_t a = r.addr;
	for (size_t i = 0; i < r.hops.size(); i++) {
		if (!readable(a, 4))
			return false;
		uint32_t p;
		memcpy(&p, (const void*)a, 4);
		if (!p)
			return false;
		a = (uintptr_t)p + r.hops[i];
	}
	if (!readable(a, r.native_len))
		return false;
	*where = a;
	return true;
}

// Rebuild a range in the GameCube layout (member offsets from MWCC's rules)
// and big-endian byte order. Padding that has no member stays zero.
void snapshot(const Range& r, uintptr_t a, std::vector<uint8_t>& buf)
{
	const uint8_t* src = (const uint8_t*)a;
	buf.assign(r.len, 0);
	for (size_t i = 0; i < r.map.size(); i++) {
		const Copy& c = r.map[i];
		for (uint32_t n = 0; n < c.count; n++) {
			uint32_t g = c.gc + n * c.width, s = c.nat + n * c.width;
			if (g + c.width > r.len)
				break;
			if (c.kind == 'b' || c.width == 1)
				memcpy(&buf[g], src + s, c.width);
			else
				for (uint32_t k = 0; k < c.width; k++)
					buf[g + k] = src[s + c.width - 1 - k];
		}
	}
}

bool parse_map(const char* s, std::vector<Copy>& out, uint32_t* native_len)
{
	*native_len = 0;
	while (*s) {
		char* e;
		Copy c;
		c.gc = (uint32_t)strtoul(s, &e, 16);
		if (*e != ':')
			return false;
		c.nat = (uint32_t)strtoul(e + 1, &e, 16);
		if (*e != ':')
			return false;
		c.width = (uint32_t)strtoul(e + 1, &e, 10);
		c.kind  = *e++;
		c.count = 1;
		if (*e == 'x')
			c.count = (uint32_t)strtoul(e + 1, &e, 10);
		if (c.nat + c.width * c.count > *native_len)
			*native_len = c.nat + c.width * c.count;
		out.push_back(c);
		if (*e == ',')
			e++;
		else if (*e && *e != '\n')
			return false;
		s = e;
		if (*s == '\n')
			break;
	}
	return true;
}

bool parse_layout(const char* s, std::vector<Run>& out)
{
	while (*s) {
		char* e;
		Run r;
		r.width = (uint32_t)strtoul(s, &e, 10);
		if (e == s || !*e)
			return false;
		r.kind  = *e++;
		r.count = 1;
		if (*e == 'x')
			r.count = (uint32_t)strtoul(e + 1, &e, 10);
		out.push_back(r);
		if (*e == ',')
			e++;
		s = e;
	}
	return true;
}

void load_ranges(const char* path)
{
	FILE* f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "[trace] cannot open ranges %s\n", path);
		return;
	}
	char* line  = NULL;
	size_t cap  = 0;
	uintptr_t anchor = 0;
	while (getline(&line, &cap, f) > 0) {
		if (sscanf(line, "# anchor %lx", (unsigned long*)&anchor) == 1)
			continue;
		if (line[0] == '#' || line[0] == '\n')
			continue;
		char loc[256], label[128];
		unsigned len;
		if (sscanf(line, "%255s %x %127s", loc, &len, label) != 3)
			continue;
		const char* lp = strstr(line, " layout=");
		const char* mp = strstr(line, " map=");
		if (!lp || !mp)
			continue;
		std::string lay(lp + 8, strcspn(lp + 8, " \n"));
		Range r;
		r.label       = label;
		r.loc         = loc;
		r.layout_text = lay;
		r.len         = len;
		const char* p = loc;
		r.host        = *p == '@';
		if (r.host)
			p++;
		char* e;
		r.addr = (uintptr_t)strtoul(p, &e, 16);
		while (*e == '/')
			r.hops.push_back((uint32_t)strtoul(e + 1, &e, 16));
		if (!parse_layout(lay.c_str(), r.layout) || !parse_map(mp + 5, r.map, &r.native_len))
			continue;
		g.ranges.push_back(r);
	}
	free(line);
	fclose(f);
	// Load bias of the executable (0 for non-PIE): static address = runtime - bias.
	struct Bias {
		static int cb(struct dl_phdr_info* info, size_t, void* data)
		{
			*(uintptr_t*)data = (uintptr_t)info->dlpi_addr; // first entry: the executable
			return 1;
		}
	};
	uintptr_t bias = 0;
	dl_iterate_phdr(Bias::cb, &bias);
	uintptr_t mine = (uintptr_t)&port_trace_anchor - bias;
	if (anchor != mine) {
		fprintf(stderr,
		        "[trace] %s was resolved for another build (anchor 0x%lx, this binary 0x%lx): "
		        "rerun tools/trace_resolve.py; tracing disabled\n",
		        path, (unsigned long)anchor, (unsigned long)mine);
		g.ranges.clear();
		return;
	}
	g.reloc = (intptr_t)bias;
	for (size_t i = 0; i < g.ranges.size(); i++)
		if (g.ranges[i].host)
			g.ranges[i].addr += g.reloc;
}

void load_pollmap(const char* path)
{
	FILE* f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "[trace] cannot open poll map %s\n", path);
		return;
	}
	char line[512];
	while (fgets(line, sizeof line, f)) {
		if (line[0] != 'F')
			continue;
		unsigned long field;
		const char* p = strstr(line, "polls=");
		if (sscanf(line, "F %lu", &field) != 1 || !p)
			continue;
		if (g.pollmap.size() <= field)
			g.pollmap.resize(field + 1, -1);
		g.pollmap[field] = strtoll(p + 6, NULL, 10);
	}
	fclose(f);
}

void init()
{
	if (g.inited)
		return;
	g.inited = true;
	g.every  = 1;
	if (const char* m = getenv("SMS_MOVIE")) {
		FILE* f = fopen(m, "rb");
		if (f) {
			uint8_t hdr[256];
			if (fread(hdr, 1, 256, f) == 256 && !memcmp(hdr, "DTM\x1a", 4)) {
				uint8_t rec[8];
				while (fread(rec, 1, 8, f) == 8)
					g.movie.insert(g.movie.end(), rec, rec + 8);
				g.movie_polls = (uint32_t)(g.movie.size() / 8);
				g.movie_on    = true;
				fprintf(stderr, "[trace] movie %s: %.6s, %u polls\n", m, (const char*)hdr + 4, g.movie_polls);
			}
			fclose(f);
		}
		if (!g.movie_on)
			fprintf(stderr, "[trace] cannot use movie %s\n", m);
	}
	if (const char* p = getenv("SMS_MOVIE_POLLMAP"))
		load_pollmap(p);
	g.bias = getenv("SMS_MOVIE_POLL_BIAS") ? atoi(getenv("SMS_MOVIE_POLL_BIAS")) : 0;
	if (const char* r = getenv("SMS_TRACE_RANGES"))
		load_ranges(r);
	// Alignment of native retraces with retail fields.
	const char* sync = getenv("SMS_TRACE_SYNC");
	if (!sync)
		sync = "app+8:1=2@231"; // TApplication::mAppState becomes 2 (boot) at retail field 231
	if (sync[0] == '+' || sync[0] == '-' || (sync[0] >= '0' && sync[0] <= '9')) {
		g.field_offset = strtoll(sync, NULL, 0);
		g.synced       = true;
	} else {
		std::string list(sync);
		size_t pos = 0;
		while (pos <= list.size()) {
			size_t comma = list.find(',', pos);
			std::string one = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
			char label[128];
			unsigned off = 0, width = 4;
			unsigned long long value = 0;
			long long field          = 0;
			if (sscanf(one.c_str(), "%127[^+]+%x:%u=%lli@%lli", label, &off, &width, &value, &field) == 5) {
				State::Sync y = { label, off, width, value, field };
				g.syncs.push_back(y);
			} else if (!one.empty())
				fprintf(stderr, "[trace] bad SMS_TRACE_SYNC item %s\n", one.c_str());
			if (comma == std::string::npos)
				break;
			pos = comma + 1;
		}
		if (g.syncs.empty())
			g.synced = true;
	}
	g.start  = getenv("SMS_TRACE_START") ? (uint32_t)atoi(getenv("SMS_TRACE_START")) : 0;
	g.every  = getenv("SMS_TRACE_EVERY") ? (uint32_t)atoi(getenv("SMS_TRACE_EVERY")) : 1;
	g.frames = getenv("SMS_TRACE_FRAMES") ? (uint32_t)atoi(getenv("SMS_TRACE_FRAMES")) : 0;
	if (!g.every)
		g.every = 1;
	if (const char* o = getenv("SMS_TRACE_OUT")) {
		g.out = fopen(o, "w");
		if (!g.out)
			fprintf(stderr, "[trace] cannot write %s\n", o);
	}
	if (g.out) {
		fprintf(g.out, "# dolphin-oracle memtrace v1\n");
		fprintf(g.out, "# game=GMSE01 mem1_size=0x1800000 page_size=0x0 every=%u start=%u\n", g.every, g.start);
		fprintf(g.out, "# native=sms-port sync=%s movie=%s\n", sync, getenv("SMS_MOVIE") ? getenv("SMS_MOVIE") : "-");
		for (size_t i = 0; i < g.ranges.size(); i++)
			fprintf(g.out, "# range %s %s 0x%x blocks=0x0 layout=%s\n", g.ranges[i].label.c_str(),
			        g.ranges[i].loc.c_str(), g.ranges[i].len, g.ranges[i].layout_text.c_str());
	}
	fprintf(stderr, "[trace] %zu ranges, sync %s%s\n", g.ranges.size(), sync, g.out ? ", tracing" : "");
}

bool sync_hit(const State::Sync& y)
{
	for (size_t i = 0; i < g.ranges.size(); i++) {
		const Range& r = g.ranges[i];
		if (r.label != y.label)
			continue;
		uintptr_t a;
		if (!resolve(r, &a) || y.off + y.width > r.len)
			return false;
		std::vector<uint8_t> b;
		snapshot(r, a, b);
		uint64_t v = 0;
		for (uint32_t k = 0; k < y.width; k++)
			v = (v << 8) | b[y.off + k];
		return v == y.value;
	}
	return false;
}

uint32_t poll_for_field(int64_t field)
{
	int64_t p = -1;
	if (field >= 0 && (size_t)field < g.pollmap.size())
		p = g.pollmap[field];
	if (p < 0 && !g.pollmap.empty()) {
		// past the end of the map: continue at two polls per field
		int64_t last = (int64_t)g.pollmap.size() - 1;
		while (last >= 0 && g.pollmap[last] < 0)
			last--;
		if (last >= 0)
			p = g.pollmap[last] + 2 * (field - last);
	}
	if (p < 0)
		p = 2 * field - 227; // dolphin-oracle: poll ~= 2*field - 227 after boot
	p += g.bias;
	return p < 0 ? 0 : (uint32_t)p;
}

void write_field(int64_t field, uint32_t retrace)
{
	fprintf(g.out, "F %lld ticks=0 pc=0 lr=0 polls=%u lag=0 retrace=%u\n", (long long)field, g.cur_poll, retrace);
	std::vector<uint8_t> buf;
	std::string hex;
	for (size_t i = 0; i < g.ranges.size(); i++) {
		const Range& r = g.ranges[i];
		uintptr_t a;
		if (!resolve(r, &a)) {
			fprintf(g.out, "D %s - -\n", r.label.c_str());
			continue;
		}
		snapshot(r, a, buf);
		hex.resize(buf.size() * 2);
		static const char digits[] = "0123456789abcdef";
		for (size_t k = 0; k < buf.size(); k++) {
			hex[2 * k]     = digits[buf[k] >> 4];
			hex[2 * k + 1] = digits[buf[k] & 15];
		}
		fprintf(g.out, "D %s %08lx %s\n", r.label.c_str(), (unsigned long)a, hex.c_str());
	}
}

} // namespace

extern "C" int port_trace_movie_active(void)
{
	init();
	return g.movie_on;
}

extern "C" void port_trace_on_retrace(uint32_t retrace)
{
	init();
	if (g.next_sync < g.syncs.size() && sync_hit(g.syncs[g.next_sync])) {
		const State::Sync& y = g.syncs[g.next_sync++];
		int64_t before       = (int64_t)retrace + g.field_offset;
		g.field_offset       = y.field - (int64_t)retrace;
		if (g.synced)
			fprintf(stderr, "[trace] re-synced on %s+0x%x: native retrace %u = retail field %lld (was %lld, %+lld)\n",
			        y.label.c_str(), y.off, retrace, (long long)y.field, (long long)before,
			        (long long)(y.field - before));
		else
			fprintf(stderr, "[trace] synced on %s+0x%x: native retrace %u = retail field %lld\n", y.label.c_str(),
			        y.off, retrace, (long long)y.field);
		g.synced = true;
		if (g.out)
			fprintf(g.out, "# sync %s+0x%x retrace=%u field=%lld\n", y.label.c_str(), y.off, retrace,
			        (long long)y.field);
	}
	if (!g.synced)
		return;
	int64_t field = (int64_t)retrace + g.field_offset;
	// The game reads the pad late in the field, after both of the field's SI
	// polls: it sees the last poll before the next boundary.
	uint32_t next = poll_for_field(field + 1);
	g.cur_poll    = next ? next - 1 : 0;
	if (g.out && field >= (int64_t)g.start && (field - g.start) % g.every == 0) {
		write_field(field, retrace);
		g.written++;
	}
	if (g.frames && field + 1 >= (int64_t)g.frames) {
		if (g.out) {
			fprintf(g.out, "E fields=%u\n", g.written);
			fclose(g.out);
			g.out = NULL;
		}
		fprintf(stderr, "[trace] reached field %lld (SMS_TRACE_FRAMES): exiting\n", (long long)field);
		fflush(stderr);
		_exit(0);
	}
	if (g.out && (g.written & 63) == 0)
		fflush(g.out);
}

extern "C" int port_trace_pad_read(PADStatus* status)
{
	init();
	if (!g.movie_on)
		return 0;
	memset(status, 0, 4 * sizeof(PADStatus));
	for (int i = 1; i < 4; i++)
		status[i].err = PAD_ERR_NO_CONTROLLER;
	// Neutral input until the native run is aligned with the movie.
	uint8_t rec[8] = { 0, 0x40, 0, 0, 128, 128, 128, 128 };
	if (g.synced && g.movie_polls)
		memcpy(rec, &g.movie[8 * (g.cur_poll < g.movie_polls ? g.cur_poll : g.movie_polls - 1)], 8);
	// .dtm record (Dolphin ControllerState): u16 LE bits, trigger L/R, stick X/Y, c-stick X/Y
	uint16_t bits = (uint16_t)(rec[0] | rec[1] << 8);
	static const uint16_t map[12] = { PAD_BUTTON_START, PAD_BUTTON_A,    PAD_BUTTON_B,     PAD_BUTTON_X,
		                              PAD_BUTTON_Y,     PAD_TRIGGER_Z,   PAD_BUTTON_UP,    PAD_BUTTON_DOWN,
		                              PAD_BUTTON_LEFT,  PAD_BUTTON_RIGHT, PAD_TRIGGER_L,   PAD_TRIGGER_R };
	uint16_t b = 0;
	for (int i = 0; i < 12; i++)
		if (bits & (1u << i))
			b |= map[i];
	PADStatus& s   = status[0];
	s.button       = b;
	s.triggerLeft  = rec[2];
	s.triggerRight = rec[3];
	s.stickX       = (s8)(rec[4] - 128);
	s.stickY       = (s8)(rec[5] - 128);
	s.substickX    = (s8)(rec[6] - 128);
	s.substickY    = (s8)(rec[7] - 128);
	s.err          = (bits & (1u << 14)) ? PAD_ERR_NONE : PAD_ERR_NO_CONTROLLER;
	return 1;
}
