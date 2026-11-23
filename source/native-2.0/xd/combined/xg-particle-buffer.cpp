#include <dolphin/types.h>
#include <game/pecontainer.h>
#include <game/gcdata.h>

extern "C" {
void* memset(void*, int, u32);
void* memcpy(void*, const void*, u32);
void DCFlushRange(void*, u32);
}

struct GSgfxLayer {
public:
    u16 tokenMark();
    void SetLayerVisibility(bool visible);
};

enum GSgfxLayerID { GS_LAYER_DUMMY };
enum GSgfxPrimType { GS_PRIM_DUMMY };
struct GSgfxVF;

struct GSgfxDL {
    u16 flags;
    u16 count;
    u8* data;
    u32 size;
#ifdef __MWERKS__
    u32 pad;
#else
    struct GSgfxVF* vf;
#endif

    bool valid() const { return (flags & 1) == 1; }
    u8* colorPtr() { return (flags & 1) ? data + 3 : 0; }
    u16 vertCount() { return (flags & 1) ? count : 0; }
};

extern "C" {
void GSgfxDLCreate(GSgfxDL* dl, GSgfxPrimType prim, GSgfxVF* vf, u16 count);
void GSgfxDLFree(GSgfxDL* dl);
void GSgfxTokenWaitForClear(u16 token);
GSgfxLayer* GSgfxLayerFindByID(GSgfxLayerID id);
u32 GSmemGetSize(void* p);
void GSmemFree(void* p);
void* GSmemAllocClr(u32 size);
void* GSmemAllocAlignClr(u32 size, u32 align);
}

extern "C" GSgfxLayer* g_current_layer;
extern u16 g_bank_token;

struct ParticleDebugStats {
    u32 pad0;
    u32 nodeMem;
    u32 dlMem;
    u32 bufMem;
    u32 pad10;
    u32 total;
    u32 pad18[3];
};
extern "C" ParticleDebugStats g_particle_debug_stats;
#ifndef __MWERKS__
extern "C" u32 pe_gc_node_size = 0;
#endif

struct peEmitParamsC {
    gc_be32 flags;
};
struct peEmitterNode {
    u8 pad0[0x20];
    void* slot20;
    u8 pad24[0x4];
    void* slot28;
    void* slot2C;
    u8 pad30[0xCC];
    peEmitParamsC* params;
};

struct peNode {
public:
    u8 pad0[0x20];
    void* slot20;
    u8 pad24[0x4];
    peNode* prev;
    peNode* next;
};

class peContainer {
public:
    PE_CONTAINER_MEMBERS

    peContainer(peEmitterNode* e, u32 nodeSize, u16 max);
    virtual ~peContainer();
    virtual void Start();
    virtual void PreUpdate();
    virtual void Emit(u16 n) = 0;
    virtual void Update(f32 dt) = 0;
    virtual void Render() = 0;
    virtual void Stop() = 0;

    void MoveNode(peNode* node, peNode* after);
    void RemoveAll();
    void RemoveNode(peNode* node);
    u16 CreateNodes(u16 n);
    static void SetLayerVisibility(GSgfxLayerID id, bool visible);
};

class peRenderContainer : public peContainer {
public:
    PE_RENDER_MEMBERS_DL(GSgfxDL dl[2])

    peRenderContainer(peEmitterNode* e, u32 nodeSize, u16 max, GSgfxVF* vf, GSgfxPrimType prim, u8 vertsPerNode,
                      u8 bytesPerVert, bool dbl);
    virtual ~peRenderContainer();
    virtual void Start() = 0;
    virtual void PreUpdate();
    void ResetSwap();
    void InitSwap();
    void ResolveMultipleSwap();
    void MarkForSwap();
};

peContainer::peContainer(peEmitterNode* e, u32 nodeSize, u16 max) {
    u8* e_c = (u8*)e;
    emitter = e;
    next = 0;
    buffer = 0;
    freeList = 0;
    activeList = 0;
    count = 0;
    maxCount = max;
    dirty = 0;
    started = 0;
    peNode* node;
    u32 hasPos = (emitter->params->flags >> 6) & 1;
    u32 total = max * nodeSize;
    if (hasPos) {
        total += max * 0x28;
    }
#ifndef __MWERKS__
    u32 gcNode = pe_gc_node_size ? pe_gc_node_size : nodeSize;
    pe_gc_node_size = 0;
#endif
    buffer = GSmemAllocClr(GC_SIZE(total, max * gcNode + (hasPos ? max * 0x28 : 0)));
    if (!buffer) {
        return;
    }
    u32 sz = GSmemGetSize(buffer);
    g_particle_debug_stats.nodeMem += sz;
    g_particle_debug_stats.total += sz;
    freeList = (peNode*)buffer;
    u8* cur = (u8*)freeList;
    peNode* prevNode = 0;
    for (u32 i = 0; i < max; i++) {
        node = (peNode*)cur;
        cur += nodeSize;
        if (hasPos) {
            PE_NODE_XFORM(node) = cur;
            cur += 0x28;
        } else {
#ifdef __MWERKS__
            PE_NODE_XFORM(node) = e_c + 0xa8;
#else
            PE_NODE_XFORM(node) = &PE_NODE_F(e_c, u8, 0xa8);
#endif
        }
        node->next = prevNode;
        if (prevNode) {
            prevNode->prev = node;
        }
        prevNode = node;
    }
    dirty = 1;
}

peContainer::~peContainer() {
    if (buffer) {
        u32 sz = GSmemGetSize(buffer);
        g_particle_debug_stats.nodeMem -= sz;
        g_particle_debug_stats.total -= sz;
        GSmemFree(buffer);
        buffer = 0;
    }
}

void peContainer::Start() {
    if (!started) {
        started = 1;
    }
}

void peContainer::PreUpdate() {}

void peContainer::SetLayerVisibility(GSgfxLayerID id, bool visible) {
    GSgfxLayer* layer = GSgfxLayerFindByID(id);
    if (layer) {
        layer->SetLayerVisibility(visible);
    }
}

u16 peContainer::CreateNodes(u16 n) {
    peNode* p;
    peNode* first = freeList;
    if (!first) {
        return 0;
    }
    peNode* last = first;
    u16 i = 1;
    for (; (p = last->prev) && i < n; i++) {
        last = p;
    }
    if (p) {
        p->next = 0;
    }
    freeList = last->prev;
    peNode* a = activeList;
    last->prev = a;
    if (a) {
        a->next = last;
    }
    first->next = 0;
    activeList = first;
    count += i;
    dirty = 1;
    return i;
}

void peContainer::RemoveNode(peNode* node) {
    peNode* p = node->prev;
    peNode* n = node->next;
    if (p) {
        p->next = n;
    }
    if (n) {
        n->prev = p;
    } else {
        activeList = p;
    }
    node->next = 0;
    peNode* f = freeList;
    node->prev = f;
    if (f) {
        f->next = node;
    }
    freeList = node;
    count--;
    dirty = 1;
}

void peContainer::RemoveAll() {
    peNode* tail = activeList;
    if (!tail) {
        return;
    }
    while (tail->prev) {
        tail = tail->prev;
    }
    peNode* f = freeList;
    tail->prev = f;
    if (f) {
        f->next = tail;
    }
    freeList = activeList;
    activeList = 0;
    count = 0;
    dirty = 1;
}

void peContainer::MoveNode(peNode* node, peNode* after) {
    peNode* p = node->prev;
    peNode* n = node->next;
    if (p) {
        p->next = n;
    }
    if (n) {
        n->prev = p;
    } else {
        activeList = p;
    }
    peNode* t = after->next;
    after->next = node;
    node->prev = after;
    node->next = t;
    if (t) {
        t->prev = node;
    } else {
        activeList = node;
    }
}

peRenderContainer::peRenderContainer(peEmitterNode* e, u32 nodeSize, u16 max, GSgfxVF* vf, GSgfxPrimType prim,
                                     u8 vertsPerNode, u8 bytesPerVert, bool dbl)
    : peContainer(e, nodeSize, max) {
    u32 aligned;
    swapPending = 0;
    u32 total = max * bytesPerVert;
    cur = 0;
    bufSize = 0;
    memset(&dl[0], 0, sizeof(dl[0]));
    memset(&dl[1], 0, sizeof(dl[1]));
    bufA = 0;
    bufB = 0;
    tokens[0] = 0;
    tokens[1] = 0;
    aligned = (total + 31) & ~31;
    u8* mem = (u8*)GSmemAllocAlignClr(dbl ? aligned * 2 : aligned, 0x20);
    if (!mem) {
        return;
    }
    u32 verts = max * vertsPerNode;
    GSgfxDLCreate(&dl[0], prim, vf, verts);
    if (!dl[0].valid()) {
        if (mem) {
            GSmemFree(mem);
        }
        return;
    }
    if (dbl) {
        GSgfxDLCreate(&dl[1], prim, vf, verts);
    }
    u32 sz = GSmemGetSize(mem);
    g_particle_debug_stats.dlMem += dl[0].size + dl[1].size;
    g_particle_debug_stats.bufMem += sz;
    g_particle_debug_stats.total = g_particle_debug_stats.total + (dl[0].size + dl[1].size) + sz;
    if (dl[1].valid()) {
        DCFlushRange(mem, aligned * 2);
#ifdef __ANDROID__
        u8* p = mem;
#else
        u8* p = mem + 0x40000000;
#endif
        bufA = p;
        bufB = p + aligned;
    } else {
        DCFlushRange(mem, total);
#ifdef __ANDROID__
        bufA = mem;
#else
        bufA = mem + 0x40000000;
#endif
    }
    bufSize = total;
    u8* color = dl[0].colorPtr();
    memset(color, 0xff, max * (vertsPerNode * dl[0].vertCount()));
    dl[0].flags |= 2;
    if (dl[1].valid()) {
        color = dl[1].colorPtr();
        memset(color, 0xff, max * (vertsPerNode * dl[1].vertCount()));
        dl[1].flags |= 2;
    }
}

peRenderContainer::~peRenderContainer() {
    if (dl[0].valid()) {
        u32 s = dl[0].size;
        g_particle_debug_stats.dlMem -= s;
        g_particle_debug_stats.total -= s;
        GSgfxDLFree(&dl[0]);
        if (dl[1].valid()) {
            u32 s2 = dl[1].size;
            g_particle_debug_stats.dlMem -= s2;
            g_particle_debug_stats.total -= s2;
            GSgfxDLFree(&dl[1]);
        }
        if (bufA) {
#ifndef __ANDROID__
            bufA = (u8*)((u32)bufA - 0x40000000);
#endif
            u32 sz = GSmemGetSize(bufA);
            g_particle_debug_stats.bufMem -= sz;
            g_particle_debug_stats.total -= sz;
            if (bufA) {
                GSmemFree(bufA);
                bufA = 0;
            }
        }
    }
}

void peRenderContainer::PreUpdate() {
    if (started) {
        if (dl[0].valid()) {
            if (tokens[cur]) {
                GSgfxTokenWaitForClear(tokens[cur]);
                tokens[cur] = 0;
            }
            if (dl[1].valid()) {
                if (swapPending) {
                    swapPending = 0;
                    if (dirty) {
                        memcpy(dl[cur].data, dl[cur == 0].data, dl[0].size);
                        GSgfxDL* d = &dl[cur];
                        d->flags |= 2;
                        dirty = 0;
                    }
                }
            }
        }
    }
}

void peRenderContainer::MarkForSwap() {
    if (dl[0].valid()) {
        g_bank_token = g_current_layer->tokenMark();
        tokens[cur] = g_bank_token;
        if (dl[1].valid()) {
            cur ^= 1;
            swapPending = 1;
        }
    }
}

void peRenderContainer::ResolveMultipleSwap() {
    if (dl[0].valid()) {
        if (dl[1].valid()) {
            if (swapPending) {
                cur ^= 1;
                swapPending = 0;
            }
        }
    }
}

void peRenderContainer::InitSwap() {
    if (dl[0].valid()) {
        tokens[0] = 0;
        dl[0].flags |= 2;
        if (dl[1].valid()) {
            tokens[1] = 0;
            dl[1].flags |= 2;
        }
    }
}

void peRenderContainer::ResetSwap() {}
