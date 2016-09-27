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
#pragma once

#include "public/include/core/Context.h"


extern "C"
{
    #include "libavformat/avformat.h"
    #include "libavformat/url.h"
    #include "libavcodec/avcodec.h"
    #include "libavcodec/audioconvert.h"
    #include "libavresample/avresample.h"
    #include "libavutil/opt.h"
}


namespace amf
{
    void              AMF_STD_CALL   InitFFMPEG();

    AMF_AUDIO_FORMAT  AMF_STD_CALL   GetAMFAudioFormat(AVSampleFormat inFormat);
    AVSampleFormat    AMF_STD_CALL   GetFFMPEGAudioFormat(AMF_AUDIO_FORMAT inFormat);

    amf_int32         AMF_STD_CALL   GetAudioSampleSize(AMF_AUDIO_FORMAT inFormat);
    bool              AMF_STD_CALL   IsAudioPlanar(AMF_AUDIO_FORMAT inFormat);
    
}

// there is no definition in FFMPEG for H264MVC so create an ID
// based on the last element in their enumeration
extern const amf_int  AV_CODEC_H264MVC;
extern AVRational AMF_TIME_BASE_Q;
extern AVRational FFMPEG_TIME_BASE_Q;
