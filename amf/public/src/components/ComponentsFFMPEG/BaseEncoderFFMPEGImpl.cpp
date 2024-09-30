#include "BaseEncoderFFMPEGImpl.h"
#include "UtilsFFMPEG.h"

#include "public/include/core/Platform.h"
#include "public/include/core/Context.h"
#include "public/include/core/Trace.h"
#include "public/common/PropertyStorageImpl.h"
#include "public/common/TraceAdapter.h"
#include "public/common/AMFFactory.h"
#include "public/common/DataStream.h"

#include <iostream>

const amf_int32  MIN_FRAME_WIDTH  = 320;
const amf_int32  MAX_FRAME_WIDTH  = 7680;
const amf_int32  MIN_FRAME_HEIGHT = 200;
const amf_int32  MAX_FRAME_HEIGHT = 4320;


#define AMF_FACILITY            L"BaseEncoderFFMPEGImpl"

using namespace amf;


//
//
// BaseEncoderFFMPEGImpl
//
//
//--------------------------------------------------------------------------------------------------------------------------------
bool BaseEncoderFFMPEGImpl::SendThread::Process(amf_ulong& /*ulID*/, amf::AMFSurfacePtr& pSurfaceIn, int& /*outData*/)
{
    int ret = 0;
    if (pSurfaceIn == nullptr)
    {
        AMFLock lock(&m_pEncoderFFMPEG->m_SyncAVCodec);
        // check if it is the end of the stream
        if (m_pEncoderFFMPEG->m_isEOF)
        {
            // NOTE: to drain, we need to submit a NULL packet to avcodec_send_frame call
            //       Sending the first flush packet will return success.
            //       Subsequent ones are unnecessary and will return AVERROR_EOF
            ret = avcodec_send_frame(m_pEncoderFFMPEG->m_pCodecContext, NULL);
        }
        return true;
    }
    // fill the frame information
    AVFrameEx  avFrame;
    m_pEncoderFFMPEG->InitializeFrame(pSurfaceIn, avFrame);
    AMFTransitFrame  transitFrame = {};
    transitFrame.pStorage = new AMFInterfaceImpl< AMFPropertyStorageImpl <AMFPropertyStorage>>();
    transitFrame.pts = pSurfaceIn->GetPts();
    transitFrame.duration = pSurfaceIn->GetDuration();
    pSurfaceIn->CopyTo(transitFrame.pStorage, true);
    while(true)
    {
        {// ffmpeg is not thread safe
            AMFLock lock(&m_pEncoderFFMPEG->m_SyncAVCodec);
            ret = avcodec_send_frame(m_pEncoderFFMPEG->m_pCodecContext, &avFrame);

            if (ret >= 0)
            {
                // if we did have a frame to submit, and we succeeded
                // it's time to increment the submitted frames counter
                if (m_pEncoderFFMPEG->m_pCodecContext->max_b_frames > 0)
                {
                    m_pEncoderFFMPEG->m_inputpts.push_back(transitFrame.pts);
                }
                m_pEncoderFFMPEG->m_inputData.push_back(transitFrame);

                AMFTraceWarning(AMF_FACILITY, L"SendThread::Process() - sent input frame count is %d",
                    m_pEncoderFFMPEG->m_videoFrameSubmitCount);
                m_pEncoderFFMPEG->m_videoFrameSubmitCount++;
                return true;
            }
        }
        //
        // handle the error returns from the encoder
        //
        if (ret == AVERROR_EOF)
        {
            return true;
        }
        // NOTE: it is possible the encoder is busy as encoded frames have not
        //       been taken out yet, so it's possible that it will not accept even
        //       to start flushing the data yet, so it can return AVERROR(EAGAIN)
        //       along the normal case when it can't accept a new frame to process
        if (ret == AVERROR(EAGAIN))
        {

            AMFTraceWarning(AMF_FACILITY, L"SendThread::Process() - input is full");
            amf_sleep(1);
            continue;
        }
        char  errBuffer[AV_ERROR_MAX_STRING_SIZE] = { 0 };
        AMFTraceWarning(AMF_FACILITY, L"EncoderThread::Run() - Error sending a frame for encoding - %S",
            av_make_error_string(errBuffer, sizeof(errBuffer) / sizeof(errBuffer[0]), ret));
        return false;
    }

    return true;
}
//-------------------------------------------------------------------------------------------------
// we can initialize PA in different modes, for external,
// internal inside encoder, or various debug modes
// the template definition that creates this object doesn't
// like the enumeration so leave the mode parameter as int
BaseEncoderFFMPEGImpl::BaseEncoderFFMPEGImpl(AMFContext* pContext)
  : m_spContext(pContext),
    m_bEncodingEnabled(true),
    m_isEOF(false),
    m_videoFrameSubmitCount(0), m_videoFrameQueryCount(0),
    m_format(AMF_SURFACE_NV12),
    m_width(0), m_height(0),
    m_pCodecContext(NULL),
    m_CodecID(AV_CODEC_ID_NONE),
    m_FrameRate(AMFConstructRate(30, 1)),
    m_firstFramePts(-1LL),
    m_SendThread(this, &m_SendQueue)
{
    g_AMFFactory.Init();

    InitFFMPEG();
}
//-------------------------------------------------------------------------------------------------
BaseEncoderFFMPEGImpl::~BaseEncoderFFMPEGImpl()
{
    Terminate();

    g_AMFFactory.Terminate();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  BaseEncoderFFMPEGImpl::Init(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height)
{
    AMFTraceDebug(AMF_FACILITY, L"BaseEncoderFFMPEGImpl::Init(%s, %d, %d)", AMFSurfaceGetFormatName(format), width, height);

    AMF_RETURN_IF_FALSE((width >= MIN_FRAME_WIDTH) && (width <= MAX_FRAME_WIDTH), AMF_INVALID_ARG, L"Init() - width (%d) not in [%d, %d]", width, MIN_FRAME_WIDTH, MAX_FRAME_WIDTH);
    AMF_RETURN_IF_FALSE((height >= MIN_FRAME_HEIGHT) && (height <= MAX_FRAME_HEIGHT), AMF_INVALID_ARG, L"Init() - height (%d) not in [%d, %d]", height, MIN_FRAME_HEIGHT, MAX_FRAME_HEIGHT);

    //
    // lock to make the call threadsafe
    AMFLock lock(&m_Sync);

    // clean up any information we might previously have
    Terminate();

    // frame number should always start at 0
    m_videoFrameSubmitCount = 0;
    m_videoFrameQueryCount = 0;

    // update the format/width/height
    m_format = format;
    m_width = width;
    m_height = height;

    // reset the forced EOF flag
    m_isEOF = false;
    // reset the first frame pts offset
    m_firstFramePts = -1LL;

    // find the correct codec
    amf_int64  streamID = 0;
    AMF_RETURN_IF_FAILED(GetProperty(AMF_STREAM_CODEC_ID, &streamID));
    m_CodecID = GetFFMPEGVideoFormat((AMF_STREAM_CODEC_ID_ENUM)streamID);

    const AVCodec* pCodec;
    const char* pCodecName = GetEncoderName();
    pCodec = avcodec_find_encoder_by_name(pCodecName);

    if (!pCodec)
    {
        Terminate();
        return AMF_CODEC_NOT_SUPPORTED;
    }
    // allocate the codec context
    m_pCodecContext = avcodec_alloc_context3(pCodec);

    // set common properties
    m_pCodecContext->pix_fmt = GetFFMPEGSurfaceFormat(format);
    m_pCodecContext->strict_std_compliance = FF_COMPLIANCE_STRICT;
    m_pCodecContext->codec_type = AVMEDIA_TYPE_VIDEO;
    m_pCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    m_pCodecContext->thread_type = FF_THREAD_SLICE;
    m_pCodecContext->slices = amf_get_cpu_cores();
    m_pCodecContext->thread_count = amf_get_cpu_cores();
    AMFTraceDebug(AMF_FACILITY, L"BaseEncoderFFMPEGImpl::Init() - using %d cores", amf_get_cpu_cores());
    if (strcasecmp(m_pCodecContext->codec->name, pCodecName) != 0)
    {
        AMFTraceError(AMF_FACILITY, L"BaseEncoderFFMPEGImpl::Init() - Failed to find %s encoder", pCodecName);
        Terminate();
        return AMF_NOT_SUPPORTED;
    }

    SetEncoderOptions();
    // start the thread
    AMF_RETURN_IF_FALSE(m_SendThread.Start(), AMF_UNEXPECTED, L"Init() - m_SendThread.Start()");

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  BaseEncoderFFMPEGImpl::ReInit(amf_int32 width, amf_int32 height)
{
    AMF_SURFACE_FORMAT  currentFormat = m_format;
    Terminate();
    return Init(currentFormat, width, height);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  BaseEncoderFFMPEGImpl::Terminate()
{
    // terminate/clean-up the thread
    AMF_RETURN_IF_FALSE(m_SendThread.RequestStop(), AMF_UNEXPECTED, L"Terminate() - m_SendThread.RequestStop()");
    AMF_RETURN_IF_FALSE(m_SendThread.WaitForStop(), AMF_UNEXPECTED, L"Terminate() - m_SendThread.WaitForStop()");

    AMFLock lock(&m_Sync);

    // clean-up codec related items
    if (m_pCodecContext != NULL)
    {
        AMFTraceInfo(AMF_FACILITY, L"Submitted %d, Queried %d", (int)m_videoFrameSubmitCount, (int)m_videoFrameQueryCount);

        avcodec_close(m_pCodecContext);
        av_free(m_pCodecContext);
        m_pCodecContext = NULL;
    }

    // reset frame numbers
    m_videoFrameSubmitCount = 0;
    m_videoFrameQueryCount = 0;
    m_isEOF = false;

    // clean-up the internal states
    m_firstFramePts = -1LL;
    m_inputData.clear();
    m_inputpts.clear();

    m_SendQueue.Clear();

    // clean-up the width/height/format/codec
    m_format = AMF_SURFACE_UNKNOWN;
    m_width = 0;
    m_height = 0;
    m_CodecID = AV_CODEC_ID_NONE;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  BaseEncoderFFMPEGImpl::Drain()
{
    AMFTraceDebug(AMF_FACILITY, L"BaseEncoderFFMPEGImpl::Drain() - current input queue size %d",
        m_inputData.size());

    AMFLock lock(&m_Sync);

    m_isEOF = true;

    return SubmitInput(nullptr);
};
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  BaseEncoderFFMPEGImpl::Flush()
{
    AMFTraceDebug(AMF_FACILITY, L"BaseEncoderFFMPEGImpl::Flush()");

    // dump all the remaining frames in the system
    // NOTE: since this should get called from the same
    //       place as SubmitInput, we should not really
    //       get any more frames in
    // terminate encoding thread
    AMF_RETURN_IF_FALSE(m_SendThread.RequestStop(), AMF_UNEXPECTED, L"Flush() - m_SendThread.RequestStop()");
    AMF_RETURN_IF_FALSE(m_SendThread.WaitForStop(), AMF_UNEXPECTED, L"Flush() - m_SendThread.WaitForStop()");

    // now that we stopped the thread, we should be
    // OK to lock and clean up anything else
    AMFLock lock(&m_Sync);

    // need to flush the encoder
    // NOTE: it will only work if encoder declares
    //       support for AV_CODEC_CAP_ENCODER_FLUSH
    avcodec_flush_buffers(m_pCodecContext);

    // clean up internal states
    m_firstFramePts = -1LL;
    m_inputData.clear();
    m_inputpts.clear();
    m_videoFrameSubmitCount = 0;
    m_videoFrameQueryCount = 0;

    m_SendQueue.Clear();

    // now that we're done with the clean-up
    // restart the thread
    AMF_RETURN_IF_FALSE(m_SendThread.Start(), AMF_UNEXPECTED, L"Flush() - m_SendThread.Start()");

    return AMF_OK;
};
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  BaseEncoderFFMPEGImpl::SubmitInput(AMFData* pData)
{
    // check some required parameters
    AMF_RETURN_IF_FALSE(m_pCodecContext != NULL, AMF_NOT_INITIALIZED, L"SubmitInput() - Codec Context not Initialized");


    AMFLock lock(&m_Sync);

    //if encoding's been disabled, no need to proceed any further
    if (!m_bEncodingEnabled)
    {
       return m_isEOF ? AMF_EOF : AMF_OK;
    }

    // update the first frame pts offset - we'll be using
    // this to figure out output pts information later on
    if (pData && (m_firstFramePts == -1))
    {
        m_firstFramePts = pData->GetPts();
    }


    // SubmitInput should always receive data
    // end of file is not done by receiving empty data
    // in that case, people should be calling drain
    // once they're done submitting input
    // start submitting information to the encoder
    AMFSurfacePtr spSurface;

    if (pData != nullptr)
    {
        // check the input data type is AMFSurface
        // if it's not, we can't work with it
        spSurface = AMFSurfacePtr(pData);
        AMF_RETURN_IF_INVALID_POINTER(spSurface, L"SubmitInput() - spSurface == NULL");

        AMFPlane* pPlane0 = spSurface->GetPlaneAt(0);
        AMF_RETURN_IF_INVALID_POINTER(pPlane0, L"SubmitInput() - surface doesn't have any planes");

        // trace surface information received
        AMFTraceDebug(AMF_FACILITY, L"BaseEncoderFFMPEGImpl::SubmitInput() : format (%s), memory (%s), width (%d), height (%d)",
            AMFSurfaceGetFormatName(spSurface->GetFormat()),
            AMFGetMemoryTypeName(pData->GetMemoryType()),
            pPlane0->GetWidth(),
            pPlane0->GetHeight());

        // check the input data matches what the encoder expects
        // there's no point in somehow getting a frame that's of a
        // different format, or other properties encoder doesn't expect
        AMF_RETURN_IF_FALSE(spSurface->GetFormat() == m_format, AMF_INVALID_DATA_TYPE, L"SubmitInput() - format (%s) received, (%s) expected", AMFSurfaceGetFormatName(spSurface->GetFormat()), AMFSurfaceGetFormatName(m_format));
        AMF_RETURN_IF_FALSE(pPlane0->GetWidth() == m_width, AMF_INVALID_ARG, L"SubmitInput() - frame width received (%d), expected (%d)", pPlane0->GetWidth(), m_width);
        AMF_RETURN_IF_FALSE(pPlane0->GetHeight() == m_height, AMF_INVALID_ARG, L"SubmitInput() - frame height received (%d), expected (%d)", pPlane0->GetHeight(), m_height);

        // whatever information we receive, FFmpeg processes it
        // in host memory, so we have to make sure the data is
        // in the correct memory, hence the Convert at this point
        AMF_RESULT err = spSurface->Convert(AMF_MEMORY_HOST);
        AMF_RETURN_IF_FAILED(err, L"SubmitInput() - Convert(AMF_MEMORY_HOST) failed");

    }
    else
    {
        // update internal flag that we reached the end of the file
        // no more frames to encode so now it's time to drain the encoder
        // draining the encoder should happen once
        // otherwise an error code will be returned by avcodec_send_frame
        m_isEOF = true;
    }

    // add surface or eof null into queue for encoding thread
    m_SendQueue.Add(0, spSurface);

    return (m_isEOF == true) ? AMF_EOF : AMF_OK;
};
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  BaseEncoderFFMPEGImpl::QueryOutput(AMFData** ppData)
{
    // check some required parameters
    AMF_RETURN_IF_FALSE(m_pCodecContext != NULL, AMF_NOT_INITIALIZED, L"QueryOutput() - Codec Context not Initialized");
    AMF_RETURN_IF_FALSE(ppData != NULL, AMF_INVALID_ARG, L"QueryOutput() - ppData == NULL");

    // initialize output
    *ppData = nullptr;


    AMFLock lock(&m_Sync);
    //
    // encode
    AVPacketEx avPacket;

    // retrieve the encoded packet - it is possible that
    // for some packets, the encoder needs more data, in
    // which case it will return AVERROR(EAGAIN)
    //
    // AVERROR(EAGAIN): output is not available in the current state -
    //                  user must try to send input
    // AVERROR_EOF: the encoder has been fully flushed, and there will be no more output packets
    // AVERROR(EINVAL): codec not opened, or it is an encoder
    // other errors: legitimate decoding errors
    {
        AMFLock lock2(&m_SyncAVCodec);

        int ret = avcodec_receive_packet(m_pCodecContext, &avPacket);
        if (ret == AVERROR(EAGAIN))
        {
            AMFTraceDebug(AMF_FACILITY, L"QueryOutput() - avcodec_receive_packet EAGAIN");

            // if we finished the packets in the encoder, we should get
            // out as there's nothing to put out and we don't need to remove
            // any of the queued input frames
            // we also don't have to unref the packet as nothing got out
            // of the encoder at this point
            return AMF_REPEAT;
        }
        else if (ret == AVERROR_EOF)
        {
            AMFTraceWarning(AMF_FACILITY, L"QueryOutput() - avcodec_receive_packet EOF");

            // if we're draining, we need to check until we get we get
            // AVERROR_EOF to know the encoder has been fully drained
            return AMF_EOF;
        }
        else
        {
            char  errBuffer[AV_ERROR_MAX_STRING_SIZE] = { 0 };
            AMF_RETURN_IF_FALSE(ret >= 0, AMF_FAIL, L"QueryOutput() - Error encoding frame - %S", av_make_error_string(errBuffer, sizeof(errBuffer) / sizeof(errBuffer[0]), ret));
        }

        //
        // if avcodec_receive_packet returns 0,
        // a valid packet should have been provided
        //
        // NOTE: For video, it should typically contain one compressed frame.
        //       For audio it may contain several compressed frames.
        //       Encoders are allowed to output empty packets, with no
        //       compressed data, containing only side data (e.g. to update
        //       some stream parameters at the end of encoding)."
        if (ret >= 0 && avPacket.size > 0)
        {
            // allocate and fill output buffer - if we fail the memory
            // allocation, we probably have more problems than worrying
            // about recovery at this point, but still try
            AMFBufferPtr pBufferOut;
            AMF_RESULT err = m_spContext->AllocBuffer(AMF_MEMORY_HOST, avPacket.size, &pBufferOut);
            AMF_RETURN_IF_FAILED(err, L"QueryOutput() - AllocBuffer failed");

            amf_uint8* pMemOut = static_cast<amf_uint8*>(pBufferOut->GetNative());
            memcpy(pMemOut, avPacket.data, avPacket.size);

            // for PTS -always 100 nanos
            amf_pts duration = av_rescale_q(avPacket.duration, m_pCodecContext->time_base, AMF_TIME_BASE_Q);
            amf_pts pts = av_rescale_q(avPacket.pts, m_pCodecContext->time_base, AMF_TIME_BASE_Q);
            pBufferOut->SetProperty(AMF_VIDEO_ENCODER_PRESENTATION_TIME_STAMP, pts);

            // copy input data properties to the encoded packet
            // now here we have to be a bit careful as there's no
            // exact way to match input frame with output packet(s)
            // so we'll try to match based on pts and duration
            //               pts    duration
            //   frame m      |-----------------|
            //
            //                 pts
            //   packet n       |
            //
            // what happens in some cases is that the input pts is
            // a bit off from the resulting pts
            //               pts    duration
            //   frame m      |-----------------|
            //
            //             pts
            //   packet n   |
            //
            // so check and see if it's closer to the next packet,
            // pick that one
            std::list<AMFTransitFrame>::iterator transitFrame = m_inputData.begin();
            while (transitFrame != m_inputData.end())
            {
                const amf_pts          inFramePts = transitFrame->pts;
                const amf_pts          inFrameDuration = transitFrame->duration;

                // we found the proper frame to copy data from
                if (((pts >= inFramePts) && (pts <= inFramePts + inFrameDuration)) || pts < m_inputData.front().pts)
                {
                    // check though if the frame is closer to the next frame, in which
                    // case it should probably copy from that frame
                    std::list<AMFTransitFrame>::iterator itNext = std::next(transitFrame, 1);

                    if ((itNext != m_inputData.end()) &&
                        (abs((amf_int64)(*itNext).pts - (amf_int64)pts) < (pts - inFramePts)))
                    {
                        (*itNext).pStorage->CopyTo(pBufferOut, false);
                        transitFrame = itNext;
                    }
                    else
                    {
                        (*transitFrame).pStorage->CopyTo(pBufferOut, false);
                    }

                    // check key frame
                    if (avPacket.flags & AV_PKT_FLAG_KEY)
                    {
                        // Set output video frame type for muxer
                        switch (m_pCodecContext->codec_id)
                        {
                        case AV_CODEC_ID_H264:
                            pBufferOut->SetProperty(AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE, AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR);
                            break;
                        case AV_CODEC_ID_HEVC:
                            pBufferOut->SetProperty(AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE, AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE_IDR);
                            break;
                        case AV_CODEC_ID_AV1:
                            pBufferOut->SetProperty(AMF_VIDEO_ENCODER_AV1_OUTPUT_FRAME_TYPE, AMF_VIDEO_ENCODER_AV1_OUTPUT_FRAME_TYPE_KEY);
                            break;
                        }
                    }

                    pBufferOut->SetPts(pts);
                    // Duration could be 0 if unknown from packet
                    // In this case we use input duration instead
                    if (duration != 0)
                    {
                        pBufferOut->SetDuration(duration);
                    }
                    else
                    {
                        pBufferOut->SetDuration((*transitFrame).duration);
                    }
                    // when b frame is enabled
                    // set buffer pts as incremental pts in inputted order
                    // muxer needs it to calculate the correct dts
                    // set property PresentationTimeStamp as the real pts
                    if (m_pCodecContext->max_b_frames > 0)
                    {
                        pBufferOut->SetProperty(AMF_VIDEO_ENCODER_PRESENTATION_TIME_STAMP, pts);
                        pBufferOut->SetPts(m_inputpts.front());
                        m_inputpts.pop_front();
                    }
                    // remove if we find the matching input surface
                    transitFrame = m_inputData.erase(transitFrame);
                    AMFTraceDebug(AMF_FACILITY, L"QueryOutput(): output pts is %" LPRId64 L" input pts is  %" LPRId64 L" output frame count is %d",
                        pts, pBufferOut->GetPts(), m_videoFrameQueryCount);
                    break;
                }

                ++transitFrame;
                // the generated frame doesn't match any input frames
                // this should not really happen - trace
                if (transitFrame == m_inputData.end())
                {
                    AMFTraceWarning(AMF_FACILITY, L"Generated packet pts %" LPRId64 L" doesn't match any input frames %", pts);
                }
            }

            *ppData = pBufferOut.Detach();
            m_videoFrameQueryCount++;

        }
    }

    //
    // if we get a frame out from avcodec_receive_packet, we need
    // to repeat until we get an error that more input needs to
    // be submitted or we get EOF
    // our documentation states that OK will return a frame out,
    // while repeat says no frame came out so it's up to the caller
    // to repeat QueryOutput while getting AMF_OK

    return (*ppData != nullptr) ? AMF_OK : AMF_REPEAT;
};
//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL  BaseEncoderFFMPEGImpl::OnPropertyChanged(const wchar_t* pName)
{
    AMFLock lock(&m_Sync);

    const amf_wstring  name(pName);
    if (name == VIDEO_ENCODER_ENABLE_ENCODING)
    {
        GetProperty(VIDEO_ENCODER_ENABLE_ENCODING, &m_bEncodingEnabled);
        return;
    }
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  BaseEncoderFFMPEGImpl::InitializeFrame(AMFSurface* pInSurface, AVFrame& avFrame)
{
    AMF_RETURN_IF_FALSE(m_pCodecContext != nullptr, AMF_NOT_INITIALIZED, L"InitializeFrame() - Codec Context not Initialized");
    AMF_RETURN_IF_FALSE(pInSurface != nullptr, AMF_INVALID_ARG, L"InitializeFrame() - pInSurface == NULL");


    // setup the data pointers in the AVFrame
    for (amf_size ch = 0; ch < pInSurface->GetPlanesCount(); ch++)
    {
        avFrame.data[ch] = static_cast<uint8_t*>(pInSurface->GetPlaneAt(ch)->GetNative());
        avFrame.linesize[ch] = pInSurface->GetPlaneAt(ch)->GetHPitch();
    }

    // setup the basic encode properties
    AMF_FRAME_TYPE eFrameType = pInSurface->GetFrameType();
    if (eFrameType != AMF_FRAME_PROGRESSIVE)
    {
        avFrame.interlaced_frame = true;
        avFrame.top_field_first = ((eFrameType == AMF_FRAME_INTERLEAVED_ODD_FIRST) ? true : false);
    }

    avFrame.extended_data = avFrame.data;
    avFrame.format = m_pCodecContext->pix_fmt;
    avFrame.width = m_pCodecContext->width;
    avFrame.height = m_pCodecContext->height;
    avFrame.key_frame = 1;
    avFrame.pts = av_rescale_q(pInSurface->GetPts(), AMF_TIME_BASE_Q, m_pCodecContext->time_base);
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  BaseEncoderFFMPEGImpl::CodecContextInit(const wchar_t* name)
{
    // we set-up the information we want FFmpeg to use - now it's
    // time to get the codec and be ready to start encoding
    //
    // the second param codec is to open this context for. If a non - NULL codec has been
    // previously passed to avcodec_alloc_context3() or
    // for this context, then this parameter MUST be either NULL or
    // equal to the previously passed codec.
    if (m_pCodecContext->codec == 0 || avcodec_open2(m_pCodecContext, m_pCodecContext->codec, NULL) < 0)
    {
        Terminate();
        AMF_RETURN_IF_FALSE(false, AMF_CODEC_NOT_SUPPORTED, L"FFmpeg encoder codec id = %d failed to open", (int)m_CodecID);
    }

    // allocate a buffer to store the extra data
    // we can't store the m_pCodecContext->extradata
    // directly as we wouldn't know its size (in one property)
    if (m_pCodecContext->extradata_size != 0)
    {
        AMFBufferPtr spBuffer;
        AMF_RESULT err = m_spContext->AllocBuffer(AMF_MEMORY_HOST, m_pCodecContext->extradata_size, &spBuffer);
        if ((err == AMF_OK) && (spBuffer->GetNative() != nullptr))
        {
            memcpy(spBuffer->GetNative(), m_pCodecContext->extradata, m_pCodecContext->extradata_size);
            AMF_RETURN_IF_FAILED(SetProperty(name, spBuffer));
        }
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------