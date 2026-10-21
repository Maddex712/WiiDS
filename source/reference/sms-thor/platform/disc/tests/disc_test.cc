// Host test for platform/disc against the user's disc image (read-only) and
// the extracted copy of the same disc:
//  - every FST entry resolves by path back to itself, files lie inside the
//    image, sizes match the extracted files;
//  - system files equal orig/<ID>/sys/*; chosen files are byte-identical;
//  - a synthetic CISO of a synthetic disc reads back identically.
//   make -C platform/disc/tests run ISO=... FILES=.../files [FULL=1]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "../gcdisc.h"

static int g_fail, g_checks;
#define CHECK(c, ...)                              \
	do {                                           \
		g_checks++;                                \
		if (!(c) && g_fail++ < 40) {               \
			fprintf(stderr, "FAIL: " __VA_ARGS__); \
			fputc('\n', stderr);                   \
		}                                          \
	} while (0)

static bool read_host(const std::string& p, std::vector<uint8_t>& out, long long max = -1)
{
	FILE* f = fopen(p.c_str(), "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long long n = ftell(f);
	if (max >= 0 && n > max)
		n = max;
	fseek(f, 0, SEEK_SET);
	out.resize(n);
	bool ok = fread(out.data(), 1, n, f) == (size_t)n;
	fclose(f);
	return ok;
}

static bool same_file(GCDisc* d, uint32_t idx, const std::string& host, uint32_t size)
{
	FILE* f = fopen(host.c_str(), "rb");
	if (!f)
		return false;
	std::vector<uint8_t> a(1 << 20), b(1 << 20);
	bool same = true;
	for (uint32_t off = 0; off < size && same; off += a.size()) {
		uint32_t n = size - off < a.size() ? size - off : (uint32_t)a.size();
		same       = gcdisc_read_file(d, idx, off, a.data(), n) == n && fread(b.data(), 1, n, f) == n &&
		       memcmp(a.data(), b.data(), n) == 0;
	}
	fclose(f);
	return same;
}

static void test_ciso(const char* tmpdir)
{
	// Synthetic disc: header, FST with /a.bin and /d/b.bin, spread over blocks.
	const uint32_t bs = 0x8000, nblocks = 64;
	std::vector<uint8_t> disc(bs * nblocks, 0);
	auto put32 = [&](uint32_t o, uint32_t v) {
		disc[o] = v >> 24, disc[o + 1] = v >> 16, disc[o + 2] = v >> 8, disc[o + 3] = v;
	};
	memcpy(&disc[0], "GTST01", 6);
	put32(0x1C, 0xC2339F3D);
	const uint32_t fst = 0x10000;
	put32(0x424, fst);
	const char strings[] = "a.bin\0d\0b.bin";
	put32(0x428, 4 * 12 + sizeof(strings));
	// root, a.bin, d (dir), d/b.bin
	put32(fst + 0, 0x01000000), put32(fst + 4, 0), put32(fst + 8, 4);
	put32(fst + 12, 0), put32(fst + 16, 0x20000), put32(fst + 20, 5000);
	put32(fst + 24, 0x01000006), put32(fst + 28, 0), put32(fst + 32, 4);
	put32(fst + 36, 8), put32(fst + 40, 0x30000 + 123), put32(fst + 44, bs * 2);
	memcpy(&disc[fst + 48], strings, sizeof(strings));
	for (uint32_t i = 0; i < 5000; i++)
		disc[0x20000 + i] = (uint8_t)(i * 7);
	for (uint32_t i = 0; i < bs * 2; i++)
		disc[0x30000 + 123 + i] = (uint8_t)(i * 13 + 1);
	// CISO: store only non-zero blocks
	std::vector<uint8_t> ciso(0x8000, 0);
	memcpy(&ciso[0], "CISO", 4);
	ciso[4] = bs & 0xFF, ciso[5] = (bs >> 8) & 0xFF, ciso[6] = (bs >> 16) & 0xFF;
	for (uint32_t b = 0; b < nblocks; b++) {
		bool nz = false;
		for (uint32_t i = 0; i < bs && !nz; i++)
			nz = disc[b * bs + i] != 0;
		if (nz) {
			ciso[8 + b] = 1;
			ciso.insert(ciso.end(), disc.begin() + b * bs, disc.begin() + (b + 1) * bs);
		}
	}
	std::string iso = std::string(tmpdir) + "/synthetic.iso", cso = std::string(tmpdir) + "/synthetic.ciso";
	FILE* f = fopen(iso.c_str(), "wb");
	fwrite(disc.data(), 1, disc.size(), f);
	fclose(f);
	f = fopen(cso.c_str(), "wb");
	fwrite(ciso.data(), 1, ciso.size(), f);
	fclose(f);
	GCDisc* a = gcdisc_open(iso.c_str(), 1);
	GCDisc* c = gcdisc_open(cso.c_str(), 1);
	CHECK(a && c, "synthetic images open");
	if (a && c) {
		CHECK(gcdisc_entry_count(c) == 4 && gcdisc_lookup(c, "/D/B.BIN") == 3 && gcdisc_lookup(c, "a.bin") == 1,
		      "synthetic FST lookup");
		std::vector<uint8_t> x(bs * 2), y(bs * 2);
		CHECK(gcdisc_read_file(c, 3, 0, x.data(), bs * 2) == bs * 2 &&
		          gcdisc_read_file(a, 3, 0, y.data(), bs * 2) == bs * 2 && x == y &&
		          x[5] == (uint8_t)(5 * 13 + 1),
		      "CISO file read equals plain image");
		CHECK(gcdisc_read(c, bs * 1, x.data(), 16) == 16 && x[0] == 0, "unstored CISO block reads zero");
		char p[64];
		gcdisc_path(c, 3, p, sizeof p);
		CHECK(strcmp(p, "/d/b.bin") == 0, "path of entry 3: %s", p);
	}
	gcdisc_close(a);
	gcdisc_close(c);
	unlink(iso.c_str());
	unlink(cso.c_str());
	printf("ciso: synthetic image ok\n");
}

int main()
{
	if (!getenv("ISO") || !getenv("FILES")) {
		fprintf(stderr, "set ISO=<GMSE01 disc image> and FILES=<its extracted files/ folder>\n");
		return 2;
	}
	std::string iso   = getenv("ISO");
	std::string files = getenv("FILES");
	bool full         = getenv("FULL") != NULL;
	const char* tmp   = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";

	test_ciso(tmp);

	GCDisc* d = gcdisc_open(iso.c_str(), 1);
	CHECK(d, "open %s", iso.c_str());
	if (!d)
		return 1;
	CHECK(gcdisc_probe(iso.c_str()), "probe");
	printf("%s: %s \"%s\", %llu bytes, %u FST entries\n", iso.c_str(), gcdisc_game_id(d),
	       (const char*)gcdisc_header(d) + 0x20, (unsigned long long)gcdisc_size(d), gcdisc_entry_count(d));

	// System files vs the extracted sys/.
	static const char* sysname[] = { "boot.bin", "bi2.bin", "apploader.img", "main.dol", "fst.bin" };
	for (int s = GCDISC_BOOT; s <= GCDISC_FST; s++) {
		uint32_t off, size;
		std::vector<uint8_t> host, img;
		CHECK(gcdisc_system_file(d, s, &off, &size), "system file %s", sysname[s]);
		if (!read_host(files + "/../sys/" + sysname[s], host))
			continue;
		img.resize(size);
		gcdisc_read(d, off, img.data(), size);
		CHECK(size == host.size() && img == host, "%s: %u bytes at %#x vs extracted %zu", sysname[s], size, off,
		      host.size());
	}
	uint32_t fstsize;
	gcdisc_fst(d, &fstsize);

	// Every entry: lookup(path) == index, inside the image, size matches.
	uint32_t n = gcdisc_entry_count(d), nfiles = 0, ndirs = 0;
	unsigned long long bytes = 0;
	std::vector<uint32_t> chosen;
	for (uint32_t i = 1; i < n; i++) {
		GCDiscEntry e;
		gcdisc_entry(d, i, &e);
		char path[512];
		gcdisc_path(d, i, path, sizeof path);
		CHECK(gcdisc_lookup(d, path) == (int32_t)i, "lookup(%s) = %d, want %u", path, gcdisc_lookup(d, path), i);
		if (e.is_dir) {
			ndirs++;
			CHECK(e.next > i && e.next <= n && e.parent < i, "dir %s range", path);
			struct stat st;
			CHECK(stat((files + path).c_str(), &st) == 0 && S_ISDIR(st.st_mode), "extracted dir %s", path);
			continue;
		}
		nfiles++;
		bytes += e.size;
		CHECK((unsigned long long)e.offset + e.size <= gcdisc_size(d), "%s past the end of the image", path);
		struct stat st;
		CHECK(stat((files + path).c_str(), &st) == 0 && (uint64_t)st.st_size == e.size, "%s size %u vs extracted",
		      path, e.size);
		if (full || strstr(path, "nintendo.szs") || strstr(path, "PerformLists.bin") || strstr(path, "mSound.asn") ||
		    strstr(path, "openingA.thp") || strstr(path, "mario.szs") || strstr(path, "/scene/dolpic0.szs") ||
		    strstr(path, "stageArc.bin") || strstr(path, "opening.bnr"))
			chosen.push_back(i);
	}
	printf("fst: %u files (%.1f MiB), %u directories: every entry resolves by path\n", nfiles, bytes / 1048576.0,
	       ndirs);
	CHECK(gcdisc_lookup(d, "/data/../data/COMMON.SZS") == gcdisc_lookup(d, "data/common.szs") &&
	          gcdisc_lookup(d, "data/common.szs") > 0 && gcdisc_lookup(d, "/nope.bin") == -1,
	      "path normalisation");

	for (uint32_t i : chosen) {
		GCDiscEntry e;
		gcdisc_entry(d, i, &e);
		char path[512];
		gcdisc_path(d, i, path, sizeof path);
		bool same = same_file(d, i, files + path, e.size);
		CHECK(same, "%s differs from the extracted copy", path);
		if (!full)
			printf("  %-32s %9u bytes at %#010x: %s\n", path, e.size, e.offset, same ? "byte-identical" : "DIFFERENT");
	}
	if (full)
		printf("full compare: %zu files byte-identical\n", chosen.size());
	gcdisc_close(d);
	printf("%d checks, %d failures\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
