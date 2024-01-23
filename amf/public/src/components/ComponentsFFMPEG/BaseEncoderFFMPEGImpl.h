///-------------------------------------------------------------------------
///
///  Copyright © 2023 Advanced Micro Devices, Inc. All rights reserved.
///
///-------------------------------------------------------------------------
///  @file   BaseEncoderFFMPEGImpl.h
///  @brief  Base Encoder FFMPEG
///-------------------------------------------------------------------------
#pragma once


#include "public/include/components/Component.h"
#include "public/include/components/VideoEncoderVCE.h"
#include "public/include/components/VideoEncoderHEVC.h"
#include "public/include/components/VideoEncoderAV1.h"
#include "public/common/PropertyStorageExImpl.h"
#include "public/common/Thread.h"
#include "public/include/core/Context.h"
#include "public/include/core/Compute.h"
#include <memory>
#include <deque>
#include <set>

extern "C"
{
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable:4244)
#endif

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/imgutils.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}

#define AMF_ENCODER_SW_CODEC_ID                L"CodecID"                      // amf_int64 (default = AV_CODEC_ID_NONE) - FFMPEG codec ID
#define VIDEO_ENCODER_ENABLE_ENCODING          L"EnableEncoding"               // bool (default = true) - if false, component will not encode anything


namespace amf
{

    ////-------------------------------------------------------------------------------------------------

    class BaseEncoderFFMPEGImpl :
        public AMFInterfaceBase,
        public AMFPropertyStorageExImpl<AMFComponent>
    {
    public:
        // interface access
        AMF_BEGIN_INTERFACE_MAP
            AMF_INTERFACE_MULTI_ENTRY(AMFComponent)
            AMF_INTERFACE_CHAIN_ENTRY(AMFPropertyStorageExImpl<AMFComponent>)
        AMF_END_INTERFACE_MAP


        BaseEncoderFFMPEGImpl(AMFContext* pContext);
        virtual ~BaseEncoderFFMPEGImpl();

        // AMFComponent interface
        virtual AMF_RESULT  AMF_STD_CALL  Init(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height);
        virtual AMF_RESULT  AMF_STD_CALL  ReInit(amf_int32 width, amf_int32 height);
        virtual AMF_RESULT  AMF_STD_CALL  Terminate();
        virtual AMF_RESULT  AMF_STD_CALL  Drain();
        virtual AMF_RESULT  AMF_STD_CALL  Flush();

        virtual AMF_RESULT  AMF_STD_CALL  SubmitInput(AMFData* pData);
        virtual AMF_RESULT  AMF_STD_CALL  QueryOutput(AMFData** ppData);
        virtual AMFContext* AMF_STD_CALL  GetContext()                                                  {  return m_spContext;  };
        virtual AMF_RESULT  AMF_STD_CALL  SetOutputDataAllocatorCB(AMFDataAllocatorCB* /*callback*/)    {  return AMF_NOT_SUPPORTED;  };
        virtual AMF_RESULT  AMF_STD_CALL  GetCaps(AMFCaps** /*ppCaps*/)                                 {  return AMF_NOT_SUPPORTED;  };
        virtual AMF_RESULT  AMF_STD_CALL  Optimize(AMFComponentOptimizationCallback* /*pCallback*/)     {  return AMF_OK;  };

        // AMFPropertyStorageObserver interface
        virtual void        AMF_STD_CALL  OnPropertyChanged(const wchar_t* pName);


    protected:
        virtual AMF_RESULT  AMF_STD_CALL  InitializeFrame(AMFSurface* pInSurface, AVFrame& avFrame);
        AMF_RESULT  AMF_STD_CALL  CodecContextInit(const wchar_t* name);
        virtual const char *AMF_STD_CALL GetEncoderName() = 0;
        virtual AMF_RESULT AMF_STD_CALL SetEncoderOptions() = 0;

    protected:

        // define the thread class that will send frames
        // in SubmitInput, we have to convert the frame
        // to host memory before send to ffmpeg encoder,
        // so a separate thread allows us to send frame
        // to ffmpeg and can copy the next incomming frame
        // to host memory at the same time.
        amf::AMFQueue<amf::AMFSurfacePtr>    m_SendQueue;

        class SendThread : public amf::AMFQueueThread< amf::AMFSurfacePtr, int>
        {
        public:
            SendThread(BaseEncoderFFMPEGImpl* pHost, amf::AMFQueue<amf::AMFSurfacePtr>* pSendQueue) :
                amf::AMFQueueThread< amf::AMFSurfacePtr, int>(pSendQueue, nullptr), m_pEncoderFFMPEG(pHost)
            { }
            virtual bool Process(amf_ulong& ulID, amf::AMFSurfacePtr& inData, int& outData) override;
        protected:
            BaseEncoderFFMPEGImpl* m_pEncoderFFMPEG;
            mutable AMFEvent       m_EventEOF;
        };
        SendThread              m_SendThread;

      mutable AMFCriticalSection        m_Sync;

      // in QueryOutput, we want to make sure that we match the
      // input frame that went in with what's coming out, so we
      // copy the right properties to the right data going out
      // the fact is that we don't want to store the full frames
      // because they can be quite large and the encoder seems
      // to queue quite a few before it spits out the first frame
      // just imagine you queue 15 x 8k frames
      struct AMFTransitFrame
      {
          AMFPropertyStoragePtr pStorage;
          amf_pts               pts;
          amf_pts               duration;
      };

        AMFContextPtr                   m_spContext;
        amf_bool                        m_bEncodingEnabled;

        AVCodecContext*                 m_pCodecContext;
        AVCodecID                       m_CodecID;
        AMFRate                         m_FrameRate;

        mutable AMFCriticalSection      m_SyncAVCodec;
        amf_list<AMFTransitFrame>       m_inputData;
        amf_list<amf_pts>               m_inputpts; // contains monotonically increasing PTS in decode order

        amf_pts                         m_firstFramePts;

        bool                            m_isEOF;

        amf_uint64                      m_videoFrameSubmitCount;
        amf_uint64                      m_videoFrameQueryCount;

        // data of the surface we are going to process
        // NOTE: For now, the frames should have the
        //       same size for each submit...
        AMF_SURFACE_FORMAT              m_format;
        amf_int32                       m_width;
        amf_int32                       m_height;


    private:
        BaseEncoderFFMPEGImpl(const BaseEncoderFFMPEGImpl&);
        BaseEncoderFFMPEGImpl& operator=(const BaseEncoderFFMPEGImpl&);
    };

 //   typedef AMFInterfacePtr_T<BaseEncoderFFMPEGImpl>    AMFBaseEncoderFFMPEGPtr;

}