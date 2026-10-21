// DVD: the disc's file system is served from an extracted disc directory
// (<root>/files, with the disc's own FST at <root>/../sys/fst.bin so entry
// numbers match the real disc). Reads are synchronous host preads; async
// completions are delivered as interrupts at the next check point.
#include "os/crash.h"
#include "port_compat.h"
#include "port_os.h"
#include "port_platform.h"
#include "port_framerate.h"
#include <dolphin/dvd.h>
#include <dolphin/os.h>
#include <dolphin/vi.h>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include "port_host.h"
#include <sys/stat.h>
#include <dirent.h>
#include <strings.h>
#include <algorithm>
#include "disc/gcdisc.h"
#include <sms_hd_movies.h>
#include <sms_language.h>
#include <deque>
#include <map>
#include <mutex>

namespace {

struct Entry {
	bool dir;
	std::string name;
	std::string host; // host path
	u32 parent;       // dirs: parent entry
	u32 next;         // dirs: one past last descendant
	u32 length;       // files: size
	int fd;
	const u8* mem;    // in-memory replacement (port_dvd_override)
	bool from_host = false; // read from `host` even with a disc image (a mod's file)
};

std::vector<Entry> g_fst;
GCDisc* g_disc; // disc image (platform/disc), or NULL for an extracted folder
u32 g_cwd;
DVDDiskID g_disk_id;
// Files and folders mods add (not on the disc), by lower-case absolute path
// without the leading '/'; their entries follow the disc's in g_fst.
std::unordered_map<std::string, u32> g_added;

// Files replaced inside disc archives, by the archive's entry: a mod folder
// named after an archive (files/data/scene/bianco0.szs/map/message.bmg)
// replaces that file inside it. The archive is rebuilt with them when it is
// opened and kept in memory for the next few opens.
struct ArchivePatch {
	std::map<std::string, std::string> files; // lower-case path below the archive's root folder -> host file
	std::vector<u8> built;                     // the rebuilt archive, while cached
	const u8* orig_mem = nullptr;              // what the entry served before
	u32 orig_len       = 0;
};
std::unordered_map<u32, ArchivePatch> g_patches;
std::deque<u32> g_patched_recent; // rebuilt archives in memory, oldest first
std::mutex g_patch_mutex;

u32 be32(const u8* p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

bool load_fst_bin(const std::string& files)
{
	std::string sys = files + "/../sys/fst.bin";
	FILE* f         = fopen(sys.c_str(), "rb");
	if (!f)
		return false;
	std::vector<u8> b;
	u8 buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0)
		b.insert(b.end(), buf, buf + n);
	fclose(f);
	if (b.size() < 12)
		return false;
	u32 count = be32(&b[8]);
	if (count * 12 > b.size())
		return false;
	const char* strings = (const char*)&b[count * 12];
	g_fst.resize(count);
	for (u32 i = 0; i < count; i++) {
		const u8* e = &b[i * 12];
		Entry& d    = g_fst[i];
		d.dir       = e[0] != 0;
		d.name      = i ? std::string(strings + (be32(e) & 0xFFFFFF)) : std::string();
		d.parent    = d.dir ? be32(e + 4) : 0;
		d.next      = d.dir ? be32(e + 8) : 0;
		d.length    = d.dir ? 0 : be32(e + 8);
		d.fd        = -1;
		d.mem       = NULL;
	}
	// Host paths: walk directories in order, tracking the path stack.
	g_fst[0].host = files;
	std::vector<u32> stack(1, 0);
	for (u32 i = 1; i < count; i++) {
		while (stack.size() > 1 && i >= g_fst[stack.back()].next)
			stack.pop_back();
		Entry& d = g_fst[i];
		d.host   = g_fst[stack.back()].host + "/" + d.name;
		if (d.dir)
			stack.push_back(i);
	}
	return true;
}

// Fallback without sys/fst.bin: build an FST from the directory tree, sorted
// case-insensitively like the mastering tools do.
void walk(const std::string& host, u32 parent)
{
	DIR* d = opendir(host.c_str());
	if (!d)
		return;
	std::vector<std::string> names;
	while (dirent* e = readdir(d))
		if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
			names.push_back(e->d_name);
	closedir(d);
	std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
		return strcasecmp(a.c_str(), b.c_str()) < 0;
	});
	for (size_t i = 0; i < names.size(); i++) {
		Entry e;
		e.name = names[i];
		e.host = host + "/" + names[i];
		e.fd   = -1;
		e.mem  = NULL;
		struct stat st;
		stat(e.host.c_str(), &st);
		e.dir    = S_ISDIR(st.st_mode);
		e.parent = parent;
		e.length = e.dir ? 0 : (u32)st.st_size;
		e.next   = 0;
		u32 idx  = (u32)g_fst.size();
		g_fst.push_back(e);
		if (e.dir) {
			walk(e.host, idx);
			g_fst[idx].next = (u32)g_fst.size();
		}
	}
}

static s32 lookup_disc(const char* path);

// The absolute path (lower case, no leading '/') of `path` from the current
// directory, for the entries mods add.
static std::string absolute_lower(const char* path)
{
	std::vector<std::string> parts;
	if (*path != '/')
		for (u32 d = g_cwd; d != 0; d = g_fst[d].parent)
			parts.insert(parts.begin(), g_fst[d].name);
	std::string p = path;
	size_t i = 0;
	while (i <= p.size()) {
		size_t j = p.find('/', i);
		if (j == std::string::npos)
			j = p.size();
		std::string part = p.substr(i, j - i);
		if (part == "..") {
			if (!parts.empty())
				parts.pop_back();
		} else if (!part.empty() && part != ".") {
			parts.push_back(part);
		}
		i = j + 1;
	}
	std::string out;
	for (size_t k = 0; k < parts.size(); k++)
		out += (k ? "/" : "") + parts[k];
	for (char& c : out)
		c = (char)tolower((unsigned char)c);
	return out;
}

s32 lookup(const char* path)
{
	s32 e = lookup_disc(path);
	if (e >= 0 || g_added.empty())
		return e;
	auto it = g_added.find(absolute_lower(path));
	return it == g_added.end() ? -1 : (s32)it->second;
}

static s32 lookup_disc(const char* path)
{
	u32 dir = g_cwd;
	if (*path == '/') {
		dir = 0;
		while (*path == '/')
			path++;
	}
	while (*path) {
		const char* end = strchr(path, '/');
		size_t len      = end ? (size_t)(end - path) : strlen(path);
		if (len == 0) {
		} else if (len == 1 && path[0] == '.') {
		} else if (len == 2 && path[0] == '.' && path[1] == '.') {
			dir = g_fst[dir].parent;
		} else {
			s32 found = -1;
			for (u32 i = dir + 1; i < g_fst[dir].next;) {
				const Entry& e = g_fst[i];
				if (e.name.size() == len && strncasecmp(e.name.c_str(), path, len) == 0) {
					found = (s32)i;
					break;
				}
				i = e.dir ? e.next : i + 1;
			}
			if (found < 0)
				return -1;
			if (!end)
				return found;
			if (!g_fst[found].dir)
				return -1;
			dir = (u32)found;
		}
		if (!end)
			break;
		path = end + 1;
	}
	return (s32)dir;
}

int file_fd(u32 entry)
{
	Entry& e = g_fst[entry];
	if (e.fd < 0)
		e.fd = open(e.host.c_str(), O_RDONLY
#ifdef _WIN32
		            | O_BINARY
#endif
		);
	return e.fd;
}

s32 do_read_raw(DVDFileInfo* fi, void* addr, s32 length, s32 offset)
{
	u32 entry = fi->startAddr;
	if (entry >= g_fst.size() || g_fst[entry].dir)
		return DVD_RESULT_FATAL_ERROR;
	if (g_disc && !g_fst[entry].mem && !g_fst[entry].from_host) {
		u32 n = gcdisc_read_file(g_disc, entry, (u32)offset, addr, (u32)length);
		fi->cb.transferredSize = n;
		return (s32)n;
	}
	if (const u8* mem = g_fst[entry].mem) {
		u32 len = g_fst[entry].length;
		s32 n   = offset >= (s32)len ? 0 : std::min(length, (s32)(len - offset));
		memcpy(addr, mem + offset, n);
		fi->cb.transferredSize = n;
		return n;
	}
	int fd = file_fd(entry);
	if (fd < 0)
		return DVD_RESULT_FATAL_ERROR;
	s32 done = 0;
	while (done < length) {
		ssize_t r = port_pread(fd, (u8*)addr + done, length - done, offset + done);
		if (r <= 0)
			break;
		done += (s32)r;
	}
	fi->cb.transferredSize = done;
	return done;
}

// The transfer wrote game memory behind the CPU's back, as DMA does: cached
// GX decodes of it are checked again (weak: absent without platform/gx).
extern "C" __attribute__((weak)) void GXPC_MemoryWritten(const void* p, u32 n);

s32 do_read(DVDFileInfo* fi, void* addr, s32 length, s32 offset)
{
	s32 n = do_read_raw(fi, addr, length, offset);
	if (n > 0 && GXPC_MemoryWritten)
		GXPC_MemoryWritten(addr, (u32)n);
	return n;
}

// --- Files replaced inside archives (g_patches) --------------------------------
void put32(u8* p, u32 v)
{
	p[0] = (u8)(v >> 24);
	p[1] = (u8)(v >> 16);
	p[2] = (u8)(v >> 8);
	p[3] = (u8)v;
}

bool yaz0_decode(const std::vector<u8>& in, std::vector<u8>& out)
{
	if (in.size() < 16)
		return false;
	u32 n = be32(&in[4]);
	out.clear();
	out.reserve(n);
	size_t i = 16;
	while (out.size() < n) {
		if (i >= in.size())
			return false;
		u8 code = in[i++];
		for (int b = 0; b < 8 && out.size() < n; b++, code <<= 1) {
			if (code & 0x80) {
				if (i >= in.size())
					return false;
				out.push_back(in[i++]);
				continue;
			}
			if (i + 1 >= in.size())
				return false;
			u32 b1 = in[i++], b2 = in[i++];
			size_t dist = ((b1 & 15) << 8 | b2) + 1;
			u32 len     = b1 >> 4;
			if (len == 0) {
				if (i >= in.size())
					return false;
				len = in[i++] + 0x12;
			} else {
				len += 2;
			}
			if (dist > out.size())
				return false;
			for (u32 k = 0; k < len && out.size() < n; k++)
				out.push_back(out[out.size() - dist]);
		}
	}
	return true;
}

// A valid Yaz0 stream holding `raw` as literals only (the loaders expect one
// where the disc had one; packing it again would only cost load time).
std::vector<u8> yaz0_store(const std::vector<u8>& raw)
{
	std::vector<u8> out(16, 0);
	memcpy(out.data(), "Yaz0", 4);
	put32(&out[4], (u32)raw.size());
	out.reserve(16 + raw.size() + raw.size() / 8 + 1);
	for (size_t i = 0; i < raw.size(); i += 8) {
		out.push_back(0xFF);
		out.insert(out.end(), raw.begin() + i, raw.begin() + std::min(raw.size(), i + 8));
	}
	return out;
}

bool read_host(const std::string& path, std::vector<u8>& out)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	out.clear();
	u8 buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0)
		out.insert(out.end(), buf, buf + n);
	fclose(f);
	return true;
}

// Replaces files in RARC archive `a` (big-endian, as on the disc): the new
// data is appended and the file entries pointed at it. Only replaces; the
// archive's folders and file list stay as they are.
bool rarc_replace(std::vector<u8>& a, const std::map<std::string, std::string>& files, const char* name)
{
	if (a.size() < 0x40 || memcmp(a.data(), "RARC", 4) || be32(&a[0x18]) != 0) {
		port_log("[dvd] %s: not an archive this can change\n", name);
		return false;
	}
	u32 dataStart = 0x20 + be32(&a[0x0C]);
	u32 nodeCount = be32(&a[0x20]), nodes = 0x20 + be32(&a[0x24]);
	u32 entryCount = be32(&a[0x28]), entries = 0x20 + be32(&a[0x2C]);
	u32 strings = 0x20 + be32(&a[0x34]);
	if (nodes + nodeCount * 16 > a.size() || entries + entryCount * 20 > a.size() || strings >= a.size())
		return false;
	auto name_at = [&](u32 off) {
		std::string n;
		for (u32 i = strings + off; i < a.size() && a[i]; i++)
			n += (char)tolower(a[i]);
		return n;
	};
	// every file's path below the root folder -> its entry
	std::map<std::string, u32> paths;
	std::vector<std::pair<u32, std::string>> todo = { { 0u, std::string() } };
	while (!todo.empty()) {
		auto [node, path] = todo.back();
		todo.pop_back();
		if (node >= nodeCount)
			return false;
		const u8* nd = &a[nodes + node * 16];
		u32 count = (u32)nd[10] << 8 | nd[11], first = be32(nd + 12);
		for (u32 k = first; k < first + count && k < entryCount; k++) {
			const u8* e  = &a[entries + k * 20];
			std::string n = name_at((u32)e[6] << 8 | e[7]);
			if (n == "." || n == "..")
				continue;
			if (e[4] & 0x02)
				todo.push_back({ be32(e + 8), path + "/" + n });
			else
				paths[path + "/" + n] = k;
		}
	}
	int done = 0;
	for (const auto& f : files) {
		auto it = paths.find(f.first);
		if (it == paths.end()) {
			port_log("[dvd] %s has no %s to replace\n", name, f.first.c_str());
			continue;
		}
		std::vector<u8> data;
		if (!read_host(f.second, data)) {
			port_log("[dvd] cannot read %s\n", f.second.c_str());
			continue;
		}
		a.resize((a.size() + 31) & ~(size_t)31, 0);
		u8* e = &a[entries + it->second * 20];
		put32(e + 8, (u32)(a.size() - dataStart));
		put32(e + 12, (u32)data.size());
		a.insert(a.end(), data.begin(), data.end());
		done++;
	}
	a.resize((a.size() + 31) & ~(size_t)31, 0);
	put32(&a[0x04], (u32)a.size());
	put32(&a[0x10], (u32)(a.size() - dataStart)); // data length
	put32(&a[0x14], (u32)(a.size() - dataStart)); // of it in main RAM (all of it)
	return done > 0;
}

// Rebuilds archive `entry` with its replaced files if it has any, before it
// is opened; the last few rebuilt archives stay in memory.
void prepare_patched(u32 entry)
{
	std::lock_guard<std::mutex> lock(g_patch_mutex);
	auto it = g_patches.find(entry);
	if (it == g_patches.end())
		return;
	ArchivePatch& p = it->second;
	Entry& d        = g_fst[entry];
	if (!p.built.empty()) {
		g_patched_recent.erase(std::remove(g_patched_recent.begin(), g_patched_recent.end(), entry),
		                       g_patched_recent.end());
		g_patched_recent.push_back(entry);
		return;
	}
	std::vector<u8> src(p.orig_len), raw;
	DVDFileInfo fi;
	memset(&fi, 0, sizeof fi);
	fi.startAddr = entry;
	if (do_read_raw(&fi, src.data(), (s32)src.size(), 0) != (s32)src.size())
		return;
	bool packed = src.size() >= 4 && !memcmp(src.data(), "Yaz0", 4);
	if (packed ? !yaz0_decode(src, raw) : (raw.swap(src), false))
		return;
	if (!rarc_replace(raw, p.files, d.name.c_str()))
		return;
	p.built  = packed ? yaz0_store(raw) : std::move(raw);
	d.mem    = p.built.data();
	d.length = (u32)p.built.size();
	g_patched_recent.push_back(entry);
	while (g_patched_recent.size() > 4) { // the oldest goes back to being made on its next open
		u32 old = g_patched_recent.front();
		g_patched_recent.pop_front();
		ArchivePatch& q  = g_patches[old];
		g_fst[old].mem    = q.orig_mem;
		g_fst[old].length = q.orig_len;
		std::vector<u8>().swap(q.built);
	}
}

} // namespace

// The disc source: SMS_DISC_IMAGE; or port_disc_root when the command line or
// SMS_DISC_ROOT names an image file (.iso/.gcm/.ciso); else the image bundled
// into the executable, if any; else port_disc_root (the platform default) as
// an image or as an extracted files/ folder.
static bool open_image()
{
	const char* img = getenv("SMS_DISC_IMAGE");
	if (!img && !port_disc_explicit) {
		g_disc = gcdisc_open_embedded(1);
		if (g_disc)
			img = "bundled with the executable";
	}
	if (!img) {
		struct stat st;
		if (port_disc_root && stat(port_disc_root, &st) == 0 && S_ISREG(st.st_mode))
			img = port_disc_root;
	}
	if (!img)
		return false;
	if (!g_disc)
		g_disc = gcdisc_open(img, 1);
	if (!g_disc) {
		port_log("[dvd] %s is not a usable GameCube disc image\n", img);
		exit(1);
	}
	u32 n = gcdisc_entry_count(g_disc);
	g_fst.resize(n);
	for (u32 i = 0; i < n; i++) {
		GCDiscEntry e;
		gcdisc_entry(g_disc, i, &e);
		Entry& d = g_fst[i];
		d.dir    = e.is_dir != 0;
		d.name   = i ? std::string(e.name) : std::string();
		d.parent = d.dir ? e.parent : 0;
		d.next   = d.dir ? e.next : 0;
		d.length = d.dir ? 0 : e.size;
		d.fd     = -1;
		d.mem    = NULL;
	}
	memcpy(&g_disk_id, gcdisc_header(g_disc), sizeof g_disk_id);
	port_log("[dvd] disc image %s (%s): %u entries\n", gcdisc_game_id(g_disc), img, n);
	return true;
}

// Mods (SMS_MOD=name;name, settings.txt `mod`): the files under each
// mods/<name>/files/ (or <name>/files/ when <name> is a path) take the place
// of the disc's file at the same path, or are added to the disc; a later mod
// wins over an earlier one. Asset-only mods (models, stages, textures in
// archives) need nothing else.
static u32 add_entry(const std::string& abs, bool dir, const std::string& host, u32 size)
{
	std::string low = abs;
	for (char& c : low)
		c = (char)tolower((unsigned char)c);
	auto it = g_added.find(low);
	if (it != g_added.end()) {
		Entry& d    = g_fst[it->second];
		d.host      = host;
		d.length    = size;
		d.fd        = -1;
		d.from_host = !d.dir;
		return it->second;
	}
	// its parent: a disc folder, or one a mod added
	u32 parent  = 0;
	size_t cut  = abs.rfind('/');
	std::string name = cut == std::string::npos ? abs : abs.substr(cut + 1);
	if (cut != std::string::npos) {
		std::string pabs = abs.substr(0, cut);
		s32 p            = lookup_disc(("/" + pabs).c_str());
		parent           = p >= 0 ? (u32)p : add_entry(pabs, true, std::string(), 0);
	}
	u32 idx = (u32)g_fst.size();
	Entry d;
	d.dir    = dir;
	d.name   = name;
	d.host   = host;
	d.parent = parent;
	d.next   = dir ? idx + 1 : 0; // lists as empty: its contents are found by path
	d.length = size;
	d.fd     = -1;
	d.mem       = NULL;
	d.from_host = !dir;
	g_fst.push_back(d);
	g_added[low] = idx;
	return idx;
}

// The files below `host`, as paths below it (`prefix` + name).
static void list_files(const std::string& host, const std::string& prefix, std::vector<std::pair<std::string, std::string>>& out)
{
	DIR* dp = opendir(host.c_str());
	if (!dp)
		return;
	while (dirent* e = readdir(dp)) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		std::string h = host + "/" + e->d_name, r = prefix + "/" + e->d_name;
		struct stat st;
		if (stat(h.c_str(), &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode))
			list_files(h, r, out);
		else
			out.push_back({ r, h });
	}
	closedir(dp);
}

static void overlay_dir(const std::string& host, const std::string& rel, int* replaced, int* added, int* inside)
{
	DIR* dp = opendir(host.c_str());
	if (!dp)
		return;
	std::vector<std::string> names;
	while (dirent* e = readdir(dp))
		if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
			names.push_back(e->d_name);
	closedir(dp);
	for (const std::string& n : names) {
		std::string h = host + "/" + n, r = rel.empty() ? n : rel + "/" + n;
		struct stat st;
		if (stat(h.c_str(), &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			s32 a = lookup_disc(("/" + r).c_str());
			if (a >= 0 && !g_fst[a].dir) { // named after a disc archive: files inside it
				std::vector<std::pair<std::string, std::string>> files;
				list_files(h, std::string(), files);
				ArchivePatch& p = g_patches[(u32)a];
				for (auto& f : files) {
					std::string low = f.first;
					for (char& c : low)
						c = (char)tolower((unsigned char)c);
					p.files[low] = f.second;
				}
				(*inside) += (int)files.size();
				continue;
			}
			overlay_dir(h, r, replaced, added, inside);
			continue;
		}
		s32 e = lookup_disc(("/" + r).c_str());
		if (e >= 0 && !g_fst[e].dir) { // the disc's file, replaced
			g_patches.erase((u32)e); // a later mod's whole archive wins over files put inside it
			Entry& d    = g_fst[e];
			d.host      = h;
			d.length    = (u32)st.st_size;
			d.fd        = -1;
			d.mem       = NULL;
			d.from_host = true;
			(*replaced)++;
		} else {
			add_entry(r, false, h, (u32)st.st_size);
			(*added)++;
		}
	}
}

static void apply_user_mods()
{
	const char* list = getenv("SMS_MOD");
	if (!list || !*list || !strcmp(list, "0") || !strcmp(list, "none"))
		return;
	std::string all = list, name;
	for (size_t i = 0; i <= all.size(); i++) {
		if (i < all.size() && all[i] != ';' && all[i] != ',') {
			name += all[i];
			continue;
		}
		if (name.empty())
			continue;
		std::string dir;
		struct stat st;
		for (const std::string& c : { name + "/files", "mods/" + name + "/files", "../../mods/" + name + "/files" })
			if (stat(c.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
				dir = c;
				break;
			}
		if (dir.empty()) {
			port_log("[dvd] mod %s: no files/ folder (looked in mods/%s/files)\n", name.c_str(), name.c_str());
		} else {
			int replaced = 0, added = 0, inside = 0;
			overlay_dir(dir, std::string(), &replaced, &added, &inside);
			port_log("[dvd] mod %s: %d disc files replaced, %d added, %d replaced inside archives (%s)\n",
			         name.c_str(), replaced, added, inside, dir.c_str());
		}
		name.clear();
	}
}

static void apply_mods()
{
	if (const char* pack = port_hd_cutscene_directory()) {
		int replaced = 0, added = 0, inside = 0;
		std::string files = std::string(pack) + "/files";
		overlay_dir(files, std::string(), &replaced, &added, &inside);
		port_log("[dvd] HD cutscenes: %d movies replaced (%s)\n", replaced, pack);
	}
	// The language pack, before explicit mods so that a mod's own files win.
	if (const char* pack = port_language_pack_dir()) {
		int replaced = 0, added = 0, inside = 0;
		overlay_dir(std::string(pack) + "/files", std::string(), &replaced, &added, &inside);
		port_log("[dvd] language %s: %d disc files replaced, %d inside archives (%s)\n", port_language_code(),
		         replaced, inside, pack);
	}
	// Explicit mods may provide their own story movies or other assets.
	apply_user_mods();
	for (auto& p : g_patches) {
		p.second.orig_mem = g_fst[p.first].mem;
		p.second.orig_len = g_fst[p.first].length;
	}
}

extern "C" void port_dvd_init(void)
{
	if (open_image()) {
		g_cwd = 0;
		apply_mods();
		return;
	}
	if (!port_disc_root) {
		port_log("[dvd] no game: pass a GMSE01 disc image (or extracted files/ folder), "
		         "or set SMS_DISC_IMAGE\n");
		exit(1);
	}
	std::string root = port_disc_root;
	if (!load_fst_bin(root)) {
		port_log("[dvd] no sys/fst.bin next to %s; building the FST from the directory tree\n", root.c_str());
		Entry r;
		r.dir    = true;
		r.host   = root;
		r.parent = 0;
		r.length = 0;
		r.fd     = -1;
		r.mem    = NULL;
		g_fst.push_back(r);
		walk(root, 0);
		g_fst[0].next = (u32)g_fst.size();
	}
	g_cwd = 0;
	memset(&g_disk_id, 0, sizeof g_disk_id);
	FILE* f = fopen((root + "/../sys/boot.bin").c_str(), "rb");
	if (f) {
		fread(&g_disk_id, 1, sizeof g_disk_id, f);
		fclose(f);
	} else {
		memcpy(g_disk_id.gameName, "GMSE", 4);
		memcpy(g_disk_id.company, "01", 2);
	}
	port_log("[dvd] FST: %u entries from %s\n", (unsigned)g_fst.size(), root.c_str());
	apply_mods();
}

// All of disc file `path`, read at once with no drive timing, for host use
// outside the game (the window icon). Returns a malloc'd buffer, NULL if the
// file is missing or unreadable.
extern "C" void* port_dvd_read_file(const char* path, size_t* size)
{
	s32 e = lookup(path);
	if (e < 0 || g_fst[e].dir)
		return NULL;
	DVDFileInfo fi;
	DVDFastOpen(e, &fi);
	void* buf = malloc(fi.length ? fi.length : 1);
	if (!buf)
		return NULL;
	if (do_read(&fi, buf, (s32)fi.length, 0) != (s32)fi.length) {
		free(buf);
		return NULL;
	}
	*size = fi.length;
	return buf;
}

// Serve `path` (an existing disc file) from memory instead of the disc.
extern "C" int port_dvd_override(const char* path, const void* data, u32 size)
{
	s32 e = lookup(path);
	if (e < 0 || g_fst[e].dir)
		return 0;
	g_fst[e].mem    = (const u8*)data;
	g_fst[e].length = size;
	return 1;
}

extern "C" void DVDInit(void) {}
#ifdef __ANDROID__
extern "C" void port_thor_note_path(const char* path);
#endif
extern "C" s32 DVDConvertPathToEntrynum(char* path)
{
	s32 r = lookup(path);
#ifdef __ANDROID__
	port_thor_note_path(path);
#endif
	port_crash_note_file(path);
	if (r < 0)
		port_log("[dvd] path not found: %s\n", path);
	return r;
}

extern "C" BOOL DVDFastOpen(s32 entry, DVDFileInfo* fi)
{
	if (entry < 0 || (u32)entry >= g_fst.size() || g_fst[entry].dir)
		return FALSE;
	if (!g_patches.empty())
		prepare_patched((u32)entry);
	memset(fi, 0, sizeof *fi);
	fi->startAddr = (u32)entry;
	fi->length    = g_fst[entry].length;
	fi->cb.state  = DVD_STATE_END;
	return TRUE;
}

extern "C" BOOL DVDOpen(char* name, DVDFileInfo* fi)
{
	s32 e = DVDConvertPathToEntrynum(name);
	if (e < 0)
		return FALSE;
	return DVDFastOpen(e, fi);
}

extern "C" BOOL DVDClose(DVDFileInfo* fi) { return TRUE; }

extern "C" BOOL DVDChangeDir(char* dirName)
{
	s32 e = lookup(dirName);
	if (e < 0 || !g_fst[e].dir)
		return FALSE;
	g_cwd = (u32)e;
	return TRUE;
}

extern "C" BOOL DVDGetCurrentDir(char* path, u32 maxlen)
{
	std::string p;
	for (u32 d = g_cwd; d != 0; d = g_fst[d].parent)
		p = "/" + g_fst[d].name + p;
	if (p.empty())
		p = "/";
	strncpy(path, p.c_str(), maxlen);
	return TRUE;
}

// --- Drive timing model --------------------------------------------------------
// Off by default (reads complete at once). SMS_DVD_BPS=<bytes/s> and
// SMS_DVD_SEEK_MS=<ms per read> make each read occupy the drive for that long,
// measured in VI fields, so the game's timeline (e.g. the boot-field count at
// which the Nintendo logo appears) can be brought in line with retail in
// SMS_VI_DETERMINISTIC runs. SMS_DVD_LOG=1 logs every read with its field.
namespace {
double g_dvd_bps, g_dvd_seek_fields;
bool g_dvd_log, g_dvd_timing_init;
double g_drive_free; // field at which the drive becomes idle
struct PendingRead {
	double due;
	std::function<void()> done;
};
std::vector<PendingRead> g_pending_reads;

void dvd_timing_init()
{
	if (g_dvd_timing_init)
		return;
	g_dvd_timing_init = true;
	if (const char* e = getenv("SMS_DVD_BPS"))
		g_dvd_bps = atof(e);
	if (const char* e = getenv("SMS_DVD_SEEK_MS"))
		g_dvd_seek_fields = atof(e) * 59.94 / 1000.0;
	g_dvd_log = getenv("SMS_DVD_LOG") != NULL;
}

// Field at which a read of `len` bytes issued now completes.
double schedule_read(u32 entry, s32 len, s32 offset)
{
	dvd_timing_init();
	double now = (double)VIGetRetraceCount();
	if (g_dvd_log)
		port_log("[dvd] field %u: read %s +0x%x, 0x%x bytes\n", (unsigned)now,
		         entry < g_fst.size() ? g_fst[entry].name.c_str() : "?", offset, len);
	if (g_dvd_bps <= 0 && g_dvd_seek_fields <= 0)
		return now;
	double start = g_drive_free > now ? g_drive_free : now;
	g_drive_free = start + port_vi_retrace_multiplier()
	    * (g_dvd_seek_fields + (g_dvd_bps > 0 ? len / g_dvd_bps * 59.94 : 0));
	return g_drive_free;
}

void poll_reads()
{
	if (g_pending_reads.empty())
		return;
	double now = (double)VIGetRetraceCount();
	for (size_t i = 0; i < g_pending_reads.size();) {
		if (g_pending_reads[i].due <= now) {
			std::function<void()> fn = g_pending_reads[i].done;
			g_pending_reads.erase(g_pending_reads.begin() + i);
			fn();
		} else {
			i++;
		}
	}
}
} // namespace

// A read is still on its way (save states wait for the drive to be idle).
extern "C" int port_dvd_busy(void) { return !g_pending_reads.empty(); }

extern "C" s32 DVDReadPrio(DVDFileInfo* fi, void* addr, s32 length, s32 offset, s32 prio)
{
	fi->cb.state = DVD_STATE_BUSY;
	double due   = schedule_read(fi->startAddr, length, offset);
	s32 r        = do_read(fi, addr, length, offset);
	while ((double)VIGetRetraceCount() < due)
		VIWaitForRetrace(); // the thread sleeps while the drive works
	fi->cb.state = r < 0 ? DVD_STATE_FATAL_ERROR : DVD_STATE_END;
	port_irq_check();
	return r;
}

extern "C" BOOL DVDReadAsyncPrio(DVDFileInfo* fi, void* addr, s32 length, s32 offset, DVDCallback cb, s32 prio)
{
	fi->cb.state = DVD_STATE_BUSY;
	fi->callback = cb;
	double due   = schedule_read(fi->startAddr, length, offset);
	s32 r        = do_read(fi, addr, length, offset);
	std::function<void()> done = [fi, r, cb]() {
		fi->cb.state = r < 0 ? DVD_STATE_FATAL_ERROR : DVD_STATE_END;
		if (cb)
			cb(r, fi);
	};
	if (due > (double)VIGetRetraceCount()) {
		static bool registered;
		if (!registered) {
			registered = true;
			port_irq_add_source(poll_reads);
		}
		PendingRead pr = { due, done };
		g_pending_reads.push_back(pr);
		return TRUE;
	}
	port_irq_defer(done);
	port_irq_kick();
	return TRUE;
}

extern "C" s32 DVDGetCommandBlockStatus(DVDCommandBlock* block)
{
	port_irq_check();
	return block->state;
}
extern "C" s32 DVDGetFileInfoStatus(DVDFileInfo* fi) { return DVDGetCommandBlockStatus(&fi->cb); }
extern "C" s32 DVDGetDriveStatus(void)
{
	port_irq_check();
	return DVD_STATE_END;
}
extern "C" BOOL DVDCheckDisk(void) { return TRUE; }
extern "C" DVDDiskID* DVDGetCurrentDiskID(void) { return &g_disk_id; }
extern "C" s32 DVDGetTransferredSize(DVDFileInfo* fi) { return fi->cb.transferredSize; }
extern "C" s32 DVDCancel(DVDCommandBlock* block)
{
	block->state = DVD_STATE_END;
	return 0;
}
extern "C" BOOL DVDCancelAsync(DVDCommandBlock* block, DVDCBCallback cb)
{
	block->state = DVD_STATE_END;
	if (cb)
		port_irq_defer([block, cb]() { cb(0, block); });
	return TRUE;
}

extern "C" BOOL DVDOpenDir(char* dirName, DVDDir* dir)
{
	s32 e = lookup(dirName);
	if (e < 0 || !g_fst[e].dir)
		return FALSE;
	dir->entryNum = (u32)e;
	dir->location = (u32)e + 1;
	dir->next     = g_fst[e].next;
	return TRUE;
}

extern "C" BOOL DVDReadDir(DVDDir* dir, DVDDirEntry* ent)
{
	u32 i = dir->location;
	if (i <= dir->entryNum || i >= dir->next)
		return FALSE;
	ent->entryNum = i;
	ent->isDir    = g_fst[i].dir;
	ent->name     = (char*)g_fst[i].name.c_str();
	dir->location = g_fst[i].dir ? g_fst[i].next : i + 1;
	return TRUE;
}

extern "C" BOOL DVDCloseDir(DVDDir* dir) { return TRUE; }

// Streaming audio (DTK): the region is read whole and played by the Audio
// Interface (platform/audio/dtk.cpp). The stream stops at the end of its
// region; DVDStopStreamAtEndAsync asks for nothing more.
extern "C" void port_dtk_prepare(u32 base, u32 offset, std::vector<u8>& data);
extern "C" void port_dtk_cancel(void);
extern "C" u32 port_dtk_play_addr(void);
extern "C" int port_dtk_active(void);

extern "C" BOOL DVDPrepareStreamAsync(DVDFileInfo* fi, u32 length, u32 offset, DVDCallback cb)
{
	std::vector<u8> data;
	u32 entry = fi->startAddr;
	if (entry < g_fst.size() && !g_fst[entry].dir && offset < g_fst[entry].length) {
		u32 len = g_fst[entry].length - offset;
		if (length && length < len)
			len = length;
		data.resize(len);
		DVDFileInfo tmp = *fi;
		s32 n           = do_read(&tmp, data.data(), (s32)len, (s32)offset);
		data.resize(n > 0 ? (size_t)n : 0);
	}
	port_dtk_prepare(entry, offset, data);
	if (cb)
		port_irq_defer([fi, cb]() { cb(0, fi); });
	return TRUE;
}
extern "C" s32 DVDCancelStream(DVDCommandBlock*)
{
	port_dtk_cancel();
	return 0;
}
extern "C" BOOL DVDCancelStreamAsync(DVDCommandBlock* block, DVDCBCallback cb)
{
	port_dtk_cancel();
	if (cb)
		port_irq_defer([block, cb]() { cb(0, block); });
	return TRUE;
}
extern "C" BOOL DVDStopStreamAtEndAsync(DVDCommandBlock* block, DVDCBCallback cb)
{
	if (cb)
		port_irq_defer([block, cb]() { cb(0, block); });
	return TRUE;
}
extern "C" BOOL DVDGetStreamPlayAddrAsync(DVDCommandBlock* block, DVDCBCallback cb)
{
	u32 addr = port_dtk_play_addr();
	if (cb)
		port_irq_defer([block, cb, addr]() { cb((s32)addr, block); });
	return TRUE;
}
// 1 while the stream plays, 0 once it is cancelled or has played its region
extern "C" BOOL DVDGetStreamErrorStatusAsync(DVDCommandBlock* block, DVDCBCallback cb)
{
	s32 status = port_dtk_active();
	if (cb)
		port_irq_defer([block, cb, status]() { cb(status, block); });
	return TRUE;
}
