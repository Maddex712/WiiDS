// Deterministic GL-thread queue/fence/PBO mock. Actual production source is injected below.
using uint8_t = unsigned char;
using u8 = unsigned char;
using uint32_t = unsigned int;
using int32_t = int;
using GLuint = unsigned int;
using GLenum = unsigned int;
using GLint = int;
using GLboolean = unsigned char;
using GLsizeiptr = long long;
using GLsync = struct Fence*;
extern "C" void* memcpy(void* out, const void* in, unsigned long long n) {
    auto a = (uint8_t*)out; auto b = (const uint8_t*)in;
    for (unsigned long long i = 0; i < n; ++i) a[i] = b[i]; return out;
}
extern "C" void* memset(void* out, int v, unsigned long long n) {
    auto a = (uint8_t*)out; for (unsigned long long i = 0; i < n; ++i) a[i] = (uint8_t)v; return out;
}
enum { GL_ALREADY_SIGNALED = 1, GL_CONDITION_SATISFIED, GL_WAIT_FAILED, GL_TIMEOUT_EXPIRED,
 GL_PIXEL_PACK_BUFFER, GL_MAP_READ_BIT, GL_TEXTURE_2D, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE,
 GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_STREAM_READ, GL_SCISSOR_TEST,
 GL_READ_FRAMEBUFFER, GL_DRAW_FRAMEBUFFER, GL_COLOR_BUFFER_BIT, GL_LINEAR, GL_PACK_ALIGNMENT,
 GL_SYNC_GPU_COMMANDS_COMPLETE, GL_TEXTURE0 = 1000, INT32_MAX = 2147483647, EFB_H = 528 };
/* CACHE_STRUCT */
static bool s_ready = true;
static int s_efbW = 640;
static GLuint s_efbFbo = 77;
static unsigned posts, maps, reads, pboAllocations, deletedBuffers, waits, invalidTimeout;
static GLuint pack, frameBuffer, nextName = 1;
static uint8_t buffers[64][320 * 180 * 4];
static uint8_t output[320 * 180 * 4];
struct Fence { GLenum status; };
static Fence fences[128]; static unsigned fenceAt;
static bool mapFails, unmapFails;
static int scaled(int x) { return x * 2; }
static void flushBatch() {}
static void glcInvalidate();
static GLuint efbReadFbo(int, int, int, int, int) { return 42; }
namespace glt {
struct Command { void (*fn)(void*); alignas(16) uint8_t data[128]; };
static Command commands[64];
static unsigned write, read;
static bool onGl;
static bool hasQueued() { return read != write; }
template <class F> static void invoke(void* p) { (*reinterpret_cast<F*>(p))(); }
template <class F> static void post(const F& f) {
    if (onGl) { f(); return; }
    ++posts; auto& c = commands[write++ % 64]; memcpy(c.data, &f, sizeof(F)); c.fn = &invoke<F>;
}
static void drain() { onGl = true; while (hasQueued()) { auto& c = commands[read++ % 64]; c.fn(c.data); } onGl = false; }
template <class F> static void sync(const F& f) { drain(); onGl = true; f(); onGl = false; }
}
static GLuint textures[8], samplers[8]; static unsigned activeTexture;
static void glActiveTexture(GLenum unit) {
    if (!glt::onGl) { glt::post([=] { glActiveTexture(unit); }); return; }
    activeTexture = unit - GL_TEXTURE0;
}
static void glBindSampler(GLuint unit, GLuint v) {
    if (!glt::onGl) { glt::post([=] { glBindSampler(unit, v); }); return; }
    samplers[unit] = v;
}
static void glDeleteSync(GLsync f) { f->status = GL_WAIT_FAILED; }
static void glDeleteBuffers(int n, const GLuint*) { deletedBuffers += n; }
static void glDeleteTextures(int, const GLuint*) {}
static void glDeleteFramebuffers(int, const GLuint*) {}
static GLenum glClientWaitSync(GLsync f, int flags, unsigned long long timeout) {
    ++waits; if (flags || timeout) ++invalidTimeout; return f->status;
}
static void glBindBuffer(GLenum, GLuint v) { pack = v; }
static const void* glMapBufferRange(GLenum, int, int, int) { ++maps; return mapFails ? nullptr : buffers[pack]; }
static bool glUnmapBuffer(GLenum) { return !unmapFails; }
static void glGenTextures(int, GLuint* v) { *v = nextName++; }
static void glBindTexture(GLenum target, GLuint v) {
    if (!glt::onGl) { glt::post([=] { glBindTexture(target, v); }); return; }
    textures[activeTexture] = v;
}
static void glTexImage2D(GLenum, int, int, int, int, int, int, int, const void*) {}
static void glGenFramebuffers(int, GLuint* v) { *v = nextName++; }
static void glBindFramebuffer(GLenum target, GLuint v) { if (target == GL_FRAMEBUFFER) frameBuffer = v; }
static void glFramebufferTexture2D(int, int, int, GLuint, int) {}
static void glGenBuffers(int, GLuint* v) { *v = nextName++; ++pboAllocations; }
static void glBufferData(int, int, const void*, int) {}
static void glDisable(int) {}
static void glBlitFramebuffer(int, int, int, int, int, int, int, int, int, int) {}
static void glPixelStorei(int, int) {}
static void glReadPixels(int, int, int w, int h, int, int, void* direct) {
    ++reads; if (direct || !pack || w != 320 || h != 180) invalidTimeout++;
    memset(buffers[pack], (int)reads, 320 * 180 * 4);
}
static GLsync glFenceSync(int, int) { auto f = &fences[fenceAt++ % 128]; f->status = GL_TIMEOUT_EXPIRED; return f; }
/* CACHE_SOURCE */
extern "C" {
/* ACTUAL_SOURCE */
}
struct FILE {};
static char* getenv(const char*) { return nullptr; }
static FILE* fopen(const char*, const char*) { return nullptr; }
static int fwrite(const void*, int, int, FILE*) { return 0; }
static int fclose(FILE*) { return 0; }
/* SNAPSHOT_SOURCE */
static void readyAll(GLenum status = GL_ALREADY_SIGNALED) {
    for (int i = 0; i < 3; ++i) if (s_asyncSky.fence[i]) s_asyncSky.fence[i]->status = status;
}
#define CHECK(x, n) do { if (!(x)) return n; } while (0)
extern "C" __declspec(dllexport) int run_tests() {
    CHECK(!GXPC_CaptureSkyEFB(nullptr, 320, 180), 1);
    CHECK(!GXPC_CaptureSkyEFB(output, 640, 360) && posts == 0, 2);
    s_ready = false; CHECK(!GXPC_CaptureSkyEFB(output, 320, 180) && posts == 0, 3); s_ready = true;
    CHECK(!GXPC_CaptureSkyEFB(output, 320, 180) && posts == 1 && reads == 0, 4);
    CHECK(!GXPC_CaptureSkyEFB(output, 320, 180) && posts == 1, 5);
    glt::drain(); CHECK(reads == 1 && maps == 0 && pack == 0 && frameBuffer == 77, 6);
    GXPC_CaptureSkyEFB(output, 320, 180); glt::drain();
    GXPC_CaptureSkyEFB(output, 320, 180); glt::drain();
    CHECK(reads == 3 && pboAllocations == 3 && maps == 0, 7);
    GXPC_CaptureSkyEFB(output, 320, 180); glt::drain();
    CHECK(reads == 3 && pboAllocations == 3 && maps == 0, 8); // full ring skips, never waits
    readyAll(); GXPC_CaptureSkyEFB(output, 320, 180); glt::drain();
    CHECK(maps == 1 && reads == 4, 9);
    CHECK(GXPC_CaptureSkyEFB(output, 320, 180) == 1, 10);
    CHECK(output[0] == 3 && output[1000] == 3 && output[320 * 180 * 4 - 1] == 3, 11);
    CHECK(GXPC_CaptureSkyEFB(output, 320, 180) == 0, 12); // no repeated publication
    glt::drain(); readyAll(); mapFails = true;
    GXPC_CaptureSkyEFB(output, 320, 180); glt::drain();
    CHECK(s_asyncSky.sequence == 1 && s_asyncSky.pixels[0] == 3, 13);
    mapFails = false; readyAll(); unmapFails = true;
    GXPC_CaptureSkyEFB(output, 320, 180); glt::drain();
    CHECK(s_asyncSky.sequence == 1 && s_asyncSky.pixels[0] == 3, 14);
    unmapFails = false; readyAll(GL_WAIT_FAILED); unsigned oldMaps = maps;
    GXPC_CaptureSkyEFB(output, 320, 180); glt::drain();
    CHECK(maps == oldMaps && s_asyncSky.sequence == 1, 15);
    readyAll(); s_asyncSky.pixelsLocked = 1;
    GXPC_CaptureSkyEFB(output, 320, 180); glt::drain();
    CHECK(maps == oldMaps && s_asyncSky.pixelsLocked == 1, 16);
    s_asyncSky.pixelsLocked = 0; readyAll();
    GXPC_CaptureSkyEFB(output, 320, 180); glt::drain();
    CHECK(maps == oldMaps + 1 && s_asyncSky.sequence == 2, 17);
    CHECK(!invalidTimeout && pack == 0 && frameBuffer == 77, 18);
    GXPC_CaptureSkyEFB(output, 320, 180); glt::drain();
    CHECK(pboAllocations == 3, 19);
    resetAsyncSkyCapture();
    CHECK(deletedBuffers == 3 && !s_asyncSky.sequence && !s_asyncSky.queued && !s_asyncSky.fbo, 20);
    CHECK(!GXPC_CaptureSkyEFB(output, 320, 180), 21); glt::drain();
    CHECK(reads > 4 && pboAllocations == 4, 22);
    resetAsyncSkyCapture();
    int copies = 0;
    for (int i = 0; i < 120; ++i) {
        readyAll(); copies += GXPC_CaptureSkyEFB(output, 320, 180); glt::drain();
    }
    CHECK(copies == 118, 23); // full-rate actual new frames after two-frame warm-up
    resetAsyncSkyCapture();
    unsigned priorReads = reads;
    port_thor_capture_sky(8); CHECK(reads == priorReads && !glt::hasQueued(), 24);
    for (int i = 0; i < 120; ++i) {
        readyAll(); port_thor_capture_sky(10); glt::drain();
    }
    CHECK(g_skySeq == 118, 25); // the old every-four-frame source produced only 30 calls
    CHECK(kSkyBytes == 320 * 180 * 4 && kPhotoBytes == 640 * 360 * 4, 26);
    CHECK(!port_thor_sky(output, kSkyBytes - 1, 0), 27);
    CHECK(port_thor_sky(output, kSkyBytes, 0) == 118 && output[3] == 255, 28);
    CHECK(!port_thor_sky(output, kSkyBytes, 118), 29);
    resetAsyncSkyCapture();
    // Actual cache code queues the logo's rebind AFTER capture, even when it
    // uses the same texture/sampler as the last draw before capture.
    glcInvalidate(); glcBindTexture(0, 41); glcBindSampler(0, 9); glt::drain();
    CHECK(textures[0] == 41 && samplers[0] == 9, 30);
    GXPC_CaptureSkyEFB(output, 320, 180);
    glcBindTexture(0, 41); glcBindSampler(0, 9);
    glt::drain();
    CHECK(textures[0] == 41 && samplers[0] == 9 && pack == 0 && frameBuffer == 77, 31);
    return 0;
}
