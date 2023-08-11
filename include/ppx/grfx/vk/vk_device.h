// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ppx_grfx_vk_device_h
#define ppx_grfx_vk_device_h

#include "ppx/grfx/vk/vk_config.h"
#include "ppx/grfx/grfx_device.h"

namespace ppx {
namespace grfx {
namespace vk {

struct VrsConfigs
// TODO: zzong, move to grfx_device once dx VRS is supported
{
    bool     enable_pipeline_vrs   = false;
    bool     enable_primitive_vrs  = false;
    bool     enable_attachment_vrs = false;
    uint32_t texel_width           = 0;
    uint32_t texel_height          = 0;

    std::vector<std::vector<uint32_t>> supported_shading_rates;
    uint32_t                           GetTexelValueOfRate(VkExtent2D rate)
    {
        // Use 1x1 (texel value 0) when requested rate is not supported
        return supported_shading_rates[rate.width >> 1][rate.height >> 1];
    };
    void UpdateSupportedrates(std::vector<VkPhysicalDeviceFragmentShadingRateKHR> fragmentShadingRates)
    {
        supported_shading_rates.resize(3, std::vector<uint32_t>(3, 0)); // Set all with 1x1 rate (texel value 0)
        for (const auto& rate : fragmentShadingRates) {
            uint32_t w                              = rate.fragmentSize.width;
            uint32_t h                              = rate.fragmentSize.height;
            supported_shading_rates[w >> 1][h >> 1] = ((w >> 1) << 2) + (h >> 1);
        }
    };
};

class Device
    : public grfx::Device
{
public:
    Device() {}
    virtual ~Device() {}

    VkDevicePtr     GetVkDevice() const { return mDevice; }
    VmaAllocatorPtr GetVmaAllocator() const { return mVmaAllocator; }

    const VkPhysicalDeviceFeatures& GetDeviceFeatures() const { return mDeviceFeatures; }

    bool HasTimelineSemaphore() const { return mHasTimelineSemaphore; }
    bool HasExtendedDynamicState() const { return mHasExtendedDynamicState; }
    bool HasUnreistrictedDepthRange() const { return mHasUnrestrictedDepthRange; }

    virtual Result WaitIdle() override;

    virtual bool PipelineStatsAvailable() const override;
    virtual bool DynamicRenderingSupported() const override;
    virtual bool IndependentBlendingSupported() const override;
    virtual bool FragmentStoresAndAtomicsSupported() const override;

    void ResetQueryPoolEXT(
        VkQueryPool queryPool,
        uint32_t    firstQuery,
        uint32_t    queryCount) const;

    uint32_t                GetGraphicsQueueFamilyIndex() const { return mGraphicsQueueFamilyIndex; }
    uint32_t                GetComputeQueueFamilyIndex() const { return mComputeQueueFamilyIndex; }
    uint32_t                GetTransferQueueFamilyIndex() const { return mTransferQueueFamilyIndex; }
    std::array<uint32_t, 3> GetAllQueueFamilyIndices() const;

    uint32_t GetMaxPushDescriptors() const { return mMaxPushDescriptors; }

protected:
    virtual Result AllocateObject(grfx::Buffer** ppObject) override;
    virtual Result AllocateObject(grfx::CommandBuffer** ppObject) override;
    virtual Result AllocateObject(grfx::CommandPool** ppObject) override;
    virtual Result AllocateObject(grfx::ComputePipeline** ppObject) override;
    virtual Result AllocateObject(grfx::DepthStencilView** ppObject) override;
    virtual Result AllocateObject(grfx::DescriptorPool** ppObject) override;
    virtual Result AllocateObject(grfx::DescriptorSet** ppObject) override;
    virtual Result AllocateObject(grfx::DescriptorSetLayout** ppObject) override;
    virtual Result AllocateObject(grfx::Fence** ppObject) override;
    virtual Result AllocateObject(grfx::GraphicsPipeline** ppObject) override;
    virtual Result AllocateObject(grfx::Image** ppObject) override;
    virtual Result AllocateObject(grfx::PipelineInterface** ppObject) override;
    virtual Result AllocateObject(grfx::Queue** ppObject) override;
    virtual Result AllocateObject(grfx::Query** ppObject) override;
    virtual Result AllocateObject(grfx::RenderPass** ppObject) override;
    virtual Result AllocateObject(grfx::RenderTargetView** ppObject) override;
    virtual Result AllocateObject(grfx::SampledImageView** ppObject) override;
    virtual Result AllocateObject(grfx::Sampler** ppObject) override;
    virtual Result AllocateObject(grfx::Semaphore** ppObject) override;
    virtual Result AllocateObject(grfx::ShaderModule** ppObject) override;
    virtual Result AllocateObject(grfx::ShaderProgram** ppObject) override;
    virtual Result AllocateObject(grfx::StorageImageView** ppObject) override;
    virtual Result AllocateObject(grfx::Swapchain** ppObject) override;

protected:
    virtual Result CreateApiObjects(const grfx::DeviceCreateInfo* pCreateInfo) override;
    virtual void   DestroyApiObjects() override;

private:
    Result ConfigureQueueInfo(const grfx::DeviceCreateInfo* pCreateInfo, std::vector<float>& queuePriorities, std::vector<VkDeviceQueueCreateInfo>& queueCreateInfos);
    Result ConfigureExtensions(const grfx::DeviceCreateInfo* pCreateInfo);
    Result ConfigureFeatures(const grfx::DeviceCreateInfo* pCreateInfo, VkPhysicalDeviceFeatures& features);
    Result ConfigureVrsProperties(const grfx::DeviceCreateInfo* pCreateInfo, VrsConfigs& vrsConfigs);
    Result CreateQueues(const grfx::DeviceCreateInfo* pCreateInfo);

private:
    std::vector<std::string> mFoundExtensions;
    std::vector<std::string> mExtensions;
    VkDevicePtr              mDevice;
    VkPhysicalDeviceFeatures mDeviceFeatures = {};
    VmaAllocatorPtr          mVmaAllocator;
    bool                     mHasTimelineSemaphore      = false;
    bool                     mHasExtendedDynamicState   = false;
    bool                     mHasUnrestrictedDepthRange = false;
    bool                     mHasDynamicRendering       = false;
    PFN_vkResetQueryPoolEXT  mFnResetQueryPoolEXT       = nullptr;
    uint32_t                 mGraphicsQueueFamilyIndex  = 0;
    uint32_t                 mComputeQueueFamilyIndex   = 0;
    uint32_t                 mTransferQueueFamilyIndex  = 0;
    uint32_t                 mMaxPushDescriptors        = 0;
    VrsConfigs               mVrsConfigs;
};

extern PFN_vkCmdPushDescriptorSetKHR CmdPushDescriptorSetKHR;

} // namespace vk
} // namespace grfx
} // namespace ppx

#endif // ppx_grfx_vk_device_h
