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

#include "VideoDecoderFFMPEGImpl.h"
#include "UtilsFFMPEG.h"

#include "public/include/core/Trace.h"
#include "public/common/TraceAdapter.h"
#include "public/include/components/VideoDecoderUVD.h"
#include "public/common/Thread.h"

#define AMF_FACILITY L"AMFVideoDecoderFFMPEGImpl"

using namespace amf;



//-------------------------------------------------------------------------------------------------
AMFVideoDecoderFFMPEGImpl::AMFVideoDecoderFFMPEGImpl(AMFContext* pContext)
  : m_pContext(pContext),
    m_bDecodingEnabled(true),
    m_bEof(false),
    m_pCodecContext(nullptr),
    m_SeekPts(0),
    m_videoFrameSubmitCount(0),
    m_videoFrameQueryCount(0),
    m_eFormat(AMF_SURFACE_UNKNOWN),
    m_FrameRate(AMFConstructRate(25,1)),
    m_CopyPipeline(&m_InputQueue)
{
    g_AMFFactory.Init();

    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoBool(VIDEO_DECODER_ENABLE_DECODING, L"Enable decoding", true, true),

        AMFPropertyInfoInt64(VIDEO_DECODER_CODEC_ID, L"Codec ID", AMF_STREAM_CODEC_ID_UNKNOWN, AMF_STREAM_CODEC_ID_UNKNOWN, INT_MAX, true),
        AMFPropertyInfoInterface(VIDEO_DECODER_EXTRA_DATA, L"Extra Data", NULL, true),
        AMFPropertyInfoSize(VIDEO_DECODER_RESOLUTION, L"Current size", AMFConstructSize(0, 0), AMFConstructSize(0, 0), AMFConstructSize(0x7fffffff, 0x7fffffff), false),
        AMFPropertyInfoInt64(VIDEO_DECODER_BITRATE, L"Bitrate", 0, 0, INT_MAX, true),
        AMFPropertyInfoRate(VIDEO_DECODER_FRAMERATE, L"Frame rate", 25, 1, false),
        AMFPropertyInfoInt64(VIDEO_DECODER_SEEK_POSITION, L"Seek Position", 0, 0, INT_MAX, true),
    AMFPrimitivePropertyInfoMapEnd

    InitFFMPEG();
}
//-------------------------------------------------------------------------------------------------
AMFVideoDecoderFFMPEGImpl::~AMFVideoDecoderFFMPEGImpl()
{
    Terminate();
    g_AMFFactory.Terminate();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::Init(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height)
{
    // check some required parameters
    AMF_RETURN_IF_FALSE(format != AMF_SURFACE_UNKNOWN, AMF_NOT_INITIALIZED, L"SubmitInput() - unknown surface format passed in");


    AMFLock lock(&m_sync);

    // clean up any information we might previously have
    Terminate();

    // reset the forced EOF flag
    m_bEof = false;

    // keep around the surface format - we will need
    // this later on when we get data out
    m_eFormat = format; //get from demuxer if not reinit

    // get the codec ID that was set
    amf_int64  codecID = 0;
    AMF_RETURN_IF_FAILED(GetProperty(VIDEO_DECODER_CODEC_ID, &codecID));
    amf_int64  codecAMF = GetFFMPEGVideoFormat((AMF_STREAM_CODEC_ID_ENUM)codecID);
    if (codecAMF != AV_CODEC_ID_NONE)
    {
        codecID = codecAMF;
    }
    if (codecID == AMF_CODEC_H265MAIN10)
    {
        codecID = AV_CODEC_ID_HEVC;
    }
    else if (codecID == AMF_CODEC_AV1_12BIT)
    {
        codecID = AV_CODEC_ID_AV1;
    }


    // find the correct codec
    const AVCodec *pCodec = avcodec_find_decoder((AVCodecID) codecID);
    AMF_RETURN_IF_FALSE(pCodec != nullptr, AMF_CODEC_NOT_SUPPORTED, L"FFmpeg codec %S is not supported", avcodec_get_name((AVCodecID) codecID));

    // allocate the codec context
    m_pCodecContext = avcodec_alloc_context3(pCodec);
    AMF_RETURN_IF_FALSE(m_pCodecContext != nullptr, AMF_CODEC_NOT_SUPPORTED, L"FFmpeg codec %S could not allocate codec context", avcodec_get_name((AVCodecID) codecID));



    AMFVariant val;
    AMF_RETURN_IF_FAILED(GetProperty(VIDEO_DECODER_EXTRA_DATA, &val));
    if (!val.Empty() && val.pInterface)
    {
        // NOTE: the buffer ptr. shouldn't disappear as the
        //       property holds it in the end, however it is 
        //       possible someone changes the property at 
        //       which point the extra data would go away...
        m_pExtraData = AMFBufferPtr(val.pInterface);
        m_pCodecContext->extradata = (uint8_t*) m_pExtraData->GetNative();
        m_pCodecContext->extradata_size = (int) m_pExtraData->GetSize();
    }

    amf_int64 bitrate = 0;
    GetProperty(VIDEO_DECODER_BITRATE, &bitrate);
    m_pCodecContext->bit_rate = bitrate;

    // make sure to fix the frame rate in case it
    // can cause a divide by 0 due to one term being 0
    GetProperty(VIDEO_DECODER_FRAMERATE, &m_FrameRate);
    if (m_FrameRate.num == 0)
    {
        m_FrameRate.num = 1;
    }
    if (m_FrameRate.den == 0)
    {
        m_FrameRate.den = 1;
    }

    m_pCodecContext->framerate.num = m_FrameRate.num;
    m_pCodecContext->framerate.den = m_FrameRate.den;
    m_pCodecContext->time_base.num = m_FrameRate.num;
    m_pCodecContext->time_base.den = m_FrameRate.den;
    m_pCodecContext->width = width;
    m_pCodecContext->height = height;

    AMFSize framesize = { width, height };
    SetProperty(VIDEO_DECODER_RESOLUTION, framesize);

    m_pCodecContext->thread_count = 8;
#ifdef _WIN32
    amf_int32 iCores = amf_get_cpu_cores();
    if (iCores > 1)
    {
        m_pCodecContext->thread_count = iCores;
    }
#endif

    //todo, expand to more codes
   const bool bImage = (codecID == AV_CODEC_ID_EXR) || (codecID == AV_CODEC_ID_PNG);
    if (bImage && (m_pCodecContext->codec->capabilities & AV_CODEC_CAP_SLICE_THREADS))
    {
        m_pCodecContext->thread_type = FF_THREAD_SLICE;
    }
    else if (m_pCodecContext->codec->capabilities & AV_CODEC_CAP_FRAME_THREADS)
    {
        m_pCodecContext->thread_type = FF_THREAD_FRAME;
    }
    else if (m_pCodecContext->codec->capabilities & AV_CODEC_CAP_SLICE_THREADS)
    {
        m_pCodecContext->thread_type = FF_THREAD_SLICE;
    }
    else
    {
        m_pCodecContext->thread_count = 1;
    }

//    avcodec_set_dimensions(m_pCodecContext, m_pCodecContext->width, m_pCodecContext->height);
//    ff_set_dimensions(m_pCodecContext, m_pCodecContext->width, m_pCodecContext->height);
    amf_int64 ret = av_image_check_size2(m_pCodecContext->width, m_pCodecContext->height, m_pCodecContext->max_pixels, AV_PIX_FMT_NONE, 0, m_pCodecContext);
    if (ret < 0)
    {
        m_pCodecContext->width = 0;
        m_pCodecContext->height = 0;
    }

    // For some codecs, such as msmpeg4 and mpeg4, width and height
    // MUST be initialized there because this information is not
    // available in the bitstream
    m_pCodecContext->coded_width  = m_pCodecContext->width;
    m_pCodecContext->coded_height = m_pCodecContext->height;
    m_pCodecContext->width        = AV_CEIL_RSHIFT(m_pCodecContext->width,  m_pCodecContext->lowres);
    m_pCodecContext->height       = AV_CEIL_RSHIFT(m_pCodecContext->height, m_pCodecContext->lowres);

    m_pCodecContext->sample_aspect_ratio.num = 1;
    m_pCodecContext->sample_aspect_ratio.den = 1;

    m_pCodecContext->strict_std_compliance = FF_COMPLIANCE_STRICT; // MM to try compliance

    if (avcodec_open2(m_pCodecContext, pCodec, NULL) < 0)
    {
        Terminate();
        AMF_RETURN_IF_FALSE(false, AMF_DECODER_NOT_PRESENT, L"FFmpeg codec %S failed to open", avcodec_get_name((AVCodecID) codecID));
    }
    GetProperty(VIDEO_DECODER_ENABLE_DECODING, &m_bDecodingEnabled);

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::ReInit(amf_int32 width, amf_int32 height)
{
    Terminate();
    return Init(m_eFormat, width, height);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::Terminate()
{
    AMFLock lock(&m_sync);

    // clear the internally stored buffers
    m_pExtraData = nullptr;
    m_inputData.clear();

    // clean-up other codec related items
    if (m_pCodecContext != nullptr)
    {
        AMFTraceInfo(AMF_FACILITY, L"Submitted %d, Queried %d", (int)m_videoFrameSubmitCount, (int)m_videoFrameQueryCount);

        avcodec_close(m_pCodecContext);
        av_free(m_pCodecContext);
        m_pCodecContext = nullptr;
    }

    m_videoFrameSubmitCount = 0;
    m_videoFrameQueryCount = 0;
    m_bEof = false;
    m_SeekPts = 0;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::Drain()
{
    AMFLock lock(&m_sync);

    m_bEof = true;
    
    return SubmitInput(nullptr);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::Flush()
{
    AMF_RESULT res = AMF_NOT_INITIALIZED;
    AMFLock lock(&m_sync);

    // need to flush the decoder
    // NOTE:  this function just releases any references the decoder might 
    //        keep internally, but the caller's references remain valid
    if (m_pCodecContext != nullptr)
    {
        avcodec_flush_buffers(m_pCodecContext);

        // clear the internally stored buffer
        m_inputData.clear();
        m_bEof = false;
        res = AMF_OK;
    }

    return res;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::SubmitInput(AMFData* pData)
{
    // check some required parameters
    AMF_RETURN_IF_FALSE(m_pCodecContext != NULL, AMF_NOT_INITIALIZED, L"SubmitInput() - Codec Context not Initialized");


    AMFLock lock(&m_sync);

    // if decoding's been disabled, no need to proceed any further
    if (!m_bDecodingEnabled)
    {
        return m_bEof ? AMF_EOF : AMF_OK;
    }

    // one problem we have is to match the properties of the of the 
    // input buffer coming in with the decompressed frame coming out
    // to that extent, it seems the DTS field of the AVPacket might
    // do the trick - for AVFrame it says that:
    //      * DTS copied from the AVPacket that triggered returning this frame. (if frame threading isn't used)
    //      * This is also the Presentation time of this AVFrame calculated from
    //      * only AVPacket.dts values without pts values.
    // since we don't care about the DTS when we create the AMFBuffer we 
    // can use the DTS as an ID so we can match infput and output data
    amf_int64  dtsId = 0;

    // start submitting information to the decoder
    // NOTE: it is possible for the decoder to need multiple input 
    //       frames before information comes out so we need to 
    //       accumulate the inforamtion to know how to properly 
    //       retrieve properties from the data going out
    int ret = 0;
    if (pData != nullptr)
    {
        AMFBufferPtr pVideoBuffer(pData);
        AMF_RETURN_IF_FALSE(pVideoBuffer != nullptr, AMF_INVALID_ARG, L"SubmitInput() - Input should be VideoBuffer");

        // the buffer we receive should already be in host memory - we open
        // files from disk, so this convert should be somewhat redundant
        // but we can leave it there, in case we ever receive some buffer
        // that's not in host memory, we could still work...
        AMF_RESULT err = pVideoBuffer->Convert(AMF_MEMORY_HOST);
        AMF_RETURN_IF_FAILED(err, L"SubmitInput() - Convert(AMF_MEMORY_HOST) failed");


        //
        // fill the packet information
        AVPacketEx  avpkt;

        avpkt.size = int(pVideoBuffer->GetSize());
        avpkt.data = static_cast<uint8_t*>(pVideoBuffer->GetNative());

        ReadAVPacketInfo(pVideoBuffer, &avpkt);

        // since filling the packet will overwrite the 
        // dts value, make sure we overwrite it afterwards 
        // with the ID we want to use
        if ((avpkt.pts == AV_NOPTS_VALUE) && (avpkt.dts == AV_NOPTS_VALUE))
        {
            avpkt.pts = pVideoBuffer->GetPts();
            avpkt.dts = avpkt.pts; 
        }

        dtsId = avpkt.dts;


        //
        // any debugging info we might want to set for the codec
        SubmitDebug(m_pCodecContext, avpkt);


        //
        // since the data that we receive in SubmitInput should always contain a full frame
        // we should probably never have a have the need to handle partial buffer data
        // as we do use avcodec_receive_frame in the demuxer, so if we get any of the errors
        // below then we probably did something wrong in the demuxer code:
        // 
        // AVERROR(EAGAIN): input is not accepted in the current state - 
        //                  user must read output with avcodec_receive_frame() 
        //                  (once all output is read, the packet should be resent, and the call will not fail with EAGAIN). 
        // AVERROR_EOF: the decoder has been flushed, and no new packets can be sent to it (also returned if more than 1 flush packet is sent) 
        // AVERROR(EINVAL): codec not opened, it is an encoder, or requires flush 
        // AVERROR(ENOMEM): failed to add packet to internal queue, or similar other errors: legitimate decoding errors
        ret = avcodec_send_packet(m_pCodecContext, &avpkt);
    }
    else
    {
        // update internal flag that we reached the end of the file
        m_bEof = true;

        // no more frames to encode so now it's time to drain the encoder
        // draining the encoder should happen once 
        // otherwise an error code will be returned by avcodec_send_frame
        // 
        // NOTE: to drain, we need to submit a NULL packet to avcodec_send_packet call
        //       Sending the first flush packet will return success. 
        //       Subsequent ones are unnecessary and will return AVERROR_EOF
        ret = avcodec_send_packet(m_pCodecContext, NULL);
    }


    //
    // handle the error returns from the decoder
    if (ret == AVERROR(EAGAIN))
    {
        return AMF_INPUT_FULL;
    }
    if (ret == AVERROR_EOF)
    {
        return AMF_EOF;
    }

    // once we get here, we should have no errors after we
    // submitted the encoded packet
    char  errBuffer[AV_ERROR_MAX_STRING_SIZE] = { 0 };
    AMF_RETURN_IF_FALSE(ret >= 0, AMF_FAIL, L"SubmitInput() - Error sending a packet for decoding - %S", av_make_error_string(errBuffer, sizeof(errBuffer)/sizeof(errBuffer[0]), ret));


    //
    // if we did have a frame to submit, and we succeeded
    // it's time to increment the submitted frames counter
    // we need to keep the input frame as we need to copy
    // properties from it in the output structure
    if (pData != nullptr)
    {
        AMFTransitFrame  transitFrame = { pData, dtsId };
        m_inputData.push_back(transitFrame);
        m_videoFrameSubmitCount++;
    }

    return (m_bEof || (ret == AVERROR_EOF)) ? AMF_EOF : AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::QueryOutput(AMFData** ppData)
{
    // check some required parameters
    AMF_RETURN_IF_FALSE(m_pCodecContext != NULL, AMF_NOT_INITIALIZED, L"QueryOutput() - Codec Context not Initialized");
    AMF_RETURN_IF_FALSE(ppData != NULL, AMF_INVALID_ARG, L"QueryOutput() - ppData == NULL");

    // initialize output
    *ppData = nullptr;


    AMFLock lock(&m_sync);


    //
    // decode
    AVFrameEx picture;  //MM: the memory allocated from preset buffers - no need to free

    // retrieve the decoded frame - it is possible that
    // for some frames, the decoder needs more data, in
    // which case it will return AVERROR(EAGAIN)
    // 
    // AVERROR(EAGAIN): output is not available in this state - 
    //                  user must try to send new input 
    // AVERROR_EOF: the decoder has been fully flushed, and there will be no more output frames 
    // AVERROR(EINVAL): codec not opened, or it is an encoder 
    // other negative values: legitimate decoding errors
    // 
    // avcodec_receive_frame, as long as it succeeds, needs to be called
    // repeatedly to get frames out, so in the case of we don't get a 
    // frame out, we need to return AMF_REPEAT so we get called again
    int ret = avcodec_receive_frame(m_pCodecContext, &picture);
    if (ret == AVERROR(EAGAIN))
    {
        // if we finished the frames in the decoder, we should get
        // out as there's nothing to put out and we don't need to remove 
        // any of the queued input frames
        // we also don't have to unref the frame as nothing got out
        // of the decoder at this point
        return AMF_REPEAT;
    }
    if (ret == AVERROR_EOF)
    {
        // if we're draining, we need to check until we get we get
        // AVERROR_EOF to know the decoder has been fully drained
        return AMF_EOF;
    }

    char  errBuffer[AV_ERROR_MAX_STRING_SIZE] = { 0 };
    AMF_RETURN_IF_FALSE(ret >= 0, AMF_FAIL, L"QueryOutput() - Error decoding frame - %S", av_make_error_string(errBuffer, sizeof(errBuffer)/sizeof(errBuffer[0]), ret));

    // a valid frame should also have a valid linesize
    AMF_RETURN_IF_FALSE(picture.linesize[0] > 0, AMF_FAIL, L"QueryOutput() - FFmpeg failed to return line size")


    //
    // we should find the correct matching buffer if we have one
    AMFData* pInputData = nullptr;
    while (m_inputData.empty() == false)
    {
        // if the current dts is less than what was produced by
        // the decoder, we are no longer interested in that frame
        const AMFTransitFrame& transitFrame = m_inputData.front();
        if (transitFrame.id < picture.pkt_dts)
        {
            m_inputData.pop_front();
            continue;
        }

        // if we found a matching input packet, return that
        if (transitFrame.id == picture.pkt_dts)
        {
            pInputData = transitFrame.pData;
            break;
        }

        // sometimes the last few frames don't have a dts and 
        // without that we can't really match them with an input frame
        if (picture.pkt_dts != AV_NOPTS_VALUE)
        {
            AMFTraceWarning(AMF_FACILITY, L"Decoded frame %" LPRId64 L" not matched to an input packet", picture.pkt_dts);
        }
        
        break;
    }


    // 
    // get some information from the original input frame
    // if no matching input frame, just pick any frame if
    // one exists - it's not using the pts from the frame
    // but one from the resulting picture - there's just
    // some rescaling data in the input frame when it comes in
    const amf_pts picPts = (pInputData != nullptr) ? GetPtsFromFFMPEG(AMFBufferPtr(pInputData), &picture) 
                                                   : (m_inputData.empty() == false) ? GetPtsFromFFMPEG(AMFBufferPtr(m_inputData.front().pData), &picture) : 0;
    if (picPts < m_SeekPts)
    {
        // we got a frame out, but it's before the point we
        // seeked to, so don't put that frame out
        return AMF_REPEAT;
    }


    //
    // allocate output frame to put out and copy the FFmpeg output data into
    // if the allocation fails, we have a bigger issue than trying to recover
    // from that, but try anyway
    AMF_RESULT err = AMF_OK;
    AMFSurfacePtr pSurfaceOut;
    if (m_pOutputDataCallback != nullptr)
    {
        err = m_pOutputDataCallback->AllocSurface(AMF_MEMORY_HOST, m_eFormat, m_pCodecContext->width, m_pCodecContext->height, 0, 0, &pSurfaceOut);
    }
    else
    {
        err = m_pContext->AllocSurface(AMF_MEMORY_HOST, m_eFormat, m_pCodecContext->width, m_pCodecContext->height, &pSurfaceOut);
    }
    AMF_RETURN_IF_FAILED(err, L"QueryOutput() - AllocSurface failed");


    //
    // update pts and duration
    amf_pts duration = (pInputData != nullptr) ? pInputData->GetDuration() : 0;
    if (duration == 0)
    {
        duration = amf_pts(AMF_SECOND * m_FrameRate.den / m_FrameRate.num);
    }
    pSurfaceOut->SetDuration(duration);
    pSurfaceOut->SetPts(picPts);

    // determine and set the frame type coming out
    AMF_FRAME_TYPE eFrameType = AMF_FRAME_PROGRESSIVE;
    if (picture.interlaced_frame)
    {
        eFrameType = picture.top_field_first ? AMF_FRAME_INTERLEAVED_EVEN_FIRST : AMF_FRAME_INTERLEAVED_ODD_FIRST;
        // unsupported???
        //AMF_FRAME_FIELD_SINGLE_EVEN                = 3,
        //AMF_FRAME_FIELD_SINGLE_ODD                = 4,
    }
    pSurfaceOut->SetFrameType(eFrameType);

    // copy the input frame properties to the frame going out
    if (pInputData != nullptr)
    {
        pInputData->CopyTo(pSurfaceOut, false);
    }


    amf_bool  bIsPlanar = (m_eFormat == AMF_SURFACE_RGBA) ? false : true;
    amf_int32 paddedLSB = (m_eFormat == AMF_SURFACE_P010) ? 6 :
                          (m_eFormat == AMF_SURFACE_P012) ? 4 :
                          (m_eFormat == AMF_SURFACE_P016) ? 0 : 0;
    amf_int32 iThreadCount = 2;


    //
    // handle the Y plane
    AMFPlanePtr pPlaneY = pSurfaceOut->GetPlane(AMF_PLANE_Y);
    AMF_RETURN_IF_INVALID_POINTER(pPlaneY, L"QueryOutput() - pPlaneY is NULL");

    if (pPlaneY->GetHeight() > 2160 && m_eFormat != AMF_SURFACE_P010 &&
                                       m_eFormat != AMF_SURFACE_P012 &&
                                       m_eFormat != AMF_SURFACE_P016)
    {
        CopyFrameThreaded(pPlaneY, picture, iThreadCount, false);
    }
    else if (picture.format == AV_PIX_FMT_YUV422P10LE)  //ProRes 10bit 4:2:2 from BM camera
    {
        CopyFrameYUV422(pPlaneY, picture);
        bIsPlanar = false;
    }
    else if (picture.format == AV_PIX_FMT_YUV444P10LE)  //YUV444
    {
        CopyFrameYUV444(pPlaneY, picture);
        bIsPlanar = false;
    }
    else if ((picture.format == AV_PIX_FMT_RGBA64LE) || //RGB -->RGBA
             (picture.format == AV_PIX_FMT_RGB48LE)  || //RGB -->RGBA
             (picture.format == AV_PIX_FMT_RGB48BE))    //RGB -->RGBA
    {
        CopyFrameRGB_FP16(pPlaneY, picture);
        pSurfaceOut->SetProperty(VIDEO_DECODER_COLOR_TRANSFER_CHARACTERISTIC, AMF_COLOR_TRANSFER_CHARACTERISTIC_LINEAR);
        bIsPlanar = false;
    }
    else if (pPlaneY->GetHPitch() == picture.linesize[0])   // AMF plane pitch and FFmpeg linesize match
    {
        CopyLineLSB((amf_uint8*) pPlaneY->GetNative(), picture.data[0], pPlaneY->GetHPitch() * pPlaneY->GetHeight(), paddedLSB);
    }
    else
    {
        amf_uint8 *pTmpMemOut = static_cast<amf_uint8*>(pPlaneY->GetNative());
        amf_uint8 *pTmpMemIn  = picture.data[0];
        amf_size  linesToCopy = pPlaneY->GetHeight();
        amf_size  to_copy     = AMF_MIN(pPlaneY->GetHPitch(), std::abs(picture.linesize[0]));

        while (linesToCopy > 0)
        {
            CopyLineLSB(pTmpMemOut, pTmpMemIn, to_copy, paddedLSB);
            pTmpMemOut += pPlaneY->GetHPitch();
            pTmpMemIn += picture.linesize[0];
            linesToCopy -= 1;
        }
    }


    //
    // handle the UV plane
    if (bIsPlanar)
    {
        AMFPlanePtr pPlaneUV = pSurfaceOut->GetPlane(AMF_PLANE_UV);
        AMF_RETURN_IF_INVALID_POINTER(pPlaneUV, L"QueryOutput() - pPlaneUV is NULL");

        if (pPlaneUV->GetHeight() > 2160 / 2 && m_eFormat != AMF_SURFACE_P010 &&
                                                m_eFormat != AMF_SURFACE_P012 &&
                                                m_eFormat != AMF_SURFACE_P016)
        {
            CopyFrameThreaded(pPlaneUV, picture, iThreadCount, true);
        }
        else
        {
            CopyFrameUV(pPlaneUV, picture, paddedLSB);
        }
    }


    //
    // determine and set the HDR information if available
    // or any color information we might have if no HDR info
    GetColorInfo(pSurfaceOut, picture);


    //
    // any debugging info we might want to process from the frame
    RetrieveDebug(m_pCodecContext, picture, pSurfaceOut);


    *ppData = pSurfaceOut.Detach();
    m_videoFrameQueryCount++;


    // if we get a frame out from avcodec_receive_frame, we need 
    // to repeat until we get an error that more input needs to 
    // be submitted or we get EOF
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::OnPropertyChanged(const wchar_t* pName)
{
    AMFLock lock(&m_sync);

    const amf_wstring  name(pName);
    if (name == VIDEO_DECODER_ENABLE_DECODING)
    {
        GetProperty(VIDEO_DECODER_ENABLE_DECODING, &m_bDecodingEnabled);
        return;
    }

    if (name == VIDEO_DECODER_SEEK_POSITION)
    {
        amf_pts  seekPts = 0;
        if (GetProperty(VIDEO_DECODER_SEEK_POSITION, &seekPts) == AMF_OK)
        {
            if (m_pCodecContext)
            {
                avcodec_flush_buffers(m_pCodecContext);
            }
            m_SeekPts = seekPts;
        }
    }
}


//
//
// protected
//
//

//-------------------------------------------------------------------------------------------------
amf_int64  AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::GetColorPrimaries(AVColorPrimaries colorPrimaries)
{
    amf_int64 eColorPrimariesAMF = AMF_COLOR_PRIMARIES_UNDEFINED;
    switch (colorPrimaries)
    {
    case AVCOL_PRI_RESERVED0:
        eColorPrimariesAMF = AMF_COLOR_PRIMARIES_UNDEFINED;
        break;
    case AVCOL_PRI_BT709:
        eColorPrimariesAMF = AMF_COLOR_PRIMARIES_BT709;
        break;
    case AVCOL_PRI_UNSPECIFIED:
        eColorPrimariesAMF = AMF_COLOR_PRIMARIES_UNSPECIFIED;
        break;
    case AVCOL_PRI_RESERVED:
        eColorPrimariesAMF = AMF_COLOR_PRIMARIES_RESERVED;
        break;
    case AVCOL_PRI_BT470M:
        eColorPrimariesAMF = AMF_COLOR_PRIMARIES_BT470M;
        break;
    case AVCOL_PRI_BT470BG:
        eColorPrimariesAMF = AMF_COLOR_PRIMARIES_BT470BG;
        break;
    case AVCOL_PRI_SMPTE170M:
        eColorPrimariesAMF = AMF_COLOR_PRIMARIES_SMPTE170M;
        break;
    case AVCOL_PRI_SMPTE240M:
        eColorPrimariesAMF = AMF_COLOR_PRIMARIES_SMPTE240M;
        break;
    case AVCOL_PRI_FILM:
        eColorPrimariesAMF = AMF_COLOR_PRIMARIES_FILM;
        break;
    case AVCOL_PRI_BT2020:
        eColorPrimariesAMF = AMF_COLOR_PRIMARIES_BT2020;
        break;
    case AVCOL_PRI_SMPTE428:
        eColorPrimariesAMF = AMF_COLOR_PRIMARIES_SMPTE428;
        break;
    case AVCOL_PRI_SMPTE431:
        eColorPrimariesAMF = AMF_COLOR_PRIMARIES_SMPTE431;
        break;
    case AVCOL_PRI_SMPTE432:
        eColorPrimariesAMF = AMF_COLOR_PRIMARIES_SMPTE432;
        break;
    case AVCOL_PRI_JEDEC_P22:
        eColorPrimariesAMF = AMF_COLOR_PRIMARIES_JEDEC_P22;
        break;
    case AVCOL_PRI_NB:
        break;
    }

    return eColorPrimariesAMF;
}
//-------------------------------------------------------------------------------------------------
amf_int64  AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::GetColorTransfer(AVColorTransferCharacteristic colorTransfer)
{
    amf_int64 eColorTransferAMF = AMF_COLOR_TRANSFER_CHARACTERISTIC_UNDEFINED;
    switch (colorTransfer)
    {
    case AVCOL_TRC_RESERVED0:
        eColorTransferAMF = AMF_COLOR_TRANSFER_CHARACTERISTIC_UNDEFINED;
        break;
    case AVCOL_TRC_BT709:
        eColorTransferAMF = AMF_COLOR_TRANSFER_CHARACTERISTIC_BT709;
        break;
    case AVCOL_TRC_UNSPECIFIED:
        eColorTransferAMF = AMF_COLOR_TRANSFER_CHARACTERISTIC_UNSPECIFIED;
        break;
    case AVCOL_TRC_RESERVED:
        eColorTransferAMF = AMF_COLOR_TRANSFER_CHARACTERISTIC_RESERVED;
        break;
    case AVCOL_TRC_GAMMA22:
        eColorTransferAMF = AMF_COLOR_TRANSFER_CHARACTERISTIC_GAMMA22;
        break;
    case AVCOL_TRC_GAMMA28:
        eColorTransferAMF = AMF_COLOR_TRANSFER_CHARACTERISTIC_GAMMA28;
        break;
    case AVCOL_TRC_SMPTE170M:
        eColorTransferAMF = AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE170M;
        break;
    case AVCOL_TRC_SMPTE240M:
        eColorTransferAMF = AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE240M;
        break;
    case AVCOL_TRC_LINEAR:
        eColorTransferAMF = AMF_COLOR_TRANSFER_CHARACTERISTIC_LINEAR;
        break;
    case AVCOL_TRC_LOG:
        eColorTransferAMF = AMF_COLOR_TRANSFER_CHARACTERISTIC_LOG;
        break;
    case AVCOL_TRC_LOG_SQRT:
        eColorTransferAMF = AMF_COLOR_TRANSFER_CHARACTERISTIC_LOG_SQRT;
        break;
    case AVCOL_TRC_IEC61966_2_4:
        eColorTransferAMF = AMF_COLOR_TRANSFER_CHARACTERISTIC_IEC61966_2_4;
        break;
    case AVCOL_TRC_BT1361_ECG:
        eColorTransferAMF = AMF_COLOR_TRANSFER_CHARACTERISTIC_BT1361_ECG;
        break;
    case AVCOL_TRC_IEC61966_2_1:
        eColorTransferAMF = AMF_COLOR_TRANSFER_CHARACTERISTIC_IEC61966_2_1;
        break;
    case AVCOL_TRC_BT2020_10:
        eColorTransferAMF = AMF_COLOR_TRANSFER_CHARACTERISTIC_BT2020_10;
        break;
    case AVCOL_TRC_BT2020_12:
        eColorTransferAMF = AMF_COLOR_TRANSFER_CHARACTERISTIC_BT2020_12;
        break;
    case AVCOL_TRC_SMPTE2084:
        eColorTransferAMF = AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE2084;
        break;
    case AVCOL_TRC_SMPTE428:
        eColorTransferAMF = AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE428;
        break;
    case AVCOL_TRC_ARIB_STD_B67:
        eColorTransferAMF = AMF_COLOR_TRANSFER_CHARACTERISTIC_ARIB_STD_B67;
        break;
    case AVCOL_TRC_NB:
        eColorTransferAMF = AMF_COLOR_TRANSFER_CHARACTERISTIC_UNDEFINED;
        break;
    }

    return eColorTransferAMF;
}
//-------------------------------------------------------------------------------------------------
amf_int64  AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::GetColorRange(AVColorRange colorRange)
{
    amf_int64 eColorRangeAMF = AMF_COLOR_RANGE_UNDEFINED;
    switch (colorRange)
    {
    case AVCOL_RANGE_UNSPECIFIED:
        eColorRangeAMF = AMF_COLOR_RANGE_UNDEFINED;
        break;
    case AVCOL_RANGE_MPEG:
        eColorRangeAMF = AMF_COLOR_RANGE_STUDIO;
        break;
    case AVCOL_RANGE_JPEG:
        eColorRangeAMF = AMF_COLOR_RANGE_FULL;
        break;
    case AVCOL_RANGE_NB:
        eColorRangeAMF = AMF_COLOR_RANGE_UNDEFINED;
        break;
    }

    return eColorRangeAMF;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::GetHDRInfo(const AVMasteringDisplayMetadata* pInFFmpegMetadata, AMFHDRMetadata* pAMFHDRInfo)
{
    AMF_RETURN_IF_INVALID_POINTER(pInFFmpegMetadata, L"GetHDRInfo() - pInFFmpegMetadata is NULL");
    AMF_RETURN_IF_INVALID_POINTER(pAMFHDRInfo, L"GetHDRInfo() - pAMFHDRInfo is NULL");


    pAMFHDRInfo->redPrimary[0] = amf_uint16(50000.f * float(pInFFmpegMetadata->display_primaries[0][0].num) / pInFFmpegMetadata->display_primaries[0][0].den);
    pAMFHDRInfo->redPrimary[1] = amf_uint16(50000.f * float(pInFFmpegMetadata->display_primaries[0][1].num) / pInFFmpegMetadata->display_primaries[0][1].den);

    pAMFHDRInfo->greenPrimary[0] = amf_uint16(50000.f * float(pInFFmpegMetadata->display_primaries[1][0].num) / pInFFmpegMetadata->display_primaries[1][0].den);
    pAMFHDRInfo->greenPrimary[1] = amf_uint16(50000.f * float(pInFFmpegMetadata->display_primaries[1][1].num) / pInFFmpegMetadata->display_primaries[1][1].den);

    pAMFHDRInfo->bluePrimary[0] = amf_uint16(50000.f * float(pInFFmpegMetadata->display_primaries[2][0].num) / pInFFmpegMetadata->display_primaries[2][0].den);
    pAMFHDRInfo->bluePrimary[1] = amf_uint16(50000.f * float(pInFFmpegMetadata->display_primaries[2][1].num) / pInFFmpegMetadata->display_primaries[2][1].den);

    pAMFHDRInfo->whitePoint[0] = amf_uint16(50000.f * float(pInFFmpegMetadata->white_point[0].num) / pInFFmpegMetadata->white_point[0].den);
    pAMFHDRInfo->whitePoint[1] = amf_uint16(50000.f * float(pInFFmpegMetadata->white_point[1].num) / pInFFmpegMetadata->white_point[1].den);

    pAMFHDRInfo->maxMasteringLuminance = amf_uint16(10000.f * float(pInFFmpegMetadata->max_luminance.num) / pInFFmpegMetadata->max_luminance.den);
    pAMFHDRInfo->minMasteringLuminance = amf_uint16(10000.f * float(pInFFmpegMetadata->min_luminance.num) / pInFFmpegMetadata->min_luminance.den);

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::GetColorInfo(AMFSurface* pSurfaceOut, const AVFrame& picture)
{
    AMF_RETURN_IF_INVALID_POINTER(pSurfaceOut, L"GetColorInfo() - pSurfaceOut is NULL");
    AMF_RETURN_IF_INVALID_POINTER(m_pContext, L"GetColorInfo() - m_pContext is NULL");


    // determine and set the HDR information if available
    // or any color information we might have if no HDR info
    AMFBufferPtr pHDRMetadata;
    const AVFrameSideData* sd = av_frame_get_side_data(&picture, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    if (sd != nullptr)
    {
        const AVMasteringDisplayMetadata* pMetadata = (AVMasteringDisplayMetadata*)sd->data;
        if ((pMetadata != nullptr) && 
            (pMetadata->max_luminance.num != 0) && (pMetadata->has_luminance))
        {
            m_pContext->AllocBuffer(AMF_MEMORY_HOST, sizeof(AMFHDRMetadata), &pHDRMetadata);
            if (pHDRMetadata != nullptr)
            {
                GetHDRInfo(pMetadata, (AMFHDRMetadata*)pHDRMetadata->GetNative());
                pSurfaceOut->SetProperty(AMF_VIDEO_DECODER_HDR_METADATA, AMFVariant((AMFInterface*)pHDRMetadata));
            }
        }
    }
    if (pHDRMetadata == nullptr)
    {
        const amf_int64 eColorPrimariesAMF = GetColorPrimaries(picture.color_primaries); 
        if (eColorPrimariesAMF != AMF_COLOR_PRIMARIES_UNDEFINED)
        {
            pSurfaceOut->SetProperty(AMF_VIDEO_COLOR_PRIMARIES, eColorPrimariesAMF);
        }

        const amf_int64 eColorTransferAMF = GetColorTransfer(picture.color_trc);
        if (eColorTransferAMF != AMF_COLOR_TRANSFER_CHARACTERISTIC_UNDEFINED)
        {
            pSurfaceOut->SetProperty(AMF_VIDEO_COLOR_TRANSFER_CHARACTERISTIC, eColorTransferAMF);
        }

        const amf_int64 eColorRangeAMF = GetColorRange(picture.color_range);
        if (eColorRangeAMF != AMF_COLOR_RANGE_UNDEFINED)
        {
            pSurfaceOut->SetProperty(AMF_VIDEO_COLOR_RANGE, eColorRangeAMF);
        }
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::CopyFrameThreaded(AMFPlane* pPlane, const AVFrame& picture, amf_int threadCount, bool isUVPlane)
{
    AMF_RETURN_IF_INVALID_POINTER(pPlane, L"CopyFrameThreaded() - pPlane is NULL");


    if (m_CopyPipeline.m_ThreadPool.size() == 0)
    {
        m_CopyPipeline.Start(threadCount, 0);
    }
    amf_long counter = threadCount;

    CopyTask task = {};

    task.pSrc = (!isUVPlane) ? picture.data[0] : picture.data[1];
    task.pSrc1 = (!isUVPlane) ? nullptr : picture.data[2];
    task.pDst = (amf_uint8*)pPlane->GetNative();
    task.SrcLineSize = (!isUVPlane) ? picture.linesize[0] : picture.linesize[1];
    task.DstLineSize = pPlane->GetHPitch();
    task.pEvent = &m_CopyPipeline.m_endEvent;
    task.pCounter = &counter;

    const amf_int planeHeight = pPlane->GetHeight();
    for (int i = 0; i < threadCount; i++)
    {
        task.lineStart = i * (planeHeight / threadCount);
        if (i < threadCount - 1)
        {
            task.lineEnd = task.lineStart + planeHeight / threadCount;
        }
        else
        {
            task.lineEnd = planeHeight;
        }
        m_InputQueue.Add(i, task);
    }
            
    m_CopyPipeline.m_endEvent.Lock(1000);

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::CopyFrameYUV422(AMFPlane* pPlane, const AVFrame& picture)
{
    AMF_RETURN_IF_INVALID_POINTER(pPlane, L"CopyFrameYUV444() - pPlane is NULL");


    amf_uint16 *pTmpMemY210 = static_cast<amf_uint16*>(pPlane->GetNative());
    const amf_uint16 *pTmpMemInY  = (const amf_uint16*)(picture.data[0]);
    const amf_uint16 *pTmpMemInV  = (const amf_uint16*)(picture.data[1]);
    const amf_uint16 *pTmpMemInU  = (const amf_uint16*)(picture.data[2]);
    const amf_size    linesToCopy = pPlane->GetHeight();
    const amf_size    uWidth      = pPlane->GetWidth() / 2;  //YUYV
    for (amf_size y = 0; y < linesToCopy; y++)
    {
        for (amf_size x = 0; x < uWidth; x++)  //UYVY
        {
            pTmpMemY210[4 * x + 0] = (pTmpMemInV[x] << 6) & 0xFFC0;        //U
            pTmpMemY210[4 * x + 1] = (pTmpMemInY[2 * x] << 6) & 0xFFC0;    //Y
            pTmpMemY210[4 * x + 2] = (pTmpMemInU[x] << 6) & 0xFFC0;        //V
            pTmpMemY210[4 * x + 3] = (pTmpMemInY[2 * x + 1] << 6) & 0xFFC0;//Y
        }
        pTmpMemInY  += picture.linesize[0] / sizeof(amf_uint16);
        pTmpMemInU  += picture.linesize[1] / sizeof(amf_uint16);
        pTmpMemInV  += picture.linesize[2] / sizeof(amf_uint16);
        pTmpMemY210 += pPlane->GetHPitch() / sizeof(amf_uint16);
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::CopyFrameYUV444(AMFPlane* pPlane, const AVFrame& picture)
{
    AMF_RETURN_IF_INVALID_POINTER(pPlane, L"CopyFrameYUV444() - pPlane is NULL");


    amf_uint16 *pTmpMemYUVA = static_cast<amf_uint16*>(pPlane->GetNative());
    const amf_uint16 *pTmpMemInY  = (const amf_uint16*)(picture.data[0]);
    const amf_uint16 *pTmpMemInU  = (const amf_uint16*)(picture.data[1]);
    const amf_uint16 *pTmpMemInV  = (const amf_uint16*)(picture.data[2]);
    const amf_size    linesToCopy = pPlane->GetHeight();
    const amf_size    uWidth      = pPlane->GetWidth();
    for (amf_size y = 0; y < linesToCopy; y++)
    {
        for (amf_size x = 0; x < uWidth; x++)
        {
            pTmpMemYUVA[4 * x + 0] = pTmpMemInU[x] << 6;
            pTmpMemYUVA[4 * x + 1] = pTmpMemInY[x] << 6;
            pTmpMemYUVA[4 * x + 2] = pTmpMemInV[x] << 6;
            pTmpMemYUVA[4 * x + 3] = 65535;
        }
        pTmpMemInY += picture.linesize[0] / sizeof(amf_uint16);
        pTmpMemInU += picture.linesize[1] / sizeof(amf_uint16);
        pTmpMemInV += picture.linesize[2] / sizeof(amf_uint16);
        pTmpMemYUVA += pPlane->GetHPitch() / sizeof(amf_uint16);
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::CopyFrameRGB_FP16(AMFPlane* pPlane, const AVFrame& picture)
{
    AMF_RETURN_IF_INVALID_POINTER(pPlane, L"CopyFrameRGB_FP16() - pPlane is NULL");

    const amf_int32  iPixelFormat = picture.format;           //pixel format
    const amf_size   uPitchIn     = picture.linesize[0];      //input pitch
    const amf_uint8* pMemIn       = picture.data[0];
    amf_uint8*       pMemOut      = static_cast<amf_uint8*>(pPlane->GetNative());
    const amf_size   uPitchOut    = pPlane->GetHPitch();      //out put pitch
    const amf_size   uWidth       = pPlane->GetWidth();       //frame width
    const amf_size   uHeight      = pPlane->GetHeight();      //frame height
    AMF_RESULT       ret          = AMF_OK;

    if (m_eFormat == AMF_SURFACE_RGBA)
    {
        const amf_uint8* pSrc = pMemIn;
        amf_uint8* pDst = pMemOut;
        for (amf_size y = 0; y < uHeight; y++)
        {
            for (amf_size x = 0; x < uWidth; x++)
            {
                if (iPixelFormat == AV_PIX_FMT_RGB48BE) //png
                {
                    pDst[4 * x + 0] = pSrc[6 * x + 0];
                    pDst[4 * x + 1] = pSrc[6 * x + 2];
                    pDst[4 * x + 2] = pSrc[6 * x + 4];
                    pDst[4 * x + 3] = 255;
                }
                else if (iPixelFormat == AV_PIX_FMT_RGB48LE) //EXR
                {
                    pDst[4 * x + 0] = pSrc[6 * x + 1];
                    pDst[4 * x + 1] = pSrc[6 * x + 3];
                    pDst[4 * x + 2] = pSrc[6 * x + 5];
                    pDst[4 * x + 3] = 255;
                }
                else if (iPixelFormat == AV_PIX_FMT_RGBA64LE) //EXR
                {
                    pDst[4 * x + 0] = pSrc[8 * x + 1];
                    pDst[4 * x + 1] = pSrc[8 * x + 3];
                    pDst[4 * x + 2] = pSrc[8 * x + 5];
                    pDst[4 * x + 3] = pSrc[8 * x + 7];
                }
                else
                {
                    pDst[4 * x + 0] = pSrc[6 * x + 1];
                    pDst[4 * x + 1] = pSrc[6 * x + 3];
                    pDst[4 * x + 2] = pSrc[6 * x + 5];
                }
            }
            pDst += uPitchOut;
            pSrc += uPitchIn;
        }
    }
    else if (m_eFormat == AMF_SURFACE_RGBA_F16)
    {
        const amf_uint16 *pSrc = (const amf_uint16 *) pMemIn;
        amf_uint16 *pDst = (amf_uint16 *)pMemOut;
        if (iPixelFormat == AV_PIX_FMT_RGBA64LE) //EXR, beter performance
        {
            const amf_size uLineWidth = uWidth * 4 * sizeof(amf_uint16);
            for (amf_size y = 0; y < uHeight; y++)
            {
                memcpy(pDst, pSrc, uLineWidth);
                pDst += uPitchOut / sizeof(amf_uint16);
                pSrc += uPitchIn / sizeof(amf_uint16);
            }
        }
        else
        {
            for (amf_size y = 0; y < uHeight; y++)
            {
                for (amf_size x = 0; x < uWidth; x++)
                {
                    if (iPixelFormat == AV_PIX_FMT_RGB48BE) //png
                    {
                        amf_uint8* pTemp = 0;
                        pTemp = (amf_uint8*)&pSrc[3 * x + 0];
                        pDst[4 * x + 0] = (pTemp[0] << 8) | pTemp[1];
                        pTemp = (amf_uint8*)&pSrc[3 * x + 1];
                        pDst[4 * x + 1] = (pTemp[0] << 8) | pTemp[1];
                        pTemp = (amf_uint8*)&pSrc[3 * x + 2];
                        pDst[4 * x + 2] = (pTemp[0] << 8) | pTemp[1];
                        pDst[4 * x + 3] = 65535;
                    }
                    else if (iPixelFormat == AV_PIX_FMT_RGB48LE) //EXR
                    {
                        pDst[4 * x + 0] = pSrc[3 * x + 0];
                        pDst[4 * x + 1] = pSrc[3 * x + 1];
                        pDst[4 * x + 2] = pSrc[3 * x + 2];
                        pDst[4 * x + 3] = 65535;
                    }
                    else if (iPixelFormat == AV_PIX_FMT_RGBA64LE) //EXR
                    {
                        pDst[4 * x + 0] = pSrc[4 * x + 0];
                        pDst[4 * x + 1] = pSrc[4 * x + 1];
                        pDst[4 * x + 2] = pSrc[4 * x + 2];
                        pDst[4 * x + 3] = pSrc[4 * x + 3];
                    }
                }
                pDst += uPitchOut / sizeof(amf_uint16);
                pSrc += uPitchIn / sizeof(amf_uint16);
            }
        }
    }
    else
    {
        ret = AMF_NOT_SUPPORTED;
    }

    return ret;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::CopyFrameUV(AMFPlane* pPlaneUV, const AVFrame& picture, amf_int32 paddedLSB)
{
    AMF_RETURN_IF_INVALID_POINTER(pPlaneUV, L"CopyFrameUV() - pPlaneUV is NULL");


    const amf_size   uHeight    = pPlaneUV->GetHeight();
    const amf_size   uWidth     = pPlaneUV->GetWidth();
    const amf_int32  iOutStride = pPlaneUV->GetHPitch();

    amf_uint8* pTmpMemOut   = static_cast<amf_uint8*>(pPlaneUV->GetNative());
    amf_uint8* pTmpMemIn[2] = { picture.data[1], picture.data[2] };

    // need to pack uv plane properly for 16-bit colour
    if (pPlaneUV->GetPixelSizeInBytes() == 4)
    {
        for (amf_size y = 0; y < uHeight; y++)
        {
            amf_uint16* pLineMemOut = (amf_uint16*) pTmpMemOut;
            amf_uint16* pLineMemIn1 = (amf_uint16*) pTmpMemIn[0];
            amf_uint16* pLineMemIn2 = (amf_uint16*) pTmpMemIn[1];
            for (amf_size x = 0; x < uWidth; x++)
            {
                // FFMPEG outputs in LSB format but we want MSB
                // 10-bit example:
                //     (LSB)         :  000000DD DDDDDDDD
                //     we want (MSB) :  DDDDDDDD DD000000
                *pLineMemOut++ = *pLineMemIn1++ << paddedLSB;
                *pLineMemOut++ = *pLineMemIn2++ << paddedLSB;
            }
            pTmpMemOut += iOutStride;
            pTmpMemIn[0] += picture.linesize[1];
            pTmpMemIn[1] += picture.linesize[2];
        }
    }
    else
    {
        for (amf_size y = 0; y < uHeight; y++)
        {
            amf_uint8* pLineMemOut = pTmpMemOut;
            amf_uint8* pLineMemIn1 = pTmpMemIn[0];
            amf_uint8* pLineMemIn2 = pTmpMemIn[1];
            for (amf_size x = 0; x < uWidth; x++)
            {
                *pLineMemOut++ = *pLineMemIn1++;
                *pLineMemOut++ = *pLineMemIn2++;
            }
            pTmpMemOut += iOutStride;
            pTmpMemIn[0] += picture.linesize[1];
            pTmpMemIn[1] += picture.linesize[2];
        }
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::CopyLineLSB(amf_uint8* pMemOut, const amf_uint8* pMemIn, amf_size sizeToCopy, amf_int32 paddedLSB)
{
    // NOTE: fact is paddedLSB for AMF_SURFACE_P016 is 0 so in that 
    //       case, it might be faster to go through the direct memcpy
//  if (m_eFormat == AMF_SURFACE_P010 ||
//      m_eFormat == AMF_SURFACE_P012 ||
//      m_eFormat == AMF_SURFACE_P016)
    if (paddedLSB > 0)
    {
        // FFMPEG outputs in LSB format but we want MSB
        // 10-bit example:
        //     (LSB)         :  000000DD DDDDDDDD
        //     we want (MSB) :  DDDDDDDD DD000000
        // modifying picture data directly messes up the 
        // decoder big time so we have to do it ourselves
        // by copying the data properly
        const amf_uint16* pTmp16Src  = (const amf_uint16 *)pMemIn;
              amf_uint16* pTmp16Dest = (amf_uint16 *)pMemOut;

        for (amf_size lineIdx = 0; lineIdx < sizeToCopy >> 1; lineIdx++)
        {
            pTmp16Dest[lineIdx] = pTmp16Src[lineIdx] << paddedLSB;
        }
    }
    else
    {
        memcpy(pMemOut, pMemIn, sizeToCopy);
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
