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
#pragma once

#include "public/include/components/Component.h"
#include "public/include/core/Buffer.h"

#if defined(_MSC_VER)
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#endif

extern "C"
{
#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable:4244)
#endif

    #include "libavformat/avformat.h"
    #include "libavformat/url.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/opt.h"

#if defined(_MSC_VER)
    #pragma warning(pop)
#endif
}

#include <type_traits>



class AVFrameEx : public AVFrame
{
public:
    AVFrameEx()
    {
        static_assert(std::is_polymorphic<AVFrameEx>::value == false, "AVFrameEx should not have a vTable");
        memset(this, 0, sizeof(AVFrame));
    }
    ~AVFrameEx()
    {
        av_frame_unref(this);
    }
};



class AVPacketEx : public AVPacket
{
public:
    AVPacketEx()
    {
        static_assert(std::is_polymorphic<AVPacketEx>::value == false, "AVPacketEx should not have a vTable");
        memset(this, 0, sizeof(AVPacket));
        av_init_packet(this);
    }
    ~AVPacketEx()
    {
        av_packet_unref(this);
    }
};



namespace amf
{
    void                      AMF_STD_CALL   InitFFMPEG();

    AMF_AUDIO_FORMAT          AMF_STD_CALL   GetAMFAudioFormat(AVSampleFormat inFormat);
    AVSampleFormat            AMF_STD_CALL   GetFFMPEGAudioFormat(AMF_AUDIO_FORMAT inFormat);

    amf_int32                 AMF_STD_CALL   GetAudioSampleSize(AMF_AUDIO_FORMAT inFormat);
    bool                      AMF_STD_CALL   IsAudioPlanar(AMF_AUDIO_FORMAT inFormat);

    AMF_STREAM_CODEC_ID_ENUM  AMF_STD_CALL   GetAMFVideoFormat(AVCodecID inFormat);
    AVCodecID                 AMF_STD_CALL   GetFFMPEGVideoFormat(AMF_STREAM_CODEC_ID_ENUM inFormat);

    bool                      AMF_STD_CALL   ReadAVPacketInfo(AMFBuffer* pBuffer, AVPacket* pPacket);
    void                      AMF_STD_CALL   AttachAVPacketInfo(AMFBuffer* pBuffer, const AVPacket* pPacket);

    amf_pts                   AMF_STD_CALL   GetPtsFromFFMPEG(AMFBuffer* pBuffer, AVFrame* pFrame);

    AMF_SURFACE_FORMAT        AMF_STD_CALL   GetAMFSurfaceFormat(AVPixelFormat eFormat);
    AVPixelFormat             AMF_STD_CALL   GetFFMPEGSurfaceFormat(AMF_SURFACE_FORMAT eFormat);
}

// there is no definition in FFMPEG for H264MVC so create an ID
// based on the last element in their enumeration
#define AMF_CODEC_H265MAIN10      1005
#define AV_CODEC_H264MVC          1006
#define AMF_CODEC_VP9_10BIT       1007
#define AMF_CODEC_AV1_12BIT       1008

extern AVRational AMF_TIME_BASE_Q;
extern AVRational FFMPEG_TIME_BASE_Q;
