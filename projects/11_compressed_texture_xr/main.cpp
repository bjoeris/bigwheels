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

#include "ppx/ppx.h"
#include "ppx/graphics_util.h"
using namespace ppx;

#if defined(USE_DX12)
const grfx::Api kApi = grfx::API_DX_12_0;
#elif defined(USE_VK)
const grfx::Api kApi = grfx::API_VK_1_1;
#endif

struct ShapeDesc
{
    const char* texturePath;
    float3      homeLoc;
};

std::vector<ShapeDesc> textures = {
    ShapeDesc{"basic/textures/box_panel_bc1.dds", float3(-6, 2, 0)},
    ShapeDesc{"basic/textures/box_panel_bc2.dds", float3(-2, 2, 0)},
    ShapeDesc{"basic/textures/box_panel_bc3.dds", float3(2, 2, 0)},
    ShapeDesc{"basic/textures/box_panel_bc4.dds", float3(6, 2, 0)},
    ShapeDesc{"basic/textures/box_panel_bc5.dds", float3(-6, -2, 0)},
    ShapeDesc{"basic/textures/box_panel_bc6h.dds", float3(-2, -2, 0)},
    ShapeDesc{"basic/textures/box_panel_bc6h_sf.dds", float3(2, -2, 0)},
    ShapeDesc{"basic/textures/box_panel_bc7.dds", float3(6, -2, 0)},
};

class ProjApp
    : public ppx::Application
{
public:
    virtual void Config(ppx::ApplicationSettings& settings) override;
    virtual void Setup() override;
    virtual void Render() override;

private:
    struct PerFrame
    {
        ppx::grfx::CommandBufferPtr cmd;
        ppx::grfx::SemaphorePtr     imageAcquiredSemaphore;
        ppx::grfx::FencePtr         imageAcquiredFence;
        ppx::grfx::SemaphorePtr     renderCompleteSemaphore;
        ppx::grfx::FencePtr         renderCompleteFence;

        // XR UI per frame elements.
        ppx::grfx::CommandBufferPtr uiCmd;
        ppx::grfx::FencePtr         uiRenderCompleteFence;

        ppx::grfx::QueryPtr timestampQuery;
    };

    struct TexturedShape
    {
        int                            id;
        ppx::grfx::DescriptorSetPtr    descriptorSet;
        ppx::grfx::BufferPtr           uniformBuffer;
        ppx::grfx::ImagePtr            image;
        ppx::grfx::SamplerPtr          sampler;
        ppx::grfx::SampledImageViewPtr sampledImageView;
        float3                         homeLoc;
    };

    std::vector<PerFrame>             mPerFrame;
    ppx::grfx::ShaderModulePtr        mVS;
    ppx::grfx::ShaderModulePtr        mPS;
    ppx::grfx::PipelineInterfacePtr   mPipelineInterface;
    ppx::grfx::GraphicsPipelinePtr    mPipeline;
    ppx::grfx::BufferPtr              mVertexBuffer;
    ppx::grfx::DescriptorPoolPtr      mDescriptorPool;
    ppx::grfx::DescriptorSetLayoutPtr mDescriptorSetLayout;
    grfx::Viewport                    mViewport;
    grfx::Rect                        mScissorRect;
    grfx::VertexBinding               mVertexBinding;
    std::vector<TexturedShape>        mShapes;
    uint64_t                          mGpuWorkDuration = 0; // zzong. remove
    grfx::FullscreenQuadPtr           mDebugQuad;
};

void ProjApp::Config(ppx::ApplicationSettings& settings)
{
    settings.appName                    = "11_compressed_textures";
    settings.enableImGui                = true;
    settings.grfx.api                   = kApi;
    settings.grfx.swapchain.depthFormat = grfx::FORMAT_D32_FLOAT;
    settings.grfx.enableDebug           = false;
    settings.grfx.pacedFrameRate        = 0; // zzong?
    settings.xr.enable                  = true;
    settings.xr.enableDebugCapture      = false;
    settings.xr.ui.pos                  = {0.1f, -0.2f, -0.5f}; // zzong?
    settings.xr.ui.size                 = {1.f, 1.f};           // zzong?
}

void ProjApp::Setup()
{
    // Uniform buffer
    int id = 1;
    for (const auto& texture : textures) {
        TexturedShape shape = {};

        grfx::BufferCreateInfo bufferCreateInfo        = {};
        bufferCreateInfo.size                          = PPX_MINIMUM_UNIFORM_BUFFER_SIZE;
        bufferCreateInfo.usageFlags.bits.uniformBuffer = true;
        bufferCreateInfo.memoryUsage                   = grfx::MEMORY_USAGE_CPU_TO_GPU;

        PPX_CHECKED_CALL(GetDevice()->CreateBuffer(&bufferCreateInfo, &shape.uniformBuffer));

        // Texture image, view, and sampler
        {
            grfx_util::ImageOptions options = grfx_util::ImageOptions().MipLevelCount(PPX_REMAINING_MIP_LEVELS);
            PPX_CHECKED_CALL(grfx_util::CreateImageFromFile(GetDevice()->GetGraphicsQueue(), GetAssetPath("basic/textures/box_panel.jpg"), &shape.image));

            grfx::SampledImageViewCreateInfo viewCreateInfo = grfx::SampledImageViewCreateInfo::GuessFromImage(shape.image);
            PPX_CHECKED_CALL(GetDevice()->CreateSampledImageView(&viewCreateInfo, &shape.sampledImageView));

            grfx::SamplerCreateInfo samplerCreateInfo = {};
            samplerCreateInfo.magFilter               = grfx::FILTER_LINEAR;
            samplerCreateInfo.minFilter               = grfx::FILTER_LINEAR;
            samplerCreateInfo.mipmapMode              = grfx::SAMPLER_MIPMAP_MODE_LINEAR;
            samplerCreateInfo.minLod                  = 0;
            samplerCreateInfo.maxLod                  = FLT_MAX;
            PPX_CHECKED_CALL(GetDevice()->CreateSampler(&samplerCreateInfo, &shape.sampler));
        }

        shape.homeLoc = texture.homeLoc;
        shape.id      = id++;
        mShapes.push_back(shape);
    }

    // Descriptor
    {
        grfx::DescriptorPoolCreateInfo poolCreateInfo = {};
        poolCreateInfo.uniformBuffer                  = static_cast<uint32_t>(textures.size());
        poolCreateInfo.sampledImage                   = static_cast<uint32_t>(textures.size());
        poolCreateInfo.sampler                        = static_cast<uint32_t>(textures.size());
        PPX_CHECKED_CALL(GetDevice()->CreateDescriptorPool(&poolCreateInfo, &mDescriptorPool));

        grfx::DescriptorSetLayoutCreateInfo layoutCreateInfo = {};
        layoutCreateInfo.bindings.push_back(grfx::DescriptorBinding(0, grfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER));
        layoutCreateInfo.bindings.push_back(grfx::DescriptorBinding(1, grfx::DESCRIPTOR_TYPE_SAMPLED_IMAGE));
        layoutCreateInfo.bindings.push_back(grfx::DescriptorBinding(2, grfx::DESCRIPTOR_TYPE_SAMPLER));
        PPX_CHECKED_CALL(GetDevice()->CreateDescriptorSetLayout(&layoutCreateInfo, &mDescriptorSetLayout));

        for (auto& shape : mShapes) {
            PPX_CHECKED_CALL(GetDevice()->AllocateDescriptorSet(mDescriptorPool, mDescriptorSetLayout, &shape.descriptorSet));

            grfx::WriteDescriptor write = {};
            write.binding               = 0;
            write.type                  = grfx::DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.bufferOffset          = 0;
            write.bufferRange           = PPX_WHOLE_SIZE;
            write.pBuffer               = shape.uniformBuffer;
            PPX_CHECKED_CALL(shape.descriptorSet->UpdateDescriptors(1, &write));

            write            = {};
            write.binding    = 1;
            write.type       = grfx::DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            write.pImageView = shape.sampledImageView;
            PPX_CHECKED_CALL(shape.descriptorSet->UpdateDescriptors(1, &write));

            write          = {};
            write.binding  = 2;
            write.type     = grfx::DESCRIPTOR_TYPE_SAMPLER;
            write.pSampler = shape.sampler;
            PPX_CHECKED_CALL(shape.descriptorSet->UpdateDescriptors(1, &write));
        }
    }

    // Pipeline
    {
        std::vector<char> bytecode = LoadShader("basic/shaders", "Texture.vs");
        PPX_ASSERT_MSG(!bytecode.empty(), "VS shader bytecode load failed");
        grfx::ShaderModuleCreateInfo shaderCreateInfo = {static_cast<uint32_t>(bytecode.size()), bytecode.data()};
        PPX_CHECKED_CALL(GetDevice()->CreateShaderModule(&shaderCreateInfo, &mVS));

        bytecode = LoadShader("basic/shaders", "Texture.ps");
        // bytecode = LoadShader("basic/shaders", "zzong.frag");
        PPX_ASSERT_MSG(!bytecode.empty(), "PS shader bytecode load failed");
        shaderCreateInfo = {static_cast<uint32_t>(bytecode.size()), bytecode.data()};
        PPX_CHECKED_CALL(GetDevice()->CreateShaderModule(&shaderCreateInfo, &mPS));

        grfx::PipelineInterfaceCreateInfo piCreateInfo = {};
        piCreateInfo.setCount                          = 1;
        piCreateInfo.sets[0].set                       = 0;
        piCreateInfo.sets[0].pLayout                   = mDescriptorSetLayout;
        PPX_CHECKED_CALL(GetDevice()->CreatePipelineInterface(&piCreateInfo, &mPipelineInterface));

        mVertexBinding.AppendAttribute({"POSITION", 0, grfx::FORMAT_R32G32B32_FLOAT, 0, PPX_APPEND_OFFSET_ALIGNED, grfx::VERTEX_INPUT_RATE_VERTEX});
        mVertexBinding.AppendAttribute({"TEXCOORD", 1, grfx::FORMAT_R32G32_FLOAT, 0, PPX_APPEND_OFFSET_ALIGNED, grfx::VERTEX_INPUT_RATE_VERTEX});

        grfx::GraphicsPipelineCreateInfo2 gpCreateInfo = {};
        gpCreateInfo.VS                                = {mVS.Get(), "vsmain"};
        gpCreateInfo.PS                                = {mPS.Get(), "psmain"};
        // gpCreateInfo.PS                                 = {mPS.Get(), "main"};
        gpCreateInfo.vertexInputState.bindingCount      = 1;
        gpCreateInfo.vertexInputState.bindings[0]       = mVertexBinding;
        gpCreateInfo.topology                           = grfx::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        gpCreateInfo.polygonMode                        = grfx::POLYGON_MODE_FILL;
        gpCreateInfo.cullMode                           = grfx::CULL_MODE_BACK;
        gpCreateInfo.frontFace                          = grfx::FRONT_FACE_CCW;
        gpCreateInfo.depthReadEnable                    = true;
        gpCreateInfo.depthWriteEnable                   = true;
        gpCreateInfo.blendModes[0]                      = grfx::BLEND_MODE_NONE;
        gpCreateInfo.outputState.renderTargetCount      = 1;
        gpCreateInfo.outputState.renderTargetFormats[0] = GetSwapchain()->GetColorFormat();
        gpCreateInfo.outputState.depthStencilFormat     = GetSwapchain()->GetDepthFormat();
        gpCreateInfo.pPipelineInterface                 = mPipelineInterface;
        PPX_CHECKED_CALL(GetDevice()->CreateGraphicsPipeline(&gpCreateInfo, &mPipeline));
        {
            grfx::ShaderModulePtr VS;

            std::vector<char> bytecode = LoadShader("basic/shaders", "FullScreenTriangle.vs");
            PPX_ASSERT_MSG(!bytecode.empty(), "VS shader bytecode load failed");
            grfx::ShaderModuleCreateInfo shaderCreateInfo = {static_cast<uint32_t>(bytecode.size()), bytecode.data()};
            PPX_CHECKED_CALL(GetDevice()->CreateShaderModule(&shaderCreateInfo, &VS));

            grfx::ShaderModulePtr PS;
            // bytecode = LoadShader("basic/shaders", "FullScreenTriangle.ps");
            bytecode = LoadShader("basic/shaders", "zzong.frag");
            PPX_ASSERT_MSG(!bytecode.empty(), "PS shader bytecode load failed");
            shaderCreateInfo = {static_cast<uint32_t>(bytecode.size()), bytecode.data()};
            PPX_CHECKED_CALL(GetDevice()->CreateShaderModule(&shaderCreateInfo, &PS));

            grfx::FullscreenQuadCreateInfo createInfo = {};
            createInfo.VS                             = VS;
            createInfo.PS                             = PS;
            createInfo.setCount                       = 0;
            createInfo.renderTargetCount              = 1;
            createInfo.renderTargetFormats[0]         = GetSwapchain()->GetColorFormat();
            createInfo.depthStencilFormat             = GetSwapchain()->GetDepthFormat();

            PPX_CHECKED_CALL(GetDevice()->CreateFullscreenQuad(&createInfo, &mDebugQuad));
        }
    }
    // Per frame data
    {
        PerFrame frame = {};

        PPX_CHECKED_CALL(GetGraphicsQueue()->CreateCommandBuffer(&frame.cmd));

        grfx::SemaphoreCreateInfo semaCreateInfo = {};
        PPX_CHECKED_CALL(GetDevice()->CreateSemaphore(&semaCreateInfo, &frame.imageAcquiredSemaphore));

        grfx::FenceCreateInfo fenceCreateInfo = {};
        PPX_CHECKED_CALL(GetDevice()->CreateFence(&fenceCreateInfo, &frame.imageAcquiredFence));

        PPX_CHECKED_CALL(GetDevice()->CreateSemaphore(&semaCreateInfo, &frame.renderCompleteSemaphore));

        fenceCreateInfo = {true}; // Create signaled
        PPX_CHECKED_CALL(GetDevice()->CreateFence(&fenceCreateInfo, &frame.renderCompleteFence));

        if (IsXrEnabled()) {
            PPX_CHECKED_CALL(GetGraphicsQueue()->CreateCommandBuffer(&frame.uiCmd));
            PPX_CHECKED_CALL(GetDevice()->CreateFence(&fenceCreateInfo, &frame.uiRenderCompleteFence));
        }

        grfx::QueryCreateInfo queryCreateInfo = {};
        queryCreateInfo.type                  = grfx::QUERY_TYPE_TIMESTAMP;
        queryCreateInfo.count                 = 2;
        PPX_CHECKED_CALL(GetDevice()->CreateQuery(&queryCreateInfo, &frame.timestampQuery));

        mPerFrame.push_back(frame);
    }

    // Vertex buffer and geometry data
    {
        // clang-format off
        std::vector<float> vertexData = {
            -1.0f,-1.0f,-1.0f,   1.0f, 1.0f,  // -Z side
             1.0f, 1.0f,-1.0f,   0.0f, 0.0f,
             1.0f,-1.0f,-1.0f,   0.0f, 1.0f,
            -1.0f,-1.0f,-1.0f,   1.0f, 1.0f,
            -1.0f, 1.0f,-1.0f,   1.0f, 0.0f,
             1.0f, 1.0f,-1.0f,   0.0f, 0.0f,

            -1.0f, 1.0f, 1.0f,   0.0f, 0.0f,  // +Z side
            -1.0f,-1.0f, 1.0f,   0.0f, 1.0f,
             1.0f, 1.0f, 1.0f,   1.0f, 0.0f,
            -1.0f,-1.0f, 1.0f,   0.0f, 1.0f,
             1.0f,-1.0f, 1.0f,   1.0f, 1.0f,
             1.0f, 1.0f, 1.0f,   1.0f, 0.0f,

            -1.0f,-1.0f,-1.0f,   0.0f, 1.0f,  // -X side
            -1.0f,-1.0f, 1.0f,   1.0f, 1.0f,
            -1.0f, 1.0f, 1.0f,   1.0f, 0.0f,
            -1.0f, 1.0f, 1.0f,   1.0f, 0.0f,
            -1.0f, 1.0f,-1.0f,   0.0f, 0.0f,
            -1.0f,-1.0f,-1.0f,   0.0f, 1.0f,

             1.0f, 1.0f,-1.0f,   0.0f, 1.0f,  // +X side
             1.0f, 1.0f, 1.0f,   1.0f, 1.0f,
             1.0f,-1.0f, 1.0f,   1.0f, 0.0f,
             1.0f,-1.0f, 1.0f,   1.0f, 0.0f,
             1.0f,-1.0f,-1.0f,   0.0f, 0.0f,
             1.0f, 1.0f,-1.0f,   0.0f, 1.0f,

            -1.0f,-1.0f,-1.0f,   1.0f, 0.0f,  // -Y side
             1.0f,-1.0f,-1.0f,   1.0f, 1.0f,
             1.0f,-1.0f, 1.0f,   0.0f, 1.0f,
            -1.0f,-1.0f,-1.0f,   1.0f, 0.0f,
             1.0f,-1.0f, 1.0f,   0.0f, 1.0f,
            -1.0f,-1.0f, 1.0f,   0.0f, 0.0f,

            -1.0f, 1.0f,-1.0f,   1.0f, 0.0f,  // +Y side
            -1.0f, 1.0f, 1.0f,   0.0f, 0.0f,
             1.0f, 1.0f, 1.0f,   0.0f, 1.0f,
            -1.0f, 1.0f,-1.0f,   1.0f, 0.0f,
             1.0f, 1.0f, 1.0f,   0.0f, 1.0f,
             1.0f, 1.0f,-1.0f,   1.0f, 1.0f,
        };
        // clang-format on
        uint32_t dataSize = ppx::SizeInBytesU32(vertexData);

        grfx::BufferCreateInfo bufferCreateInfo       = {};
        bufferCreateInfo.size                         = dataSize;
        bufferCreateInfo.usageFlags.bits.vertexBuffer = true;
        bufferCreateInfo.memoryUsage                  = grfx::MEMORY_USAGE_CPU_TO_GPU;

        PPX_CHECKED_CALL(GetDevice()->CreateBuffer(&bufferCreateInfo, &mVertexBuffer));

        void* pAddr = nullptr;
        PPX_CHECKED_CALL(mVertexBuffer->MapMemory(0, &pAddr));
        memcpy(pAddr, vertexData.data(), dataSize);
        mVertexBuffer->UnmapMemory();
    }
    // Viewport and scissor rect
    mViewport    = {0, 0, float(GetWindowWidth()), float(GetWindowHeight()), 0, 1};
    mScissorRect = {0, 0, GetWindowWidth(), GetWindowHeight()};
}

void ProjApp::Render()
{
    PerFrame& frame = mPerFrame[0];

    uint32_t currentViewIndex = 0;
    if (IsXrEnabled()) {
        currentViewIndex = GetXrComponent().GetCurrentViewIndex();
    }

    uint32_t imageIndex = UINT32_MAX;
    if (IsXrEnabled() && (currentViewIndex == 0) && GetSettings()->enableImGui) {
        grfx::SwapchainPtr uiSwapchain = GetUISwapchain();
        PPX_CHECKED_CALL(uiSwapchain->AcquireNextImage(UINT64_MAX, nullptr, nullptr, &imageIndex));
        PPX_CHECKED_CALL(frame.uiRenderCompleteFence->WaitAndReset());

        PPX_CHECKED_CALL(frame.uiCmd->Begin());
        {
            grfx::RenderPassPtr renderPass = uiSwapchain->GetRenderPass(imageIndex);
            PPX_ASSERT_MSG(!renderPass.IsNull(), "render pass object is null");

            grfx::RenderPassBeginInfo beginInfo = {};
            beginInfo.pRenderPass               = renderPass;
            beginInfo.renderArea                = renderPass->GetRenderArea();
            beginInfo.RTVClearCount             = 1;
            beginInfo.RTVClearValues[0]         = {{0, 0, 0, 0}};
            beginInfo.DSVClearValue             = {1.0f, 0xFF};

            frame.uiCmd->BeginRenderPass(&beginInfo);
            // Draw ImGui
            DrawDebugInfo();
            DrawImGui(frame.uiCmd);
            frame.uiCmd->EndRenderPass();
        }
        PPX_CHECKED_CALL(frame.uiCmd->End());

        grfx::SubmitInfo submitInfo     = {};
        submitInfo.commandBufferCount   = 1;
        submitInfo.ppCommandBuffers     = &frame.uiCmd;
        submitInfo.waitSemaphoreCount   = 0;
        submitInfo.ppWaitSemaphores     = nullptr;
        submitInfo.signalSemaphoreCount = 0;
        submitInfo.ppSignalSemaphores   = nullptr;

        submitInfo.pFence = frame.uiRenderCompleteFence;

        PPX_CHECKED_CALL(GetGraphicsQueue()->Submit(&submitInfo));
    }

    grfx::SwapchainPtr swapchain = GetSwapchain(currentViewIndex);

    if (swapchain->ShouldSkipExternalSynchronization()) {
        // No need to
        // - Signal imageAcquiredSemaphore & imageAcquiredFence.
        // - Wait for imageAcquiredFence since xrWaitSwapchainImage is called in AcquireNextImage.
        PPX_CHECKED_CALL(swapchain->AcquireNextImage(UINT64_MAX, nullptr, nullptr, &imageIndex));
    }
    else {
        // Wait semaphore is ignored for XR.
        PPX_CHECKED_CALL(swapchain->AcquireNextImage(UINT64_MAX, frame.imageAcquiredSemaphore, frame.imageAcquiredFence, &imageIndex));

        // Wait for and reset image acquired fence.
        PPX_CHECKED_CALL(frame.imageAcquiredFence->WaitAndReset());
    }

    // Wait for and reset render complete fence
    PPX_CHECKED_CALL(frame.renderCompleteFence->WaitAndReset());

    // Read query results
    if (GetFrameCount() > 0) {
        uint64_t data[2] = {0};
        PPX_CHECKED_CALL(frame.timestampQuery->GetData(data, 2 * sizeof(uint64_t)));
        mGpuWorkDuration = data[1] - data[0];

        uint64_t frequency = 0;
        GetGraphicsQueue()->GetTimestampFrequency(&frequency);
        const float gpuWorkDurationMs = static_cast<float>(mGpuWorkDuration / static_cast<double>(frequency)) * 1000.0f;
        // PPX_LOG_INFO("[zzong] GPU time: " << gpuWorkDurationMs << " ms");
    }
    // Reset queries
    frame.timestampQuery->Reset(0, 2);

    // Update uniform buffers.
    for (auto& shape : mShapes) {
        float    t = 10; // GetElapsedSeconds();
        float4x4 P = glm::perspective(glm::radians(60.0f), GetWindowAspect(), 0.001f, 10000.0f);
        float4x4 V = glm::lookAt(float3(0, 0, 3), float3(5, 0, 0), float3(0, 1, 0));
        // float4x4 V = glm::lookAt(float3(0, 0, 0), float3(0, 0, 1), float3(0, 1, 0));

        if (IsXrEnabled()) {
            P = GetXrComponent().GetProjectionMatrixForCurrentViewAndSetFrustumPlanes(0.001f, 10000.0f);
            V = GetXrComponent().GetViewMatrixForCurrentView();
        }

        float4x4 T   = glm::translate(float3(shape.homeLoc[0], shape.homeLoc[1], -8 + sin(shape.id * t / 2))); // * sin(t / 2)));
        float4x4 R   = glm::rotate(shape.id + t, float3(shape.id * t, 0, 0)) * glm::rotate(shape.id + t / 4, float3(0, shape.id * t, 0)) * glm::rotate(shape.id + t / 4, float3(0, 0, shape.id * t));
        float4x4 M   = T * R;
        float4x4 mat = P * V * M;
        // float4x4 M   = glm::translate(float3(0, 0, -3)) * glm::rotate(t, float3(0, 0, 1)) * glm::rotate(t, float3(0, 1, 0)) * glm::rotate(t, float3(1, 0, 0));

        void* pData = nullptr;
        PPX_CHECKED_CALL(shape.uniformBuffer->MapMemory(0, &pData));
        memcpy(pData, &mat, sizeof(mat));
        shape.uniformBuffer->UnmapMemory();
    }

    // Build command buffer
    PPX_CHECKED_CALL(frame.cmd->Begin());
    {
        grfx::RenderPassPtr renderPass = swapchain->GetRenderPass(imageIndex);
        PPX_ASSERT_MSG(!renderPass.IsNull(), "render pass object is null");

        grfx::RenderPassBeginInfo beginInfo = {};
        beginInfo.pRenderPass               = renderPass;
        beginInfo.renderArea                = renderPass->GetRenderArea();
        beginInfo.RTVClearCount             = 1;
        beginInfo.RTVClearValues[0]         = {{0, 0, 0, 0}};
        beginInfo.DSVClearValue             = {1.0f, 0xFF};
        if (!IsXrEnabled()) {
            frame.cmd->TransitionImageLayout(renderPass->GetRenderTargetImage(0), PPX_ALL_SUBRESOURCES, grfx::RESOURCE_STATE_PRESENT, grfx::RESOURCE_STATE_RENDER_TARGET);
        }
        frame.cmd->BeginRenderPass(&beginInfo);
        {
            frame.cmd->WriteTimestamp(frame.timestampQuery, grfx::PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);
            frame.cmd->SetScissors(1, &mScissorRect);
            frame.cmd->SetViewports(1, &mViewport);
            frame.cmd->BindGraphicsPipeline(mPipeline);
            frame.cmd->BindVertexBuffers(1, &mVertexBuffer, &mVertexBinding.GetStride());

            for (auto& shape : mShapes) {
                frame.cmd->BindGraphicsDescriptorSets(mPipelineInterface, 1, &shape.descriptorSet);
                frame.cmd->Draw(36, 1, 0, 0);
            }

            frame.cmd->Draw(mDebugQuad, 0, nullptr);

            frame.cmd->WriteTimestamp(frame.timestampQuery, grfx::PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 1);

            if (!IsXrEnabled()) {
                // Draw ImGui
                DrawDebugInfo();
                DrawImGui(frame.cmd);
            }
        }
        frame.cmd->EndRenderPass();
        frame.cmd->ResolveQueryData(frame.timestampQuery, 0, 2);
        if (!IsXrEnabled()) {
            frame.cmd->TransitionImageLayout(renderPass->GetRenderTargetImage(0), PPX_ALL_SUBRESOURCES, grfx::RESOURCE_STATE_RENDER_TARGET, grfx::RESOURCE_STATE_PRESENT);
        }
    }
    PPX_CHECKED_CALL(frame.cmd->End());

    grfx::SubmitInfo submitInfo   = {};
    submitInfo.commandBufferCount = 1;
    submitInfo.ppCommandBuffers   = &frame.cmd;
    // submitInfo.waitSemaphoreCount   = 1;
    // submitInfo.ppWaitSemaphores     = &frame.imageAcquiredSemaphore;
    // submitInfo.signalSemaphoreCount = 1;
    // submitInfo.ppSignalSemaphores   = &frame.renderCompleteSemaphore;
    submitInfo.pFence = frame.renderCompleteFence;
    // No need to use semaphore when XR is enabled.
    if (IsXrEnabled()) {
        submitInfo.waitSemaphoreCount   = 0;
        submitInfo.ppWaitSemaphores     = nullptr;
        submitInfo.signalSemaphoreCount = 0;
        submitInfo.ppSignalSemaphores   = nullptr;
    }
    else {
        submitInfo.waitSemaphoreCount   = 1;
        submitInfo.ppWaitSemaphores     = &frame.imageAcquiredSemaphore;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.ppSignalSemaphores   = &frame.renderCompleteSemaphore;
    }

    PPX_CHECKED_CALL(GetGraphicsQueue()->Submit(&submitInfo));

    // No need to present when XR is enabled.
    if (!IsXrEnabled()) {
        PPX_CHECKED_CALL(swapchain->Present(imageIndex, 1, &frame.renderCompleteSemaphore));
    }
    else {
        if (GetSettings()->xr.enableDebugCapture && (currentViewIndex == 1)) {
            // We could use semaphore to sync to have better performance,
            // but this requires modifying the submission code.
            // For debug capture we don't care about the performance,
            // so use existing fence to sync for simplicity.
            grfx::SwapchainPtr debugSwapchain = GetDebugCaptureSwapchain();
            PPX_CHECKED_CALL(debugSwapchain->AcquireNextImage(UINT64_MAX, nullptr, frame.imageAcquiredFence, &imageIndex));
            frame.imageAcquiredFence->WaitAndReset();
            PPX_CHECKED_CALL(debugSwapchain->Present(imageIndex, 0, nullptr));
        }
    }
}

SETUP_APPLICATION(ProjApp)