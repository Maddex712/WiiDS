#pragma once

// AYN Thor lower screen: the game's own space, captured from the 3D picture without blocking.
// See vk_thor_capture.cpp.

#include <vulkan/vulkan.h>

namespace gpu {

class VkRenderer;
struct VkTarget;

// Whether this frame's draw pass may capture at a MARK_THOR_SPACE (the lower screen is asking,
// none is in flight, and the last one is old enough).
bool thorCaptureArmed(const VkRenderer& renderer);
// Records the capture into the draw pass's command buffer (ends the render pass; the draw loop
// resumes it with the load pass, as after the game's own EFB copies).
void thorCaptureRecord(VkRenderer& renderer, VkCommandBuffer cmd, VkTarget& target, bool& inRenderPass);
// Records the brightness probe of the finished picture (2D and wipes included) at the end of the
// draw pass, while the lower screen is asking; the target stays ready for presenting.
void thorProbeRecord(VkRenderer& renderer, VkCommandBuffer cmd, VkTarget& target);
// Called after the previous submission's fence has signalled: publishes its capture and probe, if any.
void thorCapturePublish(VkRenderer& renderer);
// Releases the capture resources (device shutdown or loss).
void thorCaptureDestroy(VkRenderer& renderer);

}  // namespace gpu
