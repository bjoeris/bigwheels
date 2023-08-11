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

#include "ppx/grfx/vk/vk_gpu.h"

namespace ppx {
namespace grfx {
namespace vk {

namespace {
uint32_t GetQueueFamilyIndexForMask(const std::vector<VkQueueFamilyProperties>& queueFamilies, const uint32_t mask)
{
    const uint32_t count = CountU32(queueFamilies);
    for (uint32_t i = 0; i < count; ++i) {
        if ((queueFamilies[i].queueFlags & kAllQueueMask) == mask) {
            return i;
        }
    }
    return PPX_VALUE_IGNORED;
}

template <size_t Size>
uint32_t GetQueueFamilyIndexByPreferences(const std::vector<VkQueueFamilyProperties>& queueFamilies, const std::array<uint32_t, Size>& masks)
{
    for (uint32_t mask : masks) {
        uint32_t index = GetQueueFamilyIndexForMask(queueFamilies, mask);
        if (index != PPX_VALUE_IGNORED) {
            return index;
        }
    }
    return PPX_VALUE_IGNORED;
}
} // namespace

Result Gpu::CreateApiObjects(const grfx::internal::GpuCreateInfo* pCreateInfo)
{
    if (IsNull(pCreateInfo->pApiObject)) {
        return ppx::ERROR_UNEXPECTED_NULL_ARGUMENT;
    }

    mGpu = static_cast<VkPhysicalDevice>(pCreateInfo->pApiObject);

    VkPhysicalDeviceFragmentDensityMapPropertiesEXT densityMapProperties;
    densityMapProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_PROPERTIES_EXT;
    densityMapProperties.pNext = &mVrsProperties;

    mGpuProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    mGpuProperties.pNext = &densityMapProperties;
    vkGetPhysicalDeviceProperties2(mGpu, &mGpuProperties);

    PPX_LOG_INFO("[zzong] densityMapProperties.fragmentDensityInvocations: " << densityMapProperties.fragmentDensityInvocations);
    PPX_LOG_INFO("[zzong] densityMapProperties.maxFragmentDensityTexelSize: " << densityMapProperties.maxFragmentDensityTexelSize.width << ", " << densityMapProperties.maxFragmentDensityTexelSize.height);
    PPX_LOG_INFO("[zzong] densityMapProperties.minFragmentDensityTexelSize: " << densityMapProperties.minFragmentDensityTexelSize.width << ", " << densityMapProperties.minFragmentDensityTexelSize.height);

    VkPhysicalDeviceFragmentDensityMapFeaturesEXT densityMapFeatures;
    densityMapFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT;
    densityMapFeatures.pNext = nullptr; // zzong todo VkPhysicalDeviceFragmentDensityMap2FeaturesEXT

    mGpuFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    mGpuFeatures.pNext = &densityMapFeatures;
    vkGetPhysicalDeviceFeatures2(mGpu, &mGpuFeatures);

    mFoveationCapabilities.densityMap.supported = densityMapFeatures.fragmentDensityMap == VK_TRUE;
    PPX_LOG_INFO("[zzong] vk:  mFoveationCapabilities.densityMap.supported: " << mFoveationCapabilities.densityMap.supported);
    mFoveationCapabilities.densityMap.supportsDynamicImageView      = densityMapFeatures.fragmentDensityMapDynamic == VK_TRUE;
    mFoveationCapabilities.densityMap.supportsAdditionalInvocations = densityMapProperties.fragmentDensityInvocations == VK_TRUE;
    mFoveationCapabilities.densityMap.supportsNonSubsampledImages   = densityMapFeatures.fragmentDensityMapNonSubsampledImages == VK_TRUE;
    mFoveationCapabilities.densityMap.texelSize.min.width           = densityMapProperties.minFragmentDensityTexelSize.width;
    mFoveationCapabilities.densityMap.texelSize.min.height          = densityMapProperties.minFragmentDensityTexelSize.height;
    mFoveationCapabilities.densityMap.texelSize.max.width           = densityMapProperties.maxFragmentDensityTexelSize.width;
    mFoveationCapabilities.densityMap.texelSize.max.height          = densityMapProperties.maxFragmentDensityTexelSize.height;

    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(mGpu, &count, nullptr);
    if (count > 0) {
        mQueueFamilies.resize(count);
        vkGetPhysicalDeviceQueueFamilyProperties(mGpu, &count, mQueueFamilies.data());
    }

    mDeviceName     = mGpuProperties.properties.deviceName;
    mDeviceVendorId = static_cast<grfx::VendorId>(mGpuProperties.properties.deviceID);

    return ppx::SUCCESS;
}

void Gpu::DestroyApiObjects()
{
    if (mGpu) {
        mGpu.Reset();
    }
}

float Gpu::GetTimestampPeriod() const
{
    return mGpuProperties.properties.limits.timestampPeriod;
}

uint32_t Gpu::GetQueueFamilyCount() const
{
    uint32_t count = CountU32(mQueueFamilies);
    return count;
}

uint32_t Gpu::GetGraphicsQueueFamilyIndex() const
{
    constexpr std::array<uint32_t, 4> masks = {
        VK_QUEUE_GRAPHICS_BIT,
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT,
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT};
    return GetQueueFamilyIndexByPreferences(mQueueFamilies, masks);
}

uint32_t Gpu::GetComputeQueueFamilyIndex() const
{
    constexpr std::array<uint32_t, 4> masks = {
        VK_QUEUE_COMPUTE_BIT,
        VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT,
        VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT,
        VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT};
    return GetQueueFamilyIndexByPreferences(mQueueFamilies, masks);
}

uint32_t Gpu::GetTransferQueueFamilyIndex() const
{
    constexpr std::array<uint32_t, 4> masks = {
        VK_QUEUE_TRANSFER_BIT,
        VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT,
        VK_QUEUE_TRANSFER_BIT | VK_QUEUE_GRAPHICS_BIT,
        VK_QUEUE_TRANSFER_BIT | VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT};
    return GetQueueFamilyIndexByPreferences(mQueueFamilies, masks);
}

uint32_t Gpu::GetGraphicsQueueCount() const
{
    uint32_t count = 0;
    uint32_t index = GetGraphicsQueueFamilyIndex();
    if (index != PPX_VALUE_IGNORED) {
        count = mQueueFamilies[index].queueCount;
    }
    return count;
}

uint32_t Gpu::GetComputeQueueCount() const
{
    uint32_t count = 0;
    uint32_t index = GetComputeQueueFamilyIndex();
    if (index != PPX_VALUE_IGNORED) {
        count = mQueueFamilies[index].queueCount;
    }
    return count;
}

uint32_t Gpu::GetTransferQueueCount() const
{
    uint32_t count = 0;
    uint32_t index = GetTransferQueueFamilyIndex();
    if (index != PPX_VALUE_IGNORED) {
        count = mQueueFamilies[index].queueCount;
    }
    return count;
}

} // namespace vk
} // namespace grfx
} // namespace ppx
