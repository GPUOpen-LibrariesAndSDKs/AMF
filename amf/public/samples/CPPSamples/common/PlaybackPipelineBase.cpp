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
#include "PlaybackPipelineBase.h"
#include "public/common/AMFFactory.h"
#include "public/include/components/MediaSource.h"
#include "public/common/TraceAdapter.h"

#pragma warning(disable:4355)

#define AMF_FACILITY L"PlaybackPipelineBase"

const wchar_t* PlaybackPipelineBase::PARAM_NAME_INPUT      = L"INPUT";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_URL_VIDEO  = L"UrlVideo";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_URL_AUDIO  = L"UrlAudio";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_PRESENTER  = L"PRESENTER";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_FRAMERATE = L"FRAMERATE";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_LOOP = L"LOOP";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_LISTEN_FOR_CONNECTION = L"LISTEN";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_DOTIMING = L"DOTIMING";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_LOWLATENCY = L"LOWLATENCY";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_FULLSCREEN = L"FULLSCREEN";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_SW_DECODER = L"swdecoder";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_HQ_SCALER = L"HQSCALER";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_PIP = L"PIP";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_PIP_ZOOM_FACTOR = L"PIPZOOMFACTOR";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_PIP_FOCUS_X = L"PIPFOCUSX";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_PIP_FOCUS_Y = L"PIPFOCUSY";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_SIDE_BY_SIDE = L"SIDEBYSIDE";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_HQ_SCALER_RGB = L"HQSCALERRGB";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_HQ_SCALER_RATIO = L"HQSCALERRATIO";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_ENABLE_AUDIO = L"ENABLEAUDIO";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_HQSCALER_SHARPNESS = L"HQSCALERSHARPNESS";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_FRAME_RATE = L"FRAMERATE";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_FRC_ENGINE = L"FRCENGINE";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_FRC_INFO = L"FRCINFO";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_FRC_MODE = L"FRCMODE";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_FRC_ENABLE_FALLBACK = L"FRCENABLEFALLBACK";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_FRC_INDICATOR = L"FRCINDICATOR";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_EXCLUSIVE_FULLSCREEN = L"EXCLUSIVE_FULLSCREEN";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_FRC_PROFILE = L"FRCPROFILE";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_FRC_MV_SEARCH_MODE = L"FRCPERFORMANCE";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_FRC_RGB = L"FRCRGB";

PlaybackPipelineBase::PlaybackPipelineBase() :
    m_iVideoWidth(0),
    m_iVideoHeight(0),
    m_bVideoPresenterDirectConnect(false),
    m_nFfmpegRefCount(0),
    m_bURL(false),
    m_eDecoderFormat(amf::AMF_SURFACE_NV12),
    m_bCPUDecoder(false),
    m_bEnableSideBySide(false)
{
    g_AMFFactory.Init();
    SetParamDescription(PARAM_NAME_INPUT, ParamCommon,  L"Input file name", NULL);
    SetParamDescription(PARAM_NAME_URL_VIDEO, ParamCommon,  L"Input stream URL Video", NULL);
    SetParamDescription(PARAM_NAME_URL_AUDIO, ParamCommon,  L"Input stream URL Audio", NULL);
    SetParamDescription(PARAM_NAME_PRESENTER, ParamCommon,  L"Specifies presenter engine type (DX9, DX11, DX12, OPENGL)", ParamConverterVideoPresenter);
    SetParamDescription(PARAM_NAME_FRAMERATE, ParamCommon,  L"Forces Video Frame Rate (double)", ParamConverterDouble);
    SetParamDescription(PARAM_NAME_LOOP, ParamCommon,  L"Loop Video, boolean, default = true", ParamConverterBoolean);
    SetParamDescription(PARAM_NAME_LISTEN_FOR_CONNECTION, ParamCommon,  L"LIsten for connection, boolean, default = true", ParamConverterBoolean);
    SetParamDescription(PARAM_NAME_DOTIMING, ParamCommon,  L"Play Video and Audio using timestamps, boolean, default = true", ParamConverterBoolean);
    SetParamDescription(PARAM_NAME_LOWLATENCY, ParamCommon, L"Low latency mode, boolean, default = false", ParamConverterBoolean);
    SetParamDescription(PARAM_NAME_FULLSCREEN, ParamCommon, L"Specifies fullscreen mode, true, false, default false", ParamConverterBoolean);
    SetParamDescription(PARAM_NAME_SW_DECODER, ParamCommon, L"Forces sw decoder, true, false, default false", ParamConverterBoolean);
    SetParamDescription(PARAM_NAME_HQ_SCALER, ParamCommon,  L"Use HQ Scaler (OFF, Bilinear, Bicubic, VideoSR1.0, POINT, VideoSR1.1, default = OFF)", ParamConverterHQScalerAlgorithm);
    SetParamDescription(PARAM_NAME_PIP, ParamCommon, L"Specifies Picture in Picture mode, true, false, default false", ParamConverterBoolean);
    SetParamDescription(PARAM_NAME_PIP_ZOOM_FACTOR, ParamCommon, L"Specifies magnification ratio of PIP image, int, default 8", ParamConverterDouble);
    SetParamDescription(PARAM_NAME_PIP_FOCUS_X, ParamCommon, L"H position of foreground image, int, default 0", ParamConverterInt64);
    SetParamDescription(PARAM_NAME_PIP_FOCUS_Y, ParamCommon, L"V position of foreground image, int, default 0", ParamConverterInt64);
    SetParamDescription(PARAM_NAME_SIDE_BY_SIDE, ParamCommon, L"Specifies Side-by-Side mode, true, false, default false", ParamConverterBoolean);
    SetParamDescription(PARAM_NAME_HQ_SCALER_RGB, ParamCommon, L"Force RGB scaling, true, false, default false", ParamConverterBoolean);
    SetParamDescription(PARAM_NAME_HQ_SCALER_RATIO, ParamCommon, L"Scaling Ratio (1.3x, 1.5x, 1.7x, 2.0x), default 2.0x", ParamConverterRatio);
    SetParamDescription(PARAM_NAME_ENABLE_AUDIO, ParamCommon, L"Enables audio playback, boolean, default = true", ParamConverterDouble);
    SetParamDescription(PARAM_NAME_HQSCALER_SHARPNESS, ParamCommon, L"Specifies FSR RCAS attenuation, double, default = 0.75", ParamConverterDouble);
    SetParamDescription(PARAM_NAME_FRAME_RATE, ParamCommon, L"Frame Rate (off, 15fps, 30fps, 60fps), default = off", ParamConverterInt64);
    SetParamDescription(PARAM_NAME_FRC_ENGINE, ParamCommon, L"Frame Rate Conversion, (DX11, DX12) default = from '-presenter'", ParamConverterFRCEngine);
    SetParamDescription(PARAM_NAME_FRC_MODE, ParamCommon, L"FRC Mode, (OFF, ON, INTERPOLATED, x2_PRESENT) default = OFF ", ParamConverterFRCMode);
    SetParamDescription(PARAM_NAME_FRC_ENABLE_FALLBACK, ParamCommon, L"FRC enable fallback, (true, false), default = false", ParamConverterBoolean);
    SetParamDescription(PARAM_NAME_FRC_INDICATOR, ParamCommon, L"FRC Indicator, (true, false),  default = false", ParamConverterBoolean);
    SetParamDescription(PARAM_NAME_EXCLUSIVE_FULLSCREEN, ParamCommon, L"Specifies exclusive fullscreen mode, (true, false), default true", ParamConverterBoolean);
    SetParamDescription(PARAM_NAME_FRC_PROFILE, ParamCommon, L"FRC Profile,  default High", ParamConverterFRCProfile);
    SetParamDescription(PARAM_NAME_FRC_MV_SEARCH_MODE, ParamCommon, L"FRC Performance,  (NATIVE, PERFORMANCE) default = NATIVE", ParamConverterFRCPerformance);
    SetParamDescription(PARAM_NAME_FRC_RGB, ParamCommon, L"Run FRC in RGB or YUV node, true, false, default = false", ParamConverterBoolean);

    SetParam(PARAM_NAME_LISTEN_FOR_CONNECTION, false);
    SetParam(PARAM_NAME_FULLSCREEN, false);
    SetParam(PARAM_NAME_EXCLUSIVE_FULLSCREEN, true);
    SetParam(PARAM_NAME_PIP, false);
    SetParam(PARAM_NAME_PIP_ZOOM_FACTOR, 0.4f);
    SetParam(PARAM_NAME_PIP_FOCUS_X, 0);
    SetParam(PARAM_NAME_PIP_FOCUS_Y, 0);
    SetParam(PARAM_NAME_SIDE_BY_SIDE, false);
    SetParam(PARAM_NAME_HQ_SCALER_RGB, false);
    SetParam(PARAM_NAME_HQ_SCALER_RATIO, AMFRatio({ 20, 10 }));
    SetParam(PARAM_NAME_ENABLE_AUDIO, false);
    SetParam(PARAM_NAME_HQSCALER_SHARPNESS, 0.75);
    SetParam(PARAM_NAME_FRAME_RATE, 0);
#if defined(_WIN32)
    SetParam(PlaybackPipelineBase::PARAM_NAME_PRESENTER, amf::AMF_MEMORY_DX11);
#elif defined(__linux)
    SetParam(PlaybackPipelineBase::PARAM_NAME_PRESENTER, amf::AMF_MEMORY_VULKAN);
#endif
}

PlaybackPipelineBase::~PlaybackPipelineBase()
{
    Terminate();

    for (; m_nFfmpegRefCount > 0; --m_nFfmpegRefCount)
    {
        g_AMFFactory.UnLoadExternalComponent(FFMPEG_DLL_NAME);
    }
    g_AMFFactory.Terminate();
}

void PlaybackPipelineBase::Terminate()
{
    Stop();
    if (m_pContext != nullptr)
    {
        m_pContext->Terminate();
        m_pContext = NULL;
    }
}

AMF_RESULT PlaybackPipelineBase::GetDuration(amf_pts& duration) const
{
    if (m_pDemuxerVideo != NULL)
    {
        amf::AMFMediaSourcePtr pSource(m_pDemuxerVideo);
        if (pSource != NULL)
        {
            duration = pSource->GetDuration();
            return AMF_OK;
        }
    }

    return AMF_NOT_SUPPORTED;
}

AMF_RESULT PlaybackPipelineBase::GetCurrentPts(amf_pts& pts) const
{
	if (m_pDemuxerVideo != NULL)
	{
		amf::AMFMediaSourcePtr pSource(m_pDemuxerVideo);
		if (pSource != NULL)
		{
			pts = pSource->GetPosition();
			return AMF_OK;
		}
	}

	return AMF_NOT_SUPPORTED;
}

double PlaybackPipelineBase::GetProgressSize() const
{
    if (m_pVideoStream != NULL)
    {
        amf_int64 size = 0;
        AMF_RESULT res = m_pVideoStream->GetSize(&size);
        if (res == AMF_OK)
        {
            return static_cast<double>(size);
        }
    }
    else
    {
        amf_pts duration = 0;
        if (GetDuration(duration) == AMF_OK)
        {
            return static_cast<double>(duration);
        }
    }

    return 100.0;
}

double PlaybackPipelineBase::GetProgressPosition() const
{
    if(m_pVideoStream != NULL)
    {
		amf_int64 pos = 0;
		AMF_RESULT res = m_pVideoStream->GetPosition(&pos);
		if (res == AMF_OK)
		{
			return static_cast<double>(pos);
		}
    }
    else
    {
		amf_pts pts = 0;
		if (GetCurrentPts(pts) == AMF_OK)
		{
			return static_cast<double>(pts);
		}
    }

    return 0.0;
}

AMF_RESULT PlaybackPipelineBase::Seek(amf_pts pts)
{
    if(m_pVideoStream)
    {
//        m_pVideoStream->SetPosition( (amf_int64)pos);
    }
    else if(m_pDemuxerVideo != NULL)
    {
        amf::AMFMediaSourcePtr pSource(m_pDemuxerVideo);
        if(pSource != NULL)
        {
            Freeze();
            Flush();
            //MM temporarely - till provide better information that pipeline frozen
            amf_sleep(200);
            pSource->Seek(pts, amf::AMF_SEEK_PREV_KEYFRAME, -1);

            if(m_pDemuxerAudio != NULL)
            {

                amf::AMFMediaSourcePtr pSourceAudio(m_pDemuxerAudio);
                if(pSourceAudio != NULL)
                {
                    pSourceAudio->Seek(pts, amf::AMF_SEEK_PREV_KEYFRAME, -1);
                }
            }
            if(m_pAudioPresenter != NULL)
            {
                m_pAudioPresenter->Seek(pts);
            }

            UnFreeze();

            return AMF_OK;
        }
    }
    return AMF_NOT_SUPPORTED;
}

AMF_RESULT PlaybackPipelineBase::Init()
{
    Terminate();
    AMF_RESULT res = AMF_OK;
    //---------------------------------------------------------------------------------------------
    // Read Options
    std::wstring inputPath = L"";
    std::wstring inputUrlAudio = L"";
#if !defined(METRO_APP)
    res = GetParamWString(PARAM_NAME_INPUT, inputPath);
#else
    inputPath = path;
#endif
    if(inputPath.length() == 0)
    {
        res = GetParamWString(PARAM_NAME_URL_VIDEO, inputPath);
        if(inputPath.length() == 0)
        {
            return AMF_NOT_FOUND;
        }
        m_bURL = true;

        res = GetParamWString(PARAM_NAME_URL_AUDIO, inputUrlAudio);
    }

	amf::AMF_MEMORY_TYPE presenterEngine = amf::AMF_MEMORY_UNKNOWN;
    {
        amf_int64 engineInt = amf::AMF_MEMORY_UNKNOWN;
        GetParam(PARAM_NAME_PRESENTER, engineInt);

        if(amf::AMF_MEMORY_UNKNOWN != engineInt)
        {
            presenterEngine = (amf::AMF_MEMORY_TYPE)engineInt;
        }
    }

    //---------------------------------------------------------------------------------------------
    // Init context and devices
    res = InitContext(presenterEngine);
    CHECK_AMF_ERROR_RETURN(res, "Create AMF context");

    //---------------------------------------------------------------------------------------------
    // Init Video Stream Parser or demuxer
    BitStreamType streamType = BitStreamUnknown;
    if(!m_bURL)
    {
        streamType = GetStreamType(inputPath.c_str());
    }

    bool bLowlatency = false;
    GetParam(PARAM_NAME_LOWLATENCY, bLowlatency);

    amf_int32 iVideoStreamIndex = -1;
    amf_int32 iAudioStreamIndex = -1;

    amf::AMFOutputPtr       pAudioOutput;
    amf::AMFOutputPtr       pVideoOutput;
    if( streamType != BitStreamUnknown)
    {
#if !defined(METRO_APP)
        amf::AMFDataStream::OpenDataStream(inputPath.c_str(), amf::AMFSO_READ, amf::AMFFS_SHARE_READ, &m_pVideoStream);
#else
        amf::AMFDataStream::OpenDataStream(inputPath.c_str(), amf::AMFSO_READ, amf::AMFFS_SHARE_READ, &m_pVideoStream);
#endif
        CHECK_RETURN(m_pVideoStream != NULL, AMF_FILE_NOT_OPEN, "Open File");

        m_pVideoStreamParser = BitStreamParser::Create(m_pVideoStream, streamType, m_pContext);
        CHECK_RETURN(m_pVideoStreamParser != NULL, AMF_FILE_NOT_OPEN, "BitStreamParser::Create");

        double framerate = 0;
        if(GetParam(PARAM_NAME_FRAMERATE, framerate) == AMF_OK && framerate != 0)
        {
            m_pVideoStreamParser->SetFrameRate(framerate);
        }
        iVideoStreamIndex = 0;
    }
    else
    {
        amf::AMFComponentPtr  pDemuxer;

        res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*)FFMPEG_DEMUXER, &pDemuxer);
        CHECK_AMF_ERROR_RETURN(res, L"AMFCreateComponent(" << FFMPEG_DEMUXER << L") failed");
        ++m_nFfmpegRefCount;
        m_pDemuxerVideo = amf::AMFComponentExPtr(pDemuxer);

        bool bListen = false;
        GetParam(PlaybackPipelineBase::PARAM_NAME_LISTEN_FOR_CONNECTION, bListen);

        if(m_bURL)
        {
            m_pDemuxerVideo->SetProperty(FFMPEG_DEMUXER_LISTEN, bListen);
        }
        res = m_pDemuxerVideo->SetProperty(m_bURL ? FFMPEG_DEMUXER_URL : FFMPEG_DEMUXER_PATH, inputPath.c_str());
        CHECK_AMF_ERROR_RETURN(res, L"m_pDemuxerVideo->SetProperty() failed" );
        res = m_pDemuxerVideo->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
        CHECK_AMF_ERROR_RETURN(res, L"m_pDemuxerVideo->Init(" << inputPath << ") failed");

        amf_int32 videoOutputCount = m_pDemuxerVideo->GetOutputCount();
        for(amf_int32 videoOutput = 0; videoOutput < videoOutputCount; videoOutput++)
        {
            amf::AMFOutputPtr pOutput;
            res = m_pDemuxerVideo->GetOutput(videoOutput, &pOutput);
            CHECK_AMF_ERROR_RETURN(res, L"m_pDemuxerVideo->GetOutput() failed");

            amf_int64 eStreamType = AMF_STREAM_UNKNOWN;
            pOutput->GetProperty(AMF_STREAM_TYPE, &eStreamType);


            if(iVideoStreamIndex < 0 && eStreamType == AMF_STREAM_VIDEO)
            {
                iVideoStreamIndex = videoOutput;
                pVideoOutput = pOutput;
            }

            if(iAudioStreamIndex < 0 && eStreamType == AMF_STREAM_AUDIO)
            {
                iAudioStreamIndex = videoOutput;
                pAudioOutput = pOutput;
            }
        }
        if(inputUrlAudio.length() > 0)
        {
            amf::AMFComponentPtr  pDemuxerAudio;
            res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*)FFMPEG_DEMUXER, &pDemuxerAudio);
            CHECK_AMF_ERROR_RETURN(res, L"AMFCreateComponent(" << FFMPEG_DEMUXER << L") failed");
            ++m_nFfmpegRefCount;
            m_pDemuxerAudio = amf::AMFComponentExPtr(pDemuxerAudio);

            m_pDemuxerAudio->SetProperty(m_bURL ? FFMPEG_DEMUXER_URL : FFMPEG_DEMUXER_PATH, inputUrlAudio.c_str());
            res = m_pDemuxerAudio->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
            CHECK_AMF_ERROR_RETURN(res, L"m_pDemuxerAudio->Init() failed");

            amf_int32 audioOutputCount = m_pDemuxerAudio->GetOutputCount();
            for(amf_int32 audioOutput = 0; audioOutput < audioOutputCount; audioOutput++)
            {
                amf::AMFOutputPtr pOutput;
                res = m_pDemuxerAudio->GetOutput(audioOutput, &pOutput);
                CHECK_AMF_ERROR_RETURN(res, L"m_pDemuxerAudio->GetOutput() failed");

                amf_int64 eStreamType = AMF_STREAM_UNKNOWN;
                pOutput->GetProperty(AMF_STREAM_TYPE, &eStreamType);


                if(iVideoStreamIndex < 0 && eStreamType == AMF_STREAM_VIDEO)
                {
                    iVideoStreamIndex = audioOutput;
                    pVideoOutput = pOutput;
                }

                if(iAudioStreamIndex < 0 && eStreamType == AMF_STREAM_AUDIO)
                {
                    iAudioStreamIndex = audioOutput;
                    pAudioOutput = pOutput;
                }
            }

        }


    }
    //---------------------------------------------------------------------------------------------
    // init streams
    if(iVideoStreamIndex >= 0)
    {
		res = InitVideo(pVideoOutput, presenterEngine);
        CHECK_AMF_ERROR_RETURN(res, L"InitVideo() failed");
    }

    bool bEnableAudio = true;
    GetParam(PARAM_NAME_ENABLE_AUDIO, bEnableAudio);
    if(bEnableAudio && iAudioStreamIndex >= 0)
    {
        res = InitAudio(pAudioOutput);
        if (res != AMF_OK)
        {
            LOG_AMF_ERROR(res, L"InitAudio() failed");
            iAudioStreamIndex = -1;
        }
//        CHECK_AMF_ERROR_RETURN(res, L"InitAudio() failed");
    }

    //---------------------------------------------------------------------------------------------
    // Connect pipeline
	//-------------------------- Connect parser/ Demuxer
	PipelineElementPtr pPipelineElementDemuxerVideo = nullptr;
	PipelineElementPtr pPipelineElementDemuxerAudio = nullptr;
	if (m_pVideoStreamParser != NULL)
	{
		pPipelineElementDemuxerVideo = m_pVideoStreamParser;
	}
	else
	{
		pPipelineElementDemuxerVideo = PipelineElementPtr(new AMFComponentExElement(m_pDemuxerVideo));
	}
    Connect(pPipelineElementDemuxerVideo, 10);

    if(m_pDemuxerAudio != NULL)
    {
        pPipelineElementDemuxerAudio = PipelineElementPtr(new AMFComponentExElement(m_pDemuxerAudio));
	    Connect(pPipelineElementDemuxerAudio, 0, PipelineElementPtr(), 0, 10);
    }
    else
    {
        pPipelineElementDemuxerAudio = pPipelineElementDemuxerVideo;
    }
	Connect(PipelineElementPtr(new AMFComponentElement(m_pVideoDecoder)), 0, pPipelineElementDemuxerVideo, iVideoStreamIndex, m_bURL && bLowlatency == false ? 100 : 4, CT_ThreadQueue);
	// Initialize pipeline for both video and audio
	InitVideoPipeline(iVideoStreamIndex, pPipelineElementDemuxerVideo);
	InitAudioPipeline(iAudioStreamIndex, pPipelineElementDemuxerAudio);

    if(iVideoStreamIndex >= 0 && m_pVideoPresenter != NULL && m_pAudioPresenter != NULL)
    {
        m_AVSync.Reset();
        m_pVideoPresenter->SetAVSyncObject(&m_AVSync);
        m_pAudioPresenter->SetAVSyncObject(&m_AVSync);
    }

    bool bTiming = true;
    GetParam(PARAM_NAME_DOTIMING,  bTiming);
    if (bLowlatency)
    {
        bTiming = false;
    }
    if(m_pVideoPresenter != NULL)
    {
        m_pVideoPresenter->DoActualWait(bTiming);
    }
    if(m_pAudioPresenter != NULL)
    {
        m_pAudioPresenter->DoActualWait(bTiming);
    }


    return AMF_OK;
}

AMF_RESULT PlaybackPipelineBase::InitAudioPipeline(amf_uint32 iAudioStreamIndex, PipelineElementPtr pAudioSourceStream)
{
	if (m_pAudioPresenter != NULL && pAudioSourceStream != NULL)
	{
		Connect(PipelineElementPtr(new AMFComponentElement(m_pAudioDecoder)), 0, pAudioSourceStream, iAudioStreamIndex, m_bURL ? 1000 : 100, CT_ThreadQueue);
		Connect(PipelineElementPtr(new AMFComponentElement(m_pAudioConverter)), 10);
        if(m_pAudioPresenter != nullptr)
        {
		    Connect(m_pAudioPresenter, 10);
        }
	}
	return AMF_OK;
}

AMF_RESULT  PlaybackPipelineBase::ConnectScaler()
{
    AMF_RESULT res = AMF_FAIL;
    if (m_pScaler != NULL)
    {
        if (m_pHQScaler2 == NULL)
        {
            Connect(PipelineElementPtr(new AMFComponentElement(m_pScaler)), 1, CT_ThreadQueue);
            res = AMF_OK;
        }
        else
        {
            if (m_pSplitter == NULL)
            {
                m_pSplitter = SplitterPtr(new Splitter(true, 2));
            }
            if (m_pCombiner == NULL)
            {
                m_pCombiner = CombinerPtr(new Combiner(m_pContext));
            }

            if (m_pSplitter != NULL && m_pCombiner != NULL)
            {
                PipelineElementPtr upstreamConnector = GetLastElement();

                PipelineElementPtr hqScalerElement1(new AMFComponentElement(m_pScaler));
                PipelineElementPtr hqScalerElement2(new AMFComponentElement(m_pHQScaler2));

                Connect(m_pSplitter, 0, upstreamConnector, 0, 4);

                Connect(hqScalerElement1, 0, m_pSplitter, 0, 2);
                Connect(hqScalerElement2, 0, m_pSplitter, 1, 2);

                Connect(m_pCombiner, 0, hqScalerElement1, 0, 4);
                Connect(m_pCombiner, 1, hqScalerElement2, 0, 4);
                res = AMF_OK;
            }
        }
    }
    return res;
}
AMF_RESULT PlaybackPipelineBase::InitVideoPipeline(amf_uint32 /* iVideoStreamIndex */, PipelineElementPtr pVideoSourceStream)
{
    bool bAllocator = true;

    bool bLowlatency = false;
    GetParam(PARAM_NAME_LOWLATENCY, bLowlatency);

    bool bForceScalingRGB = false;
    GetParam(PARAM_NAME_HQ_SCALER_RGB, bForceScalingRGB);
    bool bForceFRCRGB = false;
    GetParam(PARAM_NAME_FRC_RGB, bForceFRCRGB);

    bool bYUVInput = (m_eDecoderFormat == amf::AMF_SURFACE_NV12) || (m_eDecoderFormat == amf::AMF_SURFACE_P010) || (m_eDecoderFormat == amf::AMF_SURFACE_P012) || (m_eDecoderFormat == amf::AMF_SURFACE_P016);

    if (bYUVInput == false)
    {
        bForceScalingRGB = true;
        bForceFRCRGB = true;
    }

//    bool bYUVScaling = (((m_eDecoderFormat == amf::AMF_SURFACE_NV12) || (m_eDecoderFormat == amf::AMF_SURFACE_P010)) &&
//                        ((m_pFRC != nullptr) ||
//                         (m_pScaler != nullptr)) &&
//                        !bForceScalingRGB);

    // connect YUV
    if (m_pFRC != NULL && bForceFRCRGB == false) //FRC
    {
        Connect(PipelineElementPtr(new AMFComponentElement(m_pFRC)), 1, CT_ThreadQueue);
    }
    // connect scaler
    if (bForceScalingRGB == false && m_pScaler != nullptr)
    {
        ConnectScaler();
        bAllocator = false;
    }
    // connect color converter
    if (m_pVideoConverter != NULL)
	{
        if (bLowlatency)
        {
            Connect(PipelineElementPtr(new AMFComponentElement(m_pVideoConverter)), 1, CT_Direct);
        }
        else
        {
            Connect(PipelineElementPtr(new AMFComponentElement(m_pVideoConverter)), m_bCPUDecoder ? 4 : 1, CT_ThreadQueue);
        }
    }
    // connect RGB
    if (m_pFRC != NULL && bForceFRCRGB == true) //FRC
    {
        Connect(PipelineElementPtr(new AMFComponentElement(m_pFRC)), 1, CT_ThreadQueue);
        bAllocator = false;
    }
    // connect scaler
    if (bForceScalingRGB == true && m_pScaler != nullptr)
    {
        ConnectScaler();
        amf_int64  hqScalerMode = -1;
        GetParam(PARAM_NAME_HQ_SCALER, hqScalerMode);
        bAllocator = (hqScalerMode != -1); // no allocator non HQ scaler is setup by calling app

    }
    // connect presenter

    m_pVideoPresenter->SetProcessor(m_pVideoConverter, m_pScaler, bAllocator);

    if(m_bVideoPresenterDirectConnect)
    {
        Connect(m_pVideoPresenter, 4, CT_ThreadPoll); // if back buffers are used in multithreading the video memory changes every Present() call so pointer returned by GetBackBuffer() points to wrong memory
    }
    else
    {
        Connect(m_pVideoPresenter, 4, CT_ThreadQueue);
    }
	return AMF_OK;
}

AMF_RESULT  PlaybackPipelineBase::InitFRC(amf::AMF_MEMORY_TYPE type)
{
    AMF_RESULT res = AMF_OK;
    amf_int64 frcMode = FRC_OFF;
    GetParam(PARAM_NAME_FRC_MODE, frcMode);

    if (frcMode != FRC_OFF)
    {
        amf_int64 frcEngineType = FRC_ENGINE_OFF;
        GetParam(PlaybackPipelineBase::PARAM_NAME_FRC_ENGINE, frcEngineType);
        if (frcEngineType == FRC_ENGINE_OFF)
        {
            switch (type)
            {
            case amf::AMF_MEMORY_OPENCL: frcEngineType = FRC_ENGINE_OPENCL; break;
            case amf::AMF_MEMORY_DX11: frcEngineType = FRC_ENGINE_DX11; break;
            case amf::AMF_MEMORY_DX12: frcEngineType = FRC_ENGINE_DX12; break;
            default:
                frcEngineType = FRC_ENGINE_DX11; break;
            }
        }
        bool bForceFRCRGB = false;
        GetParam(PARAM_NAME_FRC_RGB, bForceFRCRGB);
        bool bYUVInput = (m_eDecoderFormat == amf::AMF_SURFACE_NV12) || (m_eDecoderFormat == amf::AMF_SURFACE_P010) || (m_eDecoderFormat == amf::AMF_SURFACE_P012) || (m_eDecoderFormat == amf::AMF_SURFACE_P016);
        if (bYUVInput == false)
        {
            if (frcEngineType == FRC_ENGINE_OPENCL)
            {
                AMFTraceWarning(AMF_FACILITY, L"FRC not supported for OpenCL + RGB input, skipping FRC");
                return AMF_OK;
            }
            bForceFRCRGB = true;
        }
        if (frcEngineType == FRC_ENGINE_OPENCL && bForceFRCRGB == true)
        {
            AMFTraceInfo(AMF_FACILITY, L"FRC not supported for OpenCL + RGB, switching to YUV");
            SetParam(PARAM_NAME_FRC_RGB, false);
            bForceFRCRGB = false;
        }


        res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, AMFFRC, &m_pFRC);
        CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << AMFFRC << L") failed");
        //m_pFRC->SetProperty(AMF_HQ_SCALER_ENGINE_TYPE, type);

        // initialize
        if (m_pFRC != nullptr)
        {
            SetParam(PARAM_NAME_HQ_SCALER_RATIO, AMFRatio({ 10, 10 }));

            m_pFRC->SetProperty(AMF_FRC_ENGINE_TYPE, frcEngineType);


            m_pFRC->SetProperty(AMF_FRC_MODE, frcMode);

            amf_int64 frcFlag = 0;

            GetParam(PlaybackPipelineBase::PARAM_NAME_FRC_ENABLE_FALLBACK, frcFlag);
            m_pFRC->SetProperty(AMF_FRC_ENABLE_FALLBACK, frcFlag);

            bool indicator = false;
            GetParam(PlaybackPipelineBase::PARAM_NAME_FRC_INDICATOR, indicator);
            m_pFRC->SetProperty(AMF_FRC_INDICATOR, indicator);

            amf_int64 frcProfile = FRC_PROFILE_HIGH;
            GetParam(PlaybackPipelineBase::PARAM_NAME_FRC_PROFILE, frcProfile);
            m_pFRC->SetProperty(AMF_FRC_PROFILE, frcProfile);

            amf_int64 frcMVSearchMode = FRC_MV_SEARCH_NATIVE;
            GetParam(PlaybackPipelineBase::PARAM_NAME_FRC_MV_SEARCH_MODE, frcMVSearchMode);
            m_pFRC->SetProperty(AMF_FRC_MV_SEARCH_MODE, frcMVSearchMode);

            res = m_pFRC->Init(bForceFRCRGB == true ? m_pVideoPresenter->GetInputFormat() : m_eDecoderFormat, m_iVideoWidth, m_iVideoHeight);
            CHECK_AMF_ERROR_RETURN(res, L"m_pFRC->Init() failed with err=%d");

            if (bForceFRCRGB == true) //FRC is last filter, but doesn't support scaling
            {
                m_pVideoPresenter->SetRenderToBackBuffer(false);
            }
        }
    }
    return res;
}

AMF_RESULT  PlaybackPipelineBase::InitColorConverter()
{
    AMF_RESULT res = AMF_OK;
    // create CSC - this one is needed all the time,
    if (m_pVideoConverter == nullptr)
    {
        res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, AMFVideoConverter, &m_pVideoConverter);
        CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << AMFVideoConverter << L") failed");

        if (m_eDecoderFormat == amf::AMF_SURFACE_P010) //HDR support
        {
            //        m_pVideoConverter->SetProperty(AMF_VIDEO_CONVERTER_COLOR_PROFILE, AMF_VIDEO_CONVERTER_COLOR_PROFILE_2020);
            //        m_pVideoConverter->SetProperty(AMF_VIDEO_CONVERTER_LINEAR_RGB, true);
        }
        else
        {
            //        m_pVideoConverter->SetProperty(AMF_VIDEO_CONVERTER_COLOR_PROFILE, AMF_VIDEO_CONVERTER_COLOR_PROFILE_709);
        }
        m_pVideoConverter->SetProperty(AMF_VIDEO_CONVERTER_MEMORY_TYPE, m_pVideoPresenter->GetMemoryType());
        m_pVideoConverter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_FORMAT, m_pVideoPresenter->GetInputFormat());

        res = m_pVideoConverter->Init(m_eDecoderFormat, m_iVideoWidth, m_iVideoHeight);
        CHECK_AMF_ERROR_RETURN(res, L"m_pVideoConverter->Init() failed");


        // set-up the processor for presenter - if we use HQ scaler,
        // we need to set it, otherwise it will be CSC

        if (m_pContext->GetOpenCLContext() == NULL)
        {
            m_pVideoConverter->SetProperty(AMF_VIDEO_CONVERTER_KEEP_ASPECT_RATIO, true);
            m_pVideoConverter->SetProperty(AMF_VIDEO_CONVERTER_FILL, true);

            if (m_pScaler != nullptr)
            {
                m_pScaler->SetProperty(AMF_HQ_SCALER_KEEP_ASPECT_RATIO, true);
                m_pScaler->SetProperty(AMF_HQ_SCALER_FILL, true);
            }
        }
    }
    return res;

}
AMF_RESULT  PlaybackPipelineBase::InitVideoProcessor()
{
    // check if we need to create the HQ scaler
    AMF_RESULT res = AMF_OK;
    amf_int64  hqScalerMode = -1;

    GetParam(PARAM_NAME_HQ_SCALER, hqScalerMode);

    if (hqScalerMode != -1)
    {
        res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, AMFHQScaler, &m_pScaler);
        CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << AMFHQScaler << L") failed");

        AMFRatio scalingRatio = { 0, 0 };
        GetParam(PARAM_NAME_HQ_SCALER_RATIO, scalingRatio);

        amf_int32 oVideoWidth = m_iVideoWidth * scalingRatio.num / scalingRatio.den;
        amf_int32 oVideoHeight = m_iVideoHeight * scalingRatio.num / scalingRatio.den;

        amf_double fHQScalerSharpness = 0;
        GetParam(PARAM_NAME_HQSCALER_SHARPNESS, fHQScalerSharpness);

        amf_int64 oFrameRate = 0;
        GetParam(PARAM_NAME_FRAME_RATE, oFrameRate);

        m_pScaler->SetProperty(AMF_HQ_SCALER_OUTPUT_SIZE, ::AMFConstructSize(oVideoWidth, oVideoHeight));
        m_pScaler->SetProperty(AMF_HQ_SCALER_ENGINE_TYPE, m_pVideoPresenter->GetMemoryType());
        m_pScaler->SetProperty(AMF_HQ_SCALER_ALGORITHM, hqScalerMode);
        m_pScaler->SetProperty(AMF_HQ_SCALER_SHARPNESS, fHQScalerSharpness);
        m_pScaler->SetProperty(AMF_HQ_SCALER_FRAME_RATE, oFrameRate);

        /*
        // If enabled side-by-side, create a separate HQ Scaler
        if (m_bEnableSideBySide)
        {
            res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, AMFHQScaler, &m_pHQScaler2);
            CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << AMFHQScaler << L") failed");

            m_pHQScaler2->SetProperty(AMF_HQ_SCALER_OUTPUT_SIZE, ::AMFConstructSize(m_iVideoWidth, m_iVideoHeight));
            m_pHQScaler2->SetProperty(AMF_HQ_SCALER_ENGINE_TYPE, m_pVideoPresenter->GetMemoryType());
            m_pHQScaler2->SetProperty(AMF_HQ_SCALER_ALGORITHM, AMF_HQ_SCALER_ALGORITHM_BILINEAR);

            res = m_pHQScaler2->Init(m_pVideoPresenter->GetInputFormat(), m_iVideoWidth, m_iVideoHeight);
            CHECK_AMF_ERROR_RETURN(res, L"m_pHQScaler2->Init() failed");
        }
        */

        // initialize scaler
        bool bForceScalingRGB = false;
        GetParam(PARAM_NAME_HQ_SCALER_RGB, bForceScalingRGB);

        bool bYUVInput = (m_eDecoderFormat == amf::AMF_SURFACE_NV12) || (m_eDecoderFormat == amf::AMF_SURFACE_P010) || (m_eDecoderFormat == amf::AMF_SURFACE_P012) || (m_eDecoderFormat == amf::AMF_SURFACE_P016);
        if (bYUVInput == false)
        {
            bForceScalingRGB = true;
        }

        res = m_pScaler->Init(bForceScalingRGB ? m_pVideoPresenter->GetInputFormat() : m_eDecoderFormat, m_iVideoWidth, m_iVideoHeight);
        CHECK_AMF_ERROR_RETURN(res, L"m_pScaler->Init() failed with err=%d");
    }

    return res;
}

AMF_RESULT  PlaybackPipelineBase::InitVideoDecoder(const wchar_t *pDecoderID, amf_int64 codecID, amf::AMF_SURFACE_FORMAT surfaceFormat, amf_int64 bitrate, AMFRate frameRate, amf::AMFBuffer* pExtraData)
{
    bool bLowlatency = false;
    GetParam(PARAM_NAME_LOWLATENCY, bLowlatency);

    AMF_VIDEO_DECODER_MODE_ENUM   decoderMode = bLowlatency ? AMF_VIDEO_DECODER_MODE_LOW_LATENCY : AMF_VIDEO_DECODER_MODE_COMPLIANT; // AMF_VIDEO_DECODER_MODE_REGULAR , AMF_VIDEO_DECODER_MODE_LOW_LATENCY;

    bool bSWDecoder = false;
    GetParam(PARAM_NAME_SW_DECODER, bSWDecoder);

    if ((surfaceFormat == amf::AMF_SURFACE_YUY2) ||
        (surfaceFormat == amf::AMF_SURFACE_UYVY) ||
        (surfaceFormat == amf::AMF_SURFACE_Y210) ||
        (surfaceFormat == amf::AMF_SURFACE_Y416) ||
        (surfaceFormat == amf::AMF_SURFACE_RGBA_F16) ||
        (surfaceFormat == amf::AMF_SURFACE_RGBA))
    {
        bSWDecoder = true;
    }

    if (bSWDecoder == false)
    {
        const wchar_t* pHwDecoderId = pDecoderID;
        if ((std::wstring(pDecoderID) == AMFVideoDecoderHW_AV1) &&
            ((surfaceFormat == amf::AMF_SURFACE_P012) || (surfaceFormat == amf::AMF_SURFACE_P016)))
        {
            pHwDecoderId = AMFVideoDecoderHW_AV1_12BIT;
        }

        AMF_RESULT res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, pHwDecoderId, &m_pVideoDecoder);
        CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << pHwDecoderId << L") failed");
    }

    // switch to SW Decoder when the HW Decoder does not support this config
    amf::AMFCapsPtr pCaps;
    if (m_pVideoDecoder != nullptr &&
        m_pVideoDecoder->GetCaps(&pCaps) != AMF_OK)
    {
        m_pVideoDecoder = NULL;
    }

    if (m_pVideoDecoder != nullptr)
    {
        amf::AMFIOCapsPtr pInputCaps;
        AMF_RESULT res = pCaps->GetInputCaps(&pInputCaps);
        CHECK_AMF_ERROR_RETURN(res, L"failed to get decoder input caps");

        // check resolution
        amf_int32 minWidth = 0;
        amf_int32 maxWidth = 0;
        amf_int32 minHeight = 0;
        amf_int32 maxHeight = 0;
        pInputCaps->GetWidthRange(&minWidth, &maxWidth);
        pInputCaps->GetHeightRange(&minHeight, &maxHeight);
        if (minWidth <= m_iVideoWidth && m_iVideoWidth <= maxWidth && minHeight <= m_iVideoHeight && m_iVideoHeight <= maxHeight)
        {
            m_pVideoDecoder->SetProperty(AMF_VIDEO_DECODER_REORDER_MODE, amf_int64(decoderMode));
            m_pVideoDecoder->SetProperty(AMF_VIDEO_DECODER_EXTRADATA, amf::AMFVariant(pExtraData));

            m_pVideoDecoder->SetProperty(AMF_TIMESTAMP_MODE, amf_int64(AMF_TS_DECODE)); // our sample H264 parser provides decode order timestamps- change depend on demuxer
            m_eDecoderFormat = amf::AMF_SURFACE_NV12;
            if (std::wstring(pDecoderID) == AMFVideoDecoderHW_H265_MAIN10)
            {
                m_eDecoderFormat = amf::AMF_SURFACE_P010;
            }
            if (std::wstring(pDecoderID) == AMFVideoDecoderHW_AV1 || std::wstring(pDecoderID) == AMFVideoDecoderHW_AV1_12BIT)
            {
                m_eDecoderFormat = surfaceFormat;
            }
            if (pExtraData != nullptr || std::wstring(pDecoderID) == AMFVideoDecoderUVD_MJPEG)
            {
                res = m_pVideoDecoder->Init(m_eDecoderFormat, m_iVideoWidth, m_iVideoHeight);
            }
            else
            {
                res = m_pVideoDecoder->Init(m_eDecoderFormat, 0, 0);
            }
            CHECK_AMF_ERROR_RETURN(res, L"m_pVideoDecoder->Init(" << m_iVideoWidth << L", " << m_iVideoHeight << L") failed " << pDecoderID);
		    if (std::wstring(pDecoderID) == AMFVideoDecoderHW_AV1)
		    {
			    //Only up to above m_pVideoDecoder->Init(), got/updated actual eDecoderFormat, then also update m_eDecoderFormat here.
			    amf_int64 format = 0;
			    if (m_pVideoDecoder->GetProperty(L"AV1StreamFormat", &format) == AMF_OK)
			    {
                    m_eDecoderFormat = (amf::AMF_SURFACE_FORMAT)format;
			    }
		    }
            else if (std::wstring(pDecoderID) == AMFVideoDecoderHW_AV1_12BIT)
            {
                m_eDecoderFormat = amf::AMF_SURFACE_P012;
            }
            m_bCPUDecoder = false;
        }
        else
        {
            // fall back to software decoder if resolution not supported
            m_pVideoDecoder = nullptr;
        }
    }

    // use software decoder
    //   - if specifically requested
    //   - if resolution not supported
    //   - if codec not supported
    if (m_pVideoDecoder == nullptr)
    {
        AMF_RESULT res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*)FFMPEG_VIDEO_DECODER, &m_pVideoDecoder);
        CHECK_AMF_ERROR_RETURN(res, L"LoadExternalComponent(" << FFMPEG_VIDEO_DECODER << L") failed");
        ++m_nFfmpegRefCount;

        if (codecID == 0)
        {
            if (std::wstring(pDecoderID) == AMFVideoDecoderUVD_H264_AVC)
            {
                codecID = AMF_STREAM_CODEC_ID_H264_AVC;
            }
            else if (std::wstring(pDecoderID) == AMFVideoDecoderHW_H265_MAIN10)
            {
                codecID = AMF_STREAM_CODEC_ID_H265_MAIN10;
            }
            else if (std::wstring(pDecoderID) == AMFVideoDecoderHW_H265_HEVC)
            {
                codecID = AMF_STREAM_CODEC_ID_H265_HEVC;
            }
			else if (std::wstring(pDecoderID) == AMFVideoDecoderHW_AV1)
			{
				codecID = AMF_STREAM_CODEC_ID_AV1;
			}
            else if (std::wstring(pDecoderID) == AMFVideoDecoderHW_AV1_12BIT)
            {
                codecID = AMF_STREAM_CODEC_ID_AV1_12BIT;
            }
        }
        m_pVideoDecoder->SetProperty(VIDEO_DECODER_CODEC_ID, codecID);
        m_pVideoDecoder->SetProperty(VIDEO_DECODER_BITRATE, bitrate);
        m_pVideoDecoder->SetProperty(VIDEO_DECODER_FRAMERATE, frameRate);

//      m_pVideoDecoder->SetProperty(AMF_VIDEO_DECODER_REORDER_MODE, amf_int64(decoderMode));
        m_pVideoDecoder->SetProperty(VIDEO_DECODER_EXTRA_DATA, amf::AMFVariant(pExtraData));

        if(surfaceFormat != amf::AMF_SURFACE_UNKNOWN)
        {
            m_eDecoderFormat = surfaceFormat;
        }
        else
        {
            m_eDecoderFormat = amf::AMF_SURFACE_NV12;
        }
/*
        if(std::wstring(pDecoderID) == AMFVideoDecoderHW_H265_MAIN10)
        {
            m_eDecoderFormat = amf::AMF_SURFACE_P010;
        }
        if (std::wstring(pDecoderID) == AMFVideoDecoderHW_AV1)
        {
            // can be 10 or 12 bits per sample
            amf_int32 outputCount = m_pDemuxerVideo->GetOutputCount();
            AMF_RETURN_IF_FALSE(outputCount > 0, AMF_FAIL, L"InitVideoDecoder() - not enough output streams");

            amf::AMFOutputPtr pOutput;
            m_pDemuxerVideo->GetOutput(0, &pOutput);

            amf_int64 surfaceFormat = amf::AMF_SURFACE_UNKNOWN;
            pOutput->GetProperty(AMF_STREAM_VIDEO_FORMAT, &surfaceFormat);

            if (surfaceFormat != static_cast<amf::AMF_SURFACE_FORMAT>(amf::AMF_SURFACE_UNKNOWN))
            {
                m_eDecoderFormat = static_cast<amf::AMF_SURFACE_FORMAT>(surfaceFormat);
            }
        }
*/
        res = m_pVideoDecoder->Init(m_eDecoderFormat, m_iVideoWidth, m_iVideoHeight);
        CHECK_AMF_ERROR_RETURN(res, L"m_pVideoDecoder->Init("<< m_iVideoWidth << m_iVideoHeight << L") failed " << pDecoderID );
        m_bCPUDecoder = true;
    }

    return AMF_OK;
}

AMF_RESULT PlaybackPipelineBase::Play()
{
    switch(GetState())
    {
    case PipelineStateRunning:
        if(m_pAudioPresenter != NULL)
        {
            m_pAudioPresenter->Resume(m_pVideoPresenter->GetCurrentTime());
        }
        return m_pVideoPresenter->Resume();
    case PipelineStateReady:
        return Start();
    case PipelineStateNotReady:
    case PipelineStateEof:
    case PipelineStateFrozen:
        return UnFreeze();
    default:
        break;
    }
    return AMF_WRONG_STATE;
}

AMF_RESULT PlaybackPipelineBase::Pause()
{
    switch(GetState())
    {
    case PipelineStateRunning:
    {
        if(m_pAudioPresenter != NULL)
        {
            m_pAudioPresenter->Pause();
        }
        return m_pVideoPresenter->Pause();
    }
    case PipelineStateReady:
    case PipelineStateNotReady:
    case PipelineStateEof:
    case PipelineStateFrozen:
    default:
        break;
    }
    return AMF_WRONG_STATE;
}

bool  PlaybackPipelineBase::IsPaused() const
{
    if (m_pVideoPresenter == nullptr)
    {
        return false;
    }
    return GetState() == PipelineStateRunning && m_pVideoPresenter->GetMode() == VideoPresenter::ModePaused;
}


AMF_RESULT PlaybackPipelineBase::Step()
{
    switch(GetState())
    {
    case PipelineStateRunning:
        if(m_pAudioPresenter != NULL)
        {
            m_pAudioPresenter->Pause();
        }
        m_pVideoPresenter->Pause();
        return m_pVideoPresenter->Step();
    case PipelineStateReady:
    case PipelineStateNotReady:
    case PipelineStateEof:
    case PipelineStateFrozen:
    default:
        break;
    }
    return AMF_WRONG_STATE;
}

AMF_RESULT PlaybackPipelineBase::Stop()
{
    if(m_pAudioPresenter != NULL)
    {
        m_pAudioPresenter->Flush();
    }

    Pipeline::Stop();

    m_AVSync.Reset();

    if(m_pAudioDecoder != NULL)
    {
        m_pAudioDecoder->Terminate();
        m_pAudioDecoder = NULL;
    }

    if(m_pAudioConverter != NULL)
    {
        m_pAudioConverter->Terminate();
        m_pAudioConverter = NULL;
    }

    if(m_pAudioPresenter != NULL)
    {
        m_pAudioPresenter = NULL;
    }

    if(m_pVideoDecoder != NULL)
    {
        m_pVideoDecoder->Terminate();
        m_pVideoDecoder = NULL;
    }

    if(m_pVideoConverter != NULL)
    {
        m_pVideoConverter->Terminate();
        m_pVideoConverter = NULL;
    }

    if (m_pScaler != NULL)
    {
        m_pScaler->Terminate();
        m_pScaler = NULL;
    }

    if (m_pHQScaler2 != NULL)
    {
        m_pHQScaler2->Terminate();
        m_pHQScaler2 = NULL;
    }

   if (m_pFRC != NULL)
    {
       m_pFRC->Terminate();
       m_pFRC = NULL;
    }

   if(m_pVideoPresenter != NULL)
    {
        m_pVideoPresenter->Terminate();
        m_pVideoPresenter = NULL;
    }

    m_pVideoStreamParser = NULL;

    if(m_pDemuxerVideo != NULL)
    {
        m_pDemuxerVideo->Terminate();
        m_pDemuxerVideo = NULL;
    }
    if(m_pDemuxerAudio != NULL)
    {
        m_pDemuxerAudio->Terminate();
        m_pDemuxerAudio = NULL;
    }


    if(m_pContext != NULL)
    {
        m_pContext->Terminate();
        m_pContext = NULL;
    }

    return AMF_OK;
}

void PlaybackPipelineBase::OnParamChanged(const wchar_t* name)
{
    //check if the pipeline will need to be recreated
    if (std::wstring(name) == std::wstring(PARAM_NAME_HQ_SCALER_RGB))
    {
        ReInit();
    }
    //check if HQScaler will need to be re-created in the pipeline
    if (std::wstring(name) == std::wstring(PARAM_NAME_HQ_SCALER))
    {
        amf_int64  modeHQScaler = -1;

        if (m_pScaler != nullptr)
        {
            m_pScaler->GetProperty(AMF_HQ_SCALER_ALGORITHM, &modeHQScaler);
        }

        amf_int64  modeHQScalerNew = -1;
        GetParam(PARAM_NAME_HQ_SCALER, modeHQScalerNew);

        if ((modeHQScaler == -1 && modeHQScalerNew != -1)  ||
            (modeHQScaler != -1 && modeHQScalerNew == -1)) //need to re-build the pipeline
        {
            ReInit();
        }

        if (m_pScaler != nullptr && modeHQScalerNew != -1)
        {
            m_pScaler->SetProperty(AMF_HQ_SCALER_ALGORITHM, modeHQScalerNew);
        }
    }

    //check if FRC will need to be re-created in the pipeline
    if (std::wstring(name) == std::wstring(PARAM_NAME_FRC_ENGINE))
    {
        amf_int64  frcEngineType = FRC_ENGINE_OFF;
        if (m_pFRC != nullptr)
        {
            // Running FRC component engine type.
            m_pFRC->GetProperty(AMF_FRC_ENGINE_TYPE, &frcEngineType);
        }

        // Pipeline selected FRC engine type.
        amf_int64  frcEngineTypePipeline = FRC_ENGINE_OFF;
        GetParam(PARAM_NAME_FRC_ENGINE, frcEngineTypePipeline);

        if (frcEngineTypePipeline != frcEngineType) //need to re-build the pipeline
        {
            ReInit();
        }
    }

    //update FRC fallback mode
    if (std::wstring(name) == std::wstring(PARAM_NAME_FRC_ENABLE_FALLBACK))
    {
        if (m_pFRC != nullptr)
        {
            // Pipeline selected FRC engine type.
            amf_bool bEnableFallback = false;
            GetParam(PARAM_NAME_FRC_ENABLE_FALLBACK, bEnableFallback);
            m_pFRC->SetProperty(AMF_FRC_ENABLE_FALLBACK, bEnableFallback);
        }
    }

    //update FRC mode
    if (std::wstring(name) == std::wstring(PARAM_NAME_FRC_MODE))
    {
        if (m_pFRC != nullptr)
        {
            // Pipeline selected FRC engine type.
            amf_int64 frcMode = 0;
            GetParam(PARAM_NAME_FRC_MODE, frcMode);
            m_pFRC->SetProperty(AMF_FRC_MODE, frcMode);
        }
    }

    if (m_pVideoPresenter != nullptr)
    {
        if (std::wstring(name) == std::wstring(PARAM_NAME_FULLSCREEN))
        {
            bool bFullScreen = false;
            GetParam(PARAM_NAME_FULLSCREEN, bFullScreen);
            m_pVideoPresenter->SetFullScreen(bFullScreen);
        }
        else if (std::wstring(name) == std::wstring(PARAM_NAME_EXCLUSIVE_FULLSCREEN))
        {
            bool bExclusive = false;
            GetParam(PARAM_NAME_EXCLUSIVE_FULLSCREEN, bExclusive);
            m_pVideoPresenter->SetExclusiveFullscreen(bExclusive);
        }
        else if (std::wstring(name) == std::wstring(PARAM_NAME_DOTIMING))
        {
            bool bDoTiming = true;
            GetParam(PARAM_NAME_DOTIMING, bDoTiming);
            m_pVideoPresenter->DoActualWait(bDoTiming);
        }
        else if (std::wstring(name) == std::wstring(PARAM_NAME_PIP))
        {
            bool bEnablePIP = false;
            GetParam(PARAM_NAME_PIP, bEnablePIP);
            m_pVideoPresenter->SetEnablePIP(bEnablePIP);

            amf_int iPIPZoomFactor;
            GetParam(PARAM_NAME_PIP_ZOOM_FACTOR, iPIPZoomFactor);
            m_pVideoPresenter->SetPIPZoomFactor(iPIPZoomFactor);

            AMFPoint PIPFocusPos;
            GetParam(PARAM_NAME_PIP_FOCUS_X, PIPFocusPos.x);
            GetParam(PARAM_NAME_PIP_FOCUS_Y, PIPFocusPos.y);

            AMFFloatPoint2D fPIPFocusPos = { (amf_float)PIPFocusPos.x / (amf_float)m_iVideoWidth, (amf_float)PIPFocusPos.y / (amf_float)m_iVideoHeight };
            m_pVideoPresenter->SetPIPFocusPositions(fPIPFocusPos);
        }
        else if (std::wstring(name) == std::wstring(PARAM_NAME_PIP_ZOOM_FACTOR))
        {
            amf_int iPIPZoomFactor;
            GetParam(PARAM_NAME_PIP_ZOOM_FACTOR, iPIPZoomFactor);
            m_pVideoPresenter->SetPIPZoomFactor(iPIPZoomFactor);
        }
        else if (std::wstring(name) == std::wstring(PARAM_NAME_PIP_FOCUS_X) || std::wstring(name) == std::wstring(PARAM_NAME_PIP_FOCUS_Y))
        {
            AMFPoint PIPFocusPos;
            GetParam(PARAM_NAME_PIP_FOCUS_X, PIPFocusPos.x);
            GetParam(PARAM_NAME_PIP_FOCUS_Y, PIPFocusPos.y);

            AMFFloatPoint2D fPIPFocusPos = { (amf_float)PIPFocusPos.x / (amf_float)m_iVideoWidth, (amf_float)PIPFocusPos.y / (amf_float)m_iVideoHeight };
            m_pVideoPresenter->SetPIPFocusPositions(fPIPFocusPos);
        }
        else if (std::wstring(name) == std::wstring(PARAM_NAME_SIDE_BY_SIDE))
        {
            bool bEnableSideBySide = false;
            GetParam(PARAM_NAME_SIDE_BY_SIDE, bEnableSideBySide);
            m_bEnableSideBySide = bEnableSideBySide;
        }
        else if (std::wstring(name) == std::wstring(PARAM_NAME_HQ_SCALER_RATIO))
        {
            if (m_pScaler != nullptr)
            {
                AMFRatio scalingRatio = { 0, 0 };
                GetParam(PARAM_NAME_HQ_SCALER_RATIO, scalingRatio);

                amf_int32 oVideoWidth = m_iVideoWidth * scalingRatio.num / scalingRatio.den;
                amf_int32 oVideoHeight = m_iVideoHeight * scalingRatio.num / scalingRatio.den;
                m_pScaler->SetProperty(AMF_HQ_SCALER_OUTPUT_SIZE, ::AMFConstructSize(oVideoWidth, oVideoHeight));
            }
        }
        else if (std::wstring(name) == std::wstring(PARAM_NAME_HQSCALER_SHARPNESS))
        {
            if (m_pScaler != nullptr)
            {
                amf_double fHQScalerSharpness = 0.0f;
                GetParam(PARAM_NAME_HQSCALER_SHARPNESS, fHQScalerSharpness);

                m_pScaler->SetProperty(AMF_HQ_SCALER_SHARPNESS, fHQScalerSharpness);
            }
        }
        else if (std::wstring(name) == std::wstring(PARAM_NAME_FRAME_RATE))
        {
            if (m_pScaler != nullptr)
            {
                amf_int64 oFrameRate = 0;
                GetParam(PARAM_NAME_FRAME_RATE, oFrameRate);

                m_pScaler->SetProperty(AMF_HQ_SCALER_FRAME_RATE, oFrameRate);
            }
        }
    }
    if(m_pVideoConverter == NULL)
    {
        return;
    }
    UpdateVideoProcessorProperties(name);
}

void PlaybackPipelineBase::UpdateVideoProcessorProperties(const wchar_t* name)
{
    if (m_pVideoConverter == NULL)
    {
        return;
    }
    if(name != NULL)
    {
        ParamDescription description;
        GetParamDescription(name, description);

        if(description.m_Type == ParamVideoProcessor)
        {
            amf::AMFVariant value;
            GetParam(name, &value);
            m_pVideoConverter->SetProperty(description.m_Name.c_str(), value);
        }
    }
    else
    {
        for(amf_size i = 0; i < GetParamCount(); ++i)
        {
            std::wstring nameStr;
            amf::AMFVariant value;
            GetParamAt(i, nameStr, &value);
            ParamDescription description;
            GetParamDescription(nameStr.c_str(), description);
            if(description.m_Type == ParamVideoProcessor)
            {
                m_pVideoConverter->SetProperty(description.m_Name.c_str(), value);
            }
        }
    }
}
double PlaybackPipelineBase::GetFPS()
{
    if(m_pVideoPresenter != NULL)
    {
        return m_pVideoPresenter->GetFPS();
    }
    return 0;
}
amf_int64 PlaybackPipelineBase::GetFramesDropped() const
{
    if(m_pVideoPresenter != NULL)
    {
        return m_pVideoPresenter->GetFramesDropped();
    }
    return 0;
}
AMF_RESULT  PlaybackPipelineBase::InitAudio(amf::AMFOutput* pOutput)
{
    AMF_RESULT res = AMF_OK;

    amf_int64 codecID = 0;
    amf_int64 streamBitRate = 0;
    amf_int64 streamSampleRate = 0;
    amf_int64 streamChannels = 0;
    amf_int64 streamFormat = 0;
    amf_int64 streamLayout = 0;
    amf_int64 streamBlockAlign = 0;
    amf_int64 streamFrameSize = 0;
    amf::AMFInterfacePtr pExtradata;

    pOutput->GetProperty(AMF_STREAM_CODEC_ID, &codecID);

    pOutput->GetProperty(AMF_STREAM_BIT_RATE, &streamBitRate);
    pOutput->GetProperty(AMF_STREAM_EXTRA_DATA, &pExtradata);

    pOutput->GetProperty(AMF_STREAM_AUDIO_SAMPLE_RATE, &streamSampleRate);
    pOutput->GetProperty(AMF_STREAM_AUDIO_CHANNELS, &streamChannels);
    pOutput->GetProperty(AMF_STREAM_AUDIO_FORMAT, &streamFormat);
    pOutput->GetProperty(AMF_STREAM_AUDIO_CHANNEL_LAYOUT, &streamLayout);
    pOutput->GetProperty(AMF_STREAM_AUDIO_BLOCK_ALIGN, &streamBlockAlign);
    pOutput->GetProperty(AMF_STREAM_AUDIO_FRAME_SIZE, &streamFrameSize);

    res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*)FFMPEG_AUDIO_DECODER, &m_pAudioDecoder);
    CHECK_AMF_ERROR_RETURN(res, L"LoadExternalComponent(" << FFMPEG_AUDIO_DECODER << L") failed");
    ++m_nFfmpegRefCount;

    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_CODEC_ID, codecID);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_BIT_RATE, streamBitRate);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_EXTRA_DATA, pExtradata);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_SAMPLE_RATE, streamSampleRate);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_CHANNELS, streamChannels);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_SAMPLE_FORMAT, streamFormat);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_CHANNEL_LAYOUT, streamLayout);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_BLOCK_ALIGN, streamBlockAlign);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_FRAME_SIZE, streamFrameSize);

    res = m_pAudioDecoder->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
   CHECK_AMF_ERROR_RETURN(res, L"m_pAudioDecoder->Init() failed");

    res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*)FFMPEG_AUDIO_CONVERTER, &m_pAudioConverter);
    CHECK_AMF_ERROR_RETURN(res, L"LoadExternalComponent(" << FFMPEG_AUDIO_CONVERTER << L") failed");
    ++m_nFfmpegRefCount;

    m_pAudioDecoder->GetProperty(AUDIO_DECODER_OUT_AUDIO_BIT_RATE, &streamBitRate);
    m_pAudioDecoder->GetProperty(AUDIO_DECODER_OUT_AUDIO_SAMPLE_RATE, &streamSampleRate);
    m_pAudioDecoder->GetProperty(AUDIO_DECODER_OUT_AUDIO_CHANNELS, &streamChannels);
    m_pAudioDecoder->GetProperty(AUDIO_DECODER_OUT_AUDIO_SAMPLE_FORMAT, &streamFormat);
    m_pAudioDecoder->GetProperty(AUDIO_DECODER_OUT_AUDIO_CHANNEL_LAYOUT, &streamLayout);
    m_pAudioDecoder->GetProperty(AUDIO_DECODER_OUT_AUDIO_BLOCK_ALIGN, &streamBlockAlign);


    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_BIT_RATE, streamBitRate);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_SAMPLE_RATE, streamSampleRate);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_CHANNELS, streamChannels);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_SAMPLE_FORMAT, streamFormat);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_CHANNEL_LAYOUT, streamLayout);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_BLOCK_ALIGN, streamBlockAlign);

    res = CreateAudioPresenter();
    if(res == AMF_NOT_IMPLEMENTED)
    {
        return AMF_OK;
    }
    CHECK_AMF_ERROR_RETURN(res, "Failed to create an audio presenter");

    res = m_pAudioPresenter->Init();
    CHECK_AMF_ERROR_RETURN(res, L"m_pAudioPresenter->Init() failed");

    m_pAudioPresenter->GetDescription(
        streamBitRate,
        streamSampleRate,
        streamChannels,
        streamFormat,
        streamLayout,
        streamBlockAlign
    );

    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_BIT_RATE, streamBitRate);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_SAMPLE_RATE, streamSampleRate);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_CHANNELS, streamChannels);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_SAMPLE_FORMAT, streamFormat);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_CHANNEL_LAYOUT, streamLayout);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_BLOCK_ALIGN, streamBlockAlign);

    res = m_pAudioConverter->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
    CHECK_AMF_ERROR_RETURN(res, L"m_pAudioConverter->Init() failed");

    pOutput->SetProperty(AMF_STREAM_ENABLED, true);

   return AMF_OK;
}
AMF_RESULT  PlaybackPipelineBase::InitVideo(amf::AMFOutput* pOutput, amf::AMF_MEMORY_TYPE presenterEngine)
{
    bool decodeAsAnnexBStream = false; // switches between Annex B and AVCC types of decode input.

    AMF_RESULT res = AMF_OK;
    amf::AMFBufferPtr pExtraData;
    std::wstring pVideoDecoderID;

	amf_int64 bitRate = 0;
	double dFps = 0.0;

    amf_int64 codecID = 0;
    amf_int64 surfaceFormat = amf::AMF_SURFACE_NV12;

    AMFRate frameRate = {};

    if(m_pVideoStreamParser != NULL)
    {
        if(!decodeAsAnnexBStream) // need to provide SPS/PPS if input stream will be AVCC ( not Annex B)
        {
            const unsigned char* extraData = m_pVideoStreamParser->GetExtraData();
            size_t extraDataSize = m_pVideoStreamParser->GetExtraDataSize();
            if (extraData && extraDataSize)
            {
                m_pContext->AllocBuffer(amf::AMF_MEMORY_HOST, extraDataSize, &pExtraData);

                memcpy(pExtraData->GetNative(), extraData, extraDataSize);
            }
        }
        m_iVideoWidth = m_pVideoStreamParser->GetPictureWidth();
        m_iVideoHeight = m_pVideoStreamParser->GetPictureHeight();

        pVideoDecoderID = m_pVideoStreamParser->GetCodecComponent();

		dFps = m_pVideoStreamParser->GetFrameRate();
        frameRate.num = amf_int32(dFps * 1000);
        frameRate.den = 1000;
    }
    else if(pOutput != NULL)
    {
        amf::AMFInterfacePtr pInterface;
        pOutput->GetProperty(AMF_STREAM_EXTRA_DATA, &pInterface);
        pExtraData = amf::AMFBufferPtr(pInterface);

        AMFSize frameSize = {};
        pOutput->GetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, &frameSize);
        m_iVideoWidth = frameSize.width;
        m_iVideoHeight = frameSize.height;

        res= pOutput->GetProperty(AMF_STREAM_CODEC_ID, &codecID);
        pVideoDecoderID = StreamCodecIDtoDecoderID(AMF_STREAM_CODEC_ID_ENUM(codecID));

        if (pVideoDecoderID.size() == 0)    //try SW decoder
        {
            pOutput->GetProperty(FFMPEG_DEMUXER_VIDEO_CODEC, &codecID);
        }

        pOutput->SetProperty(AMF_STREAM_ENABLED, true);

		pOutput->GetProperty(AMF_STREAM_BIT_RATE, &bitRate);

		pOutput->GetProperty(AMF_STREAM_VIDEO_FRAME_RATE, &frameRate);

        pOutput->GetProperty(AMF_STREAM_VIDEO_FORMAT, &surfaceFormat);

		dFps = frameRate.den == 0 ? 0.0 : (static_cast<double>(frameRate.num) / static_cast<double>(frameRate.den));
    }
    //---------------------------------------------------------------------------------------------
    // Init Video Decoder
    res = InitVideoDecoder(pVideoDecoderID.c_str(), codecID, (amf::AMF_SURFACE_FORMAT)surfaceFormat, bitRate, frameRate, pExtraData);
    CHECK_AMF_ERROR_RETURN(res, L"InitVideoDecoder() failed");

    if(m_pVideoDecoder != NULL)
    {
        m_pVideoDecoder->SetProperty(AMF_TIMESTAMP_MODE, amf_int64(m_pVideoStreamParser != NULL ? AMF_TS_DECODE : AMF_TS_SORT)); // our sample H264 parser provides decode order timestamps- change depend on demuxer
    }
    //---------------------------------------------------------------------------------------------
    // Init Presenter
    res = CreateVideoPresenter(presenterEngine, bitRate, dFps);
    CHECK_AMF_ERROR_RETURN(res, L"CreatePresenter() failed");

    bool bFullScreen = false;
    GetParam(PARAM_NAME_FULLSCREEN, bFullScreen);
    m_pVideoPresenter->SetFullScreen(bFullScreen);

    bool bExclusiveFullScreen = false;
    GetParam(PARAM_NAME_EXCLUSIVE_FULLSCREEN, bExclusiveFullScreen);
    m_pVideoPresenter->SetExclusiveFullscreen(bExclusiveFullScreen);

    bool bEnablePIP = false;
    GetParam(PARAM_NAME_PIP, bEnablePIP);
    m_pVideoPresenter->SetEnablePIP(bEnablePIP);

    GetParam(PARAM_NAME_SIDE_BY_SIDE, m_bEnableSideBySide);

    if (m_eDecoderFormat == amf::AMF_SURFACE_P010 ||
        m_eDecoderFormat == amf::AMF_SURFACE_P012 ||
        m_eDecoderFormat == amf::AMF_SURFACE_P016) //HDR support
    {
        m_pVideoPresenter->SetInputFormat(amf::AMF_SURFACE_RGBA_F16);
// can be used if needed
//        m_pVideoPresenter->SetInputFormat(amf::AMF_SURFACE_R10G10B10A2);
    }

    res = m_pVideoPresenter->Init(m_iVideoWidth, m_iVideoHeight);
    CHECK_AMF_ERROR_RETURN(res, L"m_pVideoPresenter->Init(" << m_iVideoWidth << m_iVideoHeight << ") failed");

    //---------------------------------------------------------------------------------------------
    // Init Video Converter/Processor/FRC
    res = InitFRC(presenterEngine);
    CHECK_AMF_ERROR_RETURN(res, L"InitFRC() failed");

    res = InitVideoProcessor();
    CHECK_AMF_ERROR_RETURN(res, L"InitVideoProcessor() failed");

    res = InitColorConverter();
    CHECK_AMF_ERROR_RETURN(res, L"InitColorConverter() failed");


    return AMF_OK;
}

void PlaybackPipelineBase::OnEof()
{
    Pipeline::OnEof();
    if(GetState() == PipelineStateEof)
    {
        bool bLoop = true;
        GetParam(PARAM_NAME_LOOP, bLoop);
        if(bLoop)
        {
            if(m_pDemuxerVideo != nullptr)
            {
                m_pDemuxerVideo->ReInit(0, 0);
            }
            if(m_pDemuxerAudio != nullptr)
            {
                m_pDemuxerAudio->ReInit(0, 0);
            }
            if(m_pVideoStreamParser != NULL)
            {
                m_pVideoStreamParser->ReInit();
            }
            if(m_pVideoDecoder != NULL)
            {
                m_pVideoDecoder->ReInit(m_iVideoWidth, m_iVideoHeight);
            }
            if(m_pVideoConverter != NULL)
            {
                m_pVideoConverter->ReInit(m_iVideoWidth, m_iVideoHeight);
            }
            if (m_pScaler != NULL)
            {
                m_pScaler->ReInit(m_iVideoWidth, m_iVideoHeight);
            }
            if (m_pHQScaler2 != NULL)
            {
                m_pHQScaler2->ReInit(m_iVideoWidth, m_iVideoHeight);
            }
            if (m_pFRC != NULL)
            {
                m_pFRC->ReInit(m_iVideoWidth, m_iVideoHeight);
            }
            if(m_pVideoPresenter != NULL)
            {
                m_pVideoPresenter->Reset();
            }
            if(m_pAudioDecoder != NULL)
            {
                m_pAudioDecoder->ReInit(0, 0);
            }
            if(m_pAudioConverter != NULL)
            {
                m_pAudioConverter->ReInit(0, 0);
            }
            Restart();

        }
    }
}
AMF_RESULT PlaybackPipelineBase::OnActivate(bool bActivated)
{
    if (m_pVideoPresenter != nullptr)
    {
        if (bActivated)
        {
            bool bFullScreen = false;
            GetParam(PARAM_NAME_FULLSCREEN, bFullScreen);
            AMFTraceDebug(AMF_FACILITY, L"ProcessMessage() Window Activated");
            m_pVideoPresenter->SetFullScreen(bFullScreen);

            bool bEnablePIP = false;
            GetParam(PARAM_NAME_PIP, bEnablePIP);
            m_pVideoPresenter->SetEnablePIP(bEnablePIP);

            AMFPoint PIPFocusPos = {};
            GetParam(PARAM_NAME_PIP_FOCUS_X, PIPFocusPos.x);
            GetParam(PARAM_NAME_PIP_FOCUS_Y, PIPFocusPos.y);
            AMFFloatPoint2D fPIPFocusPos = { (amf_float)PIPFocusPos.x / (amf_float)m_iVideoWidth, (amf_float)PIPFocusPos.y / (amf_float)m_iVideoHeight };
            m_pVideoPresenter->SetPIPFocusPositions(fPIPFocusPos);
        }
        else
        {
            m_pVideoPresenter->SetFullScreen(false);
            m_pVideoPresenter->SetEnablePIP(false);
            AMFTraceDebug(AMF_FACILITY, L"ProcessMessage() Window Deactivated");
        }
    }
    return AMF_OK;
}

AMF_RESULT PlaybackPipelineBase::ReInit()
{
    //restart the playback with possibably different pipeline
    if (GetState() == PipelineStateRunning)
    {
        Init();
        Play();

        //update the PIP parameters
        if (m_pVideoPresenter != nullptr)
        {
            bool bEnablePIP = false;
            GetParam(PARAM_NAME_PIP, bEnablePIP);
            m_pVideoPresenter->SetEnablePIP(bEnablePIP);

            amf_int iPIPZoomFactor = 0;
            GetParam(PARAM_NAME_PIP_ZOOM_FACTOR, iPIPZoomFactor);
            m_pVideoPresenter->SetPIPZoomFactor(iPIPZoomFactor);

            AMFPoint PIPFocusPos = {};
            GetParam(PARAM_NAME_PIP_FOCUS_X, PIPFocusPos.x);
            GetParam(PARAM_NAME_PIP_FOCUS_Y, PIPFocusPos.y);
            AMFFloatPoint2D fPIPFocusPos = { (amf_float)PIPFocusPos.x / (amf_float)m_iVideoWidth, (amf_float)PIPFocusPos.y / (amf_float)m_iVideoHeight };
            m_pVideoPresenter->SetPIPFocusPositions(fPIPFocusPos);
        }
    }
    return AMF_OK;
}
