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
#include "AudioPresenterLinux.h"

#include "public/include/core/Context.h"
#include "public/include/core/Trace.h"
#include "public/common/TraceAdapter.h"

#if defined(__linux)

//#include <iostream>

#include <alsa/asoundlib.h>


using namespace amf;

#define AMF_FACILITY L"AudioPresenterLinux"

enum {BITS = 32, CHANNELS = 2, SAMPLE_RATE = 44100};

//-------------------------------------------------------------------------------------------------
AudioPresenterLinux::AudioPresenterLinux() :
    m_pSndPcm(nullptr),
    m_eEngineState(AMFAPS_UNKNOWN_STATUS),
    m_iAVSyncDelay(-1LL),
    m_iLastTime(0),
    m_uiLastBufferDataOffset(0)
{
}
//-------------------------------------------------------------------------------------------------
AudioPresenterLinux::~AudioPresenterLinux()
{
    Terminate();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterLinux::Init()
{
    amf::AMFLock lock(&m_cs);
    if(m_eEngineState != AMFAPS_UNKNOWN_STATUS)
    {
        return AMF_OK;
    }

    AMF_RETURN_IF_FAILED(InitAlsa());
    AMF_RETURN_IF_FAILED(SetAlsaParameters());
    m_eEngineState = AMFAPS_STOPPED_STATUS;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterLinux::Terminate()
{
    amf::AMFLock lock(&m_cs);
    if(m_pSndPcm != nullptr)
    {
        snd_pcm_close((snd_pcm_t*)m_pSndPcm);
        m_pSndPcm = nullptr;
    }
    m_eEngineState=AMFAPS_UNKNOWN_STATUS;

    m_iAVSyncDelay = -1LL;
    m_iLastTime = 0;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterLinux::InitAlsa()
{
    int alsaErr;
    alsaErr = snd_pcm_open((snd_pcm_t**)&m_pSndPcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if(alsaErr != 0)
    {
        return AMF_ALSA_FAILED;
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterLinux::SetAlsaParameters()
{
    int alsaErr;
    snd_pcm_hw_params_t* pcmHwParams = 0;
    alsaErr = snd_pcm_hw_params_malloc(&pcmHwParams);
    if(alsaErr != 0)
    {
        return AMF_ALSA_FAILED;
    }

    alsaErr = snd_pcm_hw_params_any((snd_pcm_t*)m_pSndPcm, pcmHwParams);

    if(alsaErr >= 0)
    {
        snd_pcm_format_t format;
        if(BITS == 8)
        {
            format = SND_PCM_FORMAT_U8;
        }
        else if(BITS == 16)
        {
            format = SND_PCM_FORMAT_S16_LE;
        }
        else if(BITS == 32)
        {
            format = SND_PCM_FORMAT_S32_LE;
        }
        else
        {
            AMF_RETURN_IF_FALSE(false, AMF_INVALID_ARG);
        }

        alsaErr = snd_pcm_hw_params_set_format((snd_pcm_t*)m_pSndPcm, pcmHwParams, format);
    }

    if(alsaErr == 0)
    {
        alsaErr = snd_pcm_hw_params_set_rate((snd_pcm_t*)m_pSndPcm, pcmHwParams, SAMPLE_RATE, 0);
    }

    if(alsaErr == 0)
    {
        alsaErr = snd_pcm_hw_params_set_channels((snd_pcm_t*)m_pSndPcm, pcmHwParams, CHANNELS);
    }

    if(alsaErr == 0)
    {
        alsaErr = snd_pcm_hw_params_set_access(
                    (snd_pcm_t*)m_pSndPcm,
                    pcmHwParams,
                    SND_PCM_ACCESS_RW_INTERLEAVED);
    }

    if(alsaErr == 0)
    {
        alsaErr = snd_pcm_hw_params((snd_pcm_t*)m_pSndPcm, pcmHwParams);
    }

    snd_pcm_hw_params_free(pcmHwParams);

    snd_pcm_sw_params_t* pcmSwParams = 0;
    if(alsaErr == 0)
    {
        alsaErr = snd_pcm_sw_params_malloc(&pcmSwParams);
    }
    if(alsaErr != 0)
    {
        return AMF_ALSA_FAILED;
    }

    if(alsaErr == 0)
    {
        alsaErr = snd_pcm_sw_params_current((snd_pcm_t*)m_pSndPcm, pcmSwParams);
    }

    if(alsaErr == 0)
    {
        alsaErr = snd_pcm_sw_params_set_start_threshold((snd_pcm_t*)m_pSndPcm, pcmSwParams, SAMPLE_RATE / 12);
    }

    if(alsaErr == 0)
    {
        alsaErr = snd_pcm_sw_params((snd_pcm_t*)m_pSndPcm, pcmSwParams);
    }

    snd_pcm_sw_params_free(pcmSwParams);

    return alsaErr == 0 ? AMF_OK : AMF_ALSA_FAILED;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterLinux::SubmitInput(amf::AMFData* pData)
{
    amf::AMFLock lock(&m_cs);

    if (m_bFrozen)
    {
        return AMF_INPUT_FULL;
    }
    // check if we get new input - if we don't and we don't 
    // have anything cached, there's nothing to process
    if (pData == NULL)
    {
        return AMF_EOF;
    }

    if(m_pAVSync != NULL)
    {
        if (m_pAVSync->IsVideoStarted() == false)
        {
            if (m_iAVSyncDelay == -1LL)
            {
                m_iAVSyncDelay = amf_high_precision_clock();;
            }
            return AMF_INPUT_FULL;
        }
        if (ShouldPresentAudioOrNot(m_pAVSync->GetVideoPts(), pData->GetPts()) == false)
        {
            return AMF_OK;
        }
        else
        {
            m_pAVSync->SetAudioPts(pData->GetPts());
        }
    }
    amf::AMFAudioBufferPtr pAudioBuffer(pData);

    amf_pts  ptsSleepTime = 0;
    AMF_RESULT err = Present(pAudioBuffer, ptsSleepTime);

    if(m_startTime == -1LL)
    {
//        AMFTraceWarning(AMF_FACILITY, L"Delay was =%5.2f", (double)(amf_high_precision_clock() - m_iAVSyncDelay) / 10000);

        m_startTime = amf_high_precision_clock();
        m_ptsSeek = pData->GetPts();
        m_iLastTime = m_startTime;
    }
    else
    { 
//        WAVEFORMATEX *pMixFormat=(WAVEFORMATEX *)m_pMixFormat;
//        amf_pts currTime = amf_high_precision_clock() - m_startTime;
//        AMFTraceWarning(AMF_FACILITY, L"Between calls=%5.2f err=%s buffers=%d offset=%d written=%d", 
//            (double)(currTime - m_iLastTime) / 10000, err == AMF_INPUT_FULL ? L"InputFull" : L"OK",
//            (int)m_UnusedBuffers.size(), (int)m_uiLastBufferDataOffset / (pMixFormat->wBitsPerSample / 8 ), (int)m_uiNumSamplesWritten);
//        m_iLastTime = currTime;
    }

    if (ptsSleepTime > 0)
    {
        amf_sleep(amf_long(ptsSleepTime / 10000)); // to milliseconds
    }

    return err;    
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT   AudioPresenterLinux::Present(amf::AMFAudioBuffer* buffer, amf_pts &sleeptime)
{
    AMF_RETURN_IF_FALSE(m_pSndPcm!=nullptr, AMF_NOT_INITIALIZED, L"Present() - Audio Client");

    AMF_RETURN_IF_FALSE(m_eEngineState==AMFAPS_STOPPED_STATUS || m_eEngineState==AMFAPS_PLAYING_STATUS || m_eEngineState==AMFAPS_PAUSED_STATUS,AMF_WRONG_STATE, L"Present() - Wrong State");

    if(m_eEngineState == AMFAPS_PAUSED_STATUS)
    {
        return AMF_INPUT_FULL;
    }
    // handle seek
    bool bDiscard = false;
    HandleSeek(buffer, bDiscard, m_uiLastBufferDataOffset);
    if (bDiscard)
    {
        return AMF_OK;
    }

    if(m_eEngineState == AMFAPS_STOPPED_STATUS)
    {
        m_eEngineState = AMFAPS_PLAYING_STATUS;
    }

    // get our buffer data
    amf_uint8* pInputData = static_cast<amf_uint8*>(buffer->GetNative());
    amf_size uiBufMemSize=buffer->GetSize();

    const int sampleSize = BITS * CHANNELS / 8;
    const int iWritten = snd_pcm_writei((snd_pcm_t*)m_pSndPcm, pInputData, uiBufMemSize / sampleSize);

    if(iWritten < 0)
    {
        snd_pcm_recover((snd_pcm_t*)m_pSndPcm, iWritten, 1);
        snd_pcm_writei((snd_pcm_t*)m_pSndPcm, pInputData, uiBufMemSize / sampleSize);
    }
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterLinux::Pause()
{
    amf::AMFLock lock(&m_cs);
    AMF_RETURN_IF_FALSE(m_pSndPcm != 0, AMF_NOT_INITIALIZED);

    AMF_RESULT err = AMF_OK;
    if(m_eEngineState == AMFAPS_PLAYING_STATUS)
    {
        snd_pcm_drop((snd_pcm_t*)m_pSndPcm);
        m_eEngineState = AMFAPS_PAUSED_STATUS;
    }
    return err;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterLinux::Resume(amf_pts currentTime)
{
    amf::AMFLock lock(&m_cs);
    AMF_RETURN_IF_FALSE(m_pSndPcm != 0, AMF_NOT_INITIALIZED);

    AMF_RESULT err = AMF_OK;
    if(m_eEngineState == AMFAPS_PAUSED_STATUS)
    {
        snd_pcm_prepare((snd_pcm_t*)m_pSndPcm);
        Reset();
        Seek(currentTime);
        m_eEngineState = AMFAPS_PLAYING_STATUS;
    }
    return err;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterLinux::Step()
{
    amf::AMFLock lock(&m_cs);
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_AUDIO_PLAYBACK_STATUS AudioPresenterLinux::GetStatus()
{
    amf::AMFLock lock(&m_cs);
   return m_eEngineState;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterLinux::Reset()
{
    amf::AMFLock lock(&m_cs);

    m_eEngineState = AMFAPS_PLAYING_STATUS;
    m_iAVSyncDelay = -1LL;
    
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterLinux::GetDescription(
        amf_int64 &streamBitRate,
        amf_int64 &streamSampleRate,
        amf_int64 &streamChannels,
        amf_int64 &streamFormat,
        amf_int64 &streamLayout,
        amf_int64 &streamBlockAlign
    ) const
{
     amf::AMFLock lock(&m_cs);

    if(m_pSndPcm == nullptr)
    {
        return AMF_NOT_INITIALIZED;
    }
    //{BITS = 32, CHANNELS = 2, SAMPLE_RATE = 44100};
    streamBitRate = (amf_int64)BITS * SAMPLE_RATE * CHANNELS;
    streamSampleRate = SAMPLE_RATE;
    streamChannels = CHANNELS;
    streamFormat = AMFAF_S32;
    streamLayout = 3;
    streamBlockAlign = 0;
    
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterLinux::Flush()
{
    amf::AMFLock lock(&m_cs);
    Reset();
    AudioPresenter::Flush();
    return AMF_OK;
}

#endif //#if defined(__linux)
