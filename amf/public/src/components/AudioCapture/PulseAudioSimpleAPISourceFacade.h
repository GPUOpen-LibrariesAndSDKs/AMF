//// Notice Regarding Standards.  AMD does not provide a license or sublicense to
// any Intellectual Property Rights relating to any standards, including but not
// limited to any audio and/or video codec technologies such as MPEG-2, MPEG-4;
// AVC/H.264; HEVC/H.265; AAC decode/FFMPEG; AAC encode/FFMPEG; VC-1; and MP3
// (collectively, the "Media Technologies"). For clarity, you will pay any
// royalties due for such third party technologies, which may include the Media
// Technologies that are owed as a result of AMD providing the Software to you.
//// MIT license////
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

#pragma once

#include <memory>
#include <sys/types.h>
#include <unistd.h>
#include "PulseAudioSimpleAPISource.h"
#include "../../../common/AMFSTL.h"
#include "../../../common/InterfaceImpl.h"
#include "../../../include/core/Context.h"
#include "../../../include/components/AudioCapture.h"

namespace amf
{
    //-------------------------------------------------------------------------------------------------
    // this class allows a root user to use pulseaudio by forking a subprocess that drops permissions
    class AMFPulseAudioSimpleAPISourceFacade : public AMFPulseAudioSimpleAPISourceImpl
    {
    public:
        AMFPulseAudioSimpleAPISourceFacade();
        virtual ~AMFPulseAudioSimpleAPISourceFacade();

        virtual AMF_RESULT Init(bool captureMic) override;
        virtual AMF_RESULT Terminate() override;

        virtual AMF_RESULT CaptureAudio(AMFAudioBufferPtr& pAudioBuffer, AMFContextPtr& pContext, amf_uint32& capturedSampleCount) override;
    private:
        AMF_RESULT Run(bool captureMic);

        AMF_RESULT Receive(int socket, void* data, size_t size);
        AMF_RESULT Send(int socket, const void* data, size_t size);
        AMF_RESULT ReceiveStringList(int socket, PASourceList& list);
        AMF_RESULT SendStringList(int socket, const PASourceList& list);

        mutable AMFCriticalSection               m_sync;
        pid_t m_iChildPid = 0;
        bool m_isRoot     = false;
        int m_iSockets[2] = {};
    };
    typedef std::shared_ptr<AMFPulseAudioSimpleAPISourceFacade>    AMFPulseAudioSimpleAPISourceFacadePtr;

}