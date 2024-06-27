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
#include <stdio.h>
#include <fstream>
#include <iosfwd>
#include "AudioCaptureImpl.h"

#include "../../../include/core/Context.h"
#include "../../../include/core/Trace.h"
#include "../../../common/TraceAdapter.h"
#include "../../../common/AMFFactory.h"
#include "../../../common/DataStream.h"
#include "public/common/CurrentTimeImpl.h"

#define AMF_FACILITY L"AMFAudioCaptureImpl"

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
    m_audioPollingThread(this),
    m_pAMFDataStreamAudio(),
    m_bForceEof(false),
    m_bTerminated(false),
    m_bShouldReInit(false),
    m_frameCount(0),
    m_deviceCount(0),
    m_deviceActive(0),
    m_pCurrentTime(),
    m_captureDevice(false),
    m_iQueueSize(10),
    m_prevPts(0),
    m_bFlush(false),
    m_CurrentPts(0),
    m_iSamplesFromStream((amf_uint64)0xFFFFFFFFFFFFFFFFLL),
    m_FirstSample(true),
    m_DiffsAcc(0),
    m_StatCount(0)
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
	//
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

    AMFLock lock(&m_sync);

    AMFTraceInfo(AMF_FACILITY, L"Init()");

	amf_int64 deviceActive = 0;
	GetProperty(AUDIOCAPTURE_DEVICE_ACTIVE, &deviceActive);
	m_deviceActive = (amf_int32)deviceActive;

	// Determine if we are using a capture or render audio device
	GetProperty(AUDIOCAPTURE_SOURCE, &m_captureDevice);

	// Get the current time interface property if it has been set
	AMFInterfacePtr pTmp;
	GetProperty(AUDIOCAPTURE_CURRENT_TIME_INTERFACE, &pTmp);
	m_pCurrentTime = (AMFCurrentTimePtr)pTmp.GetPtr();
	if (m_pCurrentTime == nullptr)
	{
		m_pCurrentTime = new amf::AMFCurrentTimeImpl();
	}
	AMF_RETURN_IF_INVALID_POINTER(m_pCurrentTime);

	// Audio stream should be null
	AMF_RETURN_IF_FALSE(NULL == m_pAMFDataStreamAudio, AMF_FAIL, L"Audio stream already initialized");

	// init device
	m_pAMFDataStreamAudio = new AMFWASAPISourceImpl();
	AMF_RETURN_IF_INVALID_POINTER(m_pAMFDataStreamAudio);

	res = m_pAMFDataStreamAudio->Init(m_captureDevice, m_deviceActive, BUFFER_DURATION);
	AMF_RETURN_IF_FAILED(res, L"Audio stream Init() failed");

	WAVEFORMATEX* pWaveFormat = m_pAMFDataStreamAudio->GetWaveFormat();
	SetProperty(AUDIOCAPTURE_CODEC, AV_CODEC_ID_PCM_F32LE);
	SetProperty(AUDIOCAPTURE_SAMPLERATE, (amf_int32)pWaveFormat->nSamplesPerSec);
	SetProperty(AUDIOCAPTURE_BITRATE, (amf_int32)pWaveFormat->nAvgBytesPerSec * 8);
	SetProperty(AUDIOCAPTURE_SAMPLES, (amf_int32)m_pAMFDataStreamAudio->GetSampleCount());

    int channels = pWaveFormat->nChannels;
    int channelLayout = GetDefaultChannelLayout(channels);

    amf_int64 format = AMFAF_UNKNOWN;
	bool bCaptureExtensibleFormat = false;
    bool isPCM = false;
    switch (pWaveFormat->wFormatTag)
    {
    case WAVE_FORMAT_PCM:
        isPCM = true;
        break;
    case WAVE_FORMAT_IEEE_FLOAT:
        format = AMFAF_FLT;
        break;
    case WAVE_FORMAT_EXTENSIBLE:
    {
		bCaptureExtensibleFormat = true;
        WAVEFORMATEXTENSIBLE *pWaveFormatEx = (WAVEFORMATEXTENSIBLE *)pWaveFormat;
        if (IsEqualGUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, pWaveFormatEx->SubFormat))
        {
            format = AMFAF_FLT;
        }
        else if (IsEqualGUID(KSDATAFORMAT_SUBTYPE_PCM, pWaveFormatEx->SubFormat))
        {
            isPCM = true;
        }
        if (pWaveFormatEx->dwChannelMask != 0)
        {
            channelLayout = pWaveFormatEx->dwChannelMask;
        }
        break;
    }
    }

    if (isPCM)
    {
        switch (pWaveFormat->wBitsPerSample)
        {
        case 8:
            format = AMFAF_U8;
            break;
        case 16:
            format = AMFAF_S16;
            break;
        case 32:
            format = AMFAF_S32;
            break;
        }
    }

    SetProperty(AUDIOCAPTURE_CHANNELS, channels);
    SetProperty(AUDIOCAPTURE_CHANNEL_LAYOUT, channelLayout);
	SetProperty(AUDIOCAPTURE_FORMAT, format);
	SetProperty(AUDIOCAPTURE_BLOCKALIGN, pWaveFormat->nBlockAlign);
	SetProperty(AUDIOCAPTURE_FRAMESIZE, (amf_int32)m_pAMFDataStreamAudio->GetFrameSize());

	if (m_deviceActive < 0) //query device list
	{
		amf_vector<amf_string> deviceList = m_pAMFDataStreamAudio->GetDeviceList();
		amf_string nameList("");
		for (UINT idx = 0; idx < deviceList.size(); idx++)
		{
			nameList += (idx == 0) ? deviceList[idx] : "\t" + deviceList[idx];
		}
		SetProperty(AUDIOCAPTURE_DEVICE_NAME, nameList.c_str());
		SetProperty(AUDIOCAPTURE_DEVICE_COUNT, deviceList.size());
	}
	else
	{
		m_CurrentPts = GetCurrentPts();
		m_FirstSample = true;
		m_audioPollingThread.Start();
	}

    m_iSamplesFromStream = (amf_uint64)0xFFFFFFFFFFFFFFFFLL;
    m_DiffsAcc = 0;
    m_StatCount = 0;

    AMFTraceDebug(AMF_FACILITY, L"Init() sample rate %d channels = %d format %d", (int)pWaveFormat->nSamplesPerSec, (int)pWaveFormat->nChannels, (int)pWaveFormat->wFormatTag);

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
	//
    AMFTraceInfo(AMF_FACILITY, L"Terminate()");

	if (m_pAMFDataStreamAudio)
	{
		m_pAMFDataStreamAudio->SetAtEOF();
	}
	Drain();

	m_audioPollingThread.RequestStop();
	m_audioPollingThread.WaitForStop();

	AMFLock lock(&m_sync);

	m_frameCount = 0;
	m_bForceEof = false;
	if (m_pAMFDataStreamAudio)
	{
		m_pAMFDataStreamAudio->Terminate();
		m_pAMFDataStreamAudio.Release();
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
    m_prevPts = 0;
    m_frameCount = 0;
    m_bFlush = true;
    m_CurrentPts = GetCurrentPts();
	return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL  AMFAudioCaptureImpl::SubmitInput(AMFData* /*pData*/)
{
    return AMF_NOT_IMPLEMENTED;
}

AMF_RESULT  AMF_STD_CALL  AMFAudioCaptureImpl::SubmitInputPrivate(AMFData* pData)
{
	AMFLock lock(&m_sync);

	AMF_RESULT  res = AMF_INPUT_FULL;
	if (m_AudioDataQueue.Add(0, pData, 0, 0))
	{
		res = AMF_OK;
	}

	return res;
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
        //        AMFTraceInfo(AMF_FACILITY, L"QueryOutput() pts=%5.2f duration=%5.2f", pFrame->GetPts()/10000., pFrame->GetDuration()/10000.);
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
	if (!m_pAMFDataStreamAudio)
	{
		return res;
	}
	UINT capturedSamples = 0;

    AMFAudioBufferPtr pAudioBuffer;
    {
        AMFLock lock(&m_sync);

		AMF_RETURN_IF_FALSE(m_pContext != nullptr, AMF_FAIL, L"AMFAudioCaptureImpl::PollStream(): AMF context is NULL");

	    uint8_t* pData = nullptr;

	    // Get some of the audio format properties
	    WAVEFORMATEX* pWaveFormat = m_pAMFDataStreamAudio->GetWaveFormat();
	    AMF_AUDIO_FORMAT sampleFormat = AMFAF_FLTP; //to be converted from pWaveFormat->wFormatTag
	    amf_int32 sampleRate = pWaveFormat->nSamplesPerSec;
	    amf_int32 channels = pWaveFormat->nChannels;

        amf_uint64 posStream = 0;
        bool bDiscontinuity = false;
        AMF_RESULT err = AMF_OK;
        res = m_pAMFDataStreamAudio->CaptureOnePacket(&pData, capturedSamples, posStream, bDiscontinuity);

        if (res == AMF_NOT_INITIALIZED)
        {
            return res;
        }

//        AMF_RETURN_IF_FAILED(res, L"CaptureOnePacket failed");

        if (m_iSamplesFromStream == amf_uint64(0xFFFFFFFFFFFFFFFFLL) || bDiscontinuity == true)
        {
            m_iSamplesFromStream = posStream;
        }

        amf_int32 sampleSize = 1;
        switch (pWaveFormat->wFormatTag)
        {
        case WAVE_FORMAT_PCM:
            sampleSize = pWaveFormat->wBitsPerSample / 8;
            break;
        case WAVE_FORMAT_IEEE_FLOAT:
            sampleSize = 4;
            break;
        }

        amf_pts silenceSamples = 0;

        if (capturedSamples == 0)
        {
            // Handle silence when there are no samples
            amf_pts expectedDuration = GetCurrentPts() - m_CurrentPts;
            silenceSamples = sampleRate * expectedDuration / AMF_SECOND;
        }
        else
        {
            if (posStream != amf_uint64(0xFFFFFFFFFFFFFFFFLL))
            {
                if (posStream > m_iSamplesFromStream)
                {
                    //partial silence
                    silenceSamples = posStream - m_iSamplesFromStream;
                }
                else if (posStream < m_iSamplesFromStream)
                {
                    m_iSamplesFromStream = posStream; // just in case if rounding failed
                }
            }
            if (pData == nullptr)
            {
                silenceSamples += capturedSamples;
                capturedSamples = 0;
            }
        }
        amf_size capturedDataSize = amf_size(capturedSamples) * sampleSize * channels;
        amf_size silenceDataSize = amf_size(silenceSamples * sampleSize * channels);
		if (silenceSamples + (amf_int32)capturedSamples > 0)
		{
			err = m_pContext->AllocAudioBuffer(AMF_MEMORY_HOST, sampleFormat, (amf_int32)silenceSamples + (amf_int32)capturedSamples, sampleRate, channels, &pAudioBuffer);
			if (AMF_OK == err && pAudioBuffer != nullptr)
			{
				amf_uint8* pDst = (amf_uint8*)pAudioBuffer->GetNative();
				if (silenceDataSize > 0)
				{
					memset(pDst, 0, silenceDataSize);
					pDst += silenceDataSize;
				}
				if (capturedSamples > 0)
				{
					if (4 == channels)  //ambisonic
					{
						err = AmbisonicFormatConvert(pData, pDst, amf_int32(capturedSamples), pWaveFormat);
					}
					else
					{
						memcpy(pDst, pData, capturedDataSize);
					}
				}
				m_iSamplesFromStream += capturedSamples;
				m_iSamplesFromStream += silenceSamples;

				amf_pts duration = (capturedSamples + silenceSamples) * AMF_SECOND / sampleRate;

				pAudioBuffer->SetPts(m_CurrentPts);
				pAudioBuffer->SetDuration(duration);
				m_prevPts = m_CurrentPts;
				m_CurrentPts += duration;
			}
		}
        err = m_pAMFDataStreamAudio->CaptureOnePacketDone();

        if(m_bFlush)
        {
            m_bFlush = false;
            return res;
        }
    }
	    // Submit the audio
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


	    AMF_RESULT err = AMF_INPUT_FULL;
	    while (m_audioPollingThread.StopRequested() == false)
	    {
            {
                AMFLock lock(&m_sync);
                if(m_bFlush)
                {
                    m_bFlush = false;
                    break;
                }
		        err = SubmitInputPrivate(pAudioBuffer);
            }
		    if (AMF_INPUT_FULL != err)
		    {
                amf_sleep(1); // milliseconds
                break;
		    }
		    amf_sleep(1); // milliseconds
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

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFAudioCaptureImpl::WaveHeader(const WAVEFORMATEX *pWaveFormat, amf_int32 lenData, BYTE** ppData, amf_int32& sizeHeader)
{
	//  A wave file consists of:
	//
	//  RIFF header:    8 bytes consisting of the signature "RIFF" followed by a 4 byte file length.
	//  WAVE header:    4 bytes consisting of the signature "WAVE".
	//  fmt header:     4 bytes consisting of the signature "fmt " followed by a WAVEFORMATEX
	//  WAVEFORMAT:     <n> bytes containing a waveformat structure.
	//  DATA header:    8 bytes consisting of the signature "data" followed by a 4 byte file length.
	//  wave data:      <m> bytes containing wave data.
	typedef struct {
		char RiFFName[4];
		long RiffLength;

		char WaveName[4];
		char WaveFormatName[4];
		long WaveFormatLength;
		WAVEFORMATEX WaveInfo;  //use cbsize to hold dataName first two bytes
		char dataName[2];
		unsigned long dataLength;
	} RiffWave;

	RiffWave* pRiffWave = new RiffWave;
	if (!pRiffWave)
	{
		return AMF_FAIL;
	}

	memset(pRiffWave, 0x00, sizeof(RiffWave));
	memcpy(pRiffWave->RiFFName, "RIFF", 4);
	memcpy(pRiffWave->WaveName, "WAVE", 4);
	memcpy(pRiffWave->WaveFormatName, "fmt ", 4);
	memcpy(&(pRiffWave->WaveInfo), pWaveFormat, sizeof(WAVEFORMATEX) - sizeof(pWaveFormat->cbSize));
	pRiffWave->WaveInfo.cbSize = 'a' << 8 | 'd';
	memcpy(pRiffWave->dataName, "ta", 2);
	pRiffWave->WaveFormatLength = sizeof(WAVEFORMATEX) - sizeof(pWaveFormat->cbSize);
	pRiffWave->dataLength = lenData;
	pRiffWave->RiffLength = lenData + sizeof(RiffWave);

	*ppData = (BYTE*)pRiffWave;
	sizeHeader = sizeof(RiffWave);
	return AMF_OK;
}

//Tetrahedral Microphone A-Format to B-Format conversion
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFAudioCaptureImpl::AmbisonicFormatConvert(const void* pSrc, void* pDst, amf_int32 numSamples, const WAVEFORMATEX *pWaveFormat)
{
	//only handle float format
	if (pWaveFormat->wFormatTag != WAVE_FORMAT_IEEE_FLOAT)
	{
		return AMF_OK;
	}

	//
	//1: Front Left Up(FLU)
	//2 : Front Right Down(FRD)
	//3 : Back Left Down(BLD)
	//4 : Back Right Up(BRU)
	//    W = FLU + FRD + BLD + BRU
	//    X = FLU + FRD - BLD - BRU
	//    Y = FLU - FRD + BLD - BRU
	//    Z = FLU - FRD - BLD + BRU
	amf_float* pDataSrc = (amf_float*)pSrc;
	amf_float* pDataDst = (amf_float*)pDst;
	for (amf_int32 idx = 0; idx < numSamples; idx++, pDataSrc += pWaveFormat->nChannels, pDataDst += pWaveFormat->nChannels)
	{
		*(pDataDst + 0) = *pDataSrc + *(pDataSrc + 1) + *(pDataSrc + 2) + *(pDataSrc + 3);
		*(pDataDst + 1) = *pDataSrc + *(pDataSrc + 1) - *(pDataSrc + 2) - *(pDataSrc + 3);
		*(pDataDst + 2) = *pDataSrc - *(pDataSrc + 1) + *(pDataSrc + 2) - *(pDataSrc + 3);
		*(pDataDst + 3) = *pDataSrc - *(pDataSrc + 1) - *(pDataSrc + 2) + *(pDataSrc + 3);
	}
	return AMF_OK;
}
