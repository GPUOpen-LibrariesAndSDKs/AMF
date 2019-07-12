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

#include "public/include/core/Context.h"
#include "public/include/core/Trace.h"
#include "public/common/TraceAdapter.h"
#include "public/include/components/ColorSpace.h"

#define AMF_FACILITY L"AMFVideoDecoderFFMPEGImpl"

extern "C"
{
#include "libavutil/imgutils.h"
}

using namespace amf;


//-------------------------------------------------------------------------------------------------
AMFVideoDecoderFFMPEGImpl::AMFVideoDecoderFFMPEGImpl(AMFContext* pContext)
  : m_pContext(pContext),
    m_bDecodingEnabled(true),
    m_bForceEof(false),
    m_pCodecContext(NULL),
    m_SeekPts(0),
    m_iLastDataOffset(0),
    m_ptsLastDataOffset(0),
    m_videoFrameSubmitCount(0),
    m_videoFrameQueryCount(0),
    m_eFormat(AMF_SURFACE_UNKNOWN),
    m_FrameRate(AMFConstructRate(25,1)),
    m_CopyPipeline(&m_InputQueue)
{
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
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::Init(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height)
{
    AMFLock lock(&m_sync);

    // clean up any information we might previously have
    Terminate();

    // reset the forced EOF flag
    m_bForceEof = false;


    // get the codec ID that was set
    amf_int64  codecID = 0;
    AMF_RETURN_IF_FAILED(GetProperty(VIDEO_DECODER_CODEC_ID, &codecID));
    amf_int64  codecAMF = GetFFMPEGVideoFormat((AMF_STREAM_CODEC_ID_ENUM)codecID);
    if (codecAMF != 0)
    {
        codecID = codecAMF;
    }

    // find the correct codec
    AVCodec *codec = avcodec_find_decoder((AVCodecID) codecID);
    if (!codec)
    {
        Terminate();
        AMF_RETURN_IF_FALSE(false, AMF_CODEC_NOT_SUPPORTED, L"FFmpeg codec %d is not supported", (int)codecID);
    }
    
    // allocate the codec context
    m_pCodecContext = avcodec_alloc_context3(codec);

    av_init_packet(&m_avpkt);

    AMFVariant val;
    AMF_RETURN_IF_FAILED(GetProperty(VIDEO_DECODER_EXTRA_DATA, &val));
    if (!val.Empty() && val.pInterface)
    {
        // NOTE: the buffer ptr. shouldn't disappear as the 
        //       property holds it in the end
        pExtraData = AMFBufferPtr(val.pInterface);
        m_pCodecContext->extradata = (uint8_t*)pExtraData->GetNative();
        m_pCodecContext->extradata_size = (int)pExtraData->GetSize();
    }

    amf_int64 bitrate = 0;
    GetProperty(VIDEO_DECODER_BITRATE, &bitrate);
    m_pCodecContext->bit_rate = bitrate;

    GetProperty(VIDEO_DECODER_FRAMERATE, &m_FrameRate);

    m_pCodecContext->framerate.num = m_FrameRate.num;
    m_pCodecContext->framerate.den = m_FrameRate.den;
    m_pCodecContext->time_base.num = m_FrameRate.num;
    m_pCodecContext->time_base.den = m_FrameRate.den;
    m_pCodecContext->width = width;
    m_pCodecContext->height = height;
    AMFSize framesize = { width, height };
    SetProperty(VIDEO_DECODER_RESOLUTION, framesize);

    m_pCodecContext->thread_count = 8;
//    m_pCodecContext->thread_count = std::thread::hardware_concurrency();
#ifdef _WIN32
    //query the number of CPU HW cores
    DWORD len = 0;
    GetLogicalProcessorInformation(NULL, &len);
    amf_int32 count = len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION* pBuffer = new SYSTEM_LOGICAL_PROCESSOR_INFORMATION[count];
    if (pBuffer)
    {
        GetLogicalProcessorInformation(pBuffer, &len);
        count = len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        amf_int32 iCores = 0;
        for (amf_int32 idx = 0; idx < count; idx++)
        {
            if (pBuffer[idx].Relationship == RelationProcessorCore)
            {
                iCores++;
            }
        }
        m_pCodecContext->thread_count = iCores;
        delete pBuffer;
    }
#endif

    //todo, expand to more codes
    bool bImage = (codecID == AV_CODEC_ID_EXR) || (codecID == AV_CODEC_ID_PNG);

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

    m_eFormat = format; //get from demuxer

//    avcodec_set_dimensions(m_pCodecContext, m_pCodecContext->width, m_pCodecContext->height);
//    ff_set_dimensions(m_pCodecContext, m_pCodecContext->width, m_pCodecContext->height);
    amf_int64 ret = av_image_check_size2(m_pCodecContext->width, m_pCodecContext->height, m_pCodecContext->max_pixels, AV_PIX_FMT_NONE, 0, m_pCodecContext);
    if (ret < 0)
        m_pCodecContext->width = m_pCodecContext->height = 0;

    m_pCodecContext->coded_width  = m_pCodecContext->width;
    m_pCodecContext->coded_height = m_pCodecContext->height;
    m_pCodecContext->width        = AV_CEIL_RSHIFT(m_pCodecContext->width,  m_pCodecContext->lowres);
    m_pCodecContext->height       = AV_CEIL_RSHIFT(m_pCodecContext->height, m_pCodecContext->lowres);


    m_pCodecContext->sample_aspect_ratio.num = 1;
    m_pCodecContext->sample_aspect_ratio.den = 1;

    m_pCodecContext->strict_std_compliance = FF_COMPLIANCE_STRICT; // MM to try compliance

    if (avcodec_open2(m_pCodecContext, codec, NULL) < 0)
    {
        Terminate();
        AMF_RETURN_IF_FALSE(false, AMF_CODEC_NOT_SUPPORTED, L"FFmpeg codec %d failed to open", (int)codecID);
        return AMF_DECODER_NOT_PRESENT;
    }
    GetProperty(VIDEO_DECODER_ENABLE_DECODING, &m_bDecodingEnabled);

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::ReInit(amf_int32 width, amf_int32 height)
{
    Terminate();
    return Init(AMF_SURFACE_UNKNOWN, width, height);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::Terminate()
{
    AMFLock lock(&m_sync);

    // clear the internally stored buffer
    m_pInputData = nullptr;

    // clean-up codec related items
    if (m_pCodecContext != NULL)
    {
        AMFTraceInfo(AMF_FACILITY, L"Submitted %d, Queried %d", (int)m_videoFrameSubmitCount, (int)m_videoFrameQueryCount);

        avcodec_close(m_pCodecContext);
        av_free(m_pCodecContext);
        m_pCodecContext = NULL;
        m_SeekPts = 0;
    }
    m_ptsLastDataOffset = 0;
    m_iLastDataOffset = 0;

    m_videoFrameSubmitCount = 0;
    m_videoFrameQueryCount = 0;
    m_bForceEof = false;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::Drain()
{
    AMFLock lock(&m_sync);

    m_bForceEof = true;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::Flush()
{
    AMFLock lock(&m_sync);

    // clear the internally stored buffer
    m_pInputData = nullptr;
    m_bForceEof = false;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::SubmitInput(AMFData* pData)
{
    // check some required parameters
    AMF_RETURN_IF_FALSE(m_pCodecContext != NULL, AMF_NOT_INITIALIZED, L"SubmitInput() - Codec Context not Initialized");


    AMFLock lock(&m_sync);

    // if the buffer is already full, we can't set more data
    if (m_pInputData && pData)
    {
        return AMF_INPUT_FULL;
    }

    // if the internal buffer is empty and the in buffer is also empty,
    // we reached the end of the available input...
    if (!m_pInputData && !pData)
    {
        return AMF_EOF;
    }

    // if the internal buffer is empty and we got new data coming in
    // update internal buffer with new information
    if (!m_pInputData && pData)
    {
        m_ptsLastDataOffset = 0;
        m_pInputData = AMFBufferPtr(pData);
        AMF_RETURN_IF_FALSE(m_pInputData != 0, AMF_INVALID_ARG, L"SubmitInput() - Input should be Buffer");

        AMF_RESULT err = m_pInputData->Convert(AMF_MEMORY_HOST);
        AMF_RETURN_IF_FAILED(err, L"SubmitInput() - Convert(AMF_MEMORY_HOST) failed");
        m_videoFrameSubmitCount++;
    }
                
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::QueryOutput(AMFData** ppData)
{
    // check some required parameters
    AMF_RETURN_IF_FALSE(m_pCodecContext != NULL, AMF_NOT_INITIALIZED, L"GetOutput() - Codec Context not Initialized");
    AMF_RETURN_IF_FALSE(ppData != NULL, AMF_INVALID_ARG, L"GetOutput() - ppData == NULL");
    // initialize output
    *ppData = NULL;
    AMFLock lock(&m_sync);

    // if decoding's been disabled, no need to proceed any further
    if (!m_bDecodingEnabled)
    {
        return m_bForceEof ? AMF_EOF : AMF_OK;
    }
    // if we requested to drain, see if we have any more packets
    if (m_bForceEof && !m_pInputData)
    {
        return AMF_EOF;
    }

    // if the input buffer is empty, we need more data
    if (!m_pInputData)
    {
        return AMF_REPEAT;
    }
    AMF_RESULT err = AMF_OK;
    int got_picture = 0;
    AMFBufferPtr pInBuffer(m_pInputData);

    uint8_t* pMemIn = NULL;
    amf_size uiMemSizeIn = 0;
    amf_pts pts = 0;
    amf_pts duration = 0;

    if (pInBuffer != NULL)
    {
        pMemIn = static_cast<uint8_t*>(pInBuffer->GetNative());
        uiMemSizeIn = pInBuffer->GetSize();
        pts = pInBuffer->GetPts();
        duration = pInBuffer->GetDuration();
    }
    AVFrame picture;  //MM: the memory allocated from preset buffers - no need to free
    memset(&picture, 0, sizeof(picture));
    av_frame_unref(&picture);

    m_avpkt.size = int(uiMemSizeIn);
    m_avpkt.data = (uint8_t *)pMemIn;

    bool bAVPacketRead = ReadAVPacketInfo(pInBuffer, &m_avpkt);

    int ret = avcodec_decode_video2(m_pCodecContext, &picture, &got_picture, &m_avpkt);
    m_pInputData = nullptr;

    if (got_picture)
    {
        m_videoFrameQueryCount++;
    }

    if (ret < 0)
    {
        return AMF_FAIL;
    }
    amf_pts picPts = pts;

    if (got_picture && bAVPacketRead)
    {
        picPts = GetPtsFromFFMPEG(pInBuffer, &picture);
    }

    if (!got_picture)
    {
        if (m_bForceEof)
        {
            return AMF_EOF;
        }
        return AMF_OK;
    }

    if (picPts < m_SeekPts)
    {
        return AMF_OK;
    }

    if (m_eFormat == AMF_SURFACE_UNKNOWN)
    {
        return AMF_OK;
    }

    AMF_RETURN_IF_FALSE(picture.linesize[0] > 0, AMF_FAIL, L"FFmpeg failed to return line size")
    AMFSurfacePtr pSurfaceOut;
    if (m_pOutputDataCallback != NULL)
    {
        err = m_pOutputDataCallback->AllocSurface(AMF_MEMORY_HOST, m_eFormat, m_pCodecContext->width, m_pCodecContext->height, 0, 0, &pSurfaceOut);
    }
    else
    {
        err = m_pContext->AllocSurface(AMF_MEMORY_HOST, m_eFormat, m_pCodecContext->width, m_pCodecContext->height, &pSurfaceOut);
    }
    AMF_RETURN_IF_FAILED(err, L"AllocSurface failed");

    //// FFMPEG outputs in 10-bit LSB format: 000000DD DDDDDDDD
    //// we want 10-bit MSB format: DDDDDDDD DD000000
    ////if (Is10BitYUV(picture.format))
    //// can't do this - modifying picture data directly messes up the decoder big time
    //if (PIX_FMT_YUV420P10LE == picture.format) 
    //{
    //    const amf_uint8 numPlanes = 3;

    //    const unsigned int yPlaneSize = picture.height * picture.width;
    //    const unsigned int uvPlaneSize = (picture.height >> 1) * (picture.width >> 1);

    //    for (amf_uint8 planeIdx = 0; planeIdx < numPlanes; planeIdx++)
    //    {
    //        amf_uint16 *pPlaneData = (amf_uint16 *)picture.data[planeIdx];
    //        unsigned int planeSize = (planeIdx > 0) ? (uvPlaneSize) : (yPlaneSize);

    //        for (unsigned int pixIdx = 0; pixIdx < planeSize; pixIdx++)
    //        {
    //            pPlaneData[pixIdx] = pPlaneData[pixIdx] << 6;
    //        }
    //    }
    //}

    bool bIsPlanar = (m_eFormat == AMF_SURFACE_RGBA) ? false : true;
    int iThreadCount = 2;
    {
        AMFPlanePtr plane = pSurfaceOut->GetPlane(AMF_PLANE_Y);

        if (plane->GetHeight() > 2160 && m_eFormat != AMF_SURFACE_P010)
        {
            if (m_CopyPipeline.m_ThreadPool.size() == 0)
            {
                m_CopyPipeline.Start(iThreadCount, 0);
            }
            amf_long counter = iThreadCount;

            CopyTask task = {};

            task.pSrc = picture.data[0];
            task.pSrc1 = nullptr;
            task.pDst = (amf_uint8*)plane->GetNative();
            task.SrcLineSize = picture.linesize[0];
            task.DstLineSize = plane->GetHPitch();
            task.pEvent = &m_CopyPipeline.m_endEvent;
            task.pCounter = &counter;

            for (int i = 0; i < iThreadCount; i++)
            {
                task.lineStart = i * (plane->GetHeight() / iThreadCount);
                if (i < iThreadCount - 1)
                {
                    task.lineEnd = task.lineStart + plane->GetHeight() / iThreadCount;
                }
                else
                {
                    task.lineEnd = plane->GetHeight();
                }
                m_InputQueue.Add(i, task);
            }
            if (m_CopyPipeline.m_endEvent.Lock(1000) == false)
            {
                // timout 
                int a = 1;
            }
        }
        else
        {
            if (picture.format == AV_PIX_FMT_YUV422P10LE)  //ProRes 10bit 4:2:2 from BM camera 
            {
                amf_uint16 *pTmpMemY210 = static_cast<amf_uint16*>(plane->GetNative());
                amf_uint16 *pTmpMemInY = (amf_uint16*)(picture.data[0]);
                amf_uint16 *pTmpMemInV = (amf_uint16*)(picture.data[1]);
                amf_uint16 *pTmpMemInU = (amf_uint16*)(picture.data[2]);
                amf_size uWidth = plane->GetWidth() / 2;  //YUYV
                amf_size linesToCopy = plane->GetHeight();
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
                    pTmpMemY210 += plane->GetHPitch() / sizeof(amf_uint16);
                }
                bIsPlanar = false;
            }
            else if (plane->GetHPitch() == picture.linesize[0])
            {
                if (m_eFormat == AMF_SURFACE_P010)
                {
                    amf_uint16 *pTmp16Src = (amf_uint16 *)picture.data[0];
                    amf_uint16 *pTmp16Dest = (amf_uint16 *)(plane->GetNative());

                    for (amf_int32 wordIdx = 0; wordIdx < (plane->GetHPitch() * plane->GetHeight()) >> 1; wordIdx++)
                    {
                        pTmp16Dest[wordIdx] = pTmp16Src[wordIdx] << 6;
                    }
                }
                else
                {
                    memcpy(plane->GetNative(), picture.data[0], plane->GetHPitch() * plane->GetHeight());
                }
            }
            else
            {
                amf_uint8 *pTmpMemOut = static_cast<amf_uint8*>(plane->GetNative());
                amf_uint8 *pTmpMemIn = picture.data[0];
                amf_size to_copy = AMF_MIN(plane->GetHPitch(), std::abs(picture.linesize[0]));
                amf_size linesToCopy = plane->GetHeight();

                bool is10BitCodec = m_eFormat == AMF_SURFACE_P010;

                if ((picture.format == AV_PIX_FMT_RGBA64LE) || //RGB -->RGBA
                    (picture.format == AV_PIX_FMT_RGB48LE)  || //RGB -->RGBA
                    (picture.format == AV_PIX_FMT_RGB48BE))    //RGB -->RGBA
                {
                    CopyFrameRGB_FP16(pTmpMemOut, pTmpMemIn, picture.format, picture.linesize[0], plane->GetHPitch(), plane->GetWidth(), linesToCopy);
                    pSurfaceOut->SetProperty(VIDEO_DECODER_COLOR_TRANSFER_CHARACTERISTIC, AMF_COLOR_TRANSFER_CHARACTERISTIC_LINEAR);
                    bIsPlanar = false;
                }
                else if (picture.format == AV_PIX_FMT_YUV444P10LE) //YUV444
                {
                    amf_uint16 *pTmpMemYUVA = static_cast<amf_uint16*>(plane->GetNative());
                    amf_uint16 *pTmpMemInY = (amf_uint16*)(picture.data[0]);
                    amf_uint16 *pTmpMemInU = (amf_uint16*)(picture.data[1]);
                    amf_uint16 *pTmpMemInV = (amf_uint16*)(picture.data[2]);
                    amf_size uWidth = plane->GetWidth();
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
                        pTmpMemYUVA += plane->GetHPitch() / sizeof(amf_uint16);
                    }
                    bIsPlanar = false;
                }
                else
                {
                    while (linesToCopy > 0)
                    {
                        if (is10BitCodec)
                        {
                            // FFMPEG outputs in 10-bit LSB format: 000000DD DDDDDDDD
                            // we want 10-bit MSB format: DDDDDDDD DD000000

                            amf_uint16 *pTmp16Src = (amf_uint16 *)pTmpMemIn;
                            amf_uint16 *pTmp16Dest = (amf_uint16 *)pTmpMemOut;

                            for (amf_size lineIdx = 0; lineIdx < to_copy >> 1; lineIdx++)
                            {
                                pTmp16Dest[lineIdx] = pTmp16Src[lineIdx] << 6;
                            }
                        }
                        else
                        {
                            memcpy(pTmpMemOut, pTmpMemIn, to_copy);
                        }

                        pTmpMemOut += plane->GetHPitch();
                        pTmpMemIn += picture.linesize[0];
                        linesToCopy -= 1;
                    }
                }
            }
        }
    }

    if (bIsPlanar)
    {
        AMFPlanePtr plane = pSurfaceOut->GetPlane(AMF_PLANE_UV);

        if (plane->GetHeight() > 2160 / 2 && m_eFormat != AMF_SURFACE_P010)
        {
            if (m_CopyPipeline.m_ThreadPool.size() == 0)
            {
                m_CopyPipeline.Start(iThreadCount, 0);
            }
            amf_long counter = iThreadCount;


            CopyTask task;

            task.pSrc = picture.data[1];
            task.pSrc1 = picture.data[2];
            task.pDst = (amf_uint8*)plane->GetNative();
            task.SrcLineSize = picture.linesize[1];
            task.DstLineSize = plane->GetHPitch();
            task.pEvent = &m_CopyPipeline.m_endEvent;
            task.pCounter = &counter;

            for (int i = 0; i < iThreadCount; i++)
            {
                task.lineStart = i * (plane->GetHeight() / iThreadCount);
                if (i < iThreadCount - 1)
                {
                    task.lineEnd = task.lineStart + plane->GetHeight() / iThreadCount;
                }
                else
                {
                    task.lineEnd = plane->GetHeight();
                }
                m_InputQueue.Add(i, task);
            }
            if (m_CopyPipeline.m_endEvent.Lock(1000) == false)
            {
                // timout 
                int a = 1;
            }
        }
        else
        {
            amf_uint8 *pTmpMemOut = static_cast<amf_uint8*>(plane->GetNative());
            amf_int32 iOutStride = plane->GetHPitch();
            amf_uint8 *pTmpMemIn[2] = { picture.data[1], picture.data[2] };

            amf_size uHeight = plane->GetHeight();
            amf_size uWidth = plane->GetWidth();

            bool is10BitCodec = m_eFormat == AMF_SURFACE_P010;

            // need to pack uv plane properly for 16-bit colour

            if (plane->GetPixelSizeInBytes() == 4)
            {
                for (amf_size y = 0; y < uHeight; y++)
                {
                    amf_uint16 *pLineMemOut = (amf_uint16*)pTmpMemOut;
                    amf_uint16 *pLineMemIn1 = (amf_uint16*)pTmpMemIn[0];
                    amf_uint16 *pLineMemIn2 = (amf_uint16*)pTmpMemIn[1];
                    for (amf_size x = 0; x < uWidth; x++)
                    {
                        // FFMPEG outputs in 10-bit LSB format: 000000DD DDDDDDDD
                        // we want 10-bit MSB format: DDDDDDDD DD000000

                        *pLineMemOut++ = (is10BitCodec) ? *pLineMemIn1++ << 6 : *pLineMemIn1++;
                        *pLineMemOut++ = (is10BitCodec) ? *pLineMemIn2++ << 6 : *pLineMemIn2++;
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
                    amf_uint8 *pLineMemOut = pTmpMemOut;
                    amf_uint8 *pLineMemIn1 = pTmpMemIn[0];
                    amf_uint8 *pLineMemIn2 = pTmpMemIn[1];
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
        }
    }
    double frame_time = (double)pts / AMF_SECOND;

    pSurfaceOut->SetPts(picPts);


    if (duration == 0)
    {
        duration = amf_pts(AMF_SECOND * m_FrameRate.den / m_FrameRate.num);
    }
    pSurfaceOut->SetDuration(duration);

    AMF_FRAME_TYPE eFrameType = AMF_FRAME_PROGRESSIVE;
    if (picture.interlaced_frame)
    {
        eFrameType = picture.top_field_first ? AMF_FRAME_INTERLEAVED_EVEN_FIRST : AMF_FRAME_INTERLEAVED_ODD_FIRST;
        // unsupported???
        //AMF_FRAME_FIELD_SINGLE_EVEN                = 3,
        //AMF_FRAME_FIELD_SINGLE_ODD                = 4,
    }

    pSurfaceOut->SetFrameType(eFrameType);

    *ppData = pSurfaceOut;
    (*ppData)->Acquire();

    if (got_picture)
    {
        // return AMF_REPEAT;
    }
    if (m_bForceEof)
    {
        if (got_picture)
        {
            return AMF_REPEAT;
        }
        else
        {
            return AMF_EOF;
        }
    }
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
bool AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::ReadAVPacketInfo(AMFBuffer* pBuffer, AVPacket *pPacket)
{
    AMFPropertyStoragePtr pStorage(pBuffer);
    if ((pStorage != NULL) && (pPacket != NULL))
    {
        if ((pStorage->GetProperty(L"FFMPEG:pts", &pPacket->pts) != AMF_OK)
            || (pStorage->GetProperty(L"FFMPEG:dts", &pPacket->dts) != AMF_OK)
            || (pStorage->GetProperty(L"FFMPEG:stream_index", &pPacket->stream_index) != AMF_OK)
            || (pStorage->GetProperty(L"FFMPEG:flags", &pPacket->flags) != AMF_OK)
            || (pStorage->GetProperty(L"FFMPEG:duration", &pPacket->duration) != AMF_OK)
            || (pStorage->GetProperty(L"FFMPEG:pos", &pPacket->pos) != AMF_OK)
            || (pStorage->GetProperty(L"FFMPEG:convergence_duration", &pPacket->convergence_duration) != AMF_OK))
        {
            return false;
        }
    }

    return true;
}
//-------------------------------------------------------------------------------------------------
amf_pts AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::GetPtsFromFFMPEG(AMFBuffer* pBuffer, AVFrame *pFrame)
{
    amf_pts retPts = 0;
    if (pBuffer != NULL)
    {
        retPts = pBuffer->GetPts();

        if (pFrame->pkt_dts >= 0)
        {
            amf_int num = 0;
            amf_int den = 1;
            amf_int64 startTime = 0;

            if ((pBuffer->GetProperty(L"FFMPEG:time_base_num", &num) == AMF_OK)
                && (pBuffer->GetProperty(L"FFMPEG:time_base_den", &den) == AMF_OK)
                && (pBuffer->GetProperty(L"FFMPEG:start_time", &startTime) == AMF_OK))
            {
                AVRational tmp = { num, den };
                retPts = (av_rescale_q((pFrame->pkt_dts - startTime), tmp, AMF_TIME_BASE_Q));
            }
        }
        amf_pts firstPts = 0;
        if (pBuffer->GetProperty(L"FFMPEG:FirstPtsOffset", &firstPts) == AMF_OK)
        {
            retPts -= firstPts;
        }
    }
    return retPts;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoDecoderFFMPEGImpl::CopyFrameRGB_FP16(
    amf_uint8* pMemOut,       //output memory
    amf_uint8* pMemIn,        //input memory
    amf_int32  iPixelFormat,  //pixel format
    amf_size   uPitchIn,      //input pitch
    amf_size   uPitchOut,     //out put pitch
    amf_size   uWidth,        //frame width
    amf_size   uHeight)       //frame height
{
    AMF_RESULT ret = AMF_OK;

    if (m_eFormat == AMF_SURFACE_RGBA)
    {
        amf_uint8 *pSrc = (amf_uint8 *)pMemIn;
        amf_uint8 *pDst = (amf_uint8 *)pMemOut;
        amf_uint16 temp = 0;
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
        amf_uint16 *pSrc = (amf_uint16 *)pMemIn;
        amf_uint16 *pDst = (amf_uint16 *)pMemOut;
        if (iPixelFormat == AV_PIX_FMT_RGBA64LE) //EXR, beter performance
        {
            amf_size uLineWidth = uWidth * 4 * sizeof(amf_uint16);
            for (amf_size y = 0; y < uHeight; y++)
            {
                memcpy(pDst, pSrc, uLineWidth);
                pDst += uPitchOut / sizeof(amf_uint16);
                pSrc += uPitchIn / sizeof(amf_uint16);
            }
        }
        else
        {
            amf_uint8* pTemp = 0;
            for (amf_size y = 0; y < uHeight; y++)
            {
                for (amf_size x = 0; x < uWidth; x++)
                {

                    if (iPixelFormat == AV_PIX_FMT_RGB48BE) //png
                    {
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
        ret = AMF_NOT_SUPPORTED;;
    }

    return ret;
}
//-------------------------------------------------------------------------------------------------
