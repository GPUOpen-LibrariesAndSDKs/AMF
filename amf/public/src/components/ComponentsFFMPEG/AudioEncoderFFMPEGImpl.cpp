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
#include "AudioEncoderFFMPEGImpl.h"
#include "UtilsFFMPEG.h"

#include "public/include/core/Context.h"
#include "public/include/core/Trace.h"
#include "public/common/TraceAdapter.h"


#define AMF_FACILITY L"AMFAudioEncoderFFMPEGImpl"

using namespace amf;

#define MAX_AUDIO_PACKET_SIZE  (128 * 1024 *4)
#define BUFFER_SIZE            (14 * 6 * MAX_AUDIO_PACKET_SIZE)


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
    { AMFAF_FLTP, L"FLTP" }, // currently FFMPEG supports only this
    { AMFAF_DBLP, L"DBLP" }, 

    { AMFAF_UNKNOWN, 0 }  // This is end of description mark
};



//
//
// AMFAudioEncoderFFMPEGImpl
//
//

//-------------------------------------------------------------------------------------------------
AMFAudioEncoderFFMPEGImpl::AMFAudioEncoderFFMPEGImpl(AMFContext* pContext)
  : m_pContext(pContext),
    m_bEncodingEnabled(true),
    m_bEof(false),
    m_bDrained(true),
    m_pCodecContext(NULL),
    m_pCompressedBuffer(NULL),
    m_iSamplesPacked(0),
    m_iFrameOffset(0),
    m_iFirstFramePts(-1LL),
	m_iSamplesInPackaet(0),
    m_audioFrameSubmitCount(0),
    m_audioFrameQueryCount(0),
    m_inSampleFormat(AMFAF_UNKNOWN),
    m_iChannelCount(0),
    m_iSampleRate(0),
    m_PrevPts(-1LL)

{
    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoBool(AUDIO_ENCODER_ENABLE_DEBUGGING, L"Enable Debug", false, false),
        AMFPropertyInfoBool(AUDIO_ENCODER_ENABLE_ENCODING, L"Enable encoding", true, false),
        AMFPropertyInfoInt64(AUDIO_ENCODER_AUDIO_CODEC_ID, L"Codec ID", AV_CODEC_ID_NONE, AV_CODEC_ID_NONE, INT_MAX, false),

        AMFPropertyInfoInt64(AUDIO_ENCODER_IN_AUDIO_SAMPLE_RATE, L"Sample Rate", 44100, 0, INT_MAX, false),
        AMFPropertyInfoInt64(AUDIO_ENCODER_IN_AUDIO_CHANNELS, L"Number of channels (0 - default)", 2, 0, 100, false),
        AMFPropertyInfoEnum(AUDIO_ENCODER_IN_AUDIO_SAMPLE_FORMAT, L"Sample Format", AMFAF_FLTP, AMF_SAMPLE_FORMAT_ENUM_DESCRIPTION, false),
        AMFPropertyInfoInt64(AUDIO_ENCODER_IN_AUDIO_CHANNEL_LAYOUT, L"Channel layout (3 - default)", 3, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AUDIO_ENCODER_IN_AUDIO_BLOCK_ALIGN, L"Block Align", 0, 0, INT_MAX, true),
        
        AMFPropertyInfoInt64(AUDIO_ENCODER_OUT_AUDIO_BIT_RATE, L"Compression Bit Rate", 128000, 0, INT_MAX, false),
        AMFPropertyInfoInterface(AUDIO_ENCODER_OUT_AUDIO_EXTRA_DATA, L"Extra Data", NULL, false),
        AMFPropertyInfoInt64(AUDIO_ENCODER_OUT_AUDIO_SAMPLE_RATE, L"Sample Rate", 44100, 0, INT_MAX, false),
        AMFPropertyInfoInt64(AUDIO_ENCODER_OUT_AUDIO_CHANNELS, L"Number of channels (0 - default)", 2, 0, 100, false),
        AMFPropertyInfoEnum(AUDIO_ENCODER_OUT_AUDIO_SAMPLE_FORMAT, L"Sample Format", AMFAF_S16, AMF_SAMPLE_FORMAT_ENUM_DESCRIPTION, false),
        AMFPropertyInfoInt64(AUDIO_ENCODER_OUT_AUDIO_CHANNEL_LAYOUT, L"Channel layout (0 - default)", 0, 0, INT_MAX, false),
        AMFPropertyInfoInt64(AUDIO_ENCODER_OUT_AUDIO_BLOCK_ALIGN, L"Block Align", 0, 0, INT_MAX, false),
        AMFPropertyInfoInt64(AUDIO_ENCODER_OUT_AUDIO_FRAME_SIZE, L"Frame Size", 0, 0, INT_MAX, false),
    AMFPrimitivePropertyInfoMapEnd

    InitFFMPEG();
}
//-------------------------------------------------------------------------------------------------
AMFAudioEncoderFFMPEGImpl::~AMFAudioEncoderFFMPEGImpl()
{
    Terminate();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioEncoderFFMPEGImpl::Init(AMF_SURFACE_FORMAT /*format*/, amf_int32 /*width*/, amf_int32 /*height*/)
{
    AMFLock lock(&m_sync);

    // clean up any information we might previously have
    Terminate();

    // reset the forced EOF flag
    m_bEof = false;
    m_bDrained = true;

    // reset the first frame pts offset
    m_PrevPts = -1LL;
    m_iFirstFramePts = -1LL;
    m_iSamplesInPackaet = 0;

    // get the codec ID that was set
    amf_int64  codecID = AV_CODEC_ID_NONE;
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_ENCODER_AUDIO_CODEC_ID, &codecID));

    // find the correct codec
    AVCodec *codec = avcodec_find_encoder((AVCodecID) codecID);
    if (!codec)
    {
        Terminate();
        return AMF_CODEC_NOT_SUPPORTED;
    }
    
    // allocate the codec context
    m_pCodecContext = avcodec_alloc_context3(codec);


    // initialize the codec context from the component properties
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_ENCODER_IN_AUDIO_CHANNELS, &m_iChannelCount));
    m_pCodecContext->channels = m_iChannelCount;
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_ENCODER_IN_AUDIO_CHANNEL_LAYOUT, &m_pCodecContext->channel_layout));
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_ENCODER_IN_AUDIO_SAMPLE_RATE, &m_iSampleRate));
    m_pCodecContext->sample_rate = m_iSampleRate;
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_ENCODER_OUT_AUDIO_BIT_RATE, &m_pCodecContext->bit_rate));
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_ENCODER_IN_AUDIO_BLOCK_ALIGN, &m_pCodecContext->block_align));


    // figure out the surface format conversion
    amf_int64 format;
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_ENCODER_IN_AUDIO_SAMPLE_FORMAT, &format));
    m_inSampleFormat = (AMF_AUDIO_FORMAT)format;
    m_pCodecContext->sample_fmt = GetFFMPEGAudioFormat(m_inSampleFormat);

    // set all default parameters
    //m_pCodecContext->strict_std_compliance = FF_COMPLIANCE_STRICT;
    m_pCodecContext->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    
    m_pCodecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;

    m_pCodecContext->time_base.num= 1;
    m_pCodecContext->time_base.den = m_iSampleRate;

    if (avcodec_open2(m_pCodecContext, codec, NULL) < 0 || m_pCodecContext->codec == 0)
    {
        Terminate();
        return AMF_DECODER_NOT_PRESENT;
    }
    // allocate required buffers
    if (m_pCompressedBuffer==NULL)
    {
        m_pCompressedBuffer = (uint8_t*)av_malloc((amf_uint) BUFFER_SIZE);
        if (!m_pCompressedBuffer)
        {
            Terminate();
            return AMF_OUT_OF_MEMORY;
        }
    }

    // input properties
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_IN_AUDIO_CHANNELS, m_pCodecContext->channels));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_IN_AUDIO_SAMPLE_RATE, m_iSampleRate));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_IN_AUDIO_SAMPLE_FORMAT, GetAMFAudioFormat(m_pCodecContext->sample_fmt)));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_IN_AUDIO_CHANNEL_LAYOUT, m_pCodecContext->channel_layout));

    // output properties
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_OUT_AUDIO_CHANNELS, m_pCodecContext->channels));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_OUT_AUDIO_SAMPLE_RATE, m_iSampleRate));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_OUT_AUDIO_SAMPLE_FORMAT, m_inSampleFormat));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_OUT_AUDIO_CHANNEL_LAYOUT, m_pCodecContext->channel_layout));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_OUT_AUDIO_FRAME_SIZE, m_pCodecContext->frame_size));

    const amf_int64  blockAlign = m_iChannelCount * GetAudioSampleSize((AMF_AUDIO_FORMAT) GetAMFAudioFormat(m_pCodecContext->sample_fmt));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_OUT_AUDIO_BLOCK_ALIGN, blockAlign));

    // allocate a buffer to store the extra data
    if(m_pCodecContext->extradata_size != 0)
    { 
        AMFBufferPtr spBuffer;
        AMF_RESULT err = m_pContext->AllocBuffer(AMF_MEMORY_HOST, m_pCodecContext->extradata_size, &spBuffer);
        if ((err == AMF_OK) && spBuffer->GetNative())
        {
            memcpy(spBuffer->GetNative(), m_pCodecContext->extradata, m_pCodecContext->extradata_size);
        }
        AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_OUT_AUDIO_EXTRA_DATA, spBuffer));
    }

    m_iFrameOffset = 0;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioEncoderFFMPEGImpl::ReInit(amf_int32 width, amf_int32 height)
{
    Terminate();
    return Init(AMF_SURFACE_UNKNOWN, width, height);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioEncoderFFMPEGImpl::Terminate()
{
    AMFLock lock(&m_sync);

    // clean-up codec related items
    if (m_pCodecContext != NULL)
    {
        AMFTraceInfo(AMF_FACILITY, L"Submitted %d, Queried %d", (int)m_audioFrameSubmitCount, (int)m_audioFrameQueryCount);

        avcodec_close(m_pCodecContext);
        av_free(m_pCodecContext);
        m_pCodecContext = NULL;
    }

    if (m_pCompressedBuffer!=NULL)
    {
        av_free(m_pCompressedBuffer);
        m_pCompressedBuffer=NULL;
    }

    m_pFrame.Release();
    m_iFrameOffset = 0;
    m_iFirstFramePts = -1;
	
    m_iSamplesPacked=0;
	m_iSamplesInPackaet = 0;
	
    m_audioFrameSubmitCount = 0;
    m_audioFrameQueryCount = 0;


    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioEncoderFFMPEGImpl::Drain()
{
    AMFLock lock(&m_sync);

    m_bEof = true;
    m_bDrained = false;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioEncoderFFMPEGImpl::Flush()
{
    AMFLock lock(&m_sync);

    m_pFrame.Release();
    m_iFrameOffset = 0;
    m_iFirstFramePts = -1;
    m_bDrained = true;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioEncoderFFMPEGImpl::SubmitInput(AMFData* pData)
{
    AMFLock lock(&m_sync);
    // check some required parameters
    AMF_RETURN_IF_FALSE(m_pCodecContext != NULL, AMF_NOT_INITIALIZED, L"SubmitInput() - Codec Context not Initialized");

    // if the in buffer is empty, we reached the end of the available input...
    if (!pData)
    {
        m_bEof = true;
        return AMF_EOF;
    }

    // if we requested to drain, we shouldn't accept any more input
    if (m_bEof)
    {
        return AMF_EOF;
    }

    // update the first frame pts offset
    if (pData && (m_iFirstFramePts == -1))
    {
        m_iFirstFramePts = pData->GetPts();
    }

    if(m_pFrame != NULL && m_pFrame->GetSampleCount() - m_iFrameOffset >= m_pCodecContext->frame_size)
    {
        return AMF_INPUT_FULL;
    }

    AMFAudioBufferPtr pAudioBuffer(pData);
    AMF_RETURN_IF_FALSE(pAudioBuffer != 0,AMF_INVALID_ARG, L"SubmitInput() - Input should be AudioBuffer");

    AMF_RESULT err=pAudioBuffer->Convert(AMF_MEMORY_HOST);
    AMF_RETURN_IF_FAILED(err,L"SubmitInput() - Convert(AMF_MEMORY_HOST) failed");

    if(m_pCodecContext->frame_size == 0 || m_pFrame == NULL)
    {
        m_pFrame = pAudioBuffer;
    }
    else
    { 
        m_pFrame = CombineBuffers(m_pFrame, m_iFrameOffset, pAudioBuffer);
    }
    m_iFrameOffset = 0;
    m_audioFrameSubmitCount++;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMFAudioBufferPtr AMFAudioEncoderFFMPEGImpl::CombineBuffers(AMFAudioBuffer *dst, amf_int32 dstOffset, AMFAudioBuffer *src)
{
    AMFAudioBufferPtr out;
    amf_int32 samples = src->GetSampleCount();
    samples += (dst->GetSampleCount() - dstOffset);

    m_pContext->AllocAudioBuffer(AMF_MEMORY_HOST, m_inSampleFormat, samples, m_iSampleRate, m_iChannelCount, &out);

    int iSampleSize = GetAudioSampleSize((AMF_AUDIO_FORMAT) m_inSampleFormat);

    int planar = IsAudioPlanar(m_inSampleFormat) ? 1 : m_iChannelCount;
    int planes = !IsAudioPlanar(m_inSampleFormat) ? 1 : m_iChannelCount;

    amf_size outPlaneSize = out->GetSampleCount() * iSampleSize * planar;
    amf_int32 dstCopy = 0;
    amf_int32 dstOffsetBytes =          dstOffset * iSampleSize * planar;
    amf_size dstPlaneSize = dst->GetSampleCount() * iSampleSize * planar;
    dstCopy = (dst->GetSampleCount() - dstOffset) * iSampleSize * planar;

    for(int ch = 0; ch < planes; ch++)
    { 
        memcpy( (amf_uint8*)out->GetNative()    + ch * outPlaneSize, 
                (amf_uint8*)dst->GetNative()    + ch * dstPlaneSize  + dstOffsetBytes, 
            dstCopy);
    }
    
    amf_size srcPlaneSize = src->GetSampleCount() * iSampleSize * planar;
    for(int ch = 0; ch < planes; ch++)
    { 
        memcpy( (amf_uint8*)out->GetNative()    + ch * outPlaneSize + dstCopy, 
                (amf_uint8*)src->GetNative()    + ch * srcPlaneSize, 
                srcPlaneSize);
    }
    return out;

}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioEncoderFFMPEGImpl::QueryOutput(AMFData** ppData)
{
    AMFLock lock(&m_sync);
    // check some required parameters
    AMF_RETURN_IF_FALSE(m_pCodecContext != NULL, AMF_NOT_INITIALIZED, L"GetOutput() - Codec Context not Initialized");
    AMF_RETURN_IF_FALSE(ppData != NULL, AMF_INVALID_ARG, L"GetOutput() - ppData == NULL");

    // initialize output
    *ppData = NULL;

    // if encoding's been disabled, no need to proceed any further
    if (!m_bEncodingEnabled)
    {
        return m_bEof ? AMF_EOF : AMF_OK;
    }
    if(m_pFrame == NULL && m_bDrained)
    {
        return m_bEof ? AMF_EOF : AMF_OK;
    }
    int samples = 0;
    if(m_pFrame != NULL)
    { 
        samples = m_pFrame->GetSampleCount() - m_iFrameOffset;
    }
    if(m_pCodecContext->frame_size > 0)
    { 
        if(samples < m_pCodecContext->frame_size)
        {
            if(!m_bEof)
            { 
                return AMF_OK;
            }
        }
        else
        {
            samples = m_pCodecContext->frame_size;
        }
    }
    int iSampleSize = GetAudioSampleSize((AMF_AUDIO_FORMAT) m_inSampleFormat);
   
    int iFrameSizeBytes = (int)samples * iSampleSize * m_iChannelCount;
    if(iFrameSizeBytes == 0)
    {
        iFrameSizeBytes = m_pCodecContext->frame_size * iSampleSize * m_iChannelCount;
    }


    // encode
    AVPacket  avPacket;
    av_init_packet(&avPacket);
    avPacket.data = m_pCompressedBuffer;
    avPacket.size = BUFFER_SIZE;

    int ret = 0;
    int pckt = 0;

    if (m_pFrame != NULL)
    {
        // fill the frame information
        AVFrame  avFrame;
        memset(&avFrame, 0, sizeof(avFrame));

        avFrame.nb_samples = samples;
        avFrame.format = m_pCodecContext->sample_fmt;
        avFrame.channel_layout = m_pCodecContext->channel_layout;
        avFrame.channels = m_iChannelCount;
        avFrame.sample_rate = m_iSampleRate;
        avFrame.key_frame = 1;
        avFrame.pts = av_rescale_q(m_pFrame->GetPts(), AMF_TIME_BASE_Q, m_pCodecContext->time_base);

        int planar = IsAudioPlanar(m_inSampleFormat) ? 1 : m_iChannelCount;
        int planes = !IsAudioPlanar(m_inSampleFormat) ? 1 : m_iChannelCount;

        // setup the data pointers in the AVFrame

        amf_int32 dstOffsetBytes = m_iFrameOffset * iSampleSize * planar;
        amf_size dstPlaneSize = m_pFrame->GetSampleCount() * iSampleSize * planar;

        for(int ch = 0; ch < planes; ch++)
        { 
            avFrame.data[ch] = (uint8_t *)m_pFrame->GetNative() + ch * dstPlaneSize + dstOffsetBytes;
        }
        avFrame.extended_data = avFrame.data;

        ret = avcodec_encode_audio2(m_pCodecContext, &avPacket, &avFrame, &pckt);
        m_iFrameOffset += samples;
        m_bDrained = false;
        if(m_iFrameOffset >= m_pFrame->GetSampleCount())
        {
            m_pFrame.Release();
            m_iFrameOffset = 0;
        }
    }
    else
    {
        pckt = 1;
        ret = avcodec_encode_audio2(m_pCodecContext, &avPacket, NULL, &pckt);
        if (!pckt)
        { 
            m_bDrained = true;
        }
        else
        {
            amf_pts amfDuration = av_rescale_q(avPacket.duration, m_pCodecContext->time_base , AMF_TIME_BASE_Q);
            samples = amfDuration * m_pCodecContext->sample_rate / AMF_SECOND;
        }
    }
    if (ret == 0)
    {
        m_iSamplesInPackaet += samples;

        if (pckt)
        {
            // allocate and fill output buffer
            AMFBufferPtr pBufferOut;
            AMF_RESULT err = m_pContext->AllocBuffer(AMF_MEMORY_HOST, avPacket.size, &pBufferOut);
            AMF_RETURN_IF_FAILED(err, L"GetOutput() - AllocBuffer failed");

            amf_uint8 *pMemOut = static_cast<uint8_t*>(pBufferOut->GetNative());
            memcpy(pMemOut, m_pCompressedBuffer, avPacket.size);

            // do PTS -always 100 nanos
            amf_pts pts = m_iSamplesPacked * AMF_SECOND / m_pCodecContext->sample_rate;
            amf_pts duration = m_iSamplesInPackaet * AMF_SECOND / m_pCodecContext->sample_rate;

            pBufferOut->SetPts(m_iFirstFramePts + pts);
            m_PrevPts = m_iFirstFramePts + pts;
            m_iSamplesPacked += m_iSamplesInPackaet;

            pBufferOut->SetDuration(duration);

            // prepare output
            *ppData = pBufferOut;
            (*ppData)->Acquire();
            m_audioFrameQueryCount++;

            m_iSamplesInPackaet = 0;
        }
    }

    // if we have more output to be consumed than a frame, we should consume it
    // before submitting more input...
    if (m_pFrame && (m_pFrame->GetSampleCount() - m_iFrameOffset >= m_pCodecContext->frame_size))
    {
        return AMF_REPEAT;
    }
    if(m_bEof && !m_bDrained)
    {
        return AMF_REPEAT;
    }
    return m_bEof ? AMF_EOF : AMF_OK;
}
//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL  AMFAudioEncoderFFMPEGImpl::OnPropertyChanged(const wchar_t* pName)
{
    AMFLock lock(&m_sync);

    const amf_wstring  name(pName);
    if (name == AUDIO_ENCODER_ENABLE_ENCODING)
    {
        GetProperty(AUDIO_ENCODER_ENABLE_ENCODING, &m_bEncodingEnabled);
        return;
    }

    if (name == AUDIO_ENCODER_ENABLE_DEBUGGING)
    {
        // do nothing
        return;
    }
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
