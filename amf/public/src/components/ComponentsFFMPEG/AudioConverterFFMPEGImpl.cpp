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
    m_pReformat(NULL),
    m_pTempBuffer(NULL),
    m_uiTempBufferSize(0),
    m_inSampleFormat(AMFAF_UNKNOWN),
    m_outSampleFormat(AMFAF_UNKNOWN),
    m_inSampleRate(0),
    m_outSampleRate(0),
    m_inChannels(0),
    m_outChannels(0),
    m_bEof(false),
    m_bDrained(true),
    m_audioFrameSubmitCount(0),
    m_audioFrameQueryCount(0),
    m_ptsNext(0)
{
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
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioConverterFFMPEGImpl::Init(AMF_SURFACE_FORMAT /*format*/, amf_int32 /*width*/, amf_int32 /*height*/)
{
    AMFLock lock(&m_sync);

    // clean up any information we might previously have
    Terminate();

    m_bEof = false;
    m_bDrained = true;
    m_ptsNext = 0;

    amf_int64  inChannelLayout  = 0;
    amf_int64  outChannelLayout = 0;
    amf_int64  inSampleFormat = AMFAF_UNKNOWN;
    amf_int64  outSampleFormat = AMFAF_UNKNOWN;
    GetProperty(AUDIO_CONVERTER_IN_AUDIO_CHANNEL_LAYOUT, &inChannelLayout);
    GetProperty(AUDIO_CONVERTER_IN_AUDIO_SAMPLE_FORMAT, &inSampleFormat);
    GetProperty(AUDIO_CONVERTER_IN_AUDIO_SAMPLE_RATE, &m_inSampleRate);
    GetProperty(AUDIO_CONVERTER_IN_AUDIO_CHANNELS, &m_inChannels);
    m_inSampleFormat = (AMF_AUDIO_FORMAT)inSampleFormat;

    GetProperty(AUDIO_CONVERTER_OUT_AUDIO_CHANNEL_LAYOUT, &outChannelLayout);
    GetProperty(AUDIO_CONVERTER_OUT_AUDIO_SAMPLE_FORMAT, &outSampleFormat);
    GetProperty(AUDIO_CONVERTER_OUT_AUDIO_SAMPLE_RATE, &m_outSampleRate);
    GetProperty(AUDIO_CONVERTER_OUT_AUDIO_CHANNELS, &m_outChannels);
    m_outSampleFormat = (AMF_AUDIO_FORMAT)outSampleFormat;

    if ((m_outSampleFormat != m_inSampleFormat) || (m_outSampleRate != m_inSampleRate) || (m_outChannels != m_inChannels))
    {

        m_pResampler = avresample_alloc_context();

        av_opt_set_int(m_pResampler, "in_channel_layout", (int) inChannelLayout, 0);
        av_opt_set_int(m_pResampler, "out_channel_layout", (int) outChannelLayout, 0);
        av_opt_set_int(m_pResampler, "in_sample_fmt", GetFFMPEGAudioFormat(m_inSampleFormat), 0);
        av_opt_set_int(m_pResampler, "out_sample_fmt", GetFFMPEGAudioFormat(m_outSampleFormat), 0);
        av_opt_set_int(m_pResampler, "in_sample_rate", m_inSampleRate, 0);
        av_opt_set_int(m_pResampler, "out_sample_rate", m_outSampleRate, 0);

        if (avresample_open(m_pResampler) == 0)
        {
        }
        else
        {
            avresample_free(&m_pResampler);
            m_pResampler = NULL;

            // try to do reformat
            //MM: FFMPEG pass 1 as number of channels for easy convert - we will follow
            m_pReformat = av_audio_convert_alloc(GetFFMPEGAudioFormat(m_outSampleFormat), 1,
                                                 GetFFMPEGAudioFormat(m_inSampleFormat), 1, NULL, 0);

            //AK: this code should be moved to Connect output slot to pass valid descr out
            if (m_pReformat != NULL)
            {
                AMF_RETURN_IF_FAILED(SetProperty(AUDIO_CONVERTER_OUT_AUDIO_SAMPLE_RATE, m_inSampleRate));
                AMF_RETURN_IF_FAILED(SetProperty(AUDIO_CONVERTER_OUT_AUDIO_CHANNELS, m_inChannels));
                AMF_RETURN_IF_FAILED(SetProperty(AUDIO_CONVERTER_OUT_AUDIO_CHANNEL_LAYOUT, inChannelLayout));

                const amf_int64  blockAlign = m_inChannels * GetAudioSampleSize(m_outSampleFormat);
                AMF_RETURN_IF_FAILED(SetProperty(AUDIO_CONVERTER_OUT_AUDIO_BLOCK_ALIGN, blockAlign));
            }
            else
            {
                return AMF_NOT_IMPLEMENTED;
            }
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
        if (avresample_is_open(m_pResampler))
        {
            avresample_close(m_pResampler);
        }
        avresample_free(&m_pResampler);
        m_pResampler = NULL;
    }

    if (m_pReformat != NULL)
    {
        av_audio_convert_free(m_pReformat);
        m_pReformat = NULL;
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
    m_bDrained = true;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFAudioConverterFFMPEGImpl::SubmitInput(AMFData* pData)
{
    AMFLock lock(&m_sync);

    // if the buffer is already full, we can't set more data
    if (m_pInputData && pData)
    {
        return AMF_INPUT_FULL;
    }
    if(m_bEof)
    {
        return AMF_EOF;
    }

    // if the internal buffer is empty and the in buffer is also empty,
    // we reached the end of the available input...
    if (!m_pInputData && !pData)
    {
        m_bEof = true;
        return AMF_EOF;
    }

    // if the internal buffer is empty and we got new data coming in
    // update internal buffer with new information
    if (!m_pInputData && pData)
    {
        m_pInputData = AMFAudioBufferPtr(pData);
        AMF_RETURN_IF_FALSE(m_pInputData != 0, AMF_INVALID_ARG, L"SubmitInput() - Input should be AudioBuffer");

        AMF_RESULT err = m_pInputData->Convert(AMF_MEMORY_HOST);
        AMF_RETURN_IF_FAILED(err, L"SubmitInput() - Convert(AMF_MEMORY_HOST) failed");

        m_audioFrameSubmitCount++;

        // just pass through
        if (m_pResampler == NULL && m_pReformat == NULL) 
        {
            return AMF_OK;
        }
    }
                
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

    // if the input buffer is empty, we need more data or drain
    if (m_pInputData == NULL && m_bEof && m_bDrained)
    {
        return AMF_EOF;
    }
    if(m_pInputData == NULL && !m_bEof)
    {
        return AMF_REPEAT;
    }

    // just pass through
    if (m_pResampler == NULL && m_pReformat == NULL) 
    {
        if (m_pInputData == NULL)
        {
            m_bDrained = true;
            return AMF_EOF;
        }

        *ppData = m_pInputData;
        (*ppData)->Acquire();
        m_pInputData.Release();
        m_audioFrameQueryCount++;

        return AMF_OK;
    }

    int       iSampleSizeIn  = GetAudioSampleSize(m_inSampleFormat);
    int       iSampleSizeOut = GetAudioSampleSize(m_outSampleFormat);
    amf_int64 iSamplesIn     = 0;
    uint8_t* pMemIn = NULL;

    if (m_pInputData != NULL)
    {

        AMF_RETURN_IF_FALSE(m_pInputData->GetSize() != 0, AMF_INVALID_ARG, L"QueryOutput() - Invalid Param");
        iSamplesIn     = (amf_int64) m_pInputData->GetSize() / (m_inChannels * iSampleSizeIn);
        pMemIn   = static_cast<uint8_t*>(m_pInputData->GetNative());
    }

    amf_int64 iSamplesOut    = 0;
    if (NULL != m_pResampler)
    {
        iSamplesOut = avresample_get_out_samples(m_pResampler, (int)iSamplesIn);
    }
    else
    {
        iSamplesOut = iSamplesIn;
    }

    amf_int64 new_size       = (amf_int64) iSamplesOut * m_outChannels * iSampleSizeOut;
    if (m_uiTempBufferSize < (amf_uint) new_size)
    {
        m_uiTempBufferSize = ((((amf_size)new_size) + (64 - 1)) & ~(64 - 1));
        m_pTempBuffer = (short *) realloc(m_pTempBuffer, m_uiTempBufferSize);
    }
    AMF_RETURN_IF_FALSE(m_pTempBuffer != NULL, AMF_OUT_OF_MEMORY, L"QueryOutput() - No memory");

    uint8_t *ibuf[12] = { pMemIn };
    uint8_t *obuf[12] = { (uint8_t*) m_pTempBuffer };

    int istride[12] = { iSampleSizeIn };
    int ostride[12] = { iSampleSizeOut };


    if (IsAudioPlanar(m_inSampleFormat) && pMemIn != NULL)
    {
        for (amf_int32 ch = 0; ch < m_inChannels; ch++)
        {
            ibuf[ch] = (amf_uint8*) pMemIn + ch * (iSampleSizeIn * iSamplesIn);
            istride[ch] = iSampleSizeIn;
        }
    }
    if (IsAudioPlanar(m_outSampleFormat))
    {
        for (amf_int32 ch = 0; ch < m_outChannels; ch++)
        {
            obuf[ch] = (amf_uint8*) m_pTempBuffer + ch * (iSampleSizeOut * iSamplesOut);
            ostride[ch] = iSampleSizeOut;
        }
    }

    if (m_pResampler != NULL)
    {
        int  writtenSamples = avresample_convert(m_pResampler,
                                        obuf, (int) (iSampleSizeOut * iSamplesOut), (int) iSamplesOut,
                                        pMemIn != NULL ? ibuf : NULL, (int) (iSampleSizeIn * iSamplesIn), (int) iSamplesIn);
//        writtenSamples = avresample_available(m_pResampler);
//        avresample_read(m_pResampler, obuf, writtenSamples);
        if(pMemIn != NULL)
        { 
            if (writtenSamples == 0)
            {
                return AMF_FAIL;
            }
            m_bDrained = false;
        }
        else
        {
            m_bDrained = true;
        }
        iSamplesOut = writtenSamples;
    }
    else if (m_pReformat != NULL)
    {
        const int  len = (int) iSamplesOut * (int) m_inChannels;
        if (av_audio_convert(m_pReformat, (void**) obuf, ostride, (void**) ibuf, istride, len) < 0)
        {
            return AMF_FAIL;
        }
    }

    if(iSamplesOut == NULL)
    { 
        if(m_bEof)
        {
            return AMF_EOF;
        }
        return AMF_REPEAT;
    }

    // allocate output buffer
    AMFAudioBufferPtr pOutputAudioBuffer;
    AMF_RESULT  err = m_pContext->AllocAudioBuffer(AMF_MEMORY_HOST, m_outSampleFormat, 
                                     (amf_int32) iSamplesOut, (amf_int32) m_outSampleRate, (amf_int32) m_outChannels, &pOutputAudioBuffer);
    AMF_RETURN_IF_FAILED(err, L"QueryOutput() - AllocAudioBuffer failed");


    amf_uint8* pMemOut = static_cast<amf_uint8*>(pOutputAudioBuffer->GetNative());
    if (IsAudioPlanar(m_outSampleFormat))
    {
        for (amf_int32 ch = 0; ch < m_outChannels; ch++)
        {
            memcpy(pMemOut + ch * (iSamplesOut * iSampleSizeOut), obuf[ch], (size_t)(iSamplesOut * iSampleSizeOut));
        }
    }
    else
    { 
        memcpy(pMemOut, m_pTempBuffer, (size_t)(iSamplesOut * m_outChannels * iSampleSizeOut));
    }
    if(m_pInputData != NULL)
    { 
        pOutputAudioBuffer->SetPts(m_pInputData->GetPts());
        m_pInputData->CopyTo(pOutputAudioBuffer, false);
    }
    else
    {
        pOutputAudioBuffer->SetPts(m_ptsNext);
    }
    pOutputAudioBuffer->SetDuration(AMF_SECOND * iSamplesOut / m_outSampleRate);
    m_ptsNext = pOutputAudioBuffer->GetPts() + pOutputAudioBuffer->GetDuration();

    *ppData = pOutputAudioBuffer;
    (*ppData)->Acquire();

    m_pInputData.Release();
    m_audioFrameQueryCount++;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL  AMFAudioConverterFFMPEGImpl::OnPropertyChanged(const wchar_t* pName)
{
    AMFLock lock(&m_sync);
    const amf_wstring  name(pName);
}
