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
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <propsys.h>
#include <functiondiscoverykeys_devpkey.h>
#include "public/common/TraceAdapter.h"
#include "WASAPISource.h"
#include "public/samples/CPPSamples/common/CmdLogger.h"

#pragma warning(disable: 4996)

using namespace amf;

#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000

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
int AMFWASAPISourceImpl::CaptureOnePacket(char** ppData, UINT& numSamples)
{
	numSamples = 0;
	*ppData = NULL;

	int result = 0;

	UINT packetLength = 0;
	while (true)
	{
		{
			AMFLock lock(&m_sync);
			// External request to exit capture loop
			if (m_eof)
			{
				break;
			}
			//
			HRESULT hr = m_capture->GetNextPacketSize(&packetLength);
			if (packetLength)
			{
				DWORD   flags;
				UINT64  pos, ts;
				hr = m_capture->GetBuffer((LPBYTE*)ppData, &numSamples, &flags, &pos, &ts);

				if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
				{
					*ppData = NULL;	// Silence
				}

				break;
			}
		}
		//
		amf_sleep(1); // milliseconds
	}

	return result;
}

//-------------------------------------------------------------------------------------------------
int AMFWASAPISourceImpl::CaptureOnePacketTry(char** ppData, UINT& numSamples)
{
	numSamples = 0;
	*ppData = NULL;

	int result = 0;

	UINT packetLength = 0;
	for (unsigned i = 0; i < 2; i++)
	{
		{
			AMFLock lock(&m_sync);
			// External request to exit capture loop
			if (m_eof)
			{
				break;
			}
			//
			HRESULT hr = m_capture->GetNextPacketSize(&packetLength);
			if (packetLength)
			{
				DWORD   flags;
				UINT64  pos, ts;
				hr = m_capture->GetBuffer((LPBYTE*)ppData, &numSamples, &flags, &pos, &ts);

				if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
				{
					*ppData = NULL;	// Silence
				}
				break;
			}
			// Wait long enough so we can make sure that we true silence
			amf_sleep(200);
		}
	}

	return result;
}

//-------------------------------------------------------------------------------------------------
int AMFWASAPISourceImpl::CaptureOnePacketDone(UINT numSamples)
{
	int hr = m_capture->ReleaseBuffer(numSamples);
	return hr;
}

//-------------------------------------------------------------------------------------------------
AMFWASAPISourceImpl::AMFWASAPISourceImpl() :
m_device(),
m_client(),
m_capture(),
m_eof(false)
{
}

//-------------------------------------------------------------------------------------------------
AMFWASAPISourceImpl::~AMFWASAPISourceImpl()
{
	Terminate();
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFWASAPISourceImpl::Terminate()
{
	AMF_RESULT err = AMF_OK;
	m_deviceList.clear();
	if (m_client)
	{
		m_client->Stop();
		m_client.Release();
	}

	m_capture.Release();
	m_device.Release();

	return err;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFWASAPISourceImpl::Init(bool capture, amf_int32 activeDevice)
{
	if (capture)
		return InitCaptureMicrophone(activeDevice);

	return InitCaptureDesktop();
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFWASAPISourceImpl::InitCaptureMicrophone(amf_int32 activeDevice)
{
	if (activeDevice < 0)
	{
		return CreateDeviceList();
	}

	ATL::CComPtr<IMMDeviceEnumerator> pEnumerator;
	ATL::CComPtr<IMMDeviceCollection> pCollection;
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"CoCreateInstance() failed");

	hr = pEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pCollection);
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

	WAVEFORMATEXWrapper formatWrapper;
	// DWORD   flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_LOOPBACK;
	hr = m_client->GetMixFormat(&formatWrapper.m_pFmt);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"GetMixFormat() failed");

	memcpy(&m_waveFormat, formatWrapper.m_pFmt, sizeof(WAVEFORMATEX));
	if ((formatWrapper.m_pFmt->cbSize > 0) && (formatWrapper.m_pFmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE))
	{
		WAVEFORMATEXTENSIBLE* pExt = (WAVEFORMATEXTENSIBLE*)formatWrapper.m_pFmt;
		if (pExt->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
		{
			m_waveFormat.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
		}
		else if (pExt->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
		{
			m_waveFormat.wFormatTag = WAVE_FORMAT_PCM;
		}
	}

	REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;
	hr = m_client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, hnsRequestedDuration, 0, formatWrapper.m_pFmt, NULL);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Initialize() failed");

	hr = m_client->GetService(__uuidof(IAudioCaptureClient), (void**)&m_capture);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"GetService() failed");

	hr = m_client->GetBufferSize(&m_sampleCount);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"GetBufferSize() failed");

	m_duration = REFTIMES_PER_SEC;
	m_duration *= m_sampleCount / m_waveFormat.nSamplesPerSec;
	m_frameSize = formatWrapper.m_pFmt->nBlockAlign;

	hr = m_client->Start();
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Start() failed");

	return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFWASAPISourceImpl::InitCaptureDesktop()
{
	ATL::CComPtr<IMMDeviceEnumerator> pEnumerator;
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);

	hr = pEnumerator->GetDefaultAudioEndpoint(
		eRender, eConsole, &m_device);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"GetDefaultAudioEndpoint() failed");

	hr = m_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&m_client);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Activate() failed");

	WAVEFORMATEXWrapper formatWrapper;
	hr = m_client->GetMixFormat(&formatWrapper.m_pFmt);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"GetMixFormat() failed");

	memcpy(&m_waveFormat, formatWrapper.m_pFmt, sizeof(WAVEFORMATEX));
	if ((formatWrapper.m_pFmt->cbSize > 0) && (formatWrapper.m_pFmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE))
	{
		WAVEFORMATEXTENSIBLE* pExt = (WAVEFORMATEXTENSIBLE*)formatWrapper.m_pFmt;
		if (pExt->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
		{
			m_waveFormat.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
		}
		else if (pExt->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
		{
			m_waveFormat.wFormatTag = WAVE_FORMAT_PCM;
		}
	}

	REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;
	hr = m_client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, hnsRequestedDuration, 0, formatWrapper.m_pFmt, NULL);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Initialize() failed");

	hr = m_client->GetService(__uuidof(IAudioCaptureClient), (void**)&m_capture);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"GetService() failed");

	hr = m_client->GetBufferSize(&m_sampleCount);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"GetBufferSize() failed");

	m_duration = REFTIMES_PER_SEC;
	m_duration *= m_sampleCount / m_waveFormat.nSamplesPerSec;
	m_frameSize = formatWrapper.m_pFmt->nBlockAlign;

	hr = m_client->Start();
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Start() failed");

	return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFWASAPISourceImpl::CreateDeviceList()
{
	ATL::CComPtr<IMMDeviceEnumerator> pEnumerator;
	ATL::CComPtr<IMMDeviceCollection> pCollection;

	m_deviceList.clear();
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"CoCreateInstance() failed");

	hr = pEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pCollection);
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

		std::wstring nameW = deviceName.pwszVal;
		std::string name(nameW.begin(), nameW.end());
		m_deviceList.push_back(name);
	}

	return AMF_OK;
}

