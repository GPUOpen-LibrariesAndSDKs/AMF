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

// this sample encodes NV12 frames using AMF Encoder and writes them to H.264 elmentary stream 

#include <stdio.h>
#ifdef _WIN32
#include <tchar.h>
#include <d3d9.h>
#include <d3d11.h>
#endif
#include "public/common/AMFFactory.h"
#include "public/common/Thread.h"
#include "public/common/AMFSTL.h"
#include "public/common/TraceAdapter.h"
#include "public/include/components/VideoEncoderVCE.h"
#include "public/include/components/VideoEncoderHEVC.h"
#include "public/samples/CPPSamples/common/EncoderParamsAVC.h"
#include "public/samples/CPPSamples/common/EncoderParamsHEVC.h"
#include "../common/ParametersStorage.h"
#include "../common/CmdLineParser.h"
#include "../common/CmdLogger.h"
#include "../common/PipelineDefines.h"
#include "SurfaceGenerator.h"

#include <fstream>
#include <iostream>


static wchar_t* pCodec = AMFVideoEncoderVCE_AVC;
//static const wchar_t *pCodec = AMFVideoEncoder_HEVC;

static wchar_t* pWorkAlgorithm = L"ASAP";
static AMF_COLOR_BIT_DEPTH_ENUM eDepth = AMF_COLOR_BIT_DEPTH_8;
static amf_int32 frameRateIn = 30;
static amf_int64 bitRateIn = 5000000L; // in bits, 5MBit
static amf_int32 frameCount = 500;
static amf_int32 preRender = 0;
static bool bMaximumSpeed = true;

amf::AMF_MEMORY_TYPE    memoryTypeIn = amf::AMF_MEMORY_DX11;
amf::AMF_SURFACE_FORMAT formatIn     = amf::AMF_SURFACE_NV12;
amf_int32               widthIn      = 1920;
amf_int32               heightIn     = 1080;
amf_int32               rectSize     = 50;

bool          writeToFileMode = false;
amf_int64     formatType      = 0;
amf_int64     memoryIn        = 0;
std::wstring  codec           = pCodec;
std::wstring  workAlgorithm   = pWorkAlgorithm;
amf_wstring   fileNameOut;


#define START_TIME_PROPERTY L"StartTimeProperty" // custom property ID to store submission time in a frame - all custom properties are copied from input to output

amf::AMFSurfacePtr pColor1;
amf::AMFSurfacePtr pColor2;

#define MILLISEC_TIME     10000

static const wchar_t*  PARAM_NAME_WORKALGORITHM = L"ALGORITHM";
static const wchar_t*  PARAM_NAME_FORMAT        = L"FORMAT";
static const wchar_t*  PARAM_NAME_FRAMERATE     = L"FRAMERATE";
static const wchar_t*  PARAM_NAME_BITRATE       = L"BITRATE";
static const wchar_t*  PARAM_NAME_COLORDEPTH    = L"COLORDEPTH";
static const wchar_t*  PARAM_NAME_PRERENDER     = L"PRERENDER";


AMF_RESULT RegisterParams(ParametersStorage* pParams)
{
    pParams->SetParamDescription(PARAM_NAME_WORKALGORITHM, ParamCommon, L"'ASAP' or 'OneInOne' frames submission algorithm", NULL);
    pParams->SetParamDescription(PARAM_NAME_ENGINE, ParamCommon, L"Memory type: DX9Ex, DX11, Vulkan (h.264 only)", ParamConverterMemoryType);
    pParams->SetParamDescription(PARAM_NAME_CODEC, ParamCommon, L"Codec name (AVC or H264, HEVC or H265)", ParamConverterCodec);
    pParams->SetParamDescription(PARAM_NAME_FORMAT, ParamCommon, L"Supported file formats: RGBA_F16, R10G10B10A2, NV12, P010", ParamConverterFormat);
    pParams->SetParamDescription(PARAM_NAME_FRAMERATE, ParamCommon, L"Output framerate", ParamConverterInt64);
    pParams->SetParamDescription(PARAM_NAME_BITRATE, ParamCommon, L"Output bitrate", ParamConverterInt64);
    pParams->SetParamDescription(PARAM_NAME_INPUT_FRAMES, ParamCommon, L"Output number of frames", ParamConverterInt64);
    pParams->SetParamDescription(PARAM_NAME_PRERENDER, ParamCommon, L"Pre-render number of frames", ParamConverterInt64);
    pParams->SetParamDescription(PARAM_NAME_OUTPUT_WIDTH, ParamCommon, L"Output resolution, width", ParamConverterInt64);
    pParams->SetParamDescription(PARAM_NAME_OUTPUT_HEIGHT, ParamCommon, L"Output resolution, height", ParamConverterInt64);
    pParams->SetParamDescription(PARAM_NAME_COLORDEPTH, ParamCommon, L"Color depth type (8 or 10). HEVC only", ParamConverterInt64);
    pParams->SetParamDescription(PARAM_NAME_OUTPUT, ParamCommon, L"Output file name", NULL);

    RegisterEncoderParamsAVC(pParams);
    RegisterEncoderParamsHEVC(pParams);

    return AMF_OK;
}

#if defined(_WIN32)
AMF_RESULT ReadParams(ParametersStorage* params)
{
    if (!parseCmdLineParameters(params))
#else
AMF_RESULT ReadParams(ParametersStorage* params, int argc, char* argv[])
{
    if (!parseCmdLineParameters(params, argc, argv))
#endif        
    {
        LOG_INFO(L"+++ AVC codec +++");
        ParametersStorage paramsAVC;
        RegisterParams(&paramsAVC);
        RegisterEncoderParamsAVC(&paramsAVC);
        LOG_INFO(paramsAVC.GetParamUsage());

        LOG_INFO(L"+++ HEVC codec +++");
        ParametersStorage paramsHEVC;
        RegisterParams(&paramsHEVC);
        RegisterEncoderParamsHEVC(&paramsHEVC);
        LOG_INFO(paramsHEVC.GetParamUsage());

        return AMF_FAIL;
    }

    // load paramters
    params->GetParam(PARAM_NAME_INPUT_FRAMES, frameCount);
    params->GetParam(PARAM_NAME_FRAMERATE, frameRateIn);
    params->GetParam(PARAM_NAME_BITRATE, bitRateIn);
    params->GetParam(PARAM_NAME_OUTPUT_WIDTH, widthIn);
    params->GetParam(PARAM_NAME_OUTPUT_HEIGHT, heightIn);
    params->GetParam(PARAM_NAME_PRERENDER, preRender);

    params->GetParamWString(PARAM_NAME_WORKALGORITHM, workAlgorithm);
    params->GetParamWString(PARAM_NAME_OUTPUT, fileNameOut);
    params->GetParamWString(PARAM_NAME_CODEC, codec);

    if (codec == std::wstring(AMFVideoEncoder_HEVC))
    {
        amf_int64 colorDepth;
        if (params->GetParam(PARAM_NAME_COLORDEPTH, colorDepth) == AMF_OK)
        {
            eDepth = colorDepth == 10 ? AMF_COLOR_BIT_DEPTH_10 : AMF_COLOR_BIT_DEPTH_8;
        }
    }

    if (params->GetParam(PARAM_NAME_ENGINE, memoryIn) == AMF_OK)
    {
        memoryTypeIn = (amf::AMF_MEMORY_TYPE)memoryIn;
    }

    if (params->GetParam(PARAM_NAME_FORMAT, formatType) == AMF_OK)
    {
        formatIn = (amf::AMF_SURFACE_FORMAT)formatType;
    }

    writeToFileMode = (fileNameOut.empty() != true);
    return AMF_OK;
}

AMF_RESULT ValidateParams(ParametersStorage* pParams)
{
    amf::AMFVariant tmp;
    if (eDepth == AMF_COLOR_BIT_DEPTH_10 && (formatIn == amf::AMF_SURFACE_NV12 || formatIn == amf::AMF_SURFACE_YV12 || formatIn == amf::AMF_SURFACE_BGRA
        || formatIn == amf::AMF_SURFACE_ARGB || formatIn == amf::AMF_SURFACE_RGBA || formatIn == amf::AMF_SURFACE_GRAY8 || formatIn == amf::AMF_SURFACE_YUV420P
        || formatIn == amf::AMF_SURFACE_U8V8 || formatIn == amf::AMF_SURFACE_YUY2))
    {
        if (pParams->GetParam(PARAM_NAME_FORMAT, tmp) == AMF_OK)
        {
            printf("[ERROR] Selected surface format is not a 10-bit format, requested parameters combination can't be applied. Program will terminate\n");
            return AMF_INVALID_ARG;
        }

        printf("[WARNING] Default surface format NV12 is an 8-bit format. Program will use P010 (10-bit) format instead.\n");
        formatIn = amf::AMF_SURFACE_P010;
    }
    else if (eDepth == AMF_COLOR_BIT_DEPTH_8 && (formatIn == amf::AMF_SURFACE_P010 || formatIn == amf::AMF_SURFACE_R10G10B10A2 || formatIn == amf::AMF_SURFACE_RGBA_F16
        || formatIn == amf::AMF_SURFACE_UYVY || formatIn == amf::AMF_SURFACE_Y210 || formatIn == amf::AMF_SURFACE_Y410 || formatIn == amf::AMF_SURFACE_Y416 || formatIn == amf::AMF_SURFACE_GRAY32))
    {
        if (pParams->GetParam(PARAM_NAME_COLORDEPTH, tmp) == AMF_OK)
        {
            printf("[ERROR] Selected surface format is not a 10-bit format, requested parameters combination can't be applied. Program will terminate\n");
            return AMF_INVALID_ARG;
        }

        printf("[WARNING] Default bit depth is 8, but selected surface format is not an 8-bit format. Color depth will be changed to 10 bits\n");
        eDepth = AMF_COLOR_BIT_DEPTH_10;
    }
    return AMF_OK;
};


AMF_RESULT SetEncoderDefaults(amf::AMFComponent* encoder, std::wstring& codec) 
{
    AMF_RESULT res;
    if (codec == std::wstring(AMFVideoEncoderVCE_AVC))
    {
        res = encoder->SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_TRANSCONDING);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_TRANSCONDING) failed");

        if (bMaximumSpeed)
        {
            res = encoder->SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0);
            // do not check error for AMF_VIDEO_ENCODER_B_PIC_PATTERN - can be not supported - check Capability Manager sample
            res = encoder->SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED) failed");
        }

        res = encoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, bitRateIn);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, %" LPRId64 L") failed", bitRateIn);
        res = encoder->SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, ::AMFConstructSize(widthIn, heightIn));
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, %dx%d) failed", widthIn, heightIn);
        res = encoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, ::AMFConstructRate(frameRateIn, 1));
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, %dx%d) failed", frameRateIn, 1);
        res = encoder->SetProperty(AMF_VIDEO_ENCODER_LOWLATENCY_MODE, true);
        AMF_RETURN_IF_FAILED(res, L"encoder->SetProperty(AMF_VIDEO_ENCODER_LOWLATENCY_MODE, true) failed");

        res = encoder->SetProperty(AMF_VIDEO_ENCODER_QUERY_TIMEOUT, 50); //ms
        AMF_RETURN_IF_FAILED(res, L"encoder->SetProperty(AMF_VIDEO_ENCODER_QUERY_TIMEOUT, 50) failed");
    }
    else
    {
        res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCONDING);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCONDING)");

        if (bMaximumSpeed)
        {
            res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED)");
        }

        res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, bitRateIn);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, %" LPRId64 L") failed", bitRateIn);
        res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, ::AMFConstructSize(widthIn, heightIn));
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, %dx%d) failed", widthIn, heightIn);
        res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, ::AMFConstructRate(frameRateIn, 1));
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, %dx%d) failed", frameRateIn, 1);

        res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_LOWLATENCY_MODE, true);
        AMF_RETURN_IF_FAILED(res, L"encoder->SetProperty(AMF_VIDEO_ENCODER_LOWLATENCY_MODE, true) failed");
        res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_COLOR_BIT_DEPTH, eDepth);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_COLOR_BIT_DEPTH, %d) failed", eDepth);

        res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUERY_TIMEOUT, 50); //ms
        AMF_RETURN_IF_FAILED(res, L"encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUERY_TIMEOUT, 50) failed");
    }
    return AMF_OK;
}


static void printTime(amf_pts latency_time, amf_pts write_duration, amf_pts min_latency, amf_pts max_latency)
{
    printf("Total: frames = %i Time = %.4fms\n" \
        "Min,Max: latency = %.4fms, %.4fms\n" \
        "Average: latency = %.4fms\n",
        frameCount,
        double(latency_time) / MILLISEC_TIME,
        double(min_latency) / MILLISEC_TIME,
        double(max_latency) / MILLISEC_TIME,
        double(latency_time) / MILLISEC_TIME / frameCount
        );
}


class PollingThread : public amf::AMFThread
{
protected:
    amf::AMFContextPtr      m_pContext;
    amf::AMFComponentPtr    m_pEncoder;
    std::ofstream           m_pFile;
    bool                    isWritingToFile;
public:
    PollingThread(amf::AMFContext* context, amf::AMFComponent* encoder, const wchar_t* pFileName, bool writeToFile);
    ~PollingThread();
    virtual void Run();
};

PollingThread::PollingThread(amf::AMFContext* context, amf::AMFComponent* encoder, const wchar_t* pFileName, bool writeToFile) : m_pContext(context), m_pEncoder(encoder)
{
    m_pFile = std::ofstream(pFileName, std::ios::binary);
    isWritingToFile = writeToFile;
}
PollingThread::~PollingThread()
{
    if (m_pFile)
    {
        m_pFile.close();
    }
}
void PollingThread::Run()
{
    RequestStop();

    amf_pts latency_time = 0;
    amf_pts write_duration = 0;
    amf_pts last_poll_time = 0;
    amf_pts min_latency = INT64_MAX;
    amf_pts max_latency = 0;

    while (true)
    {
        amf::AMFDataPtr data;
        AMF_RESULT res = m_pEncoder->QueryOutput(&data);
        if (res == AMF_EOF)
        {
            break; // Drain complete
        }
        if ((res != AMF_OK) && (res != AMF_REPEAT))
        {
            // trace possible error message
            break; // Drain complete
        }
        if (data != NULL)
        {
            amf_pts poll_time = amf_high_precision_clock();
            amf_pts start_time = 0;
            data->GetProperty(START_TIME_PROPERTY, &start_time);
            if (start_time < last_poll_time) // remove wait time if submission was faster then encode
            {
                start_time = last_poll_time;
            }
            last_poll_time = poll_time;

            min_latency = min_latency < poll_time - start_time ? min_latency : poll_time - start_time;
            max_latency = max_latency > poll_time - start_time ? max_latency : poll_time - start_time;

            latency_time += poll_time - start_time;

            amf::AMFBufferPtr buffer(data); // query for buffer interface
            if (isWritingToFile)
            {
                m_pFile.write(reinterpret_cast<char*>(buffer->GetNative()), buffer->GetSize());
                write_duration += amf_high_precision_clock() - poll_time;
            }
        }
        else
        {
            amf_sleep(1);
        }
    }

    printTime(latency_time, write_duration, min_latency, max_latency);

    m_pEncoder = NULL;
    m_pContext = NULL;
}

#if defined(_WIN32)
int _tmain(int argc, _TCHAR* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    ParametersStorage params;
    RegisterParams(&params);

#if defined(_WIN32)
    AMF_RESULT res = ReadParams(&params);
#else
    AMF_RESULT res = ReadParams(&params, argc, argv);
#endif        
    if (res != AMF_OK)
    {
        wprintf(L"Command line arguments couldn't be parsed");
        return -1;
    }

    if (ValidateParams(&params) == AMF_INVALID_ARG) 
    {
        return -1;
    }

    res = g_AMFFactory.Init();
    if (res != AMF_OK)
    {
        wprintf(L"AMF Failed to initialize");
        return -1;
    }

    ::amf_increase_timer_precision();

    amf::AMFTraceEnableWriter(AMF_TRACE_WRITER_CONSOLE, true);
    amf::AMFTraceEnableWriter(AMF_TRACE_WRITER_DEBUG_OUTPUT, true);

    // initialize AMF
    amf::AMFContextPtr context;
    amf::AMFComponentPtr encoder;
    amf::AMFSurfacePtr surfaceIn;

    // context
    res = g_AMFFactory.GetFactory()->CreateContext(&context);
    AMF_RETURN_IF_FAILED(res, L"CreateContext() failed");

    if (memoryTypeIn == amf::AMF_MEMORY_VULKAN)
    {
        res = amf::AMFContext1Ptr(context)->InitVulkan(NULL);
        AMF_RETURN_IF_FAILED(res, L"InitVulkan(NULL) failed");
        PrepareFillFromHost(context);
    }
#ifdef _WIN32
    else if (memoryTypeIn == amf::AMF_MEMORY_DX9)
    {
        res = context->InitDX9(NULL); // can be DX9 or DX9Ex device
        AMF_RETURN_IF_FAILED(res, L"InitDX9(NULL) failed");
    }
    else if (memoryTypeIn == amf::AMF_MEMORY_DX11)
    {
        res = context->InitDX11(NULL); // can be DX11 device
        AMF_RETURN_IF_FAILED(res, L"InitDX11(NULL) failed");
        PrepareFillFromHost(context);
    }
#endif

    // component: encoder
    res = g_AMFFactory.GetFactory()->CreateComponent(context, codec.c_str(), &encoder);
    AMF_RETURN_IF_FAILED(res, L"CreateComponent(%s) failed", pCodec);

    res = SetEncoderDefaults(encoder, codec);
    if (res != AMF_OK)
    {
        wprintf(L"Could not set default values in encoder. Program will terminate");
        return -1;
    }
    PushParamsToPropertyStorage(&params, ParamEncoderUsage, encoder);
    PushParamsToPropertyStorage(&params, ParamEncoderStatic, encoder);
    PushParamsToPropertyStorage(&params, ParamEncoderDynamic, encoder);

    res = encoder->Init(formatIn, widthIn, heightIn);
    AMF_RETURN_IF_FAILED(res, L"encoder->Init() failed");

    // if we want to use pre-rendered frames
    // start filling them now
    std::vector<amf::AMFSurfacePtr>  preRenderedSurf;
    for (amf_int i = 0; i < preRender; i++)
    {
        amf::AMFSurfacePtr  surfacePreRender;
        FillSurface(context, &surfacePreRender, true);
        preRenderedSurf.push_back(surfacePreRender);
    }

    if (!workAlgorithm.empty() && (wcsicmp(workAlgorithm.c_str(), L"ASAP") == 0))
    {
        PollingThread thread(context, encoder, fileNameOut.c_str(), writeToFileMode);
        thread.Start();

        // encode some frames
        amf_int32 submitted = 0;
        while (submitted < frameCount)
        {
            if (preRenderedSurf.empty() == false)
            {
                surfaceIn = preRenderedSurf[submitted % preRenderedSurf.size()];
            }
            else
            {
                FillSurface(context, &surfaceIn, false);
            }

            // encode
            amf_pts start_time = amf_high_precision_clock();
            surfaceIn->SetProperty(START_TIME_PROPERTY, start_time);

            res = encoder->SubmitInput(surfaceIn);
            if (res == AMF_NEED_MORE_INPUT)
            {
                // do nothing
            }
            else if (res == AMF_INPUT_FULL || res == AMF_DECODER_NO_FREE_SURFACES)
            {   // queue is full; sleep, try to get ready surfaces  in polling thread and repeat submission
                amf_sleep(1);
            }
            else
            {
                AMF_RETURN_IF_FAILED(res, L"SubmitInput() failed");

                surfaceIn = NULL;
                submitted++;
            }
        }

        // drain encoder; input queue can be full
        while (true)
        {
            res = encoder->Drain();
            if (res != AMF_INPUT_FULL) // handle full queue
            {
                break;
            }
            amf_sleep(1); // input queue is full: wait and try again
        }
        thread.WaitForStop();
    }
    else
    {
        // encode some frames
        amf_int32 submitted      = 0;
        amf_pts   min_latency    = INT64_MAX;
        amf_pts   max_latency    = 0;
        amf_pts   latency_time   = 0;
        amf_pts   write_duration = 0;

        // output file, if we have one
        std::ofstream  m_pFile(fileNameOut.c_str(), std::ios::binary);

        while (submitted < frameCount)
        {
            if (preRenderedSurf.empty() == false)
            {
                surfaceIn = preRenderedSurf[submitted % preRenderedSurf.size()];
            }
            else
            {
                FillSurface(context, &surfaceIn, true);
            }

            // encode
            amf_pts start_time = amf_high_precision_clock();

            // we're doing frame-in/frame-out so the input 
            // should never be full
            res = encoder->SubmitInput(surfaceIn);
            AMF_RETURN_IF_FAILED(res, L"SubmitInput() failed");

            surfaceIn = NULL;
            submitted++;

            amf::AMFDataPtr data;
            do
            {
                res = encoder->QueryOutput(&data);
                if (res == AMF_REPEAT)
                {
                    amf_sleep(1);
                }
            } while (res == AMF_REPEAT);

            amf_pts poll_time = amf_high_precision_clock();
            amf_pts tmp_time  = poll_time - start_time;

            min_latency = (tmp_time < min_latency) ? tmp_time : min_latency;
            max_latency = (tmp_time > max_latency) ? tmp_time : max_latency;
            latency_time += tmp_time;

            if ((data != NULL) && (writeToFileMode == true))
            {
                amf::AMFBufferPtr buffer(data); // query for buffer interface
                amf_pts start_time = poll_time;
                m_pFile.write(reinterpret_cast<char*>(buffer->GetNative()), buffer->GetSize());
                write_duration += amf_high_precision_clock() - start_time;
            }
        }

        printTime(latency_time, write_duration, min_latency, max_latency);
    }

    // clear any pre-rendered frames
    preRenderedSurf.clear();

    pColor1 = NULL;
    pColor2 = NULL;

    // cleanup in this order
    surfaceIn = NULL;
    encoder->Terminate();
    encoder = NULL;
    context->Terminate();
    context = NULL; // context is the last

    g_AMFFactory.Terminate();

    return 0;
}
