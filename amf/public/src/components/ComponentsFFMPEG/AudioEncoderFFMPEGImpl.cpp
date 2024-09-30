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
#include "AudioEncoderFFMPEGImpl.h"
#include "UtilsFFMPEG.h"

#include "public/include/core/Context.h"
#include "public/include/core/Trace.h"
#include "public/common/TraceAdapter.h"

#define AMF_FACILITY L"AMFAudioEncoderFFMPEGImpl"

using namespace amf;


const int  g_frameCacheSize = 5;

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
    m_isEncDrained(false),
    m_isDraining(false),
    m_pCodecContext(NULL),
    m_firstFFMPEGPts(LLONG_MIN),
    m_firstFramePts(-1LL),
    m_audioFrameSubmitCount(0),
    m_audioFrameQueryCount(0),
    m_inSampleFormat(AMFAF_UNKNOWN),
    m_samplePreProcRequired(false),
    m_channelCount(0),
    m_sampleRate(0)
{
    g_AMFFactory.Init();

    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoBool(AUDIO_ENCODER_ENABLE_DEBUGGING, L"Enable Debug", false, false),
        AMFPropertyInfoBool(AUDIO_ENCODER_ENABLE_ENCODING, L"Enable encoding", true, false),

        AMFPropertyInfoInt64(AUDIO_ENCODER_AUDIO_CODEC_ID, L"Codec ID", AV_CODEC_ID_NONE, AV_CODEC_ID_NONE, INT_MAX, false),

        AMFPropertyInfoInt64(AUDIO_ENCODER_IN_AUDIO_SAMPLE_RATE, L"Sample Rate In", 44100, 0, INT_MAX, false),
        AMFPropertyInfoInt64(AUDIO_ENCODER_IN_AUDIO_CHANNELS, L"Number of channels in (0 - default)", 2, 0, 100, false),
        AMFPropertyInfoEnum(AUDIO_ENCODER_IN_AUDIO_SAMPLE_FORMAT, L"Sample Format In", AMFAF_FLTP, AMF_SAMPLE_FORMAT_ENUM_DESCRIPTION, false),
        AMFPropertyInfoInt64(AUDIO_ENCODER_IN_AUDIO_CHANNEL_LAYOUT, L"Channel layout in (3 - default)", 3, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AUDIO_ENCODER_IN_AUDIO_BLOCK_ALIGN, L"Block Align In", 0, 0, INT_MAX, true),

        AMFPropertyInfoInt64(AUDIO_ENCODER_OUT_AUDIO_BIT_RATE, L"Compression Bit Rate Out", 128000, 0, INT_MAX, false),
        AMFPropertyInfoInterface(AUDIO_ENCODER_OUT_AUDIO_EXTRA_DATA, L"Extra Data Out", NULL, false),
        AMFPropertyInfoInt64(AUDIO_ENCODER_OUT_AUDIO_SAMPLE_RATE, L"Sample Rate Out", 44100, 0, INT_MAX, false),
        AMFPropertyInfoInt64(AUDIO_ENCODER_OUT_AUDIO_CHANNELS, L"Number of channels out (0 - default)", 2, 0, 100, false),
        AMFPropertyInfoEnum(AUDIO_ENCODER_OUT_AUDIO_SAMPLE_FORMAT, L"Sample Format Out", AMFAF_S16, AMF_SAMPLE_FORMAT_ENUM_DESCRIPTION, false),
        AMFPropertyInfoInt64(AUDIO_ENCODER_OUT_AUDIO_CHANNEL_LAYOUT, L"Channel layout our (0 - default)", 0, 0, INT_MAX, false),
        AMFPropertyInfoInt64(AUDIO_ENCODER_OUT_AUDIO_BLOCK_ALIGN, L"Block Align Out", 0, 0, INT_MAX, false),
        AMFPropertyInfoInt64(AUDIO_ENCODER_OUT_AUDIO_FRAME_SIZE, L"Frame Size Out", 0, 0, INT_MAX, false),
    AMFPrimitivePropertyInfoMapEnd

    InitFFMPEG();
}
//-------------------------------------------------------------------------------------------------
AMFAudioEncoderFFMPEGImpl::~AMFAudioEncoderFFMPEGImpl()
{
    Terminate();
    g_AMFFactory.Terminate();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioEncoderFFMPEGImpl::Init(AMF_SURFACE_FORMAT /*format*/, amf_int32 /*width*/, amf_int32 /*height*/)
{
    AMFLock lock(&m_sync);

    // clean up any information we might previously have
    Terminate();

    // reset the forced EOF flag
    m_bEof = false;

    // reset the draining flag as we're not draining at this point
    m_isDraining = false;
    m_isEncDrained = false;

    // reset the first frame pts offset
    m_firstFramePts = -1LL;
    m_firstFFMPEGPts = LLONG_MIN;

    // get the codec ID that was set
    amf_int64  codecID = AV_CODEC_ID_NONE;
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_ENCODER_AUDIO_CODEC_ID, &codecID));

    // find the correct codec
    const AVCodec* pCodec = avcodec_find_encoder((AVCodecID) codecID);
    AMF_RETURN_IF_FALSE(pCodec != nullptr, AMF_CODEC_NOT_SUPPORTED, L"FFmpeg codec %S is not supported", avcodec_get_name((AVCodecID) codecID));

    // allocate the codec context
    m_pCodecContext = avcodec_alloc_context3(pCodec);
    AMF_RETURN_IF_FALSE(m_pCodecContext != nullptr, AMF_CODEC_NOT_SUPPORTED, L"FFmpeg codec %S could not allocate codec context", avcodec_get_name((AVCodecID) codecID));


    //
    // initialize the codec context from the component properties
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_ENCODER_IN_AUDIO_CHANNELS, &m_channelCount));
    int channelLayout = 0;
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_ENCODER_IN_AUDIO_CHANNEL_LAYOUT, &channelLayout));
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_ENCODER_IN_AUDIO_SAMPLE_RATE, &m_sampleRate));
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_ENCODER_OUT_AUDIO_BIT_RATE, &m_pCodecContext->bit_rate));
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_ENCODER_IN_AUDIO_BLOCK_ALIGN, &m_pCodecContext->block_align));

    // choose closest sample rate to requested
    amf_int bestSampleRate = 0;
    for (const int* sampleRate = pCodec->supported_samplerates; sampleRate != nullptr && *sampleRate != 0; sampleRate++)
    {
        if (abs(*sampleRate - m_sampleRate) < abs(bestSampleRate - m_sampleRate))
        {
            bestSampleRate = *sampleRate;
        }
    }
    if (m_sampleRate != bestSampleRate && bestSampleRate != 0)
    {
        AMFTraceWarning(AMF_FACILITY, L"Codec didn't have sample rate %d Hz, using %d Hz instead", m_sampleRate, bestSampleRate);
        m_sampleRate = bestSampleRate;
    }

    av_channel_layout_from_mask(&m_pCodecContext->ch_layout, channelLayout);
    m_pCodecContext->ch_layout.nb_channels = m_channelCount;
    m_pCodecContext->sample_rate = m_sampleRate;


    //
    // figure out the surface format conversion
    amf_int64 format = AMFAF_UNKNOWN;
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_ENCODER_IN_AUDIO_SAMPLE_FORMAT, &format));
    m_inSampleFormat = (AMF_AUDIO_FORMAT)format;
    m_pCodecContext->sample_fmt = GetFFMPEGAudioFormat(m_inSampleFormat);

    // verify format in codec
    if (pCodec->sample_fmts != NULL)
    {
        bool bFound = false;
        for(int i = 0; ; i++)
        {
            // array is terminated by -1 which is why
            // the loop has no test condition
            if(pCodec->sample_fmts[i] == -1)
            {
                break;
            }
            if(pCodec->sample_fmts[i] == m_pCodecContext->sample_fmt)
            {
                bFound = true;
                break;
            }
        }
        if (!bFound)
        {
            m_pCodecContext->sample_fmt = pCodec->sample_fmts[0];
            m_inSampleFormat = GetAMFAudioFormat(m_pCodecContext->sample_fmt);
            SetProperty(AUDIO_ENCODER_IN_AUDIO_SAMPLE_FORMAT, m_inSampleFormat);
        }
    }

    // set all default parameters
    //m_pCodecContext->strict_std_compliance = FF_COMPLIANCE_STRICT;
    m_pCodecContext->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

    m_pCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    m_pCodecContext->time_base.num = 1;
    m_pCodecContext->time_base.den = m_sampleRate;


    //
    // we set-up the information we want FFmpeg to use - now it's
    // time to get the codec and be ready to start encoding
    if (avcodec_open2(m_pCodecContext, pCodec, NULL) < 0 || m_pCodecContext->codec == 0)
    {
        Terminate();
        AMF_RETURN_IF_FALSE(false, AMF_ENCODER_NOT_PRESENT, L"FFmpeg codec %S failed to open", avcodec_get_name((AVCodecID)codecID));
    }


    //
    // we've created the correct FFmpeg information, now
    // it's time to update the component properties with
    // the data that we obtained
    //
    // input properties
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_IN_AUDIO_CHANNELS, m_pCodecContext->ch_layout.nb_channels));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_IN_AUDIO_SAMPLE_RATE, m_sampleRate));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_IN_AUDIO_SAMPLE_FORMAT, GetAMFAudioFormat(m_pCodecContext->sample_fmt)));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_IN_AUDIO_CHANNEL_LAYOUT, m_pCodecContext->ch_layout.u.mask));

    // output properties
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_OUT_AUDIO_CHANNELS, m_pCodecContext->ch_layout.nb_channels));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_OUT_AUDIO_SAMPLE_RATE, m_sampleRate));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_OUT_AUDIO_SAMPLE_FORMAT, m_inSampleFormat));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_OUT_AUDIO_CHANNEL_LAYOUT, m_pCodecContext->ch_layout.u.mask));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_OUT_AUDIO_FRAME_SIZE, m_pCodecContext->frame_size));

    const amf_int64  blockAlign = m_channelCount * GetAudioSampleSize((AMF_AUDIO_FORMAT) GetAMFAudioFormat(m_pCodecContext->sample_fmt));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_OUT_AUDIO_BLOCK_ALIGN, blockAlign));

    // allocate a buffer to store the extra data
    // we can't store the m_pCodecContext->extradata
    // directly as we wouldn't know its size (in one property)
    if (m_pCodecContext->extradata_size != 0)
    {
        AMFBufferPtr spBuffer;
        AMF_RESULT err = m_pContext->AllocBuffer(AMF_MEMORY_HOST, m_pCodecContext->extradata_size, &spBuffer);
        if ((err == AMF_OK) && (spBuffer->GetNative() != nullptr))
        {
            memcpy(spBuffer->GetNative(), m_pCodecContext->extradata, m_pCodecContext->extradata_size);
            AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_OUT_AUDIO_EXTRA_DATA, spBuffer));
        }
    }

    // figure out if the encoder supports variable samples
    m_samplePreProcRequired = (pCodec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) ? false : true;

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

    m_firstFramePts = -1LL;
    m_firstFFMPEGPts = LLONG_MIN;
    m_samplePreProcRequired = false;

    m_audioFrameSubmitCount = 0;
    m_audioFrameQueryCount = 0;

    m_bEof = false;
    m_isDraining = false;
    m_isEncDrained = false;

    m_recycledFrames.clear();
    m_pPartialFrame = nullptr;
    m_partialFrameSampleCount = 0;

    m_inputFrames.clear();
    m_outputFrames.clear();
    m_submittedFrames.clear();

    // clean-up codec related items
    if (m_pCodecContext != NULL)
    {
        AMFTraceInfo(AMF_FACILITY, L"Submitted %d, Queried %d", (int)m_audioFrameSubmitCount, (int)m_audioFrameQueryCount);

        avcodec_close(m_pCodecContext);
        av_free(m_pCodecContext);
        m_pCodecContext = NULL;
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioEncoderFFMPEGImpl::Drain()
{
    AMFLock lock(&m_sync);


    //
    // if we already drained once, we shouldn't have
    // anything else to drain so we should just get out
    // we need to set this flag because we don't want these
    // frames to be reprocessed in the SplitOrCombine code
    // as they have already set to their appropriate size
    if (m_isDraining == true)
    {
        return AMF_OK;
    }
    m_isDraining = true;


    //
    // if we have a partial frame left, we need to submit it also
    if (m_pPartialFrame != nullptr)
    {
        // at this point, the only thing that's left to
        // send to the encoder is the final partial frame
        // NOTE: need to make the final frame proper so it contains only the
        //       partial information not a portion of a full frame which
        //       will cause the encoder to get more info than it should
        AMF_RESULT ret = SplitOrCombineFrames(m_pPartialFrame, m_partialFrameSampleCount);
        AMF_RETURN_IF_FAILED(ret, L"Drain() - Splitting or combining audio samples was required but failed");
    }


    //
    // no more frames left to process, so it's time to
    // tell the encoder that no more frames are coming
    if (m_bEof == false)
    {
        m_inputFrames.push_back(nullptr);

        // at this point we should've reached the end of
        // the stream
        m_bEof = true;
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioEncoderFFMPEGImpl::Flush()
{
    AMFLock lock(&m_sync);

    // need to flush the encoder
    // NOTE: it will only work if encoder declares
    //       support for AV_CODEC_CAP_ENCODER_FLUSH
    // NOTE: if we can't flush the encoder we need
    //       to reinitialize it, otherwise we will
    //       get incorrect results
    if (m_pCodecContext != nullptr)
    {
        const int caps = ( m_pCodecContext->codec != nullptr) ? m_pCodecContext->codec->capabilities : 0;
        if (caps & AV_CODEC_CAP_ENCODER_FLUSH)
        {
            avcodec_flush_buffers(m_pCodecContext);
        }
        else
        {
            // re-initialize the encoder
            AMF_RESULT res = ReInit(0, 0);
            if (res != AMF_OK)
            {
                avcodec_close(m_pCodecContext);
                av_free(m_pCodecContext);
                m_pCodecContext = NULL;
                return res;
            }
        }
    }

    // clean any remaining variables
    m_firstFramePts = -1LL;
    m_firstFFMPEGPts = LLONG_MIN;

    m_recycledFrames.clear();
    m_pPartialFrame = nullptr;
    m_partialFrameSampleCount = 0;

    m_inputFrames.clear();
    m_outputFrames.clear();
    m_submittedFrames.clear();

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioEncoderFFMPEGImpl::SubmitInput(AMFData* pData)
{
    // check some required parameters
    AMF_RETURN_IF_FALSE(m_pCodecContext != NULL, AMF_NOT_INITIALIZED, L"SubmitInput() - Codec Context not Initialized");


    AMFLock lock(&m_sync);

    // if encoding's been disabled, no need to proceed any further
    if (!m_bEncodingEnabled)
    {
        return m_bEof ? AMF_EOF : AMF_OK;
    }

    // update the first frame pts offset - we'll be using
    // this to figure out output pts information later on
    if (pData && (m_firstFramePts == -1))
    {
        m_firstFramePts = pData->GetPts();
    }

    // if we have too many input frames, wait for some
    // to be processed before accepting any more frames
    // as we don't want for the queue to grow uncontrolled
    if (m_inputFrames.size() > g_frameCacheSize)
    {
         return AMF_INPUT_FULL;
    }

    // if we receive null frame, that means EOF, push null
    // frame to the encoder and exit
    if (pData == nullptr)
    {
        m_bEof = true;
        m_inputFrames.push_back(nullptr);
        return AMF_OK;
    }


    //
    // if we get a valid frame, it should
    // be an audio buffer, so double check for that
    AMFAudioBufferPtr pAudioBuffer(pData);
    AMF_RETURN_IF_FALSE(pAudioBuffer != nullptr, AMF_INVALID_ARG, L"SubmitInput() - Input should be AudioBuffer");

    // check the input data matches what the encoder expects
    // there's no point in somehow getting a frame that's of a
    // different format, or other properties encoder doesn't expect
    AMF_RETURN_IF_FALSE(pAudioBuffer->GetSampleFormat() == m_inSampleFormat, AMF_INVALID_ARG, L"SubmitInput() - Input buffer sample format (%d) is different than what encoder was initialized to (%d)", pAudioBuffer->GetSampleFormat(), m_inSampleFormat);
    AMF_RETURN_IF_FALSE(pAudioBuffer->GetChannelCount() == m_channelCount, AMF_INVALID_ARG, L"SubmitInput() - Input buffer channel count (%d) is different than what encoder was initialized to (%d)", pAudioBuffer->GetChannelCount(), m_channelCount);
    AMF_RETURN_IF_FALSE(pAudioBuffer->GetSampleRate() == m_sampleRate, AMF_INVALID_ARG, L"SubmitInput() - Input buffer sample rate (%d) is different than what encoder was initialized to (%d)", pAudioBuffer->GetSampleRate(), m_sampleRate);

    // whatever information we receive, FFmpeg processes it
    // in host memory, so we have to make sure the data is
    // in the correct memory, hence the Convert at this point
    AMF_RESULT err = pAudioBuffer->Convert(AMF_MEMORY_HOST);
    AMF_RETURN_IF_FAILED(err, L"SubmitInput() - Convert(AMF_MEMORY_HOST) failed");


    //
    // if by any chance the frame size coming in is different what the
    // encoder requires to process for the format specified, we need
    // to combine or split the frames properly because the encoder will
    // not do that internally
    //
    // a good example for this would be from going from ac3 -> aac or
    // the other way around
    //
    // If AV_CODEC_CAP_VARIABLE_FRAME_SIZE is set, then each frame can have any number of
    // samples. If it is not set, frame->nb_samples must be equal to avctx->frame_size for
    // all frames except the last. The final frame may be smaller than avctx->frame_size
    //
    // NOTE: in some cases, for audio, the frame_size is set to 0, in which case the buffer
    //       should be passed directly to the encoder, without any combining or splitting
    //       (an example of this would be wav)
    //
    // NOTE: if we're draining, so we set m_isDraining, we're actually sending in
    //       the combined/split frames so we don't want to do that again
    //
    // NOTE: if by any chance we are splitting/combining the packets but the frame coming
    //       in is the exact same size as required, if we don't have anything cached we
    //       should just send the frame as is, without any combine/split, but if we
    //       have a split frame or the cached is not empty, we have to add it so we don't
    //       send stuff out of order to the encoder
    const amf_int32  inSampleCount = pAudioBuffer->GetSampleCount();
    if (m_samplePreProcRequired &&
        !m_isDraining &&
        (m_pCodecContext->frame_size > 0) &&
        ((inSampleCount != m_pCodecContext->frame_size) || (m_pPartialFrame != nullptr)) )
    {
        // limit the size of the queue we use for storing processed frames
        // this should only matter when we go from large samples to small
        // samples as we can create multiple packets and we don't want the
        // queue to increase to unmanageable size
        //
        // in that case, we don't process the incoming frame but we still
        // send what we have to the decoder
        //
        // this way we don't have to check if the frame we receive is the
        // same one we didn't process as we leave the code in the same
        // state as it was before - as we didn't add the new frame to the
        // combined/split data
        err = SplitOrCombineFrames(pAudioBuffer, m_pCodecContext->frame_size);
        if (err == AMF_NEED_MORE_INPUT)
        {
            // if we haven't managed to form a
            // full frame, then get more input
            return AMF_NEED_MORE_INPUT;
        }
        AMF_RETURN_IF_FAILED(err, L"SubmitInput() - Splitting or combining audio samples was required but failed");
    }
    else
    {
        // we will be submitting the input frames as we get them and
        // we will do the processing in QueryOutput
        // NOTE: we're making this change because there is an issue
        //       with AAC (and M4A) where we submit the same data in
        //       and the results coming out different when using
        //       transcodeHW - this seems to be an issue in FFmpeg
        //       having to deal with multi-threading
        m_inputFrames.push_back(pAudioBuffer);
    }

    return m_bEof ? AMF_EOF : AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioEncoderFFMPEGImpl::QueryOutput(AMFData** ppData)
{
    // check required parameters
    AMF_RETURN_IF_FALSE(m_pCodecContext != NULL, AMF_NOT_INITIALIZED, L"QueryOutput() - Codec Context not Initialized");
    AMF_RETURN_IF_FALSE(ppData != NULL, AMF_INVALID_ARG, L"QueryOutput() - ppData == NULL");

    // initialize output
    *ppData = nullptr;


    AMFLock lock(&m_sync);


    //
    // if we have any encoded frames, return now
    if (m_outputFrames.empty() == false)
    {
        AMFBufferPtr  pBufferOut = m_outputFrames.front();
        m_outputFrames.pop_front();

        // prepare output
        *ppData = pBufferOut.Detach();
        m_audioFrameQueryCount++;

        return AMF_OK;
    }


    //
    // if we have received any frames, keep submitting until we
    // get input full or we run out of frames
    // NOTE: for EOF we should receive an empty pointer in the
    //       input frames list - once we submit that, we should
    //       have an empty input list and the loop for submitting
    //       more data should not execute
    while (m_inputFrames.empty() == false)
    {
        AMFAudioBuffer* pInBuffer = m_inputFrames.front();
        AMF_RESULT      retCode   = SubmitFrame(pInBuffer);

        // if we succeeded in submitting the frame, take it
        // out from the input queue and go to the next frame
        // NOTE: if submit succeeded, that probably means we
        //       have output available so go get that output
        //       now and don't submit any more frames just
        //       in case it can't buffer the output data
        // NOTE: if we change the break; co continue; it has
        //       issues with AAC - looks like it doesn't like
        //       to submit frames till it returns with
        //       input full - seems caching's not working
        //       right, if there's any caching at all
        if (retCode == AMF_OK)
        {
            m_inputFrames.pop_front();
            break;
        }

        // if we submitted a NULL frame to terminate the stream
        // and we get an EOF return, we need to let the encoder
        // drain whatever encoded frames might still exist
        if ((retCode == AMF_EOF) && (pInBuffer == nullptr))
        {
            m_inputFrames.pop_front();
            break;
        }

        // if the input is full, time to start getting some
        // frames out, so exit the loop
        if (retCode == AMF_INPUT_FULL)
        {
            break;
        }

        return retCode;
    }


    //
    // try to retrieve any encoded frames
    // if we didn't get any frames out return
    // as we have no frames to supply yet
    AMF_RESULT  retCode = (m_isEncDrained == false) ? RetrievePackets() : AMF_EOF;

    // if we get a frame out from avcodec_receive_packet, we need
    // to repeat until we get an error that more input needs to
    // be submitted or we get EOF
    // our documentation states that OK will return a frame out,
    // while repeat says no frame came out so it's up to the caller
    // to repeat QueryOutput while getting AMF_OK
    return (m_outputFrames.empty() == false) ? AMF_REPEAT : retCode;
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
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL  AMFAudioEncoderFFMPEGImpl::GetNewFrame(AMFAudioBuffer** ppNewBuffer, amf_int32 requiredSize)
{
    AMF_RETURN_IF_FALSE(m_pCodecContext != nullptr, AMF_NOT_INITIALIZED, L"GetNewFrame() - Codec Context not Initialized");
    AMF_RETURN_IF_FALSE(m_pContext != nullptr, AMF_NOT_INITIALIZED, L"GetNewFrame() - AMF Context not initialized");
    AMF_RETURN_IF_FALSE(ppNewBuffer != nullptr, AMF_INVALID_ARG, L"GetNewFrame() - pInBuffer == NULL");


    //
    // check if we have a cached frame we could use instead of allocating
    // another frame and waste an allocation
    if ((m_recycledFrames.empty() == false) &&
        (m_recycledFrames.front()->GetSampleCount() == requiredSize) &&
        (m_recycledFrames.front()->GetSampleFormat() == m_inSampleFormat) &&
        (m_recycledFrames.front()->GetChannelCount() == m_channelCount))
    {
        // if we have previously allocated data
        // get the previously allocated pointer
        AMFAudioBufferPtr pRecycledFrame = m_recycledFrames.front();
        m_recycledFrames.pop_front();

        // clear its properties so we can set/copy
        // new ones from the passed in buffer
        pRecycledFrame->Clear();
        memset(pRecycledFrame->GetNative(), 0, pRecycledFrame->GetSize());

        *ppNewBuffer = pRecycledFrame.Detach();
        return AMF_OK;
    }


    //
    // allocate a new frame to fill
    AMF_RESULT err = m_pContext->AllocAudioBuffer(AMF_MEMORY_HOST, m_inSampleFormat, requiredSize, m_sampleRate, m_channelCount, ppNewBuffer);
    AMF_RETURN_IF_FAILED(err, L"GetNewFrame() - AllocAudioBuffer failed");

    // clear the contents
    memset((*ppNewBuffer)->GetNative(), 0, (*ppNewBuffer)->GetSize());

    return err;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL  AMFAudioEncoderFFMPEGImpl::SplitOrCombineFrames(AMFAudioBuffer* pInBuffer, amf_int32 requiredSize)
{
    AMF_RETURN_IF_FALSE(m_pCodecContext != nullptr, AMF_NOT_INITIALIZED, L"SplitOrCombineFrames() - Codec Context not Initialized");
    AMF_RETURN_IF_FALSE(m_pContext != nullptr, AMF_NOT_INITIALIZED, L"SplitOrCombineFrames() - AMF Context not initialized");
    AMF_RETURN_IF_FALSE(pInBuffer != nullptr, AMF_INVALID_ARG, L"SplitOrCombineFrames() - pInBuffer == NULL");


    //
    // For planar sample formats, each audio channel is in a separate
    // data plane, and linesize is the buffer size, in bytes, for a
    // single plane. All data planes must be the same size.
    //      AAAAAAAAAAAAAAAAAAAABBBBBBBBBBBBBBBBBBBB
    // For packed sample formats, only the first data plane is used,
    // and samples for each channel are interleaved. In this case,
    // linesize is the buffer size, in bytes, for the 1 plane.
    //      ABABABABABABABABABABABABABABABABABABABAB
    // NOTE: we are combining on the same format - we're not changing
    //       format here to go from planar to packed or the other way around
    // NOTE: if we receive the partial frame as input (as the last frame
    //       we need to handle, we need to pick up only the partial data
    //       so the sample count should be the the data we need to get, not
    //       the sample count of the buffer
    const amf_bool   isPlanar      = IsAudioPlanar(m_inSampleFormat);
    const amf_int32  planar        = isPlanar ? 1 : m_channelCount;
    const amf_int32  planes        = isPlanar ? m_channelCount : 1;
    const amf_int32  sampleSize    = GetAudioSampleSize((AMF_AUDIO_FORMAT) m_inSampleFormat);
    const amf_int32  inSampleCount = (pInBuffer != m_pPartialFrame) ? pInBuffer->GetSampleCount() : requiredSize;
    const amf_pts    inDuration    = pInBuffer->GetDuration();


    //
    // while we have data in the input buffer we
    // want to create new frames that contain the
    // correct number of samples for the encoder
    amf_int32  inSamplesConsumed    = 0;
    amf_int32  fullSamplesGenerated = 0;
    while (inSamplesConsumed < inSampleCount)
    {
        // if we have a partial frame, fill the partial frame first
        // otherwise create a new frame
        AMFAudioBufferPtr  pFrameToFill;
        amf_int32          existingSampleCount = 0;
        if ((m_pPartialFrame != nullptr) && (pInBuffer != m_pPartialFrame))
        {
            AMF_RETURN_IF_FALSE(m_pPartialFrame->GetSampleCount() == requiredSize, AMF_UNEXPECTED, L"SplitOrCombineFrames() - partial frame does not contain enough samples");

            // the partial frame is the one we need to add samples into
            pFrameToFill = m_pPartialFrame;
            existingSampleCount = m_partialFrameSampleCount;
        }
        else
        {
            // get a new (well, or recycled) frame to fill
            AMF_RESULT err = GetNewFrame(&pFrameToFill, requiredSize);
            AMF_RETURN_IF_FAILED(err, L"SplitOrCombineFrames() - obtaining a new frame failed");

            // as we're starting a new frame, it's time
            // to copy the incoming properties into it
            pInBuffer->CopyTo(pFrameToFill, false);

            // NOTE: Do we need to update the PTS and duration of
            //       the new packet at this point?
            pFrameToFill->SetPts(pInBuffer->GetPts() + inSamplesConsumed * inDuration / inSampleCount);
        }


        //
        // do the data copy
        // NOTE: the partial frame will contain less samples than
        //       what it has been allocated for, as it was meant to
        //       hold a full frame, but when it's the last frame
        //       we need to copy only the relevant data, BUT we need
        //       to go to the next plane correctly, based on how many
        //       samples the buffer was allocated for, to calculate
        //       the correct stride of the buffer
        const amf_int32  sourceSamples    = (pInBuffer != m_pPartialFrame) ? inSampleCount : pInBuffer->GetSampleCount();
        const amf_int32  bufferSamples    = pFrameToFill->GetSampleCount();
        const amf_int32  samplesAvailable = inSampleCount - inSamplesConsumed;
        const amf_int32  samplesRequired  = bufferSamples - existingSampleCount;
        const amf_int32  samplesToCopy    = AMF_MIN(samplesAvailable, samplesRequired);
        const amf_int32  srcOffset        = inSamplesConsumed * sampleSize * planar;
        const amf_int32  destOffset       = existingSampleCount * sampleSize * planar;
        const amf_uint8* pSrcData         = static_cast<amf_uint8*>(pInBuffer->GetNative()) + srcOffset;
              amf_uint8* pDestData        = static_cast<amf_uint8*>(pFrameToFill->GetNative()) + destOffset;
        for (amf_int32 ch = 0; ch < planes; ch++)
        {
            // copy the data from the frame that came in, into the frame going out
            memcpy(pDestData, pSrcData, samplesToCopy * sampleSize * planar);

            // update pointers for next plane (if planar format)
            // otherwise the loop will exit (if non-planar format)
            pSrcData += sourceSamples * sampleSize * planar;
            pDestData += bufferSamples * sampleSize * planar;
        }

        // update consumed/copied samples information
        inSamplesConsumed += samplesToCopy;
        existingSampleCount += samplesToCopy;

        // update the pts and duration of the newly created buffer
        pFrameToFill->SetDuration(existingSampleCount * inDuration / inSampleCount);


        //
        // if the frame is full, add it to the
        // list of frames ready for the encoder
        if (existingSampleCount == bufferSamples)
        {
            // NOTE: if we get m_pPartialFrame as input as last frame
            //       after this point, we shouldn't use pInBuffer as
            //       its ref. count might be 0
            m_inputFrames.push_back(pFrameToFill);
            m_pPartialFrame = nullptr;
            m_partialFrameSampleCount = 0;
            fullSamplesGenerated++;
        }
        else if (existingSampleCount < bufferSamples)
        {
            // otherwise, set the partial frame and exit the
            // loop as there should be no data left to process
            m_pPartialFrame = pFrameToFill;
            m_partialFrameSampleCount = existingSampleCount;
            break;
        }
        else
        {
            // we should never really run into this condition
            // where the existing sample count is more than
            // the required size
            AMF_RETURN_IF_FALSE(existingSampleCount <= bufferSamples, AMF_OUT_OF_RANGE, L"SplitOrCombineFrames() - An error occured splitting or combining frames, resulting in a frame larger than expected");
        }
    }

    return (fullSamplesGenerated > 0) ? AMF_OK : AMF_NEED_MORE_INPUT;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL  AMFAudioEncoderFFMPEGImpl::InitializeFrame(AMFAudioBuffer* pInBuffer, AVFrame& avFrame)
{
    AMF_RETURN_IF_FALSE(m_pCodecContext != nullptr, AMF_NOT_INITIALIZED, L"InitializeFrame() - Codec Context not Initialized");
    AMF_RETURN_IF_FALSE(pInBuffer != nullptr, AMF_INVALID_ARG, L"InitializeFrame() - pInBuffer == NULL");


    const amf_bool   isPlanar    = IsAudioPlanar(m_inSampleFormat);
    const amf_int32  planar      = isPlanar ? 1 : m_channelCount;
    const amf_int32  planes      = isPlanar ? m_channelCount : 1;
    const amf_int32  sampleSize  = GetAudioSampleSize((AMF_AUDIO_FORMAT) m_inSampleFormat);
    const amf_int32  sampleCount = pInBuffer->GetSampleCount();

    avFrame.nb_samples = sampleCount;
    avFrame.format = m_pCodecContext->sample_fmt;
    avFrame.ch_layout = m_pCodecContext->ch_layout;
    avFrame.ch_layout.nb_channels = m_channelCount;
    avFrame.sample_rate = m_sampleRate;
    avFrame.key_frame = 1;
    avFrame.pts = av_rescale_q(pInBuffer->GetPts(), AMF_TIME_BASE_Q, m_pCodecContext->time_base);

    // setup the data pointers in the AVFrame
    // NOTE: the FFmpeg documentation mentions that the
    //       data pointers need to point inside AVFrame.buf
    //       pointers - but from debugging FFmpeg it
    //       looks like we can pass the data pointers
    //       and they will allocate the buf internally
    //       then make a copy
    const amf_size  dstPlaneSize = sampleCount * sampleSize * planar;
    for (amf_int ch = 0; ch < planes; ch++)
    {
        avFrame.data[ch] = (uint8_t *)pInBuffer->GetNative() + ch * dstPlaneSize;
    }
    avFrame.extended_data = avFrame.data;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioEncoderFFMPEGImpl::PacketToBuffer(const AVPacket& avPacket, AMFBuffer** ppOutBuffer)
{
    AMF_RETURN_IF_FALSE(avPacket.size > 0, AMF_INVALID_ARG, L"PacketToBuffer() - packet size should be greater than 0");


    // allocate and fill output buffer - if we fail the memory
    // allocation, we probably have more problems than worrying
    // about recovery at this point, but still try
    AMF_RESULT err = m_pContext->AllocBuffer(AMF_MEMORY_HOST, avPacket.size, ppOutBuffer);
    AMF_RETURN_IF_FAILED(err, L"PacketToBuffer() - AllocBuffer failed");

    amf_uint8 *pMemOut = static_cast<uint8_t*>((*ppOutBuffer)->GetNative());
    memcpy(pMemOut, avPacket.data, avPacket.size);

    // get the pts fo the ffmpeg frame returned - we will
    // calculate our pts relative to what ffmpeg gives us
    if (m_firstFFMPEGPts == LLONG_MIN)
    {
        m_firstFFMPEGPts = avPacket.pts;
    }

    // calculate the number of samples we got out
    // for PTS -always 100 nanos
    // just make sure to round off properly
    const amf_pts duration = av_rescale_q(avPacket.duration, m_pCodecContext->time_base, AMF_TIME_BASE_Q);
    const amf_pts pts      = m_firstFramePts + av_rescale_q(avPacket.pts - m_firstFFMPEGPts, m_pCodecContext->time_base, AMF_TIME_BASE_Q);

    (*ppOutBuffer)->SetDuration(duration);
    (*ppOutBuffer)->SetPts(pts);

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioEncoderFFMPEGImpl::UpdatePacketProperties(AMFBuffer* pOutBuffer)
{
    AMF_RETURN_IF_FALSE(pOutBuffer != nullptr, AMF_INVALID_ARG, L"UpdatePacketProperties() - pOutBuffer == NULL");


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
    // so check for a 5% variance and see if it's closer to
    // the next packet, pick that one
    while (m_submittedFrames.empty() == false)
    {
        const AMFAudioBufferPtr pTransitFrame   = m_submittedFrames.front();
        const amf_pts           inFramePts      = pTransitFrame->GetPts();
        const amf_pts           inFrameDuration = pTransitFrame->GetDuration();

        // if the current pts is past the end of the frame
        // we should drop that frame as the frames should
        // come in sequentially
        std::list<AMFAudioBufferPtr>::const_iterator itNext = ++m_submittedFrames.begin();
        const amf_pts                                pts    = pOutBuffer->GetPts();
        if ((pts > inFramePts + inFrameDuration) ||
            ((itNext != m_submittedFrames.end()) && (pts == (*itNext)->GetPts())))
        {
            // if we have room in the cache, recycle the
            // frame to alleviate some allocations
            if (m_recycledFrames.size() < g_frameCacheSize)
            {
                m_recycledFrames.push_back(pTransitFrame);
            }

            m_submittedFrames.pop_front();
            continue;
        }

        // we found the proper frame to copy data from
        // due to round-off errors, we could have say for the
        // first frame pts = 10, duration 5, but the next
        // frame would have pts at 16 instead of 15 so we need
        // to take that into consideration
        // the change will not affect things much as the code
        // below will still pick between current and next frame
        // depending on which one is closer to the pts
        // without this, in the case mentioned, the first frame
        // would never get deleted and the queue will keep growing
        if ((pts >= inFramePts) && (pts <= inFramePts + inFrameDuration))
        {
            // check though if the frame is closer to the next frame, in which
            // case it should probably copy from that frame
            if ((itNext != m_submittedFrames.end()) &&
                (abs((amf_int64) (*itNext)->GetPts() - (amf_int64) pts) < (pts - inFramePts)))
            {
                (*itNext)->CopyTo(pOutBuffer, false);
            }
            else
            {
                pTransitFrame->CopyTo(pOutBuffer, false);
            }
            break;
        }

        // the generated frame is before the current input frame
        // this should not really happen - trace and exit
        AMFTraceWarning(AMF_FACILITY, L"Generated packet pts %" LPRId64 L" is before the input frame pts %" LPRId64 L"", pts, inFramePts);
        break;
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL  AMFAudioEncoderFFMPEGImpl::SubmitFrame(AMFAudioBuffer* pAudioBuffer)
{
    // start submitting information to the encoder
    int  ret = 0;
    if (pAudioBuffer != nullptr)
    {
        // fill the frame information
        AVFrameEx  avFrame;
        AMF_RESULT err = InitializeFrame(pAudioBuffer, avFrame);
        AMF_RETURN_IF_FAILED(err, L"SubmitFrame() - Failed to initialize and set frame properties");

        // since the data that we receive in SubmitFrame should always contain a full frame
        // we should probably never have a have the need to handle partial buffer data
        //
        // AVERROR(EAGAIN): input is not accepted in the current state -
        //                  user must read output with avcodec_receive_packet()
        //                  (once all output is read, the packet should be resent, and the call will not fail with EAGAIN).
        // AVERROR_EOF:     the encoder has been flushed, and no new frames can be sent to it
        // AVERROR(EINVAL): codec not opened, refcounted_frames not set, it is a decoder, or requires flush
        // AVERROR(ENOMEM): failed to add packet to internal queue, or similar other errors: legitimate decoding errors
        ret = avcodec_send_frame(m_pCodecContext, &avFrame);
    }
    else
    {
        // no more frames to encode so now it's time to drain the encoder
        // draining the encoder should happen once
        // otherwise an error code will be returned by avcodec_send_frame
        //
        // NOTE: to drain, we need to submit a NULL packet to avcodec_send_frame call
        //       Sending the first flush packet will return success.
        //       Subsequent ones are unnecessary and will return AVERROR_EOF
        ret = avcodec_send_frame(m_pCodecContext, NULL);

        // NOTE: it is possible that the encoder is full and it won't accept
        //       even a terminating packet (returning ret == AVERROR(EAGAIN))
        //       so we should mark EOF once we processed that terminating packet
        if ((ret == AVERROR_EOF) || (ret == 0))
        {
            // update internal flag that we reached the end of the file
            m_bEof = true;
        }
    }


    //
    // handle the error returns from the encoder
    //
    // NOTE: it is possible the encoder is busy as encoded frames have not
    //       been taken out yet, so it's possible that it will not accept even
    //       to start flushing the data yet, so it can return AVERROR(EAGAIN)
    //       along the normal case when it can't accept a new frame to process
    if (ret == AVERROR(EAGAIN))
    {
        return AMF_INPUT_FULL;
    }
    if (ret == AVERROR_EOF)
    {
        return AMF_EOF;
    }

    // once we get here, we should have no errors after we
    // submitted the frame
    char  errBuffer[AV_ERROR_MAX_STRING_SIZE] = { 0 };
    AMF_RETURN_IF_FALSE(ret >= 0, AMF_FAIL, L"SubmitFrame() - Error sending a frame for encoding - %S", av_make_error_string(errBuffer, sizeof(errBuffer)/sizeof(errBuffer[0]), ret));


    //
    // if we did have a frame to submit, and we succeeded
    // it's time to increment the submitted frames counter
    // also keep the submitted frame to copy properties to
    // the output frame
    if (pAudioBuffer != nullptr)
    {
        m_submittedFrames.push_back(pAudioBuffer);
        m_audioFrameSubmitCount++;
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL  AMFAudioEncoderFFMPEGImpl::RetrievePackets()
{
    while (true)
    {
        // retrieve the encoded packet - it is possible that
        // for some packets, the encoder needs more data, in
        // which case it will return AVERROR(EAGAIN)
        //
        // AVERROR(EAGAIN): output is not available in the current state -
        //                  user must try to send input
        // AVERROR_EOF: the encoder has been fully flushed, and there will be no more output packets
        // AVERROR(EINVAL): codec not opened, or it is an encoder
        // other errors: legitimate decoding errors
        AVPacketEx avPacket;
        int ret = avcodec_receive_packet(m_pCodecContext, &avPacket);
        if (ret == AVERROR(EAGAIN))
        {
            // if we finished the packets in the encoder, we should get
            // out as there's nothing to put out and we don't need to remove
            // any of the queued input frames
            // we also don't have to unref the packet as nothing got out
            // of the encoder at this point
            return AMF_NEED_MORE_INPUT;
        }
        if (ret == AVERROR_EOF)
        {
            // NOTE: it seems encoder doesn't like to be queried multiple times
            //       if it's been drained - it causes a memory exception later
            //       on when closing the programe so don't request information
            //       multiple times once the last frame came out
            m_isEncDrained = true;

            // if we're draining, we need to check until we get we get
            // AVERROR_EOF to know the encoder has been fully drained
            return AMF_EOF;
        }

        char  errBuffer[AV_ERROR_MAX_STRING_SIZE] = { 0 };
        AMF_RETURN_IF_FALSE(ret >= 0, AMF_FAIL, L"RetrievePacket() - Error encoding frame - %S", av_make_error_string(errBuffer, sizeof(errBuffer)/sizeof(errBuffer[0]), ret));


        //
        // if avcodec_receive_packet returns 0,
        // a valid packet should have been provided
        //
        // NOTE: For video, it should typically contain one compressed frame.
        //       For audio it may contain several compressed frames.
        //       Encoders are allowed to output empty packets, with no
        //       compressed data, containing only side data (e.g. to update
        //       some stream parameters at the end of encoding)."
        if (avPacket.size > 0)
        {
            // convert the packet data into an AMFBuffer that will go out
            AMFBufferPtr pBufferOut;
            AMF_RESULT err = PacketToBuffer(avPacket, &pBufferOut);
            AMF_RETURN_IF_FAILED(err, L"RetrievePacket() - conversion of packet to buffer failed");

            // copy input data properties to the encoded output buffer
            UpdatePacketProperties(pBufferOut);

            // add the retrieved packets to a list to be processed a bit later
            // before they go out as we need to set the properties from the
            // input frame if we can
            m_outputFrames.push_back(pBufferOut);
        }
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
