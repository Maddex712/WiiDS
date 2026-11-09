#include "gx/vk/vk_common.h"

namespace gpu {

VkSampler VkRenderer::getSampler(uint32_t mode0, uint32_t mode1, uint32_t levels) {
    uint64_t key = ((uint64_t)(mode0 & 0xFFFFFF) << 32) | ((uint64_t)(mode1 & 0xFFFF) << 8) | (levels & 0xFF);
    auto it = mSamplers.find(key);
    if (it != mSamplers.end()) return it->second;

    static const VkSamplerAddressMode wrap[4] = {
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT
    };

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.addressModeU = wrap[mode0 & 3];
    sci.addressModeV = wrap[(mode0 >> 2) & 3];
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.magFilter = ((mode0 >> 4) & 1) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

    // These are raw BP bits, not the differently numbered GXTexFilter API enum.
    // BP bit 7 selects spatial filtering; bits 5..6 select no/point/linear mips.
    uint32_t minf = (mode0 >> 5) & 7;
    uint32_t mipf = minf & 3;
    bool mips = levels > 1 && (mipf == 1 || mipf == 2);
    sci.minFilter = (minf & 4) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    // Reserved mip mode 3 safely uses the base level, without inventing a mip chain.
    sci.mipmapMode = mipf == 2 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    int bias = (int)((mode0 >> 9) & 0xFF);
    if (bias & 0x80) bias -= 0x100;
    const float biasLimit = mDeviceProperties.limits.maxSamplerLodBias;
    sci.mipLodBias = std::clamp(bias / 32.0f, -biasLimit, biasLimit);

    if (!mips) {
        sci.minLod = 0.0f;
        sci.maxLod = 0.0f;
    } else {
        sci.minLod = std::min((float)(levels - 1), (mode1 & 0xFF) / 16.0f);
        sci.maxLod = std::min((float)(levels - 1), ((mode1 >> 8) & 0xFF) / 16.0f);
        if (sci.maxLod < sci.minLod) sci.maxLod = sci.minLod;
    }

    VkSampler s = VK_NULL_HANDLE;
    VkResult res = vkCreateSampler(mDevice, &sci, nullptr, &s);
    if (res != VK_SUCCESS || s == VK_NULL_HANDLE) {
        LOGE("getSampler: vkCreateSampler failed (res=%d)", (int)res);
        return mDummySampler;
    }
    mSamplers[key] = s;
    return s;
}

}  // namespace gpu
