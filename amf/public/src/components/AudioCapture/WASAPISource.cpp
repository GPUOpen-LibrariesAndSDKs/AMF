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
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <propsys.h>
#include <functiondiscoverykeys_devpkey.h>
#include "../../../common/TraceAdapter.h"
#include "WASAPISource.h"
#include <sstream>

#pragma warning(disable: 4996)

using namespace amf;

namespace
{
	// Simple wrapper to free memory when object goes out of scope
	class WAVEFORMATEXWrapper
	{
	public:
		WAVEFORMATEXWrapper() : m_pFmt(NULL) {}
		~WAVEFORMATEXWrapper() { if (m_pFmt) CoTaskMemFree(m_pFmt); }
		WAVEFORMATEX* m_pFmt;
	};
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFWASAPISourceImpl::CaptureOnePacket(amf_uint8** ppData, UINT& numSamples, amf_uint64 &posStream, bool &bDiscontinuity)
{
    // check if any device event has been triggered that make the capture device no longer valid
    amf_pts lastDeviceEvent = m_notification.GetLastEventTime();
    amf_pts now = amf_high_precision_clock();
    if (lastDeviceEvent > 0 && lastDeviceEvent < (now - (AMF_SECOND / 5)))
    {
        m_notification.ResetLastEventTime();
        Terminate();
    }

    if (m_device == nullptr)
    {
        return AMF_NOT_INITIALIZED;
    }

    RenderSilence();

	numSamples = 0;
	*ppData = NULL;
    bDiscontinuity = false;

    AMF_RESULT result = AMF_OK;

	UINT packetLength = 0;
	for (unsigned i = 0; i < 2; i++)
	{
        HRESULT hr = m_capture->GetNextPacketSize(&packetLength);
        if (FAILED(hr))
        {
            AMFTraceError(AMF_FACILITY, L"GetNextPacketSize: hr=0x%X", hr);

            if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
            {
                return AMF_NOT_INITIALIZED;
            }
            else
            {
                result = AMF_FAIL;
            }
        }
        else
        {
            if (packetLength == 0)
            {
                amf_sleep(20);
            }
            else
            {
                DWORD   flags = 0;
                UINT64  pos = 0;
                UINT64  ts = 0;
                hr = m_capture->GetBuffer((LPBYTE*)ppData, &numSamples, &flags, &pos, &ts);
                if (FAILED(hr))
                {
                    AMFTraceError(AMF_FACILITY, L"GetBuffer: hr=0x%X", hr);
                    result = AMF_FAIL;
                }
                else
                {
                    m_LastNumOfSamples = numSamples;

                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                    {
                        *ppData = nullptr;	// Silence
                        if (m_silenceStarted == false)
                        {
                            m_silenceStarted = true;
                            AMFTraceInfo(AMF_FACILITY, L"GetBuffer: silence started at pos=%llu, timestamp=%llu, samples=%u, data=%llX", pos, ts, numSamples, (uint64_t)(*ppData));
                        }
                    }
                    else
                    {
                        if (m_silenceStarted == true)
                        {
                            m_silenceStarted = false;
                            AMFTraceInfo(AMF_FACILITY, L"GetBuffer: silence ended at pos=%llu, timestamp=%llu", pos, ts);
                        }
                    }
                    if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
                    {
                        bDiscontinuity = true;
                        AMFTraceInfo(AMF_FACILITY, L"GetBuffer: Discontinuity at pos=%llu, timestamp=%llu", pos, ts);
                    }
                    if (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)
                    {
                        AMFTraceError(AMF_FACILITY, L"GetBuffer: Timestamp error at pos=%llu, timestamp=%llu", pos, ts);
                        pos = 0xFFFFFFFFFFFFFFFFULL;
                    }
                    posStream = pos;
                    break;
                }
            }
        }
    }

	return result;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFWASAPISourceImpl::CaptureOnePacketDone()
{
    AMF_RESULT result = AMF_OK;
	HRESULT hr = m_capture->ReleaseBuffer(m_LastNumOfSamples);
    if (FAILED(hr))
    {
        AMFTraceError(AMF_FACILITY, L"CaptureOnePacketDone: hr=%d", hr);
        result = AMF_FAIL;
    }
	return result;
}
//-------------------------------------------------------------------------------------------------
int AMFWASAPISourceImpl::RenderSilence()
{
    if (m_render)
    {
        HRESULT hresult = S_OK;

        // Need to get current padding and subtract or else GetBuffer returns AUDCLNT_E_BUFFER_TOO_LARGE
        UINT32 numFramesPadding = 0;

        hresult = m_renderClient->GetCurrentPadding(&numFramesPadding);
        if (FAILED(hresult))
        {
            AMFTraceWarning(L"WASAPISource", L"RenderSilence: failed to get buffer padding");
            return false;
        }

        UINT32 bufferSize = 0;
        hresult = m_renderClient->GetBufferSize(&bufferSize);
        UINT32 numFramesAvailable = bufferSize - numFramesPadding;

        LPBYTE buffer = nullptr;
        // get buffer
        hresult = m_render->GetBuffer(numFramesAvailable, &buffer);
        if (FAILED(hresult))
        {
            AMFTraceWarning(L"WASAPISource", L"RenderSilence: failed to get the buffer");
            return false;
        }

        // set to zero
        //memset(buffer, 0, numFramesAvailable*DefaultDeviceFormatSilence_->nBlockAlign);

        // release buffer
        hresult = m_render->ReleaseBuffer(numFramesAvailable, AUDCLNT_BUFFERFLAGS_SILENT);
        if (FAILED(hresult))
        {
            AMFTraceWarning(L"WASAPISource", L"RenderSilence: failed to release buffer");
            return false;
        }
    }
    return 0;
}
//-------------------------------------------------------------------------------------------------
AMFWASAPISourceImpl::AMFWASAPISourceImpl() :
    m_device(),
    m_client(),
    m_capture(),
    m_renderClient(),
    m_render(),
    m_frameSize(0),
    m_sampleCount(0),
    m_duration(0),
    m_eof(false),
    m_silenceStarted(false),
    m_LastNumOfSamples(0),
#ifdef WIN32
    m_hrCoInitializeResult(S_FALSE)
#endif
{
    ::memset(&m_waveFormat, 0, sizeof(m_waveFormat));

#ifdef WIN32
    m_hrCoInitializeResult = CoInitialize(nullptr);
#endif

}

//-------------------------------------------------------------------------------------------------
AMFWASAPISourceImpl::~AMFWASAPISourceImpl()
{
	Terminate();
#ifdef WIN32
    if (m_hrCoInitializeResult == S_OK)
    {
        ::CoUninitialize();
        m_hrCoInitializeResult = S_FALSE;
    }
#endif

}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFWASAPISourceImpl::Terminate()
{
	AMF_RESULT err = AMF_OK;

    if (m_enumerator)
    {
        m_enumerator->UnregisterEndpointNotificationCallback(&m_notification);
        m_enumerator.Release();
    }

	m_deviceList.clear();
	if (m_client)
	{
		m_client->Stop();
		m_client.Release();
	}

	m_capture.Release();

    if (m_renderClient)
    {
        m_renderClient->Stop();
        m_renderClient.Release();
    }
    m_render.Release();

	m_device.Release();

	return err;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFWASAPISourceImpl::Init(bool capture, amf_int32 activeDevice, amf_pts bufferDuration)
{
	if (capture)
		return InitCaptureMicrophone(activeDevice, bufferDuration);

	return InitCaptureDesktop(bufferDuration);
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFWASAPISourceImpl::InitCaptureMicrophone(amf_int32 activeDevice, amf_pts bufferDuration)
{
	if (activeDevice < 0)
	{
		return CreateDeviceList();
	}

    AMF_RETURN_IF_FAILED(CreateEnumerator());

	ATL::CComPtr<IMMDeviceCollection> pCollection;

	HRESULT hr = m_enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pCollection);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"EnumAudioEndpoints() failed");

	UINT count(0);
	hr = pCollection->GetCount(&count);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"GetCount() failed");
	hr = (activeDevice >= (amf_int32)count) ? E_FAIL : hr;
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"activeDevice check failed");

	hr = pCollection->Item(activeDevice, &m_device);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Item() failed");

	hr = m_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&m_client);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Activate() failed");

    WAVEFORMATEX* supportedFormat = NULL;
	WAVEFORMATEXWrapper formatWrapper;
	// DWORD   flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_LOOPBACK;
	hr = m_client->GetMixFormat(&formatWrapper.m_pFmt);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"GetMixFormat() failed");

    WAVEFORMATEXWrapper pClosestMatch;
    hr = m_client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, formatWrapper.m_pFmt, &pClosestMatch.m_pFmt);
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"GetMixFormat() failed");
    switch (hr)
    {
    case S_OK:
        supportedFormat = formatWrapper.m_pFmt;
        break;
    case S_FALSE:
        supportedFormat = pClosestMatch.m_pFmt;
        break;
    default:
        break;
    }
    AMF_RETURN_IF_FALSE(supportedFormat != NULL, AMF_FAIL, L"Unable to determine supported audio capture format");

    memcpy(&m_waveFormat, supportedFormat, sizeof(WAVEFORMATEX));
    if ((supportedFormat->cbSize > 0) && (supportedFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE))
	{
        WAVEFORMATEXTENSIBLE* pExt = (WAVEFORMATEXTENSIBLE*)supportedFormat;
		if (pExt->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
		{
			m_waveFormat.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
		}
		else if (pExt->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
		{
			m_waveFormat.wFormatTag = WAVE_FORMAT_PCM;
		}
	}

	REFERENCE_TIME hnsRequestedDuration = bufferDuration;
    hr = m_client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, hnsRequestedDuration, 0, supportedFormat, NULL);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Initialize() failed");

	hr = m_client->GetService(__uuidof(IAudioCaptureClient), (void**)&m_capture);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"GetService() failed");

	hr = m_client->GetBufferSize(&m_sampleCount);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"GetBufferSize() failed");

	m_duration = bufferDuration;
	m_duration *= m_sampleCount / m_waveFormat.nSamplesPerSec;
    m_frameSize = supportedFormat->nBlockAlign;

	hr = m_client->Start();
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Start() failed");

    wchar_t *id;
    m_device->GetId(&id);
    if (nullptr != id)
    {
        m_notification.SetDeviceID(id);
        CoTaskMemFree(id);
    }

    m_enumerator->RegisterEndpointNotificationCallback(&m_notification);

	return AMF_OK;
}

static amf_wstring WaveFormatToString(const WAVEFORMATEX* format)
{
	amf_wstring str;
	std::wstringstream stream;
	stream << L"Format tag: " << format->wFormatTag <<
		L", channels: " << format->nChannels <<
		L", bits per sample: " << format->wBitsPerSample <<
		L", samples per sec: " << format->nSamplesPerSec <<
		L", block align: " << format->nBlockAlign <<
		L", avg bytes/sec: " << format->nAvgBytesPerSec;

	str = stream.str().c_str();
	return str;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFWASAPISourceImpl::InitCaptureDesktop(amf_pts bufferDuration)
{
    AMF_RETURN_IF_FAILED(CreateEnumerator());

	HRESULT hr = m_enumerator->GetDefaultAudioEndpoint(
		eRender, eConsole, &m_device);
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"GetDefaultAudioEndpoint() failed, hr=0x%X", hr);

	hr = m_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&m_client);
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Activate() failed, hr=0x%X", hr);

    WAVEFORMATEX* supportedFormat = NULL;
	WAVEFORMATEXWrapper formatWrapper;
	hr = m_client->GetMixFormat(&formatWrapper.m_pFmt);
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"GetMixFormat() failed, hr=0x%X", hr);

    WAVEFORMATEXWrapper pClosestMatch;
    hr = m_client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, formatWrapper.m_pFmt, &pClosestMatch.m_pFmt);
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"IsFormatSupported() failed, hr=0x%X", hr);
    switch (hr)
    {
    case S_OK:
        supportedFormat = formatWrapper.m_pFmt;
		AMFTraceInfo(AMF_FACILITY, L"Requested audio format (%s) is supported", WaveFormatToString(supportedFormat).c_str());
        break;
    case S_FALSE:
        supportedFormat = pClosestMatch.m_pFmt;
		AMFTraceInfo(AMF_FACILITY, L"Requested audio format (%s) is not supported, using closest match (%s)", WaveFormatToString(formatWrapper.m_pFmt).c_str(), WaveFormatToString(supportedFormat).c_str());
        break;
    default:
        break;
    }
    AMF_RETURN_IF_FALSE(supportedFormat != NULL, AMF_FAIL, L"Unable to determine supported audio capture format=%d channels=%d", (int)formatWrapper.m_pFmt->wFormatTag, (int)formatWrapper.m_pFmt->nChannels);

    memcpy(&m_waveFormat, supportedFormat, sizeof(WAVEFORMATEX));
    if ((supportedFormat->cbSize > 0) && (supportedFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE))
	{
        WAVEFORMATEXTENSIBLE* pExt = (WAVEFORMATEXTENSIBLE*)supportedFormat;
		if (pExt->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
		{
			m_waveFormat.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
		}
		else if (pExt->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
		{
			m_waveFormat.wFormatTag = WAVE_FORMAT_PCM;
		}
	}

	REFERENCE_TIME hnsRequestedDuration = bufferDuration;
    hr = m_client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, hnsRequestedDuration, 0, supportedFormat, NULL);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Initialize() failed, hr=0x%X, duration=%lld, format (%s)", hr, hnsRequestedDuration, WaveFormatToString(&m_waveFormat).c_str());

	hr = m_client->GetService(__uuidof(IAudioCaptureClient), (void**)&m_capture);
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"GetService() failed, hr=0x%X", hr);

	hr = m_client->GetBufferSize(&m_sampleCount);
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"GetBufferSize() failed, hr=0x%X", hr);

	m_duration = bufferDuration;
	m_duration *= m_sampleCount / m_waveFormat.nSamplesPerSec;
    m_frameSize = supportedFormat->nBlockAlign;

	hr = m_client->Start();
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Start() failed, hr=0x%X", hr);

    //When capturing audio, if the system is not producing any sound, sometimes the audio stream will stop producing data.
    //The current workaround in this situation is to have the audio capture app generate the silent packets.This has disadvantages like sound popping.Sometimes the audio capture fails completely after long periods of silence.
    //A better workaround is to create a render client on the same device that's being captured, and continuously render a silent stream. This way the system will never stop producing data and the capture client gets a continous audio stream.
    InitRenderClient();

    wchar_t *id;
    m_device->GetId(&id);
    if (nullptr != id)
    {
        m_notification.SetDeviceID(id);
        CoTaskMemFree(id);
    }
    m_enumerator->RegisterEndpointNotificationCallback(&m_notification);

	return AMF_OK;
}

AMF_RESULT amf::AMFWASAPISourceImpl::InitRenderClient()
{
    if (!m_device)
        return AMF_NO_DEVICE;

    HRESULT hr = S_OK;

    hr = m_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&m_renderClient);
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Activate() failed, hr=0x%X", hr);

    WAVEFORMATEX* supportedFormat = NULL;
    hr = m_renderClient->GetMixFormat(&supportedFormat);
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"GetMixFormat() failed, hr=0x%X", hr);


    REFERENCE_TIME hnsRequestedDuration = 5000000; // 5 seconds
    hr = m_renderClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, hnsRequestedDuration, 0, supportedFormat, NULL);
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Initialize() failed, hr=0x%X, duration=%lld, format (%s)", hr, hnsRequestedDuration, WaveFormatToString(&m_waveFormat).c_str());

    hr = m_renderClient->GetService(__uuidof(IAudioRenderClient), (void**)&m_render);
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"GetService() failed, hr=0x%X", hr);

    hr = m_renderClient->Start();
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Start() failed, hr=0x%X", hr);

    return AMF_RESULT();
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFWASAPISourceImpl::CreateDeviceList()
{
    AMF_RETURN_IF_FAILED(CreateEnumerator());

	ATL::CComPtr<IMMDeviceCollection> pCollection;

	m_deviceList.clear();

	HRESULT hr = m_enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pCollection);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"EnumAudioEndpoints() failed");

	UINT count(0);
	hr = pCollection->GetCount(&count);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"GetCount() failed");

	for (UINT idx = 0; idx < count; idx++)
	{
		ATL::CComPtr<IMMDevice>  pDevice;
		ATL::CComPtr<IPropertyStore> pStore;
		hr = pCollection->Item(idx, &pDevice);
		AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Item() failed");

		hr = pDevice->OpenPropertyStore(STGM_READ, &pStore);
		AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"OpenPropertyStore() failed");

		PROPVARIANT deviceName;
		hr = pStore->GetValue(PKEY_Device_FriendlyName, &deviceName);
		AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"GetValue() failed");

		amf_wstring nameW = deviceName.pwszVal;
		m_deviceList.push_back(amf_from_unicode_to_utf8(nameW));
	}

	return AMF_OK;
}

AMF_RESULT amf::AMFWASAPISourceImpl::CreateEnumerator()
{
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&m_enumerator);
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"CoCreateInstance() failed");

    return AMF_OK;

}


//-------------------------------------------------------------------------------------------------
AudioDeviceNotification::AudioDeviceNotification()
{
    m_lastEvent = 0;
}

AudioDeviceNotification::~AudioDeviceNotification()
{
}

HRESULT STDMETHODCALLTYPE AudioDeviceNotification::QueryInterface(REFIID riid, void ** ppvObject)
{
    if (ppvObject == NULL)
    {
        return E_POINTER;
    }

    *ppvObject = NULL;

    if (riid == IID_IUnknown || riid == __uuidof(IMMNotificationClient))
    {
        *ppvObject = static_cast<IMMNotificationClient *>(this);
    }
    else
    {
        return E_NOINTERFACE;
    }

    return S_OK;
}

ULONG STDMETHODCALLTYPE AudioDeviceNotification::AddRef(void)
{
    return 1;
}

ULONG STDMETHODCALLTYPE AudioDeviceNotification::Release(void)
{
    return 1;
}

HRESULT STDMETHODCALLTYPE AudioDeviceNotification::OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD /*dwNewState*/)
{
    if (0 == wcscmp(pwstrDeviceId, m_deviceId.c_str())
        || m_deviceId.empty())
    {
        AMFTraceInfo(AMF_FACILITY, L"OnDeviceStateChanged %s", pwstrDeviceId);

        m_lastEvent = amf_high_precision_clock();
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE AudioDeviceNotification::OnDeviceAdded(LPCWSTR /*pwstrDeviceId*/)
{
    // does not affect capture
    return S_OK;
}

HRESULT STDMETHODCALLTYPE AudioDeviceNotification::OnDeviceRemoved(LPCWSTR pwstrDeviceId)
{
    if (0 == wcscmp(pwstrDeviceId, m_deviceId.c_str())
        || m_deviceId.empty())
    {
        AMFTraceInfo(AMF_FACILITY, L"OnDeviceRemoved %s", pwstrDeviceId);
        m_lastEvent = amf_high_precision_clock();
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE AudioDeviceNotification::OnDefaultDeviceChanged(EDataFlow /*flow*/, ERole /*role*/, LPCWSTR pwstrDefaultDeviceId)
{
    // match if new default is not our device
    if (m_deviceId.empty() || (pwstrDefaultDeviceId != nullptr && 0 != wcscmp(pwstrDefaultDeviceId, m_deviceId.c_str())))
    {
        AMFTraceInfo(AMF_FACILITY, L"OnDefaultDeviceChanged %s", pwstrDefaultDeviceId ? pwstrDefaultDeviceId : L"nullptr");
        m_lastEvent = amf_high_precision_clock();
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE AudioDeviceNotification::OnPropertyValueChanged(LPCWSTR /*pwstrDeviceId*/, const PROPERTYKEY /*key*/)
{
    // property changes are ignored
    return S_OK;
}

void AudioDeviceNotification::SetDeviceID(const wchar_t * dev)
{
    if (nullptr != dev)
    {
        m_deviceId = dev;
    }
}

amf_pts AudioDeviceNotification::GetLastEventTime()
{
    return m_lastEvent;
}

void AudioDeviceNotification::ResetLastEventTime()
{
    m_lastEvent = 0;
}
