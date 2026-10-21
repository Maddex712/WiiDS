// Save states (src/port_include/port_savestate.h): saving, loading exactly,
// the folder, and the requests between the controls, the menu and the game.
//
// The exact part of a state is the game's memory: MEM1, ARAM and the game's
// own global variables. Which addresses those globals have is known only
// after linking, so tools/savestate_ranges.py reads the link map and writes
// them, with a fingerprint of the build, into kRanges below (it finds the
// placeholder in the executable by its magic). A build without that table
// saves and loads the portable part only.
//
// A state is saved and loaded between two frames (patch zzz-savestate-01), on
// the game's thread while it owns the CPU, with no disc read or interrupt
// work outstanding. The other game threads (audio, loaders, the card) are
// then waiting in their loops; their thread records live in game memory but
// belong to the scheduler (platform/os), so a load requires the same threads
// waiting the same way as when the state was saved, and puts their records
// back after copying the memory.
#include "port_compat.h"
#include "port_host.h"
#include "port_os.h"
#include "port_platform.h"
#include "port_savestate.h"
#include "state_file.h"
#include <dolphin/dvd.h>
#include <dolphin/os.h>
#include <dolphin/os/OSThread.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#define STBI_ONLY_PNG
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "../gx/src/third_party/stb_image.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" u8* port_ar_memory(u32* size, u32** alloc);
extern "C" int port_dvd_busy(void);
extern "C" int port_os_thread_list(OSThread** out, int max);
extern "C" int port_os_irq_work_pending(void);
extern "C" const char* port_card_dir(void);
// platform/gx (absent in a build without it)
extern "C" __attribute__((weak)) void GXPC_MemoryWritten(const void* p, uint32_t n);
extern "C" __attribute__((weak)) uint8_t* GXPC_SaveStateThumbnail(size_t* size);
extern "C" __attribute__((weak)) void GXPC_SetSaveStateApi(const PortSaveStateOverlayApi* api);
extern "C" __attribute__((weak)) int GXPC_InputStyle(void);
extern "C" __attribute__((weak)) const void* port_button_glyph(int chr);

// Filled in after linking (tools/savestate_ranges.py; keep the layout in
// step): the build's fingerprint and the game's writable data as [start, end)
// address ranges. count 0: this build cannot save or load exactly.
struct PortSaveStateRanges {
	char magic[16];
	uint8_t fingerprint[32];
	uint32_t count, reserved;
	uint64_t ranges[2048][2];
};
__attribute__((used)) static const volatile PortSaveStateRanges kRanges = { "SMSSTATE-RANGES", {}, 0, 0, {} };

namespace {

using savestate::Chunk;

// What the build and the moment were (the INFO chunk).
struct Info {
	uint8_t fingerprint[32];
	uint64_t codeAnchor; // where this code is: a build that moves (macOS) cannot load exactly
	uint32_t mem1Size, aramSize, globalsSize, rangeCount;
	int64_t created; // seconds since 1970
	char game[8];    // the disc's game code and maker (GMSE01)
	char flavor[16]; // "sunshine" or "eclipse"
	uint32_t portableSize;
	uint32_t aramAlloc;
	uint32_t reserved[6];
};

// A game thread as the scheduler sees it (the THRD chunk).
struct ThreadSig {
	uint64_t thread, queue, mutex, heldMutex;
	uint32_t state, suspend, priority, pad;
	bool operator<(const ThreadSig& o) const { return thread < o.thread; }
	bool operator==(const ThreadSig& o) const { return !memcmp(this, &o, sizeof *this); }
};

#ifdef SMS_ECLIPSE
const char kFlavor[] = "eclipse";
#else
const char kFlavor[] = "sunshine";
#endif

std::mutex g_lock;
int g_pending = PORT_SAVESTATE_NONE;
std::string g_pendingPath;
std::atomic<int> g_ready(0);
std::atomic<bool> g_writing(false);
std::string g_message;
double g_messageUntil;

double now_seconds()
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

uint32_t range_count()
{
	const uint32_t n = kRanges.count;
	return n <= sizeof kRanges.ranges / sizeof kRanges.ranges[0] ? n : 0;
}

size_t globals_size()
{
	size_t n = 0;
	for (uint32_t i = 0; i < range_count(); i++)
		n += (size_t)(kRanges.ranges[i][1] - kRanges.ranges[i][0]);
	return n;
}

bool has_fingerprint()
{
	for (int i = 0; i < 32; i++)
		if (kRanges.fingerprint[i])
			return true;
	return false;
}

void current_game(char out[8])
{
	memset(out, 0, 8);
	memcpy(out, "GMSE01", 6);
	if (const DVDDiskID* id = DVDGetCurrentDiskID())
		if (id->gameName[0]) {
			memcpy(out, id->gameName, 4);
			memcpy(out + 4, id->company, 2);
		}
}

Info current_info()
{
	Info info;
	memset(&info, 0, sizeof info);
	for (int i = 0; i < 32; i++)
		info.fingerprint[i] = kRanges.fingerprint[i];
	info.codeAnchor = (uint64_t)(uintptr_t)&port_savestate_save;
	info.mem1Size = port_mem1_size;
	u32 aramSize = 0;
	u32* alloc   = nullptr;
	if (port_ar_memory(&aramSize, &alloc) && alloc) {
		info.aramSize  = aramSize;
		info.aramAlloc = *alloc;
	}
	info.globalsSize = (uint32_t)globals_size();
	info.rangeCount = range_count();
	info.created = (int64_t)time(NULL);
	current_game(info.game);
	snprintf(info.flavor, sizeof info.flavor, "%s", kFlavor);
	info.portableSize = sizeof(PortSaveStatePortable);
	return info;
}

std::vector<ThreadSig> thread_sigs()
{
	OSThread* list[64];
	int n = port_os_thread_list(list, 64);
	if (n > 64)
		n = 64;
	std::vector<ThreadSig> out;
	for (int i = 0; i < n; i++) {
		const OSThread* t = list[i];
		ThreadSig s;
		memset(&s, 0, sizeof s);
		s.thread    = (uint64_t)(uintptr_t)t;
		s.queue     = (uint64_t)(uintptr_t)t->queue;
		s.mutex     = (uint64_t)(uintptr_t)t->mutex;
		s.heldMutex = (uint64_t)(uintptr_t)t->queueMutex.head;
		s.state     = t->state;
		s.suspend   = (uint32_t)t->suspend;
		s.priority  = (uint32_t)t->priority;
		out.push_back(s);
	}
	std::sort(out.begin(), out.end());
	return out;
}

std::string state_dir()
{
	static std::string dir;
	if (!dir.empty())
		return dir;
	if (const char* d = getenv("SMS_SAVE_STATE_DIR")) {
		dir = d;
	} else {
		std::string card = port_card_dir();
		while (!card.empty() && (card.back() == '/' || card.back() == '\\'))
			card.pop_back();
		const size_t slash = card.find_last_of("/\\");
		dir = (slash == std::string::npos ? std::string(".") : card.substr(0, slash)) + "/savestates";
	}
	for (size_t i = 1; i <= dir.size(); i++)
		if (i == dir.size() || dir[i] == '/' || dir[i] == '\\')
			port_mkdir(dir.substr(0, i).c_str(), 0755);
	port_log("[savestate] folder: %s\n", dir.c_str());
	return dir;
}

bool exists(const std::string& path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0;
}

// "Today 9:13 AM", "Yesterday 10:40 PM", "Oct 12 7:30 PM"
void when_text(int64_t created, char* out, size_t n)
{
	const time_t t = (time_t)created, nowT = time(NULL);
	struct tm at, today;
#ifdef _WIN32
	localtime_s(&at, &t);
	localtime_s(&today, &nowT);
#else
	localtime_r(&t, &at);
	localtime_r(&nowT, &today);
#endif
	char clock[16];
	strftime(clock, sizeof clock, "%I:%M %p", &at);
	const char* time12 = clock[0] == '0' ? clock + 1 : clock;
	struct tm midnight = today;
	midnight.tm_hour = midnight.tm_min = midnight.tm_sec = 0;
	const time_t startOfToday = mktime(&midnight);
	if (t >= startOfToday)
		snprintf(out, n, "Today %s", time12);
	else if (t >= startOfToday - 24 * 3600)
		snprintf(out, n, "Yesterday %s", time12);
	else {
		char day[16];
		strftime(day, sizeof day, "%b %d", &at);
		snprintf(out, n, "%s %s", day, time12);
	}
}

bool ends_with(const std::string& s, const char* tail)
{
	const size_t n = strlen(tail);
	return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

// A file name from the time and the stage: "2026-10-13 09-41-05 Bianco Hills".
std::string file_name(const PortSaveStatePortable& p, time_t when)
{
	struct tm lt;
#ifdef _WIN32
	localtime_s(&lt, &when);
#else
	localtime_r(&when, &lt);
#endif
	char stamp[32];
	strftime(stamp, sizeof stamp, "%Y-%m-%d %H-%M-%S", &lt);
	std::string name = stamp;
	std::string stage;
	for (const char* c = p.stageName; *c && stage.size() < 40; c++)
		stage += (isalnum((unsigned char)*c) || *c == ' ' || *c == '-' || *c == '\'') ? *c : ' ';
	while (!stage.empty() && stage.back() == ' ')
		stage.pop_back();
	if (!stage.empty())
		name += " " + stage;
	return name;
}

void json_string(std::string& out, const char* s)
{
	out += '"';
	for (; *s; s++) {
		const unsigned char c = (unsigned char)*s;
		if (c == '"' || c == '\\')
			out += '\\', out += (char)c;
		else if (c < 0x20 || c >= 0x7F)
			out += ' ';
		else
			out += (char)c;
	}
	out += '"';
}

// What the launcher shows (the JSON chunk, first in the file).
std::string metadata(const Info& info, const PortSaveStatePortable& p, bool exact)
{
	char created[32];
	time_t t = (time_t)info.created;
	struct tm ut;
#ifdef _WIN32
	gmtime_s(&ut, &t);
#else
	gmtime_r(&t, &ut);
#endif
	strftime(created, sizeof created, "%Y-%m-%dT%H:%M:%SZ", &ut);
	char hex[65] = "";
	for (int i = 0; exact && i < 32; i++)
		snprintf(hex + i * 2, 3, "%02x", info.fingerprint[i]);
	char game[8];
	memcpy(game, info.game, 6);
	game[6] = 0;
	std::string out = "{\"format\":1,\"created\":";
	json_string(out, created);
	out += ",\"game\":";
	json_string(out, game);
	out += ",\"flavor\":";
	json_string(out, info.flavor);
	out += ",\"stage\":";
	json_string(out, p.stageName);
	out += ",\"episode\":";
	json_string(out, p.episodeName);
	char numbers[256];
	snprintf(numbers, sizeof numbers,
	         ",\"area\":%d,\"episodeIndex\":%d,\"shines\":%d,\"blueCoins\":%d,\"coins\":%d,\"lives\":%d,\"build\":",
	         (int)p.area, (int)p.episode, (int)p.gameInts[0], (int)p.gameInts[1], (int)p.gameInts[2], (int)p.cardInts[1]);
	out += numbers;
	json_string(out, hex);
	out += ",\"exact\":";
	out += exact ? "true" : "false";
	out += "}";
	return out;
}

// The last frame shown, small, as a PNG (platform/gx).
std::vector<uint8_t> thumbnail()
{
	std::vector<uint8_t> png;
	size_t size = 0;
	if (uint8_t* data = GXPC_SaveStateThumbnail ? GXPC_SaveStateThumbnail(&size) : nullptr) {
		png.assign(data, data + size);
		free(data);
	}
	return png;
}

void set_message(const char* text)
{
	std::lock_guard<std::mutex> l(g_lock);
	g_message = text;
	g_messageUntil = now_seconds() + 2.5;
	port_log("[savestate] %s\n", text);
}

struct Pending {
	std::string path;
	std::vector<Chunk> small;
	std::vector<uint8_t> mem1, aram, globals;
};

// Compresses and writes on a thread of its own, so saving costs the game only
// the copies.
void write_in_background(Pending* job)
{
	std::thread([job]() {
		std::vector<Chunk> chunks;
		chunks.swap(job->small);
		if (!job->mem1.empty()) {
			chunks.push_back(savestate::make_chunk("MEM1", job->mem1.data(), job->mem1.size(), true));
			std::vector<uint8_t>().swap(job->mem1);
			chunks.push_back(savestate::make_chunk("ARAM", job->aram.data(), job->aram.size(), true));
			std::vector<uint8_t>().swap(job->aram);
			chunks.push_back(savestate::make_chunk("GLOB", job->globals.data(), job->globals.size(), true));
			std::vector<uint8_t>().swap(job->globals);
		}
		const bool ok = savestate::write_file(job->path, chunks);
		set_message(ok ? "State saved" : "Could not save the state");
		if (!ok)
			port_log("[savestate] writing %s failed\n", job->path.c_str());
		delete job;
		g_writing = false;
	}).detach();
}

bool read_portable(const std::vector<Chunk>& chunks, PortSaveStatePortable* out)
{
	const Chunk* c = savestate::find_chunk(chunks, "PORT");
	return c && savestate::chunk_data(*c, (uint8_t*)out, sizeof *out);
}

bool read_info(const std::vector<Chunk>& chunks, Info* out)
{
	const Chunk* c = savestate::find_chunk(chunks, "INFO");
	return c && savestate::chunk_data(*c, (uint8_t*)out, sizeof *out);
}

bool same_game(const Info& a, const Info& b)
{
	return !memcmp(a.game, b.game, sizeof a.game) && !strncmp(a.flavor, b.flavor, sizeof a.flavor);
}

// The same game threads, waiting the same way, as when the state was saved.
bool threads_match(const std::vector<Chunk>& chunks)
{
	const std::vector<ThreadSig> threads = thread_sigs();
	const Chunk* thrd = savestate::find_chunk(chunks, "THRD");
	std::vector<ThreadSig> then(thrd ? thrd->rawSize / sizeof(ThreadSig) : 0);
	return thrd && thrd->rawSize % sizeof(ThreadSig) == 0
	       && savestate::chunk_data(*thrd, (uint8_t*)then.data(), then.size() * sizeof(ThreadSig)) && then == threads;
}

// Whether this build can take a state saved as `saved` back exactly.
bool exact_compatible(const Info& saved, const Info& now, const char** why)
{
	*why = nullptr;
	if (!now.rangeCount || !has_fingerprint())
		*why = "this build cannot load states exactly";
	else if (!saved.rangeCount)
		*why = "the state has no exact copy";
	else if (memcmp(saved.fingerprint, now.fingerprint, sizeof now.fingerprint))
		*why = "it was saved by another build of the game";
	else if (saved.codeAnchor != now.codeAnchor)
		*why = "the game is loaded at another address this run";
	else if (saved.mem1Size != now.mem1Size || saved.aramSize != now.aramSize || saved.globalsSize != now.globalsSize)
		*why = "the game's memory is another size";
	return !*why;
}

} // namespace

// Every state in the folder, newest first.
static std::vector<PortSaveStateInfo> list_states()
{
	const std::string dir = state_dir();
	std::vector<PortSaveStateInfo> found;
	const Info now = current_info();
	if (DIR* d = opendir(dir.c_str())) {
		while (struct dirent* e = readdir(d)) {
			const std::string name = e->d_name;
			if (name[0] == '.' || !ends_with(name, ".smsstate"))
				continue;
			PortSaveStateInfo info;
			memset(&info, 0, sizeof info);
			info.area = -1;
			snprintf(info.path, sizeof info.path, "%s/%s", dir.c_str(), name.c_str());
			static const char* const kWanted[] = { "INFO", "PORT" };
			std::vector<Chunk> chunks;
			Info saved;
			PortSaveStatePortable p;
			if (!savestate::read_file(info.path, &chunks, kWanted, 2) || !read_info(chunks, &saved))
				continue;
			if (read_portable(chunks, &p)) {
				snprintf(info.stageName, sizeof info.stageName, "%s", p.stageName);
				snprintf(info.episodeName, sizeof info.episodeName, "%s", p.episodeName);
				// TFlagManager's game ints 0x40000.. and card int 0x20001
				info.area      = p.area;
				info.hasCounts = 1;
				info.shines    = p.gameInts[0];
				info.blueCoins = p.gameInts[1];
				info.coins     = p.gameInts[2];
				info.lives     = p.cardInts[1];
			}
			info.created = saved.created;
			when_text(saved.created, info.when, sizeof info.when);
			info.sameGame = same_game(saved, now);
			const char* why;
			info.exact = info.sameGame && exact_compatible(saved, now, &why);
			found.push_back(info);
		}
		closedir(d);
	}
	std::sort(found.begin(), found.end(), [](const PortSaveStateInfo& a, const PortSaveStateInfo& b) {
		return a.created != b.created ? a.created > b.created : strcmp(a.path, b.path) > 0;
	});
	return found;
}

extern "C" {

void port_savestate_hotkey(int action)
{
	if (!g_ready) {
		set_message(action == PORT_SAVESTATE_HOTKEY_SAVE ? "Save states work in a level, once it has loaded"
		                                                   : "Load states in a level, once it has loaded");
		return;
	}
	if (action == PORT_SAVESTATE_HOTKEY_SAVE) {
		std::lock_guard<std::mutex> l(g_lock);
		g_pending = PORT_SAVESTATE_SAVE;
		return;
	}
	PortSaveStateInfo newest;
	if (port_savestate_list(&newest, 1) < 1 || !newest.sameGame) {
		set_message("No save states yet");
		return;
	}
	port_savestate_request_load(newest.path);
}

void port_savestate_request_load(const char* path)
{
	std::lock_guard<std::mutex> l(g_lock);
	g_pending = PORT_SAVESTATE_LOAD;
	g_pendingPath = path;
}

int port_savestate_take(char* path, int pathLen)
{
	std::lock_guard<std::mutex> l(g_lock);
	const int action = g_pending;
	g_pending = PORT_SAVESTATE_NONE;
	if (action == PORT_SAVESTATE_LOAD)
		snprintf(path, pathLen, "%s", g_pendingPath.c_str());
	return action;
}

void port_savestate_set_ready(int ready) { g_ready = ready != 0; }
int port_savestate_ready(void) { return g_ready; }

void port_savestate_notify(const char* text) { set_message(text); }

float port_savestate_message(char* out, int outLen)
{
	std::lock_guard<std::mutex> l(g_lock);
	const double left = g_messageUntil - now_seconds();
	if (left <= 0)
		return 0;
	snprintf(out, outLen, "%s", g_message.c_str());
	return (float)left;
}

void port_savestate_hint(PortSaveStateHint* out)
{
	memset(out, 0, sizeof *out);
	out->count = port_pad_savestate_labels(GXPC_InputStyle ? GXPC_InputStyle() : 0, out->label);
	for (int i = 0; i < out->count; i++)
		out->hasImage[i] = port_button_glyph && port_button_glyph(PORT_PROMPT_SAVESTATE + i) != nullptr;
}

int port_savestate_save(const PortSaveStatePortable* portable)
{
	if (g_writing.exchange(true)) {
		set_message("Still saving the last state");
		return 0;
	}
	if (port_dvd_busy() || port_os_irq_work_pending()) {
		g_writing = false;
		set_message("Could not save the state: the game is busy");
		return 0;
	}
	const Info info = current_info();
	const bool exact = info.rangeCount && has_fingerprint();
	Pending* job = new Pending;
	job->path = state_dir() + "/" + file_name(*portable, (time_t)info.created) + ".smsstate";
	// another state this second: keep both
	for (int n = 2; exists(job->path) && n < 100; n++)
		job->path = state_dir() + "/" + file_name(*portable, (time_t)info.created) + " (" + std::to_string(n) + ").smsstate";

	const std::string json = metadata(info, *portable, exact);
	job->small.push_back(savestate::make_chunk("JSON", json.data(), json.size(), false));
	const std::vector<uint8_t> png = thumbnail();
	if (!png.empty())
		job->small.push_back(savestate::make_chunk("THMB", png.data(), png.size(), false));
	job->small.push_back(savestate::make_chunk("INFO", &info, sizeof info, false));
	job->small.push_back(savestate::make_chunk("PORT", portable, sizeof *portable, false));
	if (exact) {
		const std::vector<ThreadSig> threads = thread_sigs();
		job->small.push_back(
		    savestate::make_chunk("THRD", threads.data(), threads.size() * sizeof(ThreadSig), false));
		job->mem1.assign(port_mem1_base, port_mem1_base + port_mem1_size);
		u32 aramSize;
		u32* alloc;
		if (const u8* aram = port_ar_memory(&aramSize, &alloc))
			job->aram.assign(aram, aram + aramSize);
		job->globals.reserve(info.globalsSize);
		for (uint32_t i = 0; i < range_count(); i++) {
			const u8* a = (const u8*)(uintptr_t)kRanges.ranges[i][0];
			const u8* b = (const u8*)(uintptr_t)kRanges.ranges[i][1];
			job->globals.insert(job->globals.end(), a, b);
		}
	}
	port_log("[savestate] saving %s (%s)\n", job->path.c_str(), exact ? "exact" : "position only");
	write_in_background(job);
	return 1;
}

int port_savestate_can_load_exact(const char* path)
{
	static const char* const kWanted[] = { "INFO", "THRD" };
	std::vector<Chunk> chunks;
	Info saved;
	const Info now = current_info();
	const char* why = nullptr;
	return savestate::read_file(path, &chunks, kWanted, 2) && read_info(chunks, &saved) && same_game(saved, now)
	       && exact_compatible(saved, now, &why) && !port_dvd_busy() && !port_os_irq_work_pending()
	       && threads_match(chunks);
}

int port_savestate_load_exact(const char* path, PortSaveStatePortable* portable, int* hasPortable)
{
	if (hasPortable)
		*hasPortable = 0;
	std::vector<Chunk> chunks;
	if (!savestate::read_file(path, &chunks)) {
		set_message("That save state could not be read");
		return 0;
	}
	PortSaveStatePortable p;
	const bool havePortable = read_portable(chunks, &p);
	if (havePortable && portable)
		*portable = p;
	if (hasPortable)
		*hasPortable = havePortable;
	Info saved;
	const Info now = current_info();
	if (!read_info(chunks, &saved) || !same_game(saved, now)) {
		if (hasPortable)
			*hasPortable = 0;
		set_message("That save state is from another game");
		return 0;
	}
	const char* why = nullptr;
	if (!exact_compatible(saved, now, &why)) {
		port_log("[savestate] %s: loading the position only: %s\n", path, why);
		return 0;
	}
	if (port_dvd_busy() || port_os_irq_work_pending()) {
		port_log("[savestate] %s: the game is busy; loading the position only\n", path);
		return 0;
	}
	if (!threads_match(chunks)) {
		port_log("[savestate] %s: the game's threads are not as they were; loading the position only\n", path);
		return 0;
	}
	// Everything decompressed and checked before any of it is copied in.
	std::vector<uint8_t> mem1(now.mem1Size), aram(now.aramSize), globals(now.globalsSize);
	const Chunk* m = savestate::find_chunk(chunks, "MEM1");
	const Chunk* a = savestate::find_chunk(chunks, "ARAM");
	const Chunk* g = savestate::find_chunk(chunks, "GLOB");
	if (!m || !a || !g || !savestate::chunk_data(*m, mem1.data(), mem1.size())
	    || !savestate::chunk_data(*a, aram.data(), aram.size())
	    || !savestate::chunk_data(*g, globals.data(), globals.size())) {
		port_log("[savestate] %s: damaged; loading the position only\n", path);
		return 0;
	}
	chunks.clear();

	OSThread* list[64];
	const int n = std::min(port_os_thread_list(list, 64), 64);
	std::vector<OSThread> keep(n);
	for (int i = 0; i < n; i++)
		keep[i] = *list[i];

	memcpy(port_mem1_base, mem1.data(), mem1.size());
	u32 aramSize;
	u32* alloc;
	if (u8* ar = port_ar_memory(&aramSize, &alloc)) {
		memcpy(ar, aram.data(), std::min<size_t>(aramSize, aram.size()));
		*alloc = saved.aramAlloc;
	}
	size_t at = 0;
	for (uint32_t i = 0; i < range_count(); i++) {
		u8* start = (u8*)(uintptr_t)kRanges.ranges[i][0];
		const size_t len = (size_t)(kRanges.ranges[i][1] - kRanges.ranges[i][0]);
		memcpy(start, globals.data() + at, len);
		at += len;
	}
	for (int i = 0; i < n; i++)
		*list[i] = keep[i];
	// Textures and copies decoded from game memory are checked again.
	if (GXPC_MemoryWritten)
		GXPC_MemoryWritten(port_mem1_base, port_mem1_size);
	port_log("[savestate] loaded %s exactly\n", path);
	return 1;
}

int port_savestate_list(PortSaveStateInfo* out, int max)
{
	const std::vector<PortSaveStateInfo> found = list_states();
	const int n = std::min((int)found.size(), max);
	for (int i = 0; i < n; i++)
		out[i] = found[i];
	return (int)found.size();
}

uint8_t* port_savestate_thumbnail_png(const char* path, size_t* size)
{
	*size = 0;
	static const char* const kWanted[] = { "THMB" };
	std::vector<Chunk> chunks;
	if (!savestate::read_file(path, &chunks, kWanted, 1))
		return nullptr;
	const Chunk* c = savestate::find_chunk(chunks, "THMB");
	if (!c || c->codec != savestate::CODEC_RAW || c->stored.empty())
		return nullptr;
	uint8_t* out = (uint8_t*)malloc(c->stored.size());
	if (out) {
		memcpy(out, c->stored.data(), c->stored.size());
		*size = c->stored.size();
	}
	return out;
}

int port_savestate_delete(const char* path)
{
	const std::string p = path;
	if (!ends_with(p, ".smsstate") || p.compare(0, state_dir().size(), state_dir()) != 0)
		return 0;
	return remove(path) == 0;
}

} // extern "C"

namespace {
const PortSaveStateOverlayApi kOverlayApi = { port_savestate_message };
struct GiveOverlayApi {
	GiveOverlayApi()
	{
		if (GXPC_SetSaveStateApi)
			GXPC_SetSaveStateApi(&kOverlayApi);
	}
} s_giveOverlayApi;
} // namespace

// The state menu's view of the folder (src/savestate_menu.cpp) and its pictures.
namespace {
std::vector<PortSaveStateInfo> g_scanned;
struct PictureSlot {
	std::string path;
	u8* tex = nullptr;
	int w = 0, h = 0;
};
PictureSlot g_pictures[PORT_SAVESTATE_PICTURE_SLOTS];
const int kPictureMaxW = 320, kPictureMaxH = 256;

// RGBA rows to GX RGBA8 tiles: 4x4 blocks, each an AR half then a GB half.
void to_gx_rgba8(const u8* rgba, int w, int h, u8* out)
{
	size_t o = 0;
	for (int by = 0; by < h; by += 4)
		for (int bx = 0; bx < w; bx += 4)
			for (int half = 0; half < 2; half++)
				for (int y = 0; y < 4; y++)
					for (int x = 0; x < 4; x++) {
						const u8* p = rgba + ((size_t)(by + y) * w + bx + x) * 4;
						out[o++] = half ? p[1] : p[3];
						out[o++] = half ? p[2] : p[0];
					}
}
} // namespace

extern "C" int port_savestate_scan(void)
{
	g_scanned = list_states(); // all of them: no limit on how many states there are
	return (int)g_scanned.size();
}

extern "C" const PortSaveStateInfo* port_savestate_scanned(int index)
{
	return index >= 0 && index < (int)g_scanned.size() ? &g_scanned[index] : nullptr;
}

extern "C" const void* port_savestate_picture(int index, int slot, int* w, int* h)
{
	const PortSaveStateInfo* info = port_savestate_scanned(index);
	if (!info || slot < 0 || slot >= PORT_SAVESTATE_PICTURE_SLOTS)
		return nullptr;
	PictureSlot& s = g_pictures[slot];
	if (s.path != info->path) {
		s.path = info->path;
		s.w = s.h = 0;
		size_t size = 0;
		uint8_t* png = port_savestate_thumbnail_png(info->path, &size);
		int pw = 0, ph = 0, n = 0;
		stbi_uc* rgba = png ? stbi_load_from_memory(png, (int)size, &pw, &ph, &n, 4) : nullptr;
		free(png);
		if (rgba && pw > 0 && ph > 0) {
			// at most kPictureMaxW x kPictureMaxH, a multiple of four each way
			const float scale = std::min(1.0f, std::min((float)kPictureMaxW / pw, (float)kPictureMaxH / ph));
			const int tw = std::max(4, (int)(pw * scale) & ~3), th = std::max(4, (int)(ph * scale) & ~3);
			std::vector<u8> fit((size_t)tw * th * 4);
			for (int y = 0; y < th; y++)
				for (int x = 0; x < tw; x++)
					memcpy(&fit[((size_t)y * tw + x) * 4], &rgba[((size_t)(y * ph / th) * pw + x * pw / tw) * 4], 4);
			if (!s.tex)
				s.tex = (u8*)port_low_alloc((unsigned long)kPictureMaxW * kPictureMaxH * 4);
			if (s.tex) {
				to_gx_rgba8(fit.data(), tw, th, s.tex);
				s.w = tw;
				s.h = th;
			}
		}
		if (rgba)
			stbi_image_free(rgba);
	}
	if (!s.w)
		return nullptr;
	*w = s.w;
	*h = s.h;
	return s.tex;
}

// The worlds' names as the game draws them (port_savestate_world_art), read
// from the disc through the DVD layer (so mods' replacements count).
namespace {
uint32_t be32(const u8* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
uint16_t be16(const u8* p) { return (uint16_t)(p[0] << 8 | p[1]); }

std::vector<u8> read_disc(const char* path)
{
	std::vector<u8> out;
	DVDFileInfo fi;
	if (!DVDOpen((char*)path, &fi))
		return out;
	out.resize(fi.length);
	if (DVDReadPrio(&fi, out.data(), (s32)fi.length, 0, 2) != (s32)fi.length)
		out.clear();
	DVDClose(&fi);
	return out;
}

std::vector<u8> yaz0(const std::vector<u8>& src)
{
	if (src.size() < 16 || memcmp(src.data(), "Yaz0", 4) != 0)
		return src;
	const size_t size = be32(&src[4]);
	std::vector<u8> out;
	out.reserve(size);
	size_t s = 16;
	while (out.size() < size && s < src.size()) {
		const u8 code = src[s++];
		for (int bit = 0; bit < 8 && out.size() < size && s < src.size(); bit++) {
			if (code & (0x80 >> bit)) {
				out.push_back(src[s++]);
				continue;
			}
			if (s + 1 >= src.size())
				return std::vector<u8>();
			const u8 b1 = src[s], b2 = src[s + 1];
			s += 2;
			const size_t dist = ((b1 & 0xF) << 8 | b2) + 1;
			size_t n = b1 >> 4;
			n = n ? n + 2 : (s < src.size() ? src[s++] + 0x12 : 0);
			if (dist > out.size())
				return std::vector<u8>();
			for (size_t i = 0; i < n; i++)
				out.push_back(out[out.size() - dist]);
		}
	}
	return out;
}

// A file in a RARC archive by its name (anywhere in the archive); empty if absent.
std::vector<u8> rarc_file(const std::vector<u8>& arc, const char* name)
{
	if (arc.size() < 0x40 || memcmp(arc.data(), "RARC", 4) != 0)
		return std::vector<u8>();
	const u8* info = &arc[0x20];
	const uint32_t entries = be32(info + 0x08), entriesOff = be32(info + 0x0C) + 0x20;
	const uint32_t strOff = be32(info + 0x14) + 0x20, data = be32(&arc[0x0C]) + 0x20;
	for (uint32_t e = 0; e < entries; e++) {
		const size_t ent = entriesOff + e * 20;
		if (ent + 20 > arc.size())
			break;
		if (arc[ent + 4] & 0x02)
			continue; // a folder
		const size_t nameAt = strOff + (be32(&arc[ent + 4]) & 0xFFFFFF);
		if (nameAt >= arc.size() || strcasecmp((const char*)&arc[nameAt], name) != 0)
			continue;
		const size_t off = data + be32(&arc[ent + 8]), size = be32(&arc[ent + 12]);
		if (off + size <= arc.size())
			return std::vector<u8>(arc.begin() + off, arc.begin() + off + size);
	}
	return std::vector<u8>();
}

// A BTI's pixels as RGBA rows, for the formats the names use (IA4, RGB5A3).
bool bti_rgba(const std::vector<u8>& bti, std::vector<u8>& rgba, int* w, int* h)
{
	if (bti.size() < 0x20)
		return false;
	const int fmt = bti[0];
	*w = be16(&bti[2]);
	*h = be16(&bti[4]);
	const size_t img = be32(&bti[0x1C]);
	const int bw = fmt == 2 ? 8 : 4, bh = 4, bpp = fmt == 2 ? 8 : 16;
	if ((fmt != 2 && fmt != 5) || *w <= 0 || *h <= 0)
		return false;
	const size_t need = img + (size_t)((*w + bw - 1) / bw) * ((*h + bh - 1) / bh) * bw * bh * bpp / 8;
	if (need > bti.size())
		return false;
	rgba.assign((size_t)*w * *h * 4, 0);
	size_t o = img;
	for (int by = 0; by < *h; by += bh)
		for (int bx = 0; bx < *w; bx += bw)
			for (int y = 0; y < bh; y++)
				for (int x = 0; x < bw; x++) {
					u8 c[4];
					if (fmt == 2) {
						const u8 v = bti[o++];
						c[0] = c[1] = c[2] = (v & 15) * 17;
						c[3] = (v >> 4) * 17;
					} else {
						const uint16_t v = be16(&bti[o]);
						o += 2;
						if (v & 0x8000) {
							c[0] = ((v >> 10) & 31) * 255 / 31;
							c[1] = ((v >> 5) & 31) * 255 / 31;
							c[2] = (v & 31) * 255 / 31;
							c[3] = 255;
						} else {
							c[0] = ((v >> 8) & 15) * 17;
							c[1] = ((v >> 4) & 15) * 17;
							c[2] = (v & 15) * 17;
							c[3] = ((v >> 12) & 7) * 255 / 7;
						}
					}
					if (bx + x < *w && by + y < *h)
						memcpy(&rgba[((size_t)(by + y) * *w + bx + x) * 4], c, 4);
				}
	return true;
}

struct WorldArt {
	bool tried = false;
	u8* tex = nullptr;
	int w = 0, h = 0, artW = 0;
};
WorldArt g_worldArt[10][2];

// The episode select's banners and the file select's lettering, by world.
const char* const kBanner[10] = { nullptr, nullptr, "sc_map_bianco.bti", "sc_map_ricco.bti", "sc_map_mamma.bti",
	                              "sc_map_pinna.bti", "sc_map_sirena.bti", "sc_map_monte.bti", "sc_map_mare.bti",
	                              "sc_map_corona.bti" };
const char* const kLettering[10] = { "stage_airport.bti", "stage_dolpictown.bti", "stage_bianco.bti", "stage_ricco.bti",
	                                 "stage_mamma.bti", "stage_pinna.bti", "stage_sirena.bti", "stage_monte.bti",
	                                 "stage_mare.bti", nullptr };

const WorldArt* world_art(int world, int banner)
{
	WorldArt& a = g_worldArt[world][banner];
	if (a.tried)
		return a.tex ? &a : nullptr;
	a.tried = true;
	const char* name = banner ? kBanner[world] : kLettering[world];
	if (!name)
		return nullptr;
	static std::vector<u8> select, option;
	std::vector<u8>& arc = banner ? select : option;
	if (arc.empty())
		arc = yaz0(read_disc(banner ? "/data/select.szs" : "/data/option.szs"));
	std::vector<u8> rgba;
	int w = 0, h = 0;
	if (!bti_rgba(rarc_file(arc, name), rgba, &w, &h))
		return nullptr;
	// a multiple of four each way, the rest clear
	const int tw = (w + 3) & ~3, th = (h + 3) & ~3;
	std::vector<u8> padded((size_t)tw * th * 4, 0);
	for (int y = 0; y < h; y++)
		memcpy(&padded[(size_t)y * tw * 4], &rgba[(size_t)y * w * 4], (size_t)w * 4);
	a.tex = (u8*)port_low_alloc((unsigned long)padded.size());
	if (!a.tex)
		return nullptr;
	to_gx_rgba8(padded.data(), tw, th, a.tex);
	a.w = tw;
	a.h = th;
	a.artW = w;
	return &a;
}
} // namespace

extern "C" const void* port_savestate_world_art(int world, int banner, int* w, int* h, int* artW)
{
	if (world < 0 || world >= 10)
		return nullptr;
	const WorldArt* a = world_art(world, banner ? 1 : 0);
	if (!a)
		a = world_art(world, banner ? 0 : 1);
	if (!a)
		return nullptr;
	*w = a->w;
	*h = a->h;
	*artW = a->artW;
	return a->tex;
}

