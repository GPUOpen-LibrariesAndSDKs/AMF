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

#include "AudioConverterFFMPEGImpl.h"
#include "AudioDecoderFFMPEGImpl.h"
#include "AudioEncoderFFMPEGImpl.h"
#include "FileDemuxerFFMPEGImpl.h"
#include "FileMuxerFFMPEGImpl.h"


//define export declaration
#ifdef _WIN32
        #if defined(AMF_COMPONENT_FFMPEG_EXPORTS)
            #define AMF_COMPONENT_FFMPEG_LINK __declspec(dllexport)
        #else
            #define AMF_COMPONENT_FFMPEG_LINK __declspec(dllimport)
        #endif
#else
    #define AMF_COMPONENT_FFMPEG_LINK
#endif

extern "C"
{
    AMF_COMPONENT_FFMPEG_LINK AMF_RESULT AMF_CDECL_CALL AMFCreateComponentInt(amf::AMFContext* pContext, void* reserved, amf::AMFComponent** ppComponent)
    {
        // if no component name passed in, we don't know
        // which component to create
        if (!reserved)
        {
            return AMF_INVALID_ARG;
        }

        const amf_wstring  name((const wchar_t*) reserved);
        if (name == FFMPEG_MUXER)
        {
            *ppComponent = new amf::AMFInterfaceMultiImpl< amf::AMFFileMuxerFFMPEGImpl, amf::AMFComponentEx, amf::AMFContext* >(pContext);
        }
        else if (name == FFMPEG_DEMUXER)
        {
            *ppComponent = new amf::AMFInterfaceMultiImpl< amf::AMFFileDemuxerFFMPEGImpl, amf::AMFComponentEx, amf::AMFContext* >(pContext);
        }
        else if (name == FFMPEG_AUDIO_ENCODER)
        {
            *ppComponent = new amf::AMFInterfaceMultiImpl< amf::AMFAudioEncoderFFMPEGImpl, amf::AMFComponent, amf::AMFContext* >(pContext);
        }
        else if (name == FFMPEG_AUDIO_DECODER)
        {
            *ppComponent = new amf::AMFInterfaceMultiImpl< amf::AMFAudioDecoderFFMPEGImpl, amf::AMFComponent, amf::AMFContext* >(pContext);
        }
        else if (name == FFMPEG_AUDIO_CONVERTER)
        {
            *ppComponent = new amf::AMFInterfaceMultiImpl< amf::AMFAudioConverterFFMPEGImpl, amf::AMFComponent, amf::AMFContext* >(pContext);
        }

        if (*ppComponent)
        {
            (*ppComponent)->Acquire();
            return AMF_OK;
        }

        return AMF_FAIL;
    }
}
