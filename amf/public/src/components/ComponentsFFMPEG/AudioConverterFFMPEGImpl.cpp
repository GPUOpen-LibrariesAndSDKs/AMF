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

#include "AudioConverterFFMPEGImpl.h"
#include "UtilsFFMPEG.h"

#include "public/include/core/Context.h"
#include "public/include/core/Trace.h"
#include "public/common/TraceAdapter.h"


#define AMF_FACILITY L"AMFAudioConverterFFMPEGImpl"

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
// AMFAudioConverterFFMPEGImpl
//
//

//-------------------------------------------------------------------------------------------------
AMFAudioConverterFFMPEGImpl::AMFAudioConverterFFMPEGImpl(AMFContext* pContext)
  : m_pContext(pContext),
    m_pResampler(NULL),
    m_pTempBuffer(NULL),
    m_uiTempBufferSize(0),
    m_inSampleFormat(AMFAF_UNKNOWN),
    m_outSampleFormat(AMFAF_UNKNOWN),
    m_inSampleRate(0),
    m_outSampleRate(0),
    m_inChannels(0),
    m_outChannels(0),
    m_bEof(false),
    m_audioFrameSubmitCount(0),
    m_audioFrameQueryCount(0),
    m_ptsNext(-1LL),
    m_ptsPrevEnd(-1LL)
{
    g_AMFFactory.Init();

    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoBool(FFMPEG_AUDIO_CONVERTER, L"Enable Debug", false, true),

        AMFPropertyInfoInt64(AUDIO_CONVERTER_IN_AUDIO_BIT_RATE, L"Compression Bit Rate", 128000, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AUDIO_CONVERTER_IN_AUDIO_SAMPLE_RATE, L"Sample Rate", 0, 0, 256000, true),
        AMFPropertyInfoInt64(AUDIO_CONVERTER_IN_AUDIO_CHANNELS, L"Number of channels (0 - default)", 2, 0, 100, true),
        AMFPropertyInfoEnum(AUDIO_CONVERTER_IN_AUDIO_SAMPLE_FORMAT, L"Sample Format", AMFAF_UNKNOWN, AMF_SAMPLE_FORMAT_ENUM_DESCRIPTION, true),
        AMFPropertyInfoInt64(AUDIO_CONVERTER_IN_AUDIO_CHANNEL_LAYOUT, L"Channel layout (0 - default)", 0, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AUDIO_CONVERTER_IN_AUDIO_BLOCK_ALIGN, L"Block Align", 0, 0, INT_MAX, true),

        AMFPropertyInfoInt64(AUDIO_CONVERTER_OUT_AUDIO_BIT_RATE, L"Compression Bit Rate", 128000, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AUDIO_CONVERTER_OUT_AUDIO_SAMPLE_RATE, L"Sample Rate", 0, 0, 256000, true),
        AMFPropertyInfoInt64(AUDIO_CONVERTER_OUT_AUDIO_CHANNELS, L"Number of channels (0 - default)", 2, 0, 100, true),
        AMFPropertyInfoEnum(AUDIO_CONVERTER_OUT_AUDIO_SAMPLE_FORMAT, L"Sample Format", AMFAF_UNKNOWN, AMF_SAMPLE_FORMAT_ENUM_DESCRIPTION, true),
        AMFPropertyInfoInt64(AUDIO_CONVERTER_OUT_AUDIO_CHANNEL_LAYOUT, L"Channel layout (0 - default)", 0, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AUDIO_CONVERTER_OUT_AUDIO_BLOCK_ALIGN, L"Block Align", 0, 0, INT_MAX, true),
    AMFPrimitivePropertyInfoMapEnd

    InitFFMPEG();
}
//-------------------------------------------------------------------------------------------------
AMFAudioConverterFFMPEGImpl::~AMFAudioConverterFFMPEGImpl()
{
    Terminate();
    g_AMFFactory.Terminate();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioConverterFFMPEGImpl::Init(AMF_SURFACE_FORMAT /*format*/, amf_int32 /*width*/, amf_int32 /*height*/)
{
    AMFLock lock(&m_sync);

    // clean up any information we might previously have
    Terminate();

    m_bEof = false;
    m_ptsNext = -1LL;
    m_ptsPrevEnd = -1LL;

    amf_int64  inSampleFormat = AMFAF_UNKNOWN;
    amf_int64  outSampleFormat = AMFAF_UNKNOWN;
    amf_int64  channelLayout = 0;
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_CONVERTER_IN_AUDIO_CHANNEL_LAYOUT, &channelLayout), L"Init() - Failed to get in channel layout property");
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_CONVERTER_IN_AUDIO_SAMPLE_FORMAT, &inSampleFormat), L"Init() - Failed to get in sample format property");
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_CONVERTER_IN_AUDIO_SAMPLE_RATE, &m_inSampleRate), L"Init() - Failed to get in sample rate property");
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_CONVERTER_IN_AUDIO_CHANNELS, &m_inChannels), L"Init() - Failed to get in channel count property");
    m_inSampleFormat = (AMF_AUDIO_FORMAT)inSampleFormat;
    av_channel_layout_from_mask(&m_inChannelLayout, channelLayout);

    channelLayout = 0;
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_CONVERTER_OUT_AUDIO_CHANNEL_LAYOUT, &channelLayout), L"Init() - Failed to get out channel layout property");
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_CONVERTER_OUT_AUDIO_SAMPLE_FORMAT, &outSampleFormat), L"Init() - Failed to get out sample format property");
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_CONVERTER_OUT_AUDIO_SAMPLE_RATE, &m_outSampleRate), L"Init() - Failed to get out sample rate property");
    AMF_RETURN_IF_FAILED(GetProperty(AUDIO_CONVERTER_OUT_AUDIO_CHANNELS, &m_outChannels), L"Init() - Failed to get out channel count property");
    m_outSampleFormat = (AMF_AUDIO_FORMAT)outSampleFormat;
    av_channel_layout_from_mask(&m_outChannelLayout, channelLayout);

    // If there is a mismatch between input and output formats, sample rate and channels, a converter is required
    if ((m_outSampleFormat != m_inSampleFormat) || (m_outSampleRate != m_inSampleRate) || (m_outChannels != m_inChannels))
    {
        m_pResampler = swr_alloc();
        AMF_RETURN_IF_FALSE(m_pResampler != NULL, AMF_FAIL, L"Init() - m_pResampler alloc failed")

        AMF_RESULT res = InitResampler();
        if (res != AMF_OK)
        {
            swr_free(&m_pResampler);
            m_pResampler = NULL;
            return res;
        }
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioConverterFFMPEGImpl::ReInit(amf_int32 width, amf_int32 height)
{
    AMFLock lock(&m_sync);

    Terminate();
    return Init(AMF_SURFACE_UNKNOWN, width, height);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioConverterFFMPEGImpl::Terminate()
{
    AMFLock lock(&m_sync);

    // clear the internally stored buffer
    m_pInputData = nullptr;
    AMFTraceInfo(AMF_FACILITY, L"Submitted %d, Queried %d", (int)m_audioFrameSubmitCount, (int)m_audioFrameQueryCount);
    
    if (m_pResampler != NULL)
    {
        if (swr_is_initialized(m_pResampler))
        {
            swr_close(m_pResampler);
        }
        swr_free(&m_pResampler);
        m_pResampler = NULL;
    }

    if (m_pTempBuffer != NULL)
    {
        free(m_pTempBuffer);
        m_pTempBuffer = NULL;
    }

    m_audioFrameSubmitCount = 0;
    m_audioFrameQueryCount = 0;

    m_uiTempBufferSize = 0;

    m_bEof = false;
    m_ptsNext = -1LL;
    m_ptsPrevEnd = -1LL;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioConverterFFMPEGImpl::Drain()
{
    AMFLock lock(&m_sync);

    m_bEof = true;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioConverterFFMPEGImpl::Flush()
{
    AMFLock lock(&m_sync);

    // clear the internally stored buffer
    m_pInputData = nullptr;

    // clear the pts values
    m_ptsNext = -1LL;
    m_ptsPrevEnd = -1LL;

    // we need to dump everything that's stored in the resampler
    // and re-init it - draining the last buffered information in
    // the resampler will not continue if not reinitialized
    if (m_pResampler != nullptr)
    {
        // close the resampler as we got the last bits out
        swr_close(m_pResampler);

        // re-initialize the resampler to start fresh for the
        // next input sample
        AMF_RESULT res = InitResampler();
        if (res != AMF_OK)
        {
            swr_free(&m_pResampler);
            m_pResampler = NULL;
            return res;
        }
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioConverterFFMPEGImpl::SubmitInput(AMFData* pData)
{
    AMFLock lock(&m_sync);

    // if input data is null, we reached EOF
    if (!pData)
    {
        m_bEof = true;
        return AMF_EOF;
    }

    // if we reached EOF, we shouldn't accept more input
    if (m_bEof)
    {
        return AMF_EOF;
    }

    // if the buffer is already full, we can't set more data
    if (m_pInputData)
    {
        return AMF_INPUT_FULL;
    }

    // update the first frame pts offset
    if (m_ptsNext == -1)
    {
        m_ptsNext = pData->GetPts();
    }

    // if the internal buffer is empty and we got new data coming in
    // update internal buffer with new information
    m_pInputData = AMFAudioBufferPtr(pData);
    AMF_RETURN_IF_FALSE(m_pInputData != 0, AMF_INVALID_DATA_TYPE, L"SubmitInput() - Input should be AudioBuffer");

    // ffmpeg only handles data in host memory
    AMF_RESULT err = m_pInputData->Convert(AMF_MEMORY_HOST);
    AMF_RETURN_IF_FAILED(err, L"SubmitInput() - Convert(AMF_MEMORY_HOST) failed");

    m_audioFrameSubmitCount++;
                
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioConverterFFMPEGImpl::QueryOutput(AMFData** ppData)
{
    AMFLock lock(&m_sync);

    // check some required parameters
    AMF_RETURN_IF_FALSE(ppData != NULL, AMF_INVALID_ARG, L"GetOutput() - ppData == NULL");
    AMF_RETURN_IF_FALSE(m_inSampleFormat != AMFAF_UNKNOWN && m_outSampleFormat != AMFAF_UNKNOWN, AMF_NOT_INITIALIZED, L"QueryOutput() - Audio Format not Initialized");
    
    // initialize output
    *ppData = NULL;


    //
    // If no conversion is required, just pass through
    if (m_pResampler == nullptr)
    {
        if (m_pInputData != nullptr)
        {
            *ppData = m_pInputData.Detach();
            m_audioFrameQueryCount++;
        }

        return m_bEof ? AMF_EOF : AMF_OK;
    }


    //
    // if the input buffer is empty and drain hasn't been triggered yet then more input is required
    if (m_pInputData == nullptr && !m_bEof)
    {
        return AMF_REPEAT;
    }


    //
    // If there is input, prepare buffer for converter input
    // If there is no input, then that means m_bEof is true and we are draining
    amf_int64       sampleCountIn = 0;
    const uint8_t*  ibuf[12]      = { nullptr };
    if (m_pInputData != nullptr)
    {
        // retrieve how many samples are in the input audio buffer
        sampleCountIn = m_pInputData->GetSampleCount();

        // Set pointers for each channel - if data is packed, there's only one 
        // plane so the loop will set the pointer to the beginning of the plane
        const uint8_t*   pMemIn     = static_cast<uint8_t*>(m_pInputData->GetNative());
        const amf_int32  planes     = m_pInputData->GetChannelCount();
        const amf_int32  sampleSize = m_pInputData->GetSampleSize();
        for (amf_int32 ch = 0; ch < planes; ch++)
        {
            ibuf[ch] = pMemIn + ch * (sampleSize * sampleCountIn);
        }
    }


    //
    // Get the upper limit on the number of samples that we can get
    // NOTE: keep variables in 64 bit to avoid 
    //       overflow due to multiplications
    const amf_int64  sampleCountOutMax = swr_get_out_samples(m_pResampler, (int) sampleCountIn);
    AMF_RETURN_IF_FALSE(sampleCountOutMax >= 0, AMF_FAIL, L"QueryOutput() - swr_get_out_samples() failed - sampleCountOutMax = %" LPRId64 L"", sampleCountOutMax);

    // if there are no more samples, we have drained
    if (sampleCountOutMax == 0 && m_bEof == true)
    {
        return AMF_EOF;
    }


    //
    // Prepare output to retrieve the resampled data
    const amf_int64  sampleSizeOut = GetAudioSampleSize(m_outSampleFormat);
    
    // Create output buffer to store converter output temporarily
    // the reason we need the temporary buffer is because the max 
    // required buffer size, might be more than what we get in the 
    // end from actually doing the conversion as swr_get_out_samples
    // gives us an upper bound, not the exact size
    const amf_int64  requiredSize = sampleCountOutMax * m_outChannels * sampleSizeOut;
    if (m_uiTempBufferSize < (amf_size) requiredSize)
    {
        m_uiTempBufferSize = ((((amf_size)requiredSize) + (64 - 1)) & ~(64 - 1));
        m_pTempBuffer = (amf_uint8*) realloc(m_pTempBuffer, m_uiTempBufferSize);
    }
    AMF_RETURN_IF_FALSE(m_pTempBuffer != NULL, AMF_OUT_OF_MEMORY, L"QueryOutput() - No memory m_uiTempBufferSize = %" LPRId64 L"", (amf_int64)m_uiTempBufferSize);

    // Set the pointers for each channel - if data is packed, there's only one 
    // plane so the loop will set the pointer to the beginning of the plane
    const amf_int64  channelsOut = IsAudioPlanar(m_outSampleFormat) ? m_outChannels : 1;
          uint8_t*   obuf[12]    = { m_pTempBuffer };
    for (amf_int64 ch = 0; ch < channelsOut; ch++)
    {
        obuf[ch] = m_pTempBuffer + ch * (sampleSizeOut * sampleCountOutMax);
    }


    //
    // Convert audio
    // NOTE: we need to be careful if we find a gap in the audio stream
    //       in that case we want to dump any remaining frames which are
    //       still in the buffer and keep the input frame to start fresh
    //       from a new pts next time around
    // NOTE: comparison for the gap needs to be "greater than 1" 
    //       not "greater than 0" because sometimes due to roundoff
    //       you can have the current frame pts - end of last frame do
    //       come out to 1, and it's not really a gap
    const amf_pts    currGap        = (m_pInputData != NULL) ? m_pInputData->GetPts() - m_ptsPrevEnd : 0;
    const bool       gapFound       = (m_ptsPrevEnd != -1LL) && (currGap > 1);
    const amf_int64  sampleCountOut = swr_convert(m_pResampler, obuf, (int) sampleCountOutMax, 
                                                 ((m_pInputData != NULL) && (gapFound == false)) ? ibuf : NULL, (gapFound == true) ? 0 : (int) sampleCountIn);
    
    char errBuf[AV_ERROR_MAX_STRING_SIZE] = { 0 };
    AMF_RETURN_IF_FALSE(sampleCountOut >= 0, AMF_FAIL, L"QueryOutput() - conversion failed - %s", av_make_error_string(errBuf, amf_countof(errBuf), (int) sampleCountOut));

    if (sampleCountOut == 0)
    { 
        // it turns out that it is possible to have no samples come out
        // from the converter in cases where the input and output rates
        // are the same (but format might be different for example) and
        // still have a gap from the previous frame to the new frame
        // in this case there are no frames buffered so there's nothing
        // to output, but we have forced the converter to finish so we
        // need to reinitialize the converter as we know there are
        // more frames to be processed
        // NOTE: we know the resampler exists, because if we don't have
        //       it, the component just acts as pass-through, returning 
        //       the input frames (if no conversion necessary)
        if (gapFound == true)
        {
            AMF_RETURN_IF_FAILED(ReInitOnGap(), L"QueryOutput - ReInitOnGap failed");
            return AMF_REPEAT;
        }

        return (m_pInputData != NULL) ? AMF_FAIL
                                      : (m_bEof) ? AMF_EOF : AMF_REPEAT;
    }


    //
    // allocate output buffer
    AMFAudioBufferPtr pOutputAudioBuffer;
    AMF_RESULT  err = m_pContext->AllocAudioBuffer(AMF_MEMORY_HOST, m_outSampleFormat, (amf_int32) sampleCountOut, 
                                                   (amf_int32) m_outSampleRate, (amf_int32) m_outChannels, &pOutputAudioBuffer);
    AMF_RETURN_IF_FAILED(err, L"QueryOutput() - AllocAudioBuffer failed");

    // copy the data from the temporary buffer
    amf_uint8* pMemOut = static_cast<amf_uint8*>(pOutputAudioBuffer->GetNative());
    if (IsAudioPlanar(m_outSampleFormat))
    {
        const amf_size  copySize = (amf_size)(sampleCountOut * sampleSizeOut);
        for (amf_int32 ch = 0; ch < m_outChannels; ch++)
        {
            memcpy(pMemOut + ch * copySize, obuf[ch], copySize);
        }
    }
    else
    { 
        memcpy(pMemOut, obuf[0], (size_t)(sampleCountOut * m_outChannels * sampleSizeOut));
    }


    //
    // update the output buffer with new pts and duration
    // m_ptsNext is set on the first piece of data coming in 
    // and because of resizing, to get continuous values we 
    // increment from the first pts we got
    // 
    // there's a reason behind this - for example when converting 
    // from 48000 to 44100, 1024 samples coming in should get 
    // converted to 941 samples coming out, but the first frame 
    // is not coming out as 941 - it's coming out as 931, so some 
    // samples are buffered depending on the XX-tap filter being used 
    // and they're coming with the next frame, hence the pts out is 
    // not exactly the same as the frame coming in
    // 
    // in the end, the converted output compared to the input will
    // look something like this:
    // 
    // in:   --------  --------  --------  --------
    // out:  ------  -------  --------  -------  --
    //
    // if there are gaps, the desired behaviour is shown below
    // 
    // in:   --------  --------  --------         --------  --------
    // out:  ------  -------  -------  --         ------  ------  --
    //
    pOutputAudioBuffer->SetPts(m_ptsNext);
    pOutputAudioBuffer->SetDuration(AMF_SECOND * sampleCountOut / m_outSampleRate);
    m_ptsNext += pOutputAudioBuffer->GetDuration();

    // if we found a gap, leave the input frame alone as it was not sent for 
    // conversion - we just drained the remaining data in the conversion buffer
    // but reset the last pts position so we start fresh next time around
    if (gapFound == true)
    {
        AMF_RETURN_IF_FAILED(ReInitOnGap(), L"QueryOutput - ReInitOnGap failed");
    }
    else if (m_pInputData != nullptr)
    { 
        // copy properties to output buffer
        m_pInputData->CopyTo(pOutputAudioBuffer, false);

        // update last pts position
        m_ptsPrevEnd = m_pInputData->GetPts() + m_pInputData->GetDuration();

        // release frame so the next one can come in
        m_pInputData = nullptr;
    }

    *ppData = pOutputAudioBuffer.Detach();

    m_audioFrameQueryCount++;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFAudioConverterFFMPEGImpl::InitResampler()
{
    AMF_RETURN_IF_INVALID_POINTER(m_pResampler, L"SetResamplerOptions() - m_pResampler == NULL");
    
    if (swr_is_initialized(m_pResampler))
    {
        return AMF_OK;
    }

    int err = 0;
    err = av_opt_set_chlayout(m_pResampler, "in_chlayout", &m_inChannelLayout, 0);
    AMF_RETURN_IF_FALSE(err == 0, AMF_FAIL, L"InitResampler() - Failed to set resampler in_channel_layout to %" LPRId64 L"", m_inChannelLayout);

    err = av_opt_set_chlayout(m_pResampler, "out_chlayout", &m_outChannelLayout, 0);
    AMF_RETURN_IF_FALSE(err == 0, AMF_FAIL, L"InitResampler() - Failed to set resampler out_channel_layout to %" LPRId64 L"", m_outChannelLayout);

    AVSampleFormat ffmpegInSampleFormat = GetFFMPEGAudioFormat(m_inSampleFormat);
    err = av_opt_set_int(m_pResampler, "in_sample_fmt", ffmpegInSampleFormat, 0);
    AMF_RETURN_IF_FALSE(err == 0, AMF_FAIL, L"InitResampler() - Failed to set resampler in_sample_fmt to %" LPRId64 L"", ffmpegInSampleFormat);

    AVSampleFormat ffmpegOutSampleFormat = GetFFMPEGAudioFormat(m_outSampleFormat);
    err = av_opt_set_int(m_pResampler, "out_sample_fmt", ffmpegOutSampleFormat, 0);
    AMF_RETURN_IF_FALSE(err == 0, AMF_FAIL, L"InitResampler() - Failed to set resampler out_sample_fmt to %" LPRId64 L"", ffmpegOutSampleFormat);

    err = av_opt_set_int(m_pResampler, "in_sample_rate", m_inSampleRate, 0);
    AMF_RETURN_IF_FALSE(err == 0, AMF_FAIL, L"InitResampler() - Failed to set resampler in_sample_rate to %" LPRId64 L"", m_inSampleRate);

    err = av_opt_set_int(m_pResampler, "out_sample_rate", m_outSampleRate, 0);
    AMF_RETURN_IF_FALSE(err == 0, AMF_FAIL, L"InitResampler() - Failed to set resampler out_sample_rate to %" LPRId64 L"", m_outSampleRate);

    //
    // The options below are not required to be set. The converter used to use the FFmpeg AVResample library however
    // that became depracated and was replaced by the swresample library. Since there were some differences between
    // the default settings for the two libraries, the settings below are set to keep the settings the same as before
    const amf_int64 linearInterp = 0, filterSize = 16;
    const amf_double cutoff = 0.8;

    err = av_opt_set_int(m_pResampler, "linear_interp", linearInterp, 0);
    AMF_RETURN_IF_FALSE(err == 0, AMF_FAIL, L"InitResampler() - Failed to set resampler linear_interp to %" LPRId64 L"", linearInterp);

    err = av_opt_set_int(m_pResampler, "filter_size", filterSize, 0);
    AMF_RETURN_IF_FALSE(err == 0, AMF_FAIL, L"InitResampler() - Failed to set resampler filter_size to %" LPRId64 L"", filterSize);

    err = av_opt_set_double(m_pResampler, "cutoff", cutoff, 0);
    AMF_RETURN_IF_FALSE(err == 0, AMF_FAIL, L"InitResampler() - Failed to set resampler cutoff to %f", cutoff);

    // Init the resampler
    err = swr_init(m_pResampler);
    AMF_RETURN_IF_FALSE(err == 0, AMF_FAIL, L"InitResampler() - resampler init() failed");

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFAudioConverterFFMPEGImpl::ReInitOnGap()
{
    AMF_RETURN_IF_INVALID_POINTER(m_pResampler, L"ReInitOnGap() - m_pResampler == NULL");

    // the previous pts will no longer be valid, due to the gap
    m_ptsPrevEnd = -1LL;

    // close the resampler as we got the last bits out
    swr_close(m_pResampler);

    // re-initialize the resampler to start fresh for the
    // next input sample
    AMF_RESULT res = InitResampler();
    if (res != AMF_OK)
    {
        swr_free(&m_pResampler);
        m_pResampler = NULL;
        return res;
    }

    // update the next pts to take the gap into consideration
    // this is the frame with the gap so take its PTS as the 
    // start for the next audio stream
    if (m_pInputData != nullptr)
    {
        m_ptsNext = m_pInputData->GetPts();
    }

    return AMF_OK;
}
