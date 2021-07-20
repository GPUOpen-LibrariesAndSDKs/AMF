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
#include "../../../common/InterfaceImpl.h"
#include "../../../common/AMFSTL.h"

namespace amf
{
    //This class implements IMMNotificationClient
    //It will return if audio capture should re-initialize due to device change
    class AudioDeviceNotification : public IMMNotificationClient
    {
    public:
        AudioDeviceNotification();
        ~AudioDeviceNotification();

        // Inherited via IMMNotificationClient
        virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void ** ppvObject) override;
        virtual ULONG STDMETHODCALLTYPE AddRef(void) override;
        virtual ULONG STDMETHODCALLTYPE Release(void) override;
        virtual HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override;
        virtual HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId) override;
        virtual HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId) override;
        virtual HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) override;
        virtual HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) override;


        // set the device id to be monitored
        void SetDeviceID(const wchar_t *dev);

        // return last audio device event time, 0 if no event has happened since previous call
        amf_pts GetLastEventTime();
        // reset last audio device event time to 0
        void ResetLastEventTime();

    private:
        amf_pts     m_lastEvent;
        std::wstring m_deviceId;
    };

    class AMFWASAPISourceImpl : public AMFInterfaceImpl<AMFInterface>
    {
    public:
        AMFWASAPISourceImpl();
        virtual ~AMFWASAPISourceImpl();

        // Setup and teardown
        AMF_RESULT Init(bool capture, amf_int32 activeDevice = 0, amf_pts bufferDuration = AMF_SECOND);
        AMF_RESULT Terminate();

        // Capture start and done
        AMF_RESULT CaptureOnePacket(uint8_t** ppData, UINT& numSamples, amf_uint64 &posStream, bool &bDiscontinuity);
        AMF_RESULT CaptureOnePacketDone();

        // render silence workaround
        int RenderSilence();

        // Getters
        WAVEFORMATEX*	GetWaveFormat(){ return &m_waveFormat; }
        UINT			GetFrameSize(){ return m_frameSize; }
        UINT			GetSampleCount(){ return m_sampleCount; }
        REFERENCE_TIME	GetFrameDuration(){ return m_duration; }
        amf_vector<amf_string> 
                        GetDeviceList() { return m_deviceList; }

        // Call to end thread loop
        void SetAtEOF() { m_eof = true;  }

    private:
        AMF_RESULT InitCaptureMicrophone(amf_int32 activeDevice, amf_pts bufferDuration);
        AMF_RESULT InitCaptureDesktop(amf_pts bufferDuration);

        AMF_RESULT InitRenderClient();

        AMF_RESULT CreateDeviceList();
        AMF_RESULT CreateEnumerator();

        mutable AMFCriticalSection				m_sync;

        ATL::CComPtr<IMMDeviceEnumerator>       m_enumerator;
        ATL::CComPtr<IMMDevice>					m_device;
        ATL::CComPtr<IAudioClient>				m_client;
        ATL::CComPtr<IAudioCaptureClient>		m_capture;
        
        ATL::CComPtr<IAudioClient>				m_renderClient;
        ATL::CComPtr<IAudioRenderClient>        m_render;

        amf_vector<amf_string>				    m_deviceList;
        
        WAVEFORMATEX							m_waveFormat;
        amf_uint32								m_frameSize;
        amf_uint32								m_sampleCount;
        REFERENCE_TIME							m_duration;
        bool									m_eof;
        bool                                    m_silenceStarted;
        UINT                                    m_LastNumOfSamples;
        
        AudioDeviceNotification                 m_notification;
#ifdef WIN32
        HRESULT                                 m_hrCoInitializeResult;
#endif

    };
    typedef AMFInterfacePtr_T<AMFWASAPISourceImpl>    AMFWASAPISourceImplPtr;
} //namespace amf
