//
// Notice Regarding Standards.  AMD does not provide a license or sublicense to
// any Intellectual Property Rights relating to any standards, including but not
// limited to any audio and/or video codec technologies such as MPEG-2, MPEG-4;
// AVC/H.264; HEVC/H.265; AAC decode/FFMPEG; AAC encode/FFMPEG; VC-1; and MP3
// (collectively, the "Media Technologies"). For clarity, you will pay any
// royalties due for such third party technologies, which may include the Media
// Technologies that are owed as a result of AMD providing the Software to you.
//
// MIT license
//
// Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
#include "DisplayDvrPipeline.h"
#include "public/include/components/VideoDecoderUVD.h"
#include "public/include/components/VideoConverter.h"
#include "public/include/components/FFMPEGComponents.h"
#include "public/include/components/FFMPEGAudioDecoder.h"
#include "public/include/components/FFMPEGAudioConverter.h"
#include "public/include/components/FFMPEGAudioEncoder.h"
#include "public/include/components/FFMPEGFileDemuxer.h"
#include "public/include/components/FFMPEGFileMuxer.h"
#include "public/include/components/DisplayCapture.h"
#include "public/include/components/AudioCapture.h"
#include "public/common/PropertyStorageExImpl.h"

#pragma warning(disable:4355)


const wchar_t* DisplayDvrPipeline::PARAM_NAME_CODEC               = L"CODEC";
const wchar_t* DisplayDvrPipeline::PARAM_NAME_OUTPUT              = L"OUTPUT";
const wchar_t* DisplayDvrPipeline::PARAM_NAME_URL                 = L"URL";

const wchar_t* DisplayDvrPipeline::PARAM_NAME_ADAPTERID           = L"ADAPTERID";
const wchar_t* DisplayDvrPipeline::PARAM_NAME_MONITORID           = L"MONITORID";
const wchar_t* DisplayDvrPipeline::PARAM_NAME_MULTI_MONITOR       = L"MULTIMONITOR";

const wchar_t* DisplayDvrPipeline::PARAM_NAME_VIDEO_HEIGHT        = L"VIDEOHEIGHT";
const wchar_t* DisplayDvrPipeline::PARAM_NAME_VIDEO_WIDTH         = L"VIDEOWIDTH";

const wchar_t* DisplayDvrPipeline::PARAM_NAME_OPENCL_CONVERTER    = L"OPENCLCONVERTER";

const wchar_t* DisplayDvrPipeline::PARAM_NAME_CAPTURE_COMPONENT   = L"CAPTURECOMPONENT";

const wchar_t* DisplayDvrPipeline::PARAM_NAME_ENABLE_PRE_ANALYSIS = L"PREANALYSIS";

const unsigned kFFMPEG_AAC_CODEC_ID = 0x15002;

// Definitions from include/libavutil/channel_layout.h
const unsigned kFFMPEG_AUDIO_LAYOUT_STEREO = 0x00000003;

const unsigned kFrameRate = 60;

// DVR.exe streaming command line:
//-URL rtmp://192.168.50.247  -VIDEOWIDTH 3840 -VIDEOHEIGHT 2160 -TargetBitrate 48000000 -PeakBitrate 50000000 -VBVBufferSize 1000000 -QualityPreset 1 -FrameRate 60,1 -LowLatencyInternal true -IDRPeriod 0 -ProfileLevel 51 -CAPTURE dd

// PlaybackHW.exe streaming command line
// -UrlVideo rtmp://0.0.0.0 -LISTEN true -LOWLATENCY true
namespace
{
    // Helper for changing the surface format on the display capture connection
    class AMFComponentElementConverterInterceptor : public AMFComponentElement
    {
    public:
        AMFComponentElementConverterInterceptor(DisplayDvrPipeline* pDisplayDvrPipeline, amf::AMFComponent* pComponent)
            : AMFComponentElement(pComponent)
            , m_pDisplayDvrPipeline(pDisplayDvrPipeline)
            , m_bBlock(false)
            , m_blockStartTime(-1)
        {
        }

        virtual ~AMFComponentElementConverterInterceptor()
        {
            m_pDisplayDvrPipeline = NULL;
        }

        virtual AMF_RESULT SubmitInput(amf::AMFData* pData)
        {
            amf::AMFLock lock(&m_sync);

            AMF_RESULT res = AMF_OK;
            amf::AMFDataPtr pDataPtr(pData);
            if (pDataPtr->GetPts() <= m_blockStartTime)
            {
                // Discard any old data
                res = AMF_OK;
            }
            else
            {
                // Process normally
                res = AMFComponentElement::SubmitInput(pData);
            }
            return res;
        }

        virtual AMF_RESULT QueryOutput(amf::AMFData** ppData)
        {
            amf::AMFLock lock(&m_sync);

            AMF_RESULT res = AMF_OK;
            if (m_bBlock)
            {
                return AMF_REPEAT;
            }
            //
            res = AMFComponentElement::QueryOutput(ppData);
            CHECK_AMF_ERROR_RETURN(res, "AMFComponentElement::QueryOutput() failed");
            //
            return res;
        }

        void SetBlock(bool state)
        {
            amf::AMFLock lock(&m_sync);

            m_bBlock = state;
            if (state)
            {
                m_blockStartTime = m_pDisplayDvrPipeline->GetCurrentPts();
            }
        }

    private:
        DisplayDvrPipeline* m_pDisplayDvrPipeline;
        mutable amf::AMFCriticalSection    m_sync;
        bool                               m_bBlock;
        amf_pts                            m_blockStartTime;
    };

    // Helper for changing the surface format on the display capture connection
    class AMFComponentElementDisplayCaptureInterceptor : public AMFComponentElement
    {
    public:
        AMFComponentElementDisplayCaptureInterceptor(amf_int32 index,
            AMFComponentElementConverterInterceptor* converterInterceptor,
            DisplayDvrPipeline* pDisplayDvrPipeline, amf::AMFComponent *pComponent)
            : m_iIndex(index)
            , m_pConverterInterceptor(converterInterceptor)
            , AMFComponentElement(pComponent)
            , m_pDisplayDvrPipeline(pDisplayDvrPipeline)
        {
            amf_int64 eCurrentFormat = amf::AMF_SURFACE_UNKNOWN;
            pComponent->GetProperty(AMF_DISPLAYCAPTURE_FORMAT, &eCurrentFormat);
            m_eLastFormat = (amf::AMF_SURFACE_FORMAT) eCurrentFormat;
        }

        virtual ~AMFComponentElementDisplayCaptureInterceptor()
        {
            m_pDisplayDvrPipeline = NULL;
        }

        virtual amf_int32 GetInputSlotCount() const { return 0; }

        virtual AMF_RESULT QueryOutput(amf::AMFData** ppData)
        {
            AMF_RESULT res = AMFComponentElement::QueryOutput(ppData);
            CHECK_AMF_ERROR_RETURN(res, "AMFComponentElement::QueryOutput() failed");
            // Get the surface format
            amf::AMFSurfacePtr pSurfPtr(*ppData);
            if (pSurfPtr)
            {
                AMF_RESULT res2 = AMF_OK;

                amf::AMF_SURFACE_FORMAT format = pSurfPtr->GetFormat();
                // Was there a format switch?
                if (format != m_eLastFormat)
                {
                    m_pConverterInterceptor->SetBlock(true);
                    res2 = m_pDisplayDvrPipeline->SwitchConverterFormat(m_iIndex, format);
                    m_pConverterInterceptor->SetBlock(false);
                    CHECK_AMF_ERROR_RETURN(res2, "m_pDisplayDvrPipeline->SwitchConverterFormat() failed");
                    m_eLastFormat = format;
                }
            }
            return res;
        }

    private:
        DisplayDvrPipeline*                         m_pDisplayDvrPipeline;
        amf::AMF_SURFACE_FORMAT                     m_eLastFormat;
        amf_int32                                   m_iIndex;
        AMFComponentElementConverterInterceptor*    m_pConverterInterceptor;

    };


}

// PipelineElementEncoder implementation
//
class DisplayDvrPipeline::PipelineElementEncoder : public AMFComponentElement
{
public:
    //-------------------------------------------------------------------------------------------------
    PipelineElementEncoder(amf::AMFComponentPtr pComponent, DisplayDvrPipeline* pParams, amf_int64 frameParameterFreq, amf_int64 dynamicParameterFreq)
        :AMFComponentElement(pComponent),
        m_pDisplayDvrPipeline(pParams),
        m_framesSubmitted(0),
        m_frameParameterFreq(frameParameterFreq),
        m_dynamicParameterFreq(dynamicParameterFreq)
    {
    }

    //-------------------------------------------------------------------------------------------------
    virtual ~PipelineElementEncoder()
    {
    }

    //-------------------------------------------------------------------------------------------------
    AMF_RESULT SubmitInput(amf::AMFData* pData)
    {
        AMF_RESULT res = AMF_OK;
        if(pData == NULL) // EOF
        {
            res = m_pComponent->Drain();
        }
        else
        {
            if(m_frameParameterFreq != 0 && m_framesSubmitted !=0 && (m_framesSubmitted % m_frameParameterFreq) == 0)
            { // apply frame-specific properties to the current frame
                PushParamsToPropertyStorage(m_pDisplayDvrPipeline, ParamEncoderFrame, pData);
            }
            if(m_dynamicParameterFreq != 0 && m_framesSubmitted !=0 && (m_framesSubmitted % m_dynamicParameterFreq) == 0)
            { // apply dynamic properties to the encoder
                PushParamsToPropertyStorage(m_pDisplayDvrPipeline, ParamEncoderDynamic, m_pComponent);
            }

            res = m_pComponent->SubmitInput(pData);
            if(res == AMF_DECODER_NO_FREE_SURFACES)
            {
                return AMF_INPUT_FULL;
            }
            m_framesSubmitted++;
        }
        return res;
    }
    virtual AMF_RESULT QueryOutput(amf::AMFData** ppData)
    {
        AMF_RESULT res = AMFComponentElement::QueryOutput(ppData);

        /*
        if(res == AMF_OK && *ppData != nullptr)
        {
            amf_pts capturePts = (*ppData)->GetPts();
            amf_pts encodedPts = m_pDisplayDvrPipeline->m_pCurrentTime->Get();
            AMFTraceInfo(L"Latency", L"latency = %5.2f", (encodedPts - capturePts) / 10000. );
        }
        */

        return res;
    }
protected:
    DisplayDvrPipeline*      m_pDisplayDvrPipeline;
    amf_int                 m_framesSubmitted;
    amf_int64               m_frameParameterFreq;
    amf_int64               m_dynamicParameterFreq;
};

// DisplayDvrPipeline implementation
//

//-------------------------------------------------------------------------------------------------
DisplayDvrPipeline::DisplayDvrPipeline()
    : m_pContext()
    , m_pCurrentTime(new amf::AMFCurrentTimeImpl())
    , m_outVideoStreamMuxerIndex(-1)
    , m_outAudioStreamMuxerIndex(-1)
    , m_useOpenCLConverter(false)
    , m_engineMemoryType(amf::AMF_MEMORY_UNKNOWN)
{
    SetParamDescription(PARAM_NAME_CODEC, ParamCommon, L"Codec name (AVC or H264, HEVC or H265)", ParamConverterCodec);
    SetParamDescription(PARAM_NAME_OUTPUT, ParamCommon, L"Output file name", NULL);
    SetParamDescription(PARAM_NAME_URL, ParamCommon, L"Output URL name", NULL);
    SetParamDescription(PARAM_NAME_ADAPTERID, ParamCommon, L"Index of GPU adapter (number, default = 0)", NULL);
    SetParamDescription(PARAM_NAME_MONITORID, ParamCommon, L"List of indexes of monitors on GPU with comma separator (number, default = 0)", NULL);
    SetParamDescription(PARAM_NAME_MULTI_MONITOR, ParamCommon, L"Capture several monitors(boolean, default = false", ParamConverterBoolean);
    SetParamDescription(PARAM_NAME_VIDEO_HEIGHT, ParamCommon, L"Video height (number, default = 1080)", NULL);
    SetParamDescription(PARAM_NAME_VIDEO_WIDTH, ParamCommon, L"Video width (number, default = 1920)", NULL);
    SetParamDescription(PARAM_NAME_OPENCL_CONVERTER, ParamCommon, L"Use OpenCL Converter (bool, default = false)", ParamConverterBoolean);
    SetParamDescription(PARAM_NAME_CAPTURE_COMPONENT, ParamCommon, L"Display capture component (AMD DX11/DX12 or DD)", NULL);

    // to demo frame-specific properties - will be applied to each N-th frame (force IDR)
    SetParam(AMF_VIDEO_ENCODER_FORCE_PICTURE_TYPE, amf_int64(AMF_VIDEO_ENCODER_PICTURE_TYPE_IDR));
}

//-------------------------------------------------------------------------------------------------
DisplayDvrPipeline::~DisplayDvrPipeline()
{
    Terminate();
}

//-------------------------------------------------------------------------------------------------
void DisplayDvrPipeline::Terminate()
{
    Stop();
    m_pContext = NULL;
}

//-------------------------------------------------------------------------------------------------
amf_pts DisplayDvrPipeline::GetCurrentPts()
{
    return m_pCurrentTime->Get();
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT DisplayDvrPipeline::InitContext(const std::wstring& /*engineStr*/, amf::AMF_MEMORY_TYPE engineMemoryType, amf_uint32 adapterID)
{
    m_pCurrentTime = (new amf::AMFCurrentTimeImpl());
    AMF_RESULT res = AMF_OK;

    res = g_AMFFactory.GetFactory()->CreateContext(&m_pContext);
    CHECK_AMF_ERROR_RETURN(res, "Create AMF context");

    // Check to see if we need to initialize OpenCL
    GetParam(PARAM_NAME_OPENCL_CONVERTER, m_useOpenCLConverter);

    switch (engineMemoryType)
    {
#if !defined(_WIN32)
    case amf::AMF_MEMORY_VULKAN:
        res = m_deviceVulkan.Init(adapterID, m_pContext);
        CHECK_AMF_ERROR_RETURN(res, L"m_deviceVulkan.Init() failed");
        res = amf::AMFContext1Ptr(m_pContext)->InitVulkan(m_deviceVulkan.GetDevice());
        CHECK_AMF_ERROR_RETURN(res, L"InitVulkan() failed");
        break;
#else
#if !defined(METRO_APP)
    case amf::AMF_MEMORY_DX9:
        res = m_deviceDX9.Init(true, adapterID, false, 1, 1);
        CHECK_AMF_ERROR_RETURN(res, L"m_deviceDX9.Init() failed");

        res = m_pContext->InitDX9(m_deviceDX9.GetDevice());
        CHECK_AMF_ERROR_RETURN(res, L"m_pContext->InitDX9() failed");
        break;
#endif//#if !defined(METRO_APP)
    case amf::AMF_MEMORY_DX12:
        res = m_deviceDX12.Init(adapterID);
        CHECK_AMF_ERROR_RETURN(res, L"m_deviceDX12.Init() failed");

        res = amf::AMFContext2Ptr(m_pContext)->InitDX12(m_deviceDX12.GetDevice());
        CHECK_AMF_ERROR_RETURN(res, L"m_pContext->InitDX12() failed");
        break;
    case amf::AMF_MEMORY_DX11:
    default:
        res = m_deviceDX11.Init(adapterID);
        CHECK_AMF_ERROR_RETURN(res, L"m_deviceDX11.Init() failed");

        res = m_pContext->InitDX11(m_deviceDX11.GetDevice());
        CHECK_AMF_ERROR_RETURN(res, L"m_pContext->InitDX11() failed");
#endif
    }

#if defined(_WIN32)
    if (m_useOpenCLConverter)
    {
        res = m_deviceOpenCL.Init(m_deviceDX9.GetDevice(), m_deviceDX11.GetDevice(), NULL, NULL);
        CHECK_AMF_ERROR_RETURN(res, L"m_deviceOpenCL.Init() failed");

        res = m_pContext->InitOpenCL(m_deviceOpenCL.GetCommandQueue());
        CHECK_AMF_ERROR_RETURN(res, L"m_pContext->InitOpenCL() failed");
    }
#endif


    return res;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT DisplayDvrPipeline::InitVideo(amf_uint32 monitorID, amf::AMF_MEMORY_TYPE engineMemoryType,
    amf_int32 videoWidth, amf_int32 videoHeight)
{
    AMF_RESULT res = AMF_OK;

    // Get monitor ID
    // Init dvr capture component
    // create capture component here
    std::wstring captureComp = L"AMD";
    GetParamWString(DisplayDvrPipeline::PARAM_NAME_CAPTURE_COMPONENT, captureComp);
    amf::AMFComponentPtr pDisplayCapture;
    if (captureComp == L"AMD")
    {
        // Create AMD capture component 
        // Default capture type selects memory type by checking context->GetXXDevice()!=nullptr
        res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, AMFDisplayCapture, &pDisplayCapture);
        CHECK_AMF_ERROR_RETURN(res, L"AMFCreateComponent(" << L"CreateComponent()" << L") failed");
    }
#if defined(_WIN32)
    else if (captureComp == L"DD")
    {
        // Create DD capture component
        res = AMFCreateComponentDisplayCapture(m_pContext, nullptr, &pDisplayCapture);
        CHECK_AMF_ERROR_RETURN(res, L"AMFCreateComponent(" << L"AMFCreateComponentDisplayCapture()" << L") failed");
    }
#endif
    else {
        LOG_AMF_ERROR(AMF_FAIL, L"Unrecognized capture component: " << captureComp);
        return AMF_FAIL;
    }
    m_pDisplayCapture.push_back(pDisplayCapture);

    res = pDisplayCapture->SetProperty(AMF_DISPLAYCAPTURE_MONITOR_INDEX, monitorID);
    CHECK_AMF_ERROR_RETURN(res, L"Failed to set Dvr component monitor ID");
    res = pDisplayCapture->SetProperty(AMF_DISPLAYCAPTURE_CURRENT_TIME_INTERFACE, m_pCurrentTime);
    CHECK_AMF_ERROR_RETURN(res, L"Failed to set Dvr component current time interface");
    res = pDisplayCapture->SetProperty(AMF_DISPLAYCAPTURE_FRAMERATE, AMFConstructRate(kFrameRate, 1));
    CHECK_AMF_ERROR_RETURN(res, L"Failed to set Dvr component frame rate");

    res = pDisplayCapture->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
    CHECK_AMF_ERROR_RETURN(res, L"Failed to make Dvr component");

    AMFSize resolution = {};
    pDisplayCapture->GetProperty(AMF_DISPLAYCAPTURE_RESOLUTION, &resolution);

    amf_int64 eCurrentFormat = amf::AMF_SURFACE_UNKNOWN;
    pDisplayCapture->GetProperty(AMF_DISPLAYCAPTURE_FORMAT, &eCurrentFormat);

    // Init converter
    amf::AMFComponentPtr pConverter;
    res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, AMFVideoConverter, &pConverter);
    CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << AMFVideoConverter << L") failed");
    m_pConverter.push_back(pConverter);

    pConverter->SetProperty(AMF_VIDEO_CONVERTER_COMPUTE_DEVICE,
        (m_useOpenCLConverter) ? amf::AMF_MEMORY_OPENCL : engineMemoryType);
    pConverter->SetProperty(AMF_VIDEO_CONVERTER_MEMORY_TYPE, engineMemoryType);
    pConverter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_FORMAT, amf::AMF_SURFACE_NV12);
    pConverter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_SIZE, AMFConstructSize(videoWidth, videoHeight));

    pConverter->Init((amf::AMF_SURFACE_FORMAT)eCurrentFormat, resolution.width, resolution.height);

    // Init encoder
    m_szEncoderID = AMFVideoEncoderVCE_AVC;
    GetParamWString(DisplayDvrPipeline::PARAM_NAME_CODEC, m_szEncoderID);

    if (m_szEncoderID == AMFVideoEncoderVCE_AVC)
    {
        amf_int64 usage = 0;
        if (GetParam(AMF_VIDEO_ENCODER_USAGE, usage) == AMF_OK)
        {
            if (usage == amf_int64(AMF_VIDEO_ENCODER_USAGE_WEBCAM))
            {
                m_szEncoderID = AMFVideoEncoderVCE_SVC;
            }
        }
    }

    amf::AMFComponentPtr pEncoder;
    res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, m_szEncoderID.c_str(), &pEncoder);
    CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << m_szEncoderID << L") failed");
    m_pEncoder.push_back(pEncoder);

    AMFRate frameRate = { kFrameRate, 1 };
    if (m_szEncoderID == AMFVideoEncoderVCE_AVC || m_szEncoderID == AMFVideoEncoderVCE_SVC)
    {
        res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, frameRate);
        CHECK_AMF_ERROR_RETURN(res, L"Failed to set video encoder frame rate");
    }
    else if (m_szEncoderID == AMFVideoEncoder_AV1)
    {
        res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_FRAMERATE, frameRate);
        CHECK_AMF_ERROR_RETURN(res, L"Failed to set video encoder frame rate");
    }
    else
    {
        res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, frameRate);
        CHECK_AMF_ERROR_RETURN(res, L"Failed to set video encoder frame rate");
    }


    // Usage is preset that will set many parameters
    PushParamsToPropertyStorage(this, ParamEncoderUsage, pEncoder);

    // override some usage parameters
    PushParamsToPropertyStorage(this, ParamEncoderStatic, pEncoder);

    PushParamsToPropertyStorage(this, ParamEncoderDynamic, pEncoder);

    if (m_szEncoderID == AMFVideoEncoderVCE_AVC || m_szEncoderID == AMFVideoEncoderVCE_SVC)
    {
        pEncoder->SetProperty(AMF_VIDEO_ENCODER_LOWLATENCY_MODE, true);
    }
    else if (m_szEncoderID == AMFVideoEncoder_AV1)
    {
        pEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE, AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE_LOWEST_LATENCY);
    }
    else
    {
        pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_LOWLATENCY_MODE, true);
    }

    // Setup PA before call to initialize encoder
    const wchar_t *maxThroughputPropName        = AMF_VIDEO_ENCODER_CAP_MAX_THROUGHPUT;
    const wchar_t *requestedThroughputPropName  = AMF_VIDEO_ENCODER_CAP_REQUESTED_THROUGHPUT;
    bool enablePA = false;
    GetParam(PARAM_NAME_ENABLE_PRE_ANALYSIS, enablePA);

    if (m_szEncoderID == AMFVideoEncoderVCE_AVC)
    {
        pEncoder->SetProperty(AMF_VIDEO_ENCODER_PRE_ANALYSIS_ENABLE, enablePA);
        maxThroughputPropName = AMF_VIDEO_ENCODER_CAP_MAX_THROUGHPUT;
        requestedThroughputPropName = AMF_VIDEO_ENCODER_CAP_REQUESTED_THROUGHPUT;
    }
    else if (m_szEncoderID == AMFVideoEncoder_HEVC)
    {
        pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_PRE_ANALYSIS_ENABLE, enablePA);
        maxThroughputPropName = AMF_VIDEO_ENCODER_HEVC_CAP_MAX_THROUGHPUT;
        requestedThroughputPropName = AMF_VIDEO_ENCODER_HEVC_CAP_REQUESTED_THROUGHPUT;
    }
    else if (m_szEncoderID == AMFVideoEncoder_AV1)
    {
        pEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_PRE_ANALYSIS_ENABLE, enablePA);
        maxThroughputPropName = AMF_VIDEO_ENCODER_AV1_CAP_MAX_THROUGHPUT;
        requestedThroughputPropName = AMF_VIDEO_ENCODER_AV1_CAP_REQUESTED_THROUGHPUT;
    }

    amf_int64 bFrames = 3;
    if (enablePA)
    {
        pEncoder->SetProperty(AMF_PA_ENGINE_TYPE, amf::AMF_MEMORY_OPENCL);
        pEncoder->SetProperty(AMF_PA_LOOKAHEAD_BUFFER_DEPTH, 10);

        if (m_szEncoderID == AMFVideoEncoderVCE_AVC)
        {
            pEncoder->SetProperty(AMF_VIDEO_ENCODER_MAX_CONSECUTIVE_BPICTURES, bFrames);
            pEncoder->SetProperty(AMF_VIDEO_ENCODER_ADAPTIVE_MINIGOP, true);
            pEncoder->SetProperty(AMF_PA_TAQ_MODE, AMF_PA_TAQ_MODE_2);
        }
    }

    uint32_t mbSize = 16; // AVC
    if (m_szEncoderID == AMFVideoEncoder_HEVC || m_szEncoderID == AMFVideoEncoder_AV1)
    {
        mbSize = 64;
    }

    amf_int64 mb_cx = (videoWidth + (mbSize - 1)) / mbSize;
    amf_int64 mb_cy = (videoHeight + (mbSize - 1)) / mbSize;
    amf_int64 throughput = (mb_cx * mb_cy * frameRate.num) / frameRate.den;
    amf_int64 maxThroughput = 0;
    amf_int64 requestedThroughput = 0;

    amf::AMFCapsPtr pCaps;
    res = pEncoder->GetCaps(&pCaps);
    if (res == AMF_OK)
    {
        pCaps->GetProperty(maxThroughputPropName, &maxThroughput);
        pCaps->GetProperty(requestedThroughputPropName, &requestedThroughput);
    }

    if (maxThroughput - requestedThroughput < throughput)
    {
        pEncoder->SetProperty(AMF_PA_TAQ_MODE, AMF_PA_TAQ_MODE_NONE);
        res = pEncoder->GetCaps(&pCaps);
        if (res == AMF_OK)
        {
            pCaps->GetProperty(maxThroughputPropName, &maxThroughput);
            pCaps->GetProperty(requestedThroughputPropName, &requestedThroughput);
        }
    }

    while (maxThroughput - requestedThroughput < throughput &&
            bFrames > 0)
    {
        --bFrames;
        pEncoder->SetProperty(AMF_VIDEO_ENCODER_MAX_CONSECUTIVE_BPICTURES, bFrames);
        res = pEncoder->GetCaps(&pCaps);
        if (res == AMF_OK)
        {
            pCaps->GetProperty(maxThroughputPropName, &maxThroughput);
            pCaps->GetProperty(requestedThroughputPropName, &requestedThroughput);
        }
    }

    // AMF_VIDEO_ENCODER_MAX_CONSECUTIVE_BPICTURES handles turning on / off AMF_VIDEO_ENCODER_ADAPTIVE_MINIGOP
    // but still a good idea to disable it explicitly
    if (bFrames < 1)
    {
        pEncoder->SetProperty(AMF_VIDEO_ENCODER_ADAPTIVE_MINIGOP, false);
    }

    res = pEncoder->Init(amf::AMF_SURFACE_NV12, videoWidth, videoHeight);
    CHECK_AMF_ERROR_RETURN(res, L"m_pEncoder->Init() failed");

//    PushParamsToPropertyStorage(this, ParamEncoderDynamic, m_pEncoder);

    return res;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT DisplayDvrPipeline::InitAudio()
{
    AMF_RESULT res = AMF_OK;

    // Audio state
    amf_int64 codecID = 0;
    amf_int64 streamBitRate = 0;
    amf_int64 streamSampleRate = 0;
    amf_int64 streamChannels = 0;
    amf_int64 streamFormat = 0;
    amf_int64 streamLayout = 0;
    amf_int64 streamBlockAlign = 0;
    amf_int64 streamFrameSize = 0;
    amf::AMFInterfacePtr pExtradata;

    // Create the audio capture component
    res = AMFCreateComponentAudioCapture(m_pContext, &m_pAudioCapture);
    CHECK_AMF_ERROR_RETURN(res, L"Audio capture component creation failed");
    // Put the audio session component into loopback render mode
    res = m_pAudioCapture->SetProperty(AUDIOCAPTURE_SOURCE, false);
    CHECK_AMF_ERROR_RETURN(res, L"Audio capture component did not enter loopback render mode");
    // Set the current time interface
    res = m_pAudioCapture->SetProperty(AUDIOCAPTURE_CURRENT_TIME_INTERFACE, m_pCurrentTime);
    CHECK_AMF_ERROR_RETURN(res, L"Audio capture component failed to set current time interface");
    // Initialize the audio session component
    res = m_pAudioCapture->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
    CHECK_AMF_ERROR_RETURN(res, L"Audio capture component initialization failed");

    // Read the setup of the audio capture component so that its state can be
    // passed into the audio decoder
    m_pAudioCapture->GetProperty(AUDIOCAPTURE_CODEC, &codecID);
    m_pAudioCapture->GetProperty(AUDIOCAPTURE_BITRATE, &streamBitRate);
    m_pAudioCapture->GetProperty(AUDIOCAPTURE_SAMPLERATE, &streamSampleRate);
    m_pAudioCapture->GetProperty(AUDIOCAPTURE_CHANNELS, &streamChannels);
    m_pAudioCapture->GetProperty(AUDIOCAPTURE_FORMAT, &streamFormat);
    m_pAudioCapture->GetProperty(AUDIOCAPTURE_BLOCKALIGN, &streamBlockAlign);
    m_pAudioCapture->GetProperty(AUDIOCAPTURE_FRAMESIZE, &streamFrameSize);

    // Audio converter
    res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*) FFMPEG_AUDIO_CONVERTER, &m_pAudioConverter);
    CHECK_AMF_ERROR_RETURN(res, L"LoadExternalComponent(" << FFMPEG_AUDIO_CONVERTER << L") failed");

    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_BIT_RATE, streamBitRate);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_SAMPLE_RATE, streamSampleRate);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_CHANNELS, streamChannels);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_SAMPLE_FORMAT, streamFormat);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_CHANNEL_LAYOUT, kFFMPEG_AUDIO_LAYOUT_STEREO);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_BLOCK_ALIGN, streamBlockAlign);

    // Audio encoder
    res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*) FFMPEG_AUDIO_ENCODER, &m_pAudioEncoder);
    CHECK_AMF_ERROR_RETURN(res, L"LoadExternalComponent(" << FFMPEG_AUDIO_ENCODER << L") failed");

    m_pAudioEncoder->SetProperty(AUDIO_ENCODER_AUDIO_CODEC_ID, kFFMPEG_AAC_CODEC_ID);
    res = m_pAudioEncoder->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
    CHECK_AMF_ERROR_RETURN(res, L"m_pAudioEncoder->Init() failed");

    m_pAudioEncoder->GetProperty(AUDIO_ENCODER_IN_AUDIO_SAMPLE_RATE, &streamSampleRate);
    m_pAudioEncoder->GetProperty(AUDIO_ENCODER_IN_AUDIO_CHANNELS, &streamChannels);
    m_pAudioEncoder->GetProperty(AUDIO_ENCODER_IN_AUDIO_SAMPLE_FORMAT, &streamFormat);
    m_pAudioEncoder->GetProperty(AUDIO_ENCODER_IN_AUDIO_CHANNEL_LAYOUT, &streamLayout);
    m_pAudioEncoder->GetProperty(AUDIO_ENCODER_IN_AUDIO_BLOCK_ALIGN, &streamBlockAlign);

    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_BIT_RATE, streamBitRate);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_SAMPLE_RATE, streamSampleRate);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_CHANNELS, streamChannels);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_SAMPLE_FORMAT, streamFormat);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_CHANNEL_LAYOUT, streamLayout);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_BLOCK_ALIGN, streamBlockAlign);

    res = m_pAudioConverter->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
    CHECK_AMF_ERROR_RETURN(res, L"m_pAudioConverter->Init() failed");

    return res;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT DisplayDvrPipeline::InitMuxer(
    amf_bool hasDDVideoStream, amf_bool hasSessionAudioStream,
    amf_int32& outVideoStreamMuxerIndex, amf_int32& outAudioStreamMuxerIndex)
{
    AMF_RESULT res = AMF_OK;

    amf::AMFComponentPtr  pMuxer;
    res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*) FFMPEG_MUXER, &pMuxer);
    CHECK_AMF_ERROR_RETURN(res, L"AMFCreateComponent(" << FFMPEG_DEMUXER << L") failed");
    amf::AMFComponentExPtr  pMuxerEx(pMuxer);
    amf_int32 streamIdx = (amf_int32)m_pMuxer.size();
    m_pMuxer.push_back(pMuxerEx);

    std::wstring outputURL = L"";
    res = GetParamWString(PARAM_NAME_URL, outputURL);
    if (outputURL.length() > 0)
    {
        pMuxerEx->SetProperty(FFMPEG_MUXER_URL, outputURL.c_str());
    }
    else
    {
        std::wstring outputPath = L"";
        res = GetParamWString(PARAM_NAME_OUTPUT, outputPath);
        CHECK_AMF_ERROR_RETURN(res, L"Output Path");
        if (m_pEncoder.size() > 1)
        {
            wchar_t buf[100];
            swprintf(buf, amf_countof(buf), L"_%d", (int)m_pMuxer.size() - 1);
            std::wstring::size_type pos = outputPath.find_last_of(L'.');
            std::wstring tmp = outputPath.substr(0, pos);
            tmp += buf;
            tmp += outputPath.substr(pos);
            outputPath = tmp;
        }

        pMuxerEx->SetProperty(FFMPEG_MUXER_PATH, outputPath.c_str());
    }
    if (hasDDVideoStream)
    {
        pMuxerEx->SetProperty(FFMPEG_MUXER_ENABLE_VIDEO, true);
    }
    if (hasSessionAudioStream)
    {
        pMuxerEx->SetProperty(FFMPEG_MUXER_ENABLE_AUDIO, true);
    }

    pMuxerEx->SetProperty(FFMPEG_MUXER_CURRENT_TIME_INTERFACE, m_pCurrentTime);

    amf_int32 inputs = pMuxerEx->GetInputCount();
    for (amf_int32 input = 0; input < inputs; input++)
    {
        amf::AMFInputPtr pInput;
        res = pMuxerEx->GetInput(input, &pInput);
        CHECK_AMF_ERROR_RETURN(res, L"m_pMuxer->GetInput() failed");

        amf_int64 eStreamType = AMF_STREAM_UNKNOWN;
        pInput->GetProperty(AMF_STREAM_TYPE, &eStreamType);

        if (eStreamType == AMF_STREAM_VIDEO)
        {
            outVideoStreamMuxerIndex = input;

            pInput->SetProperty(AMF_STREAM_ENABLED, true);
            amf_int32 bitrate = 0;
            if (m_szEncoderID == AMFVideoEncoderVCE_AVC || m_szEncoderID == AMFVideoEncoderVCE_SVC)
            {
                pInput->SetProperty(AMF_STREAM_CODEC_ID, AMF_STREAM_CODEC_ID_H264_AVC); // default
                m_pEncoder[streamIdx]->GetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, &bitrate);
                pInput->SetProperty(AMF_STREAM_BIT_RATE, bitrate);
                amf::AMFInterfacePtr pExtraData;
                m_pEncoder[streamIdx]->GetProperty(AMF_VIDEO_ENCODER_EXTRADATA, &pExtraData);
                pInput->SetProperty(AMF_STREAM_EXTRA_DATA, pExtraData);

                AMFSize frameSize;
                m_pEncoder[streamIdx]->GetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, &frameSize);
                pInput->SetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, frameSize);

                AMFRate frameRate;
                m_pEncoder[streamIdx]->GetProperty(AMF_VIDEO_ENCODER_FRAMERATE, &frameRate);
                pInput->SetProperty(AMF_STREAM_VIDEO_FRAME_RATE, frameRate);
            }
            else if (m_szEncoderID == AMFVideoEncoder_AV1)
            {
                pInput->SetProperty(AMF_STREAM_CODEC_ID, AMF_STREAM_CODEC_ID_AV1);
                m_pEncoder[streamIdx]->GetProperty(AMF_VIDEO_ENCODER_AV1_TARGET_BITRATE, &bitrate);
                pInput->SetProperty(AMF_STREAM_BIT_RATE, bitrate);
                amf::AMFInterfacePtr pExtraData;
                m_pEncoder[streamIdx]->GetProperty(AMF_VIDEO_ENCODER_AV1_EXTRA_DATA, &pExtraData);
                pInput->SetProperty(AMF_STREAM_EXTRA_DATA, pExtraData);

                AMFSize frameSize;
                m_pEncoder[streamIdx]->GetProperty(AMF_VIDEO_ENCODER_AV1_FRAMESIZE, &frameSize);
                pInput->SetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, frameSize);

                AMFRate frameRate;
                m_pEncoder[streamIdx]->GetProperty(AMF_VIDEO_ENCODER_AV1_FRAMERATE, &frameRate);
                pInput->SetProperty(AMF_STREAM_VIDEO_FRAME_RATE, frameRate);
            }
            else
            {
                pInput->SetProperty(AMF_STREAM_CODEC_ID, AMF_STREAM_CODEC_ID_H265_HEVC);
                m_pEncoder[streamIdx]->GetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, &bitrate);
                pInput->SetProperty(AMF_STREAM_BIT_RATE, bitrate);
                amf::AMFInterfacePtr pExtraData;
                m_pEncoder[streamIdx]->GetProperty(AMF_VIDEO_ENCODER_HEVC_EXTRADATA, &pExtraData);
                pInput->SetProperty(AMF_STREAM_EXTRA_DATA, pExtraData);

                AMFSize frameSize;
                m_pEncoder[streamIdx]->GetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, &frameSize);
                pInput->SetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, frameSize);

                AMFRate frameRate;
                m_pEncoder[streamIdx]->GetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, &frameRate);
                pInput->SetProperty(AMF_STREAM_VIDEO_FRAME_RATE, frameRate);
            }
        }
        else if (eStreamType == AMF_STREAM_AUDIO)
        {
            outAudioStreamMuxerIndex = input;
            pInput->SetProperty(AMF_STREAM_ENABLED, true);

            amf_int64 codecID = 0;
            amf_int64 streamBitRate = 0;
            amf_int64 streamSampleRate = 0;
            amf_int64 streamChannels = 0;
            amf_int64 streamFormat = 0;
            amf_int64 streamLayout = 0;
            amf_int64 streamBlockAlign = 0;
            amf_int64 streamFrameSize = 0;

            m_pAudioEncoder->GetProperty(AUDIO_ENCODER_AUDIO_CODEC_ID, &codecID);
            m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_BIT_RATE, &streamBitRate);
            m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_SAMPLE_RATE, &streamSampleRate);
            m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_CHANNELS, &streamChannels);
            m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_SAMPLE_FORMAT, &streamFormat);
            m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_CHANNEL_LAYOUT, &streamLayout);
            m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_BLOCK_ALIGN, &streamBlockAlign);
            m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_FRAME_SIZE, &streamFrameSize);

            amf::AMFInterfacePtr pExtraData;
            m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_EXTRA_DATA, &pExtraData);
            pInput->SetProperty(AMF_STREAM_EXTRA_DATA, pExtraData);

            pInput->SetProperty(AMF_STREAM_CODEC_ID, codecID);
            pInput->SetProperty(AMF_STREAM_BIT_RATE, streamBitRate);
            pInput->SetProperty(AMF_STREAM_AUDIO_SAMPLE_RATE, streamSampleRate);
            pInput->SetProperty(AMF_STREAM_AUDIO_CHANNELS, streamChannels);
            pInput->SetProperty(AMF_STREAM_AUDIO_FORMAT, streamFormat);
            pInput->SetProperty(AMF_STREAM_AUDIO_CHANNEL_LAYOUT, streamLayout);
            pInput->SetProperty(AMF_STREAM_AUDIO_BLOCK_ALIGN, streamBlockAlign);
            pInput->SetProperty(AMF_STREAM_AUDIO_FRAME_SIZE, streamFrameSize);
        }
    }

    res = pMuxerEx->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
    CHECK_AMF_ERROR_RETURN(res, L"m_pMuxer->Init() failed");

    return res;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT DisplayDvrPipeline::Init()
{
    // Shut down an running pipeline
    Terminate();

    AMF_RESULT res = AMF_OK;

    //---------------------------------------------------------------------------------------------
    // State setup and declarations
    amf_bool hasDDVideoStream = false;
    amf_bool hasSessionAudioStream = false;

    amf_int32 videoWidth = 1920;
    amf_int32 videoHeight = 1080;
    GetParam(PARAM_NAME_VIDEO_WIDTH, videoWidth);
    GetParam(PARAM_NAME_VIDEO_HEIGHT, videoHeight);

    amf_uint32 adapterID = 0;
    GetParam(PARAM_NAME_ADAPTERID, adapterID);

    // The duplicate display functionality used by the DisplayDvr
    // component requires DX11
#if defined(_WIN32)
    std::wstring engineStr = L"DX11";//not used
#else
    std::wstring engineStr = L"Vulkan";
    m_engineMemoryType = amf::AMF_MEMORY_VULKAN;
#endif

    //---------------------------------------------------------------------------------------------
    // Init context and devices
    res = InitContext(engineStr, m_engineMemoryType, adapterID);
    if (AMF_OK != res)
    {
        return res;
    }

    std::vector<amf_uint32> monitorIDs;
    GetMonitorIDs(monitorIDs);

    for (std::vector<amf_uint32>::iterator it = monitorIDs.begin(); it != monitorIDs.end(); it++)
    {
        //---------------------------------------------------------------------------------------------
        // Init video except the muxer
        res = InitVideo(*it, m_engineMemoryType, videoWidth, videoHeight);
        if (AMF_OK == res)
        {
            hasDDVideoStream = true;
        }
        else if (AMF_UNEXPECTED == res)
        {
            SetErrorMessage(L"Unsupported OS.");
            return AMF_FAIL;
        }
    }
    //---------------------------------------------------------------------------------------------
    // Init audio except the muxer. If audio fails to init then we still allow video to
    // be captured.
    res = InitAudio();
    if (AMF_OK == res)
    {
        hasSessionAudioStream = true;
    }

    // Check that we have at least video
    if (!hasDDVideoStream)
    {
        SetErrorMessage(L"No video stream available.");
        return AMF_FAIL;
    }

    //---------------------------------------------------------------------------------------------
    // Init muxer which brings audio and video together
    for (std::vector<amf_uint32>::iterator it = monitorIDs.begin(); it != monitorIDs.end(); it++)
    {
        res = InitMuxer(hasDDVideoStream, hasSessionAudioStream, m_outVideoStreamMuxerIndex, m_outAudioStreamMuxerIndex);
        if (AMF_OK != res)
        {
            return res;
        }
    }
    return ConnectPipeline();
}

AMF_RESULT DisplayDvrPipeline::ConnectPipeline()
{
    PipelineElementPtr nextAudio;
    for (size_t i = 0; i != m_pDisplayCapture.size(); i++)
    {

        amf_int frameParameterFreq = 0;
        amf_int dynamicParameterFreq = 0;

        // Connect components of the video pipeline together
        PipelineElementPtr pPipelineElementVideoEncoder = PipelineElementPtr(new PipelineElementEncoder(m_pEncoder[i], this, frameParameterFreq, dynamicParameterFreq));
        PipelineElementPtr pPipelineElementMuxer = PipelineElementPtr(new AMFComponentExElement(m_pMuxer[i]));

        AMFComponentElementConverterInterceptor* interceptorConverter = new AMFComponentElementConverterInterceptor(this, m_pConverter[i]);
        AMFComponentElementDisplayCaptureInterceptor* interseptorCapture = new AMFComponentElementDisplayCaptureInterceptor((amf_int32)i, interceptorConverter, this, m_pDisplayCapture[i]);

        Connect(PipelineElementPtr(interseptorCapture), 1, CT_Direct);
        Connect(PipelineElementPtr(interceptorConverter), 0, CT_Direct);
        Connect(pPipelineElementVideoEncoder, 0, CT_Direct);
        Connect(pPipelineElementMuxer, m_outVideoStreamMuxerIndex, pPipelineElementVideoEncoder, 0, 10, CT_ThreadQueue);

        if (m_outAudioStreamMuxerIndex >= 0)
        {
            if (i == 0)
            {
                Connect(PipelineElementPtr(new AMFComponentElement(m_pAudioCapture)), 100, CT_ThreadQueue);
                // Connect components of the audio pipeline together
                PipelineElementPtr pPipelineElementAudioEncoder =
                    PipelineElementPtr(new AMFComponentElement(m_pAudioEncoder));

                Connect(PipelineElementPtr(new AMFComponentElement(m_pAudioConverter)), 4, CT_ThreadQueue);
                Connect(pPipelineElementAudioEncoder, 10, CT_Direct);
                if (m_pDisplayCapture.size() > 1)
                {
                    PipelineElementPtr splitter = PipelineElementPtr(new Splitter(true, (amf_int32)m_pDisplayCapture.size(), 1));
                    nextAudio = splitter;
                    Connect(splitter, 2, CT_Direct);
                }
                else
                {
                    nextAudio = pPipelineElementAudioEncoder;
                }
            }

            Connect(pPipelineElementMuxer, m_outAudioStreamMuxerIndex, nextAudio, (amf_int32)i, 10, CT_ThreadQueue);
        }
    }
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT DisplayDvrPipeline::Stop()
{
    Pipeline::Stop();

    if (m_pAudioCapture != NULL)
    {
        m_pAudioCapture->Terminate();
        m_pAudioCapture.Release();
    }
    if (m_pAudioDecoder != NULL)
    {
        m_pAudioDecoder->Terminate();
        m_pAudioDecoder.Release();
    }
    if (m_pAudioConverter != NULL)
    {
        m_pAudioConverter->Terminate();
        m_pAudioConverter.Release();
    }
    if (m_pAudioEncoder != NULL)
    {
        m_pAudioEncoder->Terminate();
        m_pAudioEncoder.Release();
    }

    for (size_t i = 0; i != m_pConverter.size(); i++)
    {
        m_pConverter[i]->Terminate();
    }
    m_pConverter.clear();

    for (size_t i = 0; i != m_pDisplayCapture.size(); i++)
    {
        m_pDisplayCapture[i]->Terminate();
    }
    m_pDisplayCapture.clear();
    for (size_t i = 0; i != m_pEncoder.size(); i++)
    {
        m_pEncoder[i]->Terminate();
    }
    m_pEncoder.clear();
    for (size_t i = 0; i != m_pMuxer.size(); i++)
    {
        m_pMuxer[i]->Terminate();
    }
    m_pMuxer.clear();
    if (m_pContext != NULL)
    {
        m_pContext->Terminate();
        m_pContext.Release();
    }
#if defined(_WIN32)
#if !defined(METRO_APP)
    m_deviceDX9.Terminate();
#endif // !defined(METRO_APP)
    m_deviceDX11.Terminate();
#else
    m_deviceVulkan.Terminate();
#endif

#if defined(_WIN32)
    if (m_useOpenCLConverter)
    {
        m_deviceOpenCL.Terminate();
    }
#endif

    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
void DisplayDvrPipeline::OnParamChanged(const wchar_t* name)
{
    if (m_pMuxer.size() == 0)
    {
        return;
    }
    if (name != NULL)
    {
        if (0 != wcscmp(name, PARAM_NAME_OUTPUT))
        {
            UpdateMuxerFileName();
        }
    }
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT DisplayDvrPipeline::SwitchConverterFormat(amf_int32 index, amf::AMF_SURFACE_FORMAT format)
{
    amf::AMFLock lock(&m_sync);

    // Terminate first
    m_pConverter[index]->Terminate();
    amf_int32 videoWidth = 1920;
    amf_int32 videoHeight = 1080;
    GetParam(PARAM_NAME_VIDEO_WIDTH, videoWidth);
    GetParam(PARAM_NAME_VIDEO_HEIGHT, videoHeight);
    // Init the converter
    m_pConverter[index]->Init(format, videoWidth, videoHeight);
    //
    return  AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT DisplayDvrPipeline::GetMonitorIDs(std::vector<amf_uint32>& monitorIDs)
{
    monitorIDs.clear();

    std::wstring sIDs;
    GetParamWString(PARAM_NAME_MONITORID, sIDs);
    // parse
    if (sIDs.length() == 0)
    {
        return AMF_OK;
    }
    std::wstring::size_type pos = 0;
    while (pos != std::wstring::npos)
    {
        std::wstring::size_type old_pos = pos;
        pos = sIDs.find(L',', pos);
        std::wstring::size_type end = pos == std::wstring::npos ? sIDs.length() : pos;
        std::wstring tmp = sIDs.substr(old_pos, end - old_pos);
        amf_uint32 id = wcstol(tmp.c_str(), 0, 10);
        monitorIDs.push_back(id);
        if (pos != std::wstring::npos)
        {
            pos++;
        }
    }
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT DisplayDvrPipeline::SetMonitorIDs(const std::vector<amf_uint32>& monitorIDs)
{
    std::wstringstream result;
    for (std::vector<amf_uint32>::const_iterator it = monitorIDs.begin(); it != monitorIDs.end(); it++)
    {
        result << *it;
        if (it + 1 != monitorIDs.end())
        {
            result << L",";
        }
    }
    SetParamAsString(PARAM_NAME_MONITORID, result.str());
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT DisplayDvrPipeline::SetEngineMemoryTypes(amf::AMF_MEMORY_TYPE engineMemoryType)
{
    m_engineMemoryType = engineMemoryType;
    return AMF_OK;
}
amf::AMF_MEMORY_TYPE DisplayDvrPipeline::GetEngineMemoryTypes()
{
    return m_engineMemoryType;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT                DisplayDvrPipeline::UpdateMuxerFileName()
{
    for (int i = 0; i < (int)m_pMuxer.size(); i++)
    {
        std::wstring outputPath = L"";
        AMF_RESULT res = GetParamWString(PARAM_NAME_OUTPUT, outputPath);
        CHECK_AMF_ERROR_RETURN(res, L"Output Path");
        if (m_pMuxer.size() > 1)
        {
            wchar_t buf[100];
            swprintf(buf, amf_countof(buf), L"_%d", i);
            std::wstring::size_type pos = outputPath.find_last_of(L'.');
            std::wstring tmp = outputPath.substr(0, pos);
            tmp += buf;
            tmp += outputPath.substr(pos);
            outputPath = tmp;
        }
        m_pMuxer[i]->SetProperty(FFMPEG_MUXER_PATH, outputPath.c_str());
    }
    return AMF_OK;
}