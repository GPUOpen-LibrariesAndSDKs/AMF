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
#include "StitchPipelineBase.h"

#include "public/common/TraceAdapter.h"
#define AMF_DISABLE_PLAYBACK_EXPORTS
#include "public/include/components/FFMPEGComponents.h"
#include "public/include/components/FFMPEGFileDemuxer.h"
#include "public/include/components/FFMPEGAudioDecoder.h"
#include "public/include/components/FFMPEGAudioEncoder.h"
#include "public/include/components/FFMPEGAudioConverter.h"
#include <fstream>
#define _USE_MATH_DEFINES
#include "math.h"
#include <DirectXMath.h>
#define DEBUG_D3D11_LEAKS 0
#if DEBUG_D3D11_LEAKS
#include <atlbase.h>
#include <d3d11.h>
#include <D3D11SDKLayers.h>
#endif

#include "public/include/core/Dump.h"
#include "public/include/components/FFMPEGFileMuxer.h"
#include "public/include/components/ZCamLiveStream.h"
#include "public/samples/CPPSamples/common/EncoderParamsAVC.h"
#include "public/samples/CPPSamples/common/EncoderParamsHEVC.h"

#include "public/include/components/VideoCapture.h"


using namespace DirectX;
#pragma warning(disable:4355)

#define AMF_FACILITY L"StitchPipelineBase"


// hard- coded parameters to try
static double   dForceFPS = 60.;
#if defined( _M_AMD64)
static int      iDefaultStreamCount = 6;
#else 
static int      iDefaultStreamCount = 6;
#endif
// test parameters
static amf::AMF_SURFACE_FORMAT eComposerInputDefault = amf::AMF_SURFACE_NV12; // best performance with decoder output
amf::AMF_SURFACE_FORMAT StitchPipelineBase::m_eComposerOutputDefault = amf::AMF_SURFACE_RGBA; // use if most composer input is RGBA
//amf::AMF_SURFACE_FORMAT StitchPipelineBase::m_eComposerOutputDefault = amf::AMF_SURFACE_RGBA_F16; // use if most composer input is RGBA

// class serializes decoder submission to ge all frames from all streams in right order 
class StitchPipelineBase::DecoderSubmissionSync
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

class StitchPipelineBase::PipelineElementAMFDecoder : public AMFComponentElement
{
public:
    PipelineElementAMFDecoder(amf::AMFComponent *pComponent, amf_int32 index, DecoderSubmissionSync *pDecoderSync, StitchPipelineBase *pPipeline) :
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
        if(pData == NULL || m_bEof) // EOF
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
    StitchPipelineBase *        m_pPipeline;
    DecoderSubmissionSync*  m_pDecoderSync;
    amf_int32               m_iIndex;
    bool                    m_bEof;
};


StitchElement::StitchElement(amf::AMFComponent *pComponent, amf_int32 inputCount, AMFSize outputSize) :
        m_pComponent(pComponent),
        m_iInputCount(inputCount),
        m_OutputSize(outputSize),
        m_bPause(false)
    {
        m_lastInputs.resize(inputCount);
        m_countFrame = 0;
    }

AMF_RESULT StitchElement::SubmitInput(amf::AMFData* pData, amf_int32 slot)
{ 
    amf::AMFLock lock(&m_cs);
    if(m_bPause)
    {
        return AMF_INPUT_FULL;
    }

    if (pData)
    {
        amf_pts timestamp = amf_high_precision_clock();
        pData->SetProperty(L"StitchStart", timestamp);
    }
    return SubmitInputInt(pData, slot);
}

AMF_RESULT StitchElement::OnEof()
{ 
    Drain(0); 
    return AMF_EOF;
}

AMF_RESULT StitchElement::SubmitInputInt(amf::AMFData* pData, amf_int32 slot)
{ 
    amf::AMFLock lock(&m_cs);

    AMF_RESULT res = AMF_OK;

    amf::AMFSurfacePtr pSurface(pData);
    if(pSurface != NULL)
    {
        amf::AMFInputPtr input;
        m_pComponent->GetInput(slot, &input);
        res = input->SubmitInput(pData);

        if(res == AMF_OK)
        {
            m_lastInputs[slot].data = pData;
        }

    }
    else
    {
        res = m_pComponent->Drain();
    }
    return res;
}
AMF_RESULT StitchElement::QueryOutput(amf::AMFData** ppData)
{
    AMF_RESULT res = AMF_OK;
    amf::AMFDataPtr data;
    res = m_pComponent->QueryOutput(&data);

    if ((res != AMF_REPEAT) && (res != AMF_EOF))
    {
        m_countFrame++;
    }

    if(res == AMF_REPEAT)
    {
        res = AMF_OK;
    }
    if(res == AMF_EOF && data == NULL)
    {
    }
    if(data != NULL)
    {
        amf_pts timestamp = amf_high_precision_clock();
        data->SetProperty(L"StitchEnd", timestamp);
        *ppData = data.Detach();
    }

    return res;
}
AMF_RESULT StitchElement::Drain(amf_int32 inputSlot)
{ 
    amf::AMFLock lock(&m_cs);
    for(std::vector<Data>::iterator it = m_lastInputs.begin(); it != m_lastInputs.end(); it++)
    {
        it->data = NULL;
    }
    return m_pComponent->Drain(); 
}
AMF_RESULT StitchElement::Resubmit()
{
    AMF_RESULT res = AMF_OK;
    amf::AMFLock lock(&m_cs);
    res = m_pComponent->Flush();

    for(amf_int32 i = 0; i <  m_iInputCount; i++)
    {
        amf::AMFDataPtr     data = m_lastInputs[i].data; // need to store this pointer to avoid release during overwrite 
        res = SubmitInputInt(data, i);
        if(res != AMF_OK)
        {
            int a = 1;
        }
    }
    return AMF_OK;
}
AMF_RESULT StitchElement::Pause()
{
    amf::AMFLock lock(&m_cs);
    m_bPause = true;
    return AMF_OK;
}
AMF_RESULT StitchElement::Resume()
{
    amf::AMFLock lock(&m_cs);
    m_bPause = false;
    return AMF_OK;
}

typedef std::shared_ptr<Splitter> SplitterPtr;

const wchar_t* StitchPipelineBase::PARAM_NAME_INPUT      = L"INPUT";
const wchar_t* StitchPipelineBase::PARAM_NAME_PRESENTER  = L"PRESENTER";
const wchar_t* StitchPipelineBase::PARAM_NAME_STREAM_COUNT      = L"StreamCount";
const wchar_t* StitchPipelineBase::PARAM_NAME_COMPOSITED_WIDTH = L"Width";
const wchar_t* StitchPipelineBase::PARAM_NAME_COMPOSITED_HEIGHT = L"Height";
const wchar_t* StitchPipelineBase::PARAM_NAME_LOWLATENCY = L"LowLatency";
const wchar_t* StitchPipelineBase::PARAM_NAME_FRAMERATE = L"FRAMERATE";
const wchar_t* StitchPipelineBase::PARAM_NAME_STITCH_ENGINE = L"StitchEngine";
const wchar_t* StitchPipelineBase::PARAM_NAME_ZCAMLIVE_MODE = L"ZCamLiveMode";
const wchar_t* StitchPipelineBase::PARAM_NAME_OUTPUT_FILE = L"OutputFile";
const wchar_t* StitchPipelineBase::PARAM_NAME_PTS_FILE = L"PtsFile";
const wchar_t* StitchPipelineBase::PARAM_NAME_STREAMING = L"Streaming";
const wchar_t* StitchPipelineBase::PARAM_NAME_LOOP = L"Loop";
const wchar_t* StitchPipelineBase::PARAM_NAME_ENCODER_CODEC = L"CODEC";
const wchar_t* StitchPipelineBase::PARAM_NAME_ENCODE = L"Encode";
const wchar_t* StitchPipelineBase::PARAM_NAME_ENCODE_PREVIEW = L"EncodePreview";
const wchar_t* StitchPipelineBase::PARAM_NAME_STREAMING_URL = L"StreamingUrl";
const wchar_t* StitchPipelineBase::PARAM_NAME_STREAMING_KEY = L"StreamingKey";
const wchar_t* StitchPipelineBase::PARAM_NAME_AUDIO_CH = L"AudioCh";
const wchar_t* StitchPipelineBase::PARAM_NAME_AUDIO_MODE = L"AudioMode";
const wchar_t* StitchPipelineBase::PARAM_NAME_AUDIO_FILE = L"AudioFile";
const wchar_t* StitchPipelineBase::PARAM_NAME_ZCAMLIVE_IP = L"ZCamIP";

StitchPipelineBase::StitchPipelineBase() :
    m_iCurrentChannel(8),
    m_pStitchElement(NULL),
    m_bVideoPresenterDirectConnect(true),
	m_nFfmpegRefCount(0),
    m_mediaInitilized(false),
    m_cameraInitilized(false),
    m_ConnectThread(this)
{
    g_AMFFactory.Init();
    m_sizeInput = { 0 };
    SetParamDescription(StitchPipelineBase::PARAM_NAME_INPUT, ParamCommon, L"Input file name", NULL);
    SetParamDescription(StitchPipelineBase::PARAM_NAME_PRESENTER, ParamCommon, L"Specifies presenter engine type (DX9, DX11, OPENGL)", ParamConverterVideoPresenter);

    wchar_t text[1000];
    swprintf(text,L"INT (1-100) default - %d, number of input streams", iDefaultStreamCount);
    
    SetParamDescription(StitchPipelineBase::PARAM_NAME_STREAM_COUNT, ParamCommon, text, ParamConverterInt64);
    SetParamDescription(StitchPipelineBase::PARAM_NAME_COMPOSITED_WIDTH, ParamCommon, L"Composited Frame width (integer, default = 3840)", ParamConverterInt64);
    SetParamDescription(StitchPipelineBase::PARAM_NAME_COMPOSITED_HEIGHT, ParamCommon, L"Composited Frame height (integer, default = 1920)", ParamConverterInt64);
    SetParamDescription(StitchPipelineBase::PARAM_NAME_LOWLATENCY, ParamCommon, L"Low latency (bool, default = false)", ParamConverterBoolean);

    SetParamDescription(StitchPipelineBase::PARAM_NAME_STITCH_ENGINE, ParamCommon, L"Stitch Engine (integer, default = 1 (Loom)", ParamConverterBoolean);
    SetParamDescription(StitchPipelineBase::PARAM_NAME_ZCAMLIVE_MODE, ParamCommon, L"ZCam live Mode(integer, default = 1)", ParamConverterInt64);
    SetParamDescription(StitchPipelineBase::PARAM_NAME_OUTPUT_FILE, ParamCommon, L"output file name", NULL);
    SetParamDescription(StitchPipelineBase::PARAM_NAME_STREAMING, ParamCommon, L"Streaming(bool, default = false)", ParamConverterBoolean);
    SetParamDescription(StitchPipelineBase::PARAM_NAME_STREAMING_URL, ParamCommon, L"Streaming URL", NULL);
    SetParamDescription(StitchPipelineBase::PARAM_NAME_STREAMING_KEY, ParamCommon, L"Streaming key", NULL);

    SetParamDescription(StitchPipelineBase::PARAM_NAME_LOOP, ParamCommon, L"Loop(bool, default = true)", ParamConverterBoolean);
    SetParamDescription(StitchPipelineBase::PARAM_NAME_ENCODER_CODEC, ParamCommon, L"Codec name (AVC or H264, HEVC or H265)", ParamConverterCodec);
    SetParamDescription(StitchPipelineBase::PARAM_NAME_ENCODE, ParamCommon, L"Encode(bool, default = false)", ParamConverterBoolean);
    SetParamDescription(StitchPipelineBase::PARAM_NAME_ENCODE_PREVIEW, ParamCommon, L"Preview Encode(bool, default = true)", ParamConverterBoolean);
    SetParamDescription(StitchPipelineBase::PARAM_NAME_AUDIO_CH, ParamCommon, L"Audio Channel (integer, default = 0)", ParamConverterInt64);

    SetParamDescription(StitchPipelineBase::PARAM_NAME_AUDIO_MODE, ParamCommon, L"Audio Mode(integer, default = 0)", ParamConverterInt64);
    SetParamDescription(StitchPipelineBase::PARAM_NAME_AUDIO_FILE, ParamCommon, L"audio file name", NULL);

    std::wstring cameraIPName = std::wstring(PARAM_NAME_ZCAMLIVE_IP) + std::wstring(L"_00");
    int cameraIPNameLen = (int)cameraIPName.length() - 1;
    SetParamDescription(cameraIPName.c_str(), ParamCommon, L"CameraIP 00", NULL);
    cameraIPName[cameraIPNameLen] += 1;
    SetParamDescription(cameraIPName.c_str(), ParamCommon, L"CameraIP 01", NULL);
    cameraIPName[cameraIPNameLen] += 1;
    SetParamDescription(cameraIPName.c_str(), ParamCommon, L"CameraIP 02", NULL);
    cameraIPName[cameraIPNameLen] += 1;
    SetParamDescription(cameraIPName.c_str(), ParamCommon, L"CameraIP 03", NULL);

    SetParam(StitchPipelineBase::PARAM_NAME_STREAMING_URL, L"");
    SetParam(StitchPipelineBase::PARAM_NAME_STREAMING_KEY, L"");

    SetParam(StitchPipelineBase::PARAM_NAME_LOOP, true);
    SetParam(StitchPipelineBase::PARAM_NAME_STITCH_ENGINE, false);

    RegisterEncoderParamsAVC(&m_EncoderParamsAVC);
    RegisterEncoderParamsHEVC(&m_EncoderParamsHEVC);

#if defined(METRO_APP)
    SetParam(StitchPipelineBase::PARAM_NAME_PRESENTER, amf::AMF_MEMORY_DX11);
#else
    SetParam(StitchPipelineBase::PARAM_NAME_PRESENTER, amf::AMF_MEMORY_DX11);
#endif
    SetParam(StitchPipelineBase::PARAM_NAME_ENCODE, false);
    SetParam(StitchPipelineBase::PARAM_NAME_ENCODE_PREVIEW, true);

    // Encoder default
    //

    amf_int64 bitrate = 30000000;
    AMFRate frameRate = AMFConstructRate(30, 1);
    amf_int64 gop = 30;

    //SetParam(PARAM_NAME_ENCODER_CODEC, AMFVideoEncoderVCE_AVC);

    m_EncoderParamsAVC.SetParam(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_TRANSCONDING);
    m_EncoderParamsAVC.SetParam(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED);
    m_EncoderParamsAVC.SetParam(AMF_VIDEO_ENCODER_TARGET_BITRATE, bitrate);
    m_EncoderParamsAVC.SetParam(AMF_VIDEO_ENCODER_PEAK_BITRATE, bitrate * 3 /2);
    m_EncoderParamsAVC.SetParam(AMF_VIDEO_ENCODER_FRAMERATE, frameRate);
    m_EncoderParamsAVC.SetParam(AMF_VIDEO_ENCODER_IDR_PERIOD, gop);
    m_EncoderParamsAVC.SetParam(AMF_VIDEO_ENCODER_PROFILE, AMF_VIDEO_ENCODER_PROFILE_MAIN);
    m_EncoderParamsAVC.SetParam(AMF_VIDEO_ENCODER_PROFILE_LEVEL , 51);
    m_EncoderParamsAVC.SetParam(AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0);
    m_EncoderParamsAVC.SetParam(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR);

    m_EncoderParamsHEVC.SetParam(AMF_VIDEO_ENCODER_HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCONDING);
    m_EncoderParamsHEVC.SetParam(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED);
    m_EncoderParamsHEVC.SetParam(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, bitrate);
    m_EncoderParamsHEVC.SetParam(AMF_VIDEO_ENCODER_HEVC_PEAK_BITRATE, bitrate * 3 / 2);
    m_EncoderParamsHEVC.SetParam(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, frameRate);
    m_EncoderParamsHEVC.SetParam(AMF_VIDEO_ENCODER_HEVC_GOP_SIZE, gop);
    m_EncoderParamsHEVC.SetParam(AMF_VIDEO_ENCODER_HEVC_NUM_GOPS_PER_IDR , gop);
    m_EncoderParamsHEVC.SetParam(AMF_VIDEO_ENCODER_HEVC_PROFILE, AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN);
    m_EncoderParamsHEVC.SetParam(AMF_VIDEO_ENCODER_HEVC_TIER, AMF_VIDEO_ENCODER_HEVC_TIER_MAIN);
    m_EncoderParamsHEVC.SetParam(AMF_VIDEO_ENCODER_HEVC_PROFILE_LEVEL, AMF_LEVEL_5_1);
    m_EncoderParamsHEVC.SetParam(AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD, AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR);

    m_Cameras.clear();
}

StitchPipelineBase::~StitchPipelineBase()
{
    Terminate();
	
	for (; m_nFfmpegRefCount > 0; --m_nFfmpegRefCount)
    {
        g_AMFFactory.UnLoadExternalComponent(FFMPEG_DLL_NAME);
    }
	
    g_AMFFactory.Terminate();
}

AMF_RESULT StitchPipelineBase::Terminate()
{
    Stop();
	m_pContext = NULL;
	return AMF_OK;
}

double StitchPipelineBase::GetProgressSize()
{
    double ret = 100.;
    for(std::vector<StitchStream>::iterator it = m_Streams.begin(); it != m_Streams.end(); it++)
    {
        amf_int64 size = 0;
        it->m_pStream->GetSize(&size);
        double retStr = (double)size;
        if(retStr < ret)
        {
            ret = retStr;
        }
    }
    return ret;
}

double StitchPipelineBase::GetProgressPosition()
{
    double ret = 0;
    for(std::vector<StitchStream>::iterator it = m_Streams.begin(); it != m_Streams.end(); it++)
    {
        amf_int64 pos = 0;
        it->m_pStream->GetPosition(&pos);
        double retStr = (double)pos;
        if(retStr > ret)
        {
            ret = retStr;
        }
    }
    return ret;
}

AMF_RESULT StitchPipelineBase::InitMedia(const std::vector<std::wstring> &filenames, bool forceInitilize)
{
    if (m_mediaInitilized && !forceInitilize)
    {
        return AMF_OK;
    }

    AMF_RESULT res = AMF_OK;
    amf::AMFContextPtr pContext;
    g_AMFFactory.GetFactory()->CreateContext(&pContext);
    
    amf_int32 streamCount = (amf_int32)filenames.size();
    GetParam(PARAM_NAME_STREAM_COUNT, streamCount);

    m_Media.resize(streamCount);
    for (amf_int32 ch = 0; ch < streamCount; ch++)
    {
        StitchStream stream;

        std::wstring filename;

        if (filenames.size() == 0)
        {
            ;//            filename = inputPath;
        }
        else if (ch >= (amf_int32)filenames.size())
        {
            //???repeat last one?
            filename = filenames.back();
        }
        else
        {
            filename = filenames[ch];
        }

        ParamAudioCodec paramAudioCodec;
        res = stream.GetMediaInfo(pContext, filename.c_str(), m_Media[ch], paramAudioCodec);
    }
    pContext->Terminate();
    pContext.Release();
    m_mediaInitilized = true;
    return res;
}

std::vector<MediaInfo>*  StitchPipelineBase::UpdateMediaInfo(const wchar_t *pName, amf_int32 ch)
{
    //        MediaInfo mediaInfo;
    StitchStream stream;
    ParamAudioCodec paramAudioCodec;

    if (!m_pContext)
    {
        AMF_RESULT res = InitContext(amf::AMF_MEMORY_DX11);
    }

    stream.GetMediaInfo(m_pContext, pName, m_Media[ch], paramAudioCodec);
    return  &m_Media;
}

AMF_RESULT StitchPipelineBase::InitCamera(const std::wstring &filenamesPTGui, bool forceInitilize)
{
    if (!m_cameraInitilized || forceInitilize)
    {
        ParsePTGuiProject(filenamesPTGui);
        m_cameraInitilized = true;
    }

    return AMF_OK;
}

AMF_RESULT StitchPipelineBase::Init()
{
    AMF_RESULT res = AMF_OK;

    amf::AMF_MEMORY_TYPE presenterEngine = amf::AMF_MEMORY_DX11;
    res = InitContext(presenterEngine);
    AMF_RETURN_IF_FAILED(res, L"Create AMF context");

    amf_int64 modeZCam = CAMLIVE_MODE_INVALID;
    GetParam(PARAM_NAME_ZCAMLIVE_MODE, modeZCam);
    bool isZCamLive = modeZCam != CAMLIVE_MODE_INVALID;


    //---------------------------------------------------------------------------------------------
    // Init Presenter
    amf_int32 compositedWidth = 3840;
    amf_int32 compositedHeight = 1920;
    GetParam(PARAM_NAME_COMPOSITED_WIDTH, compositedWidth);
    GetParam(PARAM_NAME_COMPOSITED_HEIGHT, compositedHeight);

    res = CreateVideoPresenter(presenterEngine, compositedWidth, compositedHeight);
    AMF_RETURN_IF_FAILED(res, L"Failed to create a video presenter");
    amf::AMF_SURFACE_FORMAT eComposerOutput = m_eComposerOutputDefault;
    m_pVideoPresenter->SetInputFormat(eComposerOutput);
    m_pVideoPresenter->Init(compositedWidth, compositedHeight);

    if(isZCamLive)
    {
        return StartConnect();
    }
    return InitInternal();
}
AMF_RESULT StitchPipelineBase::InitInternal()

{
    amf::AMF_MEMORY_TYPE presenterEngine = amf::AMF_MEMORY_DX11;

    amf_int64 modeZCam = CAMLIVE_MODE_INVALID;
    GetParam(PARAM_NAME_ZCAMLIVE_MODE, modeZCam);
    bool isZCamLive = modeZCam != CAMLIVE_MODE_INVALID;
    bool isCombinedSource = (modeZCam == CAMLIVE_MODE_THETAS) || ((m_Cameras.size() == 2 && m_Media.size()==1));
    bool isStitchedSource = (modeZCam == CAMLIVE_MODE_THETAV) || (!isCombinedSource && !isZCamLive && (m_Media.size() == 1));

    GetParam(PARAM_NAME_ZCAMLIVE_MODE, modeZCam);

    AMF_RETURN_IF_FALSE(isZCamLive || (m_Cameras.size() || m_Media.size()), AMF_UNEXPECTED, L"No input!");
    AMF_RETURN_IF_FALSE(isStitchedSource || isCombinedSource || isZCamLive || (m_Cameras.size() == m_Media.size()), AMF_UNEXPECTED, L"Number of media does not match number of camera!");


    AMF_RESULT res = AMF_OK;
    //---------------------------------------------------------------------------------------------
    // Read Options

    amf_int32 streamCount = (amf_int32)m_Cameras.size();
    if (!isZCamLive)
    {
        GetParam(PARAM_NAME_STREAM_COUNT, streamCount);
    }

    if (isCombinedSource || streamCount == 1)
    {
        isCombinedSource = true;
        streamCount = 2;
    }

    bool isStreamingEnabled = false;
    GetParam(PARAM_NAME_STREAMING, isStreamingEnabled);
    std::wstring rtmpUrl = L"";
    if (isStreamingEnabled)
    {
        std::wstring url = L"";
        std::wstring key = L"";
        GetParamWString(PARAM_NAME_STREAMING_URL, url);
        GetParamWString(PARAM_NAME_STREAMING_KEY, key);
        rtmpUrl = url + L"/" + key;
        isStreamingEnabled = (url.length() > 0) ? true : false;
    }
    //---------------------------------------------------------------------------------------------
    // Init context and devices
    bool bUseDirectOutput = false;
   
    amf::AMF_SURFACE_FORMAT eComposerInput = eComposerInputDefault;

    // use this to try OpenCL
//    res = m_pContext->InitOpenCL(NULL);
//    bUseDirectOutput = false;
    //
    amf_int32 compositedWidth = 3840;
    amf_int32 compositedHeight = 1920;
    GetParam(PARAM_NAME_COMPOSITED_WIDTH, compositedWidth);
    GetParam(PARAM_NAME_COMPOSITED_HEIGHT, compositedHeight);
    bool bLowLatency = true;
    GetParam(PARAM_NAME_LOWLATENCY, bLowLatency);
    
    bool bEncode = false;
    GetParam(StitchPipelineBase::PARAM_NAME_ENCODE, bEncode);
    //encoding
    std::wstring fileName;
    GetParamWString(PARAM_NAME_OUTPUT_FILE, fileName);
    bEncode = (bEncode && (fileName.length() > 0)) || isStreamingEnabled;

    bool bPreview = true;
    GetParam(StitchPipelineBase::PARAM_NAME_ENCODE_PREVIEW, bPreview);
    if(!bEncode)
    {
        bPreview = true;
    }

    amf_int32 modeAudio = AMF_VIDEO_STITCH_AUDIO_MODE_ENUM::AMF_VIDEO_STITCH_AUDIO_MODE_NONE;
    GetParam(PARAM_NAME_AUDIO_MODE, modeAudio);
    bool enableAudio = AMF_VIDEO_STITCH_AUDIO_MODE_ENUM::AMF_VIDEO_STITCH_AUDIO_MODE_NONE != modeAudio;
    //---------------------------------------------------------------------------------------------
    //ZCam live
    std::wstring pVideoDecoderID;
    AMFSize frameSize = { 0 };

    if (isZCamLive)
    {
        AMF_RETURN_IF_FALSE(m_pSourceZCam != NULL && m_bIsConnected, AMF_UNEXPECTED, L"Not connected");

        m_pSourceZCam->GetProperty(ZCAMLIVE_VIDEO_FRAMESIZE, &frameSize);

        amf::AMFVariant var;
        res = m_pSourceZCam->GetProperty(ZCAMLIVE_CODEC_ID, &var);
        if (res == AMF_OK)  // video stream setup
        {
            pVideoDecoderID = var.ToWString().c_str();
        }

        if (bLowLatency)
        {
            m_pVideoPresenter->DoActualWait(false);
        }

        m_pSourceZCam->GetProperty(ZCAMLIVE_STREAMCOUNT, &streamCount);
        bLowLatency = true;
    }
    //---------------------------------------------------------------------------------------------
    // Init Video Stitch
    amf::AMF_SURFACE_FORMAT stitchInputFmt = amf::AMF_SURFACE_BGRA;
    res = InitStitcher(stitchInputFmt);
    m_pStitch->SetProperty(AMF_VIDEO_STITCH_COMBINED_SOURCE, isCombinedSource);
    if (!isStitchedSource)
    {
        for (amf_int32 ch = 0; ch < streamCount; ch++)
        {
            amf::AMFInputPtr camera;
            m_pStitch->GetInput(ch, &camera);
            m_Cameras[ch].ApplyToCamera(camera);
        }
    }

    AMF_RETURN_IF_FALSE(isStitchedSource || (m_Cameras.size() == streamCount), AMF_OUT_OF_RANGE, L"stream number does not match project setting!");

    if (isCombinedSource || isStitchedSource)
    {
        streamCount = 1;
    }

    m_pStitchElement = new StitchElement(m_pStitch, streamCount, AMFConstructSize(compositedWidth, compositedHeight));
    PipelineElementPtr compositorElement = PipelineElementPtr(m_pStitchElement);

    //---------------------------------------------------------------------------------------------
    m_pDecoderSubmissionSync.reset(new DecoderSubmissionSync(streamCount, false));
    int inputWidth = 0;
    int inputHeight = 0;

    m_Converters.clear();

    PipelineElementPtr pPipelineElementDemuxerAudio;
    
    amf_int32 audioChannel = enableAudio ? 0 : -1; //passthrough audio index for encoding only for now
    GetParam(StitchPipelineBase::PARAM_NAME_AUDIO_CH, audioChannel);
    audioChannel = (audioChannel >= streamCount) ? 0 : audioChannel;
    amf_int32 streamAudioPinIndex = -1;
    amf_int32 iAudioPinIndex = enableAudio ? -1 : 0; // if outside the loop only first audio will be activated
    PipelineElementPtr pPipelineElementDemuxer;

    for (amf_int32 ch = 0; ch < streamCount; ch++)
    {
        StitchStream stream;

        amf_int32 iVideoPinIndex = -1;
        amf::AMFBufferPtr pExtraData;

        if (isZCamLive)
        {
            stream.m_pSource = m_pSourceZCam;
            iVideoPinIndex = ch;
        }
        else
        {
            std::wstring filename = m_Media[ch].fileName;
            iAudioPinIndex = (audioChannel == ch) ? -1 : 0; //only enable the channel that matches
            res = stream.InitDemuxer(m_pContext, filename.c_str(), pVideoDecoderID, frameSize, 
                0, 0, 
                iVideoPinIndex, iAudioPinIndex, &m_paramsAudio, false, pExtraData);
            AMF_RETURN_IF_FAILED(res, L"stream.InitDemuxer() failed");
        }

        res = stream.InitDecoder(m_pContext, pVideoDecoderID, frameSize, eComposerInput, bLowLatency, pExtraData);
        AMF_RETURN_IF_FAILED(res, L"stream.InitDecoder() failed");

        PipelineElementPtr pPipelineElementDemuxer;
        if (stream.m_pParser != NULL)
        {
            pPipelineElementDemuxer = stream.m_pParser;
        }
        else
        {
            pPipelineElementDemuxer = PipelineElementPtr(new AMFComponentExElement(stream.m_pSource));
        }

        if (ch == 0)
        {
            AMFSize size;
            stream.m_pDecoder->GetProperty(AMF_VIDEO_DECODER_CURRENT_SIZE, &size);
            m_sizeInput = size;
            inputWidth = size.width;
            inputHeight = size.height;
        }

        if (audioChannel == ch)
        {
            m_pSourceAudio = stream.m_pSource;
        }

        res = Connect(pPipelineElementDemuxer, 0, NULL, 0, 3);

        stream.m_pDecoderElement = PipelineElementPtr(new PipelineElementAMFDecoder(stream.m_pDecoder, ch, m_pDecoderSubmissionSync.get(), this));
        res = Connect(stream.m_pDecoderElement, 0, pPipelineElementDemuxer, iVideoPinIndex, 2);

        if (isStitchedSource)
        {
            amf::AMF_SURFACE_FORMAT surfaceFormat = amf::AMF_SURFACE_RGBA;
            m_pStitch->GetProperty(AMF_VIDEO_STITCH_OUTPUT_FORMAT, (amf_int32*)&surfaceFormat);

            amf::AMFComponentPtr pConverter;
            res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, AMFVideoConverter, &pConverter);
            AMF_RETURN_IF_FAILED(res, L"AMFCreateComponent %s failed", AMFVideoConverter);
            pConverter->SetProperty(AMF_VIDEO_CONVERTER_KEEP_ASPECT_RATIO, true);
//            pConverter->SetProperty(AMF_VIDEO_CONVERTER_MEMORY_TYPE, amf::AMF_MEMORY_OPENCL);
            pConverter->SetProperty(AMF_VIDEO_CONVERTER_MEMORY_TYPE, amf::AMF_MEMORY_DX11);
            pConverter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_FORMAT, surfaceFormat);
            pConverter->SetProperty(AMF_VIDEO_CONVERTER_COLOR_PROFILE, AMF_VIDEO_CONVERTER_COLOR_PROFILE_709);
            pConverter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_SIZE, AMFConstructSize(compositedWidth, compositedHeight));

            res = pConverter->Init(amf::AMF_SURFACE_NV12, inputWidth, inputHeight);

            PipelineElementPtr converter(new AMFComponentElement(pConverter));

            res = Connect(converter, 0, stream.m_pDecoderElement, 0, bLowLatency ? 2 : 2, CT_ThreadQueue);
            m_Converters.push_back(pConverter);

            compositorElement = converter;
        }
        else if (stitchInputFmt == amf::AMF_SURFACE_BGRA)
        {
            amf::AMFComponentPtr pConverter;
            res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, AMFVideoConverter, &pConverter);
            AMF_RETURN_IF_FAILED(res, L"AMFCreateComponent %s failed", AMFVideoConverter);
            pConverter->SetProperty(AMF_VIDEO_CONVERTER_KEEP_ASPECT_RATIO, true);
            pConverter->SetProperty(AMF_VIDEO_CONVERTER_MEMORY_TYPE, amf::AMF_MEMORY_OPENCL);
            pConverter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_FORMAT, amf::AMF_SURFACE_BGRA);
            pConverter->SetProperty(AMF_VIDEO_CONVERTER_COLOR_PROFILE, AMF_VIDEO_CONVERTER_COLOR_PROFILE_709);

            res = pConverter->Init(amf::AMF_SURFACE_NV12, inputWidth, inputHeight);

            PipelineElementPtr converter(new AMFComponentElement(pConverter));

            res = Connect(converter, 0, stream.m_pDecoderElement, 0, bLowLatency ? 2 : 2 , CT_ThreadQueue);
            res = Connect(compositorElement, ch, converter, 0, 2);
            m_Converters.push_back(pConverter);
        }
        else
        {
            res = Connect(compositorElement, ch, stream.m_pDecoderElement, 0, bLowLatency ? 2 : 2, CT_ThreadQueue);
        }

        m_Streams.push_back(stream);
    }

    m_pStitch->Init(stitchInputFmt, inputWidth, inputHeight);

    if (enableAudio)
    {
        InitAudio(bEncode);
    }

    if (bEncode)
    {
        AMF_RESULT res = InitVideoEncoder();
        AMF_RETURN_IF_FAILED(res, L"AMFCreateComponent() failed to create a SW/HW Encoder");
        PipelineElementPtr converter(new AMFComponentElement(m_pConverterEncode));
        PipelineElementPtr encoder(new AMFComponentElement(m_pEncoder));

        if(bPreview)
        {
            //splitter
            m_pSplitter = SplitterPtr(new Splitter(true, 2));
            //encoder
            Connect(m_pSplitter, 0, compositorElement, 0, 2);
            Connect(m_pVideoPresenter, 0, m_pSplitter, 0, 2);
            Connect(converter,         0, m_pSplitter, 1, 2);

            //splitter for audio
            if (enableAudio)
            {
                m_pSplitterAudio = SplitterPtr(new Splitter(true, 2));
            }
        }
        else
        {
            Connect(converter,         0, compositorElement, 0, 2, CT_Direct);
        }
        res = Connect(encoder, 0, converter, 0, 1, CT_Direct);
        //muxer
        amf::AMFComponentPtr  pMuxer;
        res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", FFMPEG_MUXER, &pMuxer);
        AMF_RETURN_IF_FAILED(res, L"AMFCreateComponent( %s) failed", FFMPEG_MUXER);
		++m_nFfmpegRefCount;
        m_pMuxer = amf::AMFComponentExPtr(pMuxer);

        if (isStreamingEnabled)
        {
            m_pMuxer->SetProperty(FFMPEG_MUXER_URL, rtmpUrl.c_str());
        }
        else
        {
            m_pMuxer->SetProperty(FFMPEG_MUXER_PATH, fileName.c_str());
        }

        m_pMuxer->SetProperty(FFMPEG_MUXER_ENABLE_VIDEO, true);

        //audio
        amf_int32 outVideoStreamIndex(0);
        amf_int32 outAudioStreamIndex(0);
        if (enableAudio)
        {
            m_pMuxer->SetProperty(FFMPEG_MUXER_ENABLE_AUDIO, true);
        }

        amf_int32 inputs = m_pMuxer->GetInputCount();
        for (amf_int32 input = 0; input < inputs; input++)
        {
            amf::AMFInputPtr pInput;
            res = m_pMuxer->GetInput(input, &pInput);
            AMF_RETURN_IF_FAILED(res, L"m_pMuxer->GetInput() failed");

            amf_int64 eStreamType = AMF_STREAM_UNKNOWN;
            pInput->GetProperty(AMF_STREAM_TYPE, &eStreamType);

            if (eStreamType == AMF_STREAM_VIDEO)
            {
                outVideoStreamIndex = input;
                pInput->SetProperty(AMF_STREAM_ENABLED, true);

                res = SetVideoMuxerParams(pInput);
                AMF_RETURN_IF_FAILED(res, L"SetVideoMuxerParams failed");
            }
            else if (((audioChannel >= 0) || isZCamLive) && (eStreamType == AMF_STREAM_AUDIO) && enableAudio)
            {
                outAudioStreamIndex = input;
                InitAudioMuxer(pInput);
            }
        }
        res = m_pMuxer->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
        AMF_RETURN_IF_FAILED(res, L"m_pMuxer->Init() failed");

        PipelineElementPtr muxer(new AMFComponentExElement(m_pMuxer));
        Connect(muxer, outVideoStreamIndex, encoder, 0, 10, CT_ThreadQueue);

        if (enableAudio)
        {
            InitAudioPipeline(muxer);
        }
    }
    else
    {
        if (m_pVideoPresenter->SupportAllocator() && m_pContext->GetOpenCLContext() == NULL && isStitchedSource)
        {
            amf::AMFComponentPtr pConverter = m_Converters[0];
            m_pVideoPresenter->SetProcessor(pConverter);
        }
        else if (m_pVideoPresenter->SupportAllocator() && m_pContext->GetOpenCLContext() == NULL && !isStitchedSource)
        {
            m_pVideoPresenter->SetProcessor(m_pStitch);
        }

        if (m_bVideoPresenterDirectConnect)
        {
            Connect(m_pVideoPresenter, 0, compositorElement, 0, 1, CT_ThreadPoll);
        }
        else
        {
            Connect(m_pVideoPresenter, 0, compositorElement, 0, 4);
        }

        if (enableAudio)
        {
            InitAudioPipeline();
        }
    }
    m_bIsConnected = true;
    return AMF_OK;
}

AMF_RESULT StitchPipelineBase::Play()
{
    if(!m_bIsConnected)
    {
        return AMF_WRONG_STATE;
    }
    switch(GetState())
    {
    case PipelineStateRunning:
        m_pStitchElement->Resume();
        return m_pVideoPresenter->Resume();
    case PipelineStateReady:
        return Start();
    case PipelineStateNotReady:
    case PipelineStateEof:
    default:
        break;
    }
    return AMF_WRONG_STATE;
}

AMF_RESULT StitchPipelineBase::Pause()
{
    switch(GetState())
    {
    case PipelineStateRunning:
        m_pStitchElement->Pause();
        return m_pVideoPresenter->Pause();
    case PipelineStateReady:
    case PipelineStateNotReady:
    case PipelineStateEof:
    default:
        break;
    }
    return AMF_WRONG_STATE;
}

AMF_RESULT StitchPipelineBase::Step()
{
    if(m_pVideoPresenter != NULL && m_pVideoPresenter->GetMode() ==  VideoPresenter::ModePaused)
    {
        m_pStitchElement->Resume();
        m_pVideoPresenter->Step();
    }
    return AMF_OK;
}

AMF_RESULT StitchPipelineBase::Stop()
{
    m_ConnectThread.RequestStop();
    m_ConnectThread.WaitForStop();

    Pipeline::Stop();

    
    for(std::vector<StitchStream>::iterator it = m_Streams.begin(); it != m_Streams.end(); it++)
    {
        it->Terminate();
    }
    m_Streams.clear();


    if(m_pStitch!= NULL)
    {
        m_pStitchElement = NULL;
        m_pStitch->Terminate();
        m_pStitch.Release();
    }

    if(m_pVideoPresenter != NULL)
    {
        m_pVideoPresenter->Terminate();
        m_pVideoPresenter = NULL;
    }

    if (m_pMuxer != NULL)
    {
        m_pMuxer->Terminate();
        m_pMuxer.Release();
    }

    if (m_pEncoder != NULL)
    {
        m_pEncoder->Terminate();
        m_pEncoder.Release();
    }

    if (m_pConverterEncode != NULL)
    {
        m_pConverterEncode->Terminate();
        m_pConverterEncode.Release();
    }

    if (m_pAudioCap != NULL)
    {
        m_pAudioCap->Terminate();
        m_pAudioCap.Release();
    }

    if (m_pSourceAudio != NULL)
    {
        m_pSourceAudio->Terminate();
        m_pSourceAudio.Release();
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

    if (m_pAudioConverterEncode != NULL)
    {
        m_pAudioConverterEncode->Terminate();
        m_pAudioConverterEncode.Release();
    }

    if (m_pAudioEncoder != NULL)
    {
        m_pAudioEncoder->Terminate();
        m_pAudioEncoder.Release();
    }

    m_pAudioPresenter = NULL;
    m_pSplitterAudio = NULL;

    m_Converters.clear();

    m_pSplitter = NULL;

    if(m_pSourceZCam != NULL)
    {
        m_pSourceZCam->Terminate();
        m_pSourceZCam.Release();
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

    g_AMFFactory.UnLoadExternalComponent(STITCH_DLL_NAME);
    return AMF_OK;
}

AMF_RESULT StitchStream::Terminate()
{
    m_pParser = NULL;
    if(m_pDecoder != NULL)
    {
        m_pDecoder->Terminate();
        m_pDecoder.Release();
    }
    if(m_pSource != NULL)
    {
        m_pSource->Terminate();
        m_pSource.Release();
    }

    return AMF_OK;
}

AMF_RESULT StitchStream::InitDemuxer(
    amf::AMFContext *pContext,
    const wchar_t *filename,
    std::wstring& pVideoDecoderID,
    AMFSize& frameSize,
    amf_int64  startTime,          // Starting encoder at timestamp startTime (secs)
    amf_int64  endTime,            // Stopping encoder at timestamp endTime (secs)
    amf_int32& iVideoStreamIndex,
    amf_int32& iAudioStreamIndex,
    ParamAudioCodec* pParamsAudio,
    bool infoOnly,
    amf::AMFBufferPtr& pExtraData)
{
    AMF_RESULT res = AMF_OK;
    //---------------------------------------------------------------------------------------------
    // Init Video Stream Parser

    BitStreamType streamType = GetStreamType(filename);

    amf_int64 startFrameNum = 0;
    amf_int64 selectedFrameCount = 0;

    if (streamType != BitStreamUnknown)
    {
        amf::AMFDataStream::OpenDataStream(filename, amf::AMFSO_READ, amf::AMFFS_SHARE_READ, &m_pStream);
        AMF_RETURN_IF_FALSE(m_pStream != NULL, AMF_FILE_NOT_OPEN, L"Open File");
        m_pParser = BitStreamParser::Create(m_pStream, streamType, pContext);
        AMF_RETURN_IF_FALSE(m_pParser != NULL, AMF_FILE_NOT_OPEN, L"BitStreamParser::Create");

        m_pParser->SetFrameRate(dForceFPS); // Force 60 FPS on elementary stream to test performance
        iVideoStreamIndex = 0;

        const unsigned char* extraData = m_pParser->GetExtraData();
        size_t extraDataSize = m_pParser->GetExtraDataSize();
        if (extraData && extraDataSize)
        {
            pContext->AllocBuffer(amf::AMF_MEMORY_HOST, extraDataSize, &pExtraData);
            memcpy(pExtraData->GetNative(), extraData, extraDataSize);
        }
        frameSize.width = m_pParser->GetPictureWidth();
        frameSize.height = m_pParser->GetPictureHeight();
        pVideoDecoderID = m_pParser->GetCodecComponent();
    }
    else
    {
        amf::AMFComponentPtr  pDemuxer;
        res = g_AMFFactory.LoadExternalComponent(pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", FFMPEG_DEMUXER, &pDemuxer);
        AMF_RETURN_IF_FAILED(res, L"AMFCreateComponent  ( %s) failed", FFMPEG_DEMUXER);
		++m_nFfmpegRefCount;
        m_pSource = amf::AMFComponentExPtr(pDemuxer);

        m_pSource->SetProperty(FFMPEG_DEMUXER_PATH, filename);

        //TODO
        // HARDCODED FOR NOW, LATER ON SHOULD QUERY FRAMERATE
        AMFRate frameRate = { 30, 1 };
        startFrameNum = startTime * frameRate.num / frameRate.den;
        amf_int64 endFrameCount = endTime * frameRate.num / frameRate.den;
        selectedFrameCount = endFrameCount - startFrameNum;
        AMF_RETURN_IF_FALSE(selectedFrameCount >= 0, AMF_EOF, L"EndTime should be later than StartTime");

        // Seek portion of video frames per component
        if (startFrameNum > 0 && selectedFrameCount > 0)
        {
            m_pSource->SetProperty(FFMPEG_DEMUXER_START_FRAME, startFrameNum);
            m_pSource->SetProperty(FFMPEG_DEMUXER_FRAME_COUNT, selectedFrameCount);
        }

        res = m_pSource->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
        AMF_RETURN_IF_FAILED(res, L"m_pSource->Init() failed");

        amf_int64 frameCount = 0;
        m_pSource->GetProperty(FFMPEG_DEMUXER_FRAME_COUNT, &frameCount);
        m_pSource->GetProperty(FFMPEG_DEMUXER_DURATION, &m_duration);
        
        amf_int32 outputs = m_pSource->GetOutputCount();
        for (amf_int32 output = 0; output < outputs; output++)
        {
            amf::AMFOutputPtr pOutput;
            res = m_pSource->GetOutput(output, &pOutput);
            AMF_RETURN_IF_FAILED(res, L"m_pSource->GetOutput() failed");

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
                amf::AMFInterfacePtr pInterface;
                pOutput->GetProperty(AMF_STREAM_EXTRA_DATA, &pInterface);
                pExtraData = amf::AMFBufferPtr(pInterface);

                pOutput->GetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, &frameSize);
                frameSize.width = frameSize.width;
                frameSize.height = frameSize.height;
                m_sizeInput = frameSize;

                amf_int64 codecID;
                res= pOutput->GetProperty(AMF_STREAM_CODEC_ID, &codecID);
                pVideoDecoderID = StreamCodecIDtoDecoderID(AMF_STREAM_CODEC_ID_ENUM(codecID));
                pOutput->SetProperty(AMF_STREAM_ENABLED, true);
                pOutput->GetProperty(AMF_STREAM_VIDEO_FRAME_RATE, &m_framerate);
            }
        }

    }
    AMF_RETURN_IF_FALSE(iVideoStreamIndex != -1, AMF_FAIL, L"Video stream not found");
    AMF_RETURN_IF_FALSE(pVideoDecoderID.length() != 0, AMF_FAIL, L"Video codec not found");
    return res;
}

AMF_RESULT StitchStream::InitDecoder(
    amf::AMFContext *pContext,
    std::wstring pVideoDecoderID,
    AMFSize frameSize,
    amf::AMF_SURFACE_FORMAT eComposerInput,
    bool bLowLatency,
    amf::AMFBufferPtr pExtraData)
{
    AMF_RESULT res = AMF_OK;
    ;
    //---------------------------------------------------------------------------------------------
    // Init Video Decoder
    // decode options to be played with
    res = g_AMFFactory.GetFactory()->CreateComponent(pContext, pVideoDecoderID.c_str(), &m_pDecoder);

    AMF_RETURN_IF_FAILED(res, L"AMFCreateComponent( %s) failed", pVideoDecoderID);

    if (bLowLatency)
    {
        res = m_pDecoder->SetProperty(AMF_VIDEO_DECODER_REORDER_MODE, AMF_VIDEO_DECODER_MODE_LOW_LATENCY);
        res = m_pDecoder->SetProperty(AMF_VIDEO_DECODER_SURFACE_POOL_SIZE, amf_int64(4));
    }
    else
    {
        res = m_pDecoder->SetProperty(AMF_VIDEO_DECODER_REORDER_MODE, AMF_VIDEO_DECODER_MODE_REGULAR);
    }
    res = m_pDecoder->SetProperty(AMF_TIMESTAMP_MODE, amf_int64(AMF_TS_DECODE)); // our sample H264 parser provides decode order timestamps- change depend on demuxer
    res = m_pDecoder->SetProperty(AMF_VIDEO_DECODER_EXTRADATA, amf::AMFVariant(pExtraData));
    res = m_pDecoder->Init(eComposerInput, frameSize.width, frameSize.height);
    return res;
}

AMF_RESULT StitchStream::GetMediaInfo(amf::AMFContext *pContext,
    const wchar_t *filename,
    MediaInfo& mediaInfo,
    ParamAudioCodec &paramsAudio)
{
    AMF_RESULT res = AMF_OK;
    //---------------------------------------------------------------------------------------------
    // Init Video Stream Parser

    BitStreamType streamType = GetStreamType(filename);

    std::wstring pVideoDecoderID;
    mediaInfo.fileName = filename;

    if (streamType != BitStreamUnknown)
    {
        amf::AMFDataStream::OpenDataStream(filename, amf::AMFSO_READ, amf::AMFFS_SHARE_READ, &m_pStream);
        AMF_RETURN_IF_FALSE(m_pStream != NULL, AMF_FILE_NOT_OPEN, L"Open File");
        m_pParser = BitStreamParser::Create(m_pStream, streamType, pContext);
        AMF_RETURN_IF_FALSE(m_pParser != NULL, AMF_FILE_NOT_OPEN, L"BitStreamParser::Create");
        mediaInfo.framerate.den = 100;
        mediaInfo.framerate.num  = amf_int32(m_pParser->GetFrameRate()*100.0);
        mediaInfo.sizeInput.width = m_pParser->GetPictureWidth();
        mediaInfo.sizeInput.height = m_pParser->GetPictureHeight();
        mediaInfo.codecID = m_pParser->GetCodecComponent();
        mediaInfo.duration = 0;
    }
    else
    {
        amf::AMFComponentPtr  pDemuxer;
        res = g_AMFFactory.LoadExternalComponent(pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", FFMPEG_DEMUXER, &pDemuxer);
        AMF_RETURN_IF_FAILED(res, L"AMFCreateComponent( %s ) failed", FFMPEG_DEMUXER);
		++m_nFfmpegRefCount;

        pDemuxer->SetProperty(FFMPEG_DEMUXER_PATH, filename);
        res = pDemuxer->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
        AMF_RETURN_IF_FAILED(res, L"m_pSource->Init() failed");

//        pDemuxer->GetProperty(FFMPEG_DEMUXER_FRAME_COUNT, &mediaInfo.frameCount);
        pDemuxer->GetProperty(FFMPEG_DEMUXER_DURATION, &mediaInfo.duration);

        amf::AMFComponentExPtr  pDemuxerEx = amf::AMFComponentExPtr(pDemuxer);
        amf_int32 outputs = pDemuxerEx->GetOutputCount();
        for (amf_int32 output = 0; output < outputs; output++)
        {
            amf::AMFOutputPtr pOutput;
            res = pDemuxerEx->GetOutput(output, &pOutput);
            AMF_RETURN_IF_FAILED(res, L"m_pSource->GetOutput() failed");

            amf_int64 eStreamType = AMF_STREAM_UNKNOWN;
            pOutput->GetProperty(AMF_STREAM_TYPE, &eStreamType);


            if (eStreamType == AMF_STREAM_AUDIO)
            {
                pOutput->GetProperty(AMF_STREAM_CODEC_ID, &paramsAudio.codecID);
                pOutput->GetProperty(AMF_STREAM_BIT_RATE, &paramsAudio.streamBitRate);
                pOutput->GetProperty(AMF_STREAM_EXTRA_DATA, &paramsAudio.pExtradata);
                pOutput->GetProperty(AMF_STREAM_AUDIO_SAMPLE_RATE, &paramsAudio.streamSampleRate);
                pOutput->GetProperty(AMF_STREAM_AUDIO_CHANNELS, &paramsAudio.streamChannels);
                pOutput->GetProperty(AMF_STREAM_AUDIO_FORMAT, &paramsAudio.streamFormat);
                pOutput->GetProperty(AMF_STREAM_AUDIO_CHANNEL_LAYOUT, &paramsAudio.streamLayout);
                pOutput->GetProperty(AMF_STREAM_AUDIO_BLOCK_ALIGN, &paramsAudio.streamBlockAlign);
                pOutput->GetProperty(AMF_STREAM_AUDIO_FRAME_SIZE, &paramsAudio.streamFrameSize);
            }
            else if (eStreamType == AMF_STREAM_VIDEO)
            {
                pOutput->GetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, &mediaInfo.sizeInput);

                amf_int64 codecID;
                res= pOutput->GetProperty(AMF_STREAM_CODEC_ID, &codecID);
                pVideoDecoderID = StreamCodecIDtoDecoderID(AMF_STREAM_CODEC_ID_ENUM(codecID));
                pOutput->GetProperty(AMF_STREAM_VIDEO_FRAME_RATE, &mediaInfo.framerate);
            }
        }

        pDemuxer->Terminate();
        pDemuxer.Release();
    }
    return res;
}
    double              StitchPipelineBase::GetFPS()
{
    if(m_pVideoPresenter != NULL)
    {
        return m_pVideoPresenter->GetFPS();
    }
    return 0;
}

amf_int64           StitchPipelineBase::GetFramesDropped()
{
    if(m_pVideoPresenter != NULL)
    {
        return m_pVideoPresenter->GetFramesDropped();
    }
    return 0;
}

void StitchPipelineBase::ChangeParameter(const wchar_t *pName, bool bPlus, double step)
{
    if(m_pStitch == NULL)
    {
        return;
    }

    if(m_iCurrentChannel >= (int)m_Streams.size())
    {
        for(int i = 0; i < (int)m_Streams.size(); i++)
        {
            ChangeParameterChannel(i, pName, bPlus, step);
        }
    }
    else
    {
        ChangeParameterChannel(m_iCurrentChannel, pName, bPlus, step);
    }
}
void StitchPipelineBase::ChangeParameterChannel(int channel, const wchar_t *pName, bool bPlus, double step)
{
    amf::AMFInputPtr input;
    m_pStitch->GetInput(channel, &input);
    if(input == NULL)
    {
        return;
    }
    double value = 0;
    input->GetProperty(pName, &value);
    if(bPlus)
    {
        value += step;
    }
    else
    {
        value -= step;
    }
    input->SetProperty(pName, value);
    if(m_pVideoPresenter->GetMode() ==  VideoPresenter::ModePaused)
    {
        m_pStitchElement->Resubmit();
        m_pVideoPresenter->Step();
        m_pStitchElement->Resubmit();
        m_pVideoPresenter->Step();
    }
}

void StitchPipelineBase::ChangeK1(bool bPlus, bool bAccel)
{
    ChangeParameter(AMF_VIDEO_STITCH_LENS_CORR_K1, bPlus, (bAccel ? 10.0 :  1.0) / 1000.0);
}
void StitchPipelineBase::ChangeK2(bool bPlus, bool bAccel)
{
    ChangeParameter(AMF_VIDEO_STITCH_LENS_CORR_K2, bPlus, (bAccel ? 10.0 :  1.0) / 1000.0);
}
void StitchPipelineBase::ChangeK3(bool bPlus, bool bAccel)
{
    ChangeParameter(AMF_VIDEO_STITCH_LENS_CORR_K3, bPlus, (bAccel ? 10.0 :  1.0) / 1000.0);
}
void StitchPipelineBase::ChangeZoom(bool bPlus, bool bAccel)
{
    int channel = m_iCurrentChannel;
    if(channel >= (int)m_Streams.size())
    {
        channel = 0;
    }
    ChangeParameter(AMF_VIDEO_CAMERA_SCALE, bPlus, (bAccel ? 0.01 : 0.001));
}
//-------------------------------------------------------------------------------------------------
void StitchPipelineBase::ChangeOffsetX(bool bPlus, bool bAccel)
{
    int channel = m_iCurrentChannel;
    if(channel >= (int)m_Streams.size())
    {
        channel = 0;
    }

    ChangeParameter(AMF_VIDEO_CAMERA_OFFSET_X, bPlus, (bAccel ? 10.0 :  1.0) );// in pixels
}
//-------------------------------------------------------------------------------------------------
void StitchPipelineBase::ChangeOffsetY(bool bPlus, bool bAccel)
{
    int channel = m_iCurrentChannel;
    if(channel >= (int)m_Streams.size())
    {
        channel = 0;
    }
    ChangeParameter(AMF_VIDEO_CAMERA_OFFSET_Y, bPlus, (bAccel ? 10.0 :  1.0) ); // in pixels
}
//-------------------------------------------------------------------------------------------------
void StitchPipelineBase::ChangeCameraPitch(bool bPlus, bool bAccel)
{
    ChangeParameter(AMF_VIDEO_CAMERA_ANGLE_PITCH, bPlus, (bAccel ? 10.0 :  1.0)  /180. * XM_PI / 4.0);
}
//-------------------------------------------------------------------------------------------------
void StitchPipelineBase::ChangeCameraYaw(bool bPlus, bool bAccel)
{
    ChangeParameter(AMF_VIDEO_CAMERA_ANGLE_YAW, bPlus, (bAccel ? 10.0 :  1.0)  /180. * XM_PI / 4.0);
}
//-------------------------------------------------------------------------------------------------
void StitchPipelineBase::ChangeCameraRoll(bool bPlus, bool bAccel)
{
    ChangeParameter(AMF_VIDEO_CAMERA_ANGLE_ROLL, bPlus, (bAccel ? 10.0 :  1.0) /180. * XM_PI / 4.0);
}
//-------------------------------------------------------------------------------------------------
void StitchPipelineBase::SetChannel(int channel)
{
    m_iCurrentChannel = channel;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT StitchTemplate::ApplyToCamera(amf::AMFPropertyStorage *camera)
{
    AMF_RESULT res = AMF_OK;

    res = camera->SetProperty(AMF_VIDEO_STITCH_LENS_MODE, lensMode);

    res = camera->SetProperty(AMF_VIDEO_CAMERA_OFFSET_X, offsetX);
    res = camera->SetProperty(AMF_VIDEO_CAMERA_OFFSET_Y, offsetY);
    res = camera->SetProperty(AMF_VIDEO_CAMERA_OFFSET_Z, offsetZ);

    res = camera->SetProperty(AMF_VIDEO_CAMERA_SCALE, scale);
    res = camera->SetProperty(AMF_VIDEO_CAMERA_HFOV, hfov);
    res = camera->SetProperty(AMF_VIDEO_CAMERA_ANGLE_PITCH, pitch);
    res = camera->SetProperty(AMF_VIDEO_CAMERA_ANGLE_YAW, yaw);
    res = camera->SetProperty(AMF_VIDEO_CAMERA_ANGLE_ROLL, roll);  

    res = camera->SetProperty(AMF_VIDEO_STITCH_LENS_CORR_K1, lensK1); 
    res = camera->SetProperty(AMF_VIDEO_STITCH_LENS_CORR_K2, lensK2);
    res = camera->SetProperty(AMF_VIDEO_STITCH_LENS_CORR_K3, lensK3);
    res = camera->SetProperty(AMF_VIDEO_STITCH_LENS_CORR_OFFX, lensOffX);
    res = camera->SetProperty(AMF_VIDEO_STITCH_LENS_CORR_OFFY, lensOffY);

    res = camera->SetProperty(AMF_VIDEO_STITCH_CROP, crop);
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
void StitchTemplate::Clear()
{
    pitch = 0.;   // in radians
    yaw = 0.;     // in radians
    roll = 0.;    // in radians

    offsetX = 0.; // in pixels
    offsetY = 0.; // in pixels
    offsetZ=0.; // in pixels
    hfov=0;    // M_PI / 2.0
    scale=0;   // 1.0

    lensK1=0.;
    lensK2=0.;
    lensK3=0.;
    lensOffX=0.;
    lensOffY=0.;

    overlayFile[0]=0;
    overlaySize = AMFConstructSize(0, 0);

    lensMode = AMF_VIDEO_STITCH_LENS_RECTILINEAR;

    crop = AMFConstructRect(0, 0, 0, 0);
}
static wchar_t * Properties[] =
{
    AMF_VIDEO_CAMERA_ANGLE_PITCH,
    AMF_VIDEO_CAMERA_ANGLE_YAW,
    AMF_VIDEO_CAMERA_ANGLE_ROLL,

    AMF_VIDEO_CAMERA_OFFSET_X,
    AMF_VIDEO_CAMERA_OFFSET_Y,
    AMF_VIDEO_CAMERA_OFFSET_Z,
    AMF_VIDEO_CAMERA_HFOV,


    AMF_VIDEO_STITCH_LENS_CORR_K1,
    AMF_VIDEO_STITCH_LENS_CORR_K2,
    AMF_VIDEO_STITCH_LENS_CORR_K3,

    AMF_VIDEO_STITCH_LENS_MODE,
    AMF_VIDEO_STITCH_CROP,

};
AMF_RESULT StitchPipelineBase::Dump()
{
    amf::AMFDataStreamPtr    pDataStream;
    amf::AMFDataStream::OpenDataStream(L"template.txt", amf::AMFSO_WRITE, amf::AMFFS_SHARE_READ, &pDataStream);

    char buf[1000];
    int pos;

    pos = sprintf(buf, "static StitchTemplate m_Cameras = \n");
    pDataStream->Write(buf, pos, NULL);
    pos = sprintf(buf, "{\n");
    pDataStream->Write(buf, pos, NULL);

    for(int channel = 0; channel < (int)m_Streams.size(); channel++)
    {
        amf::AMFInputPtr input;
        m_pStitch->GetInput(channel, &input);
        if(input == NULL)
        {
            return AMF_OK;
        }

        pos = sprintf(buf, "{ // channel = %d\n", channel);
        pDataStream->Write(buf, pos, NULL);

        for(int i = 0; i< _countof(Properties); i++)
        {

            if(std::wstring(AMF_VIDEO_STITCH_LENS_MODE) == std::wstring(Properties[i]))
            { 
                amf_int64 value = 0;
                input->GetProperty(Properties[i], &value);
                switch(value)
                {
                case     AMF_VIDEO_STITCH_LENS_RECTILINEAR:
                    pos = sprintf(buf, "%s, // %S\n", "AMF_VIDEO_STITCH_LENS_RECTILINEAR", Properties[i]);
                    break;
                case     AMF_VIDEO_STITCH_LENS_FISHEYE_FULLFRAME:
                    pos = sprintf(buf, "%s, // %S\n", "AMF_VIDEO_STITCH_LENS_FISHEYE_FULLFRAME", Properties[i]);
                    break;
                case AMF_VIDEO_STITCH_LENS_FISHEYE_CIRCULAR:
                    pos = sprintf(buf, "%s, // %S\n", "AMF_VIDEO_STITCH_LENS_FISHEYE_CIRCULAR", Properties[i]);
                    break;
                }
            }
            else if (std::wstring(AMF_VIDEO_STITCH_CROP) == std::wstring(Properties[i]))
            {
                AMFRect crop;
                input->GetProperty(Properties[i], &crop);
                pos = sprintf(buf, "{%df, %df, %df, %df} // %S\n", (int)crop.left, (int)crop.top, (int)crop.right, (int)crop.bottom, Properties[i]);
            }
            else
            { 
                double value = 0;
                input->GetProperty(Properties[i], &value);
                pos = sprintf(buf, "%.6f, // %S\n", value, Properties[i]);
            }
            pDataStream->Write(buf, pos, NULL);
        }
        pos = sprintf(buf, "},\n");
        pDataStream->Write(buf, pos, NULL);

        pos = sprintf(buf, "\n\n");
        pDataStream->Write(buf, pos, NULL);
    }
    pos = sprintf(buf, "};\n");
    pDataStream->Write(buf, pos, NULL);

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT StitchPipelineBase::Restart()
{
     amf::AMFLock lock(&m_cs);

     bool bLoop = true;
     GetParam(StitchPipelineBase::PARAM_NAME_LOOP, bLoop);
     if(!bLoop)
     {
         return AMF_OK;
     }

    if (m_Streams.size() > 0)
    {
        amf::AMFContext::AMFDX11Locker lock(m_pContext);

        for (std::vector<StitchStream>::iterator it = m_Streams.begin(); it != m_Streams.end(); it++)
        {
            if (it->m_pParser != NULL)
            {
                it->m_pParser->ReInit();
            }
            else
            {
                it->m_pSource->ReInit(0, 0);
            }

            it->m_pDecoder->Flush();
            ((PipelineElementAMFDecoder*)it->m_pDecoderElement.get())->Restart();
        }

        for (amf_int32 idx = 0; idx < (int)m_Converters.size(); idx++)
        {
            if (m_Converters[idx] != NULL)
            {
                m_Converters[idx]->ReInit(m_sizeInput.width, m_sizeInput.height);
            }
        }

        m_pStitch->Flush();
        m_pDecoderSubmissionSync->ReInit();
        m_pVideoPresenter->Reset();
        if(m_pSplitter!= NULL)
        {
            m_pSplitter->Flush();
        }
        amf_int32 compositedWidth = 3840;
        amf_int32 compositedHeight = 1920;
        GetParam(PARAM_NAME_COMPOSITED_WIDTH, compositedWidth);
        GetParam(PARAM_NAME_COMPOSITED_HEIGHT, compositedHeight);

        if(m_pEncoder != NULL)
        {
            m_pEncoder->Flush();
        }
        if(m_pConverterEncode != NULL)
        {
            m_pConverterEncode->ReInit(compositedWidth, compositedHeight);
        }
        Pipeline::Restart();
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
void StitchPipelineBase::EofDecoders()
{
    amf::AMFLock lock(&m_cs);
    for(std::vector<StitchStream>::iterator it = m_Streams.begin(); it != m_Streams.end(); it++)
    {
        it->m_pDecoderElement->OnEof();
    }
    m_pStitch->Drain();
}

//-------------------------------------------------------------------------------------------------
int ReadOneValueAndAttributeInt(std::string &text, std::string::size_type  &pos, char &attribute)
{
    int value = 0;
    while(pos < text.length())
    {
        if(text[pos] == ' ')
        {
            pos++;
            continue;
        }
        attribute = text[pos];
        pos++;
        if(pos >= text.length())
        {
            break;
        }
        int tmp = 0;
        sscanf(&text[pos], "%d", &tmp);
        value = (amf_int32)tmp;
        while(pos < text.length())
        {
            if(text[pos] == ' ')
            {
                break;
            }
            pos++;
        }
        break;
    }
    return value;
}
//-------------------------------------------------------------------------------------------------
int ReadOneValueInt(std::string &text, std::string::size_type  &pos)
{
    int value = 0;
    while(pos < text.length())
    {
        if (text[pos] == ' ' || text[pos] == ',')
        {
            pos++;
            continue;
        }
        int tmp = 0;
        sscanf(&text[pos], "%d", &tmp);
        value = tmp;
        while(pos < text.length())
        {
            if (text[pos] == ' ' || text[pos] == ',')
            {
                break;
            }
            pos++;
        }
        break;
    }
    return value;
}

char ReadOneAttribute(std::string &text, std::string::size_type  &pos)
{
    char attribute = 0;
    while (pos < text.length())
    {
        if (text[pos] == ' ' || text[pos] == ',')
        {
            pos++;
            continue;
        }
        attribute = text[pos];
        pos++;
        break;
    }
    return attribute;
}

double ReadOneValueAndAttributeDouble(std::string &text, std::string::size_type  &pos, char &attribute)
{
    double value = 0;
    while(pos < text.length())
    {
        if (text[pos] == ' ' || text[pos] == ',')
        {
            pos++;
            continue;
        }
        attribute = text[pos];
        pos++;
        if(pos >= text.length())
        {
            break;
        }
        double tmp = 0;
        sscanf(&text[pos], "%lf", &tmp);
        value = tmp;
        while(pos < text.length())
        {
            if (text[pos] == ' ' || text[pos] == ',')
            {
                break;
            }
            pos++;
        }
        break;
    }
    return value;
}
double ReadOneValueDouble(std::string &text, std::string::size_type  &pos)
{
    double value = 0;
    while(pos < text.length())
    {
        if(text[pos] == ' ')
        {
            pos++;
            continue;
        }
        double tmp = 0;
        sscanf(&text[pos], "%lf", &tmp);
        value = tmp;
        while(pos < text.length())
        {
            if(text[pos] == ' ')
            {
                break;
            }
            pos++;
        }
        break;
    }
    return value;
}
//-------------------------------------------------------------------------------------------------
void ReadOneImage(std::string &text, StitchTemplate &t)
{
    int lensType = 0;
    bool bFOV = false;
    std::string::size_type  pos = 1; // skip first '0'
    while (pos < text.length())
    {
        if (text[pos] == ' ')
        {
            pos++;
            continue;
        }
        // from here: http://wiki.panotools.org/PTStitcher
        char attribute = ReadOneAttribute(text, pos);

        double valueDbl = 0;
        double valueInt = 0;

        switch (attribute)
        {
        case 'f':
            lensType = ReadOneValueInt(text, pos);
            //MM TODO integer: use projection format:
            // 0 - rectilinear (normal lenses) 
            // 1 - Panoramic (Scanning cameras like Noblex)   
            // 2 - Circular fisheye
            // 3 - full-frame fisheye
            // 4 - PSphere, equirectangular
            break;
        case 'y':
            valueDbl = ReadOneValueDouble(text, pos);
            t.yaw = valueDbl * M_PI / 180.;
            break;
        case 'r':
            valueDbl = ReadOneValueDouble(text, pos);
            t.roll = valueDbl * M_PI / 180.;
            break;
        case 'p':
            valueDbl = ReadOneValueDouble(text, pos);
            t.pitch = valueDbl * M_PI / 180.;
            break;
        case 'a':
            valueDbl = ReadOneValueDouble(text, pos);
            if (valueDbl != 0)
            {
                t.lensK1 = valueDbl;
            }
            break;
        case 'b':
            valueDbl = ReadOneValueDouble(text, pos);
            if (valueDbl != 0)
            {
                t.lensK2 = valueDbl;
            }
            break;
        case 'c':
            valueDbl = ReadOneValueDouble(text, pos);
            if (valueDbl != 0)
            {
                t.lensK3 = valueDbl;
            }
            break;
        case 'd':
            valueDbl = ReadOneValueDouble(text, pos);
            if (valueDbl != 0)
            {
                t.lensOffX = valueDbl;
                t.offsetX = 0;
            }
            break;
        case 'e':
            valueDbl = ReadOneValueDouble(text, pos);
            if (valueDbl != 0)
            {
                t.lensOffY = valueDbl;
                t.offsetY = 0;
            }
            break;
        case 'v':
            valueDbl = ReadOneValueDouble(text, pos);
            if (valueDbl != 0)
            {
                t.hfov = valueDbl * M_PI / 180.;
                bFOV = true;
                t.offsetZ = 0;
            }
            break;
        case 'C':
            t.crop.left = ReadOneValueInt(text, pos);
            t.crop.right = ReadOneValueInt(text, pos);
            t.crop.top = ReadOneValueInt(text, pos);
            t.crop.bottom = ReadOneValueInt(text, pos);
            break;
        }
    }
    t.scale = 1.0;
    if (bFOV)
    {
        switch (lensType)
        {
        case 0: //  rectilinear (normal lenses) 
            t.lensMode = AMF_VIDEO_STITCH_LENS_RECTILINEAR;
            break;
        case 2: // Circular fisheye
            t.lensMode = AMF_VIDEO_STITCH_LENS_FISHEYE_CIRCULAR;
            break;
        case 3: // full-frame fisheye
            t.lensMode = AMF_VIDEO_STITCH_LENS_FISHEYE_FULLFRAME;
            break;
        }
        //MM TODO support more
    }
}
//-------------------------------------------------------------------------------------------------
void StitchPipelineBase::ParsePTGuiProject(const std::wstring &ptguifilename)
{
    if(ptguifilename.length() == 0)
    {
        return;
    }
    AMF_RESULT res = AMF_OK;

    //support upto 32 cameras    
    m_Cameras.resize(32);

    std::fstream stm;
    stm.open(ptguifilename, std::fstream::in );
    if(!stm.is_open())
    {
        AMFTraceWarning(AMF_FACILITY, L"Cannot open file %s", ptguifilename.c_str());
        return;
    }
    // start parsing
    while(true)
    {
        std::string line;
        if(!std::getline(stm, line))
        {
            break; //EOF
        }

        if(line == "# input images:")
        {
            bool bDummyImage = false;
            int index = 0;
            m_Cameras[index].Clear();
            while (true)
            { 
                if(!std::getline(stm, line))
                {
                    break; //EOF
                }
                if(line.length() == 0)
                {
                     break; // end of control points
                }
                if(line == "#-dummyimage")
                {
                    bDummyImage = true;
                    continue;
                }
                if(line[0] == 'o' && line[0] != 'O')
                {
                    if(bDummyImage)
                    {
                        bDummyImage = false;
                        ReadOneImage(line, m_Cameras[0]);
                        for (int i = 1; i < (int)m_Cameras.size(); i++)
                        {
                            memcpy(&m_Cameras[i], &m_Cameras[0], sizeof(m_Cameras[0]));
                        }
                        continue;
                    }
                    ReadOneImage(line, m_Cameras[index]);
                    index++;
                    if (index >= (int)m_Cameras.size())
                    {
                        break;
                    }
                }
            }        
            m_Cameras.resize(index);
        }
    }
}

AMF_RESULT StitchPipelineBase::InitVideoEncoder()
{
    AMF_RESULT res = AMF_FAIL;

    std::wstring codec = AMFVideoEncoderVCE_AVC;
    GetParamWString(StitchPipelineBase::PARAM_NAME_ENCODER_CODEC, codec);

    ParametersStorage            *encoderParams = NULL;
    if(codec == AMFVideoEncoderVCE_AVC)
    {
        encoderParams = &m_EncoderParamsAVC;
    }
    else if(codec == AMFVideoEncoder_HEVC)
    {
        encoderParams = &m_EncoderParamsHEVC;
    }
    else
    {
        AMF_RETURN_IF_FAILED(AMF_INVALID_ARG, L"Wrong Encoder ID");
    }

    amf::AMF_SURFACE_FORMAT encoderInputFormat = amf::AMF_SURFACE_UNKNOWN;
    {
        res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, codec.c_str(), &m_pEncoder);
        AMF_RETURN_IF_FAILED(res, L"g_AMFFactory.GetFactory()->CreateComponent(%s ) failed", codec);

        encoderInputFormat = amf::AMF_SURFACE_NV12;
    }
    
    amf_int32 compositedWidth = 3840;
    amf_int32 compositedHeight = 1920;
    GetParam(PARAM_NAME_COMPOSITED_WIDTH, compositedWidth);
    GetParam(PARAM_NAME_COMPOSITED_HEIGHT, compositedHeight);

    //BGRA to NV12 / YUV420P
    res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, AMFVideoConverter, &m_pConverterEncode);
    AMF_RETURN_IF_FAILED(res, L"AMFCreateComponent( %s) failed", AMFVideoConverter);
    m_pConverterEncode->SetProperty(AMF_VIDEO_CONVERTER_KEEP_ASPECT_RATIO, true);
    m_pConverterEncode->SetProperty(AMF_VIDEO_CONVERTER_MEMORY_TYPE, (encoderInputFormat == amf::AMF_SURFACE_YUV420P) ? amf::AMF_MEMORY_OPENCL : amf::AMF_MEMORY_DX11);
    m_pConverterEncode->SetProperty(AMF_VIDEO_CONVERTER_COMPUTE_DEVICE, (encoderInputFormat == amf::AMF_SURFACE_YUV420P) ? amf::AMF_MEMORY_OPENCL : amf::AMF_MEMORY_COMPUTE_FOR_DX11);
    m_pConverterEncode->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_FORMAT, encoderInputFormat);

    //    m_pConverterEncode->Init(amf::AMF_SURFACE_BGRA, compositedWidth, compositedHeight);
    m_pConverterEncode->Init(m_eComposerOutputDefault, compositedWidth, compositedHeight);


    // Usage is preset that will set many parameters
    PushParamsToPropertyStorage(encoderParams, ParamEncoderUsage, m_pEncoder);
    // override some usage parameters
    PushParamsToPropertyStorage(encoderParams, ParamEncoderStatic, m_pEncoder);
    
    res = m_pEncoder->Init(encoderInputFormat, compositedWidth, compositedHeight);
    AMF_RETURN_IF_FAILED(res, L"m_pEncoder->Init() failed");

    PushParamsToPropertyStorage(encoderParams, ParamEncoderDynamic, m_pEncoder);

    return res;

}

AMF_RESULT StitchPipelineBase::SetVideoMuxerParams(amf::AMFInput* const pInput)
{
    amf_int32 bitrate = 0;
    AMFSize frameSize = {0,0};
    AMFRate frameRate = {0,0};
    amf::AMFInterfacePtr pExtraData;

    std::wstring codec = AMFVideoEncoderVCE_AVC;
    GetParamWString(StitchPipelineBase::PARAM_NAME_ENCODER_CODEC, codec);

    ParametersStorage            *encoderParamsAVC = NULL;
    if(codec == AMFVideoEncoderVCE_AVC)
    {
        m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, &bitrate);
        m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_EXTRADATA, &pExtraData);
        m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, &frameSize);
        m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_FRAMERATE, &frameRate);

    }
    else if(codec == AMFVideoEncoder_HEVC)
    {
        m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, &bitrate);
        m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_EXTRADATA, &pExtraData);
        m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, &frameSize);
        m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, &frameRate);
    }
    else
    {
        return AMF_FAIL;
    }

    pInput->SetProperty(AMF_STREAM_BIT_RATE, bitrate);
    pInput->SetProperty(AMF_STREAM_EXTRA_DATA, pExtraData);
    pInput->SetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, frameSize);
    pInput->SetProperty(AMF_STREAM_VIDEO_FRAME_RATE, frameRate);

    return AMF_OK;
}

AMF_RESULT StitchPipelineBase::GetProperty(int channel, wchar_t* paramName, double& value)
{
    AMF_RESULT ret = AMF_OK;
    if (channel >= (int)m_Streams.size())
    {
        return AMF_FAIL;
    }

    if (m_pStitch)
    {
        amf::AMFInputPtr input;
        m_pStitch->GetInput(channel, &input);
        ret = input->GetProperty(paramName, &value);
    }

    return ret;
}
AMF_RESULT StitchPipelineBase::SetProperty(int channel, wchar_t* paramName, double value)
{
    if (channel >= (int)m_Streams.size())
    {
        return AMF_FAIL;
    }

    AMF_RESULT ret = AMF_OK;

    if (m_pStitch)
    {
        amf::AMFInputPtr input;
        m_pStitch->GetInput(channel, &input);
        ret = input->SetProperty(paramName, value);
    }

    return ret;
}
AMF_RESULT       StitchPipelineBase::StartConnect()
{
    m_ConnectThread.Start();
    return AMF_OK;
}
AMF_RESULT       StitchPipelineBase::OnConnect()
{
    AMF_RESULT res = InitZCam();

    if (res == AMF_OK)
    {
        AMFSize frameSize = { 0 };
        m_pSourceZCam->GetProperty(ZCAMLIVE_VIDEO_FRAMESIZE, &frameSize);
        res = m_pSourceZCam->Init(amf::AMF_SURFACE_FORMAT::AMF_SURFACE_NV12, frameSize.width, frameSize.height);
        //    AMF_RETURN_IF_FAILED(res, L"AMFZCamLiveStreamImpl Init() failed\n");
    }

    if(res == AMF_OK)
    {
        m_bIsConnected = true;
        res = InitInternal();
        if(res == AMF_OK)
        {
            Play();
        }
    }
    else
    {
        m_pSourceZCam->Terminate();
    }
    return res;
}

AMF_RESULT  StitchPipelineBase::InitZCam()
{
    AMF_RESULT res = AMF_OK;
    amf_int64 modeZCam = CAMLIVE_MODE_INVALID;
    GetParam(PARAM_NAME_ZCAMLIVE_MODE, modeZCam);

    if (m_pSourceZCam == NULL)
    {
        if ((CAMLIVE_MODE_THETAS == modeZCam) || (CAMLIVE_MODE_THETAV == modeZCam))
        {
            res = AMFCreateComponentVideoCapture(m_pContext, &m_pSourceZCam);
        }
        else
        {
            res = AMFCreateComponentZCamLiveStream(m_pContext, &m_pSourceZCam);
        }

        AMF_RETURN_IF_FALSE(m_pSourceZCam != NULL, AMF_OUT_OF_MEMORY, L"AMFZCamLiveStreamImpl new failed!\n");
    }

    if (m_pSourceZCam)
    {
        bool isStreamingEnabled = false;
        GetParam(PARAM_NAME_STREAMING, isStreamingEnabled);
        m_pSourceZCam->SetProperty(ZCAMLIVE_VIDEO_MODE, modeZCam);
        m_pSourceZCam->SetProperty(ZCAMLIVE_AUDIO_MODE, isStreamingEnabled ? CAM_AUDIO_MODE_SILENT : CAM_AUDIO_MODE_NONE);
        bool bLowLatency = true;
        //        GetParam(PARAM_NAME_LOWLATENCY, bLowLatency);
        m_pSourceZCam->SetProperty(ZCAMLIVE_LOWLATENCY, isStreamingEnabled ? 1 : 0); //streaming to youtube needs evenly spaced timestamp 

        if (CAMLIVE_MODE_THETAS == modeZCam)
        {
            m_pSourceZCam->SetProperty(VIDEOCAP_DEVICE_ACTIVE, L"RICOH THETA S");
        }
        else if (CAMLIVE_MODE_THETAV == modeZCam)
        {
            m_pSourceZCam->SetProperty(VIDEOCAP_DEVICE_ACTIVE, L"RICOH THETA V");
        }

        //set Camera IPs for router mode
        int streamCount = 4;
        m_pSourceZCam->GetProperty(ZCAMLIVE_STREAMCOUNT, &streamCount);

        amf::AMFVariant var;
        std::wstring cameraIPName = std::wstring(PARAM_NAME_ZCAMLIVE_IP) + std::wstring(L"_00");
        int cameraIPNameLen = (int)cameraIPName.length() - 1;
        for (amf_int32 ch = 0; ch < streamCount; ch++)
        {
            if (GetParam(cameraIPName.c_str(), var) == AMF_OK)
            {
                m_pSourceZCam->SetProperty(cameraIPName.c_str(), var.ToWString().c_str());
            }
            cameraIPName[cameraIPNameLen] += 1;
        }

    }
    return res;
}

std::vector<MediaInfo>* StitchPipelineBase::GetMediaInfo()
{
    amf_int64 modeZCam = CAMLIVE_MODE_INVALID;
    GetParam(PARAM_NAME_ZCAMLIVE_MODE, modeZCam);
    bool isZCamLive = modeZCam != CAMLIVE_MODE_INVALID;
    if (!isZCamLive)
    {
        return &m_Media;
    }

    AMF_RESULT res = InitZCam();
    if (res == AMF_OK)
    {
        MediaInfo mediaInfo = { 0 };
        mediaInfo.fileName = L"10.98.32.1";
        m_pSourceZCam->GetProperty(ZCAMLIVE_VIDEO_FRAMESIZE, &mediaInfo.sizeInput);
        amf_double fps = 0.;
        m_pSourceZCam->GetProperty(ZCAMLIVE_VIDEO_FRAMERATE, &fps);
        mediaInfo.framerate.den = 1;
        mediaInfo.framerate.num = amf_int32(fps);
        amf::AMFVariant var;
        res = m_pSourceZCam->GetProperty(ZCAMLIVE_CODEC_ID, &var);
        if (res == AMF_OK)  // video stream setup
        {
            mediaInfo.codecID = var.ToWString().c_str();
        }
        
        amf_int32 streamCount = (amf_int32)m_Cameras.size();
        m_pSourceZCam->GetProperty(ZCAMLIVE_STREAMCOUNT, &streamCount);
        m_Media.clear();
        for (amf_int32 idx = 0; idx < streamCount; idx++)
        {
            m_Media.push_back(mediaInfo);
            mediaInfo.fileName[mediaInfo.fileName.length() - 1] = mediaInfo.fileName[mediaInfo.fileName.length() - 1] + 1;
        }
    }

    return &m_Media;
};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
