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
// Copyright (c) 2018 Advanced Micro Devices, Inc. All rights reserved.
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
#define AMF_DISABLE_PLAYBACK_EXPORTS

#include "CaptureVideoPipelineBase.h"
#include "public/common/TraceAdapter.h"
#include "public/include/components/FFMPEGComponents.h"
#include "public/include/components/FFMPEGFileDemuxer.h"
#include "public/samples/CPPSamples/common/Options.h"

#pragma warning(disable:4355)

#define AMF_FACILITY L"CaptureVideoPipelineBase"

//parameters
amf::AMF_SURFACE_FORMAT CaptureVideoPipelineBase::m_eComposerOutputDefault = amf::AMF_SURFACE_RGBA; // use if most composer input is RGBA

// class serializes decoder submission to ge all frames from all streams in right order
class CaptureVideoPipelineBase::DecoderSubmissionSync
{
protected:
    std::vector<bool>       m_bStreamState;
    amf::AMFCriticalSection m_sectData;
    bool                    m_bDecoderLocked;
    bool                    m_bLowLatency;
public:
    DecoderSubmissionSync(amf_int32 streamCount, bool bLowLatency) : m_bDecoderLocked(false), m_bLowLatency(bLowLatency)
    {
        m_bStreamState.resize(streamCount, false);
    }
    virtual ~DecoderSubmissionSync()
    {
    }
    void ReInit()
    {
        amf::AMFLock lock(&m_sectData);
        for(std::vector<bool>::iterator it = m_bStreamState.begin(); it != m_bStreamState.end(); it++)
        {
            *it = false;
            m_bDecoderLocked = false;
        }
    }
    bool   Lock(amf_int32 index)
    {
        if(!m_bLowLatency)
        {
            return true;
        }
        amf::AMFLock lock(&m_sectData);
        if(!m_bStreamState[index] && !m_bDecoderLocked)
        {
            m_bDecoderLocked = true;
            m_bStreamState[index] = true;
            return true;
        }
        return false;
    }
    bool   Reset(amf_int32 index)
    {
        if(!m_bLowLatency)
        {
            return true;
        }
        amf::AMFLock lock(&m_sectData);
        m_bStreamState[index] = false;
        m_bDecoderLocked = false;
        return true;
    }
    bool   Unlock()
    {
        if(!m_bLowLatency)
        {
            return true;
        }
        amf::AMFLock lock(&m_sectData);
        if(!m_bDecoderLocked)
        {
            return false;
        }
        m_bDecoderLocked = false;
        for(size_t i = 0; i < m_bStreamState.size(); i++)
        {
            if(!m_bStreamState[i])
            {
                return true; // wait till all streams submit one frame
            }
        }

        for(size_t i = 0; i < m_bStreamState.size(); i++)
        {
            m_bStreamState[i] = false;
        }
        return true;
    }

};

class CaptureVideoPipelineBase::PipelineElementAMFDecoder : public AMFComponentElement
{
public:
    PipelineElementAMFDecoder(amf::AMFComponent *pComponent, amf_int32 index, DecoderSubmissionSync *pDecoderSync, CaptureVideoPipelineBase *pPipeline) :
        AMFComponentElement(pComponent),
        m_pDecoderSync(pDecoderSync),
        m_iIndex(index),
        m_pPipeline(pPipeline),
        m_bEof(false)
    {
    }
    virtual ~PipelineElementAMFDecoder(){}

    virtual AMF_RESULT OnEof()
    {
        m_bEof = true;
        return AMFComponentElement::OnEof();
    }
    virtual void Restart(){m_bEof = false;}


    AMF_RESULT SubmitInput(amf::AMFData* pData)
    {
        AMF_RESULT res = AMF_OK;
        if (pData == NULL || m_bEof) // EOF
        {
            res = m_pComponent->Drain();
        }
        else
        {
            if(!m_pDecoderSync->Lock(m_iIndex))
            {
                return AMF_INPUT_FULL;
            }

            res = m_pComponent->SubmitInput(pData);
            if(res == AMF_OK)
            {
                m_pDecoderSync->Unlock();
            }
            else
            {
                m_pDecoderSync->Reset(m_iIndex);
            }
            if(res == AMF_DECODER_NO_FREE_SURFACES)
            {
                return AMF_INPUT_FULL;
            }
        }
        return res;
    }
    virtual AMF_RESULT QueryOutput(amf::AMFData** ppData)
    {
        AMF_RESULT res = AMFComponentElement::QueryOutput(ppData);
        if(res == AMF_EOF)
        {
            m_pPipeline->EofDecoders();
        }
        return res;
    }

protected:
    CaptureVideoPipelineBase *        m_pPipeline;
    DecoderSubmissionSync*  m_pDecoderSync;
    amf_int32               m_iIndex;
    bool                    m_bEof;
};
//-------------------------------------------------------------------------------------------------
const wchar_t* CaptureVideoPipelineBase::PARAM_NAME_VIDEO_SOURCE_MODE         = L"VideoSourceMode";
const wchar_t* CaptureVideoPipelineBase::PARAM_NAME_VIDEO_SOURCE_DEVICE_INDEX = L"VideoSourceDeviceIndex";
const wchar_t* CaptureVideoPipelineBase::PARAM_NAME_VIDEO_SOURCE_SCALE        = L"VideoSourceScale";
const wchar_t* CaptureVideoPipelineBase::PARAM_NAME_LOOP                      = L"Loop";
const wchar_t* CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY                = L"ChromaKay";
const wchar_t* CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_BK             = L"ChromaKeyBK";
const wchar_t* CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_DEBUG          = L"ChromaKeyDebug";
const wchar_t* CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_RGB            = L"ChromaKeyRGB";
const wchar_t* CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_COLOR_ADJ      = L"ChromaKeyColorAdj";
const wchar_t* CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_SPILL          = L"ChromaKeySpill";
const wchar_t* CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_SCALING        = L"ChromaKeyScaling";
const wchar_t* CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_COLOR          = L"ChromaKeyColor";
const wchar_t* CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_COLOR_EX       = L"ChromaKeyColorEx";
const wchar_t* CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_RGBAFP16       = L"ChromaKeyRGBAFP16";
const wchar_t* CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_10BITLIVE      = L"ChromaKey10BITLIVE";
const wchar_t* CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_ALPHA_SRC      = L"ChromaKeyALPHASRC";

//-------------------------------------------------------------------------------------------------
CaptureVideoPipelineBase::CaptureVideoPipelineBase() :
    m_bIsConnected(false),
    m_bUseBackBufferPresenter(false),
    m_bEnableScaling(true)
{
    g_AMFFactory.Init();
    SetParamDescription(CaptureVideoPipelineBase::PARAM_NAME_VIDEO_SOURCE_MODE,         ParamCommon, L"Video Source Mode(integer, default = 0)", ParamConverterInt64);
    SetParamDescription(CaptureVideoPipelineBase::PARAM_NAME_VIDEO_SOURCE_DEVICE_INDEX, ParamCommon, L"Video Source device name", ParamConverterInt64);
    SetParamDescription(CaptureVideoPipelineBase::PARAM_NAME_VIDEO_SOURCE_SCALE,        ParamCommon, L"Video Source Scaling(integer, default = 100)", ParamConverterInt64);
    SetParamDescription(CaptureVideoPipelineBase::PARAM_NAME_LOOP,                      ParamCommon, L"Loop(bool, default = true)", ParamConverterBoolean);

    SetParamDescription(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY,             ParamCommon, L"Apply Chroma Key(bool, default = false)", ParamConverterBoolean);
    SetParamDescription(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_BK,          ParamCommon, L"Background blending (bool, default = false)", ParamConverterBoolean);
    SetParamDescription(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_DEBUG,       ParamCommon, L"enable debug (bool, default = false)", ParamConverterBoolean);
    SetParamDescription(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_RGB,         ParamCommon, L"enable RGB output (bool, default = true)", ParamConverterBoolean);
    SetParamDescription(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_SPILL,       ParamCommon, L"enable spill supression(bool, default = true)", ParamConverterBoolean);
    SetParamDescription(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_SCALING,     ParamCommon, L"enable zoom in/out(on, off)", ParamConverterBoolean);
    SetParamDescription(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_COLOR_ADJ,   ParamCommon, L"color adjustment mode(off, on, advanced)", ParamConverterInt64);
    SetParamDescription(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_RGBAFP16,    ParamCommon, L"enable FP16 output(on, off)", ParamConverterBoolean);
    SetParamDescription(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_10BITLIVE,   ParamCommon, L"enable 10bit capture(on, off)", ParamConverterBoolean);
    SetParamDescription(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_ALPHA_SRC,   ParamCommon, L"using the alpha data from source, EXR(on, off)", ParamConverterBoolean);

    SetParamDescription(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_COLOR,       ParamCommon, L"ChromaKey key color", ParamConverterInt64);
    SetParamDescription(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_COLOR_EX,    ParamCommon, L"ChromaKey key color, second", ParamConverterInt64);

    //for saving settings
    SetParamDescription(AMF_CHROMAKEY_RANGE_MIN, ParamCommon, L"minimum threshold)", ParamConverterInt64);
    SetParamDescription(AMF_CHROMAKEY_RANGE_MAX, ParamCommon, L"maimum threshold)",  ParamConverterInt64);
    SetParamDescription(AMF_CHROMAKEY_POSX,      ParamCommon, L"H position of foreground image)", ParamConverterInt64);
    SetParamDescription(AMF_CHROMAKEY_POSY,      ParamCommon, L"V position of foreground image)", ParamConverterInt64);

    ResetOptions();
}
//-------------------------------------------------------------------------------------------------
CaptureVideoPipelineBase::~CaptureVideoPipelineBase()
{
    Terminate();
    g_AMFFactory.Terminate();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT CaptureVideoPipelineBase::Terminate()
{
    Stop();
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
void CaptureVideoPipelineBase::ResetOptions()
{
    SetParam(CaptureVideoPipelineBase::PARAM_NAME_LOOP, true);
    SetParam(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY, true);
    SetParam(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_BK, true);
    SetParam(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_RGB, true);
    SetParam(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_SPILL, true);
    SetParam(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_COLOR_ADJ, 1);
    SetParam(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_DEBUG, false);
    SetParam(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_SCALING, true);
    SetParam(CaptureVideoPipelineBase::PARAM_NAME_VIDEO_SOURCE_SCALE, 100);
    SetParam(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_COLOR, 0);
    SetParam(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_COLOR_EX, 0);
    SetParam(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_RGBAFP16, false);
    SetParam(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_10BITLIVE, false);
    SetParam(CaptureVideoPipelineBase::PARAM_NAME_CHROMAKEY_ALPHA_SRC, false);

    //need to match ChromaKeyImpl.cpp
    SetParam(AMF_CHROMAKEY_RANGE_MIN, 8);
    SetParam(AMF_CHROMAKEY_RANGE_MAX, 10);
    SetParam(AMF_CHROMAKEY_POSX, 0);
    SetParam(AMF_CHROMAKEY_POSY, 0);
    SetParam(AMF_CHROMAKEY_COLOR, 0);
    SetParam(AMF_CHROMAKEY_COLOR_EX, 0);
}
//-------------------------------------------------------------------------------------------------
double CaptureVideoPipelineBase::GetProgressSize()
{
    double ret = 0.;
    return ret;
}
//-------------------------------------------------------------------------------------------------
double CaptureVideoPipelineBase::GetProgressPosition()
{
    double ret = 0;
    return ret;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT CaptureVideoPipelineBase::Init()
{
    AMF_RESULT res = AMF_OK;

    amf::AMF_MEMORY_TYPE presenterEngine = amf::AMF_MEMORY_DX11;
    res = InitContext(presenterEngine);
    AMF_RETURN_IF_FAILED(res, L"Create AMF context");

    //---------------------------------------------------------------------------------------------
    // Init Presenter
    amf_int32 compositedWidth = 3860;
    amf_int32 compositedHeight = 2160;

    res = CreateVideoPresenter(presenterEngine, compositedWidth, compositedHeight);
    AMF_RETURN_IF_FAILED(res, L"Failed to create a video presenter");

    bool bEnableFP16 = false;
    GetParam(PARAM_NAME_CHROMAKEY_RGBAFP16, bEnableFP16);   //should be disabled to avoid failure for 10bit video
    m_eComposerOutputDefault = bEnableFP16 ? amf::AMF_SURFACE_RGBA_F16 : amf::AMF_SURFACE_RGBA;
    amf::AMF_SURFACE_FORMAT eComposerOutput = m_eComposerOutputDefault;
//    m_pVideoPresenter->DoActualWait(false);
    m_pVideoPresenter->SetInputFormat(eComposerOutput);
    m_pVideoPresenter->Init(compositedWidth, compositedHeight);

    return InitInternal();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT CaptureVideoPipelineBase::InitInternal()
{
    AMF_RESULT res = AMF_OK;
    amf_int64 modeVideoSource = VIDEO_SOURCE_MODE_INVALID;
    GetParam(PARAM_NAME_VIDEO_SOURCE_MODE, modeVideoSource);

    bool bChromaKey = false;
    GetParam(PARAM_NAME_CHROMAKEY, bChromaKey);

    bool enableBK = true;
    GetParam(PARAM_NAME_CHROMAKEY_BK, enableBK);

    bool enableRGBOut = true;
    GetParam(PARAM_NAME_CHROMAKEY_RGB, enableRGBOut);

    GetParam(PARAM_NAME_CHROMAKEY_SCALING, m_bEnableScaling);   //should be disabled to avoid failure for 10bit video

    amf_int32 compositedWidth = m_pVideoPresenter->GetFrameWidth();
    amf_int32 compositedHeight = m_pVideoPresenter->GetFrameHeight();

    //---------------------------------------------------------------------------------------------
    m_pDecoderSubmissionSync.reset(new DecoderSubmissionSync(enableBK? 2:1, false));

    //init demuxer or capture
    amf::AMFComponentExPtr  pDemuxerEx;
    bool bLowLatency = false;
    bool isCamLive = (modeVideoSource == VIDEO_SOURCE_MODE_CAPTURE);
    AMF_RETURN_IF_FALSE((isCamLive || m_filename.length()), AMF_UNEXPECTED, L"No input!");
    if (!isCamLive)
    {
        amf::AMFComponentPtr  pDemuxer;
        res = InitDemuxer(m_filename, &pDemuxer);
        AMF_RETURN_IF_FAILED(res, L"InitDemuxer() failed!");
        pDemuxerEx = (amf::AMFComponentExPtr)pDemuxer;
        m_stream.m_fileName = m_filename;
    }
    else
    {
        res = InitCapture(amf::AMF_SURFACE_FORMAT::AMF_SURFACE_NV12);
        AMF_RETURN_IF_FAILED(res, L"InitCapture() failed!");
        pDemuxerEx = m_pCaptureDevice;
    }

    amf_int32 iVideoPinIndex = -1;
    amf_int32 iAudioPinIndex = 0;       //audio is not support yet
    res = m_stream.InitStream(m_pContext, pDemuxerEx, iVideoPinIndex, iAudioPinIndex, NULL, bLowLatency);
    AMF_RETURN_IF_FAILED(res, L"InitStream() failed!");

    PipelineElementPtr pPipelineElementDemuxer = PipelineElementPtr(new AMFComponentExElement(m_stream.m_pDemuxer));
    res = Connect(pPipelineElementDemuxer, 0, NULL, 0, 3);

    //init decoder
    PipelineElementPtr  pPrevElement;
    if (m_stream.m_pDecoder != NULL)
    {
        m_stream.m_pDecoderElement = PipelineElementPtr(new PipelineElementAMFDecoder(m_stream.m_pDecoder, 0, m_pDecoderSubmissionSync.get(), this));
        amf_int32 iQueueSize = m_stream.m_bSWDecoder ? 4 : 2;
        res = Connect(m_stream.m_pDecoderElement, 0, pPipelineElementDemuxer, iVideoPinIndex, iQueueSize);
        iVideoPinIndex = 0;
        pPrevElement = m_stream.m_pDecoderElement;
    }
    else
    {
        pPrevElement = pPipelineElementDemuxer;
    }

    amf_int32 scaleRatio = 100;
    GetParam(PARAM_NAME_VIDEO_SOURCE_SCALE, scaleRatio);

    //CSC does not support
    if ((m_stream.m_eVideoDecoderFormat == amf::AMF_SURFACE_FORMAT::AMF_SURFACE_UYVY) ||
        (m_stream.m_eVideoDecoderFormat == amf::AMF_SURFACE_FORMAT::AMF_SURFACE_Y210) ||
        (m_stream.m_eVideoDecoderFormat == amf::AMF_SURFACE_FORMAT::AMF_SURFACE_Y416))
    {
        m_bEnableScaling = false;
    }

    bool bRGBSrc = (m_stream.m_eVideoDecoderFormat == amf::AMF_SURFACE_FORMAT::AMF_SURFACE_RGBA) ||
                   (m_stream.m_eVideoDecoderFormat == amf::AMF_SURFACE_FORMAT::AMF_SURFACE_RGBA_F16);
    bool bSupportedFormat = (m_stream.m_eVideoDecoderFormat == amf::AMF_SURFACE_FORMAT::AMF_SURFACE_NV12) ||
                            (m_stream.m_eVideoDecoderFormat == amf::AMF_SURFACE_FORMAT::AMF_SURFACE_P010) ||
                            (m_stream.m_eVideoDecoderFormat == amf::AMF_SURFACE_FORMAT::AMF_SURFACE_UYVY) ||
                            (m_stream.m_eVideoDecoderFormat == amf::AMF_SURFACE_FORMAT::AMF_SURFACE_Y416) ||
                            (m_stream.m_eVideoDecoderFormat == amf::AMF_SURFACE_FORMAT::AMF_SURFACE_Y210) ||
                            (m_stream.m_eVideoDecoderFormat == amf::AMF_SURFACE_FORMAT::AMF_SURFACE_RGBA_F16);

    if (!bChromaKey && !bRGBSrc)
    {
        res = InitVideoConverter(m_stream.m_eVideoDecoderFormat, m_eComposerOutputDefault,
            m_stream.m_frameSize, AMFConstructSize(compositedWidth, compositedHeight), &m_pConverter);
        AMF_RETURN_IF_FAILED(res, L"InitVideoConverter() failed!");
    }
    else if (!bSupportedFormat)
    {
        res = InitVideoConverter(m_stream.m_eVideoDecoderFormat, amf::AMF_SURFACE_FORMAT::AMF_SURFACE_NV12,
            m_stream.m_frameSize, AMFConstructSize(compositedWidth, compositedHeight), &m_pConverter);
        AMF_RETURN_IF_FAILED(res, L"InitVideoConverter() failed!");
    }
    else if (m_bEnableScaling)
    {
        AMFSize sizeSrc = AMFConstructSize(m_stream.m_frameSize.width * 100 / scaleRatio, m_stream.m_frameSize.height * 100 / scaleRatio);
        res = InitVideoConverter(m_stream.m_eVideoDecoderFormat, m_stream.m_eVideoDecoderFormat,
            m_stream.m_frameSize, sizeSrc, &m_pConverter);
        AMF_RETURN_IF_FAILED(res, L"InitVideoConverter() failed!");
    }

    PipelineElementPtr pConverter(new AMFComponentElement(m_pConverter));

    //---------------------------------------------------------------------------------------------
    if(bChromaKey)
    {
        //background video
        if (enableBK)
        {
            AMF_RETURN_IF_FALSE(m_filenameBK.length(), AMF_UNEXPECTED, L"No background input!");

            //init demuxer
            amf::AMFComponentPtr  pDemuxerBK;
            res = InitDemuxer(m_filenameBK, &pDemuxerBK);
            AMF_RETURN_IF_FAILED(res, L"InitDemuxer() failed!");

            amf_int32 iVideoPinIndexBK = -1;
            amf_int32 iAudioPinIndexBK = 0;       //audio is not support yet
            amf::AMFComponentExPtr  pDemuxerExBK = (amf::AMFComponentExPtr)pDemuxerBK;
            res = m_streamBK.InitStream(m_pContext, pDemuxerExBK, iVideoPinIndexBK, iAudioPinIndexBK, NULL, bLowLatency);
            AMF_RETURN_IF_FAILED(res, L"InitStream() failed!");

            PipelineElementPtr pPipelineElementBKDemuxer = PipelineElementPtr(new AMFComponentExElement(m_streamBK.m_pDemuxer));
            res = Connect(pPipelineElementBKDemuxer, 0, NULL, 0, 3);
            AMF_RETURN_IF_FAILED(res, L"Connect() failed!");

            //init decoder
            m_streamBK.m_pDecoderElement = PipelineElementPtr(new PipelineElementAMFDecoder(m_streamBK.m_pDecoder, 1, m_pDecoderSubmissionSync.get(), this));
            res = Connect(m_streamBK.m_pDecoderElement, 0, pPipelineElementBKDemuxer, iVideoPinIndexBK, 2);
            AMF_RETURN_IF_FAILED(res, L"Connect() failed!");
        }

        if (isCamLive)  //update stream information
        {
            m_stream.Update();
        }

        res = InitChromakeyer(enableBK);
        AMF_RETURN_IF_FAILED(res, L"InitChromakeyer() failed!");

        PipelineElementPtr chromaKeyer = PipelineElementPtr(new AMFComponentExElement(m_pChromaKeyer));

        if (m_pConverter)
        {
            res = Connect(pConverter, 0, pPrevElement, iVideoPinIndex, 2, CT_ThreadQueue);
            pPrevElement = pConverter;
            iVideoPinIndex = 0;
        }

        res = Connect(chromaKeyer, 0, pPrevElement, iVideoPinIndex, 2, CT_ThreadQueue);
        AMF_RETURN_IF_FAILED(res, L"Connect() failed!");

        if (enableBK)
        {
            res = Connect(chromaKeyer, 1, m_streamBK.m_pDecoderElement, 0, 2, CT_ThreadQueue);
            AMF_RETURN_IF_FAILED(res, L"Connect() failed!");
        }

        iVideoPinIndex = 0;
        pPrevElement = chromaKeyer;
    }

    if (m_pVideoPresenter->SupportAllocator())
    {
        if (bChromaKey && enableRGBOut)
        {
            if (!m_bUseBackBufferPresenter)
            {
                m_pVideoPresenter->SetProcessor(m_pChromaKeyer, nullptr, true);
            }
        }
        else if (m_pContext->GetOpenCLContext() == NULL)
        {
            m_pVideoPresenter->SetProcessor(m_pConverter, nullptr, true);
        }
    }

    if ((enableRGBOut && bChromaKey) || !m_pConverter)
    {
        res = Connect(m_pVideoPresenter, 0, pPrevElement, 0,  2, CT_ThreadQueue);
        AMF_RETURN_IF_FAILED(res, L"Connect() failed!");
    }
    else
    {
        res = Connect(pConverter, 0, pPrevElement, iVideoPinIndex,  2, CT_ThreadQueue);
        AMF_RETURN_IF_FAILED(res, L"Connect() failed!");

        res = Connect(m_pVideoPresenter, 0, pConverter, 0, 1, CT_ThreadPoll);
        AMF_RETURN_IF_FAILED(res, L"Connect() failed!");
    }
    m_bIsConnected = true;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT CaptureVideoPipelineBase::InitVideoConverter(
    amf::AMF_SURFACE_FORMAT eFormatIn,
    amf::AMF_SURFACE_FORMAT eFormatOut,
    AMFSize sizeIn,
    AMFSize sizeOut,
    amf::AMFComponent**  ppConverter)
{
    AMF_RESULT res = AMF_OK;
    res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, AMFVideoConverter, ppConverter);
    AMF_RETURN_IF_FAILED(res, L"AMFCreateComponent %s failed", AMFVideoConverter);
    (*ppConverter)->SetProperty(AMF_VIDEO_CONVERTER_KEEP_ASPECT_RATIO, true);
    (*ppConverter)->SetProperty(AMF_VIDEO_CONVERTER_MEMORY_TYPE, amf::AMF_MEMORY_DX11);
    (*ppConverter)->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_FORMAT, eFormatOut);
    (*ppConverter)->SetProperty(AMF_VIDEO_CONVERTER_COLOR_PROFILE, AMF_VIDEO_CONVERTER_COLOR_PROFILE_709);
    (*ppConverter)->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_SIZE, sizeOut);

    res = (*ppConverter)->Init(eFormatIn, sizeIn.width, sizeIn.height);
    AMF_RETURN_IF_FAILED(res, L"(*ppConverter)->Init() failed");
    return res;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT CaptureVideoPipelineBase::InitCapture(
    amf::AMF_SURFACE_FORMAT /* eFormat */)
{
    AMF_RESULT res = AMF_OK;

    amf_int32 index = -1;
    GetParam(PARAM_NAME_VIDEO_SOURCE_DEVICE_INDEX, index);
    if (index >= 0)
    {
        m_pCaptureManager->GetDevice(index, &m_pCaptureDevice);
        if (m_pCaptureDevice != NULL)
        {
            amf_int32 outputCount = m_pCaptureDevice->GetOutputCount();

            for(amf_int32 i = 0; i < outputCount; i++)
            {
                amf_int64 eStreamType = AMF_STREAM_UNKNOWN;
                amf::AMFOutputPtr pOutput;
                m_pCaptureDevice->GetOutput(i, &pOutput);
                pOutput->SetProperty(AMF_STREAM_ENABLED, true);
#if 1
                pOutput->GetProperty(AMF_STREAM_TYPE, &eStreamType);
                if (eStreamType == AMF_STREAM_VIDEO)
                {
                    bool bEnable10bit = false;
                    GetParam(PARAM_NAME_CHROMAKEY_10BITLIVE, bEnable10bit);
                    if (bEnable10bit)
                    {
                        pOutput->SetProperty(AMF_STREAM_VIDEO_FORMAT, amf::AMF_SURFACE_Y210);
                        pOutput->SetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, AMFConstructSize(1920, 1080));  //2160p has issue
                    }
                }
#endif
            }
        }
    }
    return res;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT CaptureVideoPipelineBase::InitDemuxer(
    std::wstring&        sFilename,
    amf::AMFComponent**  ppDemuxer)
{
    AMF_RESULT res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*) FFMPEG_DEMUXER, ppDemuxer);
    AMF_RETURN_IF_FAILED(res, L"AMFCreateComponent  ( %s) failed", FFMPEG_DEMUXER);
    (*ppDemuxer)->SetProperty(FFMPEG_DEMUXER_PATH, sFilename.c_str());
    return res;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT CaptureVideoPipelineBase::InitChromakeyer(bool enableBK)
{
    AMF_RESULT res = AMF_OK;
    amf::AMF_SURFACE_FORMAT captureVideoInputFmt = amf::AMF_SURFACE_BGRA;
    res = AMFCreateComponentChromaKey(m_pContext, &m_pChromaKeyer);
    AMF_RETURN_IF_FAILED(res, L"AMFCreateComponentChromaKey() failed!");

    m_pChromaKeyer->SetProperty(AMF_CHROMAKEY_INPUT_COUNT, enableBK ? 2 : 1);

    amf_int32 flagDebug = 0;
    GetParam(PARAM_NAME_CHROMAKEY_DEBUG, flagDebug);
    m_pChromaKeyer->SetProperty(AMF_CHROMAKEY_DEBUG, flagDebug);

    bool enableSpill = true;
    GetParam(PARAM_NAME_CHROMAKEY_SPILL, enableSpill);
    m_pChromaKeyer->SetProperty(AMF_CHROMAKEY_SPILL_MODE, enableSpill ? 1 : 0);

    bool enableRGBOut = true;
    GetParam(PARAM_NAME_CHROMAKEY_RGB, enableRGBOut);
    m_pChromaKeyer->SetProperty(AMF_CHROMAKEY_OUT_FORMAT, enableRGBOut ? m_eComposerOutputDefault : amf::AMF_SURFACE_NV12);

    amf_int32 greenReducing = 1;
    GetParam(AMF_CHROMAKEY_COLOR_ADJ, greenReducing);
    m_pChromaKeyer->SetProperty(AMF_CHROMAKEY_COLOR_ADJ, greenReducing);

    bool bAlphaFromSrc = false;
    GetParam(PARAM_NAME_CHROMAKEY_ALPHA_SRC, bAlphaFromSrc);
    if (bAlphaFromSrc)   //use alpha data from source
    {
        m_pChromaKeyer->SetProperty(AMF_CHROMAKEY_BYPASS, 4);
    }

    //    m_pChromaKeyer->SetProperty(AMF_CHROMAKEY_MEMORY_TYPE, amf::AMF_MEMORY_OPENCL);
    m_pChromaKeyer->SetProperty(AMF_CHROMAKEY_MEMORY_TYPE, amf::AMF_MEMORY_DX11);

    //load saved chromakey properities
    amf_int32 value = 0;
    if (GetParam(AMF_CHROMAKEY_RANGE_MIN, value) == AMF_OK)
    {
        UpdateChromakeyProperty(AMF_CHROMAKEY_RANGE_MIN, value);
    }

    if (GetParam(AMF_CHROMAKEY_RANGE_MAX, value) == AMF_OK)
    {
        UpdateChromakeyProperty(AMF_CHROMAKEY_RANGE_MAX, value);
    }

    if (GetParam(AMF_CHROMAKEY_POSX, value) == AMF_OK)
    {
        UpdateChromakeyProperty(AMF_CHROMAKEY_POSX, value);
    }

    if (GetParam(AMF_CHROMAKEY_POSY, value) == AMF_OK)
    {
        UpdateChromakeyProperty(AMF_CHROMAKEY_POSY, value);
    }

    if (GetParam(PARAM_NAME_CHROMAKEY_COLOR, value) == AMF_OK)
    {
        UpdateChromakeyProperty(AMF_CHROMAKEY_COLOR, value, false);  //BK bokeh radius, increase
    }

    if (GetParam(PARAM_NAME_CHROMAKEY_COLOR_EX, value) == AMF_OK)
    {
        UpdateChromakeyProperty(AMF_CHROMAKEY_COLOR_EX, value, false);  //BK bokeh radius, increase
    }

    int width = enableBK ? m_streamBK.m_frameSize.width : m_stream.m_frameSize.width;
    int height = enableBK ? m_streamBK.m_frameSize.height : m_stream.m_frameSize.height;
    res = m_pChromaKeyer->Init(captureVideoInputFmt, width, height);
    AMF_RETURN_IF_FAILED(res, L"m_pChromaKeyer->Init failed");
    return res;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT CaptureVideoPipelineBase::Play()
{
    AMF_RESULT res  = AMF_OK;
    if(!m_bIsConnected)
    {
        return AMF_WRONG_STATE;
    }
    switch(GetState())
    {
    case PipelineStateRunning:
        return m_pVideoPresenter->Resume();
    case PipelineStateReady:
        res = Start();
        if(m_pCaptureDevice != NULL && res == AMF_OK)
        {
            m_pCaptureDevice->Start();
        }
        return res;
    case PipelineStateNotReady:
    case PipelineStateEof:
    default:
        break;
    }
    return AMF_WRONG_STATE;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT CaptureVideoPipelineBase::Pause()
{
    switch(GetState())
    {
    case PipelineStateRunning:
        return m_pVideoPresenter->Pause();
    case PipelineStateReady:
    case PipelineStateNotReady:
    case PipelineStateEof:
    default:
        break;
    }
    return AMF_WRONG_STATE;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT CaptureVideoPipelineBase::Step()
{
    if(m_pVideoPresenter != NULL && m_pVideoPresenter->GetMode() ==  VideoPresenter::ModePaused)
    {
        m_pVideoPresenter->Step();
    }
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT CaptureVideoPipelineBase::Stop()
{
    Pipeline::Stop();


    m_stream.Terminate();
    m_streamBK.Terminate();

    if(m_pChromaKeyer!= NULL)
    {
        m_pChromaKeyer->Terminate();
        m_pChromaKeyer.Release();
    }

    if(m_pVideoPresenter != NULL)
    {
        m_pVideoPresenter->Terminate();
        m_pVideoPresenter = NULL;
    }

    if(m_pCaptureDevice != NULL)
    {
        m_pCaptureDevice->Terminate();
        m_pCaptureDevice.Release();
    }

    if (m_pConverter)
    {
        m_pConverter->Terminate();
        m_pConverter.Release();
    }

    if (m_pContext != NULL)
    {
#if DEBUG_D3D11_LEAKS
        ATL::CComQIPtr<ID3D11Debug>  pDebug((ID3D11Device*)m_pContext->GetDX11Device());
#endif
        m_pContext->Terminate();
        m_pContext.Release();
#if DEBUG_D3D11_LEAKS
        if(pDebug != NULL)
        {
            pDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
        }
#endif
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT PlaybackStream::Terminate()
{
    if(m_pDecoder != NULL)
    {
        m_pDecoder->Terminate();
        m_pDecoder.Release();
    }
    if(m_pDemuxer != NULL)
    {
        m_pDemuxer->Terminate();
        m_pDemuxer.Release();
    }

    m_pDecoderElement = NULL;
    m_frameCount = 0;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT PlaybackStream::InitStream(amf::AMFContext *pContext,
    amf::AMFComponentEx      *pDemuxer,
    amf_int32&               iVideoStreamIndex,
    amf_int32&               iAudioStreamIndex,
    ParamAudioCodec*         pParamsAudio,
    bool                     bLowLatency)
{
    AMF_RESULT res = AMF_OK;
    m_pDemuxer = pDemuxer;
    m_frameCount = 0;

    res = m_pDemuxer->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
    AMF_RETURN_IF_FAILED(res, L"m_pDemuxer->Init() failed");

    amf_int32 outputs = m_pDemuxer->GetOutputCount();
    for (amf_int32 output = 0; output < outputs; output++)
    {
        amf::AMFOutputPtr pOutput;
        res = m_pDemuxer->GetOutput(output, &pOutput);
        AMF_RETURN_IF_FAILED(res, L"m_pDemuxer->GetOutput() failed");

        amf_int64 eStreamType = AMF_STREAM_UNKNOWN;
        pOutput->GetProperty(AMF_STREAM_TYPE, &eStreamType);

        if (eStreamType == AMF_STREAM_AUDIO)
        {
            if (iAudioStreamIndex < 0)
            {
                iAudioStreamIndex = output;
                if (pParamsAudio)
                {
                    pOutput->GetProperty(AMF_STREAM_CODEC_ID, &pParamsAudio->codecID);
                    pOutput->GetProperty(AMF_STREAM_BIT_RATE, &pParamsAudio->streamBitRate);
                    pOutput->GetProperty(AMF_STREAM_EXTRA_DATA, &pParamsAudio->pExtradata);
                    pOutput->GetProperty(AMF_STREAM_AUDIO_SAMPLE_RATE, &pParamsAudio->streamSampleRate);
                    pOutput->GetProperty(AMF_STREAM_AUDIO_CHANNELS, &pParamsAudio->streamChannels);
                    pOutput->GetProperty(AMF_STREAM_AUDIO_FORMAT, &pParamsAudio->streamFormat);
                    pOutput->GetProperty(AMF_STREAM_AUDIO_CHANNEL_LAYOUT, &pParamsAudio->streamLayout);
                    pOutput->GetProperty(AMF_STREAM_AUDIO_BLOCK_ALIGN, &pParamsAudio->streamBlockAlign);
                    pOutput->GetProperty(AMF_STREAM_AUDIO_FRAME_SIZE, &pParamsAudio->streamFrameSize);
                }
                pOutput->SetProperty(AMF_STREAM_ENABLED, true);
            }
        }
        else if (iVideoStreamIndex < 0 && eStreamType == AMF_STREAM_VIDEO)
        {
            iVideoStreamIndex = output;
            res = InitVideoDecoder(pContext, pOutput, bLowLatency);
        }
    }
    AMF_RETURN_IF_FALSE(iVideoStreamIndex != -1, AMF_FAIL, L"Video stream not found");
    return res;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT PlaybackStream::Update()
{
    AMF_RESULT res = AMF_OK;
    amf_int32 outputs = m_pDemuxer->GetOutputCount();
    for (amf_int32 output = 0; output < outputs; output++)
    {
        amf::AMFOutputPtr pOutput;
        res = m_pDemuxer->GetOutput(output, &pOutput);
        AMF_RETURN_IF_FAILED(res, L"m_pDemuxer->GetOutput() failed");

        amf_int64 eStreamType = AMF_STREAM_UNKNOWN;
        pOutput->GetProperty(AMF_STREAM_TYPE, &eStreamType);

        if (eStreamType == AMF_STREAM_VIDEO)
        {
            pOutput->GetProperty(AMF_STREAM_VIDEO_FRAME_RATE, &m_frameRate);
            pOutput->GetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, &m_frameSize);
            amf_int64 format = amf::AMF_SURFACE_UNKNOWN;
            pOutput->GetProperty(AMF_STREAM_VIDEO_FORMAT, &format);
            m_eVideoDecoderFormat = amf::AMF_SURFACE_FORMAT(format);
        }
    }
    return res;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT PlaybackStream::InitVideoDecoder(amf::AMFContext *pContext,
    amf::AMFOutput* pOutput,
    bool            bLowLatency)
{
    amf::AMFInterfacePtr pInterfaceExtraData;
    pOutput->GetProperty(AMF_STREAM_EXTRA_DATA, &pInterfaceExtraData);

    pOutput->GetProperty(AMF_STREAM_VIDEO_FRAME_RATE, &m_frameRate);
    pOutput->GetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, &m_frameSize);

    amf_int64 format = amf::AMF_SURFACE_UNKNOWN;
    pOutput->GetProperty(AMF_STREAM_VIDEO_FORMAT, &format);
    m_eVideoDecoderFormat = amf::AMF_SURFACE_FORMAT(format);

    m_pDemuxer->GetProperty(FFMPEG_DEMUXER_FRAME_COUNT, &m_frameCount);
    if (m_frameCount == 0)
    {
        amf_pts duration = 0;
        m_pDemuxer->GetProperty(FFMPEG_DEMUXER_DURATION, &duration);
        double ptsPerFrame = AMF_SECOND / (double)m_frameRate.num * (double)m_frameRate.den;
        m_frameCount = static_cast<amf_uint64>((duration + 0.5) / ptsPerFrame);
    }

    amf_int64 codecID;
    AMF_RESULT res = pOutput->GetProperty(AMF_STREAM_CODEC_ID, &codecID);
    pOutput->SetProperty(AMF_STREAM_ENABLED, true);

    amf_wstring pVideoDecoderID = StreamCodecIDtoDecoderID(AMF_STREAM_CODEC_ID_ENUM(codecID));
    m_bSWDecoder = false;

    if ((m_eVideoDecoderFormat == amf::AMF_SURFACE_YUY2)     ||
        (m_eVideoDecoderFormat == amf::AMF_SURFACE_UYVY)     ||
        (m_eVideoDecoderFormat == amf::AMF_SURFACE_Y210)     ||
        (m_eVideoDecoderFormat == amf::AMF_SURFACE_Y416)     ||
        (m_eVideoDecoderFormat == amf::AMF_SURFACE_RGBA_F16) ||
        (m_eVideoDecoderFormat == amf::AMF_SURFACE_RGBA))
    {
        m_bSWDecoder = true;
    }

    if (pVideoDecoderID.size() == 0)    //try SW decoder
    {
        pOutput->GetProperty(FFMPEG_DEMUXER_VIDEO_CODEC, &codecID);
    }

    // try HW decoder
    if (!m_bSWDecoder && (pVideoDecoderID.size() > 0))
    {
        bool bHWDecoder = false;
        res = g_AMFFactory.GetFactory()->CreateComponent(pContext, pVideoDecoderID.c_str(), &m_pDecoder);
        AMF_RETURN_IF_FAILED(res, L"AMFCreateComponent( %s) failed", pVideoDecoderID.c_str());

        amf::AMFCapsPtr pCaps;
        m_pDecoder->GetCaps(&pCaps);
        if (pCaps != nullptr)
        {
            amf::AMFIOCapsPtr pInputCaps;
            pCaps->GetInputCaps(&pInputCaps);
            if (pInputCaps != nullptr)
            {
                amf_int32 minWidth = 0;
                amf_int32 maxWidth = 0;
                amf_int32 minHeight = 0;
                amf_int32 maxHeight = 0;
                pInputCaps->GetWidthRange(&minWidth, &maxWidth);
                pInputCaps->GetHeightRange(&minHeight, &maxHeight);
                if (minWidth <= m_frameSize.width && m_frameSize.width <= maxWidth && minHeight <= m_frameSize.height && m_frameSize.height <= maxHeight)
                {
                    bHWDecoder = true;
                }
            }
        }

        if (!bHWDecoder)
        {
            m_bSWDecoder = true;
            m_pDecoder = nullptr;
        }
    }

    if (!m_bSWDecoder && m_pDecoder)
    {
        if (bLowLatency)
        {
            m_pDecoder->SetProperty(AMF_VIDEO_DECODER_REORDER_MODE, AMF_VIDEO_DECODER_MODE_LOW_LATENCY);
            m_pDecoder->SetProperty(AMF_VIDEO_DECODER_SURFACE_POOL_SIZE, amf_int64(4));
        }
        else
        {
            m_pDecoder->SetProperty(AMF_VIDEO_DECODER_REORDER_MODE, AMF_VIDEO_DECODER_MODE_REGULAR);
            m_pDecoder->SetProperty(AMF_TIMESTAMP_MODE, amf_int64(AMF_TS_DECODE)); // our sample H264 parser provides decode order timestamps- change depend on demuxer
        }

        m_pDecoder->SetProperty(AMF_VIDEO_DECODER_EXTRADATA, amf::AMFVariant(pInterfaceExtraData));
        res = m_pDecoder->Init(m_eVideoDecoderFormat, m_frameSize.width, m_frameSize.height);
        LOG_AMF_ERROR(res, L"m_pVideoDecoder->Init(" << m_frameSize.width << m_frameSize.height << L") failed " << pVideoDecoderID);

        if (res != AMF_OK)    //try SW decoder
        {
            m_bSWDecoder = true;
            m_pDecoder = nullptr;
        }
    }

    if (m_bSWDecoder && (codecID != AMF_STREAM_CODEC_ID_UNKNOWN))
    {
        res = g_AMFFactory.LoadExternalComponent(pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*)FFMPEG_VIDEO_DECODER, &m_pDecoder);
        CHECK_AMF_ERROR_RETURN(res, L"LoadExternalComponent(" << FFMPEG_VIDEO_DECODER << L") failed");
        ++m_nFfmpegRefCount;

        if (bLowLatency)
        {
            m_pDecoder->SetProperty(AMF_VIDEO_DECODER_REORDER_MODE, AMF_VIDEO_DECODER_MODE_LOW_LATENCY);
            m_pDecoder->SetProperty(AMF_VIDEO_DECODER_SURFACE_POOL_SIZE, amf_int64(4));
        }
        else
        {
            m_pDecoder->SetProperty(AMF_VIDEO_DECODER_REORDER_MODE, AMF_VIDEO_DECODER_MODE_REGULAR);
            m_pDecoder->SetProperty(AMF_TIMESTAMP_MODE, amf_int64(AMF_TS_DECODE)); // our sample H264 parser provides decode order timestamps- change depend on demuxer
        }

        m_pDecoder->SetProperty(VIDEO_DECODER_CODEC_ID, codecID);
        m_pDecoder->SetProperty(VIDEO_DECODER_FRAMERATE, m_frameRate);
        m_pDecoder->SetProperty(AMF_VIDEO_DECODER_EXTRADATA, amf::AMFVariant(pInterfaceExtraData));
        res = m_pDecoder->Init(m_eVideoDecoderFormat, m_frameSize.width, m_frameSize.height);
    }
    else   //SDI capture
    {
        format = amf::AMF_SURFACE_UNKNOWN;
        pOutput->GetProperty(AMF_STREAM_VIDEO_FORMAT, &format);
        m_eVideoDecoderFormat = amf::AMF_SURFACE_FORMAT(format);
    }

    return res;
}

//-------------------------------------------------------------------------------------------------
double CaptureVideoPipelineBase::GetFPS()
{
    if(m_pVideoPresenter != NULL)
    {
        return m_pVideoPresenter->GetFPS();
    }
    return 0;
}
//-------------------------------------------------------------------------------------------------
amf_int64 CaptureVideoPipelineBase::GetFramesDropped()
{
    if(m_pVideoPresenter != NULL)
    {
        return m_pVideoPresenter->GetFramesDropped();
    }
    return 0;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT CaptureVideoPipelineBase::Dump()
{
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT CaptureVideoPipelineBase::Restart()
{
     amf::AMFLock lock(&m_cs);
     m_state = PipelineStateNotReady;

     bool bLoop = true;
     GetParam(CaptureVideoPipelineBase::PARAM_NAME_LOOP, bLoop);
     if(!bLoop)
     {
         return AMF_OK;
     }

    amf::AMFContext::AMFDX11Locker lock2(m_pContext);

    if (!m_stream.m_pDemuxer)
    {
        return AMF_OK;
    }

    if (m_stream.m_pDemuxer)
    {
        m_stream.m_pDemuxer->ReInit(0, 0);
    }

    if (m_stream.m_pDecoder)
    {
        m_stream.m_pDecoder->Flush();
    }

    if (m_stream.m_pDecoderElement)
    {
        ((PipelineElementAMFDecoder*)m_stream.m_pDecoderElement.get())->Restart();
    }

    if (m_streamBK.m_pDemuxer)
    {
        m_streamBK.m_pDemuxer->ReInit(0, 0);
    }

    if (m_streamBK.m_pDecoder)
    {
        m_streamBK.m_pDecoder->Flush();
    }

    if (m_streamBK.m_pDecoderElement)
    {
        ((PipelineElementAMFDecoder*)m_streamBK.m_pDecoderElement.get())->Restart();
    }

    if (m_pConverter)
    {
        m_pConverter->ReInit(m_stream.m_frameSize.width, m_stream.m_frameSize.height);
    }

    if (m_pChromaKeyer)
    {
        m_pChromaKeyer->Flush();
    }

    m_pDecoderSubmissionSync->ReInit();
    m_pVideoPresenter->Reset();
    Pipeline::Restart();

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
void CaptureVideoPipelineBase::EofDecoders()
{
    amf::AMFLock lock(&m_cs);

    if (m_stream.m_pDecoderElement) //Live capture does not have decoder
    {
        m_stream.m_pDecoderElement->OnEof();
    }

    if (m_stream.m_pDecoder)
    {
        m_stream.m_pDecoder->Drain();
    }

    if (m_stream.m_pDemuxer)
    {
        m_stream.m_pDemuxer->Drain();
    }

    if (m_streamBK.m_pDecoder)
    {
        m_streamBK.m_pDecoder->Drain();
    }

    if (m_streamBK.m_pDemuxer)
    {
        m_streamBK.m_pDemuxer->Drain();
    }

    if (m_pChromaKeyer)
    {
        m_pChromaKeyer->Drain();
    }
}
//-------------------------------------------------------------------------------------------------
void CaptureVideoPipelineBase::SelectColorFromPosition(AMFPoint posIn, AMFRect clientRect)
{
    if (m_pChromaKeyer)
    {
        //handle black borders
        amf_int32 width = m_stream.m_frameSize.width;
        amf_int32 height = m_stream.m_frameSize.height;

        //handle different size of background and foreground
        if (width < m_streamBK.m_frameSize.width)
        {
            width = m_streamBK.m_frameSize.width;
            height = m_streamBK.m_frameSize.height;
        }

        amf_float ratioWnd = static_cast<amf_float>(clientRect.Height()) / clientRect.Width();
        amf_float ratioVideo = static_cast<amf_float>(height) / width;
        AMFPoint offset = { 0 };
        amf_float ratioScaling = 0;

        if (ratioWnd < ratioVideo)  //Pillarbox
        {
            ratioScaling = static_cast<amf_float>(height) / clientRect.Height();
            offset.x = (clientRect.Width() - static_cast<amf_int32>(width / ratioScaling)) / 2;
        }
        else  //LetterBox
        {
            ratioScaling = static_cast<amf_float>(width) / clientRect.Width();
            offset.y = (clientRect.Height() - static_cast<amf_int32>(height/ ratioScaling)) / 2;
        }

        AMFPoint pos = { 0 };
        pos.x = static_cast<amf_int32>((posIn.x - offset.x) * ratioScaling);
        pos.y = static_cast<amf_int32>((posIn.y - offset.y) * ratioScaling);
        m_pChromaKeyer->SetProperty(AMF_CHROMAKEY_COLOR_POS, pos);
    }
}
//-------------------------------------------------------------------------------------------------
void CaptureVideoPipelineBase::ToggleChromakeyProperty(const wchar_t* name)
{
    if (m_pChromaKeyer == NULL)
    {
        return;
    }
    bool value = false;
    m_pChromaKeyer->GetProperty(name, &value);
    value = !value;
    m_pChromaKeyer->SetProperty(name, value);
}
//-------------------------------------------------------------------------------------------------
void CaptureVideoPipelineBase::LoopChromakeyProperty(const wchar_t* name, amf_int32 rangeMin, amf_int32 rangeMax)
{
    if (m_pChromaKeyer == NULL)
    {
        return;
    }
    amf_int32 value = 0;
    m_pChromaKeyer->GetProperty(name, &value);
    value++;

    if (value > rangeMax)
    {
        value = rangeMin;
    }
    m_pChromaKeyer->SetProperty(name, value);
}
//-------------------------------------------------------------------------------------------------
amf_int64 CaptureVideoPipelineBase::GetFrameCount()
{
    return (m_stream.m_pDemuxer) ? m_stream.m_frameCount : -1;
}
//-------------------------------------------------------------------------------------------------
void CaptureVideoPipelineBase::UpdateChromakeyProperty(const wchar_t* name, amf_int32 valueNew, bool delta)
{
    if (m_pChromaKeyer == NULL)
    {
        return;
    }

    amf_int32 value = valueNew;

    if (delta)
    {
        amf_int32 valueOld = 0;
        m_pChromaKeyer->GetProperty(name, &valueOld);
        value += valueOld;
    }

    m_pChromaKeyer->SetProperty(name, value);
    m_pChromaKeyer->GetProperty(name, &value);//verify
    SetParam(name, value);	//for saving settings
    AMFTraceInfo(AMF_FACILITY, L"UpdateChromakeyProperty %s, %d", name, (int)value);
}
//-------------------------------------------------------------------------------------------------
void CaptureVideoPipelineBase::UpdateScalingProperty(const wchar_t* name, amf_int32 valueNew, bool delta)
{
    if (m_pConverter == NULL)
    {
        return;
    }

    int scaleRatio = valueNew;
    if (name == CaptureVideoPipelineBase::PARAM_NAME_VIDEO_SOURCE_SCALE)	//change scaling ratio
    {
        if (delta)
        {
            GetParam(CaptureVideoPipelineBase::PARAM_NAME_VIDEO_SOURCE_SCALE, scaleRatio);
            scaleRatio += valueNew;
        }

        scaleRatio = std::max(1, scaleRatio);
        SetParam(CaptureVideoPipelineBase::PARAM_NAME_VIDEO_SOURCE_SCALE, scaleRatio);
        AMFSize sizeSrc = AMFConstructSize(m_stream.m_frameSize.width * 100 / scaleRatio, m_stream.m_frameSize.height * 100 / scaleRatio);
        // need to be algined with 2, for NV12
        sizeSrc.width = sizeSrc.width / 2 * 2;
        sizeSrc.height = sizeSrc.height / 2 * 2;
        m_pConverter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_SIZE, sizeSrc);
    }

    AMFTraceInfo(AMF_FACILITY, L"UpdateScalingProperty %s, %d", name, (int)scaleRatio);
}

//-------------------------------------------------------------------------------------------------
void CaptureVideoPipelineBase::UpdateChromakeyParams()
{
    if (m_pChromaKeyer == NULL)
    {
        return;
    }
    amf_uint64 iKeyColor = 0;
    m_pChromaKeyer->GetProperty(AMF_CHROMAKEY_COLOR, &iKeyColor);
    SetParam(PARAM_NAME_CHROMAKEY_COLOR, static_cast<amf_int32>(iKeyColor));	//for saving settings

    m_pChromaKeyer->GetProperty(AMF_CHROMAKEY_COLOR_EX, &iKeyColor);
    SetParam(PARAM_NAME_CHROMAKEY_COLOR_EX, static_cast<amf_int32>(iKeyColor));	//for saving settings
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
