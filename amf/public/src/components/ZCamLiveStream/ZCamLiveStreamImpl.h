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

#include "public/include/core/Context.h"
#include "public/include/components/Component.h"
#include "public/common/PropertyStorageExImpl.h"
#include "public/include/components/ZCamLiveStream.h"
#include "public/src/components/ZCamLiveStream/DataStreamZCam.h"

namespace amf
{
    //-------------------------------------------------------------------------------------------------

    class AMFZCamLiveStreamImpl : 
        public AMFInterfaceBase,
        public AMFPropertyStorageExImpl<AMFComponentEx>
    {
    //-------------------------------------------------------------------------------------------------
    typedef AMFInterfaceImpl < AMFPropertyStorageExImpl <AMFOutput> > baseclassOutputProperty;

    class AMFOutputZCamLiveStreamImpl :
        public baseclassOutputProperty
    {
        friend class AMFZCamLiveStreamImpl;

    public:
        AMF_BEGIN_INTERFACE_MAP
            AMF_INTERFACE_ENTRY(AMFOutput)
            AMF_INTERFACE_CHAIN_ENTRY(AMFPropertyStorageExImpl <AMFOutput>)
            AMF_INTERFACE_CHAIN_ENTRY(baseclassOutputProperty)
        AMF_END_INTERFACE_MAP

        AMFOutputZCamLiveStreamImpl(AMFZCamLiveStreamImpl* pHost, amf_int32 index);
        virtual ~AMFOutputZCamLiveStreamImpl();

        // AMFOutput interface
        virtual AMF_RESULT AMF_STD_CALL  QueryOutput(AMFData** ppData);
    protected:
        AMF_RESULT              SubmitFrame(AMFData* pData);
        AMF_RESULT              GetAudioData(AMFData** ppData);

        amf_int32                m_iQueueSize;
        AMFZCamLiveStreamImpl*   m_pHost;
        amf_int32                m_iIndex;
        amf_int64                m_frameCount;
        AMFQueue<AMFDataPtr>     m_VideoFrameQueue;
        bool                     m_isAudio;
        amf_pts                  m_audioTimeStamp;
        amf_pts                  m_ptsLast;                 
        amf_int64                m_lowLatency;
    };
    typedef AMFInterfacePtr_T<AMFOutputZCamLiveStreamImpl>    AMFOutputZCamLiveStreamImplPtr;
//-------------------------------------------------------------------------------------------------
    protected:
        class ZCamLiveStreamPollingThread : public amf::AMFThread
        {
        protected:
            AMFZCamLiveStreamImpl*         m_pHost;
        public:
            ZCamLiveStreamPollingThread(AMFZCamLiveStreamImpl* pHost);
            ~ZCamLiveStreamPollingThread();
            virtual void Run();
        };

    public:
        // interface access
        AMF_BEGIN_INTERFACE_MAP
            AMF_INTERFACE_MULTI_ENTRY(AMFComponent)
            AMF_INTERFACE_MULTI_ENTRY(AMFComponentEx)
            AMF_INTERFACE_CHAIN_ENTRY(AMFPropertyStorageExImpl<AMFComponentEx>)
        AMF_END_INTERFACE_MAP


        AMFZCamLiveStreamImpl(AMFContext* pContext);
        virtual ~AMFZCamLiveStreamImpl();

        // AMFComponent interface
        virtual AMF_RESULT  AMF_STD_CALL  Init(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height);
        virtual AMF_RESULT  AMF_STD_CALL  ReInit(amf_int32 width, amf_int32 height);
        virtual AMF_RESULT  AMF_STD_CALL  Terminate();
        virtual AMF_RESULT  AMF_STD_CALL  Drain();
        virtual AMF_RESULT  AMF_STD_CALL  Flush();

        virtual AMF_RESULT  AMF_STD_CALL  SubmitInput(AMFData* /* pData */)                             { return AMF_NOT_SUPPORTED; };
        virtual AMF_RESULT  AMF_STD_CALL  QueryOutput(AMFData** /* ppData */)                           { return AMF_NOT_SUPPORTED; };
        virtual AMF_RESULT  AMF_STD_CALL  SetOutputDataAllocatorCB(AMFDataAllocatorCB* /* pCallback */) { return AMF_OK; };
        virtual AMF_RESULT  AMF_STD_CALL  GetCaps(AMFCaps** /* ppCaps */)                               { return AMF_NOT_SUPPORTED; };
        virtual AMF_RESULT  AMF_STD_CALL  Optimize(AMFComponentOptimizationCallback* /* pCallback */)   { return AMF_OK; };
        virtual AMFContext* AMF_STD_CALL  GetContext()                                                  { return m_pContext; };

        // AMFComponentEx interface
        virtual amf_int32   AMF_STD_CALL  GetInputCount()                                               {  return 0;  };
        virtual amf_int32   AMF_STD_CALL  GetOutputCount();

        virtual AMF_RESULT  AMF_STD_CALL  GetInput(amf_int32 /* index */, AMFInput** /* ppInput */)     { return AMF_NOT_SUPPORTED; };
        virtual AMF_RESULT  AMF_STD_CALL  GetOutput(amf_int32 index, AMFOutput** ppOutput);

        // AMFPropertyStorageObserver interface
        virtual void        AMF_STD_CALL  OnPropertyChanged(const wchar_t* pName);

    protected:
        AMF_RESULT PollStream();

    protected:
        static const AMFSize   VideoFrameSizeList[];
        static const amf_int32 VideoFrameRateList[];
    private:
        AMFContextPtr               m_pContext;
        mutable AMFCriticalSection  m_sync;
        amf_vector<AMFOutputZCamLiveStreamImplPtr>  m_OutputStreams;
        ZCamLiveStreamPollingThread     m_ZCamPollingThread;
        AMFDataStreamZCamImpl           m_dataStreamZCam;
        bool                    m_bForceEof;
        bool                    m_bTerminated;
        amf_int64               m_frameCount;
        amf_int32               m_streamCount;
        amf_int32               m_streamActive;
        amf_int64               m_videoMode;
        amf_int64               m_audioMode;

        AMFZCamLiveStreamImpl(const AMFZCamLiveStreamImpl&);
        AMFZCamLiveStreamImpl& operator=(const AMFZCamLiveStreamImpl&);
    };

    typedef AMFInterfacePtr_T<AMFZCamLiveStreamImpl>    AMFZCamLiveStreamImplPtr;
}