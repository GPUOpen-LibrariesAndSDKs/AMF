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

#include "public/include/components/Component.h"
#include "public/include/components/FFMPEGFileDemuxer.h"
#include "public/include/components/MediaSource.h"
#include "public/common/PropertyStorageExImpl.h"
#include "public/include/core/Context.h"

#include "H264Mp4ToAnnexB.h"

extern "C"
{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavformat/url.h"
}


namespace amf
{

    //-------------------------------------------------------------------------------------------------

    class AMFFileDemuxerFFMPEGImpl : 
        public AMFInterfaceBase,
        public AMFMediaSource,
        public AMFPropertyStorageExImpl<AMFComponentEx>
    {


    //-------------------------------------------------------------------------------------------------
    typedef AMFInterfaceImpl < AMFPropertyStorageExImpl <AMFOutput> > baseclassOutputProperty;

        class AMFOutputDemuxerImpl : 
            public baseclassOutputProperty
        {
            friend class AMFFileDemuxerFFMPEGImpl;

        public:
            AMF_BEGIN_INTERFACE_MAP
                AMF_INTERFACE_ENTRY(AMFOutput)
                AMF_INTERFACE_CHAIN_ENTRY(AMFPropertyStorageExImpl <AMFOutput>)
                AMF_INTERFACE_CHAIN_ENTRY(baseclassOutputProperty)
            AMF_END_INTERFACE_MAP

            AMFOutputDemuxerImpl(AMFFileDemuxerFFMPEGImpl* pHost, amf_int32 index);
            virtual ~AMFOutputDemuxerImpl();

            // AMFOutput interface
            virtual AMF_RESULT AMF_STD_CALL  QueryOutput(AMFData** ppData);

        protected:
            virtual void        AMF_STD_CALL  OnPropertyChanged(const wchar_t* pName);

            AMFFileDemuxerFFMPEGImpl*   m_pHost;
            amf_int32                   m_iIndex;
            bool                        m_bEnabled;
            // packet cache...
            amf_list<AVPacket*>        m_packetsCache;
            amf_int64                  m_iPacketCount;


            // packet handling helper methods
            AMF_RESULT  CachePacket(AVPacket* pPacket);
            void        ClearPacketCache();
            bool        IsCached();

        };
        typedef AMFInterfacePtr_T<AMFOutputDemuxerImpl>    AMFOutputDemuxerImplPtr;
    //-------------------------------------------------------------------------------------------------

        class AMFVideoOutputDemuxerImpl :
            public AMFOutputDemuxerImpl
        {
        public:
            AMFVideoOutputDemuxerImpl(AMFFileDemuxerFFMPEGImpl* pHost, amf_int32 index);
            virtual ~AMFVideoOutputDemuxerImpl()    {};
        };

    //-------------------------------------------------------------------------------------------------

        class AMFAudioOutputDemuxerImpl :
            public AMFOutputDemuxerImpl
        {
        public:
            AMFAudioOutputDemuxerImpl(AMFFileDemuxerFFMPEGImpl* pHost, amf_int32 index);
            virtual ~AMFAudioOutputDemuxerImpl()    {};
        };


    public:
        // interface access
        AMF_BEGIN_INTERFACE_MAP
            AMF_INTERFACE_MULTI_ENTRY(AMFComponent)
            AMF_INTERFACE_MULTI_ENTRY(AMFComponentEx)
            AMF_INTERFACE_MULTI_ENTRY(AMFMediaSource)
            AMF_INTERFACE_CHAIN_ENTRY(AMFPropertyStorageExImpl<AMFComponentEx>)
        AMF_END_INTERFACE_MAP


        AMFFileDemuxerFFMPEGImpl(AMFContext* pContext);
        virtual ~AMFFileDemuxerFFMPEGImpl();

        // AMFComponent interface
        virtual AMF_RESULT  AMF_STD_CALL  Init(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height);
        virtual AMF_RESULT  AMF_STD_CALL  ReInit(amf_int32 width, amf_int32 height);
        virtual AMF_RESULT  AMF_STD_CALL  Terminate();
        virtual AMF_RESULT  AMF_STD_CALL  Drain();
        virtual AMF_RESULT  AMF_STD_CALL  Flush();

        virtual AMF_RESULT  AMF_STD_CALL  SubmitInput(AMFData* pData)                               {  return AMF_NOT_SUPPORTED;  };
        virtual AMF_RESULT  AMF_STD_CALL  QueryOutput(AMFData** ppData);
        virtual AMFContext* AMF_STD_CALL  GetContext()                                              {  return m_pContext;  };
        virtual AMF_RESULT  AMF_STD_CALL  SetOutputDataAllocatorCB(AMFDataAllocatorCB* callback)    {  return AMF_OK;  };
        virtual AMF_RESULT  AMF_STD_CALL  GetCaps(AMFCaps** ppCaps)                                 {  return AMF_NOT_SUPPORTED;  };
        virtual AMF_RESULT  AMF_STD_CALL  Optimize(AMFComponentOptimizationCallback* pCallback)     {  return AMF_OK;  };

        // AMFComponentEx interface
        virtual amf_int32   AMF_STD_CALL  GetInputCount()                                           {  return 0;  };
        virtual amf_int32   AMF_STD_CALL  GetOutputCount();

        virtual AMF_RESULT  AMF_STD_CALL  GetInput(amf_int32 index, AMFInput** ppInput)             {  return AMF_NOT_SUPPORTED;  };
        virtual AMF_RESULT  AMF_STD_CALL  GetOutput(amf_int32 index, AMFOutput** ppOutput);

        // AMFMediaSource
        virtual AMF_RESULT           AMF_STD_CALL Seek(amf_pts ptsPos, AMF_SEEK_TYPE eType, amf_int32 whichStream);

        virtual amf_pts              AMF_STD_CALL GetPosition()                                     {  return m_ptsPosition;  };
        virtual amf_pts              AMF_STD_CALL GetDuration();

        virtual void                 AMF_STD_CALL SetMinPosition(amf_pts pts);
        virtual amf_pts              AMF_STD_CALL GetMinPosition();
        virtual void                 AMF_STD_CALL SetMaxPosition(amf_pts pts);
        virtual amf_pts              AMF_STD_CALL GetMaxPosition();

        virtual amf_uint64           AMF_STD_CALL GetFrameFromPts(amf_pts pts);
        virtual amf_pts              AMF_STD_CALL GetPtsFromFrame(amf_uint64 iFrame);

        virtual bool                 AMF_STD_CALL SupportFramesAccess();


        // AMFPropertyStorageObserver interface
        virtual void        AMF_STD_CALL  OnPropertyChanged(const wchar_t* pName);

    protected:
        AMF_RESULT AMF_STD_CALL  Open();
        AMF_RESULT AMF_STD_CALL  Close();

        // helper functions
        AMF_RESULT AMF_STD_CALL  ReadPacket(AVPacket **packet);
        AMF_RESULT AMF_STD_CALL  FindNextPacket(amf_int32 streamIndex, AVPacket **packet, bool saveSkipped);
        bool       AMF_STD_CALL  OutOfRange();
        void       AMF_STD_CALL  ClearCachedPackets();

        AMF_RESULT AMF_STD_CALL  BufferFromPacket(const AVPacket* pPacket, AMFBuffer** ppBuffer);
        AMF_RESULT AMF_STD_CALL  UpdateBufferProperties(AMFBuffer* pBuffer, const AVPacket* pPacket);
        void       AMF_STD_CALL  UpdateBufferVideoDuration(AMFBuffer* pBuffer, const AVPacket* pPacket, const AVStream *ist);
        void       AMF_STD_CALL  UpdateBufferAudioDuration(AMFBuffer* pBuffer, const AVPacket* pPacket, const AVStream *ist);

        void AMF_STD_CALL        FindSPSAndMVC(const amf_uint8 *buf, amf_int buf_size, bool &has_sps, bool &has_mvc) const;
        bool AMF_STD_CALL        CheckH264MVC();

        amf_uint64 AMF_STD_CALL  GetPropertyStartFrame();
        amf_uint64 AMF_STD_CALL  GetPropertyFramesNumber();
        amf_pts AMF_STD_CALL     CheckPtsRange(amf_pts pts);

        void AMF_STD_CALL        ReadRangeSettings();
        bool AMF_STD_CALL        IsCached();

    private:
      mutable AMFCriticalSection  m_sync;

        AMFContextPtr                        m_pContext;
        amf_vector<AMFOutputDemuxerImplPtr>  m_OutputStreams;

        // member variables from AMFDemuxerFFMPEG
        AVFormatContext*        m_pInputContext;
        amf_wstring             m_Url;
//        bool                    m_bSyncAV;

        amf_int64               m_iPacketCount;

        bool                    m_bTerminated;
        bool                    m_bForceEof;
        bool                    m_bStreamingMode;

        amf_pts                 m_ptsDuration;
        amf_pts                 m_ptsPosition;
        amf_pts                 m_ptsInitialMinPosition;
        amf_pts                 m_ptsSeekPos;

        // two main streams for sync
        amf_int32               m_iVideoStreamIndex;
        amf_int32               m_iAudioStreamIndex;
        amf_int                 m_eVideoCodecID;

#ifdef __USE_H264Mp4ToAnnexB
        amf::H264Mp4ToAnnexB    m_H264Mp4ToAnnexB;
#endif


        AMFFileDemuxerFFMPEGImpl(const AMFFileDemuxerFFMPEGImpl&);
        AMFFileDemuxerFFMPEGImpl& operator=(const AMFFileDemuxerFFMPEGImpl&);
    };
    
}