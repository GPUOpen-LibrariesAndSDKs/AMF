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
#include <iostream>
#include <iosfwd>
#include <vector>

#include "AudioCaptureImplLinux.h"
#include "PulseAudioSimpleAPISourceFacade.h"

#include "../../../include/core/Context.h"
#include "../../../include/core/Trace.h"
#include "../../../common/TraceAdapter.h"
#include "../../../common/AMFFactory.h"
#include "../../../common/DataStream.h"

#define AMF_FACILITY L"AMFAudioCaptureImplLinux"

using namespace amf;

// Defines
#define  AV_CODEC_ID_PCM_F32LE 65557

#define BUFFER_DURATION 500000 //500 ms

extern "C"
{
    //-------------------------------------------------------------------------------------------------
    AMF_RESULT AMFCreateComponentAudioCapture(amf::AMFContext* pContext, amf::AMFComponent** ppComponent)
    {
        *ppComponent = new amf::AMFInterfaceMultiImpl< amf::AMFAudioCaptureImpl, amf::AMFComponent, amf::AMFContext*>(pContext);
        (*ppComponent)->Acquire();
        return AMF_OK;
    }
}

// AMFAudioCaptureImpl
//-------------------------------------------------------------------------------------------------
AMFAudioCaptureImpl::AMFAudioCaptureImpl(AMFContext* pContext) :
    m_pContext(pContext),
    m_audioPollingThread(this)
{
    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoInt64(AUDIOCAPTURE_DEVICE_COUNT, AUDIOCAPTURE_DEVICE_COUNT, m_deviceCount, 0, 8, false),
        AMFPropertyInfoWString(AUDIOCAPTURE_DEVICE_NAME, AUDIOCAPTURE_DEVICE_NAME, L"", false),
        AMFPropertyInfoInt64(AUDIOCAPTURE_DEVICE_ACTIVE, AUDIOCAPTURE_DEVICE_ACTIVE, m_deviceActive, -1, 1024, false),

        AMFPropertyInfoInt64(AUDIOCAPTURE_SAMPLERATE, AUDIOCAPTURE_SAMPLERATE, 44100, 0, 100000000, false),
        AMFPropertyInfoInt64(AUDIOCAPTURE_SAMPLES, AUDIOCAPTURE_SAMPLES, 1024, 0, 10240, false),
        AMFPropertyInfoInt64(AUDIOCAPTURE_CHANNELS, AUDIOCAPTURE_CHANNELS, 2, 1, 16, false),
        AMFPropertyInfoInt64(AUDIOCAPTURE_CHANNEL_LAYOUT, AUDIOCAPTURE_CHANNEL_LAYOUT, 3, 0, 0xffffffff, false),
        AMFPropertyInfoInt64(AUDIOCAPTURE_FORMAT, AUDIOCAPTURE_FORMAT, AMFAF_U8, AMFAF_UNKNOWN, AMFAF_LAST, false),
        AMFPropertyInfoInt64(AUDIOCAPTURE_BLOCKALIGN, AUDIOCAPTURE_BLOCKALIGN, 0, 0, -1, false),
        AMFPropertyInfoInt64(AUDIOCAPTURE_FRAMESIZE, AUDIOCAPTURE_FRAMESIZE, 0, 0, -1, false),
        AMFPropertyInfoInt64(AUDIOCAPTURE_LOWLATENCY, AUDIOCAPTURE_LOWLATENCY, 1, 0, 1, false),
        AMFPropertyInfoInt64(AUDIOCAPTURE_CODEC, AUDIOCAPTURE_CODEC, 0, 0, INT_MAX, false),
        AMFPropertyInfoInt64(AUDIOCAPTURE_BITRATE, AUDIOCAPTURE_BITRATE, 0, 0, INT_MAX, false),
        AMFPropertyInfoBool(AUDIOCAPTURE_SOURCE, AUDIOCAPTURE_SOURCE, true, false),
        AMFPropertyInfoInterface(AUDIOCAPTURE_CURRENT_TIME_INTERFACE, L"Interface object for getting current time", NULL, false),
    AMFPrimitivePropertyInfoMapEnd

    // Audio queue
    m_AudioDataQueue.SetQueueSize(m_iQueueSize);
}

//-------------------------------------------------------------------------------------------------
AMFAudioCaptureImpl::~AMFAudioCaptureImpl()
{
    m_pCurrentTime.Release();
    Terminate();
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioCaptureImpl::Init(AMF_SURFACE_FORMAT /*format*/, amf_int32 /*width*/, amf_int32 /*height*/)
{
    AMF_RESULT res = AMF_OK;
    if (!m_bTerminated)
    {
        Terminate();
    }

    AMFTraceInfo(AMF_FACILITY, L"Init()");

    //  At first will get a number -1.
    amf_int64 deviceActive = 0;
    GetProperty(AUDIOCAPTURE_DEVICE_ACTIVE, &deviceActive);
    m_deviceActive = (amf_int32)deviceActive;

    // Determine if we are capturing display or mic, true is mic.
    GetProperty(AUDIOCAPTURE_SOURCE, &m_captureMic);

    // Get the current time interface property if it has been set.
    AMFInterfacePtr pTmp;
    GetProperty(AUDIOCAPTURE_CURRENT_TIME_INTERFACE, &pTmp);
    m_pCurrentTime = (AMFCurrentTimePtr)pTmp.GetPtr();
    AMF_RETURN_IF_INVALID_POINTER(m_pCurrentTime);

    // Audio stream should be null.
    AMF_RETURN_IF_FALSE(NULL == m_pAMFDataStreamAudio, AMF_FAIL, L"Audio stream already initialized");

    // Init audio stream.
    m_pAMFDataStreamAudio = AMFPulseAudioSimpleAPISourceImplPtr(new AMFPulseAudioSimpleAPISourceFacade);
    AMF_RETURN_IF_INVALID_POINTER(m_pAMFDataStreamAudio);

    res = m_pAMFDataStreamAudio->Init(m_captureMic);
    AMF_RETURN_IF_FAILED(res,L"Audio stream Init() failed");

    SetProperty(AUDIOCAPTURE_CODEC, AV_CODEC_ID_PCM_F32LE);
    SetProperty(AUDIOCAPTURE_SAMPLERATE, m_pAMFDataStreamAudio->GetSampleRate());
    SetProperty(AUDIOCAPTURE_BITRATE, m_pAMFDataStreamAudio->GetSampleRate()*m_pAMFDataStreamAudio->GetChannelCount()*sizeof(short));
    SetProperty(AUDIOCAPTURE_SAMPLES, m_pAMFDataStreamAudio->GetSampleCount());


    SetProperty(AUDIOCAPTURE_CHANNELS, m_pAMFDataStreamAudio->GetChannelCount()); // By default 2 channels
    SetProperty(AUDIOCAPTURE_CHANNEL_LAYOUT, GetDefaultChannelLayout(m_pAMFDataStreamAudio->GetChannelCount()));
    SetProperty(AUDIOCAPTURE_FORMAT, m_pAMFDataStreamAudio->GetFormat()); // Signed 16 bit
    SetProperty(AUDIOCAPTURE_BLOCKALIGN, m_pAMFDataStreamAudio->GetBlockAlign()); // Bytes per sample, 2 bytes
    SetProperty(AUDIOCAPTURE_FRAMESIZE, m_pAMFDataStreamAudio->GetFrameSize()); // 2 Block Aligned.

    // Set Device name and count
    if (m_deviceActive < 0)
    {
        std::vector<amf_string> srcList = m_pAMFDataStreamAudio->GetSourceList(m_captureMic);

        // Produce a \t seperated string fro device list
        amf_string nameList("");
        for (size_t idx = 0; idx < srcList.size(); idx ++)
        {
            nameList += (idx == 0)? srcList[idx] : "\t" + srcList[idx];
        }

        SetProperty(AUDIOCAPTURE_DEVICE_NAME, nameList.c_str());
        SetProperty(AUDIOCAPTURE_DEVICE_COUNT, srcList.size());
    } else
    {
        m_audioPollingThread.Start();
    }

    m_iSamplesFromStream = 0xFFFFFFFFFFFFFFFFLL;
    m_DiffsAcc = 0;
    m_StatCount = 0;


    m_bTerminated = false;
    m_bShouldReInit = false;
    return res;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioCaptureImpl::ReInit(amf_int32 width, amf_int32 height)
{
    Terminate();
    return Init(AMF_SURFACE_UNKNOWN, width, height);
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioCaptureImpl::Terminate()
{

    AMFTraceInfo(AMF_FACILITY, L"Terminate()");
    Drain();

    m_audioPollingThread.RequestStop();
    m_audioPollingThread.WaitForStop();

    AMFLock lock(&m_sync);

    m_frameCount = 0;
    m_bForceEof = false;

    if (m_pAMFDataStreamAudio)
    {
        m_pAMFDataStreamAudio->Terminate();
        m_pAMFDataStreamAudio.reset();
    }

    m_bTerminated = true;

    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioCaptureImpl::Drain()
{
    AMFLock lock(&m_sync);
    m_bForceEof = true;
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioCaptureImpl::Flush()
{
    AMFLock lock(&m_sync);
    m_AudioDataQueue.Clear();
    m_frameCount = 0;
    m_bFlush = true;
    m_CurrentPts = 0;
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL  AMFAudioCaptureImpl::SubmitInput(AMFData* /*pData*/)
{
    return AMF_NOT_IMPLEMENTED;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioCaptureImpl::QueryOutput(AMFData** ppData)
{
    AMFLock lock(&m_sync);

    if (m_bShouldReInit)
    {
        ReInit(0, 0);
        return AMF_INVALID_FORMAT;
    }

    AMF_RESULT  res = AMF_REPEAT;
    AMFDataPtr  pFrame;
    amf_ulong   ulID = 0;

    if (m_AudioDataQueue.Get(ulID, pFrame, 0))
    {
        *ppData = pFrame.Detach();
        res = AMF_OK;
    }
    return res;
}

//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL  AMFAudioCaptureImpl::OnPropertyChanged(const wchar_t* pName)
{
    AMFLock lock(&m_sync);
    amf_wstring name(pName);
}

//-------------------------------------------------------------------------------------------------
amf_pts AMFAudioCaptureImpl::GetCurrentPts() const
{
    amf_pts result = 0;
    if (m_pCurrentTime)
    {
        return m_pCurrentTime->Get();
    }
    else
    {
        result = amf_high_precision_clock();
    }
    return result;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFAudioCaptureImpl::PollStream()
{
    AMF_RESULT res = AMF_OK; // error checking can be added later

    amf_uint32 capturedSamples = 0;
    AMFAudioBufferPtr pAudioBuffer;

    {
        AMFLock lock(&m_sync);
        // m_pContext should not be nullptr.
        AMF_RETURN_IF_FALSE(m_pContext != nullptr, AMF_FAIL, L"AMFAudioCaptureImpl::PollStream(): AMF context is NULL");

        // Allocates memory for pAudioBuffer and capture audio directly into it.
        res = m_pAMFDataStreamAudio->CaptureAudio(pAudioBuffer, m_pContext, capturedSamples);
        AMF_RETURN_IF_FALSE(pAudioBuffer!=nullptr, AMF_FAIL, L"CaptureAudio failed! pAudioBuffer is nullptr!");
        AMF_RETURN_IF_FAILED(res, L"CaptureAudio failed!");

        m_iSamplesFromStream += capturedSamples;
        amf_pts duration = capturedSamples * AMF_SECOND / m_pAMFDataStreamAudio->GetSampleRate();

        // If it's the frist time we capture, use the "real" time as current pts. For the rest we add the sample duration
        // calculated from sample amount and sample rate.
        if (0 == m_CurrentPts)
        {
            m_CurrentPts = GetCurrentPts() - duration;
        }
        pAudioBuffer->SetPts(m_CurrentPts);
        pAudioBuffer->SetDuration(duration);
        m_CurrentPts += duration;

        if (m_bFlush)
        {
            m_bFlush = false;
            return res;
        }
    }

    // Add the captured audio to AMFQueue.
    if (pAudioBuffer != nullptr)
    {

        m_DiffsAcc += pAudioBuffer->GetPts() - GetCurrentPts();
        m_StatCount++;
        if (m_StatCount == 50)
        {

            if (m_DiffsAcc / m_StatCount > AMF_MILLISECOND * 32)
            {
                AMFTraceDebug(AMF_FACILITY, L"desync between video and audio = %5.2f", m_DiffsAcc / m_StatCount / 10000.);
                m_CurrentPts = GetCurrentPts();
            }

            m_DiffsAcc = 0;
            m_StatCount = 0;
        }

        AMFTraceDebug(AMF_FACILITY, L"Processing in_pts=%5.2f duration =%5.2f", pAudioBuffer->GetPts() / 10000., pAudioBuffer->GetDuration() / 10000.);

        while (m_audioPollingThread.StopRequested() == false)
        {
            {
                AMFLock lock(&m_sync);
                if (m_bFlush)
                {
                    m_bFlush = false;
                    break;
                }
            }
            // Add the captured audio into data queue. AMF queue is thread safe, timeout set to 1 (not infinite).
            // If data was successfully added, break the while loop to capture next frame.
            if (m_AudioDataQueue.Add(0, static_cast<AMFData*>(pAudioBuffer), 0, 1))
            {
                break;
            }
        }
        m_frameCount++;
    }
    return res;
}

//-------------------------------------------------------------------------------------------------
void AMFAudioCaptureImpl::AudioCapturePollingThread::Run()
{
    AMF_RESULT res = AMF_OK; // error checking can be added later
    while (true)
    {
        res = m_pHost->PollStream();
        if (res == AMF_EOF || m_pHost->m_bForceEof)
        {
            break; // Drain complete
        }
        // this return value means capture device has changed
        if (res == AMF_NOT_INITIALIZED)
        {
            m_pHost->m_bShouldReInit = true;
            break;
        }
        if (StopRequested())
        {
            break;
        }
    }
}

// AMFAudioCaptureImpl::AudioCapturePollingThread::
//-------------------------------------------------------------------------------------------------
AMFAudioCaptureImpl::AudioCapturePollingThread::AudioCapturePollingThread(AMFAudioCaptureImpl *pHost) :
m_pHost(pHost)
{
}

//-------------------------------------------------------------------------------------------------
AMFAudioCaptureImpl::AudioCapturePollingThread::~AudioCapturePollingThread()
{
    m_pHost = NULL;
}
