/* Big-endian -> host conversion of Super Mario Sunshine resource files that the
 * game reads in place (platform/endian). See INVENTORY.md.
 *
 * Every converter works in place on a buffer holding the whole file, is
 * bounds-checked against `size`, and is idempotent where the format has a
 * magic (the magic itself is converted, so a converted file is recognised).
 * Texture/TLUT payloads and display lists stay big-endian: platform/gx decodes
 * them in disc byte order. Vertex arrays are converted to host order (the CPU
 * reads them too); do not register converted models with
 * GXPC_AddBigEndianRange. Data the game reads through JSUInputStream typed
 * reads (decomp-patches/0013) is left big-endian. */
#ifndef SMS_PORT_ENDIAN_H
#define SMS_PORT_ENDIAN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum PortEndianFormat {
	PE_FMT_UNKNOWN = 0,
	PE_FMT_ALREADY_NATIVE, /* recognised, converted before */
	PE_FMT_J3D_MODEL,      /* J3D2 bmd2/bmd3/bdl3/bmt2/bmt3 */
	PE_FMT_J3D_ANIM,       /* J3D1 bck1/bca1/btk1/brk1/bpk1/bpa1/btp1/bva1/blk1/bla1/bxk1/bxa1 */
	PE_FMT_BTI,            /* ResTIMG header (no magic) */
	PE_FMT_JPA,            /* JEFFjpa1 */
	PE_FMT_BFN,            /* FONTbfn1 */
	PE_FMT_AAF,            /* JAudio init data (no magic) */
	PE_FMT_BNK,            /* JAudio IBNK instrument bank */
	PE_FMT_WSYS,           /* JAudio WSYS wave system */
	PE_FMT_BAS,            /* JAudio animation sound table */
	PE_FMT_GAME,           /* SMS formats: .col .ral .ymp .pad .bcr .sb */
	PE_FMT_KEEP_BE,        /* recognised; stays big-endian (read via typed streams / BE-aware code) */
};

/* Convert a resource recognised by its magic. `name` (may be NULL) is the
 * archive/file name, used for formats without a magic (.bti, .aaf). Returns a
 * PortEndianFormat; PE_FMT_UNKNOWN leaves the buffer untouched. */
int port_endian_resource(void* data, uint32_t size, const char* name);

/* Individual converters (return 1 if converted, 0 if not recognised or already
 * converted). */
int port_endian_j3d(void* data, uint32_t size);
int port_endian_bti(void* data, uint32_t size);          /* one ResTIMG at data (sets a converted mark at 0x19) */
int port_endian_timg_header(void* timg);                 /* 0x20-byte ResTIMG header */
int port_endian_tlut(void* data, uint32_t size);          /* ResTLUT header (.bpl) */
int port_endian_ntab(void* ntab, uint32_t size);          /* ResNTAB */
int port_endian_jpa(void* data, uint32_t size);
int port_endian_aaf(void* data, uint32_t size);
int port_endian_ibnk(void* data, uint32_t size);
int port_endian_wsys(void* data, uint32_t size);
int port_endian_sound_table(void* data, uint32_t size);
int port_endian_game(void* data, uint32_t size, const char* name); /* SMS formats by name */  /* JAISoundTable image (.bst in AAF) */

/* Archive-loader hook: convert a freshly fetched resource (by magic, else by
 * name), falling back to port_res_to_native (platform/misc) for RARC/BFN and
 * the log of unconverted formats. */
void port_endian_fetched(void* data, uint32_t size, const char* name);

/* Host-order copy of a big-endian s16 oscillator table in sequence data
 * (cached by content; decomp-patches/endian-10). */
int16_t* port_seq_s16_osc_table(const void* be_table);

/* Name of a PortEndianFormat, for logs. */
const char* port_endian_format_name(int fmt);

#ifdef __cplusplus
}
#endif

#endif
