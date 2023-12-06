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
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_ENCODER_IN_AUDIO_CHANNEL_LAYOUT, &m_pCodecContext->channel_layout));
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

    m_pCodecContext->channels = m_channelCount;
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
        AMF_RETURN_IF_FALSE(false, AMF_ENCODER_NOT_PRESENT, L"FFmpeg codec %S failed to open", avcodec_get_name((AVCodecID) codecID));
    }


    //
    // we've created the correct FFmpeg information, now 
    // it's time to update the component properties with
    // the data that we obtained
    // 
    // input properties
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_IN_AUDIO_CHANNELS, m_pCodecContext->channels));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_IN_AUDIO_SAMPLE_RATE, m_sampleRate));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_IN_AUDIO_SAMPLE_FORMAT, GetAMFAudioFormat(m_pCodecContext->sample_fmt)));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_IN_AUDIO_CHANNEL_LAYOUT, m_pCodecContext->channel_layout));

    // output properties
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_OUT_AUDIO_CHANNELS, m_pCodecContext->channels));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_OUT_AUDIO_SAMPLE_RATE, m_sampleRate));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_OUT_AUDIO_SAMPLE_FORMAT, m_inSampleFormat));
    AMF_RETURN_IF_FAILED(SetProperty(AUDIO_ENCODER_OUT_AUDIO_CHANNEL_LAYOUT, m_pCodecContext->channel_layout));
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

    // clean-up codec related items
    if (m_pCodecContext != NULL)
    {
        AMFTraceInfo(AMF_FACILITY, L"Submitted %d, Queried %d", (int)m_audioFrameSubmitCount, (int)m_audioFrameQueryCount);

        avcodec_close(m_pCodecContext);
        av_free(m_pCodecContext);
        m_pCodecContext = NULL;
    }

    m_firstFramePts = -1LL;
    m_firstFFMPEGPts = LLONG_MIN;
    m_samplePreProcRequired = false;
	
    m_audioFrameSubmitCount = 0;
    m_audioFrameQueryCount = 0;
    m_bEof = false;

    m_preparedFrames.clear();
    m_recycledFrames.clear();
    m_pPartialFrame = nullptr;
    m_partialFrameSampleCount = 0;

    m_inputData.clear();

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioEncoderFFMPEGImpl::Drain()
{
    AMFLock lock(&m_sync);

    // if we start draining, flag that we're doing so
    m_bEof = true;


    //
    // if we have remaining frames, start pushing them as we 
    // need to consume them to fully drain the component
    // submit frames until the decoder is full
    while (m_preparedFrames.empty() == false)
    {
        AMF_RESULT retCode = SubmitInput(m_preparedFrames.front());
        if (retCode == AMF_INPUT_FULL)
        {
            // if the encoder is full, submitting any frames again
            // won't help until some frames are drained through
            // QueryOutput, but that wouldn't help since we're under
            // lock so it would be very hard for QueryOutput to come
            // in and get frames out, so we unlock and sleep than
            // lock again before getting out
            lock.Unlock();
            amf_sleep(1);
            lock.Lock();

            return retCode;
        }
    }


    //
    // if we have a partial frame left, we need to submit it also
    if (m_pPartialFrame != nullptr)
    {
        // at this point, the only thing that's left to 
        // send to the encoder is the final partial frame
        AMFAudioBufferPtr  pTempFrame = m_pPartialFrame;
        m_pPartialFrame = nullptr;

        // also clear any possible recycled frames as they 
        // would also have been allocated for the wrong size
        // and if we need a new partial frame it has got to
        // be the correct size
        m_recycledFrames.clear();

        // NOTE: need to make the final frame proper so it contains only the 
        //       partial information not a portion of a full frame which 
        //       will cause the encoder to get more info than it should
        SplitOrCombineFrames(pTempFrame, m_partialFrameSampleCount);
        AMF_RETURN_IF_FALSE(m_preparedFrames.empty() == false, AMF_FAIL, L"Drain() - we should've obtained a 'full' frame at this point");

        // we don't care about any remaining data in this case
        // as the partial frame we split should've been the 
        // ending partial data to be submitted to the encoder
        m_pPartialFrame = nullptr;
        m_partialFrameSampleCount = 0;

        // what can happen is that the remaining partial frame 
        // might be able to generate more than one frame out 
        // (Ex: remaining samples 566, on 1536 sample size)
        // but we only care about the first frame generated
        // the rest would contain invalid information
        while (m_preparedFrames.size() > 1)
        {
            m_preparedFrames.pop_back();
        }

        AMF_RESULT retCode = SubmitInput(m_preparedFrames.front());
        if (retCode == AMF_INPUT_FULL)
        {
            return retCode;
        }
    }


    //
    // no more frames left to process, so it's time to 
    // tell the encoder that no more frames are coming
    // if the encoder can't start flushing frames it 
    // will return AMF_INPUT_FULL from SubmitInput
    return SubmitInput(nullptr);
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

    m_preparedFrames.clear();
    m_recycledFrames.clear();
    m_pPartialFrame = nullptr;
    m_partialFrameSampleCount = 0;

    m_inputData.clear();

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


    //
    // start submitting information to the encoder
    AMFAudioBufferPtr pAudioBuffer;
    amf_bool          resendFrame = false;
    int               ret         = 0;
    if (pData != nullptr)
    {
        pAudioBuffer = AMFAudioBufferPtr(pData);
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
        // NOTE: if we're draining, so we set m_bEof, we're actually sending in 
        //       the combined/split frames so we don't want to do that again
        //
        // NOTE: if by any chance we are splitting/combining the packets but the frame coming
        //       in is the exact same size as required, if we don't have anything cached we 
        //       should just send the frame as is, without any combine/split, but if we 
        //       have a split frame or the cached is not empty, we have to add it so we don't
        //       send stuff out of order to the encoder
        const amf_int32  inSampleCount = pAudioBuffer->GetSampleCount();
        if (m_samplePreProcRequired && !m_bEof &&
            (m_pCodecContext->frame_size > 0) && 
            ((inSampleCount != m_pCodecContext->frame_size) || (m_preparedFrames.empty() == false) || (m_pPartialFrame != nullptr)) )
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
            // 
            // NOTE: it is possible when we request the caller to resend the frame
            //       to create a completely new frame with the same data (in which
            //       case a frame with the same PTS would probably come), but it's
            //       also possible that the old frame is dropped completely and a 
            //       new one is sent instead, which is why checking previous frames
            //       doesn't always work properly
            if (m_preparedFrames.size() <= AMF_MAX(3 * inSampleCount / m_pCodecContext->frame_size, 5))
            {
                err = SplitOrCombineFrames(pAudioBuffer, m_pCodecContext->frame_size);
                AMF_RETURN_IF_FAILED(err, L"SubmitInput() - Splitting or combining audio samples was required but failed");
            }
            else
            {
                resendFrame = true;
            }

            // if we haven't managed to form a 
            // full frame, then get more input
            if (m_preparedFrames.empty() == true)
            {
                return AMF_NEED_MORE_INPUT;
            }

            // if we have any combined or split frames we 
            // should pick those to send to the encoder
            pAudioBuffer = m_preparedFrames.front();
        }


        //
        // fill the frame information
        AVFrameEx  avFrame;
        err = InitializeFrame(pAudioBuffer, avFrame);
        AMF_RETURN_IF_FAILED(err, L"SubmitInput() - Failed to initialize and set frame properties");


        //
        // since the data that we receive in SubmitInput should always contain a full frame
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
        // update internal flag that we reached the end of the file
        m_bEof = true;

        // no more frames to encode so now it's time to drain the encoder
        // draining the encoder should happen once 
        // otherwise an error code will be returned by avcodec_send_frame
        // 
        // NOTE: to drain, we need to submit a NULL packet to avcodec_send_frame call
        //       Sending the first flush packet will return success. 
        //       Subsequent ones are unnecessary and will return AVERROR_EOF
        ret = avcodec_send_frame(m_pCodecContext, NULL);
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
        // the issue here for the case when we need to combine split frames is
        // that we could process the frame coming in, add it to the partial/combined
        // frame buffer, which we then pass to the encoder, but if the encoder comes
        // back and says that it's busy, we can't say the input is full because that
        // would send the frame back, and we've already added it to the partial frame
        // so in that case, we need to say that it's OK, because we will get a new
        // frame coming in, but we'll send to the encoder the processed frame we
        // send the last time so it should all be good
        //
        // NOTE: if we've already set EOF, then if we get here it's because we
        //       drain combined frames we still have so in that case, we need
        //       to return AMF_INPUT_FULL, so the pipeline knows to keep removing
        //       frames through QueryOutput
        //
        // NOTE: the default is if we get normal frames which don't need to be 
        //       combined or split, in which case the prepared frames is empty
        return ((m_preparedFrames.empty() == true) || 
                (m_bEof == true) ||
                (resendFrame == true)) ? AMF_INPUT_FULL : AMF_OK;
    }
    if (ret == AVERROR_EOF)
    {
        return AMF_EOF;
    }

    // once we get here, we should have no errors after we
    // submitted the frame
    char  errBuffer[AV_ERROR_MAX_STRING_SIZE] = { 0 };
    AMF_RETURN_IF_FALSE(ret >= 0, AMF_FAIL, L"SubmitInput() - Error sending a frame for encoding - %S", av_make_error_string(errBuffer, sizeof(errBuffer)/sizeof(errBuffer[0]), ret));


    //
    // if we did have a frame to submit, and we succeeded
    // it's time to increment the submitted frames counter
    if (pAudioBuffer != nullptr)
    {
        const amf_bool   isProcFrame  = (m_preparedFrames.empty() == false) && (pAudioBuffer == m_preparedFrames.front());
        AMFTransitFrame  transitFrame = { (AMFData*) pAudioBuffer, !isProcFrame };

        m_inputData.push_back(transitFrame);
        m_audioFrameSubmitCount++;

        // if the frame we received is a combined/split frame it's time to 
        // remove it from the list since it's been processed at this point
        if (isProcFrame == true)
        {
            m_preparedFrames.pop_front();
        }

        // if the queue is too large (and we don't want to make it really large
        // due to memory usage, when we go from a large frame to a smaller frame
        // then ask the sender to resend the frame - we don't have to worry as 
        // we don't reprocess the same frame to split or combine it
        if (resendFrame == true)
        {
            return AMF_INPUT_FULL;
        }
    }

    return ((m_bEof == true) || (ret == AVERROR_EOF)) ? AMF_EOF : AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioEncoderFFMPEGImpl::QueryOutput(AMFData** ppData)
{
    // check some required parameters
    AMF_RETURN_IF_FALSE(m_pCodecContext != NULL, AMF_NOT_INITIALIZED, L"QueryOutput() - Codec Context not Initialized");
    AMF_RETURN_IF_FALSE(ppData != NULL, AMF_INVALID_ARG, L"QueryOutput() - ppData == NULL");

    // initialize output
    *ppData = nullptr;


    AMFLock lock(&m_sync);


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
        // if we're draining, we need to check until we get we get
        // AVERROR_EOF to know the encoder has been fully drained
        return AMF_EOF;
    }

    char  errBuffer[AV_ERROR_MAX_STRING_SIZE] = { 0 };
    AMF_RETURN_IF_FALSE(ret >= 0, AMF_FAIL, L"QueryOutput() - Error encoding frame - %S", av_make_error_string(errBuffer, sizeof(errBuffer)/sizeof(errBuffer[0]), ret));


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
        AMF_RESULT err = m_pContext->AllocBuffer(AMF_MEMORY_HOST, avPacket.size, &pBufferOut);
        AMF_RETURN_IF_FAILED(err, L"QueryOutput() - AllocBuffer failed");

        amf_uint8 *pMemOut = static_cast<uint8_t*>(pBufferOut->GetNative());
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

        pBufferOut->SetDuration(duration);
        pBufferOut->SetPts(pts);


        //
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
        while (m_inputData.empty() == false)
        {
            const AMFTransitFrame& transitFrame    = m_inputData.front();
            const amf_pts          inFramePts      = transitFrame.pData->GetPts();
            const amf_pts          inFrameDuration = transitFrame.pData->GetDuration();

            // if the current pts is past the end of the frame
            // we should drop that frame as the frames should 
            // come in sequentially
            std::list<AMFTransitFrame>::const_iterator itNext = ++m_inputData.begin();
            if ((pts > inFramePts + inFrameDuration) || 
                ((itNext != m_inputData.end()) && (pts == itNext->pData->GetPts())))
            {
                m_inputData.pop_front();
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
                if ((itNext != m_inputData.end()) && 
                    (abs((amf_int64) itNext->pData->GetPts() - (amf_int64) pts) < (pts - inFramePts)))
                {
                    itNext->pData->CopyTo(pBufferOut, false);
                }
                else
                {
                    transitFrame.pData->CopyTo(pBufferOut, false);
                }
                break;
            }

            // the generated frame is before the current input frame
            // this should not really happen - trace and exit
            AMFTraceWarning(AMF_FACILITY, L"Generated packet pts %" LPRId64 L" is before the input frame pts %" LPRId64 L"", pts, inFramePts);
            break;
        }


        //
        // prepare output
        *ppData = pBufferOut.Detach();
        m_audioFrameQueryCount++;
    }

    // if we get a frame out from avcodec_receive_packet, we need 
    // to repeat until we get an error that more input needs to 
    // be submitted or we get EOF
    // our documentation states that OK will return a frame out,
    // while repeat says no frame came out so it's up to the caller
    // to repeat QueryOutput while getting AMF_OK
    return (*ppData != nullptr) ? AMF_OK : AMF_REPEAT;
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

        *ppNewBuffer = pRecycledFrame.Detach();
        return AMF_OK;
    }


    //
    // allocate a new frame to fill
    AMF_RESULT err = m_pContext->AllocAudioBuffer(AMF_MEMORY_HOST, m_inSampleFormat, requiredSize, m_sampleRate, m_channelCount, ppNewBuffer);
    AMF_RETURN_IF_FAILED(err, L"GetNewFrame() - AllocAudioBuffer failed");

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
    const amf_bool   isPlanar      = IsAudioPlanar(m_inSampleFormat);
    const amf_int32  planar        = isPlanar ? 1 : m_channelCount;
    const amf_int32  planes        = isPlanar ? m_channelCount : 1;
    const amf_int32  sampleSize    = GetAudioSampleSize((AMF_AUDIO_FORMAT) m_inSampleFormat);
    const amf_int32  inSampleCount = pInBuffer->GetSampleCount();
    const amf_pts    inDuration    = pInBuffer->GetDuration();


    //
    // while we have data in the input buffer we 
    // want to create new frames that contain the 
    // correct number of samples for the encoder
    amf_int32  inSamplesConsumed = 0;
    while (inSamplesConsumed < inSampleCount)
    {
        // if we have a partial frame, fill the partial frame first
        // otherwise create a new frame
        AMFAudioBufferPtr  pFrameToFill;
        amf_int32          existingSampleCount = 0;
        if (m_pPartialFrame != nullptr)
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
            pSrcData += inSampleCount * sampleSize * planar;
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
            m_preparedFrames.push_back(pFrameToFill);
            m_pPartialFrame = nullptr;
            m_partialFrameSampleCount = 0;
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

    return AMF_OK;
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
    avFrame.channel_layout = m_pCodecContext->channel_layout;
    avFrame.channels = m_channelCount;
    avFrame.sample_rate = m_sampleRate;
    avFrame.key_frame = 1;
    avFrame.pts = av_rescale_q(pInBuffer->GetPts(), AMF_TIME_BASE_Q, m_pCodecContext->time_base);

    // setup the data pointers in the AVFrame
    const amf_size  dstPlaneSize = sampleCount * sampleSize * planar;
    for (amf_int ch = 0; ch < planes; ch++)
    { 
        avFrame.data[ch] = (uint8_t *)pInBuffer->GetNative() + ch * dstPlaneSize;
    }
    avFrame.extended_data = avFrame.data;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
