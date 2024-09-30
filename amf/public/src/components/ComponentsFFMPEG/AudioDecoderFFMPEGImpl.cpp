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

#include "libavutil/channel_layout.h"
#include "AudioDecoderFFMPEGImpl.h"
#include "UtilsFFMPEG.h"

#include "public/include/core/Context.h"
#include "public/include/core/Trace.h"
#include "public/common/TraceAdapter.h"
#include "public/common/PropertyStorageImpl.h"

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
    m_bEof(false),
    m_pCodecContext(nullptr),
    m_SeekPts(0),
    m_dtsId(0),
    m_ptsLastDataOffset(0),
    m_audioFrameSubmitCount(0),
    m_audioFrameQueryCount(0)
{
    g_AMFFactory.Init();

    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoBool(AUDIO_DECODER_ENABLE_DEBUGGING, L"Enable Debug", false, true),
        AMFPropertyInfoBool(AUDIO_DECODER_ENABLE_DECODING, L"Enable decoding", true, true),

        AMFPropertyInfoInt64(AUDIO_DECODER_IN_AUDIO_CODEC_ID, L"Codec ID", AV_CODEC_ID_NONE, AV_CODEC_ID_NONE, INT_MAX, true),

        AMFPropertyInfoInt64(AUDIO_DECODER_IN_AUDIO_BIT_RATE, L"Compression Bit Rate In", 128000, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AUDIO_DECODER_IN_AUDIO_SAMPLE_RATE, L"Sample Rate In", 0, 0, 256000, true),
        AMFPropertyInfoInt64(AUDIO_DECODER_IN_AUDIO_CHANNELS, L"Number of channels in (0 - default)", 2, 0, 100, true),
        AMFPropertyInfoEnum(AUDIO_DECODER_IN_AUDIO_SAMPLE_FORMAT, L"Sample Format In", AMFAF_UNKNOWN, AMF_SAMPLE_FORMAT_ENUM_DESCRIPTION, true),
        AMFPropertyInfoInt64(AUDIO_DECODER_IN_AUDIO_CHANNEL_LAYOUT, L"Channel layout in (0 - default)", 0, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AUDIO_DECODER_IN_AUDIO_BLOCK_ALIGN, L"Block Align In", 0, 0, INT_MAX, true),

        AMFPropertyInfoInt64(AUDIO_DECODER_IN_AUDIO_FRAME_SIZE, L"Frame Size", 0, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AUDIO_DECODER_IN_AUDIO_SEEK_POSITION, L"Seek Position", 0, 0, INT_MAX, true),
        AMFPropertyInfoInterface(AUDIO_DECODER_IN_AUDIO_EXTRA_DATA, L"Extra Data", NULL, true),

        AMFPropertyInfoInt64(AUDIO_DECODER_OUT_AUDIO_BIT_RATE, L"Compression Bit Rate Out", 128000, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AUDIO_DECODER_OUT_AUDIO_SAMPLE_RATE, L"Sample Rate Out", 0, 0, 256000, true),
        AMFPropertyInfoInt64(AUDIO_DECODER_OUT_AUDIO_CHANNELS, L"Number of channels out (0 - default)", 2, 0, 100, true),
        AMFPropertyInfoEnum(AUDIO_DECODER_OUT_AUDIO_SAMPLE_FORMAT, L"Sample Format Out", AMFAF_UNKNOWN, AMF_SAMPLE_FORMAT_ENUM_DESCRIPTION, true),
        AMFPropertyInfoInt64(AUDIO_DECODER_OUT_AUDIO_CHANNEL_LAYOUT, L"Channel layout out (0 - default)", 0, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AUDIO_DECODER_OUT_AUDIO_BLOCK_ALIGN, L"Block Align Out", 0, 0, INT_MAX, true),
    AMFPrimitivePropertyInfoMapEnd

    InitFFMPEG();
}
//-------------------------------------------------------------------------------------------------
AMFAudioDecoderFFMPEGImpl::~AMFAudioDecoderFFMPEGImpl()
{
    Terminate();
    g_AMFFactory.Terminate();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioDecoderFFMPEGImpl::GetInputCodecAt(amf_size index, AMFPropertyStorage** codec) const
{
    AMF_RETURN_IF_INVALID_POINTER(codec);
    amf_int64 codecID = 0;
    amf_int32 sampleRate = 0;
    switch (index)
    {
        case 0:
            codecID = AV_CODEC_ID_AAC;
            sampleRate = 0;
            break;
        default:
            return AMF_OUT_OF_RANGE;
    }
    *codec = new AMFInterfaceImpl<amf::AMFPropertyStorageImpl<amf::AMFPropertyStorage>>();
    (*codec)->Acquire();
    (*codec)->SetProperty(SUPPORTEDCODEC_ID, codecID);
    (*codec)->SetProperty(SUPPORTEDCODEC_SAMPLERATE, sampleRate);
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioDecoderFFMPEGImpl::Init(AMF_SURFACE_FORMAT /*format*/, amf_int32 /*width*/, amf_int32 /*height*/)
{
    AMFLock lock(&m_sync);

    // clean up any information we might previously have
    Terminate();

    // reset the forced EOF flag
    m_bEof = false;

    // set the value we will use for ID for matching 
    // input packets with output frames - it doesn't
    // really matter what the DTS value is initially - it 
    // only matters it increases for each data submitted
    m_dtsId = 27182800000;


    // get the codec ID that was set
    amf_int64  codecID = AV_CODEC_ID_NONE;
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_DECODER_IN_AUDIO_CODEC_ID, &codecID));

    // find the correct codec
    const AVCodec *pCodec = avcodec_find_decoder((AVCodecID) codecID);
    AMF_RETURN_IF_FALSE(pCodec != nullptr, AMF_CODEC_NOT_SUPPORTED, L"FFmpeg codec %S is not supported", avcodec_get_name((AVCodecID) codecID));
    
    // allocate the codec context
    m_pCodecContext = avcodec_alloc_context3(pCodec);
    AMF_RETURN_IF_FALSE(m_pCodecContext != nullptr, AMF_CODEC_NOT_SUPPORTED, L"FFmpeg codec %S could not allocate codec context", avcodec_get_name((AVCodecID) codecID));


    // initialize the codec context from the component properties
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_DECODER_IN_AUDIO_BIT_RATE, &m_pCodecContext->bit_rate));
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_DECODER_IN_AUDIO_CHANNELS, &m_pCodecContext->ch_layout.nb_channels));
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_DECODER_IN_AUDIO_BLOCK_ALIGN, &m_pCodecContext->block_align));
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_DECODER_IN_AUDIO_CHANNEL_LAYOUT, &m_pCodecContext->ch_layout.u.mask));
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_DECODER_IN_AUDIO_FRAME_SIZE, &m_pCodecContext->frame_size));

    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_DECODER_IN_AUDIO_SAMPLE_RATE, &m_pCodecContext->sample_rate));
    m_pCodecContext->time_base.num = m_pCodecContext->sample_rate;
    m_pCodecContext->time_base.den = 1;

    AMFVariant val;
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_DECODER_IN_AUDIO_EXTRA_DATA, &val));
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
            AMF_RETURN_IF_FALSE(false, AMF_NOT_SUPPORTED, L"FFmpeg could not determine audio sample format for codec %S", avcodec_get_name((AVCodecID) codecID));
    }

	AMFTraceInfo(AMF_FACILITY, L"AudioDecoder:IN codec=%S format=%d rate=%d channels=%d layout=%lld frame-size=%d",
            avcodec_get_name((AVCodecID) codecID), m_pCodecContext->sample_fmt, m_pCodecContext->sample_rate, m_pCodecContext->ch_layout.nb_channels,
            m_pCodecContext->ch_layout.u.mask, m_pCodecContext->frame_size);
    if (avcodec_open2(m_pCodecContext, pCodec, NULL) < 0)
    {
        Terminate();
        AMF_RETURN_IF_FALSE(false, AMF_DECODER_NOT_PRESENT, L"FFmpeg codec %S failed to open", avcodec_get_name((AVCodecID) codecID));
    }

    // MM get - around some WMV audio codecs
    if(m_pCodecContext->ch_layout.u.mask == 0)
    {
        m_pCodecContext->ch_layout.u.mask = AV_CH_FRONT_LEFT | AV_CH_FRONT_RIGHT;
    }

	switch (m_pCodecContext->sample_fmt)
	{
	case AV_SAMPLE_FMT_U8:   sampleFormat = AMFAF_U8;    break;
	case AV_SAMPLE_FMT_S16:  sampleFormat = AMFAF_S16;   break;
	case AV_SAMPLE_FMT_S32:  sampleFormat = AMFAF_S32;   break;
	case AV_SAMPLE_FMT_FLT:  sampleFormat = AMFAF_FLT;   break;
	case AV_SAMPLE_FMT_DBL:  sampleFormat = AMFAF_DBL;   break;

	case AV_SAMPLE_FMT_U8P:  sampleFormat = AMFAF_U8P;   break;
	case AV_SAMPLE_FMT_S16P: sampleFormat = AMFAF_S16P;  break;
	case AV_SAMPLE_FMT_S32P: sampleFormat = AMFAF_S32P;  break;
	case AV_SAMPLE_FMT_FLTP: sampleFormat = AMFAF_FLTP;  break;
	case AV_SAMPLE_FMT_DBLP: sampleFormat = AMFAF_DBLP;  break;

	case AV_SAMPLE_FMT_NONE:
	default:
		sampleFormat = AMFAF_UNKNOWN; break;
	}

    // output properties
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_DECODER_OUT_AUDIO_SAMPLE_FORMAT, sampleFormat));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_DECODER_OUT_AUDIO_SAMPLE_RATE, m_pCodecContext->sample_rate));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_DECODER_OUT_AUDIO_CHANNELS, m_pCodecContext->ch_layout.nb_channels));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_DECODER_OUT_AUDIO_CHANNEL_LAYOUT, m_pCodecContext->ch_layout.u.mask));

    const amf_int64  outputBitRate = (amf_int64) GetAudioSampleSize((AMF_AUDIO_FORMAT) sampleFormat) * m_pCodecContext->sample_rate * m_pCodecContext->ch_layout.nb_channels * 8;
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_DECODER_OUT_AUDIO_BIT_RATE, outputBitRate));

    const amf_int64  blockAlign = m_pCodecContext->ch_layout.nb_channels * GetAudioSampleSize((AMF_AUDIO_FORMAT) sampleFormat);
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_DECODER_OUT_AUDIO_BLOCK_ALIGN, blockAlign));

    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_DECODER_ENABLE_DECODING, &m_bDecodingEnabled));


    AMFTraceDebug(AMF_FACILITY, L"AMFAudioDecoderFFMPEGImpl::Init() - Completed for codec %d", m_pCodecContext->codec_id);
	AMFTraceInfo(AMF_FACILITY, L"AudioDecoder:OUT codec=%S format=%lld rate=%d channels=%d layout=%d",
		avcodec_get_name((AVCodecID) codecID), sampleFormat, m_pCodecContext->sample_rate, m_pCodecContext->ch_layout.nb_channels,
        m_pCodecContext->ch_layout.u.mask);

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

    // clear the internally stored buffers
    m_pExtraData = nullptr;
    m_inputData.clear();

    // clean-up codec related items
    if (m_pCodecContext != NULL)
    {
        AMFTraceInfo(AMF_FACILITY, L"Submitted %d, Queried %d", (int)m_audioFrameSubmitCount, (int)m_audioFrameQueryCount);

        avcodec_close(m_pCodecContext);
        av_free(m_pCodecContext);
        m_pCodecContext = NULL;
    }

    m_audioFrameSubmitCount = 0;
    m_audioFrameQueryCount = 0;
    m_ptsLastDataOffset = 0;
    m_bEof = false;
    m_SeekPts = 0;
    m_dtsId = 0;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioDecoderFFMPEGImpl::Drain()
{
    AMFLock lock(&m_sync);

    m_bEof = true;

    // tell decoder to finalize and generate any remaining frames
    return SubmitInput(nullptr);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioDecoderFFMPEGImpl::Flush()
{
    AMFLock lock(&m_sync);

    // need to flush the decoder
    // NOTE: this function just releases any references the decoder might 
    //       keep internally, but the caller's references remain valid
    // NOTE: unlike the encoder, the decoder doesn't seem to have an issue
    //       flushing the buffers
    if (m_pCodecContext != nullptr)
    {
        avcodec_flush_buffers(m_pCodecContext);
    }

    // clear the internally stored buffers
    m_inputData.clear();
    m_bEof = false;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioDecoderFFMPEGImpl::SubmitInput(AMFData* pData)
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
    const amf_int64  dtsId = m_dtsId++;

    // if the internal buffer is empty and we got new data coming in
    // update internal buffer with new information
    // Mikhail mentioned that we always get data for one frame in 
    // from the demuxer, so we shouldn't have to worry about 
    // buffers containing a partial frame
    int ret = 0;
    if (pData != nullptr)
    {
        AMFBufferPtr pAudioBuffer(pData);
        AMF_RETURN_IF_FALSE(pAudioBuffer != nullptr, AMF_INVALID_ARG, L"SubmitInput() - Input should be AudioBuffer");

        // the buffer we receive should already be in host memory - we open
        // files from disk, so this convert should be somewhat redundant
        // but we can leave it there, in case we ever receive some buffer
        // that's not in host memory, we could still work...
        AMF_RESULT err = pAudioBuffer->Convert(AMF_MEMORY_HOST);
        AMF_RETURN_IF_FAILED(err, L"SubmitInput() - Convert(AMF_MEMORY_HOST) failed");


        //
        // the input data contains a frame that came from av_read_frame
        // to that extent, in the case of audio:
        // 
        //      "... it contains an integer number of frames if each frame has
        //       a known fixed size (e.g. PCM or ADPCM data). If the audio frames have
        //       a variable size (e.g. MPEG audio), then it contains one frame."
        //
        // Unlike with older APIs, the packet is always fully consumed, and if it 
        // contains multiple frames (e.g. some audio codecs), will require you to call 
        // avcodec_receive_frame() multiple times afterwards before you can send a new packet.
        AVPacketEx avpkt;
        avpkt.size = static_cast<int32_t>(pAudioBuffer->GetSize());
        avpkt.data = static_cast<uint8_t*>(pAudioBuffer->GetNative());

        // fill packet with information from the buffer
        ReadAVPacketInfo(pAudioBuffer, &avpkt);

        // since filling the packet will overwrite the 
        // dts value, make sure we overwrite it afterwards 
        // with the ID we want to use
        avpkt.dts = dtsId;


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
        // 
        // NOTE: to drain, we need to submit a NULL packet to avcodec_send_packet call
        //       Sending the first flush packet will return success. 
        //       Subsequent ones are unnecessary and will return AVERROR_EOF
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
    AMF_RETURN_IF_FALSE(ret >= 0, AMF_FAIL, L"QueryOutput() - Error sending a packet for decoding - %S", av_make_error_string(errBuffer, sizeof(errBuffer)/sizeof(errBuffer[0]), ret));


    //
    // if we did have a frame to submit, and we succeeded
    // it's time to increment the submitted frames counter
    // we need to keep the input frame as we need to copy
    // properties from it in the output structure
    if (pData != nullptr)
    {
        AMFTransitFrame  transitFrame = { pData, dtsId };
        m_inputData.push_back(transitFrame);
        m_audioFrameSubmitCount++;
    }
                
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioDecoderFFMPEGImpl::QueryOutput(AMFData** ppData)
{
    // check some required parameters
    AMF_RETURN_IF_FALSE(m_pCodecContext != NULL, AMF_NOT_INITIALIZED, L"QueryOutput() - Codec Context not Initialized");
    AMF_RETURN_IF_FALSE(ppData != NULL, AMF_INVALID_ARG, L"QueryOutput() - ppData == NULL");

    // initialize output
    *ppData = nullptr;


    AMFLock lock(&m_sync);


    //
    // prepare a frame to get the decoded data
    AVFrameEx decoded_frame;

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
    int ret = avcodec_receive_frame(m_pCodecContext, &decoded_frame);
    if (ret == AVERROR(EAGAIN))
    {
        // if we finished the frames in the decoder, we should get
        // out as there's nothing to put out and we don't need to remove 
        // any of the queued input frames
        // we also don't have to unref the frame as nothing got out
        // of the decoder at this point
        return AMF_NEED_MORE_INPUT;
    }
    if (ret == AVERROR_EOF)
    {
        // if we're draining, we need to check until we get we get
        // AVERROR_EOF to know the decoder has been fully drained
        return AMF_EOF;
    }

    char  errBuffer[AV_ERROR_MAX_STRING_SIZE] = { 0 };
    AMF_RETURN_IF_FALSE(ret >= 0, AMF_FAIL, L"QueryOutput() - Error decoding frame - %S", av_make_error_string(errBuffer, sizeof(errBuffer)/sizeof(errBuffer[0]), ret));


    //
    // we should find the correct matching buffer if we have one
    AMFData* pInputData = nullptr;
    while (m_inputData.empty() == false)
    {
        // if the current dts is less than what was produced by
        // the decoder, we are no longer interested in that frame
        const AMFTransitFrame& transitFrame = m_inputData.front();
        if (transitFrame.id < decoded_frame.pkt_dts)
        {
            m_inputData.pop_front();
            m_ptsLastDataOffset = 0;
            continue;
        }

        // if we found a matching input packet, return that
        if (transitFrame.id == decoded_frame.pkt_dts)
        {
            pInputData = transitFrame.pData;
            break;
        }

        // we should never really get here - as the input/output 
        // correlation should be provided by ffmpeg - trace and exit
        AMFTraceWarning(AMF_FACILITY, L"Decoded frame id %" LPRId64 L" not found in the input packet", decoded_frame.pkt_dts);
        break;
    }

    // analyze the next steps we need to take now that we've
    // received the decompresed data (if we did receive it)
    const amf_pts  AudioBufferPts = (pInputData != nullptr) ? GetPtsFromFFMPEG(AMFBufferPtr(pInputData), &decoded_frame) : 0;
    if (AudioBufferPts < m_SeekPts)
    {
        // we got a frame out, but it's before the point we
        // seeked to, so don't put that frame out
        return AMF_NEED_MORE_INPUT;
    }


    //
    // if we did get samples out, it's time to process that
    // information and return the data
    if (decoded_frame.nb_samples > 0)
    {
        // get the sample format of the input
        amf_int64  sampleFormat = AMFAF_UNKNOWN;
        GetProperty(AUDIO_DECODER_IN_AUDIO_SAMPLE_FORMAT, &sampleFormat);


        //
        // allocate a buffer large enough to contain the data we decoded
        // if the allocation fails, we have a bigger issue than trying to 
        // recover from that, but try anyway
        AMFAudioBufferPtr  pOutputAudioBuffer;
        AMF_RESULT err = m_pContext->AllocAudioBuffer(
            AMF_MEMORY_HOST,
            (AMF_AUDIO_FORMAT) sampleFormat,
            decoded_frame.nb_samples,
            m_pCodecContext->sample_rate,
            m_pCodecContext->ch_layout.nb_channels,
            &pOutputAudioBuffer);
        AMF_RETURN_IF_FAILED(err, L"QueryOutput() - AllocAudioBuffer failed");

        amf_uint8* pMemOut = static_cast<amf_uint8*>(pOutputAudioBuffer->GetNative());
        AMF_RETURN_IF_INVALID_POINTER(pMemOut, L"QueryOutput() - AllocAudioBuffer failed - pMemOut is NULL");


        //
        // copy data to output buffer
        const amf_bool   isPlanar        = IsAudioPlanar((AMF_AUDIO_FORMAT) sampleFormat);
        const amf_int32  audioSampleSize = GetAudioSampleSize((AMF_AUDIO_FORMAT) sampleFormat) * (isPlanar ? 1 : m_pCodecContext->ch_layout.nb_channels);
        const amf_int32  outputChannels  = isPlanar ? m_pCodecContext->ch_layout.nb_channels : 1;
        for (amf_int32 ch = 0; ch < outputChannels; ch++)
        {
            AMF_RETURN_IF_INVALID_POINTER(decoded_frame.data[ch], L"QueryOutput() - decoded_frame.data[%d] is NULL", ch);

            const int  iDataSize = audioSampleSize * decoded_frame.nb_samples;
            memcpy(pMemOut, decoded_frame.data[ch], iDataSize);
            pMemOut += iDataSize;
        }


        //
        // update output buffer information
        const amf_pts  tmpPts   = (amf_pts(AMF_SECOND) * decoded_frame.nb_samples);
        const amf_pts  duration = (tmpPts / m_pCodecContext->sample_rate);
        pOutputAudioBuffer->SetPts(AudioBufferPts + m_ptsLastDataOffset);
        pOutputAudioBuffer->SetDuration(duration);
        m_ptsLastDataOffset += duration;

        // copy input buffer properties
        if (pInputData != nullptr)
        {
            pInputData->CopyTo(pOutputAudioBuffer, false);
        }

        *ppData = pOutputAudioBuffer.Detach();
        m_audioFrameQueryCount++;
    }

    // if we get a frame out from avcodec_receive_frame, we need 
    // to repeat until we get an error that more input needs to 
    // be submitted or we get EOF
    // our documentation states that OK will return a frame out,
    // while repeat says no frame came out so it's up to the caller
    // to repeat QueryOutput while getting AMF_OK
    return (*ppData != nullptr) ? AMF_OK : AMF_REPEAT;
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
            Flush();
            m_SeekPts = seekPts;
        }
    }
}
