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
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
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

#pragma warning(disable:4355)


const wchar_t* PlaybackPipelineBase::PARAM_NAME_INPUT      = L"INPUT";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_URL_VIDEO  = L"UrlVideo";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_URL_AUDIO  = L"UrlAudio";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_PRESENTER  = L"PRESENTER";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_FRAMERATE = L"FRAMERATE";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_LOOP = L"LOOP";
const wchar_t* PlaybackPipelineBase::PARAM_NAME_LISTEN_FOR_CONNECTION = L"LISTEN";

PlaybackPipelineBase::PlaybackPipelineBase() :
    m_iVideoWidth(0),
    m_iVideoHeight(0),
    m_bVideoPresenterDirectConnect(false),
    m_nFfmpegRefCount(0),
    m_bURL(false),
    m_eDecoderFormat(amf::AMF_SURFACE_NV12)
{
    g_AMFFactory.Init();
    SetParamDescription(PARAM_NAME_INPUT, ParamCommon,  L"Input file name", NULL);
    SetParamDescription(PARAM_NAME_URL_VIDEO, ParamCommon,  L"Input stream URL Video", NULL);
    SetParamDescription(PARAM_NAME_URL_AUDIO, ParamCommon,  L"Input stream URL Audio", NULL);
    SetParamDescription(PARAM_NAME_PRESENTER, ParamCommon,  L"Specifies presenter engine type (DX9, DX11, OPENGL)", ParamConverterVideoPresenter);
    SetParamDescription(PARAM_NAME_FRAMERATE, ParamCommon,  L"Forces Video Frame Rate (double)", ParamConverterDouble);
    SetParamDescription(PARAM_NAME_LOOP, ParamCommon,  L"Loop Video, boolean, default = true", ParamConverterBoolean);
    SetParamDescription(PARAM_NAME_LISTEN_FOR_CONNECTION, ParamCommon,  L"LIsten for connection, boolean, default = true", ParamConverterBoolean);
    
    SetParam(PARAM_NAME_LISTEN_FOR_CONNECTION, false);

#if defined(METRO_APP)
    SetParam(PlaybackPipelineBase::PARAM_NAME_PRESENTER, amf::AMF_MEMORY_DX11);
#else
    SetParam(PlaybackPipelineBase::PARAM_NAME_PRESENTER, amf::AMF_MEMORY_DX11);
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
    m_pContext = NULL;
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

    amf::AMF_MEMORY_TYPE presenterEngine;
    {
        amf_int64 engineInt = amf::AMF_MEMORY_UNKNOWN;
        if(GetParam(PARAM_NAME_PRESENTER, engineInt) == AMF_OK)
        {
            if(amf::AMF_MEMORY_UNKNOWN != engineInt)
            {
                presenterEngine = (amf::AMF_MEMORY_TYPE)engineInt;
            }
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

        res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", FFMPEG_DEMUXER, &pDemuxer);
        CHECK_AMF_ERROR_RETURN(res, L"AMFCreateComponent(" << FFMPEG_DEMUXER << L") failed");
        ++m_nFfmpegRefCount;
        m_pDemuxerVideo = amf::AMFComponentExPtr(pDemuxer);

        bool bListen = false;
        GetParam(PlaybackPipelineBase::PARAM_NAME_LISTEN_FOR_CONNECTION, bListen);

        if(m_bURL)
        {
            bool bListen = false;
            GetParam(PlaybackPipelineBase::PARAM_NAME_LISTEN_FOR_CONNECTION, bListen);
            m_pDemuxerVideo->SetProperty(FFMPEG_DEMUXER_LISTEN, bListen);
        }
        m_pDemuxerVideo->SetProperty(m_bURL ? FFMPEG_DEMUXER_URL : FFMPEG_DEMUXER_PATH, inputPath.c_str());
        res = m_pDemuxerVideo->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
        CHECK_AMF_ERROR_RETURN(res, L"m_pDemuxerVideo->Init() failed");

        amf_int32 outputs = m_pDemuxerVideo->GetOutputCount();
        for(amf_int32 output = 0; output < outputs; output++)
        {
            amf::AMFOutputPtr pOutput;
            res = m_pDemuxerVideo->GetOutput(output, &pOutput);
            CHECK_AMF_ERROR_RETURN(res, L"m_pDemuxerVideo->GetOutput() failed");

            amf_int64 eStreamType = DEMUXER_UNKNOWN;
            pOutput->GetProperty(FFMPEG_DEMUXER_STREAM_TYPE, &eStreamType);


            if(iVideoStreamIndex < 0 && eStreamType == DEMUXER_VIDEO)
            {
                iVideoStreamIndex = output;
                pVideoOutput = pOutput;
            }

            if(iAudioStreamIndex < 0 && eStreamType == DEMUXER_AUDIO)
            {
                iAudioStreamIndex = output;
                pAudioOutput = pOutput;
            }
        }
        if(inputUrlAudio.length() > 0)
        {
            amf::AMFComponentPtr  pDemuxerAudio;
            res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", FFMPEG_DEMUXER, &pDemuxerAudio);
            CHECK_AMF_ERROR_RETURN(res, L"AMFCreateComponent(" << FFMPEG_DEMUXER << L") failed");
            ++m_nFfmpegRefCount;
            m_pDemuxerAudio = amf::AMFComponentExPtr(pDemuxerAudio);

            m_pDemuxerAudio->SetProperty(m_bURL ? FFMPEG_DEMUXER_URL : FFMPEG_DEMUXER_PATH, inputUrlAudio.c_str());
            res = m_pDemuxerAudio->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
            CHECK_AMF_ERROR_RETURN(res, L"m_pDemuxerAudio->Init() failed");

            amf_int32 outputs = m_pDemuxerAudio->GetOutputCount();
            for(amf_int32 output = 0; output < outputs; output++)
            {
                amf::AMFOutputPtr pOutput;
                res = m_pDemuxerAudio->GetOutput(output, &pOutput);
                CHECK_AMF_ERROR_RETURN(res, L"m_pDemuxerAudio->GetOutput() failed");

                amf_int64 eStreamType = DEMUXER_UNKNOWN;
                pOutput->GetProperty(FFMPEG_DEMUXER_STREAM_TYPE, &eStreamType);


                if(iVideoStreamIndex < 0 && eStreamType == DEMUXER_VIDEO)
                {
                    iVideoStreamIndex = output;
                    pVideoOutput = pOutput;
                }

                if(iAudioStreamIndex < 0 && eStreamType == DEMUXER_AUDIO)
                {
                    iAudioStreamIndex = output;
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
    if(iAudioStreamIndex >= 0)
    {
        res = InitAudio(pAudioOutput);
        CHECK_AMF_ERROR_RETURN(res, L"InitAudio() failed");
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
	Connect(PipelineElementPtr(new AMFComponentElement(m_pVideoDecoder)), 0, pPipelineElementDemuxerVideo, iVideoStreamIndex, m_bURL ? 100 : 4, CT_ThreadQueue);
	// Initialize pipeline for both video and audio
	InitVideoPipeline(iVideoStreamIndex, pPipelineElementDemuxerVideo);
	InitAudioPipeline(iAudioStreamIndex, pPipelineElementDemuxerAudio);

    if(iVideoStreamIndex >= 0 && m_pVideoPresenter != NULL && m_pAudioPresenter != NULL)
    {
        m_AVSync.Reset();
        m_pVideoPresenter->SetAVSyncObject(&m_AVSync);
        m_pAudioPresenter->SetAVSyncObject(&m_AVSync);
    }
    
    return AMF_OK;
}

AMF_RESULT PlaybackPipelineBase::InitAudioPipeline(amf_uint32 iAudioStreamIndex, PipelineElementPtr pAudioSourceStream)
{
	if (iAudioStreamIndex >= 0 && m_pAudioPresenter != NULL && pAudioSourceStream != NULL)
	{
		Connect(PipelineElementPtr(new AMFComponentElement(m_pAudioDecoder)), 0, pAudioSourceStream, iAudioStreamIndex, m_bURL ? 1000 : 100, CT_ThreadQueue);
		Connect(PipelineElementPtr(new AMFComponentElement(m_pAudioConverter)), 10);
		Connect(m_pAudioPresenter, 10);
	}
	return AMF_OK;
}

AMF_RESULT PlaybackPipelineBase::InitVideoPipeline(amf_uint32 iVideoStreamIndex, PipelineElementPtr pVideoSourceStream)
{
    if (m_pVideoProcessor != NULL)
	{
        Connect(PipelineElementPtr(new AMFComponentElement(m_pVideoProcessor)), 1, CT_ThreadQueue);
    }
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



AMF_RESULT  PlaybackPipelineBase::InitVideoProcessor()
{
    AMF_RESULT res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, AMFVideoConverter, &m_pVideoProcessor);
    CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << AMFVideoConverter << L") failed");

    if (m_pVideoPresenter->SupportAllocator() && m_pContext->GetOpenCLContext() == NULL)
    {
        m_pVideoProcessor->SetProperty(AMF_VIDEO_CONVERTER_KEEP_ASPECT_RATIO, true);
        m_pVideoProcessor->SetProperty(AMF_VIDEO_CONVERTER_FILL, true);
        m_pVideoPresenter->SetProcessor(m_pVideoProcessor);
    }
    m_pVideoProcessor->SetProperty(AMF_VIDEO_CONVERTER_MEMORY_TYPE, m_pVideoPresenter->GetMemoryType());
    m_pVideoProcessor->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_FORMAT, m_pVideoPresenter->GetInputFormat());

    m_pVideoProcessor->Init(m_eDecoderFormat, m_iVideoWidth, m_iVideoHeight);
    return AMF_OK;
}

AMF_RESULT  PlaybackPipelineBase::InitVideoDecoder(const wchar_t *pDecoderID, amf::AMFBuffer* pExtraData)
{
    AMF_VIDEO_DECODER_MODE_ENUM   decoderMode = AMF_VIDEO_DECODER_MODE_COMPLIANT; // AMF_VIDEO_DECODER_MODE_REGULAR , AMF_VIDEO_DECODER_MODE_LOW_LATENCY;

    AMF_RESULT res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, pDecoderID, &m_pVideoDecoder);
    CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << pDecoderID << L") failed");

    m_pVideoDecoder->SetProperty(AMF_VIDEO_DECODER_REORDER_MODE, amf_int64(decoderMode));
    m_pVideoDecoder->SetProperty(AMF_VIDEO_DECODER_EXTRADATA, amf::AMFVariant(pExtraData));

    m_pVideoDecoder->SetProperty(AMF_TIMESTAMP_MODE, amf_int64(AMF_TS_DECODE)); // our sample H264 parser provides decode order timestamps- change depend on demuxer
    m_eDecoderFormat = amf::AMF_SURFACE_NV12;
    if(std::wstring(pDecoderID) == AMFVideoDecoderHW_H265_MAIN10)
    {
        m_eDecoderFormat = amf::AMF_SURFACE_P010;
    }
    res = m_pVideoDecoder->Init(m_eDecoderFormat, m_iVideoWidth, m_iVideoHeight);
    CHECK_AMF_ERROR_RETURN(res, L"m_pVideoDecoder->Init("<< m_iVideoWidth << m_iVideoHeight << L") failed " << pDecoderID );

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
    
    if(m_pVideoProcessor != NULL)
    {
        m_pVideoProcessor->Terminate();
        m_pVideoProcessor = NULL;
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
    if(m_pVideoProcessor == NULL)
    {
        return;
    }
    UpdateVideoProcessorProperties(name);
}

void PlaybackPipelineBase::UpdateVideoProcessorProperties(const wchar_t* name)
{
    if (m_pVideoProcessor == NULL)
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
            m_pVideoProcessor->SetProperty(description.m_Name.c_str(), value);
        }
    }
    else
    {
        for(amf_size i = 0; i < GetParamCount(); ++i)
        {
            std::wstring name;
            amf::AMFVariant value;
            GetParamAt(i, name, &value);
            ParamDescription description;
            GetParamDescription(name.c_str(), description);
            if(description.m_Type == ParamVideoProcessor)
            {
                m_pVideoProcessor->SetProperty(description.m_Name.c_str(), value);
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

    pOutput->GetProperty(FFMPEG_DEMUXER_CODEC_ID, &codecID);

    pOutput->GetProperty(FFMPEG_DEMUXER_BIT_RATE, &streamBitRate);
    pOutput->GetProperty(FFMPEG_DEMUXER_EXTRA_DATA, &pExtradata);

    pOutput->GetProperty(FFMPEG_DEMUXER_AUDIO_SAMPLE_RATE, &streamSampleRate);
    pOutput->GetProperty(FFMPEG_DEMUXER_AUDIO_CHANNELS, &streamChannels);
    pOutput->GetProperty(FFMPEG_DEMUXER_AUDIO_SAMPLE_FORMAT, &streamFormat);
    pOutput->GetProperty(FFMPEG_DEMUXER_AUDIO_CHANNEL_LAYOUT, &streamLayout);
    pOutput->GetProperty(FFMPEG_DEMUXER_AUDIO_BLOCK_ALIGN, &streamBlockAlign);
    pOutput->GetProperty(FFMPEG_DEMUXER_AUDIO_FRAME_SIZE, &streamFrameSize);

    res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", FFMPEG_AUDIO_DECODER, &m_pAudioDecoder);
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

    res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", FFMPEG_AUDIO_CONVERTER, &m_pAudioConverter);
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

    CHECK_AMF_ERROR_RETURN(CreateAudioPresenter(), "Failed to create an audio presenter");

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

    pOutput->SetProperty(FFMPEG_DEMUXER_STREAM_ENABLED, true);

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
    }
    else if(pOutput != NULL)
    { 
        amf::AMFInterfacePtr pInterface;
        pOutput->GetProperty(FFMPEG_DEMUXER_EXTRA_DATA, &pInterface);
        pExtraData = amf::AMFBufferPtr(pInterface);

        AMFSize frameSize;
        pOutput->GetProperty(FFMPEG_DEMUXER_VIDEO_FRAMESIZE, &frameSize);
        m_iVideoWidth = frameSize.width;
        m_iVideoHeight = frameSize.height;
        
        amf::AMFVariant var;
        res= pOutput->GetProperty(FFMPEG_DEMUXER_VIDEO_DECODER_ID, &var);
        if(res == AMF_OK)  // video stream setup
        {
            pVideoDecoderID = var.ToWString().c_str();
        }
        pOutput->SetProperty(FFMPEG_DEMUXER_STREAM_ENABLED, true);

		pOutput->GetProperty(FFMPEG_DEMUXER_BIT_RATE, &bitRate);

		AMFRate frameRate = {};
		pOutput->GetProperty(FFMPEG_DEMUXER_VIDEO_FRAME_RATE, &frameRate);

		dFps = frameRate.den == 0 ? 0.0 : (static_cast<double>(frameRate.num) / static_cast<double>(frameRate.den));
    }
    //---------------------------------------------------------------------------------------------
    // Init Video Decoder
    res = InitVideoDecoder(pVideoDecoderID.c_str(), pExtraData);
    CHECK_AMF_ERROR_RETURN(res, L"InitVideoDecoder() failed");

    if(m_pVideoDecoder != NULL)
    { 
        m_pVideoDecoder->SetProperty(AMF_TIMESTAMP_MODE, amf_int64(m_pVideoStreamParser != NULL ? AMF_TS_DECODE : AMF_TS_SORT)); // our sample H264 parser provides decode order timestamps- change depend on demuxer
    }
    //---------------------------------------------------------------------------------------------
    // Init Presenter
    res = CreateVideoPresenter(presenterEngine, bitRate, dFps);
    CHECK_AMF_ERROR_RETURN(res, L"CreatePresenter() failed");

    res = m_pVideoPresenter->Init(m_iVideoWidth, m_iVideoHeight);
    CHECK_AMF_ERROR_RETURN(res, L"m_pVideoPresenter->Init(" << m_iVideoWidth << m_iVideoHeight << ") failed");

    //---------------------------------------------------------------------------------------------
    // Init Video Converter/Processor
    res = InitVideoProcessor();
    CHECK_AMF_ERROR_RETURN(res, L"InitVideoProcessor() failed");

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
            if(m_pVideoProcessor != NULL)
            {
                m_pVideoProcessor->ReInit(m_iVideoWidth, m_iVideoHeight);
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

