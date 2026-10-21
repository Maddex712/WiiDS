/* GameCube disc-image reader (platform/disc).
 *
 * Reads a plain 1:1 disc image (.iso/.gcm) or a CISO (.ciso, Dolphin's
 * sparse block format) and exposes the disc header, the system files and the
 * file system table (FST). All on-disc fields are big-endian; the API returns
 * host values. Reads use pread and are safe from several threads. */
#ifndef SMS_PORT_GCDISC_H
#define SMS_PORT_GCDISC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GCDisc GCDisc;

typedef struct GCDiscEntry {
	const char* name;  /* entry name ("" for the root) */
	int is_dir;
	uint32_t parent;   /* dirs: parent entry index */
	uint32_t next;     /* dirs: one past the last entry inside it */
	uint32_t offset;   /* files: disc offset of the data */
	uint32_t size;     /* files: size in bytes */
} GCDiscEntry;

enum GCDiscSystemFile {
	GCDISC_BOOT,      /* disc header "boot.bin", 0x000..0x440 */
	GCDISC_BI2,       /* "bi2.bin", 0x440..0x2440 */
	GCDISC_APPLOADER, /* "apploader.img" at 0x2440 */
	GCDISC_DOL,       /* "main.dol" */
	GCDISC_FST,       /* "fst.bin" (big-endian, as on disc) */
};

/* Open an image; NULL on failure (errors are printed to stderr when
 * `verbose`). */
GCDisc* gcdisc_open(const char* path, int verbose);
void gcdisc_close(GCDisc* d);

/* The disc image bundled into the running executable by
 * tools/bundle_disc.py (on macOS, else Contents/Resources/disc.gcm of the
 * app bundle it runs in), or NULL if there is none. */
GCDisc* gcdisc_open_embedded(int verbose);
/* Path of the running executable; 0 on failure. */
int gcdisc_self_path(char* buf, uint32_t bufsize);

/* True if `path` looks like a disc image (by content, not by name). */
int gcdisc_probe(const char* path);

/* Game ID ("GMSE01", NUL-terminated) and the 0x440-byte header. */
const char* gcdisc_game_id(const GCDisc* d);
const uint8_t* gcdisc_header(const GCDisc* d);
/* Size of the disc (the image's logical size). */
uint64_t gcdisc_size(const GCDisc* d);

/* Read `size` bytes at disc offset `offset`; returns bytes read. */
uint32_t gcdisc_read(GCDisc* d, uint64_t offset, void* buf, uint32_t size);

/* System files: location on disc (offset/size); 0 if absent. */
int gcdisc_system_file(const GCDisc* d, int which, uint32_t* offset, uint32_t* size);

/* FST access. Entry 0 is the root. The raw big-endian table (fst.bin) is
 * available for layers that parse it themselves (platform/dvd). */
uint32_t gcdisc_entry_count(const GCDisc* d);
int gcdisc_entry(const GCDisc* d, uint32_t index, GCDiscEntry* out);
const uint8_t* gcdisc_fst(const GCDisc* d, uint32_t* size);

/* Resolve a path ("/data/common.szs", "data/common.szs"; case-insensitive,
 * like DVDConvertPathToEntrynum from the root) to an entry index, or -1. */
int32_t gcdisc_lookup(const GCDisc* d, const char* path);
/* Full path of an entry ("/data/common.szs"); returns the length written. */
uint32_t gcdisc_path(const GCDisc* d, uint32_t index, char* buf, uint32_t bufsize);

/* Read from a file entry; returns bytes read (0 past the end / for dirs). */
uint32_t gcdisc_read_file(GCDisc* d, uint32_t index, uint32_t file_offset, void* buf, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif
