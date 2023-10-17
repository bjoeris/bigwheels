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

#include "ppx/grfx/vk/vk_render_pass.h"
#include "ppx/grfx/vk/vk_device.h"
#include "ppx/grfx/vk/vk_image.h"
#include "ppx/grfx/vk/vk_util.h"

#include "ppx/grfx/vk/vk_profiler_fn_wrapper.h"

namespace ppx {
namespace grfx {
namespace vk {

Result RenderPass::CreateRenderPass(const grfx::internal::RenderPassCreateInfo* pCreateInfo)
{
    bool                                   hasDepthSencil      = mDepthStencilView ? true : false;
    bool                                   foveationFDM        = !IsNull(pCreateInfo->pFoveationPattern) && pCreateInfo->pFoveationPattern->GetFoveationMode() == grfx::FOVEATION_DENSITY_MAP;
    bool                                   foveationVRS        = !IsNull(pCreateInfo->pFoveationPattern) && pCreateInfo->pFoveationPattern->GetFoveationMode() == grfx::FOVEATION_VRS;
    VkFragmentShadingRateAttachmentInfoKHR vrs_attachment_info = {};
    VkAttachmentDescription2               vrs_desc            = {};
    VkAttachmentReference2                 vrs_reference       = {};

    uint32_t      depthStencilAttachment = -1;
    uint32_t      foveationMapAttachment = -1;
    size_t        rtvCount               = CountU32(mRenderTargetViews);
    VkImageLayout depthStencillayout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Determine layout for depth/stencil
    {
        // These variables are not used for anything meaningful
        // in ToVkBarrierDst so they can be all zeroes.
        //
        VkPhysicalDeviceFeatures features   = {};
        VkPipelineStageFlags     stageMask  = 0;
        VkAccessFlags            accessMask = 0;

        Result ppxres = ToVkBarrierDst(pCreateInfo->depthStencilState, grfx::CommandType::COMMAND_TYPE_GRAPHICS, features, stageMask, accessMask, depthStencillayout);
        if (Failed(ppxres)) {
            PPX_ASSERT_MSG(false, "failed to determine layout for depth stencil state");
            return ppxres;
        }
    }

    // Attachment descriptions
    std::vector<VkAttachmentDescription2> attachmentDesc;
    {
        for (uint32_t i = 0; i < rtvCount; ++i) {
            grfx::RenderTargetViewPtr rtv = mRenderTargetViews[i];

            VkAttachmentDescription2 desc = {};
            desc.sType                    = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
            desc.flags                    = 0;
            desc.format                   = ToVkFormat(rtv->GetFormat());
            desc.samples                  = ToVkSampleCount(rtv->GetSampleCount());
            desc.loadOp                   = ToVkAttachmentLoadOp(rtv->GetLoadOp());
            desc.storeOp                  = ToVkAttachmentStoreOp(rtv->GetStoreOp());
            desc.stencilLoadOp            = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            desc.stencilStoreOp           = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            desc.initialLayout            = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            desc.finalLayout              = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            attachmentDesc.push_back(desc);
        }

        if (hasDepthSencil) {
            grfx::DepthStencilViewPtr dsv = mDepthStencilView;

            VkAttachmentDescription2 desc = {};
            desc.sType                    = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
            desc.flags                    = 0;
            desc.format                   = ToVkFormat(dsv->GetFormat());
            desc.samples                  = VK_SAMPLE_COUNT_1_BIT;
            desc.loadOp                   = ToVkAttachmentLoadOp(dsv->GetDepthLoadOp());
            desc.storeOp                  = ToVkAttachmentStoreOp(dsv->GetDepthStoreOp());
            desc.stencilLoadOp            = ToVkAttachmentLoadOp(dsv->GetStencilLoadOp());
            desc.stencilStoreOp           = ToVkAttachmentStoreOp(dsv->GetStencilStoreOp());
            desc.initialLayout            = depthStencillayout;
            desc.finalLayout              = depthStencillayout;

            depthStencilAttachment = attachmentDesc.size();
            attachmentDesc.push_back(desc);
        }
    }

    std::vector<VkAttachmentReference2> colorRefs;
    {
        for (uint32_t i = 0; i < rtvCount; ++i) {
            VkAttachmentReference2 ref = {};
            ref.sType                  = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
            ref.attachment             = i;
            ref.layout                 = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorRefs.push_back(ref);
        }
    }

    VkAttachmentReference2 depthStencilRef = {};
    depthStencilRef.sType                  = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    if (hasDepthSencil) {
        depthStencilRef.attachment = depthStencilAttachment;
        depthStencilRef.layout     = depthStencillayout;
    }

    // zzong. foveation attachment setup
    if (foveationFDM) {
        VkAttachmentDescription2 densityMapDesc = {.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2};
        densityMapDesc.flags                    = 0;
        densityMapDesc.format                   = VK_FORMAT_R8G8_UNORM;
        densityMapDesc.samples                  = VK_SAMPLE_COUNT_1_BIT;
        densityMapDesc.loadOp                   = VK_ATTACHMENT_LOAD_OP_LOAD; // fragmentDensityMapAttachment-02550
        densityMapDesc.initialLayout            = VK_IMAGE_LAYOUT_GENERAL;
        densityMapDesc.stencilLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD;
        densityMapDesc.storeOp                  = VK_ATTACHMENT_STORE_OP_DONT_CARE; // fragmentDensityMapAttachment-02551
        densityMapDesc.stencilStoreOp           = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        densityMapDesc.finalLayout              = VK_IMAGE_LAYOUT_GENERAL;
        // VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT; zzong: validation error
        foveationMapAttachment = attachmentDesc.size();
        attachmentDesc.push_back(densityMapDesc);
    }
    else if (foveationVRS) {
        grfx::FoveationCapabilities foveationCapabilities = ToApi(GetDevice())->GetFoveationCapabilities();

        vrs_desc.sType          = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
        vrs_desc.pNext          = nullptr;
        vrs_desc.flags          = 0;
        vrs_desc.format         = VK_FORMAT_R8_UINT;
        vrs_desc.samples        = VK_SAMPLE_COUNT_1_BIT;
        vrs_desc.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
        vrs_desc.initialLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vrs_desc.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_LOAD;
        vrs_desc.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        vrs_desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        vrs_desc.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        attachmentDesc.push_back(vrs_desc);

        vrs_reference.sType      = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2_KHR;
        vrs_reference.pNext      = nullptr;
        vrs_reference.attachment = attachmentDesc.size() - 1;
        vrs_reference.layout     = VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
        vrs_reference.aspectMask = VK_IMAGE_ASPECT_NONE_KHR;

        vrs_attachment_info.sType                          = VK_STRUCTURE_TYPE_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR;
        vrs_attachment_info.pNext                          = nullptr;
        vrs_attachment_info.pFragmentShadingRateAttachment = &vrs_reference;
        vrs_attachment_info.shadingRateAttachmentTexelSize = {
            .width  = foveationCapabilities.vrs.minTexelSize.x,
            .height = foveationCapabilities.vrs.minTexelSize.y,
        }; // [VRS] todo, get from vrs capability
    }

    VkSubpassDescription2 subpassDescription   = {.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2};
    subpassDescription.flags                   = 0;
    subpassDescription.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassDescription.inputAttachmentCount    = 0;
    subpassDescription.pInputAttachments       = nullptr;
    subpassDescription.colorAttachmentCount    = CountU32(colorRefs);
    subpassDescription.pColorAttachments       = DataPtr(colorRefs);
    subpassDescription.pResolveAttachments     = nullptr;
    subpassDescription.pDepthStencilAttachment = hasDepthSencil ? &depthStencilRef : nullptr;
    subpassDescription.preserveAttachmentCount = 0;
    subpassDescription.pPreserveAttachments    = nullptr;
    if (foveationVRS) {
        subpassDescription.pNext = &vrs_attachment_info;
    }

    VkSubpassDependency2 subpassDependencies = {.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2};
    subpassDependencies.srcSubpass           = VK_SUBPASS_EXTERNAL;
    subpassDependencies.dstSubpass           = 0;
    subpassDependencies.srcStageMask         = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    subpassDependencies.dstStageMask         = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependencies.srcAccessMask        = 0;
    subpassDependencies.dstAccessMask        = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    subpassDependencies.dependencyFlags      = 0;

    VkRenderPassCreateInfo2 vkci = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2};
    vkci.flags                   = 0;
    vkci.attachmentCount         = CountU32(attachmentDesc);
    vkci.pAttachments            = DataPtr(attachmentDesc);
    vkci.subpassCount            = 1;
    vkci.pSubpasses              = &subpassDescription;
    vkci.dependencyCount         = 1;
    vkci.pDependencies           = &subpassDependencies;

    VkAttachmentReference                       densityMapReference = {};
    VkRenderPassFragmentDensityMapCreateInfoEXT density_map_info    = {};
    if (foveationFDM) {
        densityMapReference.attachment = foveationMapAttachment;
        densityMapReference.layout     = VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT; // fragmentDensityMapAttachment-02549

        density_map_info.sType                        = VK_STRUCTURE_TYPE_RENDER_PASS_FRAGMENT_DENSITY_MAP_CREATE_INFO_EXT;
        density_map_info.fragmentDensityMapAttachment = densityMapReference;
        density_map_info.pNext                        = nullptr;

        vkci.pNext = &density_map_info;
    }

    VkResult vkres = vk::CreateRenderPass(
        ToApi(GetDevice())->GetVkDevice(),
        &vkci,
        nullptr,
        &mRenderPass);
    if (vkres != VK_SUCCESS) {
        PPX_ASSERT_MSG(false, "vkCreateRenderPass failed: " << ToString(vkres));
        return ppx::ERROR_API_FAILURE;
    }

    return ppx::SUCCESS;
}

Result RenderPass::CreateFramebuffer(const grfx::internal::RenderPassCreateInfo* pCreateInfo)
{
    bool hasDepthSencil = mDepthStencilView ? true : false;
    bool foveationFDM   = !IsNull(pCreateInfo->pFoveationPattern) && pCreateInfo->pFoveationPattern->GetFoveationMode() == grfx::FOVEATION_DENSITY_MAP;
    bool foveationVRS   = !IsNull(pCreateInfo->pFoveationPattern) && pCreateInfo->pFoveationPattern->GetFoveationMode() == grfx::FOVEATION_VRS;

    size_t rtvCount = CountU32(mRenderTargetViews);

    std::vector<VkImageView> attachments;
    for (uint32_t i = 0; i < rtvCount; ++i) {
        grfx::RenderTargetViewPtr rtv = mRenderTargetViews[i];
        attachments.push_back(ToApi(rtv.Get())->GetVkImageView());
    }

    if (hasDepthSencil) {
        grfx::DepthStencilViewPtr dsv = mDepthStencilView;
        attachments.push_back(ToApi(dsv.Get())->GetVkImageView());
    }

    if (foveationFDM || foveationVRS) {
        attachments.push_back(ToApi(pCreateInfo->pFoveationPattern->GetFoveationImageViewPtr().Get())->GetVkImageView());
    }
    VkFramebufferCreateInfo vkci = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    vkci.flags                   = 0;
    vkci.renderPass              = mRenderPass;
    vkci.attachmentCount         = CountU32(attachments);
    vkci.pAttachments            = DataPtr(attachments);
    vkci.width                   = pCreateInfo->width;
    vkci.height                  = pCreateInfo->height;
    vkci.layers                  = 1;

    VkResult vkres = vkCreateFramebuffer(
        ToApi(GetDevice())->GetVkDevice(),
        &vkci,
        nullptr,
        &mFramebuffer);
    if (vkres != VK_SUCCESS) {
        PPX_ASSERT_MSG(false, "vkCreateFramebuffer failed: " << ToString(vkres));
        return ppx::ERROR_API_FAILURE;
    }

    return ppx::SUCCESS;
}

Result RenderPass::CreateApiObjects(const grfx::internal::RenderPassCreateInfo* pCreateInfo)
{
    Result ppxres = CreateRenderPass(pCreateInfo);
    if (Failed(ppxres)) {
        return ppxres;
    }

    ppxres = CreateFramebuffer(pCreateInfo);
    if (Failed(ppxres)) {
        return ppxres;
    }

    return ppx::SUCCESS;
}

void RenderPass::DestroyApiObjects()
{
    if (mFramebuffer) {
        vkDestroyFramebuffer(
            ToApi(GetDevice())->GetVkDevice(),
            mFramebuffer,
            nullptr);
        mFramebuffer.Reset();
    }

    if (mRenderPass) {
        vkDestroyRenderPass(
            ToApi(GetDevice())->GetVkDevice(),
            mRenderPass,
            nullptr);
        mRenderPass.Reset();
    }
}

// -------------------------------------------------------------------------------------------------

VkResult CreateTransientRenderPass(
    VkDevice              device,
    uint32_t              renderTargetCount,
    const VkFormat*       pRenderTargetFormats,
    VkFormat              depthStencilFormat,
    VkSampleCountFlagBits sampleCount,
    VkRenderPass*         pRenderPass,
    grfx::FoveationMode   foveationMode)
{
    bool                                   hasDepthSencil         = (depthStencilFormat != VK_FORMAT_UNDEFINED);
    bool                                   foveationFDM           = foveationMode == grfx::FOVEATION_DENSITY_MAP;
    bool                                   foveationVRS           = foveationMode == grfx::FOVEATION_VRS;
    VkAttachmentDescription2               vrs_desc               = {};
    VkAttachmentReference2                 vrs_reference          = {};
    VkFragmentShadingRateAttachmentInfoKHR vrs_attachment_info    = {};
    uint32_t                               depthStencilAttachment = -1;
    uint32_t                               foveationMapAttachment = -1;

    std::vector<VkAttachmentDescription2> attachmentDescs;
    {
        for (uint32_t i = 0; i < renderTargetCount; ++i) {
            VkAttachmentDescription2 desc = {.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2};
            desc.flags                    = 0;
            desc.format                   = pRenderTargetFormats[i];
            desc.samples                  = sampleCount;
            desc.loadOp                   = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            desc.finalLayout              = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachmentDescs.push_back(desc);
        }

        if (hasDepthSencil) {
            VkAttachmentDescription2 desc = {.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2};
            desc.flags                    = 0;
            desc.format                   = depthStencilFormat;
            desc.samples                  = VK_SAMPLE_COUNT_1_BIT;
            desc.loadOp                   = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            desc.stencilLoadOp            = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            desc.finalLayout              = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthStencilAttachment        = attachmentDescs.size();
            attachmentDescs.push_back(desc);
        }
    }

    std::vector<VkAttachmentReference2> colorRefs;
    {
        for (uint32_t i = 0; i < renderTargetCount; ++i) {
            VkAttachmentReference2 ref = {};
            ref.sType                  = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
            ref.attachment             = i;
            ref.layout                 = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorRefs.push_back(ref);
        }
    }

    VkAttachmentReference2 depthStencilRef = {};
    depthStencilRef.sType                  = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    if (hasDepthSencil) {
        depthStencilRef.attachment = depthStencilAttachment;
        depthStencilRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    if (foveationFDM) {
        VkAttachmentDescription2 densityMapDesc = {.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2};
        densityMapDesc.flags                    = 0;
        densityMapDesc.format                   = VK_FORMAT_R8G8_UNORM;
        densityMapDesc.samples                  = VK_SAMPLE_COUNT_1_BIT;
        densityMapDesc.loadOp                   = VK_ATTACHMENT_LOAD_OP_LOAD; // fragmentDensityMapAttachment-02550
        densityMapDesc.initialLayout            = VK_IMAGE_LAYOUT_GENERAL;
        densityMapDesc.stencilLoadOp            = VK_ATTACHMENT_LOAD_OP_LOAD;
        densityMapDesc.storeOp                  = VK_ATTACHMENT_STORE_OP_DONT_CARE; // fragmentDensityMapAttachment-02551
        densityMapDesc.stencilStoreOp           = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        densityMapDesc.finalLayout              = VK_IMAGE_LAYOUT_GENERAL; // VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT; zzong???? validation error
        foveationMapAttachment                  = attachmentDescs.size();
        attachmentDescs.push_back(densityMapDesc);
    }
    else if (foveationVRS) {
        // grfx::FoveationCapabilities foveationCapabilities =  ToApi(GetDevice())->GetFoveationCapabilities();// zzong. todo
        vrs_desc.sType          = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
        vrs_desc.pNext          = nullptr;
        vrs_desc.flags          = 0;
        vrs_desc.format         = VK_FORMAT_R8_UINT;
        vrs_desc.samples        = VK_SAMPLE_COUNT_1_BIT;
        vrs_desc.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
        vrs_desc.initialLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vrs_desc.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_LOAD;
        vrs_desc.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        vrs_desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        vrs_desc.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        attachmentDescs.push_back(vrs_desc);

        vrs_reference.sType      = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2_KHR;
        vrs_reference.pNext      = nullptr;
        vrs_reference.attachment = attachmentDescs.size() - 1;
        vrs_reference.layout     = VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
        vrs_reference.aspectMask = VK_IMAGE_ASPECT_NONE_KHR;

        vrs_attachment_info.sType                          = VK_STRUCTURE_TYPE_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR;
        vrs_attachment_info.pNext                          = nullptr;
        vrs_attachment_info.pFragmentShadingRateAttachment = &vrs_reference;
        vrs_attachment_info.shadingRateAttachmentTexelSize = {
            .width  = 8, // foveationCapabilities.vrs.minTexelSize.x, //zzong.todo
            .height = 8, // foveationCapabilities.vrs.minTexelSize.y, //zzong.todo
        };
    }

    VkSubpassDescription2 subpassDescription   = {.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2};
    subpassDescription.flags                   = 0;
    subpassDescription.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassDescription.inputAttachmentCount    = 0;
    subpassDescription.pInputAttachments       = nullptr;
    subpassDescription.colorAttachmentCount    = CountU32(colorRefs);
    subpassDescription.pColorAttachments       = DataPtr(colorRefs);
    subpassDescription.pResolveAttachments     = nullptr;
    subpassDescription.pDepthStencilAttachment = hasDepthSencil ? &depthStencilRef : nullptr;
    subpassDescription.preserveAttachmentCount = 0;
    subpassDescription.pPreserveAttachments    = nullptr;
    if (foveationVRS) {
        subpassDescription.pNext = &vrs_attachment_info;
    }

    VkSubpassDependency2 subpassDependencies = {.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2};
    subpassDependencies.srcSubpass           = VK_SUBPASS_EXTERNAL;
    subpassDependencies.dstSubpass           = 0;
    subpassDependencies.srcStageMask         = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    subpassDependencies.dstStageMask         = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependencies.srcAccessMask        = 0;
    subpassDependencies.dstAccessMask        = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    subpassDependencies.dependencyFlags      = 0;

    VkRenderPassCreateInfo2 vkci = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2};
    vkci.flags                   = 0;
    vkci.attachmentCount         = CountU32(attachmentDescs);
    vkci.pAttachments            = DataPtr(attachmentDescs);
    vkci.subpassCount            = 1;
    vkci.pSubpasses              = &subpassDescription;
    vkci.dependencyCount         = 1;
    vkci.pDependencies           = &subpassDependencies;

    VkRenderPassFragmentDensityMapCreateInfoEXT density_map_info    = {};
    VkAttachmentReference                       densityMapReference = {};
    if (foveationFDM) {
        densityMapReference.attachment = attachmentDescs.size() - 1;
        densityMapReference.layout     = VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT; // fragmentDensityMapAttachment-02549

        density_map_info.sType                        = VK_STRUCTURE_TYPE_RENDER_PASS_FRAGMENT_DENSITY_MAP_CREATE_INFO_EXT;
        density_map_info.fragmentDensityMapAttachment = densityMapReference;
        density_map_info.pNext                        = nullptr;
        vkci.pNext                                    = &density_map_info;
    }
    VkResult vkres = vk::CreateRenderPass(
        device,
        &vkci,
        nullptr,
        pRenderPass);
    if (vkres != VK_SUCCESS) {
        return vkres;
    }

    return VK_SUCCESS;
}

} // namespace vk
} // namespace grfx
} // namespace ppx
