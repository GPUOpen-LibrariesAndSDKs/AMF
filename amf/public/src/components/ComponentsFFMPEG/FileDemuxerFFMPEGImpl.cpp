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

extern "C"
{
#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable:4244) // possible loss of data with conversion
#endif

    // This includes winsock2.h first so windows.h cannot be
    // included first otherwise there will be linker errors
    #include "libavformat/internal.h"

#if defined(_MSC_VER)
    #pragma warning(pop)
#endif
}

#include "FileDemuxerFFMPEGImpl.h"
#include "UtilsFFMPEG.h"
#include <float.h>

#include "public/include/components/VideoDecoderUVD.h"
#include "public/include/core/Context.h"
#include "public/include/core/Trace.h"
#include "public/common/TraceAdapter.h"
#include "public/common/AMFFactory.h"
#include "public/common/DataStream.h"

#define AMF_FACILITY L"AMFFileDemuxerFFMPEGImpl"

using namespace amf;



static const AMFEnumDescriptionEntry VIDEO_CODEC_IDS_ENUM[] =
{
    { AMF_STREAM_CODEC_ID_UNKNOWN, L"UNKNOWN" },
    { AMF_STREAM_CODEC_ID_MPEG2, AMFVideoDecoderUVD_MPEG2 },
    { AMF_STREAM_CODEC_ID_MPEG4, AMFVideoDecoderUVD_MPEG4 },
    { AMF_STREAM_CODEC_ID_WMV3, AMFVideoDecoderUVD_WMV3 },
    { AMF_STREAM_CODEC_ID_VC1, AMFVideoDecoderUVD_VC1 },
    { AMF_STREAM_CODEC_ID_H264_AVC, AMFVideoDecoderUVD_H264_AVC },
    { AMF_STREAM_CODEC_ID_H264_MVC, AMFVideoDecoderUVD_H264_MVC },
    { AMF_STREAM_CODEC_ID_MJPEG, AMFVideoDecoderUVD_MJPEG },
    { AMF_STREAM_CODEC_ID_H265_HEVC, AMFVideoDecoderHW_H265_HEVC },
    { AMF_STREAM_CODEC_ID_H265_MAIN10, AMFVideoDecoderHW_H265_MAIN10},
    { AMF_STREAM_CODEC_ID_VP9, AMFVideoDecoderHW_VP9},
    { AMF_STREAM_CODEC_ID_VP9_10BIT, AMFVideoDecoderHW_VP9_10BIT},
	{ AMF_STREAM_CODEC_ID_AV1, AMFVideoDecoderHW_AV1},
    { AMF_STREAM_CODEC_ID_AV1_12BIT, AMFVideoDecoderHW_AV1_12BIT},
    { 0, 0 }
};

static const AMFEnumDescriptionEntry AMF_OUTPUT_FORMATS_ENUM[] =
{
    { AMF_SURFACE_UNKNOWN,  L"DEFAULT" },
    { AMF_SURFACE_BGRA,     L"BGRA" },
    { AMF_SURFACE_RGBA,     L"RGBA" },
    { AMF_SURFACE_ARGB,     L"ARGB" },
    { AMF_SURFACE_NV12,     L"NV12" },
    { AMF_SURFACE_YUV420P,  L"YUV420P" },
    { AMF_SURFACE_YV12,     L"YV12" },
    { AMF_SURFACE_P010,     L"P010" },
    { AMF_SURFACE_P012,     L"P012" },
    { AMF_SURFACE_P016,     L"P016" },
    { AMF_SURFACE_Y210,     L"Y210" },
    { AMF_SURFACE_Y410,     L"Y410" },
    { AMF_SURFACE_Y416,     L"Y416" },
    { AMF_SURFACE_RGBA_F16, L"RGBA_F16" },
    { AMF_SURFACE_UNKNOWN,  0 }  // This is end of description mark
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

const AMFEnumDescriptionEntry AMF_STREAM_TYPE_ENUM_DESCRIPTION[] =
{
    {AMF_STREAM_UNKNOWN,   L"Unknown"},
    {AMF_STREAM_VIDEO,     L"Video"},
    {AMF_STREAM_AUDIO,     L"Audio"},
    {AMF_STREAM_DATA,      L"Data"},
    { AMF_STREAM_UNKNOWN   , 0 }  // This is end of description mark
};


void  ClearPacket(AVPacket* pPacket)
{
    av_packet_unref(pPacket);
    delete pPacket;
}



//
//
// AMFOutputDemuxerImpl
//
//

//-------------------------------------------------------------------------------------------------
AMFFileDemuxerFFMPEGImpl::AMFOutputDemuxerImpl::AMFOutputDemuxerImpl(AMFFileDemuxerFFMPEGImpl* pHost, amf_int32 index)
    : m_pHost(pHost),
      m_iIndexFFmpeg(index),
      m_bEnabled(false),
      m_iPacketCount(0)
{
}
//-------------------------------------------------------------------------------------------------
AMFFileDemuxerFFMPEGImpl::AMFOutputDemuxerImpl::~AMFOutputDemuxerImpl()
{
    AMFTraceInfo(AMF_FACILITY,L"Stream# %d, packets read %d", m_iIndexFFmpeg, (int)m_iPacketCount);
    ClearPacketCache();
}
//-------------------------------------------------------------------------------------------------
// NOTE: this call will return one compressed frame for each QueryOutput call
AMF_RESULT AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::AMFOutputDemuxerImpl::QueryOutput(AMFData** ppData)
{
    AMF_RETURN_IF_FALSE(m_bEnabled, AMF_UNEXPECTED, L"QueryOutput() - Stream is disabled");
    AMF_RETURN_IF_FALSE(m_pHost != NULL, AMF_UNEXPECTED, L"QueryOutput() - Host not Initialized");
    AMF_RETURN_IF_FALSE(m_pHost->m_pInputContext != NULL, AMF_NOT_INITIALIZED, L"QueryOutput() - Input Context not Initialized");
    AMF_RETURN_IF_FALSE(m_pHost->m_bStreamingMode, AMF_NOT_SUPPORTED, L"QueryOutput() - Individual stream output has been disabled");


    AMFLock lock(&m_pHost->m_sync);

    // look into our internal cache and see if we have packets to read
    // if we do, return the first packet from the list...
    AMF_RESULT  err    = AMF_OK;
    AVPacket*   packet = nullptr;
    if (!m_packetsCache.empty())
    {
        // don't forget to remove the packet from the list now that we "read" it
        packet = m_packetsCache.front();
        m_packetsCache.pop_front();
    }
    else
    {
        // if we don't have any packets cached, look to find the next one
        err = m_pHost->FindNextPacket(m_iIndexFFmpeg, &packet, true);
        if (err != AMF_OK)
        {
            if(err == AMF_EOF)
            {
                if(m_pHost->IsCached())
                {
                    return AMF_OK;
                }
            }
            return err;
        }
    }


    //
    // allocate a buffer from a packet
    AMFBufferPtr buf;
    err = m_pHost->BufferFromPacket(packet, &buf);
    if (err != AMF_OK)
    {
        ClearPacket(packet);
        return err;
    }


    *ppData = buf;
    (*ppData)->Acquire();

    ClearPacket(packet);

    m_iPacketCount++;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMFFileDemuxerFFMPEGImpl::AMFOutputDemuxerImpl::CachePacket(AVPacket* pPacket)
{
    // check the packet exists - no point caching a null pointer
    AMF_RETURN_IF_FALSE(pPacket != NULL, AMF_INVALID_ARG, L"CachePacket() - packet not passed in");
    // check the packet has the same index as the stream
    AMF_RETURN_IF_FALSE(pPacket->stream_index == m_iIndexFFmpeg, AMF_INVALID_ARG, L"CachePacket() - invalid packet for stream passed in");

    if(!m_bEnabled)
    {
        ClearPacket(pPacket);
        return AMF_FAIL;
    }
       // add the packet to the cache...
    m_packetsCache.push_back(pPacket);
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
void  AMFFileDemuxerFFMPEGImpl::AMFOutputDemuxerImpl::ClearPacketCache()
{
    for (amf_list<AVPacket*>::iterator it = m_packetsCache.begin(); it != m_packetsCache.end(); ++it)
    {
        ClearPacket(*it);
    }
    m_packetsCache.clear();
}
bool        AMFFileDemuxerFFMPEGImpl::AMFOutputDemuxerImpl::IsCached()
{
    return m_packetsCache.size() != 0;
}
//-------------------------------------------------------------------------------------------------
void        AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::AMFOutputDemuxerImpl::OnPropertyChanged(const wchar_t* pName)
{
    const amf_wstring  name(pName);
    if (name == AMF_STREAM_ENABLED)
    {
        AMFLock lock(&m_pHost->m_sync);
        AMFPropertyStorage::GetProperty(AMF_STREAM_ENABLED, &m_bEnabled);
    }
}
//-------------------------------------------------------------------------------------------------



//
//
// AMFVideoOutputDemuxerImpl
//
//

AMFFileDemuxerFFMPEGImpl::AMFVideoOutputDemuxerImpl::AMFVideoOutputDemuxerImpl(AMFFileDemuxerFFMPEGImpl* pHost, amf_int32 index)
    : AMFFileDemuxerFFMPEGImpl::AMFOutputDemuxerImpl(pHost, index)
{
    const AVStream* ist = pHost->m_pInputContext->streams[index];


    // determine codec ID
    amf_int  codecID = AV_CODEC_ID_NONE;
    switch (ist->codecpar->codec_id)
    {
        case AV_CODEC_ID_MPEG2VIDEO :
        case AV_CODEC_ID_MPEG4 :
        case AV_CODEC_ID_WMV3 :
        case AV_CODEC_ID_VC1 :
        case AV_CODEC_ID_H264 :
        case AV_CODEC_ID_MJPEG :
        case AV_CODEC_ID_HEVC :
        case AV_CODEC_ID_VP9 :
		case AV_CODEC_ID_AV1:
            codecID = ist->codecpar->codec_id;
            break;

        default:
            codecID = AV_CODEC_ID_NONE;
    }


    // calculate pixel aspect ratio
    double  pixelAspectRatio = 1.0;
    if (ist->sample_aspect_ratio.den != 0 && ist->sample_aspect_ratio.num != 0)
    {
        pixelAspectRatio = (amf_double)ist->sample_aspect_ratio.num / (amf_double)ist->sample_aspect_ratio.den;
    }
    else if (ist->codecpar->sample_aspect_ratio.den != 0 && ist->codecpar->sample_aspect_ratio.num != 0)
    {
        pixelAspectRatio = (amf_double)ist->codecpar->sample_aspect_ratio.num / (amf_double)ist->codecpar->sample_aspect_ratio.den;
    }

    // allocate a buffer to store the extra data
    AMFBufferPtr spBuffer;
    if(ist->codecpar->extradata_size > 0)
    {
        AMF_RESULT err = m_pHost->m_pContext->AllocBuffer(AMF_MEMORY_HOST, ist->codecpar->extradata_size, &spBuffer);
        if ((err == AMF_OK) && spBuffer->GetNative())
        {
            memcpy(spBuffer->GetNative(), ist->codecpar->extradata, ist->codecpar->extradata_size);
        }
    }

    //handle h264, NV12
    AVPixelFormat pix_fmt = (ist->codecpar->format == AV_PIX_FMT_NONE) ? AV_PIX_FMT_NV12 : (AVPixelFormat)ist->codecpar->format;

    // figure out the surface format conversion
    AMF_SURFACE_FORMAT  surfaceFormat = GetAMFSurfaceFormat(pix_fmt);

    if(surfaceFormat == AMF_SURFACE_P010 && codecID == AV_CODEC_ID_HEVC)
    {
         codecID = AMF_CODEC_H265MAIN10;
    }
    if(surfaceFormat == AMF_SURFACE_P010 && codecID == AV_CODEC_ID_VP9)
    {
         codecID = AMF_CODEC_VP9_10BIT;
    }
    if (surfaceFormat == AMF_SURFACE_P012 && codecID == AV_CODEC_ID_AV1)
    {
        codecID = AMF_CODEC_AV1_12BIT;
    }

    AMFSize frame = AMFConstructSize(ist->codecpar->width, ist->codecpar->height);

    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoEnum(AMF_STREAM_TYPE, L"Stream Type", AMF_STREAM_VIDEO, AMF_STREAM_TYPE_ENUM_DESCRIPTION, false),
        AMFPropertyInfoBool(AMF_STREAM_ENABLED, L"Enabled", false, true),
        AMFPropertyInfoEnum(AMF_STREAM_CODEC_ID, L"Codec ID", (amf_int64)GetAMFVideoFormat((AVCodecID)codecID), VIDEO_CODEC_IDS_ENUM, false),
        AMFPropertyInfoInt64(AMF_STREAM_BIT_RATE, L"Bit Rate", 0, 0, INT_MAX, false),
        AMFPropertyInfoInterface(AMF_STREAM_EXTRA_DATA, L"Extra Data", NULL, false),
        AMFPropertyInfoRate(AMF_STREAM_VIDEO_FRAME_RATE, L"Frame Rate", ist->r_frame_rate.num, ist->r_frame_rate.den, false),
        AMFPropertyInfoSize(AMF_STREAM_VIDEO_FRAME_SIZE, L"Frame Size", frame, AMFConstructSize(0, 0), AMFConstructSize(INT_MAX, INT_MAX), false),
        AMFPropertyInfoEnum(AMF_STREAM_VIDEO_FORMAT, L"Surface Format", surfaceFormat, AMF_OUTPUT_FORMATS_ENUM, false),
        AMFPropertyInfoDouble(FFMPEG_DEMUXER_VIDEO_PIXEL_ASPECT_RATIO, L"Pixel Aspect Ratio", pixelAspectRatio, 0, DBL_MAX, false),
        AMFPropertyInfoInt64(FFMPEG_DEMUXER_VIDEO_CODEC,    L"FFMPEG codec", ist->codecpar->codec_id, AV_CODEC_ID_NONE, AV_CODEC_ID_WRAPPED_AVFRAME, false),
    AMFPrimitivePropertyInfoMapEnd

    SetProperty(AMF_STREAM_CODEC_ID, GetAMFVideoFormat(AVCodecID(codecID)));
    SetProperty(AMF_STREAM_BIT_RATE, ist->codecpar->bit_rate);
    SetProperty(AMF_STREAM_VIDEO_FRAME_RATE, AMFConstructRate(ist->r_frame_rate.num, ist->r_frame_rate.den));
    SetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, frame);
    SetProperty(AMF_STREAM_VIDEO_FORMAT, surfaceFormat);
    SetProperty(FFMPEG_DEMUXER_VIDEO_PIXEL_ASPECT_RATIO, pixelAspectRatio);
    SetProperty(FFMPEG_DEMUXER_VIDEO_CODEC, ist->codecpar->codec_id);
    AMFPropertyStorage::SetProperty(AMF_STREAM_EXTRA_DATA, spBuffer);
}



//
//
// AMFAudioOutputDemuxerImpl
//
//

//-------------------------------------------------------------------------------------------------
AMFFileDemuxerFFMPEGImpl::AMFAudioOutputDemuxerImpl::AMFAudioOutputDemuxerImpl(AMFFileDemuxerFFMPEGImpl* pHost, amf_int32 index)
    : AMFFileDemuxerFFMPEGImpl::AMFOutputDemuxerImpl(pHost, index)
{
    const AVStream* ist = pHost->m_pInputContext->streams[index];

//    AMF_ASSERT_OK(ist->codecpar->codec_type == AVMEDIA_TYPE_AUDIO);


    // allocate a buffer to store the extra data
    AMFBufferPtr spBuffer;
    if(ist->codecpar->extradata_size != 0)
    {
    AMF_RESULT err = m_pHost->m_pContext->AllocBuffer(AMF_MEMORY_HOST, ist->codecpar->extradata_size, &spBuffer);
    if ((err == AMF_OK) && spBuffer->GetNative())
    {
        memcpy(spBuffer->GetNative(), ist->codecpar->extradata, ist->codecpar->extradata_size);
    }
    }
    // figure out the surface format conversion
    AMF_AUDIO_FORMAT  audioFormat = GetAMFAudioFormat((AVSampleFormat)ist->codecpar->format);

    if(ist->codecpar->ch_layout.nb_channels == 0) // ffmpeg return 0 for some AAC files
    {
        ist->codecpar->ch_layout.nb_channels = 2;
    }


    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoEnum(AMF_STREAM_TYPE, L"Stream Type", AMF_STREAM_AUDIO, AMF_STREAM_TYPE_ENUM_DESCRIPTION, false),
        AMFPropertyInfoBool(AMF_STREAM_ENABLED, L"Enabled", false, true),
        AMFPropertyInfoInt64(AMF_STREAM_CODEC_ID, L"Codec ID", ist->codecpar->codec_id, AV_CODEC_ID_NONE, INT_MAX, false),
        AMFPropertyInfoInt64(AMF_STREAM_BIT_RATE, L"Bit Rate", ist->codecpar->bit_rate, 0, INT_MAX, false),
        AMFPropertyInfoInterface(AMF_STREAM_EXTRA_DATA, L"Extra Data", NULL, false),
        AMFPropertyInfoInt64(AMF_STREAM_AUDIO_SAMPLE_RATE, L"Sample Rate", ist->codecpar->sample_rate, 0, INT_MAX, false),
        AMFPropertyInfoInt64(AMF_STREAM_AUDIO_CHANNELS, L"Channels", ist->codecpar->ch_layout.nb_channels, 0, 100, false),
        AMFPropertyInfoEnum(AMF_STREAM_AUDIO_FORMAT, L"Sample Format", audioFormat, AMF_SAMPLE_FORMAT_ENUM_DESCRIPTION, false),
        AMFPropertyInfoInt64(AMF_STREAM_AUDIO_CHANNEL_LAYOUT, L"Channel Layout", ist->codecpar->ch_layout.u.mask, 0, INT_MAX, false),
        AMFPropertyInfoInt64(AMF_STREAM_AUDIO_BLOCK_ALIGN, L"Block Align", ist->codecpar->block_align, 0, INT_MAX, false),
        AMFPropertyInfoInt64(AMF_STREAM_AUDIO_FRAME_SIZE, L"Frame Size", ist->codecpar->frame_size, 0, INT_MAX, false),
    AMFPrimitivePropertyInfoMapEnd

    SetProperty(AMF_STREAM_CODEC_ID, ist->codecpar->codec_id);
    SetProperty(AMF_STREAM_BIT_RATE, ist->codecpar->bit_rate);
    SetProperty(AMF_STREAM_AUDIO_SAMPLE_RATE, ist->codecpar->sample_rate);
    SetProperty(AMF_STREAM_AUDIO_CHANNELS, ist->codecpar->ch_layout.nb_channels);
    SetProperty(AMF_STREAM_AUDIO_FORMAT, audioFormat);
    SetProperty(AMF_STREAM_AUDIO_CHANNEL_LAYOUT, ist->codecpar->ch_layout.u.mask);
    SetProperty(AMF_STREAM_AUDIO_BLOCK_ALIGN, ist->codecpar->block_align);
    SetProperty(AMF_STREAM_AUDIO_FRAME_SIZE, ist->codecpar->frame_size);

    AMFPropertyStorage::SetProperty(AMF_STREAM_EXTRA_DATA, spBuffer);
}



//
//
// AMFFileDemuxerFFMPEGImpl
//
//

//-------------------------------------------------------------------------------------------------
AMFFileDemuxerFFMPEGImpl::AMFFileDemuxerFFMPEGImpl(AMFContext* pContext)
  : m_pContext(pContext),
    m_pInputContext(NULL),
    m_ptsDuration(0),
    m_ptsPosition(0),
    m_ptsSeekPos(-1),
    m_ptsInitialMinPosition(0),
    m_iPacketCount(0),
    m_bForceEof(false),
    m_bStreamingMode(true),
    m_iVideoStreamIndexFFmpeg(-1),
    m_iAudioStreamIndexFFmpeg(-1),
    m_bTerminated(true),
    m_bStreaming(false)
//    m_bSyncAV(false)
{
    g_AMFFactory.Init();

    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoPath(FFMPEG_DEMUXER_PATH, L"File Path", L"", false),
        AMFPropertyInfoPath(FFMPEG_DEMUXER_URL, L"Stream URL", L"", false),

        AMFPropertyInfoInt64(FFMPEG_DEMUXER_START_FRAME, L"StartFrame", 0, 0, LLONG_MAX, true),
        AMFPropertyInfoInt64(FFMPEG_DEMUXER_FRAME_COUNT, L"FramesNumber", 0, 0, LLONG_MAX, true),
        AMFPropertyInfoInt64(FFMPEG_DEMUXER_DURATION, L"Duration", 0, 0, LLONG_MAX, false),
//        AMFPropertyInfoBool(FFMPEG_DEMUXER_SYNC_AV, L"Sync Audio and Video by PTS", false, false),
        AMFPropertyInfoBool(FFMPEG_DEMUXER_CHECK_MVC, L"Check MVC", true, false),
        AMFPropertyInfoBool(FFMPEG_DEMUXER_INDIVIDUAL_STREAM_MODE, L"Stream mode", true, false),
        AMFPropertyInfoBool(FFMPEG_DEMUXER_LISTEN, L"Listen", false, false)

    AMFPrimitivePropertyInfoMapEnd

    InitFFMPEG();
}
//-------------------------------------------------------------------------------------------------
AMFFileDemuxerFFMPEGImpl::~AMFFileDemuxerFFMPEGImpl()
{
    Terminate();
    Close();
    g_AMFFactory.Terminate();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::Init(AMF_SURFACE_FORMAT /*format*/, amf_int32 /*width*/, amf_int32 /*height*/)
{
    AMFLock lock(&m_sync);

    if (m_bTerminated)
    {
        Close();
    }

    AMF_RESULT res = Open();
    if (res == AMF_OK)
    {
        amf_pts pos = GetMinPosition();
        Seek(pos, AMF_SEEK_PREV, -1);

        ReadRangeSettings();
    }

    return res;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::ReInit(amf_int32 width, amf_int32 height)
{
    Terminate();
    return Init(AMF_SURFACE_UNKNOWN, width, height);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::Terminate()
{
    AMFLock lock(&m_sync);

    m_ptsPosition = GetMinPosition();
    m_ptsSeekPos = -1;
    m_bTerminated = true;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::Drain()
{
    AMFLock lock(&m_sync);

    m_bForceEof = true;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::Flush()
{
    AMFLock lock(&m_sync);

    ClearCachedPackets();

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::QueryOutput(AMFData** ppData)
{
    // this mode is available if streaming mode (reading from individual
    // streams) is disabled - by default this is disabled...
    AMF_RETURN_IF_FALSE(!m_bStreamingMode, AMF_NOT_SUPPORTED, L"QueryOutput() - Bulk packet output has not been set");


    AMFLock lock(&m_sync);

    // when we check for MVC, we should fill the caches of the one/two/n streams
    // we created on Init.  This should be about 5 frames or so...
    // to not seek back and re-read again, we will drain the caches first,
    // in no particular order before we continue to read from the file...
    AVPacket* pPacket = nullptr;
    for (size_t idx = 0; idx < m_OutputStreams.size(); idx++)
    {
        if (!m_OutputStreams[idx]->m_packetsCache.empty())
        {
            // get the packet
            pPacket = m_OutputStreams[idx]->m_packetsCache.front();
            m_OutputStreams[idx]->m_packetsCache.pop_front();
            break;
        }
    }

    // if we haven't found a cached packet, read another one
    if (!pPacket)
    {
        AMF_RESULT err = FindNextPacket(-1, &pPacket, false);
        if (err != AMF_OK)
        {
            return err;
        }
    }


    //
    // allocate a buffer from a packet
    AMFBufferPtr buf;
    AMF_RESULT err = BufferFromPacket(pPacket, &buf);
    if (err != AMF_OK)
    {
        ClearPacket(pPacket);
        return err;
    }


    *ppData = buf;
    (*ppData)->Acquire();

    ClearPacket(pPacket);

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::GetOutput(amf_int32 index, AMFOutput** ppOutput)
{
    AMF_RETURN_IF_FALSE(index >= 0 && index < (amf_int32)m_OutputStreams.size(), AMF_INVALID_ARG, L"Invalid index");
    AMF_RETURN_IF_FALSE(ppOutput != NULL, AMF_INVALID_ARG, L"ppOutput = NULL");

    *ppOutput = m_OutputStreams[index];
    (*ppOutput)->Acquire();

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFFileDemuxerFFMPEGImpl::Seek(amf_pts ptsPos, AMF_SEEK_TYPE eType, amf_int32 whichStream)
{
    AMFLock lock(&m_sync);

    AMF_RETURN_IF_FALSE(!m_bTerminated, AMF_WRONG_STATE, L"Seek() - Primitive Terminated");
    AMF_RETURN_IF_FALSE(m_pInputContext != NULL, AMF_NOT_INITIALIZED, L"Seek() - Input ontext not Initialized");

    if(m_ptsPosition == ptsPos)
    {
        return AMF_OK;
    }

    int flags = 0;
    switch (eType)
    {
    case AMF_SEEK_PREV: // nearest packet before pts
        //flags=AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY;
        flags = AVSEEK_FLAG_BACKWARD;
        break;
    case AMF_SEEK_NEXT: // nearest packet after pts
        //flags=AVSEEK_FLAG_ANY;
        flags = AVSEEK_FLAG_BACKWARD;
        break;
    case AMF_SEEK_PREV_KEYFRAME: // nearest keyframe packet before pts
        flags = AVSEEK_FLAG_BACKWARD;
        break;
    case AMF_SEEK_NEXT_KEYFRAME: // nearest keyframe packet after pts
        flags = 0;
        break;
    }

    // check if requested stream is out of range - if it is
    // use the default stream...
    int stream_index = whichStream;
    if (whichStream < 0 || (whichStream >= GetOutputCount()))
    {
        stream_index = av_find_default_stream_index(m_pInputContext);
    }

//    bool validDuration = (m_pInputContext->duration != AV_NOPTS_VALUE);
//    if(validDuration)
    {

        AVStream*  ist    = m_pInputContext->streams[stream_index];
        int64_t    offset = av_rescale_q(ptsPos, AMF_TIME_BASE_Q, ist->time_base);

        // AVSEEK_FLAG_BACKWARD means that we need packet before ptsPos
        int ret = av_seek_frame(m_pInputContext, stream_index, offset, flags);
        if (ret<0)
        {
            // sometimes failed av_seek_frame cause further av_read functions return errors too.
            AMFTraceError(AMF_FACILITY, L"Seek() - failed. Reinitialize ffmpeg context.");
            Close();
            Open();
        }
        else
        {
            ClearCachedPackets();
        }
    }
    m_iPacketCount = 0;
    m_ptsPosition = ptsPos;

    ReadRangeSettings();

    m_ptsSeekPos = ptsPos;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
amf_pts AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::GetDuration()
{
    AMFLock lock(&m_sync);
    return m_ptsDuration;
}
//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::SetMinPosition(amf_pts pts)
{
    AMFLock lock(&m_sync);

    if (m_iVideoStreamIndexFFmpeg != -1)
    {
        pts = CheckPtsRange(pts);

        // get the new start position
        amf_int64 newStartFrame   = (amf_int64) GetFrameFromPts(pts);
        amf_int64 oldEndFrame     = GetPropertyStartFrame() + GetPropertyFramesNumber();

        // we need to recalculate if the new min position is
        // beyoond the last frame of the previous range
        amf_int64 newEndFrame     = (newStartFrame > oldEndFrame) ? (amf_int64) GetFrameFromPts(GetDuration()) : oldEndFrame;
        amf_int64 newFramesNumber = (newEndFrame - newStartFrame);

        SetProperty(FFMPEG_DEMUXER_START_FRAME, newStartFrame);
        SetProperty(FFMPEG_DEMUXER_FRAME_COUNT, newFramesNumber);
    }
}
//-------------------------------------------------------------------------------------------------
amf_pts AMF_STD_CALL AMFFileDemuxerFFMPEGImpl::GetMinPosition()
{
    AMFLock lock(&m_sync);

    if (m_iVideoStreamIndexFFmpeg != -1)
    {
        amf_uint64 startFrame = GetPropertyStartFrame();
        return GetPtsFromFrame(startFrame);
    }

    return 0;
}
//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL AMFFileDemuxerFFMPEGImpl::SetMaxPosition(amf_pts pts)
{
    AMFLock lock(&m_sync);

    if (m_iVideoStreamIndexFFmpeg != -1)
    {
        pts = CheckPtsRange(pts);

        amf_int64 iNewFramesNumber = (amf_int64)(GetFrameFromPts(pts) - GetPropertyStartFrame());
        if (iNewFramesNumber <= 0)
        {
            iNewFramesNumber = 1;
        }

        SetProperty(FFMPEG_DEMUXER_FRAME_COUNT, iNewFramesNumber);
    }
}
//-------------------------------------------------------------------------------------------------
amf_pts AMF_STD_CALL AMFFileDemuxerFFMPEGImpl::GetMaxPosition()
{
    AMFLock lock(&m_sync);

    if (GetPropertyFramesNumber() > 0)
    {
        return AMF_MIN(GetPtsFromFrame(GetPropertyStartFrame() + GetPropertyFramesNumber()), GetDuration());
    }

    return GetDuration();
}
//-------------------------------------------------------------------------------------------------
static double GetPtsPerFrame(const AVStream *ist)
{
    double frameFate = (double(ist->r_frame_rate.num) / ist->r_frame_rate.den);
    double ptsPerFrame = (AMF_SECOND / frameFate);
    return ptsPerFrame;
}
//-------------------------------------------------------------------------------------------------
amf_uint64 AMF_STD_CALL AMFFileDemuxerFFMPEGImpl::GetFrameFromPts(amf_pts pts)
{
    AMFLock lock(&m_sync);
    if (m_pInputContext)
    {
        if (m_iVideoStreamIndexFFmpeg != -1)
        {
            const AVStream *ist = m_pInputContext->streams[m_iVideoStreamIndexFFmpeg];
            amf_uint64 uFrame = static_cast<amf_uint64>((pts + 0.5) / GetPtsPerFrame(ist));
            return uFrame;
        }
    }
    return 0;
}
//-------------------------------------------------------------------------------------------------
amf_pts AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::GetPtsFromFrame(amf_uint64 iFrame)
{
    AMFLock lock(&m_sync);
    if (m_pInputContext)
    {
        if (m_iVideoStreamIndexFFmpeg != -1)
        {
            const AVStream *ist = m_pInputContext->streams[m_iVideoStreamIndexFFmpeg];
            amf_pts framePts = static_cast<amf_pts>((iFrame * GetPtsPerFrame(ist)) + 0.5);
            return framePts;
        }
    }

    return 0;
}
//-------------------------------------------------------------------------------------------------
bool AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::SupportFramesAccess()
{
    return (m_iVideoStreamIndexFFmpeg != -1);
}
//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::OnPropertyChanged(const wchar_t* pName)
{
    AMFLock lock(&m_sync);

    const amf_wstring  name(pName);
    if (name == FFMPEG_DEMUXER_PATH || name == FFMPEG_DEMUXER_URL)
    {
//        m_OutputStreams.clear();
//        ReInit(0, 0);
        return;
    }

    if (name == FFMPEG_DEMUXER_INDIVIDUAL_STREAM_MODE)
    {
        GetProperty(FFMPEG_DEMUXER_INDIVIDUAL_STREAM_MODE, &m_bStreamingMode);
        return;
    }
}


//
//
// protected
//
//

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::Open()
{
    AMFLock lock(&m_sync);

    // if the URL is the same, ignore the call
    amf_wstring Url;
    amf_wstring Path;

    GetPropertyWString(FFMPEG_DEMUXER_URL, &Url);
    GetPropertyWString(FFMPEG_DEMUXER_PATH, &Path);

    amf_string convertedfilename;
    bool bListen = false;
    const AVInputFormat* file_iformat = NULL;
    bool bStreaming = false;
    if(Url.length() >0)
    {
        if (m_Url == Url)
        {
            return AMF_OK;
        }
        convertedfilename = amf_from_unicode_to_utf8(Url);;
        amf_string::size_type pos = convertedfilename.find(':');
        if(pos != amf_string::npos)
        {
            amf_string protocol = convertedfilename.substr(0, pos);
            protocol = amf_string_to_lower(protocol);
            if (protocol == "rtmp")
            {
//                protocol = "flv";
//                bListen = true;
            }
            else if (protocol == "tcp")
            {
                protocol = "mpegts";
                file_iformat = av_find_input_format(protocol.c_str());
            }
        }
        GetProperty(FFMPEG_DEMUXER_LISTEN, &bListen);
        bStreaming = true;
    }
    else
    {
        if (m_Url == Path)
        {
            return AMF_OK;
        }
//        convertedfilename = amf_string("vlfile:") + amf_from_unicode_to_utf8(Path);
        convertedfilename = amf_string("file:") + amf_from_unicode_to_utf8(Path);
        Url = Path;
    }

    Close();

    m_bStreaming = bStreaming;

    m_ptsDuration = 0;
    m_ptsPosition = 0;
    m_bTerminated = false;

    AVDictionary *options = NULL;

    if(bListen)
    {
        av_dict_set(&options, "listen", "1", 0);
        av_dict_set(&options, "timeout", "30", 0);
    }

    // try open the file, if it fails, return error code
    AVInputFormat* fmt               = NULL;
    amf_bool bImageFormat = false;
    AMF_RESULT res = OpenFile(convertedfilename, fmt, options, bImageFormat);
    AMF_RETURN_IF_FALSE(res==AMF_OK && m_pInputContext!=NULL, AMF_INVALID_ARG, L"Open() failed to open file %s", Url.c_str());

    if(file_iformat!= NULL)
    {
        m_pInputContext->iformat = file_iformat;
    }

    if (bImageFormat)
    {
        res = OpenAsImageSequence(convertedfilename, fmt, options);
    }

    int videoIndex = -1;
    amf_vector<AMFOutputDemuxerImplPtr>  outputStreams;
    for (amf_int32 i = 0; i < static_cast<amf_int32>(m_pInputContext->nb_streams); i++)
    {
        const AVStream* ist = m_pInputContext->streams[i];
        AMFOutputDemuxerImplPtr newOutput;
        if (ist->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && m_iVideoStreamIndexFFmpeg == i)
        {
            if (ist->codecpar->format == AV_PIX_FMT_BGR24)
                continue;

            newOutput = new AMFVideoOutputDemuxerImpl(this, i);
            videoIndex = i;
        }

        if (ist->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && m_iAudioStreamIndexFFmpeg == i)
        {
            newOutput = new AMFAudioOutputDemuxerImpl(this, i);
        }
        if(newOutput != NULL)
        {
            for(amf_vector<AMFOutputDemuxerImplPtr>::iterator it = m_OutputStreams.begin(); it != m_OutputStreams.end(); it++)
            {
                if((*it)->m_iIndexFFmpeg == i)
                {
                    newOutput->SetProperty(AMF_STREAM_ENABLED, (*it)->m_bEnabled);
                    break;
                }
            }
            outputStreams.push_back(newOutput);
        }
    }
    m_OutputStreams = outputStreams;



    if (m_iVideoStreamIndexFFmpeg >= 0 && m_pInputContext->streams[m_iVideoStreamIndexFFmpeg]->codecpar->codec_id == AV_CODEC_ID_H264)
    {
        bool checkMVC = true;
        GetProperty(FFMPEG_DEMUXER_CHECK_MVC, &checkMVC);

        bool bEnabled = false;
        m_OutputStreams[videoIndex]->GetProperty(AMF_STREAM_ENABLED, &bEnabled);
        m_OutputStreams[videoIndex]->SetProperty(AMF_STREAM_ENABLED, true);
        if (checkMVC && CheckH264MVC())
        {
            m_OutputStreams[videoIndex]->SetProperty(AMF_STREAM_CODEC_ID, GetAMFVideoFormat(AVCodecID(AV_CODEC_H264MVC)));
        }
        m_OutputStreams[videoIndex]->SetProperty(AMF_STREAM_ENABLED, bEnabled);
    }


    if (m_ptsDuration == 0)
    {
        m_ptsDuration = GetPtsFromFrame(GetPropertyStartFrame() + GetPropertyFramesNumber());
    }

    // decide on main streams
    m_Url = Url;

    amf_pts ptsMaxPosition = GetMaxPosition();
    if (ptsMaxPosition == 0)
    {
        amf_pts ptsDuration = GetDuration();
        if (ptsDuration != 0)
        {
            SetMaxPosition(ptsDuration);
        }
    }

//    GetProperty(FFMPEG_DEMUXER_SYNC_AV, &m_bSyncAV);
    SetProperty(FFMPEG_DEMUXER_DURATION, m_ptsDuration);

    // trace info about file and number of streams
    AMFTrace(AMF_TRACE_INFO, AMF_FACILITY, L"Open(%s) succeeded; streams=%d", Url.c_str(), m_pInputContext->nb_streams);

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::Close()
{
    AMFLock lock(&m_sync);

    if (m_pInputContext != NULL)
    {
        avformat_close_input(&m_pInputContext);
        m_pInputContext = NULL;
    }

    ClearCachedPackets();

    m_iVideoStreamIndexFFmpeg = -1;
    m_iAudioStreamIndexFFmpeg = -1;
    m_bForceEof = false;
    m_iPacketCount = 0;
    m_ptsPosition = GetMinPosition();
    m_ptsDuration = 0;
    m_bTerminated = false;
    m_bStreaming = false;

    m_Url.clear();
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::ReadPacket(AVPacket **packet)
{
    *packet = NULL;

    AVPacket  pkt ={};
    pkt.dts = AV_NOPTS_VALUE;
    pkt.pts = AV_NOPTS_VALUE;
//    amf_pts currTime = amf_high_precision_clock();
    if (m_bForceEof || av_read_frame(m_pInputContext, &pkt) < 0)
    {
//        AMFTraceInfo(AMF_FACILITY, L"ReadPacket() - EOF, END");
        return AMF_EOF;
    }
//    amf_pts readDuration = amf_high_precision_clock() - currTime;

    AVStream *ist = m_pInputContext->streams[pkt.stream_index];
    FFStream* fst = ffstream(ist);
    int64_t wrap = 1LL << ist->pts_wrap_bits;

//    AMFTraceWarning(AMF_FACILITY, L"ReadPacket() %s pts=%" LPRId64 L", dts=% " LPRId64 L" first_dts=%" LPRId64,
//        pkt.stream_index == m_iVideoStreamIndexFFmpeg ? L"video" : L"audio",
//        pkt.pts, pkt.dts, fst->first_dts);

    if (pkt.stream_index == m_iVideoStreamIndexFFmpeg)
    {
//        AMFTraceWarning(AMF_FACILITY, L"ReadPacket() video pts=%" LPRId64 L", dts=% " LPRId64 L" first_dts=%" LPRId64 , pkt.pts, pkt.dts, ist->first_dts);
        if (pkt.dts == AV_NOPTS_VALUE)
        {
            pkt.dts = av_rescale(m_iPacketCount, ist->time_base.den, (int64_t)ist->time_base.num);
            pkt.dts = av_rescale(pkt.dts, ist->r_frame_rate.den, (int64_t)ist->r_frame_rate.num);
        }
        else
        {
            if (pkt.pts != AV_NOPTS_VALUE && pkt.dts != AV_NOPTS_VALUE && pkt.dts<0)
            {
                pkt.dts += wrap;
            }
            if (fst->first_dts != AV_NOPTS_VALUE)
            {
                pkt.dts -= fst->first_dts;
            }
        }
        m_iPacketCount++;
    }
    else
    {
//        AMFTraceWarning(AMF_FACILITY, L"ReadPacket() audio pts=%" LPRId64 L", dts=% " LPRId64 L" first_dts=%" LPRId64 , pkt.pts, pkt.dts, ist->first_dts);
        if (pkt.pts != AV_NOPTS_VALUE && pkt.dts != AV_NOPTS_VALUE && pkt.dts<0)
        {
            pkt.dts += wrap;
        }

        if (fst->first_dts != AV_NOPTS_VALUE && pkt.dts != AV_NOPTS_VALUE)
        {
            pkt.dts -= fst->first_dts;
        }
    }


    //
    // PTS to standard time
    const amf_int64  pts = av_rescale_q(pkt.dts, ist->time_base, AMF_TIME_BASE_Q);
    if (m_iVideoStreamIndexFFmpeg == -1 || pkt.stream_index == m_iVideoStreamIndexFFmpeg)
    {
        m_ptsPosition = pts;
    }
    if (OutOfRange())
    {
        return AMF_EOF;
    }


    *packet = new AVPacket;
    memcpy(*packet, &pkt, sizeof(pkt));

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::FindNextPacket(amf_int32 streamIndex, AVPacket **packet, bool saveSkipped)
{
    // clear the return pointer in case we
    // return with an error code...
    *packet = nullptr;

    AMF_RESULT err = AMF_OK;
    while (true)
    {
        // if we're forced to EOF, exit loop
        if (m_bForceEof)
        {
            return AMF_EOF;
        }

        // read the next packet and assign it
        // to the appropriate cache/queue
        AVPacket* pTempPacket = nullptr;
        err = ReadPacket(&pTempPacket);
        if (err == AMF_EOF)
        {
            return AMF_EOF;
        }
        if (err != AMF_OK)
        {
            return err;
        }

        // we got a valid packet, assign it to
        // the appropriate cache/queue
        // NOTE: for now we skip all streams
        //       except 1 video and 1 audio
        const amf_int32  packetStreamIndex = pTempPacket->stream_index;
        if ( ((m_iVideoStreamIndexFFmpeg != -1) && (packetStreamIndex == m_iVideoStreamIndexFFmpeg))
            ||
             ((m_iAudioStreamIndexFFmpeg != -1) && (packetStreamIndex == m_iAudioStreamIndexFFmpeg))
            )
        {
            // nothing to do - code to handle correct packets
            // right after this "if"
        }
        else
        {
            ClearPacket(pTempPacket);
//          pTempPacket = nullptr;

            // if we're requesting packets from streams we don't
            // handle at this point, return EOF...
            if (packetStreamIndex == streamIndex)
            {
                return AMF_EOF;
            }
            continue;
        }

        // if it's not the packet for the stream we're
        // looking for, cache it in the other stream
        // if the packet we got is from the same index
        // as what we were looking for, it's time to exit
        if ((packetStreamIndex != streamIndex && streamIndex != -1) && saveSkipped)
        {
            bool bFound = false;
            for(size_t idx = 0; idx < m_OutputStreams.size(); idx++)
            {
                if(m_OutputStreams[idx]->m_iIndexFFmpeg == packetStreamIndex)
                {
                    m_OutputStreams[idx]->CachePacket(pTempPacket);
                    bFound = true;
                    break;
                }
            }
            if(!bFound)
            {
                ClearPacket(pTempPacket);
            }
            if (m_bStreaming)
            {
                *packet = NULL;
                return AMF_REPEAT;
            }
        }
        else
        {
            *packet = pTempPacket;
            return AMF_OK;
        }
    }
    return err;
}
//-------------------------------------------------------------------------------------------------
bool AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::OutOfRange()
{
    amf_uint64 val = 0;
    AMF_ASSERT_OK(GetProperty(FFMPEG_DEMUXER_FRAME_COUNT, &val));
    if (val == 0)
    {
        val = GetFrameFromPts(GetDuration());
    }

    const amf_pts maxPos     = GetMaxPosition();
    const bool    outOfRange = ((val != 0) && (m_ptsPosition >= maxPos) && (maxPos != 0));

    return outOfRange;
}
//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::ClearCachedPackets()
{
    for (amf_vector<AMFOutputDemuxerImplPtr>::iterator it = m_OutputStreams.begin(); it != m_OutputStreams.end(); ++it)
    {
        (*it)->ClearPacketCache();
    }
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::BufferFromPacket(const AVPacket* pPacket, AMFBuffer** ppBuffer)
{
    AMF_RETURN_IF_FALSE(pPacket != NULL, AMF_INVALID_ARG, L"BufferFromPacket() - packet not passed in");
    AMF_RETURN_IF_FALSE(ppBuffer != NULL, AMF_INVALID_ARG, L"BufferFromPacket() - buffer pointer not passed in");


    // Reproduce FFMPEG packet allocate logic (file libavcodec/avpacket.c function av_packet_duplicate)
    // ...
    //    data = av_malloc(pkt->size + FF_INPUT_BUFFER_PADDING_SIZE);
    // ...
    //MM this causes problems because there is no way to set real buffer size. Allocation has 32 byte alignment - should be enough.
    AMF_RESULT err = m_pContext->AllocBuffer(AMF_MEMORY_HOST, pPacket->size + AV_INPUT_BUFFER_PADDING_SIZE, ppBuffer);
    AMF_RETURN_IF_FAILED(err, L"BufferFromPacket() - AllocBuffer failed");

    AMFBuffer* pBuffer = *ppBuffer;
    err = pBuffer->SetSize(pPacket->size);
    AMF_RETURN_IF_FAILED(err, L"BufferFromPacket() - SetSize failed");

    // get the memory location and check the buffer was indeed allocated
    void* pMem = pBuffer->GetNative();
    AMF_RETURN_IF_FALSE(pMem != NULL, AMF_INVALID_POINTER, L"BufferFromPacket() - GetMemory failed");

    // copy the packet memory and don't forget to
    // clear data padding like it is done by FFMPEG
    memcpy(pMem, pPacket->data, pPacket->size);
    memset(reinterpret_cast<amf_int8*>(pMem)+pPacket->size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    // now that we created the buffer, it's time to update
    // it's properties from the packet information...
    return UpdateBufferProperties(pBuffer, pPacket);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::UpdateBufferProperties(AMFBuffer* pBuffer, const AVPacket* pPacket)
{
    AMF_RETURN_IF_FALSE(pBuffer != NULL, AMF_INVALID_ARG, L"UpdateBufferProperties() - buffer not passed in");
    AMF_RETURN_IF_FALSE(pPacket != NULL, AMF_INVALID_ARG, L"UpdateBufferProperties() - packet not passed in");

    const AVStream*  ist = m_pInputContext->streams[pPacket->stream_index];
    AMF_RETURN_IF_FALSE(ist != NULL, AMF_UNEXPECTED, L"UpdateBufferProperties() - stream not available");

    amf_int64 first_dts = cffstream(ist)->first_dts;
    if (first_dts != AV_NOPTS_VALUE)
    {
        ((AVPacket*)pPacket)->dts += first_dts;
    }

    const amf_int64  pts = av_rescale_q(pPacket->dts, ist->time_base, AMF_TIME_BASE_Q);
    pBuffer->SetPts(pts - GetMinPosition());

    AttachAVPacketInfo(pBuffer, pPacket);

    pBuffer->SetProperty(L"FFMPEG:FirstPtsOffset", AMFVariant(m_ptsInitialMinPosition));

    if (ist->start_time != AV_NOPTS_VALUE)
    {
        pBuffer->SetProperty(L"FFMPEG:start_time", AMFVariant(ist->start_time));
    }
    pBuffer->SetProperty(L"FFMPEG:time_base_den", AMFVariant(ist->time_base.den));
    pBuffer->SetProperty(L"FFMPEG:time_base_num", AMFVariant(ist->time_base.num));


    if ((m_iVideoStreamIndexFFmpeg == -1 || pPacket->stream_index == m_iVideoStreamIndexFFmpeg) && m_ptsSeekPos != -1)
    {
        if (pts < m_ptsSeekPos)
        {
            pBuffer->SetProperty(L"Seeking", AMFVariant(true));
        }

        if (m_ptsSeekPos <= m_ptsPosition)
        {
            pBuffer->SetProperty(L"EndSeeking", AMFVariant(true));

            int default_stream_index = av_find_default_stream_index(m_pInputContext);
            if (pPacket->stream_index == default_stream_index)
            {
                m_ptsSeekPos = -1;
            }
            // set end
        }
        else
        {
            if (pPacket->flags & AV_PKT_FLAG_KEY)
            {
                pBuffer->SetProperty(L"BeginSeeking", AMFVariant(true));
            }
        }
    }

    amf_int32 outputIndex = FromFFmpegToOutputIndex(pPacket->stream_index);
    // update buffer duration, based on the type if info stored
    if (ist->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
    {
        pBuffer->SetProperty(FFMPEG_DEMUXER_BUFFER_TYPE, AMFVariant(AMF_STREAM_VIDEO));
        UpdateBufferVideoDuration(pBuffer, pPacket, ist);
//        AMFTraceWarning(AMF_FACILITY, L"Video count=%lld size=%d PTS=%5.2f", m_OutputStreams[outputIndex]->GetPacketCount(), (int)pBuffer->GetSize(), pBuffer->GetPts() / 10000.);

    }
    else if (ist->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
    {
        pBuffer->SetProperty(FFMPEG_DEMUXER_BUFFER_TYPE, AMFVariant(AMF_STREAM_AUDIO));
        UpdateBufferAudioDuration(pBuffer, pPacket, ist);
//        AMFTraceWarning(AMF_FACILITY, L"Audio count=%lld size=%d PTS=%5.2f", m_OutputStreams[outputIndex]->GetPacketCount(), (int)pBuffer->GetSize(),  pBuffer->GetPts() / 10000.);
    }
    if (outputIndex >= 0)
    {
        pBuffer->SetProperty(FFMPEG_DEMUXER_BUFFER_STREAM_INDEX, outputIndex);
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::UpdateBufferVideoDuration(AMFBuffer* pBuffer, const AVPacket* pPacket, const AVStream *ist)
{
    if (pPacket->duration != 0)
    {
        amf_int64 durationByFFMPEG    = av_rescale_q(pPacket->duration, ist->time_base, AMF_TIME_BASE_Q);
        amf_int64 durationByFrameRate = (amf_int64)((amf_double)AMF_SECOND / ((amf_double)ist->r_frame_rate.num / (amf_double)ist->r_frame_rate.den));
        if (abs(durationByFrameRate - durationByFFMPEG)> AMF_MIN(durationByFrameRate, durationByFFMPEG) / 2)
        {
            durationByFFMPEG = durationByFrameRate;
        }

        pBuffer->SetDuration(durationByFFMPEG);
    }
}
//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::UpdateBufferAudioDuration(AMFBuffer* pBuffer, const AVPacket* pPacket, const AVStream *ist)
{
    if (pPacket->duration != 0)
    {
        amf_int64 durationByFFMPEG = av_rescale_q(pPacket->duration, ist->time_base, AMF_TIME_BASE_Q);
        pBuffer->SetDuration(durationByFFMPEG);
    }
}
//--------------------------------------------------------------------------------------------------------------------------------
static bool GetNextAnnexbNALUType(amf_uint8 *&buf, amf_int &buf_size, int &nal_unit_type, amf_uint8 *&data)
{
    if (buf_size == 0)
    {
        return 0;
    }
    int iNullCount = 0;
    int iStartPos = -1;
    int iEndPos = buf_size;
    amf_uint8 *pData = buf;
    int currStreamPos = 0;
    while (currStreamPos != buf_size)
    {
        unsigned int tmp = *pData;
        pData++;
        currStreamPos++;
        if (tmp == 0)
        {
            iNullCount++;
        }
        else if (tmp == 0x1 && iNullCount >= 2)
        {
            if (iStartPos == -1)
            {
                iStartPos = currStreamPos;
                iNullCount = 0;
            }
            else
            {
                currStreamPos += -iNullCount - 1;
                iEndPos = currStreamPos;
                break;
            }
        }
        else
        {
            iNullCount = 0;
        }
    }
    if (iStartPos == -1) // start not found
    {
        return false;
    }
    //remove trailing 0
    while (iEndPos>0)
    {
        if (buf[iEndPos - 1] != 0)
            break;
        iEndPos--;
    }
    // get it
//    int len = iEndPos - iStartPos;
    nal_unit_type = (buf[iStartPos]) & 0x1f;
    data = buf + iStartPos + 1;
    buf += currStreamPos;
    buf_size -= currStreamPos;
    return true;
}
//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL AMFFileDemuxerFFMPEGImpl::FindSPSAndMVC(const amf_uint8 *buf, amf_int buf_size, bool &has_sps, bool &has_mvc) const
{
    AMFLock lock(&m_sync);
    amf_uint8 *pData = (amf_uint8 *)buf;
    amf_int iDataSize = buf_size;
    while (iDataSize>0)
    {
        int nal_unit_type = 0;
        amf_uint8 *data = NULL;
        if (!GetNextAnnexbNALUType(pData, iDataSize, nal_unit_type, data))
        {
            break;
        }
        if (nal_unit_type == 7) //SPS
        {
            has_sps = true;
        }
        if (nal_unit_type == 15) //NALU_TYPE_SUBSET_SPS
        {
#define PROFILE_H264_MULTI_VIEW1          118   //MVC profile
#define PROFILE_H264_MULTI_VIEW2          128   //MVC profile

            int profile = data[0];
            if (profile == PROFILE_H264_MULTI_VIEW1 || profile == PROFILE_H264_MULTI_VIEW2)
            {
                has_mvc = true;
                break;
            }
        }
        if (nal_unit_type == 20) //NALU_NON_BASE_VIEW_SLICE
        {
            has_mvc = true;
            break;
        }
    }
}
//-------------------------------------------------------------------------------------------------
bool AMF_STD_CALL AMFFileDemuxerFFMPEGImpl::CheckH264MVC()
{
    AMFLock lock(&m_sync);

#ifdef __USE_H264Mp4ToAnnexB
    AMF_RESULT err = AMF_OK;
    // first check extradata
    if(m_iVideoStreamIndexFFmpeg == -1)
    {
        return false;
    }
    AVStream *ist = m_pInputContext->streams[m_iVideoStreamIndexFFmpeg];

//    if(!m_OutputStreams[m_iVideoStreamIndexFFmpeg]->m_bEnabled)
//    {
//        return false;
//    }


    if (ist->codecpar->extradata != NULL)
    {
        bool sps = false;
        bool mvc = false;
        m_H264Mp4ToAnnexB.ProcessExtradata(ist->codecpar->extradata, ist->codecpar->extradata_size);
        FindSPSAndMVC(ist->codecpar->extradata, ist->codecpar->extradata_size, sps, mvc);
        if (mvc)
        {
            return true;
        }
    }

    amf_int iVideoFrames = 5; // look forward for this number of frames
    while (true){
        AVPacket *packet = NULL;
        err = ReadPacket(&packet);
        if (err != AMF_OK)
        {
            return false;
        }

        // if it's the video or audio streams we handle
        // cache the packet, otherwise remove it...
        if ( ((m_iVideoStreamIndexFFmpeg != -1) && (packet->stream_index == m_iVideoStreamIndexFFmpeg)) ||
             ((m_iAudioStreamIndexFFmpeg != -1) && (packet->stream_index == m_iAudioStreamIndexFFmpeg)))
        {

            amf_int32 outputIndex = FromFFmpegToOutputIndex(packet->stream_index);
            if (outputIndex >= 0)
            {
                err = m_OutputStreams[outputIndex]->CachePacket(packet);
                if (err != AMF_OK)
                {
                    continue;
                }
            }
            else
            {
                ClearPacket(packet);
                continue;
            }
        }
        else
        {
            ClearPacket(packet);
            continue;
        }

        if (packet->stream_index != m_iVideoStreamIndexFFmpeg)
        {
            continue;
        }
        // analyze packet
        bool sps = false;
        bool mvc = false;

        amf_uint8 *annexb = NULL;
        amf_size annexb_size = 0;
        m_H264Mp4ToAnnexB.Filter(&annexb, &annexb_size, (amf_uint8*)packet->data, packet->size);

        FindSPSAndMVC(annexb, (amf_int)annexb_size, sps, mvc);
        if (mvc)
        {
            return true;
        }
        if (sps)
        {
            return false;
        }
        iVideoFrames--;
        if (iVideoFrames == 0)
        {
            break;
        }
    }
#endif
    return false;
}
//-------------------------------------------------------------------------------------------------
amf_uint64 AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::GetPropertyStartFrame()
{
    amf_int val = 0;
    AMF_ASSERT_OK(GetProperty(FFMPEG_DEMUXER_START_FRAME, &val));
    return val;
}
//-------------------------------------------------------------------------------------------------
amf_uint64 AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::GetPropertyFramesNumber()
{
    AMFLock lock(&m_sync);
    amf_uint64 val = 0;
    AMF_ASSERT_OK(GetProperty(FFMPEG_DEMUXER_FRAME_COUNT, &val));
    if (val == 0)
    {
        val = GetFrameFromPts(GetDuration());
    }
    return val;
}
//-------------------------------------------------------------------------------------------------
amf_pts AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::CheckPtsRange(amf_pts pts)
{
    AMFLock lock(&m_sync);
    if (pts < 0)
    {
        pts = 0;
    }

    if (pts > GetDuration())
    {
        pts = GetDuration();
    }

    return pts;
}
//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL AMFFileDemuxerFFMPEGImpl::ReadRangeSettings()
{
    AMFLock lock(&m_sync);
    m_ptsInitialMinPosition = GetMinPosition();
}
//-------------------------------------------------------------------------------------------------
bool AMF_STD_CALL       AMFFileDemuxerFFMPEGImpl::IsCached()
{
    for (amf_vector<AMFOutputDemuxerImplPtr>::iterator it = m_OutputStreams.begin(); it != m_OutputStreams.end(); ++it)
    {
        if((*it)->IsCached())
        {
            return true;
        }
    }
    return false;
}
amf_int32   AMF_STD_CALL  AMFFileDemuxerFFMPEGImpl::GetOutputCount()
{
    AMFLock lock(&m_sync);
    return (amf_int32)m_OutputStreams.size();
};
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFFileDemuxerFFMPEGImpl::OpenAsImageSequence(
    amf_string filename,
    AVInputFormat* pFmt,
    AVDictionary* pOptions)
{
    avformat_close_input(&m_pInputContext);
    size_t posEnd = filename.rfind(".");
    size_t posStart = filename.find_last_of("\\/");
    amf_string ext = filename.c_str() + posEnd;
    amf_string fmt = "%00d";
    std::string startNum = "";
    for (size_t idx = posEnd - 1; idx > posStart; idx--)
    {
        const char* pBuf = (filename.c_str() + idx);
        if ((*pBuf < '0') || (*pBuf > '9')) break;
        fmt[2] += 1;
        startNum.insert(0, pBuf, 1);
        filename.erase(idx);
    }

    filename += fmt + ext;
    amf_int32 iStartNum = std::stoi(startNum);
    av_dict_set_int(&pOptions, "start_number", iStartNum, 0);
    amf_bool bIsImage(false);
    return OpenFile(filename, pFmt, pOptions, bIsImage);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFFileDemuxerFFMPEGImpl::OpenFile(
    amf_string filename,
    AVInputFormat* pFmt,
    AVDictionary* pOptions,
    amf_bool& bIsImage)
{
    bIsImage = false;
    int ret = avformat_open_input(&m_pInputContext, filename.c_str(), pFmt, &pOptions);
	if (ret < 0)
	{
        return AMF_NOT_FOUND;
	}
    // disable raw video support. toos should use raw reader
    ret = avformat_find_stream_info(m_pInputContext, NULL);
    if (ret < 0)
    {
        return AMF_NOT_SUPPORTED;
    }

    if (m_pInputContext->metadata != nullptr)
    {
        AVDictionaryEntry *entry = nullptr;
        while (true)
        {
            entry = av_dict_get(m_pInputContext->metadata, "", entry, AV_DICT_IGNORE_SUFFIX);
            if (entry == nullptr)
            {
                break;
            }
            //            AMFTraceWarning(AMF_FACILITY, L"Metadata:%S:%S", entry->key, entry->value);
        }
    }


    // performs some checks on the video and audio streams
    if (m_pInputContext->duration != AV_NOPTS_VALUE)
    {
        m_ptsDuration = av_rescale_q(m_pInputContext->duration, FFMPEG_TIME_BASE_Q, AMF_TIME_BASE_Q);
    }
    else
    {
        m_ptsDuration = 0;
    }
    for (amf_uint32 i = 0; i < m_pInputContext->nb_streams; i++)
    {
        const AVStream* ist = m_pInputContext->streams[i];

        if (ist->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            if (ist->metadata != nullptr)
            {
                AVDictionaryEntry *entry = nullptr;
                while (true)
                {
                    entry = av_dict_get(ist->metadata, "", entry, AV_DICT_IGNORE_SUFFIX);
                    if (entry == nullptr)
                    {
                        break;
                    }
                    //                    AMFTraceWarning(AMF_FACILITY, L"Metadata:%S:%S", entry->key, entry->value);
                }
            }

            if ((ist->codecpar->codec_id == AV_CODEC_ID_EXR)  ||
                (ist->codecpar->codec_id == AV_CODEC_ID_TIFF) ||
                (ist->codecpar->codec_id == AV_CODEC_ID_PNG))
            {
                bIsImage = true;
            }

            if (ist->codecpar->format == AV_PIX_FMT_BGR24)
            {
                AMFTraceError(AMF_FACILITY, L"Picture format PIX_FMT_BGR24 is not supported.");
                return AMF_INVALID_FORMAT;
            }

            if (m_iVideoStreamIndexFFmpeg < 0)
            {
                m_iVideoStreamIndexFFmpeg = i;
            }
            else
            { // find biggest
                const AVStream *ist_prev = m_pInputContext->streams[m_iVideoStreamIndexFFmpeg];
                if (ist->codecpar->width > ist_prev->codecpar->width && ist->codecpar->height > ist_prev->codecpar->height)
                {
                    m_iVideoStreamIndexFFmpeg = i;
                }
            }
        }
        if (ist->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            if (m_iAudioStreamIndexFFmpeg<0)
            {
                m_iAudioStreamIndexFFmpeg = i;
            }
            else
            { // find biggest
                const AVStream *ist_prev = m_pInputContext->streams[m_iAudioStreamIndexFFmpeg];
                if (ist->codecpar->sample_rate >ist_prev->codecpar->sample_rate)
                {
                    m_iAudioStreamIndexFFmpeg = i;
                }
            }
        }
    }
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
amf_int32               AMFFileDemuxerFFMPEGImpl::FromFFmpegToOutputIndex(amf_int32 indexFFmpeg)
{
    for (size_t idx = 0; idx < m_OutputStreams.size(); idx++)
    {
        if (m_OutputStreams[idx]->m_iIndexFFmpeg == indexFFmpeg)
        {
            return (amf_int32)idx;
        }
    }
    return -1;
}
//-------------------------------------------------------------------------------------------------
amf_int32               AMFFileDemuxerFFMPEGImpl::FromOutputToFFmpegIndex(amf_int32 indexOutput)
{
    if (indexOutput < amf_int32(m_OutputStreams.size()))
    {
        return m_OutputStreams[indexOutput]->m_iIndexFFmpeg;
    }
    return -1;
}
//-------------------------------------------------------------------------------------------------

