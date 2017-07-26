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
#pragma once

extern "C"
{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavformat/url.h"

//    #include "config.h"
//#ifdef _WIN32
//    #if defined HAVE_STRUCT_POLLFD
//        #undef HAVE_STRUCT_POLLFD
//    #endif
//        #define     HAVE_STRUCT_POLLFD 1
//#endif
//    #include "libavformat/internal.h"
}

#include "public/include/components/Component.h"
#include "public/include/components/FFMPEGFileMuxer.h"
#include "public/common/PropertyStorageExImpl.h"
#include "public/include/core/Context.h"



namespace amf
{

    //-------------------------------------------------------------------------------------------------

    class AMFFileMuxerFFMPEGImpl : 
        public AMFInterfaceBase,
        public AMFPropertyStorageExImpl<AMFComponentEx>
    {


    //-------------------------------------------------------------------------------------------------
    typedef AMFInterfaceImpl < AMFPropertyStorageExImpl <AMFInput> > baseclassInputProperty;

        class AMFInputMuxerImpl : 
            public baseclassInputProperty
        {
            friend class AMFFileMuxerFFMPEGImpl;

        public:
            AMF_BEGIN_INTERFACE_MAP
                AMF_INTERFACE_ENTRY(AMFInput)
                AMF_INTERFACE_CHAIN_ENTRY(AMFPropertyStorageExImpl <AMFInput>)
                AMF_INTERFACE_CHAIN_ENTRY(baseclassInputProperty)
            AMF_END_INTERFACE_MAP

            AMFInputMuxerImpl(AMFFileMuxerFFMPEGImpl* pHost);
            virtual ~AMFInputMuxerImpl();

            virtual AMF_RESULT  AMF_STD_CALL  Init() = 0;

            // AMFInput inteface
            virtual AMF_RESULT  AMF_STD_CALL  SubmitInput(AMFData* pData);

        protected:
            virtual void        AMF_STD_CALL  OnPropertyChanged(const wchar_t* pName);
            virtual AMF_RESULT  AMF_STD_CALL  Drain();

            AMFFileMuxerFFMPEGImpl*   m_pHost;
            amf_int32                 m_iIndex;
            bool                      m_bEnabled;
            amf_int64                 m_iPacketCount;
            amf_pts                   m_ptsLast;
            amf_pts                   m_ptsShift;

        };
        typedef AMFInterfacePtr_T<AMFInputMuxerImpl>    AMFInputMuxerImplPtr;
    //-------------------------------------------------------------------------------------------------

        class AMFVideoInputMuxerImpl :
            public AMFInputMuxerImpl
        {
        public:
            AMFVideoInputMuxerImpl(AMFFileMuxerFFMPEGImpl* pHost);
            virtual ~AMFVideoInputMuxerImpl()    {};
            virtual AMF_RESULT  AMF_STD_CALL  Init();
        };

    //-------------------------------------------------------------------------------------------------

        class AMFAudioInputMuxerImpl :
            public AMFInputMuxerImpl
        {
        public:
            AMFAudioInputMuxerImpl(AMFFileMuxerFFMPEGImpl* pHost);
            virtual ~AMFAudioInputMuxerImpl()    {};
            virtual AMF_RESULT  AMF_STD_CALL  Init();

        };


    public:
        // interface access
        AMF_BEGIN_INTERFACE_MAP
            AMF_INTERFACE_MULTI_ENTRY(AMFComponent)
            AMF_INTERFACE_MULTI_ENTRY(AMFComponentEx)
            AMF_INTERFACE_CHAIN_ENTRY(AMFPropertyStorageExImpl<AMFComponentEx>)
        AMF_END_INTERFACE_MAP


        AMFFileMuxerFFMPEGImpl(AMFContext* pContext);
        virtual ~AMFFileMuxerFFMPEGImpl();

        // AMFComponent interface
        virtual AMF_RESULT  AMF_STD_CALL  Init(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height);
        virtual AMF_RESULT  AMF_STD_CALL  ReInit(amf_int32 width, amf_int32 height);
        virtual AMF_RESULT  AMF_STD_CALL  Terminate();
        virtual AMF_RESULT  AMF_STD_CALL  Drain();
        virtual AMF_RESULT  AMF_STD_CALL  Flush()                                                   {  return AMF_OK;  };

        virtual AMF_RESULT  AMF_STD_CALL  SubmitInput(AMFData* pData)                               {  return AMF_NOT_SUPPORTED;  };
        virtual AMF_RESULT  AMF_STD_CALL  QueryOutput(AMFData** ppData)                             {  return AMF_NOT_SUPPORTED;  };
        virtual AMFContext* AMF_STD_CALL  GetContext()                                              {  return m_pContext;  };
        virtual AMF_RESULT  AMF_STD_CALL  SetOutputDataAllocatorCB(AMFDataAllocatorCB* callback)    {  return AMF_OK;  };
        virtual AMF_RESULT  AMF_STD_CALL  GetCaps(AMFCaps** ppCaps)                                 {  return AMF_NOT_SUPPORTED;  };
        virtual AMF_RESULT  AMF_STD_CALL  Optimize(AMFComponentOptimizationCallback* pCallback)     {  return AMF_OK;  };

        // AMFComponentEx interface
        virtual amf_int32   AMF_STD_CALL  GetInputCount()                                           {  return (amf_int32) m_InputStreams.size();  };
        virtual amf_int32   AMF_STD_CALL  GetOutputCount()                                          {  return 0;  };

        virtual AMF_RESULT  AMF_STD_CALL  GetInput(amf_int32 index, AMFInput** ppInput);
        virtual AMF_RESULT  AMF_STD_CALL  GetOutput(amf_int32 index, AMFOutput** ppOutput)          {  return AMF_NOT_SUPPORTED;  };

        // AMFPropertyStorageObserver interface
        virtual void        AMF_STD_CALL  OnPropertyChanged(const wchar_t* pName);

    protected:
        AMF_RESULT AMF_STD_CALL     AllocateContext();
        AMF_RESULT AMF_STD_CALL     FreeContext();

        AMF_RESULT AMF_STD_CALL     Open();
        AMF_RESULT AMF_STD_CALL     Close();

        AMF_RESULT AMF_STD_CALL     WriteHeader();
        AMF_RESULT AMF_STD_CALL     WriteData(AMFData* pData, amf_int32 iIndex);
    private:
      mutable AMFCriticalSection  m_sync;

        AMFContextPtr                     m_pContext;
        amf_vector<AMFInputMuxerImplPtr>  m_InputStreams;

        // member variables from AMFMuxerFMPEG
        AVFormatContext*        m_pOutputContext;
        bool                    m_bHeaderIsWritten;
//
        amf_vector<bool>        m_bEofList;

        bool                    m_bTerminated;
        bool                    m_bForceEof;
        AMFFileMuxerFFMPEGImpl(const AMFFileMuxerFFMPEGImpl&);
        AMFFileMuxerFFMPEGImpl& operator=(const AMFFileMuxerFFMPEGImpl&);

        amf_int64               m_iViewFrameCount;
        amf_pts                 m_ptsStatTime;
    };

 //   typedef AMFInterfacePtr_T<AMFFileMuxerFFMPEGImpl>    AMFFileMuxerFFMPEGPtr;
    
}