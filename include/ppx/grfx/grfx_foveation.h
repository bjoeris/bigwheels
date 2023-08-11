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

#ifndef ppx_grfx_foveation_h
#define ppx_grfx_foveation_h

#include "ppx/grfx/grfx_config.h"
#include "ppx/grfx/grfx_texture.h"

// @zzong. TODO: add comments in this file
namespace ppx {
namespace grfx {

struct FoveationPatternCreateInfo
{
    uint32_t            fbWidth       = 0;
    uint32_t            fbHeight      = 0;
    grfx::FoveationMode foveationMode = grfx::FOVEATION_NONE;
};

struct FoveationCapabilities
{
    struct
    {
        bool supported                     = false;
        bool supportsDynamicImageView      = false;
        bool supportsNonSubsampledImages   = false;
        bool supportsAdditionalInvocations = false;
        struct
        {
            struct
            {
                uint32_t width;
                uint32_t height;
            } min;
            struct
            {
                uint32_t width;
                uint32_t height;
            } max;
        } texelSize;
    } densityMap;

    struct
    {
        bool supportPipelineVRS   = false;
        bool supportPrimitiveVRS  = false;
        bool supportAttachmentVRS = false;
    } vrs;
};

class FoveationPattern
    : public grfx::DeviceObject<grfx::FoveationPatternCreateInfo>
{
public:
    FoveationPattern() {}
    virtual ~FoveationPattern() {}

    grfx::FoveationMode       GetFoveationMode() { return mFoveationMode; }
    grfx::SampledImageViewPtr GetFoveationImageViewPtr() { return mFoveationTexture->GetSampledImageView(); }

protected:
    virtual Result CreateApiObjects(const grfx::FoveationPatternCreateInfo* pCreateInfo) override;
    virtual void   DestroyApiObjects() override;

private:
    Result CreateDefaultTextureForDensityMap(const grfx::FoveationPatternCreateInfo* pCreateInfo, grfx::Texture** ppFoveationTexture);

private:
    grfx::FoveationMode mFoveationMode;
    grfx::TexturePtr    mFoveationTexture;
};

} // namespace grfx
} // namespace ppx
#endif // ppx_grfx_foveation_h