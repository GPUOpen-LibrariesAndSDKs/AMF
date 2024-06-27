#include "StitchPipeline.h"

#include "public/common/TraceAdapter.h"
#define AMF_DISABLE_PLAYBACK_EXPORTS
#include "public/include/components/FFMPEGComponents.h"
#include "public/include/components/FFMPEGFileDemuxer.h"
#include <fstream>
#include "public/include/components/ZCamLiveStream.h"

#define _USE_MATH_DEFINES
#include "math.h"
#include <DirectXMath.h>
using namespace DirectX;

#pragma warning(disable:4355)

#define AMF_FACILITY L"StitchPipeline"

// hard- coded parameters to try
static double   dForceFPS = 60.;
#if defined( _M_AMD64)
static int      iDefaultStreamCount = 6;
#else
static int      iDefaultStreamCount = 6;
#endif

// test parameters
static amf::AMF_SURFACE_FORMAT eComposerInputDefault = amf::AMF_SURFACE_NV12; // best performance with decoder output
static amf::AMF_SURFACE_FORMAT eComposerOutputDefault = amf::AMF_SURFACE_BGRA; // use if most composer input is RGBA


typedef std::shared_ptr<Splitter> SplitterPtr;
double ReadOneValueDouble(std::string &text, std::string::size_type  &pos);
void ReadOneImage(std::string &text, StitchTemplate &t);
int ReadOneValueInt(std::string &text, std::string::size_type  &pos);

StitchPipeline::StitchPipeline() :
    m_hwnd(NULL),
    m_outputMode(AMF_VIDEO_STITCH_OUTPUT_MODE_ENUM::AMF_VIDEO_STITCH_OUTPUT_MODE_PREVIEW)
{
}

StitchPipeline::~StitchPipeline()
{
    Terminate();
    g_AMFFactory.Terminate();
}

AMF_RESULT StitchPipeline::Init(
    const std::vector<std::wstring>& filenames,
    const std::wstring& ptguifilename,
    AMF_VIDEO_STITCH_OUTPUT_MODE_ENUM outputMode
)
{
    m_outputMode = outputMode;
    AMF_RESULT res = InitMedia(filenames, true);
    CHECK_AMF_ERROR_RETURN(res, L"InitMedia() failed!\n");
    res = InitCamera(ptguifilename, false);
    CHECK_AMF_ERROR_RETURN(res, L"InitCamera() failed!\n");
    res = StitchPipelineBase::Init();
    CHECK_AMF_ERROR_RETURN(res, L"StitchPipelineBase::Init() failed!\n");
    return res;
}

//-------------------------------------------------------------------------------------------------
void StitchPipeline::ToggleWire()
{
    if(m_pStitch == NULL)
    {
        return;
    }
    bool value = false;
    m_pStitch->GetProperty(AMF_VIDEO_STITCH_WIRE_RENDER, &value);
    value = !value;
    m_pStitch->SetProperty(AMF_VIDEO_STITCH_WIRE_RENDER, value);
    if(m_pVideoPresenter->GetMode() ==  VideoPresenter::ModePaused)
    {
        m_pStitchElement->Resubmit();
        m_pVideoPresenter->Step();
        m_pStitchElement->Resubmit();
        m_pVideoPresenter->Step();
    }
}
//-------------------------------------------------------------------------------------------------
void StitchPipeline::ToggleColorBalance()
{
    if(m_pStitch == NULL)
    {
        return;
    }
    bool value = false;
    m_pStitch->GetProperty(AMF_VIDEO_STITCH_COLOR_BALANCE, &value);
    value = !value;
    m_pStitch->SetProperty(AMF_VIDEO_STITCH_COLOR_BALANCE, value);
    if(m_pVideoPresenter->GetMode() ==  VideoPresenter::ModePaused)
    {
        m_pStitchElement->Resubmit();
        m_pVideoPresenter->Step();
        m_pStitchElement->Resubmit();
        m_pVideoPresenter->Step();
    }
}
//-------------------------------------------------------------------------------------------------
void StitchPipeline::ToggleOutputMode()
{
    if(m_pStitch == NULL)
    {
        return;
    }
    amf_int64 value = AMF_VIDEO_STITCH_OUTPUT_MODE_PREVIEW;
    m_pStitch->GetProperty(AMF_VIDEO_STITCH_OUTPUT_MODE, &value);
    value = value == AMF_VIDEO_STITCH_OUTPUT_MODE_LAST ? 0 : value + 1;


    m_pStitch->SetProperty(AMF_VIDEO_STITCH_OUTPUT_MODE, value);
    if(m_pVideoPresenter != NULL)
    {
        if(value == AMF_VIDEO_STITCH_OUTPUT_MODE_CUBEMAP)
        {
            m_pVideoPresenter ->SetProcessor(nullptr, nullptr, true);
        }
        else
        {
            m_pVideoPresenter ->SetProcessor(m_pStitch, nullptr, true);
        }

        m_pVideoPresenter->SetRenderToBackBuffer(value != AMF_VIDEO_STITCH_OUTPUT_MODE_CUBEMAP);
    }

    if(m_pVideoPresenter->GetMode() ==  VideoPresenter::ModePaused)
    {
        m_pStitchElement->Resubmit();
        m_pVideoPresenter->Step();
        m_pStitchElement->Resubmit();
        m_pVideoPresenter->Step();
    }
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT StitchPipeline::InitStitcher(amf::AMF_SURFACE_FORMAT &stitchInputFmt)
{
    stitchInputFmt = amf::AMF_SURFACE_NV12;
    //---------------------------------------------------------------------------------------------
    amf_int32 streamCount = (amf_int32)m_Cameras.size();
    GetParam(StitchPipelineBase::PARAM_NAME_STREAM_COUNT, streamCount);
    amf_int32 compositedWidth = 3840;
    amf_int32 compositedHeight = 1920;
    GetParam(StitchPipelineBase::PARAM_NAME_COMPOSITED_WIDTH, compositedWidth);
    GetParam(StitchPipelineBase::PARAM_NAME_COMPOSITED_HEIGHT, compositedHeight);

    amf_int32 computeEngine = amf::AMF_MEMORY_COMPUTE_FOR_DX11;
    GetParam(StitchPipelineBase::PARAM_NAME_PRESENTER, computeEngine);

    //---------------------------------------------------------------------------------------------
    // Init Video Stitch
    amf::AMFComponentPtr pStitch;
    AMF_RESULT res = g_AMFFactory.LoadExternalComponent(m_pContext, STITCH_DLL_NAME, "AMFCreateComponentInt", NULL, &pStitch);
    CHECK_AMF_ERROR_RETURN(res, L"AMFCreateComponent(" << AMFVideoStitch << L") failed");
    m_pStitch = amf::AMFComponentExPtr(pStitch);

    m_pStitch->SetProperty(AMF_VIDEO_STITCH_COMPUTE_DEVICE, computeEngine);

    m_pStitch->SetProperty(AMF_VIDEO_STITCH_MEMORY_TYPE, m_pVideoPresenter->GetMemoryType());
    m_pStitch->SetProperty(AMF_VIDEO_STITCH_INPUTCOUNT, amf_int64(streamCount));
    m_pStitch->SetProperty(AMF_VIDEO_STITCH_OUTPUT_SIZE, AMFConstructSize(compositedWidth, compositedHeight));
    m_pStitch->SetProperty(AMF_VIDEO_STITCH_OUTPUT_FORMAT, StitchPipelineBase::m_eComposerOutputDefault);

    m_pStitch->SetProperty(AMF_VIDEO_STITCH_OUTPUT_MODE, m_outputMode);
    m_pStitch->SetProperty(AMF_VIDEO_STITCH_OUTPUT_MODE, 1);
    return res;
}
//-------------------------------------------------------------------------------------------------
void StitchPipeline::SetFullSpeed(bool bFullSpeed)
{
    if(m_pVideoPresenter != NULL)
    {
        if(!bFullSpeed)
        {
            m_pVideoPresenter->Reset();
        }
        m_pVideoPresenter->DoActualWait(!bFullSpeed);
    }
}
//-------------------------------------------------------------------------------------------------
void StitchPipeline::SetSubresourceIndex(amf_int32 index)
{
    if(m_pVideoPresenter != NULL)
    {
        m_pVideoPresenter->SetSubresourceIndex(index);
    }
}
//-------------------------------------------------------------------------------------------------
amf_int32 StitchPipeline::GetSubresourceIndex()
{
    if(m_pVideoPresenter != NULL)
    {
        return m_pVideoPresenter->GetSubresourceIndex();
    }
    return 0;
}
