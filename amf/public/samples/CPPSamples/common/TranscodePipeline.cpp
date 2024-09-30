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
#include "PipelineDefines.h"
#include "TranscodePipeline.h"
#include "public/include/components/VideoDecoderUVD.h"
#include "public/include/components/VideoConverter.h"
#include "public/include/components/FFMPEGComponents.h"
#include "public/include/components/FFMPEGAudioDecoder.h"
#include "public/include/components/FFMPEGVideoDecoder.h"
#include "public/include/components/FFMPEGAudioConverter.h"
#include "public/include/components/FFMPEGAudioEncoder.h"
#include "public/include/components/FFMPEGFileDemuxer.h"
#include "public/include/components/FFMPEGFileMuxer.h"
#include "public/include/components/FFMPEGEncoderH264.h"
#include "public/include/components/FFMPEGEncoderHEVC.h"
#include "public/include/components/FFMPEGEncoderAV1.h"

#pragma warning(disable:4355)



const wchar_t* TranscodePipeline::PARAM_NAME_SCALE_WIDTH  = L"WIDTH";
const wchar_t* TranscodePipeline::PARAM_NAME_SCALE_HEIGHT = L"HEIGHT";
const wchar_t* TranscodePipeline::PARAM_NAME_FRAMES       = L"FRAMES";
const wchar_t* TranscodePipeline::PARAM_NAME_SCALE_TYPE   = L"SCALETYPE";


// NOTE: codec ID for ffmpeg 4.1.3 - id can change with different ffmpeg versions
#pragma message("NOTE: Audio codec IDs for ffmpeg 4.1.3 - id might change on other versions of ffmpeg")
#define CODEC_ID_PCM_S16LE  0x10000
#define CODEC_ID_MP3        0x15001
#define CODEC_ID_AAC        0x15002
#define CODEC_ID_AC3        0x15003
#define CODEC_ID_FLAC       0x1500C



class TranscodePipeline::PipelineElementEncoder : public AMFComponentElement
{
public:
    PipelineElementEncoder(amf::AMFComponentPtr pComponent, ParametersStorage* pParams, amf_int64 frameParameterFreq, amf_int64 dynamicParameterFreq)
        :AMFComponentElement(pComponent),
        m_pParams(pParams),
        m_framesSubmitted(0),
        m_frameParameterFreq(frameParameterFreq),
        m_dynamicParameterFreq(dynamicParameterFreq)
    {

    }

    virtual ~PipelineElementEncoder(){}

    AMF_RESULT SubmitInput(amf::AMFData* pData)
    {
        AMF_RESULT res = AMF_OK;
        if(pData == NULL) // EOF
        {
            res = m_pComponent->Drain();
        }
        else
        {
            if(m_frameParameterFreq != 0 && m_framesSubmitted !=0 && (m_framesSubmitted % m_frameParameterFreq) == 0)
            { // apply frame-specific properties to the current frame
                PushParamsToPropertyStorage(m_pParams, ParamEncoderFrame, pData);
            }
            if(m_dynamicParameterFreq != 0 && m_framesSubmitted !=0 && (m_framesSubmitted % m_dynamicParameterFreq) == 0)
            { // apply dynamic properties to the encoder
                PushParamsToPropertyStorage(m_pParams, ParamEncoderDynamic, m_pComponent);
            }


            res = m_pComponent->SubmitInput(pData);
            if(res == AMF_DECODER_NO_FREE_SURFACES)
            {
                return AMF_INPUT_FULL;
            }
            m_framesSubmitted++;
        }
        return res;
    }

protected:
    ParametersStorage*      m_pParams;
    amf_int                 m_framesSubmitted;
    amf_int64               m_frameParameterFreq;
    amf_int64               m_dynamicParameterFreq;
};


TranscodePipeline::TranscodePipeline()
    :m_pContext(),
    m_eDecoderFormat(amf::AMF_SURFACE_NV12),
    m_eEncoderFormat(amf::AMF_SURFACE_NV12)
{
}

TranscodePipeline::~TranscodePipeline()
{
    Terminate();
}

void TranscodePipeline::Terminate()
{
    Pipeline::Stop();

    m_pStreamIn = NULL;
    m_pStreamOut = NULL;

    if(m_pAudioDecoder != NULL)
    {
        m_pAudioDecoder->Terminate();
        m_pAudioDecoder = NULL;
    }
    if(m_pAudioConverter != NULL)
    {
        m_pAudioConverter->Terminate();
        m_pAudioConverter= NULL;
    }
    if(m_pAudioEncoder != NULL)
    {
        m_pAudioEncoder->Terminate();
        m_pAudioEncoder = NULL;
    }
    if(m_pDecoder != NULL)
    {
        m_pDecoder->Terminate();
        m_pDecoder = NULL;
    }
    if(m_pConverter != NULL)
    {
        m_pConverter->Terminate();
        m_pConverter = NULL;
    }
    if(m_pPreProcFilter != NULL)
    {
        m_pPreProcFilter->Terminate();
        m_pPreProcFilter = NULL;
    }
    if(m_pEncoder != NULL)
    {
        m_pEncoder->Terminate();
        m_pEncoder = NULL;
    }
    m_pStreamWriter = NULL;

    m_pSplitter = NULL;
    m_pPresenter = NULL;
    if(m_pConverter2)
    {
        m_pConverter2->Terminate();
        m_pConverter2 = NULL;
    }

    if(m_pMuxer != NULL)
    {
        m_pMuxer->Terminate();
        m_pMuxer = NULL;
    }
    if(m_pDemuxer != NULL)
    {
        m_pDemuxer->Terminate();
        m_pDemuxer = NULL;
    }
    if(m_pRawStreamReader != NULL)
    {
//      m_pRawStreamReader->Terminate();
        m_pRawStreamReader = NULL;
    }
    if(m_pContext != NULL)
    {
        m_pContext->Terminate();
        m_pContext = NULL;
    }
#if defined(_WIN32)
#if !defined(METRO_APP)
    m_deviceDX9.Terminate();
#endif//#if !defined(METRO_APP)
    m_deviceDX11.Terminate();
#endif
    m_deviceVulkan.Terminate();

}

double TranscodePipeline::GetProgressSize()
{
    amf_int64 size = 100;
    if(m_pStreamIn)
    {
        m_pStreamIn->GetSize(&size);
    }
    return (double)size;
}

double TranscodePipeline::GetProgressPosition()
{
    amf_int64 pos = 0;
    if(m_pStreamIn)
    {
        m_pStreamIn->GetPosition(&pos);
    }
    return (double)pos;
}

AMF_RESULT TranscodePipeline::SetRecommendedDecoderPoolSize(ParametersStorage* pParams)
{
    // Since we initialize the decoder before the encoder, we cannot just check if PA is enabled in the encoder.
    // There are three ways we can determine if PA is enabled from the params.
    // 
    // 1. PA enabled by direct param
    // 2. PA enabled by HQ usage
    // 3. PA enabled by QVBR, HQVBR, HQCBR rate control methods
    amf::AMFVariant  rateControlMethod;
    amf_bool enablePAbyParam = false;
    amf_bool enablePAbyRCmethod = false;
    amf_bool enablePAbyUsage = false;
    amf_int64 usage = 0;
    amf_size labDepth = 0;

    if (m_EncoderID == AMFVideoEncoderVCE_AVC)
    {
        pParams->GetParam(AMF_VIDEO_ENCODER_PRE_ANALYSIS_ENABLE, enablePAbyParam);

        pParams->GetParam(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, rateControlMethod);
        if (rateControlMethod.ToInt64() == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_QUALITY_VBR ||
            rateControlMethod.ToInt64() == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_HIGH_QUALITY_VBR ||
            rateControlMethod.ToInt64() == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_HIGH_QUALITY_CBR)
        {
            enablePAbyRCmethod = true;
            labDepth = 1; // default LAB depth for QVBR, HQVBR, HQCBR
        }

        // The LAB value from usage setting takes precedence over the LAB value from rate control method
        pParams->GetParam(AMF_VIDEO_ENCODER_USAGE, usage);
        if (usage == AMF_VIDEO_ENCODER_USAGE_HIGH_QUALITY)
        {
            enablePAbyUsage = true;
            labDepth = 11; // default LAB depth for AMF_VIDEO_ENCODER_USAGE_HIGH_QUALITY
        }
    }
    else if (m_EncoderID == AMFVideoEncoder_HEVC)
    {
        pParams->GetParam(AMF_VIDEO_ENCODER_HEVC_PRE_ANALYSIS_ENABLE, enablePAbyParam);

        pParams->GetParam(AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD, rateControlMethod);
        if (rateControlMethod.ToInt64() == AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_QUALITY_VBR ||
            rateControlMethod.ToInt64() == AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_HIGH_QUALITY_VBR ||
            rateControlMethod.ToInt64() == AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_HIGH_QUALITY_CBR)
        {
            enablePAbyRCmethod = true;
            labDepth = 1; // default LAB depth for QVBR, HQVBR, HQCBR
        }

        // The LAB value from usage setting takes precedence over the LAB value from rate control method
        pParams->GetParam(AMF_VIDEO_ENCODER_HEVC_USAGE, usage);
        if (usage == AMF_VIDEO_ENCODER_HEVC_USAGE_HIGH_QUALITY)
        {
            enablePAbyUsage = true;
            labDepth = 11; // default LAB depth for AMF_VIDEO_ENCODER_HEVC_USAGE_HIGH_QUALITY
        }
    }
    else if (m_EncoderID == AMFVideoEncoder_AV1)
    {
        pParams->GetParam(AMF_VIDEO_ENCODER_AV1_PRE_ANALYSIS_ENABLE, enablePAbyParam);

        pParams->GetParam(AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD, rateControlMethod);
        if (rateControlMethod.ToInt64() == AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_QUALITY_VBR ||
            rateControlMethod.ToInt64() == AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_HIGH_QUALITY_VBR ||
            rateControlMethod.ToInt64() == AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_HIGH_QUALITY_CBR)
        {
            enablePAbyRCmethod = true;
            labDepth = 1; // default LAB depth for QVBR, HQVBR, HQCBR
        }

        // The LAB value from usage setting takes precedence over the LAB value from rate control method
        pParams->GetParam(AMF_VIDEO_ENCODER_AV1_USAGE, usage);
        if (usage == AMF_VIDEO_ENCODER_AV1_USAGE_HIGH_QUALITY)
        {
            enablePAbyUsage = true;
            labDepth = 11; // default LAB depth for AMF_VIDEO_ENCODER_AV1_USAGE_HIGH_QUALITY
        }
    }

    if (enablePAbyParam || enablePAbyUsage || enablePAbyRCmethod)
    {
        // User specified LAB depth takes priority. If the param is not specified,
        // then the input value remains.
        pParams->GetParam(AMF_PA_LOOKAHEAD_BUFFER_DEPTH, labDepth);

        // pool size will be = (16 for compliant mode) + (labDepth) + (2 for transit)
        amf_int64 poolSize = 18 + labDepth;
        m_pDecoder->SetProperty(AMF_VIDEO_DECODER_SURFACE_POOL_SIZE, poolSize);
    }

    return AMF_OK;
}

#if !defined(METRO_APP)
AMF_RESULT TranscodePipeline::Init(ParametersStorage* pParams, amf_handle previewTarget, amf_handle display, int threadID)
#else
AMF_RESULT TranscodePipeline::Init(const wchar_t* path, IRandomAccessStream^ inputStream, IRandomAccessStream^ outputStream,
                                   ISwapChainBackgroundPanelNative* previewTarget, AMFSize swapChainPanelSize, ParametersStorage* pParams)
#endif
{
    AMF_RESULT res = AMF_OK;
    //---------------------------------------------------------------------------------------------
    // Read Options
    amf_uint32 adapterID = 0;
    pParams->GetParam(PARAM_NAME_ADAPTERID, adapterID);


    std::wstring inputPath = L"";
#if !defined(METRO_APP)
    res = pParams->GetParamWString(PARAM_NAME_INPUT, inputPath);
    CHECK_AMF_ERROR_RETURN(res, L"Input Path");
    std::wstring outputPath = L"";
    res= pParams->GetParamWString(PARAM_NAME_OUTPUT, outputPath);
    CHECK_AMF_ERROR_RETURN(res, L"Output Path");
#if defined(_WIN32)
    std::wstring engineStr = L"DX11";
#else
    std::wstring engineStr = L"VULKAN";
#endif
    pParams->GetParamWString(PARAM_NAME_ENGINE, engineStr);
    engineStr = toUpper(engineStr);
#else
    std::wstring engineStr = L"DX11";
    inputPath = path;
#endif//#if !defined(METRO_APP)


    amf::AMF_MEMORY_TYPE engineMemoryType = amf::AMF_MEMORY_UNKNOWN;
    if (engineStr == L"HOST")
    {
        engineMemoryType = amf::AMF_MEMORY_HOST;
    }
    else if (engineStr == L"DX9")
    {
        engineMemoryType = amf::AMF_MEMORY_DX9;
    }
    else if (engineStr == L"DX11")
    {
        engineMemoryType = amf::AMF_MEMORY_DX11;
    }
    else if (engineStr == L"VULKAN")
    {
        engineMemoryType = amf::AMF_MEMORY_VULKAN;
    }
	else if (engineStr == L"DX12")
	{
		engineMemoryType = amf::AMF_MEMORY_DX12;
	}
    else
    {
        CHECK_RETURN(false, AMF_INVALID_ARG, L"Wrong parameter " << engineStr);
    }

    // frequency of dynamic changes in the encoder parameters
    amf_int frameParameterFreq = 0;
    amf_int dynamicParameterFreq = 0;
    pParams->GetParam(SETFRAMEPARAMFREQ_PARAM_NAME, frameParameterFreq);
    pParams->GetParam(SETDYNAMICPARAMFREQ_PARAM_NAME, dynamicParameterFreq);

    amf_int64 frames = 0;
    pParams->GetParam(PARAM_NAME_FRAMES, frames);


    //---------------------------------------------------------------------------------------------
    // Init context and devices

    g_AMFFactory.GetFactory()->CreateContext(&m_pContext);

    switch(engineMemoryType)
    {
#if defined(_WIN32)
#if !defined(METRO_APP)
    case amf::AMF_MEMORY_DX9:

        res = m_deviceDX9.Init(true, adapterID, false, 1, 1);
        CHECK_AMF_ERROR_RETURN(res, L"m_deviceDX9.Init() failed");

        res = m_pContext->InitDX9(m_deviceDX9.GetDevice());
        CHECK_AMF_ERROR_RETURN(res, L"m_pContext->InitDX9() failed");
        break;
#endif//#if !defined(METRO_APP)
    case amf::AMF_MEMORY_DX11:
        res = m_deviceDX11.Init(adapterID);
        CHECK_AMF_ERROR_RETURN(res, L"m_deviceDX11.Init() failed");

        res = m_pContext->InitDX11(m_deviceDX11.GetDevice());
        CHECK_AMF_ERROR_RETURN(res, L"m_pContext->InitDX11() failed");
        break;
	case amf::AMF_MEMORY_DX12:
    {
        amf::AMFContext2Ptr pContext2(m_pContext);
        CHECK_RETURN(pContext2 != nullptr, AMF_FAIL, "amf::AMFContext2 is not available");
        res = pContext2->InitDX12(NULL);
        CHECK_AMF_ERROR_RETURN(res, L"m_pContext->InitDX12() failed");
    }
		break;
#endif
    case amf::AMF_MEMORY_VULKAN:
    {
        amf_int64 computeQueue = 0;
        if (AMF_OK == pParams->GetParam(PARAM_NAME_COMPUTE_QUEUE, computeQueue))
        {
            amf::AMFContext1Ptr(m_pContext)->SetProperty(AMF_CONTEXT_VULKAN_COMPUTE_QUEUE, computeQueue);
        }
        //        res = m_deviceVulkan.Init(adapterID, m_pContext);
        res = amf::AMFContext1Ptr(m_pContext)->InitVulkan(NULL);
        CHECK_AMF_ERROR_RETURN(res, L"m_pContext->InitVulkan() failed");
        break;
    }
    }


    //---------------------------------------------------------------------------------------------
    // Init Video Stream Parser or demuxer
    BitStreamType inStreamType = GetStreamType(inputPath.c_str());
    BitStreamType outStreamType = GetStreamType(outputPath.c_str());

    amf_int32 iVideoStreamIndex = -1;
    amf_int32 iAudioStreamIndex = -1;

    amf_int32 outVideoStreamIndex = -1;
    amf_int32 outAudioStreamIndex = -1;
    amf::AMF_SURFACE_FORMAT format = amf::AMF_SURFACE_UNKNOWN;

    BitStreamParserPtr      pParser;
    amf::AMFOutputPtr       pAudioOutput;
    amf::AMFOutputPtr       pVideoOutput;
    if( inStreamType != BitStreamUnknown)
    {
#if !defined(METRO_APP)
        amf::AMFDataStream::OpenDataStream(inputPath.c_str(), amf::AMFSO_READ, amf::AMFFS_SHARE_READ, &m_pStreamIn);
#else
        amf::AMFDataStream::OpenDataStream(inputPath.c_str(), amf::AMFSO_READ, amf::AMFFS_SHARE_READ, &m_pStreamIn);
#endif
        CHECK_RETURN(m_pStreamIn != NULL, AMF_FILE_NOT_OPEN, "Open File");

        pParser = BitStreamParser::Create(m_pStreamIn, inStreamType, m_pContext);
        CHECK_RETURN(pParser != NULL, AMF_FILE_NOT_OPEN, "BitStreamParser::Create");

        iVideoStreamIndex = 0;

        if(frames != 0)
        {
            pParser->SetMaxFramesNumber((amf_size)frames);
        }
    }
    else
    {
        amf_int32               width  = 0;
        amf_int32               height = 0;
        RawStreamReader::ParseRawFileFormat(inputPath, width, height, format);
        if ((format != amf::AMF_SURFACE_UNKNOWN) && (width > 0) && (height > 0))
        {
            m_pRawStreamReader = RawStreamReaderPtr(new RawStreamReader());

            // make a copy of the properties and then update width/height
            amf::AMFVariantStruct  origWidth;
            amf::AMFVariantStruct  origHeight;
            pParams->GetParam(L"WIDTH", &origWidth);
            pParams->GetParam(L"HEIGHT", &origHeight);
            pParams->SetParam(L"WIDTH", width);
            pParams->SetParam(L"HEIGHT", height);

            // initialize raw stream reader with the correct
            // width/height obtained from the string name
            res = m_pRawStreamReader->Init(pParams, m_pContext);
            CHECK_AMF_ERROR_RETURN(res, L"m_pRawStreamReader->Init() failed");

            pParams->SetParam(L"WIDTH", origWidth);
            pParams->SetParam(L"HEIGHT", origHeight);

            iVideoStreamIndex = 0;
        }
        else
        {
            amf::AMFComponentPtr  pDemuxer;
            res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*)FFMPEG_DEMUXER, &pDemuxer);
            CHECK_AMF_ERROR_RETURN(res, L"AMFCreateComponent(" << FFMPEG_DEMUXER << L") failed");
            m_pDemuxer = amf::AMFComponentExPtr(pDemuxer);

            m_pDemuxer->SetProperty(FFMPEG_DEMUXER_PATH, inputPath.c_str());
            res = m_pDemuxer->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
            CHECK_AMF_ERROR_RETURN(res, L"m_pDemuxer->Init() failed");

            if(frames != 0)
            {
                m_pDemuxer->SetProperty(FFMPEG_DEMUXER_FRAME_COUNT, frames);
            }

            amf_int32 outputs = m_pDemuxer->GetOutputCount();
            for(amf_int32 output = 0; output < outputs; output++)
            {
                amf::AMFOutputPtr pOutput;
                res = m_pDemuxer->GetOutput(output, &pOutput);
                CHECK_AMF_ERROR_RETURN(res, L"m_pDemuxer->GetOutput() failed");

                amf_int64 eStreamType = AMF_STREAM_UNKNOWN;
                pOutput->GetProperty(AMF_STREAM_TYPE, &eStreamType);


                if(iVideoStreamIndex < 0 && eStreamType == AMF_STREAM_VIDEO)
                {
                    iVideoStreamIndex = output;
                    pVideoOutput = pOutput;

                    amf_int64 eFormat = 0;
                    if (AMF_OK == pOutput->GetProperty(AMF_STREAM_VIDEO_FORMAT, &eFormat))
                    {
                        format = static_cast<amf::AMF_SURFACE_FORMAT>(eFormat);
                    }
                }

                if(iAudioStreamIndex < 0 && eStreamType == AMF_STREAM_AUDIO)
                {
                    iAudioStreamIndex = output;
                    pAudioOutput = pOutput;
                }
            }
        }
    }

    //---------------------------------------------------------------------------------------------
    // init streams
    if(iVideoStreamIndex >= 0)
    {
        InitVideo(pParser, m_pRawStreamReader, pVideoOutput, engineMemoryType,  previewTarget, display, pParams, format);
    }
    if(iAudioStreamIndex >= 0 && outStreamType == BitStreamUnknown) // do not process audio if we write elmentary stream
    {
        InitAudio(pAudioOutput, pParams);
    }

    //---------------------------------------------------------------------------------------------
    // Init Stream Writer or Muxer

#if !defined(METRO_APP)
    if(threadID != -1)
    {
        std::wstring::size_type pos_dot = outputPath.rfind(L'.');
        if(pos_dot == std::wstring::npos)
        {
            LOG_ERROR(L"Bad file name (no extension): " << outputPath);
            return AMF_FAIL;
        }
        std::wstringstream prntstream;
        prntstream << threadID;
        outputPath = outputPath.substr(0, pos_dot) + L"_" + prntstream.str() + outputPath.substr(pos_dot);
    }

    if( outStreamType != BitStreamUnknown)
    {
        amf::AMFDataStream::OpenDataStream(outputPath.c_str(), amf::AMFSO_WRITE, amf::AMFFS_SHARE_READ, &m_pStreamOut);
        CHECK_RETURN(m_pStreamOut != NULL, AMF_FILE_NOT_OPEN, "Open File");
#else
        m_pStreamOut = AMFDataStream::Create(outputStream);
        CHECK_RETURN(m_pStreamOut != NULL, AMF_FILE_NOT_OPEN, "Open File");
#endif//#if !defined(METRO_APP)
        m_pStreamWriter = StreamWriterPtr(new StreamWriter(m_pStreamOut));
    }
    else
    {
        amf::AMFComponentPtr  pMuxer;
        res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*)FFMPEG_MUXER, &pMuxer);
        CHECK_AMF_ERROR_RETURN(res, L"AMFCreateComponent(" << FFMPEG_DEMUXER << L") failed");
        m_pMuxer = amf::AMFComponentExPtr(pMuxer);

        m_pMuxer->SetProperty(FFMPEG_MUXER_PATH, outputPath.c_str());

        m_pMuxer->SetProperty(FFMPEG_MUXER_ENABLE_VIDEO, iVideoStreamIndex >= 0);
        m_pMuxer->SetProperty(FFMPEG_MUXER_ENABLE_AUDIO, iAudioStreamIndex >= 0);

        amf_int32 inputs = m_pMuxer->GetInputCount();
        for(amf_int32 input = 0; input < inputs; input++)
        {
            amf::AMFInputPtr pInput;
            res = m_pMuxer->GetInput(input, &pInput);
            CHECK_AMF_ERROR_RETURN(res, L"m_pMuxer->GetInput() failed");

            amf_int64 eStreamType = AMF_STREAM_UNKNOWN;
            pInput->GetProperty(AMF_STREAM_TYPE, &eStreamType);


            if(eStreamType == AMF_STREAM_VIDEO)
            {
                outVideoStreamIndex = input;

                pInput->SetProperty(AMF_STREAM_ENABLED, true);
                amf_int32 bitrate = 0;
                if(m_EncoderID == AMFVideoEncoderVCE_AVC || m_EncoderID == AMFVideoEncoderVCE_SVC)
                {
                    pInput->SetProperty(AMF_STREAM_CODEC_ID, AMF_STREAM_CODEC_ID_H264_AVC); // default
                    m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, &bitrate);
                    pInput->SetProperty(AMF_STREAM_BIT_RATE, bitrate);
                    amf::AMFInterfacePtr pExtraData;
                    m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_EXTRADATA, &pExtraData);
                    pInput->SetProperty(AMF_STREAM_EXTRA_DATA, pExtraData);

                    AMFSize frameSize;
                    m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, &frameSize);
                    pInput->SetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, frameSize);

                    AMFRate frameRate;
                    m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_FRAMERATE, &frameRate);
                    pInput->SetProperty(AMF_STREAM_VIDEO_FRAME_RATE, frameRate);
                }
                else if (m_EncoderID == AMFVideoEncoder_AV1)
                {
                    pInput->SetProperty(AMF_STREAM_CODEC_ID, AMF_STREAM_CODEC_ID_AV1);
                    m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_AV1_TARGET_BITRATE, &bitrate);
                    pInput->SetProperty(AMF_STREAM_BIT_RATE, bitrate);
                    amf::AMFInterfacePtr pExtraData;
                    m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_AV1_EXTRA_DATA, &pExtraData);
                    pInput->SetProperty(AMF_STREAM_EXTRA_DATA, pExtraData);

                    AMFSize frameSize;
                    m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_AV1_FRAMESIZE, &frameSize);
                    pInput->SetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, frameSize);

                    AMFRate frameRate;
                    m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_AV1_FRAMERATE, &frameRate);
                    pInput->SetProperty(AMF_STREAM_VIDEO_FRAME_RATE, frameRate);
                }
                else
                {
                    pInput->SetProperty(AMF_STREAM_CODEC_ID, AMF_STREAM_CODEC_ID_H265_HEVC);
                    m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, &bitrate);
                    pInput->SetProperty(AMF_STREAM_BIT_RATE, bitrate);
                    amf::AMFInterfacePtr pExtraData;
                    m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_EXTRADATA, &pExtraData);
                    pInput->SetProperty(AMF_STREAM_EXTRA_DATA, pExtraData);

                    AMFSize frameSize;
                    m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, &frameSize);
                    pInput->SetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, frameSize);

                    AMFRate frameRate;
                    m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, &frameRate);
                    pInput->SetProperty(AMF_STREAM_VIDEO_FRAME_RATE, frameRate);
                }
            }
            else if(eStreamType == AMF_STREAM_AUDIO)
            {
                outAudioStreamIndex = input;
                pInput->SetProperty(AMF_STREAM_ENABLED, true);


                amf_int64 codecID = 0;
                amf_int64 streamBitRate = 0;
                amf_int64 streamSampleRate = 0;
                amf_int64 streamChannels = 0;
                amf_int64 streamFormat = 0;
                amf_int64 streamLayout = 0;
                amf_int64 streamBlockAlign = 0;
                amf_int64 streamFrameSize = 0;

                m_pAudioEncoder->GetProperty(AUDIO_ENCODER_AUDIO_CODEC_ID, &codecID); // currently the same codec as input
                m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_BIT_RATE, &streamBitRate);
                m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_SAMPLE_RATE, &streamSampleRate);
                m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_CHANNELS, &streamChannels);
                m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_SAMPLE_FORMAT, &streamFormat);
                m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_CHANNEL_LAYOUT, &streamLayout);
                m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_BLOCK_ALIGN, &streamBlockAlign);
                m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_FRAME_SIZE, &streamFrameSize);


                amf::AMFInterfacePtr pExtraData;
                m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_EXTRA_DATA, &pExtraData);
                pInput->SetProperty(AMF_STREAM_EXTRA_DATA, pExtraData);

                pInput->SetProperty(AMF_STREAM_CODEC_ID, codecID);
                pInput->SetProperty(AMF_STREAM_BIT_RATE, streamBitRate);
                pInput->SetProperty(AMF_STREAM_AUDIO_SAMPLE_RATE, streamSampleRate);
                pInput->SetProperty(AMF_STREAM_AUDIO_CHANNELS, streamChannels);
                pInput->SetProperty(AMF_STREAM_AUDIO_FORMAT, streamFormat);
                pInput->SetProperty(AMF_STREAM_AUDIO_CHANNEL_LAYOUT, streamLayout);
                pInput->SetProperty(AMF_STREAM_AUDIO_BLOCK_ALIGN, streamBlockAlign);
                pInput->SetProperty(AMF_STREAM_AUDIO_FRAME_SIZE, streamFrameSize);
            }
        }
        res = m_pMuxer->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
        CHECK_AMF_ERROR_RETURN(res, L"m_pMuxer->Init() failed");

    }
    //---------------------------------------------------------------------------------------------
    // Connect pipeline
    if(previewTarget != NULL)
    {
        m_pSplitter = SplitterPtr(new Splitter(true, 2));
    }

    PipelineElementPtr pPipelineElementDemuxer;
    PipelineElementPtr pPipelineElementEncoder;

    if(pParser != NULL)
    {
        pPipelineElementDemuxer = pParser;
    }
    else if (m_pRawStreamReader != NULL)
    {
        pPipelineElementDemuxer = PipelineElementPtr(m_pRawStreamReader);
    }
    else
    {
        pPipelineElementDemuxer = PipelineElementPtr(new AMFComponentExElement(m_pDemuxer));
    }
    Connect(pPipelineElementDemuxer, 10);

    // video
    if(iVideoStreamIndex >= 0)
    {
        SetStatSlot( pPipelineElementDemuxer, iVideoStreamIndex);
        if (m_pRawStreamReader == NULL)
        {
            Connect(PipelineElementPtr(new AMFComponentElement(m_pDecoder)), 0, pPipelineElementDemuxer, iVideoStreamIndex, 4, CT_Direct);
        }
        if(m_pSplitter != 0)
        {
            Connect(m_pSplitter, 4, CT_Direct);
        }
        Connect(PipelineElementPtr(new AMFComponentElement(m_pConverter)), 4, CT_Direct);
        if (m_pPreProcFilter != NULL)
        {
            Connect(PipelineElementPtr(new AMFComponentElement(m_pPreProcFilter)), 4, CT_Direct);
        }

        pPipelineElementEncoder = PipelineElementPtr(new PipelineElementEncoder(m_pEncoder, pParams, frameParameterFreq, dynamicParameterFreq));
        Connect(pPipelineElementEncoder, 10, CT_Direct);
    }
    //
    if(m_pStreamWriter != NULL)
    {
        Connect(m_pStreamWriter, 5, CT_ThreadQueue);
    }
    else
    {
        PipelineElementPtr pPipelineElementMuxer = PipelineElementPtr(new AMFComponentExElement(m_pMuxer));

        if (outVideoStreamIndex >= 0)
        {
            Connect(pPipelineElementMuxer, outVideoStreamIndex, pPipelineElementEncoder, 0, 10, CT_ThreadQueue);
        }

        SetStatSlot( pPipelineElementMuxer, 0);

        // audio
        if(iAudioStreamIndex >= 0)
        {
            Connect(PipelineElementPtr(new AMFComponentElement(m_pAudioDecoder)), 0, pPipelineElementDemuxer, iAudioStreamIndex, 4, CT_Direct);
            Connect(PipelineElementPtr(new AMFComponentElement(m_pAudioConverter)), 4, CT_Direct);
            PipelineElementPtr pPipelineElementAudioEncoder = PipelineElementPtr(new AMFComponentElement(m_pAudioEncoder));
            Connect(pPipelineElementAudioEncoder, 10, CT_Direct);
            Connect(pPipelineElementMuxer, outAudioStreamIndex, pPipelineElementAudioEncoder, 0, 10, CT_ThreadQueue);
        }
    }
    if(m_pSplitter != NULL)
    {
        PipelineElementPtr pConverterElement = PipelineElementPtr(new AMFComponentElement(m_pConverter));

        CHECK_AMF_ERROR_RETURN(
            VideoPresenter::Create(m_pPresenter, engineMemoryType, previewTarget, m_pContext),
            "Failed to create a video presenter"
        );

        AMFSize frame;
        m_pDecoder->GetProperty(AMF_VIDEO_DECODER_CURRENT_SIZE, &frame);
        m_pPresenter->Init(frame.width, frame.height);

        res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, AMFVideoConverter, &m_pConverter2);
        CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << AMFVideoConverter << L") failed");
        m_pConverter2->SetProperty(AMF_VIDEO_CONVERTER_MEMORY_TYPE, m_pPresenter->GetMemoryType());
        m_pConverter2->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_FORMAT, m_pPresenter->GetInputFormat());
        res = m_pConverter2->Init(m_eDecoderFormat, frame.width, frame.height);
        CHECK_AMF_ERROR_RETURN(res, L"m_pConverter->Init() failed");

        Connect(PipelineElementPtr(new AMFComponentElement(m_pConverter2)), 0, m_pSplitter, 1, 4, CT_ThreadQueue);
        Connect(m_pPresenter, 4, CT_ThreadQueue);
    }
    return res;
}


AMF_RESULT TranscodePipeline::Run()
{
    AMF_RESULT res = AMF_OK;
    Pipeline::Start();
    return res;
}

AMF_RESULT  TranscodePipeline::InitAudio(amf::AMFOutput* pOutput, ParametersStorage* pParams)
{
    AMF_RESULT res = AMF_OK;

    amf_int64 codecID = 0;
    amf_int64 streamBitRate = 0;
    amf_int64 streamSampleRate = 0;
    amf_int64 streamChannels = 0;
    amf_int64 streamFormat = 0;
    amf_int64 streamLayout = 0;
    amf_int64 streamBlockAlign = 0;
    amf_int64 streamFrameSize = 0;
    amf::AMFInterfacePtr pExtradata;

    pOutput->GetProperty(AMF_STREAM_CODEC_ID, &codecID);

    pOutput->GetProperty(AMF_STREAM_BIT_RATE, &streamBitRate);
    pOutput->GetProperty(AMF_STREAM_EXTRA_DATA, &pExtradata);

    pOutput->GetProperty(AMF_STREAM_AUDIO_SAMPLE_RATE, &streamSampleRate);
    pOutput->GetProperty(AMF_STREAM_AUDIO_CHANNELS, &streamChannels);
    pOutput->GetProperty(AMF_STREAM_AUDIO_FORMAT, &streamFormat);
    pOutput->GetProperty(AMF_STREAM_AUDIO_CHANNEL_LAYOUT, &streamLayout);
    pOutput->GetProperty(AMF_STREAM_AUDIO_BLOCK_ALIGN, &streamBlockAlign);
    pOutput->GetProperty(AMF_STREAM_AUDIO_FRAME_SIZE, &streamFrameSize);

    res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*)FFMPEG_AUDIO_DECODER, &m_pAudioDecoder);
    CHECK_AMF_ERROR_RETURN(res, L"LoadExternalComponent(" << FFMPEG_AUDIO_DECODER << L") failed");

    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_CODEC_ID, codecID);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_BIT_RATE, streamBitRate);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_EXTRA_DATA, pExtradata);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_SAMPLE_RATE, streamSampleRate);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_CHANNELS, streamChannels);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_SAMPLE_FORMAT, streamFormat);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_CHANNEL_LAYOUT, streamLayout);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_BLOCK_ALIGN, streamBlockAlign);
    m_pAudioDecoder->SetProperty(AUDIO_DECODER_IN_AUDIO_FRAME_SIZE, streamFrameSize);

    res = m_pAudioDecoder->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
    CHECK_AMF_ERROR_RETURN(res, L"m_pAudioDecoder->Init() failed");

    res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*)FFMPEG_AUDIO_CONVERTER, &m_pAudioConverter);
    CHECK_AMF_ERROR_RETURN(res, L"LoadExternalComponent(" << FFMPEG_AUDIO_CONVERTER << L") failed");

    m_pAudioDecoder->GetProperty(AUDIO_DECODER_OUT_AUDIO_BIT_RATE, &streamBitRate);
    m_pAudioDecoder->GetProperty(AUDIO_DECODER_OUT_AUDIO_SAMPLE_RATE, &streamSampleRate);
    m_pAudioDecoder->GetProperty(AUDIO_DECODER_OUT_AUDIO_CHANNELS, &streamChannels);
    m_pAudioDecoder->GetProperty(AUDIO_DECODER_OUT_AUDIO_SAMPLE_FORMAT, &streamFormat);
    m_pAudioDecoder->GetProperty(AUDIO_DECODER_OUT_AUDIO_CHANNEL_LAYOUT, &streamLayout);
    m_pAudioDecoder->GetProperty(AUDIO_DECODER_OUT_AUDIO_BLOCK_ALIGN, &streamBlockAlign);


    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_BIT_RATE, streamBitRate);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_SAMPLE_RATE, streamSampleRate);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_CHANNELS, streamChannels);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_SAMPLE_FORMAT, streamFormat);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_CHANNEL_LAYOUT, streamLayout);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_BLOCK_ALIGN, streamBlockAlign);



    res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*)FFMPEG_AUDIO_ENCODER, &m_pAudioEncoder);
    CHECK_AMF_ERROR_RETURN(res, L"LoadExternalComponent(" << FFMPEG_AUDIO_ENCODER << L") failed");

    std::wstring outputPath = L"";
    res = pParams->GetParamWString(PARAM_NAME_OUTPUT, outputPath);

    amf_int64 encoderCodecID = CODEC_ID_AAC;  // Default to AAC for video and unknown formats

    // Set encoder codec based on output file extension
    const wchar_t EXT_DELIMITER = L'.';
    std::wstring::size_type pos = outputPath.find_last_of(EXT_DELIMITER);
    if (pos != std::wstring::npos)
    {
        std::wstring outputFileExt = outputPath.substr(pos + 1);
        std::transform(outputFileExt.begin(), outputFileExt.end(), outputFileExt.begin(), towlower);
        
        if (outputFileExt == L"wav")
        {
            encoderCodecID = CODEC_ID_PCM_S16LE;
        }
        else if (outputFileExt == L"flac")
        {
            encoderCodecID = CODEC_ID_FLAC;
        }
        else if (outputFileExt == L"mp3")
        {
            encoderCodecID = CODEC_ID_MP3;
        }
        else if (outputFileExt == L"ac3")
        {
            encoderCodecID = CODEC_ID_AC3;
        }
    }

    m_pAudioEncoder->SetProperty(AUDIO_ENCODER_AUDIO_CODEC_ID, encoderCodecID);
    res = m_pAudioEncoder->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
    CHECK_AMF_ERROR_RETURN(res, L"m_pAudioEncoder->Init() failed");

    m_pAudioEncoder->GetProperty(AUDIO_ENCODER_IN_AUDIO_SAMPLE_RATE, &streamSampleRate);
    m_pAudioEncoder->GetProperty(AUDIO_ENCODER_IN_AUDIO_CHANNELS, &streamChannels);
    m_pAudioEncoder->GetProperty(AUDIO_ENCODER_IN_AUDIO_SAMPLE_FORMAT, &streamFormat);
    m_pAudioEncoder->GetProperty(AUDIO_ENCODER_IN_AUDIO_CHANNEL_LAYOUT, &streamLayout);
    m_pAudioEncoder->GetProperty(AUDIO_ENCODER_IN_AUDIO_BLOCK_ALIGN, &streamBlockAlign);

    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_BIT_RATE, streamBitRate);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_SAMPLE_RATE, streamSampleRate);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_CHANNELS, streamChannels);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_SAMPLE_FORMAT, streamFormat);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_CHANNEL_LAYOUT, streamLayout);
    m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_BLOCK_ALIGN, streamBlockAlign);

    res = m_pAudioConverter->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
    CHECK_AMF_ERROR_RETURN(res, L"m_pAudioConverter->Init() failed");

    pOutput->SetProperty(AMF_STREAM_ENABLED, true);

   return AMF_OK;
}
AMF_RESULT  TranscodePipeline::InitVideoDecoder(const wchar_t *pDecoderID, amf_int64 codecID, amf_int32 videoWidth, amf_int32 videoHeight, AMFRate frameRate, amf::AMFBuffer* pExtraData, ParametersStorage* pParams, amf_bool enableSmartAccess, amf_bool enableLowLatency, amf::AMF_SURFACE_FORMAT format)
{
    AMF_VIDEO_DECODER_MODE_ENUM   decoderMode = AMF_VIDEO_DECODER_MODE_COMPLIANT; //amf:::AMF_VIDEO_DECODER_MODE_REGULAR , AMF_VIDEO_DECODER_MODE_LOW_LATENCY;
    AMF_RESULT res = AMF_OK;

    bool bSWDecoder = false;
    pParams->GetParam(PARAM_NAME_SWDECODE, bSWDecoder);

    if (false == bSWDecoder)
    {
        res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, pDecoderID, &m_pDecoder);
        CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << AMFVideoDecoderUVD_H264_AVC << L") failed");
    }
    

    // check resolution
    amf::AMFCapsPtr pCaps;
    if (nullptr != m_pDecoder)
    {
        m_pDecoder->GetCaps(&pCaps);
    }
    // if use swdecoder, m_pDecoder is nullptr and we don't go through this part.
    if (pCaps != nullptr && m_pDecoder != nullptr) 
    {
        amf::AMFIOCapsPtr pInputCaps;
        pCaps->GetInputCaps(&pInputCaps);
        if (pInputCaps != nullptr)
        {
            amf_int32 minWidth = 0;
            amf_int32 maxWidth = 0;
            amf_int32 minHeight = 0;
            amf_int32 maxHeight = 0;
            pInputCaps->GetWidthRange(&minWidth, &maxWidth);
            pInputCaps->GetHeightRange(&minHeight, &maxHeight);
            if (minWidth <= videoWidth && videoWidth <= maxWidth && minHeight <= videoHeight && videoHeight <= maxHeight)
            {
                m_pDecoder->SetProperty(AMF_VIDEO_DECODER_ENABLE_SMART_ACCESS_VIDEO, enableSmartAccess);
                m_pDecoder->SetProperty(AMF_VIDEO_DECODER_REORDER_MODE, amf_int64(decoderMode));
                m_pDecoder->SetProperty(AMF_VIDEO_DECODER_EXTRADATA, amf::AMFVariant(pExtraData));
                m_pDecoder->SetProperty(AMF_TIMESTAMP_MODE, amf_int64(AMF_TS_DECODE)); // our sample H264 parser provides decode order timestamps- change depend on demuxer
                m_pDecoder->SetProperty(AMF_VIDEO_DECODER_LOW_LATENCY, enableLowLatency);

                SetRecommendedDecoderPoolSize(pParams);

                if (format != amf::AMF_SURFACE_UNKNOWN)
                {
                    // convert to supported native decoder formats
                    // These are AMF_SURFACE_NV12, AMF_SURFACE_P010, and AMF_SURFACE_P012
                    switch (format)
                    {
                    case amf::AMF_SURFACE_NV12:
                    case amf::AMF_SURFACE_YV12:               ///< 2  - planar 4:2:0 Y width x height + V width/2 x height/2 + U width/2 x height/2 - 8 bit per component
                    case amf::AMF_SURFACE_BGRA:               ///< 3  - packed 4:4:4 - 8 bit per component
                    case amf::AMF_SURFACE_ARGB:               ///< 4  - packed 4:4:4 - 8 bit per component
                    case amf::AMF_SURFACE_RGBA:               ///< 5  - packed 4:4:4 - 8 bit per component
                    case amf::AMF_SURFACE_GRAY8:              ///< 6  - single component - 8 bit
                    case amf::AMF_SURFACE_YUV420P:            ///< 7  - planar 4:2:0 Y width x height + U width/2 x height/2 + V width/2 x height/2 - 8 bit per component
                    case amf::AMF_SURFACE_U8V8:               ///< 8  - packed double component - 8 bit per component
                    case amf::AMF_SURFACE_YUY2:               ///< 9  - packed 4:2:2 Byte 0=8-bit Y'0; Byte 1=8-bit Cb; Byte 2=8-bit Y'1; Byte 3=8-bit Cr
                    case amf::AMF_SURFACE_UYVY:               ///< 12 - packed 4:2:2 the similar to YUY2 but Y and UV swapped: Byte 0=8-bit Cb; Byte 1=8-bit Y'0; Byte 2=8-bit Cr Byte 3=8-bit Y'1; (used the same DX/CL/Vulkan storage as YUY2)
                    case amf::AMF_SURFACE_AYUV:               ///< 15 - packed 4:4:4 - 8 bit per component YUVA
                        m_eDecoderFormat = amf::AMF_SURFACE_NV12;
                        break;

                    case amf::AMF_SURFACE_P010:               ///< 10 - planar 4:2:0 Y width x height + packed UV width/2 x height/2 - 10 bit per component (16 allocated: upper 10 bits are used)
                    case amf::AMF_SURFACE_R10G10B10A2:        ///< 13 - packed 4:4:4 to 4 bytes: 10 bit per RGB component: 2 bits per A 
                    case amf::AMF_SURFACE_Y210:               ///< 14 - packed 4:2:2 - Word 0=10-bit Y'0; Word 1=10-bit Cb; Word 2=10-bit Y'1; Word 3=10-bit Cr
                    case amf::AMF_SURFACE_Y410:               ///< 16 - packed 4:4:4 - 10 bit per YUV component: 2 bits per A: AVYU 
                        m_eDecoderFormat = amf::AMF_SURFACE_P010;
                        break;

                    case amf::AMF_SURFACE_RGBA_F16:           ///< 11 - packed 4:4:4 - 16 bit per component float
                    case amf::AMF_SURFACE_Y416:               ///< 16 - packed 4:4:4 - 16 bit per component 4 bytes: AVYU
                    case amf::AMF_SURFACE_P012:               ///< 18 - planar 4:2:0 Y width x height + packed UV width/2 x height/2 - 12 bit per component (16 allocated: upper 12 bits are used)
                    case amf::AMF_SURFACE_P016:               ///< 19 - planar 4:2:0 Y width x height + packed UV width/2 x height/2 - 16 bit per component (16 allocated: all bits are used)
                        m_eDecoderFormat = amf::AMF_SURFACE_P012;
                        break;
                    default:
                        m_eDecoderFormat = format;
                        break;
                    }
                }
                else
                {
                    // default formats
                    m_eDecoderFormat = amf::AMF_SURFACE_NV12;
                    if (std::wstring(pDecoderID) == AMFVideoDecoderHW_H265_MAIN10)
                    {
                        m_eDecoderFormat = amf::AMF_SURFACE_P010;
                    }
                    else if (std::wstring(pDecoderID) == AMFVideoDecoderHW_VP9_10BIT)
                    {
                        m_eDecoderFormat = amf::AMF_SURFACE_P010;
                    }
                    else if (std::wstring(pDecoderID) == AMFVideoDecoderHW_AV1_12BIT)
                    {
                        m_eDecoderFormat = amf::AMF_SURFACE_P012;
                    }
                }
                res = m_pDecoder->Init(m_eDecoderFormat, videoWidth, videoHeight);
                if (res != AMF_OK)
                {
                    LOG_WRITE(L"m_pDecoder->Init(" << videoWidth << videoHeight << L") failed " << pDecoderID << L" Error:" << g_AMFFactory.GetTrace()->GetResultText(res) << std::endl, AMFLogLevelInfo);
                    m_pDecoder = nullptr;
                }
            }
            else
            {
                m_pDecoder = nullptr;
            }
        }
        else
        {
            m_pDecoder = nullptr;
        }
    }
    else
    {
        m_pDecoder = nullptr;
    }

    // use software decoder
    //   - if specifically requested
    //   - if resolution not supported
    //   - if codec not supported
    if (m_pDecoder == nullptr)
    {
        res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*)FFMPEG_VIDEO_DECODER, &m_pDecoder);
        CHECK_AMF_ERROR_RETURN(res, L"LoadExternalComponent(" << FFMPEG_VIDEO_DECODER << L") failed");

        if (codecID == 0)
        {
            if (std::wstring(pDecoderID) == AMFVideoDecoderUVD_H264_AVC)
            {
                codecID = AMF_STREAM_CODEC_ID_H264_AVC;
            }
            else if (std::wstring(pDecoderID) == AMFVideoDecoderHW_H265_MAIN10)
            {
                codecID = AMF_STREAM_CODEC_ID_H265_MAIN10;
            }
            else if (std::wstring(pDecoderID) == AMFVideoDecoderHW_H265_HEVC)
            {
                codecID = AMF_STREAM_CODEC_ID_H265_HEVC;
            }
			else if (std::wstring(pDecoderID) == AMFVideoDecoderHW_AV1)
			{
				codecID = AMF_STREAM_CODEC_ID_AV1;
			}
            else if (std::wstring(pDecoderID) == AMFVideoDecoderHW_AV1_12BIT)
            {
                codecID = AMF_STREAM_CODEC_ID_AV1_12BIT;
            }
        }
        m_pDecoder->SetProperty(VIDEO_DECODER_CODEC_ID, codecID);
        m_pDecoder->SetProperty(VIDEO_DECODER_BITRATE, 0);
        m_pDecoder->SetProperty(VIDEO_DECODER_FRAMERATE, frameRate);

        //      m_pVideoDecoder->SetProperty(AMF_VIDEO_DECODER_REORDER_MODE, amf_int64(decoderMode));
        m_pDecoder->SetProperty(VIDEO_DECODER_EXTRA_DATA, amf::AMFVariant(pExtraData));

        m_eDecoderFormat = amf::AMF_SURFACE_NV12;
        if (std::wstring(pDecoderID) == AMFVideoDecoderHW_H265_MAIN10)
        {
            m_eDecoderFormat = amf::AMF_SURFACE_P010;
        }
        res = m_pDecoder->Init(m_eDecoderFormat, videoWidth, videoHeight);
        CHECK_AMF_ERROR_RETURN(res, L"m_pDecoder->Init(" << videoWidth << videoHeight << L") failed " << pDecoderID);
    }
    return AMF_OK;
}

AMF_RESULT  TranscodePipeline::InitVideoProcessor(amf::AMF_MEMORY_TYPE presenterEngine, amf_int32 inWidth, amf_int32 inHeight, amf_int32 outWidth, amf_int32 outHeight, amf_int64 scaleType)
{
    AMF_RESULT res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, AMFVideoConverter, &m_pConverter);
    CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << AMFVideoConverter << L") failed");

    if (scaleType != AMF_VIDEO_CONVERTER_SCALE_INVALID)
    {
        m_pConverter->SetProperty(AMF_VIDEO_CONVERTER_SCALE, scaleType);
    }
    m_pConverter->SetProperty(AMF_VIDEO_CONVERTER_MEMORY_TYPE, presenterEngine);
    m_pConverter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_FORMAT, m_eEncoderFormat);
    m_pConverter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_SIZE, AMFConstructSize(outWidth, outHeight));

    m_pConverter->Init(m_eDecoderFormat, inWidth, inHeight);

    return AMF_OK;
}

AMF_RESULT  TranscodePipeline::InitPreProcessFilter(ParametersStorage* pParams)
{
    amf::AMFComponentPtr  spPreProcFilter;
    AMF_RESULT res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, AMFPreProcessing, &spPreProcFilter);
    CHECK_AMF_ERROR_RETURN(res, L"AMFPreProcessing creation failed");
    m_pPreProcFilter = spPreProcFilter;

    // get memory type for the edge filter
    amf::AMF_MEMORY_TYPE  engineType = amf::AMF_MEMORY_HOST;
    if (pParams->GetParam(AMF_PP_ENGINE_TYPE, (int&)engineType) == AMF_OK)
    {
        res = m_pPreProcFilter->SetProperty(AMF_PP_ENGINE_TYPE, (int)engineType);
        CHECK_AMF_ERROR_RETURN(res, L"AMFPreProcessing failed to set engine type");
    }

    // get tor and sigma values
    double sigma = 0;
    if (pParams->GetParam(AMF_PP_ADAPTIVE_FILTER_STRENGTH, sigma) == AMF_OK)
    {
        res = m_pPreProcFilter->SetProperty(AMF_PP_ADAPTIVE_FILTER_STRENGTH, sigma);
        CHECK_AMF_ERROR_RETURN(res, L"AMFPreProcessing failed to set sigma");
    }
    double tor = 0;
    if (pParams->GetParam(AMF_PP_ADAPTIVE_FILTER_SENSITIVITY, tor) == AMF_OK)
    {
        m_pPreProcFilter->SetProperty(AMF_PP_ADAPTIVE_FILTER_SENSITIVITY, tor);
        CHECK_AMF_ERROR_RETURN(res, L"AMFPreProcessing failed to set tor");
    }

    // Set adaptive filter enable
    bool adaptiveFilterEnable = false;
    if (pParams->GetParam(AMF_PP_ADAPTIVE_FILTER_ENABLE, adaptiveFilterEnable) == AMF_OK)
    {
        res = m_pPreProcFilter->SetProperty(AMF_PP_ADAPTIVE_FILTER_ENABLE, adaptiveFilterEnable);
        CHECK_AMF_ERROR_RETURN(res, L"AMFPreProcessing failed to set adaptive filter enable");
    }

    return AMF_OK;
}

AMF_RESULT  TranscodePipeline::InitVideo(BitStreamParserPtr pParser, RawStreamReaderPtr pRawReader, amf::AMFOutput* pOutput, amf::AMF_MEMORY_TYPE presenterEngine, amf_handle hwnd, amf_handle display, ParametersStorage* pParams, amf::AMF_SURFACE_FORMAT format)
{
    bool decodeAsAnnexBStream = false; // switches between Annex B and AVCC types of decode input.

    AMF_RESULT res = AMF_OK;
    amf::AMFBufferPtr pExtraData;
    amf_int32 videoWidth = 0;
    amf_int32 videoHeight = 0;
    std::wstring pVideoDecoderID;
    AMFRate frameRate = {};

    amf_int64 codecID = 0;
    if(pParser != NULL)
    {
        if(!decodeAsAnnexBStream) // need to provide SPS/PPS if input stream will be AVCC ( not Annex B)
        {
            const unsigned char* extraData = pParser->GetExtraData();
            size_t extraDataSize = pParser->GetExtraDataSize();
            if (extraData && extraDataSize)
            {
                m_pContext->AllocBuffer(amf::AMF_MEMORY_HOST, extraDataSize, &pExtraData);

                memcpy(pExtraData->GetNative(), extraData, extraDataSize);
            }
        }
        videoWidth = pParser->GetPictureWidth();
        videoHeight = pParser->GetPictureHeight();
        pParser->GetFrameRate(&frameRate);

        pVideoDecoderID = pParser->GetCodecComponent();

    }
    else if(pOutput != NULL)
    {
        amf::AMFInterfacePtr pInterface;
        pOutput->GetProperty(AMF_STREAM_EXTRA_DATA, &pInterface);
        pExtraData = amf::AMFBufferPtr(pInterface);

        AMFSize frameSize = {};
        pOutput->GetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, &frameSize);
        videoWidth = frameSize.width;
        videoHeight = frameSize.height;

        res= pOutput->GetProperty(AMF_STREAM_CODEC_ID, &codecID);
        pVideoDecoderID = StreamCodecIDtoDecoderID(AMF_STREAM_CODEC_ID_ENUM(codecID));

        pOutput->SetProperty(AMF_STREAM_ENABLED, true);
        pOutput->GetProperty(AMF_STREAM_VIDEO_FRAME_RATE, &frameRate);
    }
    else if (pRawReader != NULL)
    {
        videoWidth = pRawReader->GetWidth();
        videoHeight = pRawReader->GetHeight();
        frameRate.num = 30;
        frameRate.den = 1;
        m_eDecoderFormat = pRawReader->GetFormat();
    }


    // The decoder needs to know about PA because some PA features such
    // as LAB depend on an adequate amount of decoder output buffers
    m_EncoderID = AMFVideoEncoderVCE_AVC;
    pParams->GetParamWString(PARAM_NAME_CODEC, m_EncoderID);

    //---------------------------------------------------------------------------------------------
    if ((pParser != NULL) || (pOutput != NULL))
    {
        amf_bool enableDecoderSmartAccess = false;
        pParams->GetParam(AMF_VIDEO_DECODER_ENABLE_SMART_ACCESS_VIDEO, enableDecoderSmartAccess);

        amf_bool enableDecoderLowLatency = false;
        pParams->GetParam(AMF_VIDEO_DECODER_LOW_LATENCY, enableDecoderLowLatency);

        // Init Video Decoder
        res = InitVideoDecoder(pVideoDecoderID.c_str(), codecID, videoWidth, videoHeight, frameRate, pExtraData, pParams, enableDecoderSmartAccess, enableDecoderLowLatency, format);
        CHECK_AMF_ERROR_RETURN(res, L"InitVideoDecoder() failed");
    }

    //---------------------------------------------------------------------------------------------

    amf_int scaleWidth = 0;    // if 0 - no scaling
    amf_int scaleHeight = 0;   // if 0 - no scaling

    pParams->GetParam(PARAM_NAME_SCALE_WIDTH, scaleWidth);
    pParams->GetParam(PARAM_NAME_SCALE_HEIGHT, scaleHeight);

    if(scaleWidth == 0)
    {
        scaleWidth = videoWidth;
    }
    if(scaleHeight == 0)
    {
        scaleHeight = videoHeight;
    }

    amf_int64 scaleType = AMF_VIDEO_CONVERTER_SCALE_INVALID;
    pParams->GetParam(PARAM_NAME_SCALE_TYPE, scaleType);

    // Check the bit depth of output to adjust encoder surface format appropriately, AVC is always 8bit
    amf_int64 colorBitDepth = 8;
    if (m_EncoderID == AMFVideoEncoder_HEVC)
    {
        pParams->GetParam(AMF_VIDEO_ENCODER_HEVC_COLOR_BIT_DEPTH, colorBitDepth);
    }
    else if (m_EncoderID == AMFVideoEncoder_AV1)
    {
        pParams->GetParam(AMF_VIDEO_ENCODER_AV1_COLOR_BIT_DEPTH, colorBitDepth);
    }
    switch (colorBitDepth)
    {
    case 8:
        m_eEncoderFormat = amf::AMF_SURFACE_NV12;
        break;
    case 10:
        m_eEncoderFormat = amf::AMF_SURFACE_P010;
        break;
    case 12:
        m_eEncoderFormat = amf::AMF_SURFACE_P012;
        break;
    default:
        LOG_ERROR("[Error] Unknown color bit depth: " << colorBitDepth);
        break;
    }

    //---------------------------------------------------------------------------------------------
    // Init Video Converter/Processor
    res = InitVideoProcessor(presenterEngine, videoWidth, videoHeight, scaleWidth, scaleHeight, scaleType);
    CHECK_AMF_ERROR_RETURN(res, L"InitVideoProcessor() failed");

    if(hwnd != NULL)
    {
        // Init Presenter
        CHECK_AMF_ERROR_RETURN(
            VideoPresenter::Create(m_pPresenter, presenterEngine, hwnd, m_pContext, display),
            "Failed to create a video presenter"
        );

        m_pPresenter->Init(scaleWidth, scaleHeight);
    }

    //---------------------------------------------------------------------------------------------
    // init pre processing filter
    amf_bool  preProcfilterEnable = false;
    pParams->GetParam(AMF_VIDEO_PRE_ENCODE_FILTER_ENABLE, preProcfilterEnable);
    if (preProcfilterEnable)
    {
        InitPreProcessFilter(pParams);
        res = m_pPreProcFilter->Init(amf::AMF_SURFACE_NV12, scaleWidth, scaleHeight);

        // Set bitrate and framerate of adaptive filter if they're passed as parameters
        amf_int64 targetBitrate = 2000000;
        if (pParams->GetParam(AMF_PP_TARGET_BITRATE, targetBitrate) == AMF_OK)
        {
            res = m_pPreProcFilter->SetProperty(AMF_PP_TARGET_BITRATE, targetBitrate);
            CHECK_AMF_ERROR_RETURN(res, L"AMFPreProcessing failed to set target bitrate for the adaptive filter mechanism");
        }
        AMFRate preProcFrameRate = AMFConstructRate(30, 1);
        if (pParams->GetParam(AMF_PP_FRAME_RATE, preProcFrameRate) == AMF_OK)
        {
            res = m_pPreProcFilter->SetProperty(AMF_PP_FRAME_RATE, preProcFrameRate);
            CHECK_AMF_ERROR_RETURN(res, L"AMFPreProcessing failed to set frame rate for the adaptive filter mechanism");
        }

        CHECK_AMF_ERROR_RETURN(res, L"m_pPreProcFilter->Init() failed");
    }

    //---------------------------------------------------------------------------------------------
    // Init Video Encoder


    if(m_EncoderID == AMFVideoEncoderVCE_AVC)
    {
        amf_int64 usage = 0;
        if(pParams->GetParam(AMF_VIDEO_ENCODER_USAGE, usage) == AMF_OK)
        {
            if(usage == amf_int64(AMF_VIDEO_ENCODER_USAGE_WEBCAM))
            {
                m_EncoderID = AMFVideoEncoderVCE_SVC;
            }
        }
    }

    amf_bool  enableSWencode = false;
    pParams->GetParam(PARAM_NAME_SWENCODE, enableSWencode);
    // SW encoder enable - require full build version of ffmpeg dlls with shared libs
    if (enableSWencode) {
        if (m_EncoderID == AMFVideoEncoderVCE_AVC)
        {
            res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*)FFMPEG_ENCODER_H264, &m_pEncoder);
            CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.LoadExternalComponent(" << FFMPEG_ENCODER_H264 << L") failed");
            m_pEncoder->SetProperty(AMF_STREAM_CODEC_ID, AMF_STREAM_CODEC_ID_H264_AVC); // default
        }
        else if (m_EncoderID == AMFVideoEncoder_HEVC)
        {
            res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*)FFMPEG_ENCODER_HEVC, &m_pEncoder);
            CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.LoadExternalComponent(" << FFMPEG_ENCODER_HEVC << L") failed");
            m_pEncoder->SetProperty(AMF_STREAM_CODEC_ID, AMF_STREAM_CODEC_ID_H265_HEVC); // default
        }
        else if (m_EncoderID == AMFVideoEncoder_AV1)
        {
            res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", (void*)FFMPEG_ENCODER_AV1, &m_pEncoder);
            CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.LoadExternalComponent(" << FFMPEG_ENCODER_AV1 << L") failed");
            m_pEncoder->SetProperty(AMF_STREAM_CODEC_ID, AMF_STREAM_CODEC_ID_AV1); // default
        }
        else
        {
            CHECK_AMF_ERROR_RETURN(AMF_CODEC_NOT_SUPPORTED, L"Codecs(" << m_EncoderID << L") is not supported in software encoder");
        }
    }
    else
    {
        res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, m_EncoderID.c_str(), &m_pEncoder);
        CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << m_EncoderID << L") failed");
    }
    // Usage is preset that will set many parameters
    PushParamsToPropertyStorage(pParams, ParamEncoderUsage, m_pEncoder);

    // if we enable PA, we need to make sure RateCotrolMode gets set first
    // otherwise setting the PA properties might not work...
    const wchar_t* PAproperty;
    const wchar_t* RCproperty;
    if (m_EncoderID == AMFVideoEncoderVCE_AVC)
    {
        PAproperty = AMF_VIDEO_ENCODER_PRE_ANALYSIS_ENABLE;
        RCproperty = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD;
    }
    else if (m_EncoderID == AMFVideoEncoder_HEVC)
    {
        PAproperty = AMF_VIDEO_ENCODER_HEVC_PRE_ANALYSIS_ENABLE;
        RCproperty = AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD;
    }
    else if (m_EncoderID == AMFVideoEncoder_AV1)
    {
        PAproperty = AMF_VIDEO_ENCODER_AV1_PRE_ANALYSIS_ENABLE;
        RCproperty = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD;
    }
    else
        return AMF_FAIL;
    amf::AMFVariant  enablePA(true);
    amf::AMFVariant  enablePAbyUsage(true);
    //PA setting may come from parameters or usage. Can not only check parameters.
    res = pParams->GetParam(PAproperty, enablePA);
    if (res == AMF_OK)//if parameter sets PA, just follow it since it has higher priority
    {
        amf::AMFVariant  rateControlMode;
        if (pParams->GetParam(RCproperty, rateControlMode) == AMF_OK)
        {
            m_pEncoder->SetProperty(RCproperty, rateControlMode);
        }
        m_pEncoder->SetProperty(PAproperty, enablePA);
    }
    else if (m_pEncoder->GetProperty(PAproperty, &enablePAbyUsage) == AMF_OK)//not set in parameter, check if PA settings in usage
    {
        if (enablePAbyUsage.boolValue == true)
        {
            amf::AMFVariant  rateControlMode;
            if (pParams->GetParam(RCproperty, rateControlMode) == AMF_OK)
            {
                m_pEncoder->SetProperty(RCproperty, rateControlMode);
            }
            m_pEncoder->SetProperty(PAproperty, true);
        }
    }
	// override some usage parameters
	if (frameRate.den != 0 && frameRate.num != 0)
	{
		if (m_EncoderID == AMFVideoEncoderVCE_AVC || m_EncoderID == AMFVideoEncoderVCE_SVC)
		{
			m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, frameRate);
		}
		else if (m_EncoderID == AMFVideoEncoder_HEVC)
		{
			m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, frameRate);
		}
        else
        {
            m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_FRAMERATE, frameRate);
        }
	}

    PushParamsToPropertyStorage(pParams, ParamEncoderStatic, m_pEncoder);

    PushParamsToPropertyStorage(pParams, ParamEncoderDynamic, m_pEncoder);

    res = m_pEncoder->Init(m_eEncoderFormat, scaleWidth, scaleHeight);
    CHECK_AMF_ERROR_RETURN(res, L"m_pEncoder->Init() failed");

//    PushParamsToPropertyStorage(pParams, ParamEncoderDynamic, m_pEncoder);
//    m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_EXTRADATA, NULL); //samll way - around  forces to regenerate extradata

    return AMF_OK;
}