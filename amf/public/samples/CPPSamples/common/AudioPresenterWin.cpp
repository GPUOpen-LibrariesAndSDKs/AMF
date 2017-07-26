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

#include "AudioPresenterWin.h"

#include "public/include/core/Context.h"
#include "public/include/core/Trace.h"
#include "public/common/TraceAdapter.h"


#if defined(_WIN32)

#include <windows.h>
#include <comdef.h>
#include <MMDeviceAPI.h>
#include <AudioClient.h>
#include <AudioPolicy.h>

#define TRACE_AMBISONIC_STAT 0

using namespace amf;


#ifndef AMF_SAFE_RELEASE
template <class T>
inline void AMF_SAFE_RELEASE(T*& p)
{
    if (p)
    {
        p->Release();
        p = NULL;
    }
}
#endif

#define CHECK_HR(hr) if (FAILED(hr)) { return AMF_DIRECTX_FAILED; }

_COM_SMARTPTR_TYPEDEF(IMMDeviceEnumerator, __uuidof(IMMDeviceEnumerator));
_COM_SMARTPTR_TYPEDEF(IMMDeviceCollection, __uuidof(IMMDeviceCollection));



AudioPresenterWin::AudioPresenterWin() 
  : m_pDevice(NULL),
    m_pAudioClient(NULL),
    m_pMixFormat(NULL),
    m_pRenderClient(NULL),
    m_iBufferDuration(0),
    m_iBufferSampleSize(0),
    m_uiLastBufferDataOffset(0),
    m_eEngineState(AMFAPS_UNKNOWN_STATUS),
    m_iAVSyncDelay(-1LL),
    m_iLastTime(0),
    m_uiNumSamplesWritten(0)
{
}
//-------------------------------------------------------------------------------------------------
AudioPresenterWin::~AudioPresenterWin()
{
    Terminate();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterWin::Init()
{
    amf::AMFLock lock(&m_cs);

    if(m_eEngineState!=AMFAPS_UNKNOWN_STATUS)
    {
        return AMF_OK;
    }
    AMF_RESULT err=AMF_OK;
    err=InitDevice();
    if(err != AMF_OK)
    {
        return err;
    }
    AMF_RETURN_IF_FAILED(err,L"Init() - InitDevice failed");
    err=SelectFormat();
    AMF_RETURN_IF_FAILED(err,L"Init() - SelectFormat failed");
    err=InitClient();
    AMF_RETURN_IF_FAILED(err,L"Init() - InitClient failed");

    m_eEngineState=AMFAPS_STOPPED_STATUS;
    return err;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterWin::Terminate()
{
    amf::AMFLock lock(&m_cs);

    AMF_RESULT err=AMF_OK;

    m_UnusedBuffers.clear();

    if(m_pRenderClient!=NULL){
        AMF_SAFE_RELEASE((IAudioRenderClient*&)m_pRenderClient);
    }

    if(m_pAudioClient!=NULL){
        AMF_SAFE_RELEASE((IAudioClient*&)m_pAudioClient);
    }
    m_pAudioClient=NULL;

    if(m_pDevice!=NULL){
        AMF_SAFE_RELEASE((IMMDevice*&)m_pDevice);
    }
    m_pDevice=NULL;

    if(m_pMixFormat!=NULL){
        CoTaskMemFree(m_pMixFormat);
    }
    m_pMixFormat=NULL;

    m_iBufferDuration=0;
    m_iBufferSampleSize=0;
    m_uiLastBufferDataOffset=0;
    m_eEngineState=AMFAPS_UNKNOWN_STATUS;

    return err;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterWin::InitDevice()
{
    AMF_RESULT err=AMF_OK;

    HRESULT hr=S_OK;
    IMMDeviceEnumeratorPtr pDeviceEnumerator;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDeviceEnumerator));
    CHECK_HR(hr);

    IMMDevice *pDevice = NULL;
    hr= pDeviceEnumerator->GetDefaultAudioEndpoint(eRender,eMultimedia,&pDevice);
    CHECK_HR(hr);
    m_pDevice=pDevice;

    //
    //  Now activate an IAudioClient object on our preferred endpoint and retrieve the mix format for that endpoint.
    //
    IAudioClient *pAudioClient=NULL;
    hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, NULL, reinterpret_cast<void **>(&pAudioClient));
    CHECK_HR(hr);

    m_pAudioClient=pAudioClient;


    return err;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterWin::SelectFormat()
{
    AMF_RETURN_IF_FALSE(m_pAudioClient!=NULL,AMF_NOT_INITIALIZED, L"SelectFormat() - Audio Client not Initialized");

    HRESULT hr=S_OK;

    IAudioClient *pAudioClient=(IAudioClient *)m_pAudioClient;

    WAVEFORMATEX *pMixFormat;
    hr = pAudioClient->GetMixFormat(&pMixFormat);
    CHECK_HR(hr);

    assert(pMixFormat != NULL);
    m_pMixFormat=pMixFormat;

    //MM: need to set it properly
    pMixFormat->wFormatTag = WAVE_FORMAT_PCM;
    pMixFormat->cbSize=0;

    WAVEFORMATEX *pMixFormatSuggested=NULL;
    hr = pAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,pMixFormat,&pMixFormatSuggested);
    CHECK_HR(hr);
    if(pMixFormatSuggested!=NULL){
        CoTaskMemFree(m_pMixFormat);
        m_pMixFormat=pMixFormatSuggested;
    }


    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
#define REFTIMES_PER_SEC  10000000
#define LOW_LATENCY_DIVIDER  20
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterWin::InitClient()
{
    AMF_RETURN_IF_FALSE(m_pAudioClient!=NULL,AMF_NOT_INITIALIZED, L"InitClient() - Audio Client not Initialized");

    HRESULT hr=S_OK;

    REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC*3;
    if(m_bLowLatency)
    {
        hnsRequestedDuration = REFTIMES_PER_SEC / LOW_LATENCY_DIVIDER;
        hnsRequestedDuration = ((hnsRequestedDuration + (64 - 1)) & ~(64 - 1));
    }
    WAVEFORMATEX *pMixFormat=(WAVEFORMATEX *)m_pMixFormat;
    IAudioClient *pAudioClient=(IAudioClient *)m_pAudioClient;
    hr = pAudioClient->Initialize(
                         AUDCLNT_SHAREMODE_SHARED,
//                        AUDCLNT_SHAREMODE_EXCLUSIVE,
                         0,
                         hnsRequestedDuration,
                         0,
                         pMixFormat,
                         NULL);
    CHECK_HR(hr);

    IAudioRenderClient *pRenderClient = NULL;
    hr = pAudioClient->GetService(__uuidof(IAudioRenderClient),(void**)&pRenderClient);
    CHECK_HR(hr);
    m_pRenderClient=pRenderClient;

    hr = pAudioClient->GetBufferSize(&m_iBufferSampleSize);
    CHECK_HR(hr);

    m_iBufferDuration=(amf_pts)m_iBufferSampleSize*AMF_SECOND/pMixFormat->nSamplesPerSec;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterWin::SubmitInput(amf::AMFData* pData)
{
    amf::AMFLock lock(&m_cs);
    m_uiNumSamplesWritten = 0;
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
    AMF_RESULT err = AMF_OK;

    if(m_pAVSync != NULL && !m_pAVSync->IsVideoStarted())
    {
        if(m_iAVSyncDelay == 1LL)
        {
            m_iAVSyncDelay = amf_high_precision_clock();;
        }
        return AMF_INPUT_FULL;
    }

    // there's not much to do in the base class
    amf::AMFAudioBufferPtr pAudioBuffer(pData);

    amf_pts  ptsSleepTime = 0;
    err = Present(pAudioBuffer, ptsSleepTime);

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
AMF_RESULT AudioPresenterWin::Present(AMFAudioBuffer *buffer,amf_pts &sleeptime)
{
    AMF_RETURN_IF_FALSE(m_pRenderClient!=NULL && m_pAudioClient!=NULL,AMF_NOT_INITIALIZED, L"Present() - Audio Client");
    HRESULT hr = S_OK;
    AMF_RESULT err = AMF_OK;

    AMFAudioBufferPtr localBuffer;

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

    IAudioClient *pAudioClient=(IAudioClient *)m_pAudioClient;
    IAudioRenderClient *pRenderClient = (IAudioRenderClient *)m_pRenderClient;
    WAVEFORMATEX *pMixFormat=(WAVEFORMATEX *)m_pMixFormat;

    if(m_eEngineState==AMFAPS_STOPPED_STATUS)
    {
        hr = pAudioClient->Start();  // Start playing.
        CHECK_HR(hr);
        m_eEngineState = AMFAPS_PLAYING_STATUS;
    }
    // process leftover
    if((m_UnusedBuffers.size() < 10 && !m_bLowLatency) || (m_UnusedBuffers.size() < 1 && m_bLowLatency))
    {
        m_UnusedBuffers.push_back(buffer); // add to the end
#if TRACE_AMBISONIC_STAT
        amf_pts ambiStart;
        amf_pts ambiExec;
        amf_pts ambiPeriod;
        buffer->GetProperty(L"AmbiStart", &ambiStart);
        buffer->GetProperty(L"AmbiExec", &ambiExec);
        buffer->GetProperty(L"AmbiPeriod", &ambiPeriod);
        

        amf_pts currTime = amf_high_precision_clock();
        
        AMFTraceWarning(AMF_FACILITY, L"AmbiLatency=%5.2f AmbiExec=%5.2f AmbiPeriod=%5.2f", (double)(currTime - ambiStart) / 10000, (double)ambiExec / 10000,(double)ambiPeriod / 10000);
#endif
    }
    else
    { 
        err = AMF_INPUT_FULL;
    }
    while(m_UnusedBuffers.size()>0)
    {
        AMFAudioBufferPtr pLastBuffer=m_UnusedBuffers.front();
        AMF_RESULT errPush = PushBuffer(pLastBuffer,m_uiLastBufferDataOffset); // uiBufferDataOffset - in/out
        if(errPush != AMF_OK)
        {
            AMFTraceWarning(AMF_FACILITY, L"PushBuffer() failed");
        }
        amf_size uiBufMemSize=pLastBuffer->GetSize();
        if(m_uiLastBufferDataOffset>=uiBufMemSize)
        { // buffer used completely - discard
            m_UnusedBuffers.erase(m_UnusedBuffers.begin());
            m_uiLastBufferDataOffset=0;
        }
        else
        { // buffer has unused data - keep the new buffer is kept in the list: just exit
            break;
        }
    }
    // define delay
    UINT32 iSamplesInBuffer=0;
    hr = pAudioClient->GetCurrentPadding(&iSamplesInBuffer);
    if(iSamplesInBuffer < m_iBufferSampleSize / 4)
    {
        AMFTraceWarning(AMF_FACILITY, L"Buffer is too empty = %d", iSamplesInBuffer);
    }

    sleeptime=0;
    if(m_eEngineState==AMFAPS_PLAYING_STATUS)
    {
        if(m_bLowLatency)
        {
            sleeptime = 10000; // 1 ms
        }
        else if(iSamplesInBuffer > m_iBufferSampleSize* 2 / 3)
        { // 2/3
            sleeptime = (amf_pts)iSamplesInBuffer / 2 * AMF_SECOND / pMixFormat->nSamplesPerSec;
        }
        else
        {
            sleeptime = 10000; // 1 ms
        }
    }
    return err;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterWin::PushBuffer(AMFAudioBufferPtr &buffer,amf_size &uiBufferDataOffset)
{
    AMF_RETURN_IF_FALSE(m_pRenderClient!=NULL && m_pAudioClient!=NULL,AMF_NOT_INITIALIZED, L"PushBuffer() - Render Client or Audio Client not Initialized");
    HRESULT hr=S_OK;
    AMF_RESULT err=AMF_OK;

    IAudioClient *pAudioClient=(IAudioClient *)m_pAudioClient;
    IAudioRenderClient *pRenderClient = (IAudioRenderClient *)m_pRenderClient;
    WAVEFORMATEX *pMixFormat=(WAVEFORMATEX *)m_pMixFormat;

    // get DXSound buffer data
    UINT32 numSamplesPadding=0;
    hr = pAudioClient->GetCurrentPadding(&numSamplesPadding);
    CHECK_HR(hr);


    UINT32 numSamplesAvailable = m_iBufferSampleSize - numSamplesPadding;
    if(numSamplesAvailable==0)
    { 
        return AMF_OK; // buffer full
    }
    amf_uint8 *pOutputData=NULL;
    hr = pRenderClient->GetBuffer(numSamplesAvailable, &pOutputData);
    CHECK_HR(hr);
    amf_size numBytesAvailable=(amf_size)numSamplesAvailable*pMixFormat->nChannels*(pMixFormat->wBitsPerSample/8);

    // get our buffer data
    amf_uint8* pInputData = static_cast<amf_uint8*>(buffer->GetNative());
    amf_size uiBufMemSize=buffer->GetSize();

    // copy what we can
    amf_size to_copy=AMF_MIN(numBytesAvailable,uiBufMemSize-uiBufferDataOffset);
    memcpy(pOutputData,pInputData+uiBufferDataOffset,to_copy);
    uiBufferDataOffset+=to_copy; // for next call use 

    // release thir buffer
    UINT32 numSamplesWritten=(UINT32)(to_copy/pMixFormat->nChannels/(pMixFormat->wBitsPerSample/8));
    hr = pRenderClient->ReleaseBuffer(numSamplesWritten, 0);
    m_uiNumSamplesWritten = numSamplesWritten;
    CHECK_HR(hr);

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterWin::Pause()
{
    amf::AMFLock lock(&m_cs);
    AMF_RETURN_IF_FALSE(m_pRenderClient!=NULL && m_pAudioClient!=NULL,AMF_NOT_INITIALIZED, L"Pause() - Render Client or AudioClient not Initialized");
    HRESULT hr=S_OK;
    AMF_RESULT err=AMF_OK;

    IAudioClient *pAudioClient=(IAudioClient *)m_pAudioClient;
    if(m_eEngineState==AMFAPS_PLAYING_STATUS){
        hr=pAudioClient->Stop();
        m_eEngineState=AMFAPS_PAUSED_STATUS;
    }
    return err;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterWin::Resume(amf_pts currentTime)
{
    amf::AMFLock lock(&m_cs);
    AMF_RETURN_IF_FALSE(m_pRenderClient!=NULL && m_pAudioClient!=NULL,AMF_NOT_INITIALIZED, L"Resume() - Render Client or Audio Client not Initialized");
    HRESULT hr=S_OK;
    AMF_RESULT err=AMF_OK;

    IAudioClient *pAudioClient=(IAudioClient *)m_pAudioClient;
    if(m_eEngineState==AMFAPS_PAUSED_STATUS)
    {
        Reset();
        Seek(currentTime);
    }
    return err;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterWin::Step()
{
    amf::AMFLock lock(&m_cs);
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_AUDIO_PLAYBACK_STATUS AudioPresenterWin::GetStatus()
{
    amf::AMFLock lock(&m_cs);
    return m_eEngineState;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterWin::Reset()
{
    amf::AMFLock lock(&m_cs);

    m_UnusedBuffers.clear();
    IAudioClient *pAudioClient=(IAudioClient *)m_pAudioClient;
    pAudioClient->Stop();
    pAudioClient->Reset();
    pAudioClient->Start();
    m_uiLastBufferDataOffset = 0;
    m_eEngineState = AMFAPS_PLAYING_STATUS;
    m_iAVSyncDelay = -1LL;
    
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterWin::GetDescription(
    amf_int64 &streamBitRate,
    amf_int64 &streamSampleRate,
    amf_int64 &streamChannels,
    amf_int64 &streamFormat,
    amf_int64 &streamLayout,
    amf_int64 &streamBlockAlign
) const
{
    amf::AMFLock lock(&m_cs);

    if(m_pMixFormat == nullptr)
    {
        return AMF_NOT_INITIALIZED;
    }
    WAVEFORMATEX *pMixFormat=(WAVEFORMATEX *)m_pMixFormat;

    streamBitRate = (amf_int64)pMixFormat->wBitsPerSample * pMixFormat->nSamplesPerSec * pMixFormat->nChannels;

    // audio properties
    streamSampleRate = pMixFormat->nSamplesPerSec;
    streamChannels = pMixFormat->nChannels;
    switch(pMixFormat->wBitsPerSample)
    {
    case 8: 
        streamFormat = AMFAF_U8;
        break;
    case 16: 
        streamFormat = AMFAF_S16;
        break;
    case 32: 
        streamFormat = AMFAF_S32;
        break;
    default: 
        streamFormat = AMFAF_S16;
        break;

    }
    streamLayout = 3;
    streamBlockAlign = pMixFormat->nBlockAlign;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterWin::Flush()
{
    amf::AMFLock lock(&m_cs);
    Reset();
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
#endif //#if defined(_WIN32)
