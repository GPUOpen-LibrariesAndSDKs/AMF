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



AudioPresenterWin::AudioPresenterWin(amf::AMFContext* pContext) 
  : AudioPresenter(pContext),
    m_pDevice(NULL),
    m_pAudioClient(NULL),
    m_pMixFormat(NULL),
    m_pRenderClient(NULL),
    m_iBufferDuration(0),
    m_iBufferSampleSize(0),
    m_uiLastBufferDataOffset(0),
    m_eEngineState(AMFAPS_UNKNOWN_STATUS)
{
}
//-------------------------------------------------------------------------------------------------
AudioPresenterWin::~AudioPresenterWin()
{
    Terminate();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenterWin::SubmitInput(amf::AMFData* pData)
{
    // get the base class to do processing first and
    // see that it succeeds
    AMF_RESULT err = AudioPresenter::SubmitInput(pData);
    if (err != AMF_OK)
    {
        return err;
    }

    amf_pts  ptsSleepTime = 0;
    err = Present(m_pLastData, ptsSleepTime);
    if(ptsSleepTime > 0)
    {
        amf_sleep(amf_long(ptsSleepTime / 10000)); // to milliseconds
    }
    m_pLastData.Release();
    return err;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AudioPresenterWin::Init()
{
    if(m_eEngineState!=AMFAPS_UNKNOWN_STATUS)
    {
        return AMF_OK;
    }
    AMF_RESULT err=AMF_OK;
    err=InitDevice();
    AMF_RETURN_IF_FAILED(err,L"Init() - InitDevice failed");
    err=SelectFormat();
    AMF_RETURN_IF_FAILED(err,L"Init() - SelectFormat failed");
    err=InitClient();
    AMF_RETURN_IF_FAILED(err,L"Init() - InitClient failed");

    m_eEngineState=AMFAPS_STOPPED_STATUS;
    return err;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AudioPresenterWin::Terminate()
{
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
AMF_RESULT AMF_STD_CALL  AudioPresenterWin::InitDevice()
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
AMF_RESULT AMF_STD_CALL  AudioPresenterWin::SelectFormat()
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
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AudioPresenterWin::InitClient()
{
    AMF_RETURN_IF_FALSE(m_pAudioClient!=NULL,AMF_NOT_INITIALIZED, L"InitClient() - Audio Client not Initialized");

    HRESULT hr=S_OK;

    REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC*3;

    WAVEFORMATEX *pMixFormat=(WAVEFORMATEX *)m_pMixFormat;
    IAudioClient *pAudioClient=(IAudioClient *)m_pAudioClient;
    hr = pAudioClient->Initialize(
                         AUDCLNT_SHAREMODE_SHARED,
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
AMF_RESULT AMF_STD_CALL  AudioPresenterWin::Present(AMFAudioBuffer *buffer,amf_pts &sleeptime)
{
    AMF_RETURN_IF_FALSE(m_pRenderClient!=NULL && m_pAudioClient!=NULL,AMF_NOT_INITIALIZED, L"Present() - Audio Client");
    HRESULT hr=S_OK;
    AMF_RESULT err=AMF_OK;

    AMF_RETURN_IF_FALSE(m_eEngineState==AMFAPS_STOPPED_STATUS || m_eEngineState==AMFAPS_PLAYING_STATUS || m_eEngineState==AMFAPS_PAUSED_STATUS,AMF_WRONG_STATE, L"Present() - Wrong State");
    if(m_eEngineState==AMFAPS_PAUSED_STATUS)
    {
        return err;
    }

    IAudioClient *pAudioClient=(IAudioClient *)m_pAudioClient;
    IAudioRenderClient *pRenderClient = (IAudioRenderClient *)m_pRenderClient;
    WAVEFORMATEX *pMixFormat=(WAVEFORMATEX *)m_pMixFormat;

    if(m_eEngineState==AMFAPS_STOPPED_STATUS)
    {
        hr = pAudioClient->Start();  // Start playing.
        CHECK_HR(hr);
        m_eEngineState=AMFAPS_PLAYING_STATUS;
    }
    // process leftover
    if(m_UnusedBuffers.size() < 10)
    {
        m_UnusedBuffers.push_back(buffer); // add to the end
    }
    else
    { 
        err = AMF_INPUT_FULL;
    }
    while(m_UnusedBuffers.size()>0)
    {
        AMFAudioBufferPtr pLastBuffer=m_UnusedBuffers.front();
        PushBuffer(pLastBuffer,m_uiLastBufferDataOffset); // uiBufferDataOffset - in/out
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

    sleeptime=0;
    if(m_eEngineState==AMFAPS_PLAYING_STATUS)
    {
        if(iSamplesInBuffer > m_iBufferSampleSize* 2 /3)
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
AMF_RESULT AMF_STD_CALL  AudioPresenterWin::PushBuffer(AMFAudioBufferPtr &buffer,amf_size &uiBufferDataOffset)
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
    CHECK_HR(hr);

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AudioPresenterWin::Pause()
{
    AMF_RETURN_IF_FALSE(m_pRenderClient!=NULL && m_pAudioClient!=NULL,AMF_NOT_INITIALIZED, L"Pause() - Render Client ot AudioClient not Initialized");
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
AMF_RESULT AMF_STD_CALL  AudioPresenterWin::Resume()
{
    AMF_RETURN_IF_FALSE(m_pRenderClient!=NULL && m_pAudioClient!=NULL,AMF_NOT_INITIALIZED, L"Resume() - Render Client or Audio Client not Initialized");
    HRESULT hr=S_OK;
    AMF_RESULT err=AMF_OK;

    IAudioClient *pAudioClient=(IAudioClient *)m_pAudioClient;
    if(m_eEngineState==AMFAPS_PAUSED_STATUS){
        hr=pAudioClient->Start();
        m_eEngineState=AMFAPS_PLAYING_STATUS;
    }
    return err;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AudioPresenterWin::Step()
{
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_AUDIO_PLAYBACK_STATUS AMF_STD_CALL  AudioPresenterWin::GetStatus()
{
    return m_eEngineState;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AudioPresenterWin::Reset()
{
    m_UnusedBuffers.clear();
    IAudioClient *pAudioClient=(IAudioClient *)m_pAudioClient;
    pAudioClient->Stop();
    pAudioClient->Reset();
    pAudioClient->Start();
    m_eEngineState=AMFAPS_PLAYING_STATUS;
    
    return AMF_OK;
}

AMF_RESULT AudioPresenterWin::GetDescription(
    amf_int64 &streamBitRate,
    amf_int64 &streamSampleRate,
    amf_int64 &streamChannels,
    amf_int64 &streamFormat,
    amf_int64 &streamLayout,
    amf_int64 &streamBlockAlign
    )
{
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

#endif //#if defined(_WIN32)
