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

#include "AudioDecoderFFMPEGImpl.h"
#include "UtilsFFMPEG.h"

#include "public/include/core/Context.h"
#include "public/include/core/Trace.h"
#include "public/common/TraceAdapter.h"


#define AMF_FACILITY L"AMFAudioDecoderFFMPEGImpl"

using namespace amf;



const AMFEnumDescriptionEntry AMF_SAMPLE_FORMAT_ENUM_DESCRIPTION[] =
{
    { AMFAF_UNKNOWN, L"UNKNOWN" },
    { AMFAF_U8, L"U8" },
    { AMFAF_S16, L"S16" },
    { AMFAF_S32, L"S32" },
    { AMFAF_FLT, L"FLT" },
    { AMFAF_DBL, L"DBL" },
    { AMFAF_U8P, L"U8P" },
    { AMFAF_S16P, L"S16P" },
    { AMFAF_S32P, L"S32P" },
    { AMFAF_FLTP, L"FLTP" },
    { AMFAF_DBLP, L"DBLP" },

    { AMFAF_UNKNOWN, 0 }  // This is end of description mark
};



//
//
// AMFAudioDecoderFFMPEGImpl
//
//

//-------------------------------------------------------------------------------------------------
AMFAudioDecoderFFMPEGImpl::AMFAudioDecoderFFMPEGImpl(AMFContext* pContext)
  : m_pContext(pContext),
    m_bDecodingEnabled(true),
    m_bForceEof(false),
    m_pCodecContext(NULL),
    m_SeekPts(0),
    m_iLastDataOffset(0),
    m_ptsLastDataOffset(0),
    m_audioFrameSubmitCount(0),
    m_audioFrameQueryCount(0)
{
    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoBool(AUDIO_DECODER_ENABLE_DEBUGGING, L"Enable Debug", false, true),
        AMFPropertyInfoBool(AUDIO_DECODER_ENABLE_DECODING, L"Enable decoding", true, true),

        AMFPropertyInfoInt64(AUDIO_DECODER_IN_AUDIO_CODEC_ID, L"Codec ID", AV_CODEC_ID_NONE, AV_CODEC_ID_NONE, INT_MAX, true),
        AMFPropertyInfoInt64(AUDIO_DECODER_IN_AUDIO_BIT_RATE, L"Compression Bit Rate", 128000, 0, INT_MAX, true),
        AMFPropertyInfoInterface(AUDIO_DECODER_IN_AUDIO_EXTRA_DATA, L"Extra Data", NULL, true),
        AMFPropertyInfoInt64(AUDIO_DECODER_IN_AUDIO_SAMPLE_RATE, L"Sample Rate", 0, 0, 48000, true),
        AMFPropertyInfoInt64(AUDIO_DECODER_IN_AUDIO_CHANNELS, L"Number of channels (0 - default)", 2, 0, 100, true),
        AMFPropertyInfoEnum(AUDIO_DECODER_IN_AUDIO_SAMPLE_FORMAT, L"Sample Format", AMFAF_UNKNOWN, AMF_SAMPLE_FORMAT_ENUM_DESCRIPTION, true),
        AMFPropertyInfoInt64(AUDIO_DECODER_IN_AUDIO_CHANNEL_LAYOUT, L"Channel layout (0 - default)", 0, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AUDIO_DECODER_IN_AUDIO_BLOCK_ALIGN, L"Block Align", 0, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AUDIO_DECODER_IN_AUDIO_FRAME_SIZE, L"Frame Size", 0, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AUDIO_DECODER_IN_AUDIO_SEEK_POSITION, L"Seek Position", 0, 0, INT_MAX, true),

        AMFPropertyInfoInt64(AUDIO_DECODER_OUT_AUDIO_BIT_RATE, L"Compression Bit Rate", 128000, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AUDIO_DECODER_OUT_AUDIO_SAMPLE_RATE, L"Sample Rate", 0, 0, 48000, true),
        AMFPropertyInfoInt64(AUDIO_DECODER_OUT_AUDIO_CHANNELS, L"Number of channels (0 - default)", 2, 0, 100, true),
        AMFPropertyInfoEnum(AUDIO_DECODER_OUT_AUDIO_SAMPLE_FORMAT, L"Sample Format", AMFAF_UNKNOWN, AMF_SAMPLE_FORMAT_ENUM_DESCRIPTION, true),
        AMFPropertyInfoInt64(AUDIO_DECODER_OUT_AUDIO_CHANNEL_LAYOUT, L"Channel layout (0 - default)", 0, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AUDIO_DECODER_OUT_AUDIO_BLOCK_ALIGN, L"Block Align", 0, 0, INT_MAX, true),
    AMFPrimitivePropertyInfoMapEnd

    InitFFMPEG();
}
//-------------------------------------------------------------------------------------------------
AMFAudioDecoderFFMPEGImpl::~AMFAudioDecoderFFMPEGImpl()
{
    Terminate();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioDecoderFFMPEGImpl::Init(AMF_SURFACE_FORMAT /*format*/, amf_int32 /*width*/, amf_int32 /*height*/)
{
    AMFLock lock(&m_sync);

    // clean up any information we might previously have
    Terminate();

    // reset the forced EOF flag
    m_bForceEof = false;


    // get the codec ID that was set
    amf_int64  codecID = AV_CODEC_ID_NONE;
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_DECODER_IN_AUDIO_CODEC_ID, &codecID));

    // find the correct codec
    AVCodec *codec = avcodec_find_decoder((AVCodecID) codecID);
    if (!codec)
    {
        Terminate();
        return AMF_CODEC_NOT_SUPPORTED;
    }
    
    // allocate the codec context
    m_pCodecContext = avcodec_alloc_context3(codec);


    // initialize the codec context from the component properties
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_DECODER_IN_AUDIO_BIT_RATE, &m_pCodecContext->bit_rate));
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_DECODER_IN_AUDIO_CHANNELS, &m_pCodecContext->channels));
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_DECODER_IN_AUDIO_BLOCK_ALIGN, &m_pCodecContext->block_align));
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_DECODER_IN_AUDIO_CHANNEL_LAYOUT, &m_pCodecContext->channel_layout));
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_DECODER_IN_AUDIO_FRAME_SIZE, &m_pCodecContext->frame_size));

    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_DECODER_IN_AUDIO_SAMPLE_RATE, &m_pCodecContext->sample_rate));
    m_pCodecContext->time_base.num = m_pCodecContext->sample_rate;
    m_pCodecContext->time_base.den = 1;

    AMFVariant val;
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_DECODER_IN_AUDIO_EXTRA_DATA, &val));
    if (!val.Empty() && val.pInterface)
    {
        // NOTE: the buffer ptr. shouldn't disappear as the 
        //       property holds it in the end
        AMFBufferPtr pBuffer = AMFBufferPtr(val.pInterface);
        m_pCodecContext->extradata = (uint8_t*) pBuffer->GetNative();
        m_pCodecContext->extradata_size = (int)pBuffer->GetSize();
    }

    // figure out the surface format conversion
    amf_int64  sampleFormat = AMFAF_UNKNOWN;
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_DECODER_IN_AUDIO_SAMPLE_FORMAT, &sampleFormat));

    m_pCodecContext->sample_fmt = AV_SAMPLE_FMT_NONE;
    switch (sampleFormat)
    {
        case AMFAF_U8:   m_pCodecContext->sample_fmt = AV_SAMPLE_FMT_U8;    break;
        case AMFAF_S16:  m_pCodecContext->sample_fmt = AV_SAMPLE_FMT_S16;   break;
        case AMFAF_S32:  m_pCodecContext->sample_fmt = AV_SAMPLE_FMT_S32;   break;
        case AMFAF_FLT:  m_pCodecContext->sample_fmt = AV_SAMPLE_FMT_FLT;   break;
        case AMFAF_DBL:  m_pCodecContext->sample_fmt = AV_SAMPLE_FMT_DBL;   break;

        case AMFAF_U8P:  m_pCodecContext->sample_fmt = AV_SAMPLE_FMT_U8P;   break;
        case AMFAF_S16P: m_pCodecContext->sample_fmt = AV_SAMPLE_FMT_S16P;  break;
        case AMFAF_S32P: m_pCodecContext->sample_fmt = AV_SAMPLE_FMT_S32P;  break;
        case AMFAF_FLTP: m_pCodecContext->sample_fmt = AV_SAMPLE_FMT_FLTP;  break;
        case AMFAF_DBLP: m_pCodecContext->sample_fmt = AV_SAMPLE_FMT_DBLP;  break;
           
        case AMFAF_UNKNOWN: 
        default :
            return AMF_NOT_SUPPORTED;
    }

    if (avcodec_open2(m_pCodecContext, codec, NULL) < 0)
    {
        Terminate();
        return AMF_DECODER_NOT_PRESENT;
    }

    // MM get - around some WMV audio codecs
    if(m_pCodecContext->channel_layout == 0)
    {
        m_pCodecContext->channel_layout = 3;
    }

    // output properties
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_DECODER_OUT_AUDIO_SAMPLE_FORMAT, sampleFormat));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_DECODER_OUT_AUDIO_SAMPLE_RATE, m_pCodecContext->sample_rate));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_DECODER_OUT_AUDIO_CHANNELS, m_pCodecContext->channels));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_DECODER_OUT_AUDIO_CHANNEL_LAYOUT, m_pCodecContext->channel_layout));

    const amf_int64  outputBitRate = (amf_int64) GetAudioSampleSize((AMF_AUDIO_FORMAT) sampleFormat) * m_pCodecContext->sample_rate * m_pCodecContext->channels * 8;
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_DECODER_OUT_AUDIO_BIT_RATE, outputBitRate));

    const amf_int64  blockAlign = m_pCodecContext->channels * GetAudioSampleSize((AMF_AUDIO_FORMAT) sampleFormat);
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_DECODER_OUT_AUDIO_BLOCK_ALIGN, blockAlign));

    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_DECODER_ENABLE_DECODING, &m_bDecodingEnabled));

    bool debug = false;
    GetProperty(AUDIO_DECODER_ENABLE_DEBUGGING, &debug);
    if (debug)
    {
        AMFTraceDebug(AMF_FACILITY, L"AMFAudioDecoderFFMPEG::InitContext() - Completed", m_pCodecContext->codec_id);
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioDecoderFFMPEGImpl::ReInit(amf_int32 width, amf_int32 height)
{
    Terminate();
    return Init(AMF_SURFACE_UNKNOWN, width, height);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioDecoderFFMPEGImpl::Terminate()
{
    AMFLock lock(&m_sync);

    // clear the internally stored buffer
    m_pInputData = nullptr;

    // clean-up codec related items
    if (m_pCodecContext != NULL)
    {
        AMFTraceInfo(AMF_FACILITY, L"Submitted %d, Queried %d", (int)m_audioFrameSubmitCount, (int)m_audioFrameQueryCount);

        avcodec_close(m_pCodecContext);
        av_free(m_pCodecContext);
        m_pCodecContext = NULL;
        m_SeekPts = 0;
    }
    m_ptsLastDataOffset = 0;
    m_iLastDataOffset = 0;

    m_audioFrameSubmitCount = 0;
    m_audioFrameQueryCount = 0;
    m_bForceEof = false;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioDecoderFFMPEGImpl::Drain()
{
    AMFLock lock(&m_sync);

    m_bForceEof = true;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioDecoderFFMPEGImpl::Flush()
{
    AMFLock lock(&m_sync);

    // clear the internally stored buffer
    m_pInputData = nullptr;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioDecoderFFMPEGImpl::SubmitInput(AMFData* pData)
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
        m_audioFrameSubmitCount++;
    }
                
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioDecoderFFMPEGImpl::QueryOutput(AMFData** ppData)
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



    amf_pts       AudioBufferPts = m_pInputData->GetPts();
    amf_size      uiMemSizeIn    = m_pInputData->GetSize() - m_iLastDataOffset;
    AMF_RETURN_IF_FALSE(uiMemSizeIn != 0, AMF_INVALID_ARG, L"QueryOutput() - Invalid Param");



    // decode
    AVPacket avpkt;
    memset(&avpkt, 0, sizeof(AVPacket));
    avpkt.size = int(uiMemSizeIn);
    avpkt.data = static_cast<uint8_t*>(m_pInputData->GetNative()) + m_iLastDataOffset;

    AVFrame decoded_frame;
    memset(&decoded_frame, 0, sizeof(AVFrame));
    av_frame_unref(&decoded_frame);

    int  iGotFrame = 0;
    bool bAVPacketRead = ReadAVPacketInfo(m_pInputData, &avpkt);
    int  ret           = avcodec_decode_audio4(m_pCodecContext, &decoded_frame, &iGotFrame, &avpkt);

    if (iGotFrame && bAVPacketRead)
    {
        AudioBufferPts = GetPtsFromFFMPEG(m_pInputData, &decoded_frame);
    }

    AMF_RESULT  err = AMF_OK;
    if (ret > 0 && ret < avpkt.size)
    {
        m_iLastDataOffset += ret;
        err = AMF_REPEAT;
    }
    else
    {
        m_pInputData = NULL;
        m_iLastDataOffset = 0;
    }

    if (AudioBufferPts < m_SeekPts)
    {
        return m_bForceEof ? AMF_EOF : err;
    }

    if (m_iLastDataOffset == 0)
    {
        m_pInputData = NULL;
    }

    if (ret < 0)
    {
        return m_bForceEof ? AMF_EOF : err;
    }


    if (iGotFrame && decoded_frame.nb_samples > 0)
    {
        amf_int64  sampleFormat = AMFAF_UNKNOWN;
        GetProperty(AUDIO_DECODER_IN_AUDIO_SAMPLE_FORMAT, &sampleFormat);

        AMFAudioBufferPtr  pOutputAudioBuffer;
        AMF_RESULT err1 = m_pContext->AllocAudioBuffer(
            AMF_MEMORY_HOST,
            (AMF_AUDIO_FORMAT) sampleFormat,
            decoded_frame.nb_samples,
            m_pCodecContext->sample_rate,
            m_pCodecContext->channels,
            &pOutputAudioBuffer);

        AMF_RETURN_IF_FAILED(err1, L"Process() - AllocAudioBuffer failed");

        amf_uint8* pMemOut = static_cast<amf_uint8*>(pOutputAudioBuffer->GetNative());

        // copy data to output buffer
        if (IsAudioPlanar((AMF_AUDIO_FORMAT) sampleFormat))
        {
            const int        iPlaneSize     = GetAudioSampleSize((AMF_AUDIO_FORMAT) sampleFormat) * decoded_frame.nb_samples;
            const amf_int64  outputChannels = m_pCodecContext->channels;
            for (amf_int32 ch = 0; ch < outputChannels; ch++)
            {
                memcpy(pMemOut, decoded_frame.data[ch], iPlaneSize);
                pMemOut += iPlaneSize;
            }
        }
        else
        {
            memcpy(pMemOut, decoded_frame.data[0], pOutputAudioBuffer->GetSize());
        }

        pOutputAudioBuffer->SetPts(AudioBufferPts + m_ptsLastDataOffset);
        amf_pts tmpPts = (amf_pts(AMF_SECOND) * decoded_frame.nb_samples);
        amf_pts duration = (tmpPts / m_pCodecContext->sample_rate);
        pOutputAudioBuffer->SetDuration(duration);
        m_ptsLastDataOffset += duration;

        *ppData = pOutputAudioBuffer;
        (*ppData)->Acquire();

        m_audioFrameQueryCount++;

        bool debug = false;
        GetProperty(AUDIO_DECODER_ENABLE_DEBUGGING, &debug);
        if (debug)
        {
            AMFTraceDebug(AMF_FACILITY, L"AMFAudioDecoderFFMPEG::Process() - output block pts=%.2f", (double)pOutputAudioBuffer->GetPts() / AMF_SECOND);
        }
    }

    return m_bForceEof ? AMF_EOF : err;
}
//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL  AMFAudioDecoderFFMPEGImpl::OnPropertyChanged(const wchar_t* pName)
{
    AMFLock lock(&m_sync);

    const amf_wstring  name(pName);
    if (name == AUDIO_DECODER_ENABLE_DECODING)
    {
        GetProperty(AUDIO_DECODER_ENABLE_DECODING, &m_bDecodingEnabled);
        return;
    }

    if (name == AUDIO_DECODER_IN_AUDIO_SEEK_POSITION)
    {
        amf_pts  seekPts = 0;
        if (GetProperty(AUDIO_DECODER_IN_AUDIO_SEEK_POSITION, &seekPts) == AMF_OK)
        {
            if (m_pCodecContext)
            {
                avcodec_flush_buffers(m_pCodecContext);
            }
            m_SeekPts = seekPts;
        }
    }

    if (name == AUDIO_DECODER_ENABLE_DEBUGGING)
    {
        // do nothing
        return;
    }
}


//
//
// protected
//
//

//-------------------------------------------------------------------------------------------------
bool AMF_STD_CALL  AMFAudioDecoderFFMPEGImpl::ReadAVPacketInfo(AMFBufferPtr pBuffer, AVPacket *pPacket)
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
amf_pts AMF_STD_CALL  AMFAudioDecoderFFMPEGImpl::GetPtsFromFFMPEG(AMFBufferPtr pBuffer, AVFrame *pFrame)
{
    amf_pts retPts = 0;
    if (pBuffer != NULL)
    {
        retPts = pBuffer->GetPts();

        if (pFrame->pkt_pts >= 0)
        {
            amf_int num = 0;
            amf_int den = 1;
            amf_int64 startTime = 0;

            if ((pBuffer->GetProperty(L"FFMPEG:time_base_num", &num) == AMF_OK)
                && (pBuffer->GetProperty(L"FFMPEG:time_base_den", &den) == AMF_OK)
                && (pBuffer->GetProperty(L"FFMPEG:start_time", &startTime) == AMF_OK))
            {
                AVRational tmp = { num, den };
                retPts = (av_rescale_q((pFrame->pkt_pts - startTime), tmp, AMF_TIME_BASE_Q));
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
