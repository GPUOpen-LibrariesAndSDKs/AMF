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
#pragma once

#include "public/include/components/Capture.h"
#include "public/common/PropertyStorageExImpl.h"
#include <atlbase.h>
#include <mfapi.h>
#include <mfidl.h>
#include <Mfreadwrite.h>

namespace amf
{
    //-------------------------------------------------------------------------------------------------
    class MFCaptureManagerImpl : public AMFInterfaceImpl<AMFCaptureManager>
    {
    public:
        MFCaptureManagerImpl(amf::AMFContext* pContext);
        virtual ~MFCaptureManagerImpl();

        virtual AMF_RESULT          AMF_STD_CALL Update();
        virtual amf_int32           AMF_STD_CALL GetDeviceCount();
        virtual AMF_RESULT          AMF_STD_CALL GetDevice(amf_int32 index,AMFCaptureDevice **pDevice);

    protected:
        amf::AMFContextPtr          m_pContext;

        struct Device
        {
            CComPtr<IMFActivate> video;
            CComPtr<IMFActivate> audio;
            amf_wstring          name;
            amf_wstring          id;

        };

        amf_vector< Device > m_Devices;

    };
    //-------------------------------------------------------------------------------------------------

    class AMFMFCaptureImpl :
        public AMFInterfaceBase,
        public AMFPropertyStorageExImpl<AMFCaptureDevice>
    {
    protected:
        friend class MFCaptureManagerImpl;

        //-------------------------------------------------------------------------------------------------
        class AMFOutputBase : public AMFInterfaceImpl < AMFPropertyStorageExImpl <AMFOutput> >
        {
        public:
            AMFOutputBase(AMFMFCaptureImpl* pHost, IMFActivate* pActivate);
            virtual ~AMFOutputBase(){}
            // AMFOutput interface
            virtual AMF_RESULT AMF_STD_CALL     QueryOutput(AMFData** ppData);

            virtual AMF_RESULT  AMF_STD_CALL    Init();
            virtual AMF_RESULT  AMF_STD_CALL    Start();
            virtual AMF_RESULT  AMF_STD_CALL    Stop();
            virtual AMF_RESULT  AMF_STD_CALL    Drain();
        protected:
            virtual AMF_RESULT  AMF_STD_CALL    GetInitAttributes(IMFAttributes** /* ppAttributes */) { return AMF_OK; }
            virtual AMF_RESULT  AMF_STD_CALL    SelectSource(IMFMediaSource* pMediaSource) = 0;
            virtual AMF_RESULT  AMF_STD_CALL    ConvertData(IMFSample* pSample, IMFMediaBuffer* pMediaBufffer, AMFData **ppData);

            //-------------------------------------------------------------------------------------------------
            class PollingThread : public amf::AMFThread
            {
            public:
                PollingThread(AMFOutputBase* pHost) : m_pHost(pHost){}
                ~PollingThread(){}
                virtual void Run();
            protected:
                AMFOutputBase*          m_pHost;         //host
            };
            //-------------------------------------------------------------------------------------------------

            AMFMFCaptureImpl*                   m_pHost;
            AMFQueue<AMFDataPtr>                m_DataQueue; //video frame queue
            CComPtr<IMFActivate>                m_pActivate;
            CComPtr<IMFMediaSource>             m_pMediaSource;
            CComPtr<IMFSourceReader>            m_pReader;
            int                                 m_iSelectedIndex;
            PollingThread                       m_PollingThread;
            bool m_bEof;
        };
        typedef AMFInterfacePtr_T<AMFOutputBase>    AMFOutputBasePtr;

        //-------------------------------------------------------------------------------------------------
        class AMFVideoOutput : public AMFOutputBase
        {
        public:
            AMFVideoOutput(AMFMFCaptureImpl* pHost, IMFActivate* pActivate);
            virtual ~AMFVideoOutput(){}

            // AMFPropertyStorageExImpl
            //virtual AMF_RESULT  AMF_STD_CALL    ValidateProperty(const wchar_t* name, AMFVariantStruct value, AMFVariantStruct* pOutValidated) const;
        protected:
            virtual AMF_RESULT  AMF_STD_CALL    GetInitAttributes(IMFAttributes** ppAttributes);
            virtual AMF_RESULT  AMF_STD_CALL    SelectSource(IMFMediaSource* pMediaSource);
            virtual AMF_RESULT  AMF_STD_CALL    ConvertData(IMFSample* pSample, IMFMediaBuffer* pMediaBufffer, AMFData **ppData);

            class InputSampleTracker :  public AMFSurfaceObserver
            {
            public:
                InputSampleTracker(IMFSample* sample) : m_Sample(sample){}
                virtual ~InputSampleTracker(){}
            protected:
                virtual void AMF_STD_CALL OnSurfaceDataRelease(AMFSurface* /* pSurface */) { delete this; }

            private:
                CComPtr<IMFSample>  m_Sample;
            };

            struct Format
            {
                Format() : m_eFormat(AMF_SURFACE_UNKNOWN), m_iCodecID(0),
                    m_FrameSize(AMFConstructSize(0,0)), m_FrameRate(AMFConstructRate(0,1)),
                    m_iStreamIndex(-1), m_iMediaTypeIndex(-1),
                    m_pCodecNameFound(L"")
                {}

                AMF_SURFACE_FORMAT                  m_eFormat;
                amf_int64                           m_iCodecID;
                AMFSize                             m_FrameSize;
                AMFRate                             m_FrameRate;
                amf_int32                           m_iStreamIndex;
                amf_int32                           m_iMediaTypeIndex;
                const wchar_t *                     m_pCodecNameFound;
            };
            Format                                  m_Format;
            CComPtr<IMFDXGIDeviceManager>           m_pDxgiDeviceManager;
        };
        //-------------------------------------------------------------------------------------------------
        class AMFAudioOutput : public AMFOutputBase
        {
        public:
            AMFAudioOutput(AMFMFCaptureImpl* pHost, IMFActivate* pActivate);
            virtual ~AMFAudioOutput(){}
            // AMFPropertyStorageExImpl
            //virtual AMF_RESULT  AMF_STD_CALL    ValidateProperty(const wchar_t* name, AMFVariantStruct value, AMFVariantStruct* pOutValidated) const;
        protected:
            virtual AMF_RESULT  AMF_STD_CALL    SelectSource(IMFMediaSource* pMediaSource);
            virtual AMF_RESULT  AMF_STD_CALL    ConvertData(IMFSample* pSample, IMFMediaBuffer* pMediaBufffer, AMFData **ppData);

            AMF_AUDIO_FORMAT                    m_eFormat;
            amf_int32                           m_iSampleRate;
            amf_int32                           m_iChannels;
            amf_int64                           m_iCodecID;
        };
    //-------------------------------------------------------------------------------------------------
    public:
        // interface access
        AMF_BEGIN_INTERFACE_MAP
            AMF_INTERFACE_MULTI_ENTRY(AMFComponent)
            AMF_INTERFACE_MULTI_ENTRY(AMFComponentEx)
            AMF_INTERFACE_MULTI_ENTRY(AMFCaptureDevice)
            AMF_INTERFACE_CHAIN_ENTRY(AMFPropertyStorageExImpl<AMFCaptureDevice>)
        AMF_END_INTERFACE_MAP

        AMFMFCaptureImpl(AMFContext* pContext);
        virtual ~AMFMFCaptureImpl();

        // AMFComponent interface
        virtual AMF_RESULT  AMF_STD_CALL  Init(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height);
        virtual AMF_RESULT  AMF_STD_CALL  ReInit(amf_int32 width, amf_int32 height);
        virtual AMF_RESULT  AMF_STD_CALL  Terminate();
        virtual AMF_RESULT  AMF_STD_CALL  Drain();
        virtual AMF_RESULT  AMF_STD_CALL  Flush();

        virtual AMF_RESULT  AMF_STD_CALL  SubmitInput(AMFData* /* pData */)                             { return AMF_NOT_SUPPORTED; };
        virtual AMF_RESULT  AMF_STD_CALL  QueryOutput(AMFData** /* ppData */)                           { return AMF_NOT_SUPPORTED; };
        virtual AMF_RESULT  AMF_STD_CALL  SetOutputDataAllocatorCB(AMFDataAllocatorCB* /* callback */)  { return AMF_OK; };
        virtual AMF_RESULT  AMF_STD_CALL  GetCaps(AMFCaps** /* ppCaps */)                               { return AMF_NOT_SUPPORTED; };
        virtual AMF_RESULT  AMF_STD_CALL  Optimize(AMFComponentOptimizationCallback* /* pCallback */)   { return AMF_OK; };
        virtual AMFContext* AMF_STD_CALL  GetContext()                                                  { return m_pContext; };

        // AMFComponentEx interface
        virtual amf_int32   AMF_STD_CALL  GetInputCount()                                               {  return 0;  };
        virtual amf_int32   AMF_STD_CALL  GetOutputCount();

        virtual AMF_RESULT  AMF_STD_CALL  GetInput(amf_int32 /* index */, AMFInput** /* ppInput */)     { return AMF_NOT_SUPPORTED; };
        virtual AMF_RESULT  AMF_STD_CALL  GetOutput(amf_int32 index, AMFOutput** ppOutput);

        // AMFCaptureDevice
        AMF_RESULT  AMF_STD_CALL            Start();
        AMF_RESULT  AMF_STD_CALL            Stop();

    protected:
        AMF_RESULT  AMF_STD_CALL            UpdateDevice(IMFActivate *pActivateVideo, IMFActivate *pActivateAudio, const wchar_t* pName);

        AMFContextPtr                       m_pContext;              //context
        amf_vector<AMFOutputBasePtr>        m_OutputStreams;
        AMFCriticalSection                  m_sync;                   //sync
    private:
        AMFMFCaptureImpl(const AMFMFCaptureImpl&);
        AMFMFCaptureImpl& operator=(const AMFMFCaptureImpl&);
    };
    typedef AMFInterfacePtr_T<AMFMFCaptureImpl>    AMFMFCaptureImplPtr;
}