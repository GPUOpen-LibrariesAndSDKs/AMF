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
#include "PlaybackPipeline.h"
#include "public/common/AMFFactory.h"

#pragma warning(disable:4355)


const wchar_t* PlaybackPipeline::PARAM_NAME_INPUT      = L"INPUT";
const wchar_t* PlaybackPipeline::PARAM_NAME_PRESENTER  = L"PRESENTER";
const wchar_t* PlaybackPipeline::PARAM_NAME_FRAMERATE = L"FRAMERATE";

PlaybackPipeline::PlaybackPipeline()
{
    g_AMFFactory.Init();
    SetParamDescription(PARAM_NAME_INPUT, ParamCommon,  L"Input file name");
    SetParamDescription(PARAM_NAME_PRESENTER, ParamCommon,  L"Specifies presenter engine type (DX9, DX11, OPENGL)", ParamConverterVideoPresenter);
    SetParamDescription(PARAM_NAME_FRAMERATE, ParamCommon,  L"Forces Video Frame Rate (double)");


#if defined(METRO_APP)
    SetParam(PlaybackPipeline::PARAM_NAME_PRESENTER, amf::AMF_MEMORY_DX11);
#else
    SetParam(PlaybackPipeline::PARAM_NAME_PRESENTER, amf::AMF_MEMORY_DX9);
#endif

}

PlaybackPipeline::~PlaybackPipeline()
{
    Terminate();
    g_AMFFactory.Terminate();
}

void PlaybackPipeline::Terminate()
{
    Stop();
    m_pContext = NULL;
}

double PlaybackPipeline::GetProgressSize()
{
    amf_int64 size = 100;
    if(m_pStream)
    {
        m_pStream->GetSize(&size);
    }
    return (double)size;
}

double PlaybackPipeline::GetProgressPosition()
{
    amf_int64 pos = 0;
    if(m_pStream)
    {
        m_pStream->GetPosition(&pos);
    }
    return (double)pos;
}


#if !defined(METRO_APP)
AMF_RESULT PlaybackPipeline::Init(HWND hwnd)
#else
AMF_RESULT PlaybackPipeline::Init(const wchar_t* path, IRandomAccessStream^ inputStream, ISwapChainBackgroundPanelNative* pSwapChainPanel, AMFSize swapChainPanelSize)
#endif
{
    Terminate();
    AMF_RESULT res = AMF_OK;
    //---------------------------------------------------------------------------------------------
    // Read Options
    std::wstring inputPath = L"";

#if !defined(METRO_APP)
    res = GetParamWString(PARAM_NAME_INPUT, inputPath);
    CHECK_AMF_ERROR_RETURN(res , L"Input Path");
#else
    inputPath = path;
#endif

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

    // decode options to be played with
    bool bUseDirectOutput = false;

    //---------------------------------------------------------------------------------------------
    // Init context and devices

    g_AMFFactory.GetFactory()->CreateContext(&m_pContext);
    
    if(presenterEngine == amf::AMF_MEMORY_DX9)
    {
        res = m_pContext->InitDX9(NULL);
        CHECK_AMF_ERROR_RETURN(res, "Init DX9");
        bUseDirectOutput = true;
    }

    if(presenterEngine == amf::AMF_MEMORY_DX11)
    {
        res = m_pContext->InitDX11(NULL);
        CHECK_AMF_ERROR_RETURN(res , "Init DX11");
        bUseDirectOutput = true;
    }
    
#if !defined(METRO_APP)
    if(presenterEngine == amf::AMF_MEMORY_OPENGL)
    {
        res = m_pContext->InitDX9(NULL);
        res = m_pContext->InitOpenGL(NULL, hwnd, NULL);
        CHECK_AMF_ERROR_RETURN(res, "Init OpenGL");
        bUseDirectOutput = false;
    }
#endif

    //---------------------------------------------------------------------------------------------
    // Init Video Stream Parser or demuxer
    BitStreamType streamType = GetStreamType(inputPath.c_str());

    amf_int32 iVideoStreamIndex = -1;
    amf_int32 iAudioStreamIndex = -1;

    BitStreamParserPtr      pParser;
    amf::AMFOutputPtr       pAudioOutput;
    amf::AMFOutputPtr       pVideoOutput;
    if( streamType != BitStreamUnknown)
    { 
#if !defined(METRO_APP)
        amf::AMFDataStream::OpenDataStream(inputPath.c_str(), amf::AMFSO_READ, amf::AMFFS_SHARE_READ, &m_pStream);
#else
        amf::AMFDataStream::OpenDataStream(inputPath.c_str(), amf::AMFSO_READ, amf::AMFFS_SHARE_READ, &m_pStream);
#endif
        CHECK_RETURN(m_pStream != NULL, AMF_FILE_NOT_OPEN, "Open File");

        pParser = BitStreamParser::Create(m_pStream, streamType, m_pContext);
        CHECK_RETURN(pParser != NULL, AMF_FILE_NOT_OPEN, "BitStreamParser::Create");

        double framerate = 0;
        if(GetParam(PARAM_NAME_FRAMERATE, framerate) == AMF_OK && framerate != 0)
        {
            pParser->SetFrameRate(framerate);
        }
        iVideoStreamIndex = 0;
    }
    else
    {
        amf::AMFComponentPtr  pDemuxer;

        res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", FFMPEG_DEMUXER, &pDemuxer);
        CHECK_AMF_ERROR_RETURN(res, L"AMFCreateComponent(" << FFMPEG_DEMUXER << L") failed");
        m_pDemuxer = amf::AMFComponentExPtr(pDemuxer);

        m_pDemuxer->SetProperty(FFMPEG_DEMUXER_PATH, inputPath.c_str());
        res = m_pDemuxer->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
        CHECK_AMF_ERROR_RETURN(res, L"m_pDemuxer->Init() failed");


        amf_int32 outputs = m_pDemuxer->GetOutputCount();
        for(amf_int32 output = 0; output < outputs; output++)
        {
            amf::AMFOutputPtr pOutput;
            res = m_pDemuxer->GetOutput(output, &pOutput);
            CHECK_AMF_ERROR_RETURN(res, L"m_pDemuxer->GetOutput() failed");

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
    //---------------------------------------------------------------------------------------------
    // init streams
    if(iVideoStreamIndex >= 0)
    {
        InitVideo(pParser, pVideoOutput, presenterEngine, hwnd, bUseDirectOutput);
    }
    if(iAudioStreamIndex >= 0)
    {
        InitAudio(pAudioOutput);
    }

    //---------------------------------------------------------------------------------------------
    // Connect pipeline
    PipelineElementPtr pPipelineElementDemuxer;
    if(pParser != NULL)
    { 
        Connect(pParser, 10);
        Connect(PipelineElementPtr(new AMFComponentElement(m_pDecoder)), 4);
    }
    else
    {
        pPipelineElementDemuxer = PipelineElementPtr(new AMFComponentExElement(m_pDemuxer));
        Connect(pPipelineElementDemuxer, 10);

        // video
        Connect(PipelineElementPtr(new AMFComponentElement(m_pDecoder)), 0, pPipelineElementDemuxer, iVideoStreamIndex, 4, CT_ThreadQueue);

    }
    Connect(PipelineElementPtr(new AMFComponentElement(m_pConverter)), 4);
    if(bUseDirectOutput)
    {
        Connect(m_pPresenter, 4, CT_ThreadPoll); // if back buffers are used in multithreading the video memory changes every Present() call so pointer returned by GetBackBuffer() points to wrong memory
    }
    else
    {
        Connect(m_pPresenter, 4);
    }
    if(iAudioStreamIndex >= 0 && m_pAudioPresenter != NULL && pPipelineElementDemuxer != NULL)
    {
        Connect(PipelineElementPtr(new AMFComponentElement(m_pAudioDecoder)), 0, pPipelineElementDemuxer, iAudioStreamIndex, 100, CT_ThreadQueue);
        Connect(PipelineElementPtr(new AMFComponentElement(m_pAudioConverter)), 10);
        Connect(m_pAudioPresenter, 10);
    }

    return AMF_OK;
}

AMF_RESULT  PlaybackPipeline::InitVideoProcessor(bool bUseDirectOutput, amf_int32 videoWidth, amf_int32 videoHeight)
{
    AMF_RESULT res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, AMFVideoConverter, &m_pConverter);
    CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << AMFVideoConverter << L") failed");

    if (bUseDirectOutput)
    {
        m_pConverter->SetProperty(AMF_VIDEO_CONVERTER_KEEP_ASPECT_RATIO, true);
        m_pConverter->SetProperty(AMF_VIDEO_CONVERTER_FILL, true);
        m_pPresenter->SetConverter(m_pConverter);
    }
    m_pConverter->SetProperty(AMF_VIDEO_CONVERTER_MEMORY_TYPE, m_pPresenter->GetMemoryType());
    m_pConverter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_FORMAT, m_pPresenter->GetInputFormat());

    m_pConverter->Init(amf::AMF_SURFACE_NV12, videoWidth, videoHeight);

    return AMF_OK;
}

AMF_RESULT  PlaybackPipeline::InitVideoDecoder(const wchar_t *pDecoderID, amf_int32 videoWidth, amf_int32 videoHeight, amf::AMFBuffer* pExtraData)
{
    AMF_VIDEO_DECODER_MODE_ENUM   decoderMode = AMF_VIDEO_DECODER_MODE_COMPLIANT; //amf:::AMF_VIDEO_DECODER_MODE_REGULAR , AMF_VIDEO_DECODER_MODE_LOW_LATENCY;

    AMF_RESULT res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, pDecoderID, &m_pDecoder);
    CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << AMFVideoDecoderUVD_H264_AVC << L") failed");

    m_pDecoder->SetProperty(AMF_VIDEO_DECODER_REORDER_MODE, amf_int64(decoderMode));
        
    m_pDecoder->SetProperty(AMF_VIDEO_DECODER_EXTRADATA, amf::AMFVariant(pExtraData));

    m_pDecoder->SetProperty(AMF_TIMESTAMP_MODE, amf_int64(AMF_TS_DECODE)); // our sample H264 parser provides decode order timestamps- change depend on demuxer
    m_pDecoder->Init(amf::AMF_SURFACE_NV12, videoWidth, videoHeight);

    return AMF_OK;
}

AMF_RESULT PlaybackPipeline::Play()
{
    switch(GetState())
    {
    case PipelineStateRunning:
        return m_pPresenter->Resume();
    case PipelineStateReady:
        return Start();
    case PipelineStateNotReady:
    case PipelineStateEof:
    default:
        break;
    }
    return AMF_WRONG_STATE;
}

AMF_RESULT PlaybackPipeline::Pause()
{
    switch(GetState())
    {
    case PipelineStateRunning:
        return m_pPresenter->Pause();
    case PipelineStateReady:
    case PipelineStateNotReady:
    case PipelineStateEof:
    default:
        break;
    }
    return AMF_WRONG_STATE;
}

AMF_RESULT PlaybackPipeline::Step()
{
    switch(GetState())
    {
    case PipelineStateRunning:
        return m_pPresenter->Step();
    case PipelineStateReady:
    case PipelineStateNotReady:
    case PipelineStateEof:
    default:
        break;
    }
    return AMF_WRONG_STATE;
}

AMF_RESULT PlaybackPipeline::Stop()
{
    Pipeline::Stop();

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

    if(m_pDecoder != NULL)
    {
        m_pDecoder->Terminate();
        m_pDecoder = NULL;
    }
    if(m_pConverter != NULL)
    {
        m_pConverter->Terminate();
        m_pConverter = NULL;
    }
    if(m_pPresenter != NULL)
    {
        m_pPresenter->Terminate();
        m_pPresenter = NULL;
    }
    if(m_pContext != NULL)
    {
        m_pContext->Terminate();
        m_pContext = NULL;
    }
    return AMF_OK;
}

void PlaybackPipeline::OnParamChanged(const wchar_t* name)
{
    if(m_pConverter == NULL)
    {
        return;
    }
    UpdateVideoProcessorProperties(name);
}

void PlaybackPipeline::UpdateVideoProcessorProperties(const wchar_t* name)
{
    if(name != NULL)
    {
        ParamDescription description;
        GetParamDescription(name, description);

        if(description.m_Type == ParamVideoProcessor)
        {
            amf::AMFVariant value;
            GetParam(name, &value);
            m_pConverter->SetProperty(description.m_Name.c_str(), value);
        }
    }
    else
    {
        if(m_pConverter == NULL)
        {
            return;
        }
        for(amf_size i = 0; i < GetParamCount(); ++i)
        {
            std::wstring name;
            amf::AMFVariant value;
            GetParamAt(i, name, &value);
            ParamDescription description;
            GetParamDescription(name.c_str(), description);
            if(description.m_Type == ParamVideoProcessor)
            {
                m_pConverter->SetProperty(description.m_Name.c_str(), value);
            }
        }
    }
}
double              PlaybackPipeline::GetFPS()
{
    if(m_pPresenter != NULL)
    {
        return m_pPresenter->GetFPS();
    }
    return 0;
}
amf_int64           PlaybackPipeline::GetFramesDropped()
{
    if(m_pPresenter != NULL)
    {
        return m_pPresenter->GetFramesDropped();
    }
    return 0;
}
AMF_RESULT  PlaybackPipeline::InitAudio(amf::AMFOutput* pOutput)
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

    m_pAudioPresenter = AudioPresenter::Create(m_pContext);

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
AMF_RESULT  PlaybackPipeline::InitVideo(BitStreamParserPtr pParser, amf::AMFOutput* pOutput, amf::AMF_MEMORY_TYPE presenterEngine, HWND hwnd, bool bUseDirectOutput)
{
    bool decodeAsAnnexBStream = false; // switches between Annex B and AVCC types of decode input.

    AMF_RESULT res = AMF_OK; 
    amf::AMFBufferPtr pExtraData;
    amf_int32 videoWidth = 0;
    amf_int32 videoHeight = 0;
    std::wstring pVideoDecoderID;
    if(pParser != NULL)
    {
        if(!decodeAsAnnexBStream) // need to provide SPS/PPS if input stream will be AVCC ( not Annex B)
        {
            const unsigned char* extraData = pParser->GetExtraData();
            size_t extraDataSize = pParser->GetExtraDataSize();
            if (extraData && extraDataSize)
            {
                m_pContext->AllocBuffer(amf::AMF_MEMORY_HOST, extraDataSize, &pExtraData);

                memcpy(pExtraData->GetNative(), extraData, extraDataSize);
            }
        }
        videoWidth = pParser->GetPictureWidth();
        videoHeight = pParser->GetPictureHeight();
    
        pVideoDecoderID = pParser->GetCodecComponent();
    }
    else if(pOutput != NULL)
    { 
        amf::AMFInterfacePtr pInterface;
        pOutput->GetProperty(FFMPEG_DEMUXER_EXTRA_DATA, &pInterface);
        pExtraData = amf::AMFBufferPtr(pInterface);

        AMFSize frameSize;
        pOutput->GetProperty(FFMPEG_DEMUXER_VIDEO_FRAMESIZE, &frameSize);
        videoWidth = frameSize.width;
        videoHeight = frameSize.height;
        
        amf::AMFVariant var;
        res= pOutput->GetProperty(FFMPEG_DEMUXER_VIDEO_DECODER_ID, &var);
        if(res == AMF_OK)  // video stream setup
        {
            pVideoDecoderID = var.ToWString().c_str();
        }
        pOutput->SetProperty(FFMPEG_DEMUXER_STREAM_ENABLED, true);
    }
    //---------------------------------------------------------------------------------------------
    // Init Video Decoder
    res = InitVideoDecoder(pVideoDecoderID.c_str(), videoWidth, videoHeight, pExtraData);
    CHECK_AMF_ERROR_RETURN(res, L"InitVideoDecoder() failed");

    //---------------------------------------------------------------------------------------------
    // Init Presenter
#if !defined(METRO_APP)
    m_pPresenter = VideoPresenter::Create(presenterEngine, hwnd, m_pContext);
#else
    m_pPresenter = VideoPresenter::Create(pSwapChainPanel, swapChainPanelSize, m_pContext);
#endif
    m_pPresenter->Init(videoWidth, videoHeight);

    //---------------------------------------------------------------------------------------------
    // Init Video Converter/Processor
    res = InitVideoProcessor(bUseDirectOutput, videoWidth, videoHeight);
    CHECK_AMF_ERROR_RETURN(res, L"InitVideoProcessor() failed");
    return AMF_OK;
}