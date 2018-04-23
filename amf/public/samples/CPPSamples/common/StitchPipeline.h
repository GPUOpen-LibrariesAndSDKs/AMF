#pragma once

#include "public/include/core/Context.h"
#include "public/include/components/Component.h"
#include "public/include/components/VideoDecoderUVD.h"
#include "public/include/components/VideoConverter.h"
#include "public/samples/CPPSamples/common/BitStreamParser.h"
#include "public/samples/CPPSamples/common/VideoPresenter.h"
#include "public/samples/CPPSamples/common/ParametersStorage.h"
#include "public/samples/CPPSamples/common/Pipeline.h"
#include "public/samples/CPPSamples/common/StitchPipelineBase.h"


class StitchPipeline : public StitchPipelineBase
{
public:
    StitchPipeline();
    virtual ~StitchPipeline();
public:

    AMF_RESULT Init(const std::vector<std::wstring>& filenames, const std::wstring& ptguifilename,
        AMF_VIDEO_STITCH_OUTPUT_MODE_ENUM outputMode);

    void ToggleWire();
    void ToggleColorBalance();
    void ToggleOutputMode();
    void SetFullSpeed(bool bFullSpeed);
    void SetSubresourceIndex(amf_int32 index);
    amf_int32 GetSubresourceIndex();

    amf::AMFContext* GetContext() {return m_pContext;}

protected:
    AMF_RESULT InitStitcher(amf::AMF_SURFACE_FORMAT &stitchInputFmt);
    HWND m_hwnd;
private:
    AMF_VIDEO_STITCH_OUTPUT_MODE_ENUM m_outputMode;
};