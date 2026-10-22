// AYN Thor lower screen: the game's own space, live.
//
// The Super Mario Sunshine companion showed the game's own title sky by capturing the frame just
// before the 2D layer. Galaxy does the same through Vulkan, without ever waiting:
//  - the draw pass, when the lower screen is asking, blits the 3D picture (the matching region,
//    the lower screen's own shape) into a small image at the game's MARK_THOR_SPACE, copies it into
//    one of two persistently mapped host buffers, and resumes drawing, exactly like the game's EFB
//    copies;
//  - the next render already waits for the previous submission's fence (waitInFlightSubmissions),
//    so its buffer is complete then and is published into a double-buffered result;
//  - the Java lower screen copies the newest result (GalaxyActivity.nativeSpace).
// A second, tiny readback (the probe) measures how bright the finished upper picture is, so the
// lower screen goes black whenever the upper one does (fades, loading, logos), as in the Sunshine
// and NSMB companions.
// The game marks only the title's frames (GameScene::draw, just before the 2D layer, so the logo is
// excluded); the marker travels with the frame, so a camera cut is never taken. Elsewhere nothing
// new is taken and the last title picture stays. At most one capture every kIntervalNs, and none
// while the lower screen is not polling.
#include "gx/vk/vk_thor_capture.h"

#include "gx/vk/vk_common.h"

#include <atomic>
#include <string.h>

namespace {

constexpr int kW = 620, kH = 540;  // half the lower panel (1240x1080), its exact shape
constexpr VkDeviceSize kBytes = (VkDeviceSize)kW * kH * 4;
constexpr int64_t kIntervalNs = 30000000ll;  // ~30 pictures a second at most: the sky turns slowly

struct Slot {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    bool coherent = true;
};

struct Capture {
    VkDevice device = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    Slot slots[2];
    int next = 0;
    int pending = -1;  // slot recorded into the last submission
    bool failed = false;
};
Capture sCap;

// The probe: the whole finished picture shrunk to kPW x kPH at the end of each draw pass.
constexpr int kPW = 64, kPH = 36;
constexpr VkDeviceSize kPBytes = (VkDeviceSize)kPW * kPH * 4;
struct Probe {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    Slot slots[2];
    int next = 0;
    int pending = -1;
    bool failed = false;
};
Probe sProbe;
// The upper picture's bright level: the luma (0..255) its brightest 0.5% of samples reach, so one
// stray pixel does not count; -1 until measured.
std::atomic<int> sTopLevel{-1};

// Published pictures (render thread writes, UI thread reads): no torn reads, no waiting.
uint8_t sPublished[2][kW * kH * 4];
std::atomic<int> sLatest{-1};
std::atomic<int> sReading{-1};
std::atomic<uint32_t> sSeq{0};

std::atomic<int64_t> sLastAsk{0};
int64_t sLastCapture = 0;  // render thread only

bool createImage(gpu::VkRenderer& r, int w, int h, VkImage& image, VkDeviceMemory& memory) {
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {(uint32_t)w, (uint32_t)h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(r.mDevice, &ici, nullptr, &image) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(r.mDevice, image, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = r.findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    return vkAllocateMemory(r.mDevice, &ai, nullptr, &memory) == VK_SUCCESS &&
           vkBindImageMemory(r.mDevice, image, memory, 0) == VK_SUCCESS;
}

bool createReadback(gpu::VkRenderer& r, VkDeviceSize bytes, Slot& s) {
    // Host-cached when available (the CPU reads it); coherent either way.
    bool ok = r.createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                 VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                             s.buffer, s.memory);
    s.coherent = true;
    if (!ok) {
        ok = r.createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, s.buffer, s.memory);
    }
    return ok && vkMapMemory(r.mDevice, s.memory, 0, bytes, 0, &s.mapped) == VK_SUCCESS;
}

bool ensure(gpu::VkRenderer& r) {
    if (sCap.failed) return false;
    if (sCap.image != VK_NULL_HANDLE) return true;
    sCap.device = r.mDevice;
    if (!createImage(r, kW, kH, sCap.image, sCap.imageMemory) || !createReadback(r, kBytes, sCap.slots[0]) ||
        !createReadback(r, kBytes, sCap.slots[1])) {
        sCap.failed = true;
        return false;
    }
    LOGI("Thor lower screen: space capture ready (%dx%d)", kW, kH);
    return true;
}

bool ensureProbe(gpu::VkRenderer& r) {
    if (sProbe.failed) return false;
    if (sProbe.image != VK_NULL_HANDLE) return true;
    sCap.device = r.mDevice;
    if (!createImage(r, kPW, kPH, sProbe.image, sProbe.imageMemory) || !createReadback(r, kPBytes, sProbe.slots[0]) ||
        !createReadback(r, kPBytes, sProbe.slots[1])) {
        sProbe.failed = true;
        return false;
    }
    LOGI("Thor lower screen: brightness probe ready (%dx%d)", kPW, kPH);
    return true;
}

bool asking() {
    const int64_t asked = sLastAsk.load(std::memory_order_relaxed);
    return asked != 0 && port_host_time_ns() - asked <= 1500000000ll;
}

// The luma its brightest 0.5% of samples reach (RGBA rows).
int brightLevel(const uint8_t* px) {
    uint32_t histogram[256] = {};
    for (int i = 0; i < kPW * kPH; i++, px += 4) histogram[(px[0] * 54 + px[1] * 183 + px[2] * 19) >> 8]++;
    const uint32_t top = kPW * kPH / 200;
    uint32_t seen = 0;
    for (int level = 255; level > 0; level--) {
        seen += histogram[level];
        if (seen > top) return level;
    }
    return 0;
}

void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags srcAccess,
                  VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = srcAccess;
    b.dstAccessMask = dstAccess;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

}  // namespace

namespace gpu {

bool thorCaptureArmed(const VkRenderer& r) {
    if (sCap.failed || r.mCopySlot != 0) return false;  // flat rendering only, never a VR eye
    if (sCap.pending >= 0) return false;                 // one capture in flight at a time
    if (!asking()) return false;  // nobody is looking
    return port_host_time_ns() - sLastCapture >= kIntervalNs;
}

void thorProbeRecord(VkRenderer& r, VkCommandBuffer cmd, VkTarget& t) {
    if (sProbe.failed || r.mCopySlot != 0 || sProbe.pending >= 0 || !asking()) return;
    if (t.colorImage == VK_NULL_HANDLE || t.width <= 0 || t.height <= 0 || t.colorLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
        !ensureProbe(r)) {
        return;
    }
    const int slot = sProbe.next;
    // The draw pass's colour writes (and its move to SHADER_READ_ONLY) come first.
    imageBarrier(cmd, t.colorImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    imageBarrier(cmd, sProbe.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[1] = {t.width, t.height, 1};
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[1] = {kPW, kPH, 1};
    vkCmdBlitImage(cmd, t.colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sProbe.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   VK_FILTER_LINEAR);
    // Back to how the present pass samples it.
    imageBarrier(cmd, t.colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                 VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    imageBarrier(cmd, sProbe.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                 VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {(uint32_t)kPW, (uint32_t)kPH, 1};
    vkCmdCopyImageToBuffer(cmd, sProbe.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sProbe.slots[slot].buffer, 1, &region);
    VkBufferMemoryBarrier hostRead{};
    hostRead.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    hostRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hostRead.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    hostRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostRead.buffer = sProbe.slots[slot].buffer;
    hostRead.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &hostRead, 0, nullptr);
    sProbe.pending = slot;
    sProbe.next = slot ^ 1;
}

void thorCaptureRecord(VkRenderer& r, VkCommandBuffer cmd, VkTarget& t, bool& inRenderPass) {
    if (!ensure(r) || t.colorImage == VK_NULL_HANDLE || t.width <= 0 || t.height <= 0) return;
    const int slot = sCap.next;
    if (inRenderPass) {
        vkCmdEndRenderPass(cmd);
        inRenderPass = false;
        t.colorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    const bool fromAttachment = t.colorLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    imageBarrier(cmd, t.colorImage, t.colorLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 fromAttachment ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                 fromAttachment ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT);
    t.colorLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    imageBarrier(cmd, sCap.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    // The middle of the picture in the lower panel's shape (1240:1080), the full height.
    int cropW = (int)((float)t.height * kW / kH);
    int cropH = t.height;
    if (cropW > t.width) {
        cropW = t.width;
        cropH = (int)((float)t.width * kH / kW);
    }
    const int x0 = (t.width - cropW) / 2, y0 = (t.height - cropH) / 2;
    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[0] = {x0, y0, 0};
    blit.srcOffsets[1] = {x0 + cropW, y0 + cropH, 1};
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {kW, kH, 1};
    vkCmdBlitImage(cmd, t.colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sCap.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   VK_FILTER_LINEAR);

    imageBarrier(cmd, sCap.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                 VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {(uint32_t)kW, (uint32_t)kH, 1};
    vkCmdCopyImageToBuffer(cmd, sCap.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sCap.slots[slot].buffer, 1, &region);
    VkBufferMemoryBarrier hostRead{};
    hostRead.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    hostRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hostRead.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    hostRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostRead.buffer = sCap.slots[slot].buffer;
    hostRead.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &hostRead, 0, nullptr);

    // The game's picture goes back to being drawn into.
    imageBarrier(cmd, t.colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    t.colorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    sCap.pending = slot;
    sLastCapture = port_host_time_ns();
    sCap.next = slot ^ 1;
}

void thorCapturePublish(VkRenderer& r) {
    (void)r;
    const int probe = sProbe.pending;
    if (probe >= 0) {
        sProbe.pending = -1;
        if (sProbe.slots[probe].mapped != nullptr)
            sTopLevel.store(brightLevel(static_cast<const uint8_t*>(sProbe.slots[probe].mapped)), std::memory_order_relaxed);
    }
    const int slot = sCap.pending;
    if (slot < 0) return;
    sCap.pending = -1;
    const Slot& s = sCap.slots[slot];
    if (s.mapped == nullptr) return;
    // Both sides store then load (sLatest here, sReading in the copy): sequentially consistent, or a
    // torn picture is possible.
    const int latest = sLatest.load();
    const int target = latest == 0 ? 1 : 0;
    if (sReading.load() == target) return;  // the UI is copying it: skip, never wait
    uint8_t* out = sPublished[target];
    memcpy(out, s.mapped, (size_t)kBytes);
    // The EFB's alpha is the game's destination alpha, not coverage: the picture is opaque.
    for (size_t i = 3; i < (size_t)kBytes; i += 4) out[i] = 0xFF;
    sLatest.store(target);
    sSeq.fetch_add(1);
}

void thorCaptureDestroy(VkRenderer& r) {
    VkDevice device = sCap.device != VK_NULL_HANDLE ? sCap.device : r.mDevice;
    if (device == VK_NULL_HANDLE) {
        sCap = Capture{};
        return;
    }
    for (Slot& s : sCap.slots) {
        if (s.mapped) vkUnmapMemory(device, s.memory);
        if (s.buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, s.buffer, nullptr);
        if (s.memory != VK_NULL_HANDLE) vkFreeMemory(device, s.memory, nullptr);
    }
    if (sCap.image != VK_NULL_HANDLE) vkDestroyImage(device, sCap.image, nullptr);
    if (sCap.imageMemory != VK_NULL_HANDLE) vkFreeMemory(device, sCap.imageMemory, nullptr);
    for (Slot& s : sProbe.slots) {
        if (s.mapped) vkUnmapMemory(device, s.memory);
        if (s.buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, s.buffer, nullptr);
        if (s.memory != VK_NULL_HANDLE) vkFreeMemory(device, s.memory, nullptr);
    }
    if (sProbe.image != VK_NULL_HANDLE) vkDestroyImage(device, sProbe.image, nullptr);
    if (sProbe.imageMemory != VK_NULL_HANDLE) vkFreeMemory(device, sProbe.imageMemory, nullptr);
    sCap = Capture{};
    sProbe = Probe{};
}

}  // namespace gpu

// Whether the lower screen is up (it polled within the last 1.5 s).
extern "C" int port_thor_lower_visible(void) { return asking() ? 1 : 0; }

// The upper picture's bright level (0..255; -1 before the first measurement).
extern "C" int port_thor_top_level(void) { return sTopLevel.load(std::memory_order_relaxed); }

// The UI's copy (620x540 RGBA, rows top first): the newest picture when newer than `have`.
// Returns its sequence (0 when nothing new); *mode is 1, the title (the only picture taken).
// Each call also tells the renderer the lower screen is still looking.
extern "C" uint32_t port_thor_space_copy(uint8_t* out, int size, uint32_t have, int* mode) {
    sLastAsk.store(port_host_time_ns(), std::memory_order_relaxed);
    if (out == nullptr || size < (int)kBytes) return 0;
    const uint32_t seq = sSeq.load(std::memory_order_acquire);
    if (seq == 0 || seq == have) return 0;
    for (int tries = 0; tries < 4; tries++) {
        const int latest = sLatest.load();
        if (latest < 0) return 0;
        sReading.store(latest);
        if (sLatest.load() != latest) continue;
        memcpy(out, sPublished[latest], (size_t)kBytes);
        if (mode) *mode = 1;
        sReading.store(-1, std::memory_order_release);
        return sSeq.load(std::memory_order_acquire);
    }
    sReading.store(-1, std::memory_order_release);
    return 0;
}
