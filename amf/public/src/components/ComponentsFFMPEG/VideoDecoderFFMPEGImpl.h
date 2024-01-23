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
// Copyright (c) 2018 Advanced Micro Devices, Inc. All rights reserved.
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
#include "public/include/components/FFMPEGVideoDecoder.h"
#include "public/include/components/ColorSpace.h"
#include "public/include/core/Context.h"
#include "public/common/PropertyStorageExImpl.h"

extern "C"
{
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable:4244)
#endif

    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/imgutils.h"
    #include "libavutil/mastering_display_metadata.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}

#include <list>


namespace amf
{

    //-------------------------------------------------------------------------------------------------

    class AMFVideoDecoderFFMPEGImpl :
        public AMFInterfaceBase,
        public AMFPropertyStorageExImpl<AMFComponent>
    {

    public:
        // interface access
        AMF_BEGIN_INTERFACE_MAP
            AMF_INTERFACE_MULTI_ENTRY(AMFComponent)
            AMF_INTERFACE_CHAIN_ENTRY(AMFPropertyStorageExImpl<AMFComponent>)
        AMF_END_INTERFACE_MAP


        AMFVideoDecoderFFMPEGImpl(AMFContext* pContext);
        virtual ~AMFVideoDecoderFFMPEGImpl();


        // AMFComponent interface
        virtual AMF_RESULT  AMF_STD_CALL  Init(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height);
        virtual AMF_RESULT  AMF_STD_CALL  ReInit(amf_int32 width, amf_int32 height);
        virtual AMF_RESULT  AMF_STD_CALL  Terminate();
        virtual AMF_RESULT  AMF_STD_CALL  Drain();
        virtual AMF_RESULT  AMF_STD_CALL  Flush();

        virtual AMF_RESULT  AMF_STD_CALL  SubmitInput(AMFData* pData);
        virtual AMF_RESULT  AMF_STD_CALL  QueryOutput(AMFData** ppData);
        virtual AMFContext* AMF_STD_CALL  GetContext()                                                  {  return m_pContext;  };
        virtual AMF_RESULT  AMF_STD_CALL  SetOutputDataAllocatorCB(AMFDataAllocatorCB* callback)        {  m_pOutputDataCallback = callback;  return AMF_OK; };
        virtual AMF_RESULT  AMF_STD_CALL  GetCaps(AMFCaps** /*ppCaps*/)                                 {  return AMF_NOT_SUPPORTED;  };
        virtual AMF_RESULT  AMF_STD_CALL  Optimize(AMFComponentOptimizationCallback* /*pCallback*/)     {  return AMF_OK;  };


        // AMFPropertyStorageObserver interface
        virtual void        AMF_STD_CALL  OnPropertyChanged(const wchar_t* pName);


    protected:
        amf_int64  AMF_STD_CALL  GetColorPrimaries(AVColorPrimaries colorPrimaries);
        amf_int64  AMF_STD_CALL  GetColorTransfer(AVColorTransferCharacteristic colorTransfer);
        amf_int64  AMF_STD_CALL  GetColorRange(AVColorRange colorRange);
        AMF_RESULT AMF_STD_CALL  GetHDRInfo(const AVMasteringDisplayMetadata* pInFFmpegMetadata, AMFHDRMetadata* pAMFHDRInfo);
        AMF_RESULT AMF_STD_CALL  GetColorInfo(AMFSurface* pSurfaceOut, const AVFrame& picture);

        AMF_RESULT AMF_STD_CALL  CopyFrameThreaded(AMFPlane* pPlane, const AVFrame& picture, amf_int threadCount, bool isUVPlane);
        AMF_RESULT AMF_STD_CALL  CopyFrameYUV422(AMFPlane* pPlane, const AVFrame& picture);
        AMF_RESULT AMF_STD_CALL  CopyFrameYUV444(AMFPlane* pPlane, const AVFrame& picture);
        AMF_RESULT AMF_STD_CALL  CopyFrameRGB_FP16(AMFPlane* pPlane, const AVFrame& picture);
        AMF_RESULT AMF_STD_CALL  CopyFrameUV(AMFPlane* pPlaneUV, const AVFrame& picture, amf_int32 paddedLSB);
        AMF_RESULT AMF_STD_CALL  CopyLineLSB(amf_uint8* pMemOut, const amf_uint8* pMemIn, amf_size sizeToCopy, amf_int32 paddedLSB);


        virtual AMF_RESULT AMF_STD_CALL  SubmitDebug(AVCodecContext* /*pCodecContext*/, AVPacket& /*avpkt*/)                                            {  return AMF_OK;  };
        virtual AMF_RESULT AMF_STD_CALL  RetrieveDebug(const AVCodecContext* /*pCodecContext*/, const AVFrame& /*picture*/, AMFData* /*pOutputData*/)   {  return AMF_OK;  };


    private:
        mutable AMFCriticalSection  m_sync;


      // in QueryOutput, we want to make sure that we match the
      // input frame that went in with what's coming out, so we
      // copy the right properties to the right data going out
      struct AMFTransitFrame
      {
          AMFDataPtr  pData;
          amf_int64   id;
      };


        AMFContextPtr               m_pContext;
        bool                        m_bDecodingEnabled;
        bool                        m_bEof;

        AVCodecContext*             m_pCodecContext;
        amf_pts                     m_SeekPts;

        AMFBufferPtr                m_pExtraData;
        std::list<AMFTransitFrame>  m_inputData;

        amf_int64                   m_videoFrameSubmitCount;
        amf_int64                   m_videoFrameQueryCount;

        AMF_SURFACE_FORMAT          m_eFormat;
        AMFRate                     m_FrameRate;

        AMFDataAllocatorCBPtr       m_pOutputDataCallback;

        struct CopyTask
        {
            amf_uint8 *pSrc;
            amf_uint8 *pSrc1;
            amf_uint8 *pDst;
            amf_size   SrcLineSize;
            amf_size   DstLineSize;
            amf_int    lineStart;
            amf_int    lineEnd;
            AMFEvent*  pEvent;
            amf_long*  pCounter;

            void Run()
            {

                if (pSrc1 == nullptr)
                {
                    amf_size   toCopy = AMF_MIN(SrcLineSize, DstLineSize);
                    for (amf_int i = lineStart; i < lineEnd; i++)
                    {
                        memcpy(pDst + i * DstLineSize, pSrc + i * SrcLineSize, toCopy);
                    }
                }
                else
                {
                    amf_size   toCopy = SrcLineSize;

                    for (amf_int i = lineStart; i < lineEnd; i++)
                    {
                        amf_uint8 *pLineMemOut = pDst + i * DstLineSize;
                        amf_uint8 *pLineMemIn1 = pSrc + i * SrcLineSize;
                        amf_uint8 *pLineMemIn2 = pSrc1 + i * SrcLineSize;

                        for (amf_size x = 0; x < toCopy; x++)
                        {
                            *pLineMemOut++ = *pLineMemIn1++;
                            *pLineMemOut++ = *pLineMemIn2++;
                        }
                    }
                }
                if (amf_atomic_dec(pCounter) == 0)
                {
                    pEvent->SetEvent();
                }
            }
        };
        AMFQueue<CopyTask> m_InputQueue;

        class CopyThread : public AMFQueueThread<CopyTask, int>
        {
        public:
            CopyThread(AMFQueue<CopyTask>* pInputQueue, AMFQueue<int>* pOutputQueue, int /*param*/) :
                AMFQueueThread<CopyTask, int>(pInputQueue, pOutputQueue) {}
            virtual bool Process(amf_ulong& /*ulID*/, CopyTask& inData, int& /*outData*/)
            {
                inData.Run();
                return true;
            }
        };

        class CopyPipeline : public AMFQueueThreadPipeline<CopyTask, int, CopyThread, int>
        {
        public:
            CopyPipeline(AMFQueue<CopyTask>* pInputQueue) :
                AMFQueueThreadPipeline<CopyTask, int, CopyThread, int>(pInputQueue, nullptr),
                m_endEvent(false, false)
            {
            }
            AMFEvent m_endEvent;
        };
        CopyPipeline m_CopyPipeline;

        AMFVideoDecoderFFMPEGImpl(const AMFVideoDecoderFFMPEGImpl&);
        AMFVideoDecoderFFMPEGImpl& operator=(const AMFVideoDecoderFFMPEGImpl&);
    };

}