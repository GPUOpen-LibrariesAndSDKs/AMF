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

#include "AudioPresenter.h"

#if defined(_WIN32)

#include "public/common/AMFSTL.h"

#if defined(METRO_APP)
#include <collection.h>
#include <windows.ui.xaml.media.dxinterop.h>
using namespace Platform;
using namespace Windows::UI;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
#endif



enum AMF_AUDIO_PLAYBACK_STATUS
{
    AMFAPS_UNKNOWN_STATUS = -1,
    AMFAPS_PLAYING_STATUS = 2,
    AMFAPS_PAUSED_STATUS = 3,
    AMFAPS_STOPPED_STATUS = 6,
    AMFAPS_STOPPING_STATUS = 7,
    AMFAPS_EOF_STATUS = 8,
};



class AudioPresenterWin: public AudioPresenter
{
public:
    AudioPresenterWin();
    virtual ~AudioPresenterWin();

    virtual AMF_RESULT GetDescription(
        amf_int64 &streamBitRate,
        amf_int64 &streamSampleRate,
        amf_int64 &streamChannels,
        amf_int64 &streamFormat,
        amf_int64 &streamLayout,
        amf_int64 &streamBlockAlign
    ) const;

    // PipelineElement
    virtual AMF_RESULT SubmitInput(amf::AMFData* pData);
    virtual AMF_RESULT Flush();

        
    virtual AMF_RESULT          Init();

    AMF_RESULT                  Terminate();
    AMF_RESULT                  Pause();
    AMF_RESULT                  Resume(amf_pts currentTime);
    AMF_RESULT                  Step();
    AMF_AUDIO_PLAYBACK_STATUS   GetStatus();
    AMF_RESULT                  Reset();


protected:
    virtual AMF_RESULT          AMF_STD_CALL Present(amf::AMFAudioBuffer* pBuffer, amf_pts &sleeptime);

    AMF_RESULT                  InitDevice();
    AMF_RESULT                  SelectFormat();
    AMF_RESULT                  InitClient();
    AMF_RESULT                  PushBuffer(amf::AMFAudioBufferPtr &buffer, amf_size &uiBufferDataOffset); // uiBufferDataOffset - in/out

    amf::amf_vector<amf::AMFAudioBufferPtr>  m_UnusedBuffers;

    amf_handle                  m_pDevice;
    amf_handle                  m_pAudioClient;
    amf_handle                  m_pMixFormat;
    amf_handle                  m_pRenderClient;
    amf_size                    m_uiLastBufferDataOffset;
    amf_pts                     m_iBufferDuration;
    amf_uint32                  m_iBufferSampleSize;

    AMF_AUDIO_PLAYBACK_STATUS   m_eEngineState;
    amf_pts                     m_iAVSyncDelay;
    amf_pts                     m_iLastTime;
    UINT32                      m_uiNumSamplesWritten;
};


typedef std::shared_ptr<AudioPresenterWin> AudioPresenterWinPtr;

#endif //#if defined(_WIN32)