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
#include "public/include/components/FFMPEGAudioEncoder.h"
#include "public/common/PropertyStorageExImpl.h"
#include "public/include/core/Context.h"

extern "C"
{
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable:4244)
#endif

    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/fifo.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}

#include <list>


namespace amf
{

    //-------------------------------------------------------------------------------------------------

    class AMFAudioEncoderFFMPEGImpl :
        public AMFInterfaceBase,
        public AMFPropertyStorageExImpl<AMFComponent>
    {

    public:
        // interface access
        AMF_BEGIN_INTERFACE_MAP
            AMF_INTERFACE_MULTI_ENTRY(AMFComponent)
            AMF_INTERFACE_CHAIN_ENTRY(AMFPropertyStorageExImpl<AMFComponent>)
        AMF_END_INTERFACE_MAP


        AMFAudioEncoderFFMPEGImpl(AMFContext* pContext);
        virtual ~AMFAudioEncoderFFMPEGImpl();

        // AMFComponent interface
        virtual AMF_RESULT  AMF_STD_CALL  Init(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height);
        virtual AMF_RESULT  AMF_STD_CALL  ReInit(amf_int32 width, amf_int32 height);
        virtual AMF_RESULT  AMF_STD_CALL  Terminate();
        virtual AMF_RESULT  AMF_STD_CALL  Drain();
        virtual AMF_RESULT  AMF_STD_CALL  Flush();

        virtual AMF_RESULT  AMF_STD_CALL  SubmitInput(AMFData* pData);
        virtual AMF_RESULT  AMF_STD_CALL  QueryOutput(AMFData** ppData);
        virtual AMFContext* AMF_STD_CALL  GetContext()                                                  {  return m_pContext;  };
        virtual AMF_RESULT  AMF_STD_CALL  SetOutputDataAllocatorCB(AMFDataAllocatorCB* /*callback*/)    {  return AMF_NOT_SUPPORTED;  };
        virtual AMF_RESULT  AMF_STD_CALL  GetCaps(AMFCaps** /*ppCaps*/)                                 {  return AMF_NOT_SUPPORTED;  };
        virtual AMF_RESULT  AMF_STD_CALL  Optimize(AMFComponentOptimizationCallback* /*pCallback*/)     {  return AMF_OK;  };


        // AMFPropertyStorageObserver interface
        virtual void        AMF_STD_CALL  OnPropertyChanged(const wchar_t* pName);


    protected:
        AMF_RESULT  AMF_STD_CALL  GetNewFrame(AMFAudioBuffer** pNewBuffer, amf_int32 requiredSize);
        AMF_RESULT  AMF_STD_CALL  SplitOrCombineFrames(AMFAudioBuffer* pInBuffer, amf_int32 requiredSize);
        AMF_RESULT  AMF_STD_CALL  InitializeFrame(AMFAudioBuffer* pInBuffer, AVFrame& avFrame);
        AMF_RESULT  AMF_STD_CALL  PacketToBuffer(const AVPacket& avPacket, AMFBuffer** ppOutBuffer);
        AMF_RESULT  AMF_STD_CALL  UpdatePacketProperties(AMFBuffer* pOutBuffer);

        AMF_RESULT  AMF_STD_CALL  SubmitFrame(AMFAudioBuffer* pInBuffer);
        AMF_RESULT  AMF_STD_CALL  RetrievePackets();


    private:
      mutable AMFCriticalSection  m_sync;

        AMFContextPtr                 m_pContext;
        amf_bool                      m_bEncodingEnabled;

        AVCodecContext*               m_pCodecContext;

        // information in relation to what the encoder was set-up to use
        AMF_AUDIO_FORMAT              m_inSampleFormat;
        amf_int32                     m_channelCount;
        amf_int32                     m_sampleRate;

        std::list<AMFAudioBufferPtr>  m_inputFrames;
        std::list<AMFBufferPtr>       m_outputFrames;

        // in QueryOutput, we want to make sure that we match the
        // input frame that went in with what's coming out, so we
        // copy the right properties to the right data going out
        std::list<AMFAudioBufferPtr>  m_submittedFrames;

        amf_pts                       m_firstFramePts;
        amf_int64                     m_firstFFMPEGPts;

        // it is possible we need to combine/split input
        // frames to get them to what the encoder needs
        // in which case, we need to keep track of the
        // partial frame that still has data that needs
        // to be filled with the next frame coming in
        amf_bool                      m_samplePreProcRequired;
        std::list<AMFAudioBufferPtr>  m_recycledFrames;
        AMFAudioBufferPtr             m_pPartialFrame;
        amf_int32                     m_partialFrameSampleCount;

        amf_bool                      m_isDraining;
        amf_bool                      m_isEncDrained;
        amf_bool                      m_bEof;

        amf_int64                     m_audioFrameSubmitCount;
        amf_int64                     m_audioFrameQueryCount;


        AMFAudioEncoderFFMPEGImpl(const AMFAudioEncoderFFMPEGImpl&);
        AMFAudioEncoderFFMPEGImpl& operator=(const AMFAudioEncoderFFMPEGImpl&);
    };

 //   typedef AMFInterfacePtr_T<AMFAudioEncoderFFMPEGImpl>    AMFAudioEncoderFFMPEGImplPtr;

}