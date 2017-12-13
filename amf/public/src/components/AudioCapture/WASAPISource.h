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

#pragma once

#include <atlbase.h>
#include <mmdeviceapi.h>
#include <Audioclient.h>
#include "public/common/InterfaceImpl.h"


namespace amf
{
	class AMFWASAPISourceImpl : public AMFInterfaceImpl<AMFInterface>
	{
	public:
		AMFWASAPISourceImpl();
		virtual ~AMFWASAPISourceImpl();

		// Setup and teardown
		AMF_RESULT Init(bool capture, amf_int32 activeDevice = 0);
		AMF_RESULT Terminate();

		// Capture start and done
		int CaptureOnePacket(char** ppData, UINT& numSamples);
		int CaptureOnePacketTry(char** ppData, UINT& numSamples);
		int CaptureOnePacketDone(UINT numSamples);

		// Getters
		WAVEFORMATEX*	GetWaveFormat(){ return &m_waveFormat; };
		UINT			GetFrameSize(){ return m_frameSize; };
		UINT			GetSampleCount(){ return m_sampleCount; };
		REFERENCE_TIME	GetFrameDuration(){ return m_duration; };
		std::vector<std::string> 
						GetDeviceList() { return m_deviceList; };

		// Call to end thread loop
		void SetAtEOF() { m_eof = true;  }

	private:
		AMF_RESULT InitCaptureMicrophone(amf_int32 activeDevice = 0);
		AMF_RESULT InitCaptureDesktop();

		AMF_RESULT CreateDeviceList();

		mutable AMFCriticalSection				m_sync;

		ATL::CComPtr<IMMDevice>					m_device;
		ATL::CComPtr<IAudioClient>				m_client;
		ATL::CComPtr<IAudioCaptureClient>		m_capture;
		std::vector<std::string>				m_deviceList;
		
		WAVEFORMATEX							m_waveFormat;
		amf_uint32								m_frameSize;
		amf_uint32								m_sampleCount;
		REFERENCE_TIME							m_duration;
		bool									m_eof;
	};
	typedef AMFInterfacePtr_T<AMFWASAPISourceImpl>    AMFWASAPISourceImplPtr;
} //namespace amf
