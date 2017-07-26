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

extern "C"
{
    #include "config.h"
#ifdef _WIN32
    #if defined HAVE_STRUCT_POLLFD
        #undef HAVE_STRUCT_POLLFD
    #endif
        #define     HAVE_STRUCT_POLLFD 1
#endif
    #include "libavformat/internal.h"
}

#include "FileMuxerFFMPEGImpl.h"
#include "UtilsFFMPEG.h"

#include "public/include/components/VideoDecoderUVD.h"
#include "public/include/components/VideoEncoderVCE.h"
#include "public/include/core/Context.h"
#include "public/include/core/Trace.h"
#include "public/common/TraceAdapter.h"
#include "public/common/AMFFactory.h"
#include "public/common/DataStream.h"

#define FAKE_MUXING 0

#define AMF_FACILITY            L"AMFFileMuxerFFMPEGImpl"
#define MY_AV_NOPTS_VALUE       ((int64_t)0x8000000000000000LL)
#define ENABLE_H265

using namespace amf;


const AMFEnumDescriptionEntry VIDEO_CODEC_IDS_ENUM[] =
{
    { AV_CODEC_ID_NONE, L"UNKNOWN" },
    { AV_CODEC_ID_MPEG2VIDEO, AMFVideoDecoderUVD_MPEG2 },
    { AV_CODEC_ID_MPEG4, AMFVideoDecoderUVD_MPEG4 },
    { AV_CODEC_ID_WMV3, AMFVideoDecoderUVD_WMV3 },
    { AV_CODEC_ID_VC1, AMFVideoDecoderUVD_VC1 },
    { AV_CODEC_ID_H264, AMFVideoDecoderUVD_H264_AVC },
    { AV_CODEC_H264MVC, AMFVideoDecoderUVD_H264_MVC },
    { AV_CODEC_ID_MJPEG, AMFVideoDecoderUVD_MJPEG },
#if defined(ENABLE_H265)
    { AV_CODEC_ID_HEVC, AMFVideoDecoderHW_H265_HEVC },
#endif
    { 0, 0 }
};

const AMFEnumDescriptionEntry AMF_OUTPUT_FORMATS_ENUM[] =
{
    { AMF_SURFACE_UNKNOWN, L"DEFAULT" },
    { AMF_SURFACE_BGRA, L"BGRA" },
    { AMF_SURFACE_RGBA, L"RGBA" },
    { AMF_SURFACE_ARGB, L"ARGB" },
    { AMF_SURFACE_NV12, L"NV12" },
    { AMF_SURFACE_YUV420P, L"YUV420P" },
    { AMF_SURFACE_YV12, L"YV12" },

    { AMF_SURFACE_UNKNOWN, 0 }  // This is end of description mark
};

const AMFEnumDescriptionEntry AMF_SAMPLE_FORMAT_ENUM_DESCRIPTION[] =
{
    { AMFAF_UNKNOWN, L"UNKNOWN" },
    { AMFAF_U8, L"U8" },
    { AMFAF_S16, L"S16" },
    { AMFAF_S32, L"S32" },
    { AMFAF_FLT, L"FLT" },
    { AMFAF_DBL, L"DBL" },
    { AMFAF_U8P, L"U8P" },
    { AMFAF_S16P, L"S16P" },
    { AMFAF_S32P, L"S32P" },
    { AMFAF_FLTP, L"FLTP" },
    { AMFAF_DBLP, L"DBLP" },

    { AMFAF_UNKNOWN, 0 }  // This is end of description mark
};

const AMFEnumDescriptionEntry FFMPEG_MUXER_STREAM_TYPE_ENUM_DESCRIPTION[] =
{
    {MUXER_UNKNOWN,   L"Unknown"},
    {MUXER_VIDEO,     L"Video"},
    {MUXER_AUDIO,     L"Audio"},
    {MUXER_DATA,      L"Data"},
    { MUXER_UNKNOWN   , 0 }  // This is end of description mark
};

struct FormatMap
{
    AVPixelFormat ffmpegFormat;
    amf::AMF_SURFACE_FORMAT amfFormat;
    FormatMap(AVPixelFormat ffmpeg,
        amf::AMF_SURFACE_FORMAT amf) :ffmpegFormat(ffmpeg), amfFormat(amf){}
}

const sFormatMap[] =
{
    FormatMap(AV_PIX_FMT_NONE, AMF_SURFACE_UNKNOWN),
    FormatMap(AV_PIX_FMT_NV12, AMF_SURFACE_NV12),
    FormatMap(AV_PIX_FMT_BGRA, AMF_SURFACE_BGRA),
    FormatMap(AV_PIX_FMT_ARGB, AMF_SURFACE_ARGB),
    FormatMap(AV_PIX_FMT_RGBA, AMF_SURFACE_RGBA),
    FormatMap(AV_PIX_FMT_GRAY8, AMF_SURFACE_GRAY8),
    FormatMap(AV_PIX_FMT_YUV420P, AMF_SURFACE_YUV420P),
    FormatMap(AV_PIX_FMT_BGR0, AMF_SURFACE_BGRA),
    FormatMap(AV_PIX_FMT_YUV420P, AMF_SURFACE_YV12),

    FormatMap(AV_PIX_FMT_YUYV422, AMF_SURFACE_YUY2),
    //FormatMap(PIX_FMT_YUV422P, AMF_SURFACE_YUV422P),
    //FormatMap(PIX_FMT_YUVJ422P, AMF_SURFACE_YUV422P)
};


//
//
// AMFInputMuxerImpl
//
//

//-------------------------------------------------------------------------------------------------
AMFFileMuxerFFMPEGImpl::AMFInputMuxerImpl::AMFInputMuxerImpl(AMFFileMuxerFFMPEGImpl* pHost) 
    : m_pHost(pHost),
      m_iIndex(-1),
      m_bEnabled(true),
      m_iPacketCount(0),
      m_ptsLast(0),
      m_ptsShift(0)
{
}
//-------------------------------------------------------------------------------------------------
AMFFileMuxerFFMPEGImpl::AMFInputMuxerImpl::~AMFInputMuxerImpl()
{
    AMFTraceInfo(AMF_FACILITY,L"Stream# %d, packets written %d", m_iIndex, (int)m_iPacketCount);
}
//-------------------------------------------------------------------------------------------------
// NOTE: this call will return one compressed frame for each QueryOutput call
AMF_RESULT AMF_STD_CALL  AMFFileMuxerFFMPEGImpl::AMFInputMuxerImpl::SubmitInput(AMFData* pData)
{
    AMF_RETURN_IF_FALSE(m_pHost != NULL, AMF_UNEXPECTED, L"SubmitInput() - Host not Initialized");
    AMF_RETURN_IF_FALSE(m_pHost->m_pOutputContext != NULL, AMF_NOT_INITIALIZED, L"SubmitInput() - Input Context not Initialized");

    if (m_bEnabled)
    {
        m_iPacketCount++;
        bool bVideo = m_pHost->m_pOutputContext->streams[m_iIndex]->codec->codec_type == AVMEDIA_TYPE_VIDEO;
//        AMFTraceW(L"",0, AMF_TRACE_INFO, AMF_FACILITY, 2, L"Stream# %s, packet # %d, time = %.2f ms", bVideo ? L"Video" : L"Audio", (int)m_iPacketCount, pData->GetPts() / 10000.);
        if(pData != NULL)
        {
            m_ptsLast = pData->GetPts() + m_ptsShift;
            pData->SetPts(m_ptsLast);
            
        }
        return m_pHost->WriteData(pData, m_iIndex);

    }
    return pData != NULL ? AMF_OK : AMF_EOF;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL  AMFFileMuxerFFMPEGImpl::AMFInputMuxerImpl::Drain()
{
    m_ptsShift = m_ptsLast;
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
//
//
// AMFVideoInputMuxerImpl
//
//

void AMF_STD_CALL  AMFFileMuxerFFMPEGImpl::AMFInputMuxerImpl::OnPropertyChanged(const wchar_t* pName)
{
    const amf_wstring  name(pName);
    if (name == FFMPEG_MUXER_STREAM_ENABLED)
    {
        AMFLock lock(&m_pHost->m_sync);
        AMFPropertyStorage::GetProperty(FFMPEG_MUXER_STREAM_ENABLED, &m_bEnabled);
        return;
    }
}
//-------------------------------------------------------------------------------------------------
AMFFileMuxerFFMPEGImpl::AMFVideoInputMuxerImpl::AMFVideoInputMuxerImpl(AMFFileMuxerFFMPEGImpl* pHost)
    : AMFFileMuxerFFMPEGImpl::AMFInputMuxerImpl(pHost)
{
    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoEnum(FFMPEG_MUXER_STREAM_TYPE, L"Stream Type", MUXER_VIDEO, FFMPEG_MUXER_STREAM_TYPE_ENUM_DESCRIPTION, false),
        AMFPropertyInfoBool(FFMPEG_MUXER_STREAM_ENABLED, L"Enabled", true, false),
        AMFPropertyInfoEnum(FFMPEG_MUXER_CODEC_ID, L"Codec ID", AV_CODEC_ID_H264, VIDEO_CODEC_IDS_ENUM, false),
        AMFPropertyInfoInt64(FFMPEG_MUXER_BIT_RATE, L"Bit Rate", 100000, 1, INT_MAX, false),
        AMFPropertyInfoInterface(FFMPEG_MUXER_EXTRA_DATA, L"Extra Data", NULL, false),
        AMFPropertyInfoRate(FFMPEG_MUXER_VIDEO_FRAME_RATE, L"Frame Rate", 30, 1, false),
        AMFPropertyInfoSize(FFMPEG_MUXER_VIDEO_FRAMESIZE, L"Frame Size", AMFConstructSize(1920,1080), AMFConstructSize(1,1), AMFConstructSize(100000,100000), false),
    AMFPrimitivePropertyInfoMapEnd
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL  AMFFileMuxerFFMPEGImpl::AMFVideoInputMuxerImpl::Init()
{
    for(int st = 0; st < (int)m_pHost->m_pOutputContext->nb_streams; st++)
    {
        if(m_pHost->m_pOutputContext->streams[st]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            m_iIndex = st;
            break;
        }
    }
    if(m_iIndex < 0 )
    {
        return AMF_FAIL;
    }
    return AMF_OK;
}

//
//
// AMFAudioInputMuxerImpl
//
//

//-------------------------------------------------------------------------------------------------
AMFFileMuxerFFMPEGImpl::AMFAudioInputMuxerImpl::AMFAudioInputMuxerImpl(AMFFileMuxerFFMPEGImpl* pHost)
    : AMFFileMuxerFFMPEGImpl::AMFInputMuxerImpl(pHost)
{
    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoEnum(FFMPEG_MUXER_STREAM_TYPE, L"Stream Type", MUXER_AUDIO, FFMPEG_MUXER_STREAM_TYPE_ENUM_DESCRIPTION, false),
        AMFPropertyInfoBool(FFMPEG_MUXER_STREAM_ENABLED, L"Enabled", true, false),
        AMFPropertyInfoInt64(FFMPEG_MUXER_CODEC_ID, L"Codec ID", AV_CODEC_ID_NONE, AV_CODEC_ID_NONE, INT_MAX, false),
        AMFPropertyInfoInt64(FFMPEG_MUXER_BIT_RATE, L"Bit Rate", 100000, 1, INT_MAX, false),
        AMFPropertyInfoInterface(FFMPEG_MUXER_EXTRA_DATA, L"Extra Data", NULL, false),

        AMFPropertyInfoInt64(FFMPEG_MUXER_AUDIO_SAMPLE_RATE, L"Sample Rate", 44100, 0, INT_MAX, false),
        AMFPropertyInfoInt64(FFMPEG_MUXER_AUDIO_CHANNELS, L"Channels", 2, 1, INT_MAX, false),
        AMFPropertyInfoEnum(FFMPEG_MUXER_AUDIO_SAMPLE_FORMAT, L"Sample Format", AMFAF_UNKNOWN, AMF_SAMPLE_FORMAT_ENUM_DESCRIPTION, false),
        AMFPropertyInfoInt64(FFMPEG_MUXER_AUDIO_CHANNEL_LAYOUT, L"Channel Layout", 3, 0, INT_MAX, false),
        AMFPropertyInfoInt64(FFMPEG_MUXER_AUDIO_BLOCK_ALIGN, L"Block Align", 0, 0, INT_MAX, false),
        AMFPropertyInfoInt64(FFMPEG_MUXER_AUDIO_FRAME_SIZE, L"Frame Size", 0, 0, INT_MAX, false),
    AMFPrimitivePropertyInfoMapEnd

}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL  AMFFileMuxerFFMPEGImpl::AMFAudioInputMuxerImpl::Init()
{
    for(int st = 0; st < (int)m_pHost->m_pOutputContext->nb_streams; st++)
    {
        if(m_pHost->m_pOutputContext->streams[st]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            m_iIndex = st;
            break;
        }
    }
    if(m_iIndex < 0 )
    {
        return AMF_FAIL;
    }
    return AMF_OK;
}

//
//
// AMFFileMuxerFFMPEGImpl
//
//

//-------------------------------------------------------------------------------------------------
AMFFileMuxerFFMPEGImpl::AMFFileMuxerFFMPEGImpl(AMFContext* pContext)
  : m_pContext(pContext),
    m_pOutputContext(NULL),
    m_bHeaderIsWritten(false),
    m_bTerminated(true),
    m_bForceEof(false),
    m_iViewFrameCount(0),
    m_ptsStatTime(0)
{
    g_AMFFactory.Init();

    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoPath(FFMPEG_MUXER_PATH, L"File Path", L"", false),
        AMFPropertyInfoPath(FFMPEG_MUXER_URL, L"Stream URL", L"", false),
        AMFPropertyInfoBool(FFMPEG_MUXER_ENABLE_VIDEO, L"Enable video stream", true, true),
        AMFPropertyInfoBool(FFMPEG_MUXER_ENABLE_AUDIO, L"Enable audio stream", false, true),
        AMFPropertyInfoBool(FFMPEG_MUXER_LISTEN, L"Listen", false, false)

    AMFPrimitivePropertyInfoMapEnd

    m_InputStreams.push_back(new AMFVideoInputMuxerImpl(this));

    InitFFMPEG();
}
//-------------------------------------------------------------------------------------------------
AMFFileMuxerFFMPEGImpl::~AMFFileMuxerFFMPEGImpl()
{
    Terminate();
    g_AMFFactory.Terminate();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileMuxerFFMPEGImpl::Init(AMF_SURFACE_FORMAT /*format*/, amf_int32 /*width*/, amf_int32 /*height*/)
{
    AMFLock lock(&m_sync);

    Close();
    AMF_RESULT res = Open();
    AMF_RETURN_IF_FAILED(res, L"Open() failed");

    for(amf_vector<AMFInputMuxerImplPtr>::iterator it = m_InputStreams.begin(); it != m_InputStreams.end(); it++)
    {
        (*it)->Init();
    }
    return res;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileMuxerFFMPEGImpl::ReInit(amf_int32 width, amf_int32 height)
{
    Terminate();
    return Init(AMF_SURFACE_UNKNOWN, width, height);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileMuxerFFMPEGImpl::Terminate()
{
    AMFLock lock(&m_sync);

    m_bTerminated = true;

    Close();

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileMuxerFFMPEGImpl::Drain()
{
    AMFLock lock(&m_sync);

    for(amf_vector<AMFInputMuxerImplPtr>::iterator it = m_InputStreams.begin(); it != m_InputStreams.end(); it++)
    {
        (*it)->Drain();
    }
    m_bForceEof = true;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileMuxerFFMPEGImpl::GetInput(amf_int32 index, AMFInput** ppInput)
{
    AMF_RETURN_IF_FALSE(index >= 0 && index < (amf_int32) m_InputStreams.size(), AMF_INVALID_ARG, L"Invalid index");
    AMF_RETURN_IF_FALSE(ppInput != NULL, AMF_INVALID_ARG, L"ppOutput = NULL");

    *ppInput = m_InputStreams[index];
    (*ppInput)->Acquire();

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL  AMFFileMuxerFFMPEGImpl::OnPropertyChanged(const wchar_t* pName)
{
    AMFLock lock(&m_sync);

    const amf_wstring  name(pName);
    if(name == FFMPEG_MUXER_ENABLE_VIDEO || name == FFMPEG_MUXER_ENABLE_AUDIO)
    {
        FFMPEG_MUXER_STREAM_TYPE_ENUM eType = name == FFMPEG_MUXER_ENABLE_AUDIO ? MUXER_AUDIO : MUXER_VIDEO;

        bool enableStream = false;
        GetProperty(name.c_str(), &enableStream);

        bool bFound = false;
        for(amf_vector<AMFInputMuxerImplPtr>::iterator it = m_InputStreams.begin(); it != m_InputStreams.end(); it++)
        {
            amf_int64 type;
            (*it)->GetProperty(FFMPEG_MUXER_STREAM_TYPE, &type);
            if(type == eType)
            {
                bFound = true;
                if(!enableStream)
                {
                    m_InputStreams.erase(it);
                }
                break;
            }
        }
        if(!bFound && enableStream)
        {
            if(eType == MUXER_VIDEO)
            {
                m_InputStreams.push_back(new AMFVideoInputMuxerImpl(this));
            }
            else
            {
                m_InputStreams.push_back(new AMFAudioInputMuxerImpl(this));
            }

        }
    }
}


//
//
// protected
//
//
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileMuxerFFMPEGImpl::AllocateContext()
{
    m_pOutputContext = avformat_alloc_context();
    m_pOutputContext->nb_streams = (amf_uint32) GetInputCount();

    if (m_pOutputContext->streams)
    {
        amf_free(m_pOutputContext->streams);
    }
    m_pOutputContext->streams = (AVStream**) amf_alloc(m_pOutputContext->nb_streams * sizeof(AVStream*)); 

    for (unsigned int ind=0; ind < m_pOutputContext->nb_streams; ind++) 
    {
        AVStream *ist = 0;
        ist = (AVStream*)av_mallocz(sizeof(AVStream));
        if (ist==NULL)
        {
            return AMF_OUT_OF_MEMORY;
        }
        m_pOutputContext->streams[ind] = ist;
        ist->index = (amf_int32)ind;

        ist->id = AVMEDIA_TYPE_UNKNOWN;
        ist->start_time = MY_AV_NOPTS_VALUE;
        ist->duration = MY_AV_NOPTS_VALUE;
        ist->cur_dts = MY_AV_NOPTS_VALUE;
        ist->first_dts = MY_AV_NOPTS_VALUE;

        ist->last_IP_pts = MY_AV_NOPTS_VALUE;
        for(int i=0; i<MAX_REORDER_DELAY+1; i++)
            ist->pts_buffer[i]= MY_AV_NOPTS_VALUE;

        ist->sample_aspect_ratio.den=1;
        ist->sample_aspect_ratio.num=0;
        // fake codec - not opened - some params will come from real Encoder later
        ist->codec= avcodec_alloc_context3(NULL);
        ist->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
        ist->codecpar = avcodec_parameters_alloc();

              

        AMFInputMuxerImplPtr  spInput    = m_InputStreams[ind];
        amf_int64             streamType = MUXER_UNKNOWN;
        AMF_RETURN_IF_FAILED(spInput->GetProperty(FFMPEG_MUXER_STREAM_TYPE, &streamType));

        switch (streamType)
        {
        case MUXER_VIDEO:   ist->id=AVMEDIA_TYPE_VIDEO; break;
        case MUXER_AUDIO:   ist->id=AVMEDIA_TYPE_AUDIO; break;
        case MUXER_DATA:    ist->id=AVMEDIA_TYPE_DATA; break;
        case MUXER_UNKNOWN: 
        default:            ist->id=AVMEDIA_TYPE_UNKNOWN; break;
        }

        ist->codec->codec_type=(AVMediaType)ist->id;
        ist->codecpar->codec_type=(AVMediaType)ist->id;

        //Codec ID is FFMPEG's
        amf_int64  codecID = AV_CODEC_ID_NONE;
        AMF_RETURN_IF_FAILED(spInput->GetProperty(FFMPEG_MUXER_CODEC_ID, &codecID));
        ist->codec->codec_id = (AVCodecID) codecID;
        ist->codecpar->codec_id = (AVCodecID) codecID;

        AMF_RETURN_IF_FAILED(spInput->GetProperty(FFMPEG_MUXER_BIT_RATE, &ist->codec->bit_rate));
        ist->codecpar->bit_rate = ist->codec->bit_rate;

        AMFVariant val;
        AMF_RETURN_IF_FAILED(spInput->GetProperty(FFMPEG_MUXER_EXTRA_DATA, &val));
        if (!val.Empty() && val.pInterface)
        {
            // NOTE: the buffer ptr. shouldn't disappear as the 
            //       property holds it in the end
            AMFBufferPtr pBuffer = AMFBufferPtr(val.pInterface);
            ist->codec->extradata = (uint8_t*) pBuffer->GetNative();
            ist->codec->extradata_size = (int) pBuffer->GetSize();

            ist->codecpar->extradata = (uint8_t*) pBuffer->GetNative();
            ist->codecpar->extradata_size = (int) pBuffer->GetSize();
        }

        ist->internal = (AVStreamInternal*)av_mallocz(sizeof(*ist->internal));
        ist->internal->avctx = avcodec_alloc_context3(NULL);


        if (streamType==MUXER_VIDEO)
        {
            AMFRate  frameRate;
            AMF_RETURN_IF_FAILED(spInput->GetProperty(FFMPEG_MUXER_VIDEO_FRAME_RATE, &frameRate));

            // default pts settings is MPEG like 
            avpriv_set_pts_info(ist, 33, 1, 90000);
// this is set in the line above 
            ist->time_base.num = frameRate.num;
            ist->time_base.den = frameRate.den;

            ist->codec->time_base=ist->time_base;


            AMFSize frame;
            AMF_RETURN_IF_FAILED(spInput->GetProperty(FFMPEG_MUXER_VIDEO_FRAMESIZE, &frame));

            ist->codec->width = frame.width;
            ist->codec->height = frame.height;
            ist->codecpar->width = frame.width;
            ist->codecpar->height = frame.height;

            ist->codec->framerate.num =frameRate.num;
            ist->codec->framerate.den =frameRate.den;

            ist->codec->pix_fmt = AV_PIX_FMT_YUV420P; // always
            ist->codecpar->format = AV_PIX_FMT_YUV420P; // always

        }
        else if (streamType==MUXER_AUDIO)
        {
            AMF_RETURN_IF_FAILED(spInput->GetProperty(FFMPEG_MUXER_AUDIO_SAMPLE_RATE, &ist->codec->sample_rate));
            AMF_RETURN_IF_FAILED(spInput->GetProperty(FFMPEG_MUXER_AUDIO_CHANNELS, &ist->codec->channels));
            ist->codecpar->sample_rate = ist->codec->sample_rate;
            ist->codecpar->channels = ist->codec->channels;


            amf_int64  sampleFormat = AMFAF_UNKNOWN;
            AMF_RETURN_IF_FAILED(spInput->GetProperty(FFMPEG_MUXER_AUDIO_SAMPLE_FORMAT, &sampleFormat));

            ist->codec->sample_fmt=GetFFMPEGAudioFormat((AMF_AUDIO_FORMAT) sampleFormat);
            ist->codecpar->format = ist->codec->sample_fmt;

            AMF_RETURN_IF_FAILED(spInput->GetProperty(FFMPEG_MUXER_AUDIO_CHANNEL_LAYOUT, &ist->codec->channel_layout));
            AMF_RETURN_IF_FAILED(spInput->GetProperty(FFMPEG_MUXER_AUDIO_BLOCK_ALIGN, &ist->codec->block_align));
            AMF_RETURN_IF_FAILED(spInput->GetProperty(FFMPEG_MUXER_AUDIO_FRAME_SIZE, &ist->codec->frame_size));

            ist->codecpar->channel_layout = ist->codec->channel_layout;
            ist->codecpar->block_align = ist->codec->block_align;
            ist->codecpar->frame_size = ist->codec->frame_size;

            ist->codec->time_base.num = 1;
            ist->codec->time_base.den = ist->codec->sample_rate;
            ist->time_base=ist->codec->time_base;
        }
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileMuxerFFMPEGImpl::FreeContext()
{
    if(m_pOutputContext!=NULL)
    {
        for(unsigned int j=0;j<m_pOutputContext->nb_streams;j++) 
        {
            av_free(m_pOutputContext->streams[j]->internal->avctx);
            av_free(m_pOutputContext->streams[j]->codec);
            av_free(m_pOutputContext->streams[j]->codecpar);
            av_free(m_pOutputContext->streams[j]);
        }

        if(m_pOutputContext->streams)
        {
            amf_free( m_pOutputContext->streams );        
        }

        m_pOutputContext->nb_streams=0;
        av_free(m_pOutputContext);
        m_pOutputContext=NULL;
    }

    m_bHeaderIsWritten = false;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileMuxerFFMPEGImpl::Open()
{
    Close();
    AllocateContext();

    amf_wstring path;
    amf_wstring url;
    GetPropertyWString(FFMPEG_MUXER_PATH, &path);
    GetPropertyWString(FFMPEG_MUXER_URL, &url);

    AVOutputFormat* file_oformat = NULL;
    amf_string convertedfilename;
    bool bListen = false;
    bool bRtmpLive = false;
    if(url.length() > 0)
    {
        GetProperty(FFMPEG_MUXER_LISTEN, &bListen);
        convertedfilename = amf_from_unicode_to_utf8(url);
        amf_string::size_type pos = convertedfilename.find(':');
        if(pos != amf_string::npos)
        {
            amf_string protocol = convertedfilename.substr(0, pos);
            protocol = amf_string_to_lower(protocol);
            if (protocol == "rtmp")
            {
                protocol = "flv";
                bRtmpLive = true;
            }
            else if (protocol == "tcp")
            {
                protocol = "mpegts";
            }
            file_oformat = av_guess_format(protocol.c_str(), NULL, NULL);
        }
    }
    else if(path.length() > 0)
    {
        convertedfilename = amf_string("file:") + amf_from_unicode_to_multibyte(path);
    }
    if(file_oformat == NULL)
    {
        file_oformat = av_guess_format(NULL, convertedfilename.c_str(), NULL);
    }

    if (file_oformat==NULL)
    {
        return AMF_FILE_NOT_OPEN;
    }
    
    m_pOutputContext->oformat = file_oformat;

    int iret = 0;
    AVDictionary *options = NULL;
    if(bListen)
    { 
        iret = av_dict_set(&options, "listen", "1", 0);
    }
    if(bRtmpLive)
    {
        iret = av_dict_set(&options, "rtmp_live", "live", 0);
    }
    // open file
//    int iret = avio_open(&m_pOutputContext->pb, convertedfilename.c_str(), AVIO_FLAG_WRITE);
#if !FAKE_MUXING
    iret = avio_open2(&m_pOutputContext->pb, convertedfilename.c_str(), AVIO_FLAG_WRITE, NULL, &options);

    if(iret != 0)
    {
        return AMF_FILE_NOT_OPEN;
    }

    AMF_RESULT err = WriteHeader();
    AMF_RETURN_IF_FAILED(err,  L"Open() - WriteHeader() failed");
#endif
    m_bEofList.resize(GetInputCount());
    for(amf_size i=0; i <m_bEofList.size(); i++)
    {
        m_bEofList[i] = false;
    }
    m_iViewFrameCount = 0;
    m_ptsStatTime = 0;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileMuxerFFMPEGImpl::Close()
{
    if(m_pOutputContext && m_pOutputContext->pb)
    {
        if(m_bHeaderIsWritten)
        {
#if !FAKE_MUXING
            av_write_trailer(m_pOutputContext);
#endif
        }
        avio_close(m_pOutputContext->pb);
        m_pOutputContext->pb = 0;
        m_pOutputContext->oformat = 0;
    }
    FreeContext();
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileMuxerFFMPEGImpl::WriteHeader()
{
    if (!m_bHeaderIsWritten)
    {
        m_bHeaderIsWritten = true;
#if !FAKE_MUXING
        int ret = avformat_write_header(m_pOutputContext, NULL);
        if (ret != 0)
        {
            return AMF_FAIL;
        }
#endif
    }
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileMuxerFFMPEGImpl::WriteData(AMFData* pData, amf_int32 iIndex)
{
    AMF_RETURN_IF_FALSE(iIndex >= 0 && iIndex < (amf_int32) m_InputStreams.size(), AMF_INVALID_ARG, L"Invalid index");

    AMFLock lock(&m_sync);

    if (pData)
    {
        m_bForceEof = false;
        AVStream *ost = m_pOutputContext->streams[iIndex];

        // fill packet 
        AVPacket  pkt = {};
        av_init_packet(&pkt);
#if !FAKE_MUXING

        AMFBufferPtr pInBuffer(pData);
        AMF_RETURN_IF_FALSE(pInBuffer != 0,AMF_INVALID_ARG, L"WriteData() - Input should be Buffer");

        AMF_RESULT err = pInBuffer->Convert(AMF_MEMORY_HOST);
        AMF_RETURN_IF_FAILED(err, L"WriteData() - Convert(AMF_MEMORY_HOST) failed");

        amf_size uiMemSizeIn = pInBuffer->GetSize();
        AMF_RETURN_IF_FALSE(uiMemSizeIn!=0, AMF_INVALID_ARG, L"WriteData() - Invalid param");

        pkt.data = static_cast<uint8_t*>(pInBuffer->GetNative());
        pkt.size = (int)uiMemSizeIn;
        pkt.stream_index = iIndex;
#endif

        // Try to determine the output video frame type
        amf_int64 outputDataType = -1;
        if (AMF_OK == pData->GetProperty(AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE, &outputDataType))
        {
            // set key flag for key frames
            if (outputDataType == AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR || outputDataType == AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_I)
            {
                pkt.flags |= AV_PKT_FLAG_KEY;
            }
        }

        // resample pts
        amf_pts pts = pData->GetPts();
        amf_pts duration = pData->GetDuration();

        pkt.pts=av_rescale_q(pts, AMF_TIME_BASE_Q, ost->time_base);
        pkt.dts=pkt.pts;
        pkt.duration = av_rescale_q(duration, AMF_TIME_BASE_Q, ost->time_base);

        amf_pts currentTime = amf_high_precision_clock();

        if(ost->codec->codec_type  == AVMEDIA_TYPE_VIDEO)
        { 
  
//            AMFTraceWarning(AMF_FACILITY, L"WritePacket() video in_pts=%" LPRId64 L"  pts=%" LPRId64 L", dts=% " LPRId64, pts, pkt.pts, pkt.dts);
        }
//        AMFTraceWarning(AMF_FACILITY, L"WritePacket() %s in_pts=%" LPRId64 L"  pts=%" LPRId64 L", dts=% " LPRId64,
//            ost->codec->codec_type == AVMEDIA_TYPE_VIDEO ? L"video" : L"audio",
//            pts, pkt.pts, pkt.dts);

        amf_int64 ptsFFmpeg = pkt.pts;
#if !FAKE_MUXING
        if (av_interleaved_write_frame(m_pOutputContext,&pkt)<0)
        {
            return AMF_FAIL;
        }
#endif
        if(ost->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            if(m_iViewFrameCount == 0)
            {
                m_ptsStatTime = currentTime;
            }
            m_iViewFrameCount++;            
            if((m_iViewFrameCount % 100) == 0)
            {
#if FAKE_MUXING
                AMFTraceWarning(AMF_FACILITY, L" FPS=%5.2f", (double)100 * AMF_SECOND / (currentTime - m_ptsStatTime));
#endif
                m_ptsStatTime = currentTime;
            }
        }
//        AMFTraceWarning(AMF_FACILITY, L"WritePacket() %s in_pts=%" LPRId64 L"pts=%" LPRId64 L"time=%5.2f",
//            ost->codec->codec_type == AVMEDIA_TYPE_VIDEO ? L"video" : L"audio",
//            pts, ptsFFmpeg, double(amf_high_precision_clock() - currentTime) / 10000. 
//            );
    }
    // check if all streams reached EOF
    if (!pData || m_bForceEof)
    {
        m_bEofList[iIndex] = true;
    }
    for(amf_size i=0; i < m_bEofList.size(); i++)
    {
        if (!m_bEofList[i])
        {
            return AMF_OK;
        }
    }
    Close(); // EOF detected - close the file
    return AMF_EOF;
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
