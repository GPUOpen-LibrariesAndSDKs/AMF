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
#include <map>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <pulse/pulseaudio.h>
#include "../../../common/AMFSTL.h"
#include "../../../common/InterfaceImpl.h"
#include "../../../common/Linux/PulseAudioImportTable.h"
#include "../../../include/core/Context.h"
#include "../../../include/components/AudioCapture.h"

namespace amf
{
    //-------------------------------------------------------------------------------------------------
    class AMFPulseAudioSimpleAPISourceImpl
    {
    protected:
        typedef std::vector<amf_string> PASourceList;

        pa_simple*                      m_pPaSimple = nullptr;
        // Hard code these info for now. By default we use signed 16 bits, stereo, little endian,
        // and 44100 sample rate(to avoid pulse audio server to resample)
        // if they become non-const in the future, communication with the subprocess must be added
        // to AMFPulseAudioSimpleAPISourceFacade
        const amf_uint32                m_SampleRate = 44100;
        const amf_uint32                m_ChannelCount = 2;
        const amf_uint32                m_SampleCount = 128;
        const amf_uint64                m_Format = AMFAF_S16;
        const amf_uint32                m_BlockAlign = 2; // Bytes per sample, 2 bytes by default.
        const amf_uint32                m_FrameSize = 2;
        amf_string                      m_DefaultSinkMonitor = "";
        amf_string                      m_DefaultSource = "";
        PASourceList                    m_SrcList;
        PASourceList                    m_SinkMonitorList;
        PASourceList                    m_SinkList;

        // Get the device names from pulse audio async api, and sets the m_DisplaySrc, m_MicSrc
        // This is only called within Init.
        // TODO: get a list of mic and displays.
        AMF_RESULT InitDeviceNames();
    public:
        AMFPulseAudioSimpleAPISourceImpl();
        virtual ~AMFPulseAudioSimpleAPISourceImpl();

        // Setup and teardown.
        virtual AMF_RESULT Init(bool captureMic);
        virtual AMF_RESULT Terminate();

        PulseAudioImportTable m_pa;

        // PulseAudio simple API does not support async calls. Read will block until specified
        // amount of data has been read into the buffer.
        // With those constraints, currently we always capture 490 samples (corresponds to 1/90 ms)
        // so capturedSampleCount will always be 490.
        // CaptureAudio allocates pAudioBuffer and directly capture data into it.
        virtual AMF_RESULT CaptureAudio(AMFAudioBufferPtr& pAudoBuffer, AMFContextPtr& pContext, amf_uint32& capturedSampleCount);
        AMF_RESULT CaptureAudioRaw(short* dest, amf_uint32 sampleCount, amf_uint32& capturedSampleCount);

        void AddToSourceList(amf_string& srcName);
        void AddToSinkMonitorList(amf_string& srcName);
        void AddToSinkList(amf_string& sinkName);
        void SetDefaultSource(amf_string& deviceName);
        void SetDefaultSinkMonitor(amf_string& devcieName);

        // Getters.

        // Returns a tab seperated string of device names, containing mic and displays.
        // Currently only contains default display.
        amf_string GetDeviceNames()                  { return m_SrcList[0];   }
        amf_uint32 GetSampleRate()                   { return m_SampleRate;   }
        amf_uint32 GetSampleCount()                  { return m_SampleCount;  }
        amf_uint32 GetChannelCount()                 { return m_ChannelCount; }
        amf_uint64 GetFormat()                       { return m_Format;       }
        amf_uint32 GetBlockAlign()                   { return m_BlockAlign;   }
        amf_uint32 GetFrameSize()                    { return m_FrameSize;    }
        PASourceList GetSinkList()                   { return m_SinkList;     }
        PASourceList GetSourceList(bool captureMic)  { return (true == captureMic)? m_SrcList : m_SinkMonitorList; }
    };
    typedef std::shared_ptr<AMFPulseAudioSimpleAPISourceImpl>    AMFPulseAudioSimpleAPISourceImplPtr;
}
