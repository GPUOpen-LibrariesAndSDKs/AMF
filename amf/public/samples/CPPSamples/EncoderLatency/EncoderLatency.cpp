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
#include "public/include/components/VideoEncoderAV1.h"
#ifdef _WIN32
#include "public/samples/CPPSamples/common/DeviceDX9.h"
#include "public/samples/CPPSamples/common/DeviceDX11.h"
#include "public/samples/CPPSamples/common/DeviceDX12.h"
#endif
#include "public/samples/CPPSamples/common/DeviceVulkan.h"
#include "public/samples/CPPSamples/common/EncoderParamsAVC.h"
#include "public/samples/CPPSamples/common/EncoderParamsHEVC.h"
#include "public/samples/CPPSamples/common/EncoderParamsAV1.h"
#include "public/samples/CPPSamples/common/SurfaceGenerator.h"
#include "public/samples/CPPSamples/common/PollingThread.h"
#include "../common/ParametersStorage.h"
#include "../common/CmdLineParser.h"
#include "../common/CmdLogger.h"
#include "../common/PipelineDefines.h"

#include <fstream>
#include <iostream>

#define AMF_FACILITY L"EncoderLatency"

static AMF_COLOR_BIT_DEPTH_ENUM eDepth = AMF_COLOR_BIT_DEPTH_8;
static amf_int32 frameCount = 500;
static amf_bool frameCountPassedIn = false;
static amf_int32 preRender = 0;
static amf_int32 vcnInstance = -1;
static bool bMaximumSpeed = true;
static bool bRTModeFrameBase = true;              // real time encode is frame based (true) or stream based (false)
static float fFrameRate = 30.f;
static bool bRealTime = false;
static amf_bool outputExpectedLatency = false;

#ifdef _WIN32
amf::AMF_MEMORY_TYPE    memoryTypeIn = amf::AMF_MEMORY_DX11;
#else
amf::AMF_MEMORY_TYPE    memoryTypeIn = amf::AMF_MEMORY_VULKAN;
#endif
amf::AMF_SURFACE_FORMAT formatIn = amf::AMF_SURFACE_NV12;
amf_int32               widthIn = 1920;
amf_int32               heightIn = 1080;
amf_int32               rectSize = 50;

bool        bWriteToFile = false;
amf_int64   formatType = 0;
amf_int64   memoryIn = 0;
amf_wstring codec = AMFVideoEncoderVCE_AVC; // AVC default. can set using '-codec avc' '-codec hevc' '-codec av1' command line argument
amf_wstring workAlgorithm = L"ASAP";
amf_wstring fileNameOut;

amf::AMFSurfacePtr pColor1;
amf::AMFSurfacePtr pColor2;

amf::AMFSurfacePtr pColor3;
amf::AMFSurfacePtr pColor4;

static const wchar_t*  PARAM_NAME_WORKALGORITHM = L"ALGORITHM";
static const wchar_t*  PARAM_NAME_PRERENDER     = L"PRERENDER";
static const wchar_t*  PARAM_NAME_VCN_INSTANCE  = L"VCNINSTANCE";
static const wchar_t*  PARAM_NAME_REALTIME      = L"REALTIME";
static const wchar_t*  PARAM_NAME_OUTPUT_EXPECTED_LATENCY = L"OUTPUTEXPECTEDLATENCY";
#if defined(_WIN32)
static const wchar_t*  PARAM_NAME_PRIORITY      = L"PRIORITY";

struct PriorityParam
{
    static AMF_RESULT Converter(const std::wstring& value, amf::AMFVariant& valueOut)
    {
        amf_int64 paramValue = amf::AMFVariant(value.c_str()).ToInt64();
        std::wstring uppValue = toUpper(value);

        if (uppValue == L"IDLE")
        {
            paramValue = IDLE_PRIORITY_CLASS;
        }
        else if (uppValue == L"BELOW_NORMAL")
        {
            paramValue = BELOW_NORMAL_PRIORITY_CLASS;
        }
        else if (uppValue == L"NORMAL")
        {
            paramValue = NORMAL_PRIORITY_CLASS;
        }
        else if (uppValue == L"ABOVE_NORMAL")
        {
            paramValue = ABOVE_NORMAL_PRIORITY_CLASS;
        }
        else if (uppValue == L"HIGH")
        {
            paramValue = HIGH_PRIORITY_CLASS;
        }
        else if (uppValue == L"REALTIME")
        {
            paramValue = REALTIME_PRIORITY_CLASS;
        }
        valueOut = amf_int64(paramValue);
        return AMF_OK;
    }

    static amf_wstring ToString(amf_int64 priorityClass)
    {
#define CASE(x) case x: return L#x
        switch (priorityClass)
        {
            CASE(IDLE_PRIORITY_CLASS);
            CASE(BELOW_NORMAL_PRIORITY_CLASS);
            CASE(NORMAL_PRIORITY_CLASS);
            CASE(ABOVE_NORMAL_PRIORITY_CLASS);
            CASE(HIGH_PRIORITY_CLASS);
            CASE(REALTIME_PRIORITY_CLASS);
        }
        return amf::amf_string_format(L"Priority Unknown(%lld)", priorityClass);
#undef CASE
    }
};
#endif

AMF_RESULT RegisterParams(ParametersStorage* pParams)
{
    pParams->SetParamDescription(PARAM_NAME_WORKALGORITHM, ParamCommon, L"'ASAP' or 'OneInOne' frames submission algorithm", NULL);
    pParams->SetParamDescription(PARAM_NAME_ENGINE, ParamCommon, L"Memory type: DX9Ex, DX11, DX12, Vulkan (h.264 only)", ParamConverterMemoryType);
    pParams->SetParamDescription(PARAM_NAME_CODEC, ParamCommon, L"Codec name (AVC or H264, HEVC or H265, AV1)", ParamConverterCodec);
    pParams->SetParamDescription(PARAM_NAME_INPUT_FORMAT, ParamCommon, L"Supported file formats: RGBA_F16, R10G10B10A2, NV12, P010", ParamConverterFormat);
    pParams->SetParamDescription(PARAM_NAME_INPUT_FRAMES, ParamCommon, L"Output number of frames", ParamConverterInt64);
    pParams->SetParamDescription(PARAM_NAME_PRERENDER, ParamCommon, L"Pre-render number of frames", ParamConverterInt64);
    pParams->SetParamDescription(PARAM_NAME_OUTPUT_WIDTH, ParamCommon, L"Output resolution, width", ParamConverterInt64);
    pParams->SetParamDescription(PARAM_NAME_OUTPUT_HEIGHT, ParamCommon, L"Output resolution, height", ParamConverterInt64);
    pParams->SetParamDescription(PARAM_NAME_VCN_INSTANCE, ParamCommon, L"VCN to test (0 or 1). Navi21 and up only", ParamConverterInt64);
    pParams->SetParamDescription(PARAM_NAME_OUTPUT, ParamCommon, L"Output file name", NULL);
    pParams->SetParamDescription(PARAM_NAME_INPUT, ParamCommon, L"Input file name", NULL);
    pParams->SetParamDescription(PARAM_NAME_INPUT_WIDTH, ParamCommon, L"Input file width", ParamConverterInt64);
    pParams->SetParamDescription(PARAM_NAME_INPUT_HEIGHT, ParamCommon, L"Input file height", ParamConverterInt64);
    pParams->SetParamDescription(PARAM_NAME_REALTIME, ParamCommon, L"Bool, Keep real-time framerate, default false", ParamConverterBoolean);
    pParams->SetParamDescription(PARAM_NAME_OUTPUT_EXPECTED_LATENCY, ParamCommon, L"Bool, Prints an 'expected latency' value (a theoretical performance floor) if true, default false", ParamConverterBoolean);
#if defined(_WIN32)
    pParams->SetParamDescription(PARAM_NAME_PRIORITY, ParamCommon, L"Sets process priority class: (Idle, Below_Normal, Normal, Above_Normal, High, Realtime)", PriorityParam::Converter);
#endif
    pParams->SetParamDescription(PARAM_NAME_ADAPTERID, ParamCommon, L"Index of GPU adapter (number, default = 0)", NULL);

    return AMF_OK;
}

#if defined(_WIN32)
AMF_RESULT ReadParams(ParametersStorage* params)
#else
AMF_RESULT ReadParams(ParametersStorage* params, int argc, char* argv[])
#endif
{
    // register all parameters first
    RegisterParams(params);
    RegisterEncoderParamsAVC(params);
    RegisterEncoderParamsHEVC(params);
    RegisterEncoderParamsAV1(params);

#if defined(_WIN32)
    if (!parseCmdLineParameters(params))
#else
    if (!parseCmdLineParameters(params, argc, argv))
#endif
    {
        LOG_INFO(L"+++ standard +++");
        ParametersStorage paramsCommon;
        RegisterParams(&paramsCommon);
        LOG_INFO(paramsCommon.GetParamUsage());

        LOG_INFO(L"+++ AVC codec +++");
        ParametersStorage paramsAVC;
        RegisterEncoderParamsAVC(&paramsAVC);
        LOG_INFO(paramsAVC.GetParamUsage());

        LOG_INFO(L"+++ HEVC codec +++");
        ParametersStorage paramsHEVC;
        RegisterEncoderParamsHEVC(&paramsHEVC);
        LOG_INFO(paramsHEVC.GetParamUsage());

        LOG_INFO(L"+++ AV1 codec +++");
        ParametersStorage paramsAV1;
        RegisterEncoderParamsAV1(&paramsAV1);
        LOG_INFO(paramsAV1.GetParamUsage());

        return AMF_FAIL;
    }

    // read the codec
    params->GetParamWString(PARAM_NAME_CODEC, codec);

    // clear existing parameters
    params->Clear();

    // update the proper parameters for the correct codec
    RegisterParams(params);
    if (codec == amf_wstring(AMFVideoEncoderVCE_AVC))
    {
        RegisterEncoderParamsAVC(params);
    }
    else if (codec == amf_wstring(AMFVideoEncoder_HEVC))
    {
        RegisterEncoderParamsHEVC(params);
    }
    else if (codec == amf_wstring(AMFVideoEncoder_AV1))
    {
        RegisterEncoderParamsAV1(params);
    }
    else
    {
        LOG_ERROR(L"Invalid codec ID");
        return AMF_FAIL;
    }

    // parse parameters for a final time
#if defined(_WIN32)
    if (!parseCmdLineParameters(params))
#else
    if (!parseCmdLineParameters(params, argc, argv))
#endif
    {
        return AMF_FAIL;
    }

    // load paramters
    if (params->GetParam(PARAM_NAME_INPUT_FRAMES, frameCount) == AMF_OK)
    {
        frameCountPassedIn = true;
    }
    if (params->GetParam(PARAM_NAME_INPUT_HEIGHT, NULL) == AMF_OK &&
        params->GetParam(PARAM_NAME_INPUT_WIDTH, NULL) == AMF_OK)
    {
        params->GetParam(PARAM_NAME_INPUT_WIDTH, widthIn);
        params->GetParam(PARAM_NAME_INPUT_HEIGHT, heightIn);
    }
    else
    {
        params->GetParam(PARAM_NAME_OUTPUT_WIDTH, widthIn);
        params->GetParam(PARAM_NAME_OUTPUT_HEIGHT, heightIn);
    }
    params->GetParam(PARAM_NAME_PRERENDER, preRender);
    params->GetParam(PARAM_NAME_VCN_INSTANCE, vcnInstance);

    params->GetParamWString(PARAM_NAME_WORKALGORITHM, workAlgorithm);
    workAlgorithm = amf::amf_string_to_upper(workAlgorithm);
    params->GetParamWString(PARAM_NAME_OUTPUT, fileNameOut);

    params->GetParam(PARAM_NAME_REALTIME, bRealTime);
    params->GetParam(PARAM_NAME_OUTPUT_EXPECTED_LATENCY, outputExpectedLatency);

    if (codec == amf_wstring(AMFVideoEncoder_HEVC))
    {
        amf_int64 colorDepth;
        if (params->GetParam(AMF_VIDEO_ENCODER_HEVC_COLOR_BIT_DEPTH, colorDepth) == AMF_OK)
        {
            eDepth = colorDepth == 10 ? AMF_COLOR_BIT_DEPTH_10 : AMF_COLOR_BIT_DEPTH_8;
        }
        AMFRate fps = {};
        if (params->GetParam(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, fps) == AMF_OK)
        {
            fFrameRate = float(fps.num) / fps.den;
        }
    }
    else if (codec == amf_wstring(AMFVideoEncoder_AV1))
    {
        AMFRate fps = {};
        if (params->GetParam(AMF_VIDEO_ENCODER_AV1_FRAMERATE, fps) == AMF_OK)
        {
            fFrameRate = float(fps.num) / fps.den;
        }
    }
    else
    {
        AMFRate fps = {};
        if (params->GetParam(AMF_VIDEO_ENCODER_FRAMERATE, fps) == AMF_OK)
        {
            fFrameRate = float(fps.num) / fps.den;
        }

    }

    if (params->GetParam(PARAM_NAME_ENGINE, memoryIn) == AMF_OK)
    {
        memoryTypeIn = (amf::AMF_MEMORY_TYPE)memoryIn;
    }

    if (params->GetParam(PARAM_NAME_INPUT_FORMAT, formatType) == AMF_OK)
    {
        formatIn = (amf::AMF_SURFACE_FORMAT)formatType;
    }

    bWriteToFile = (fileNameOut.empty() != true);
    return AMF_OK;
}

AMF_RESULT ValidateParams(ParametersStorage * pParams)
{
    amf::AMFVariant tmp;
    if (eDepth == AMF_COLOR_BIT_DEPTH_10 && (formatIn == amf::AMF_SURFACE_NV12 || formatIn == amf::AMF_SURFACE_YV12 || formatIn == amf::AMF_SURFACE_BGRA
        || formatIn == amf::AMF_SURFACE_ARGB || formatIn == amf::AMF_SURFACE_RGBA || formatIn == amf::AMF_SURFACE_GRAY8 || formatIn == amf::AMF_SURFACE_YUV420P
        || formatIn == amf::AMF_SURFACE_U8V8 || formatIn == amf::AMF_SURFACE_YUY2))
    {
        if (pParams->GetParam(PARAM_NAME_INPUT_FORMAT, tmp) == AMF_OK)
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
        if (pParams->GetParam(AMF_VIDEO_ENCODER_HEVC_COLOR_BIT_DEPTH, tmp) == AMF_OK)
        {
            printf("[ERROR] Selected surface format is not a 10-bit format, requested parameters combination can't be applied. Program will terminate\n");
            return AMF_INVALID_ARG;
        }

        printf("[WARNING] Default bit depth is 8, but selected surface format is not an 8-bit format. Color depth will be changed to 10 bits\n");
        eDepth = AMF_COLOR_BIT_DEPTH_10;
    }

    if ( ((pParams->GetParam(PARAM_NAME_INPUT, NULL) == AMF_OK) || (pParams->GetParam(PARAM_NAME_INPUT_WIDTH, NULL) == AMF_OK) || (pParams->GetParam(PARAM_NAME_INPUT_HEIGHT, NULL) == AMF_OK)) &&
         ((pParams->GetParam(PARAM_NAME_OUTPUT_WIDTH, NULL) == AMF_OK) || (pParams->GetParam(PARAM_NAME_OUTPUT_HEIGHT, NULL) == AMF_OK)) )
    {
        printf("[WARNING] Input and output dimensions are exclusive - output values ignored and input values used\n");
    }

    return AMF_OK;
};


AMF_RESULT SetEncoderDefaults(ParametersStorage* pParams, amf::AMFComponent* encoder, const  amf_wstring& codecStr)
{
    AMF_RESULT res;
    if (codecStr == amf_wstring(AMFVideoEncoderVCE_AVC))
    {
        // AMF_VIDEO_ENCODER_USAGE needs to be set before the rest, default transcoding
        AMF_RETURN_IF_FAILED(PushParamsToPropertyStorage(pParams, ParamEncoderUsage, encoder));

        // initialize command line parameters
        AMF_RETURN_IF_FAILED(PushParamsToPropertyStorage(pParams, ParamEncoderStatic, encoder));
        AMF_RETURN_IF_FAILED(PushParamsToPropertyStorage(pParams, ParamEncoderDynamic, encoder));

        // if we requested to run a specific VCN instance, check
        // if it's available, otherwise we can't run the test...
        if (vcnInstance != -1)
        {
            amf::AMFCapsPtr encoderCaps;
            if (encoder->GetCaps(&encoderCaps) == AMF_OK)
            {
                amf_uint64  vcnInstCount = 0;
                AMF_RETURN_IF_FAILED(encoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_NUM_OF_HW_INSTANCES, &vcnInstCount), L"Multiple VCN instances not supported");
                AMF_RETURN_IF_FALSE((vcnInstance >= 0) && (vcnInstance < vcnInstCount), AMF_OUT_OF_RANGE, L"Invalid VCN instance %d, requested.  Only %d instances supported.", vcnInstance, vcnInstCount);

                res = encoder->SetProperty(AMF_VIDEO_ENCODER_INSTANCE_INDEX, vcnInstance);
                AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_INSTANCE_INDEX, %d) failed", vcnInstance);
            }
        }

        if (bMaximumSpeed)
        {

             encoder->SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0);
            // do not check error for AMF_VIDEO_ENCODER_B_PIC_PATTERN - can be not supported - check Capability Manager sample
            res = encoder->SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED) failed");
        }

        res = encoder->SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, ::AMFConstructSize(widthIn, heightIn));
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, %dx%d) failed", widthIn, heightIn);
        res = encoder->SetProperty(AMF_VIDEO_ENCODER_LOWLATENCY_MODE, true);
        AMF_RETURN_IF_FAILED(res, L"encoder->SetProperty(AMF_VIDEO_ENCODER_LOWLATENCY_MODE, true) failed");

        res = encoder->SetProperty(AMF_VIDEO_ENCODER_QUERY_TIMEOUT, 50); //ms
        AMF_RETURN_IF_FAILED(res, L"encoder->SetProperty(AMF_VIDEO_ENCODER_QUERY_TIMEOUT, 50) failed");
    }
    else if (codecStr == amf_wstring(AMFVideoEncoder_HEVC))
    {
        // AMF_VIDEO_ENCODER_HEVC_USAGE needs to be set before the rest, default transcoding
        AMF_RETURN_IF_FAILED(PushParamsToPropertyStorage(pParams, ParamEncoderUsage, encoder));

        // initialize command line parameters
        AMF_RETURN_IF_FAILED(PushParamsToPropertyStorage(pParams, ParamEncoderStatic, encoder));
        AMF_RETURN_IF_FAILED(PushParamsToPropertyStorage(pParams, ParamEncoderDynamic, encoder));

        // if we requested to run a specific VCN instance, check
        // if it's available, otherwise we can't run the test...
        if (vcnInstance != -1)
        {
            amf::AMFCapsPtr encoderCaps;
            if (encoder->GetCaps(&encoderCaps) == AMF_OK)
            {
                amf_uint64  vcnInstCount = 0;
                AMF_RETURN_IF_FAILED(encoderCaps->GetProperty(AMF_VIDEO_ENCODER_HEVC_CAP_NUM_OF_HW_INSTANCES, &vcnInstCount), L"Multiple VCN instances not supported");
                AMF_RETURN_IF_FALSE((vcnInstance >= 0) && (vcnInstance < vcnInstCount), AMF_OUT_OF_RANGE, L"Invalid VCN instance %d, requested.  Only %d instances supported.", vcnInstance, vcnInstCount);

                res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_INSTANCE_INDEX, vcnInstance);
                AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_INSTANCE_INDEX, %d) failed", vcnInstance);
            }
        }

        if (bMaximumSpeed)
        {
            res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED)");
        }

        res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, ::AMFConstructSize(widthIn, heightIn));
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, %dx%d) failed", widthIn, heightIn);

        res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_LOWLATENCY_MODE, true);
        AMF_RETURN_IF_FAILED(res, L"encoder->SetProperty(AMF_VIDEO_ENCODER_LOWLATENCY_MODE, true) failed");

        res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUERY_TIMEOUT, 50); //ms
        AMF_RETURN_IF_FAILED(res, L"encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUERY_TIMEOUT, 50) failed");
    }
    else if (codecStr == amf_wstring(AMFVideoEncoder_AV1))
    {
        // AMF_VIDEO_ENCODER_AV1_USAGE needs to be set before the rest, default transcoding
        res = encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_USAGE, AMF_VIDEO_ENCODER_AV1_USAGE_LOW_LATENCY);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_USAGE, AMF_VIDEO_ENCODER_AV1_USAGE_LOW_LATENCY)");

        AMF_RETURN_IF_FAILED(PushParamsToPropertyStorage(pParams, ParamEncoderUsage, encoder));

        // initialize command line parameters
        AMF_RETURN_IF_FAILED(PushParamsToPropertyStorage(pParams, ParamEncoderStatic, encoder));
        AMF_RETURN_IF_FAILED(PushParamsToPropertyStorage(pParams, ParamEncoderDynamic, encoder));

        // if we requested to run a specific VCN instance, check
        // if it's available, otherwise we can't run the test...
        if (vcnInstance != -1)
        {
            amf::AMFCapsPtr encoderCaps;
            if (encoder->GetCaps(&encoderCaps) == AMF_OK)
            {
                amf_uint64  vcnInstCount = 0;
                AMF_RETURN_IF_FAILED(encoderCaps->GetProperty(AMF_VIDEO_ENCODER_AV1_CAP_NUM_OF_HW_INSTANCES, &vcnInstCount), L"Multiple VCN instances not supported");
                AMF_RETURN_IF_FALSE((vcnInstance >= 0) && (vcnInstance < vcnInstCount), AMF_OUT_OF_RANGE, L"Invalid VCN instance %d, requested.  Only %d instances supported.", vcnInstance, vcnInstCount);

                res = encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_ENCODER_INSTANCE_INDEX, vcnInstance);
                AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_ENCODER_INSTANCE_INDEX, %d) failed", vcnInstance);
            }
        }

        if (bMaximumSpeed)
        {
            res = encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET, AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_SPEED);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET, AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_SPEED)");
        }

        res = encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_FRAMESIZE, ::AMFConstructSize(widthIn, heightIn));
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_FRAMESIZE, %dx%d) failed", widthIn, heightIn);

        res = encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE, AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE_LOWEST_LATENCY);
        AMF_RETURN_IF_FAILED(res, L"encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE, AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE_LOWEST_LATENCY) failed");

        res = encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_QUERY_TIMEOUT, 50); //ms
        AMF_RETURN_IF_FAILED(res, L"encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_QUERY_TIMEOUT, 50) failed");
    }
    return AMF_OK;
}

static void PrintTime(amf_int32 totalFrames, amf_pts totalTime, amf_pts firstFrameLatency,
                      amf_pts minLatency, amf_pts maxLatency, amf_pts latencyTime,
                      amf_pts firstSliceLatency, amf_pts expectedLatency = INT64_MAX)
{
    if (totalFrames == 0 || totalTime == 0)
    {
        AMFTraceWarning(AMF_FACILITY, L"PrintTime 'totalFrames' or 'totalTime' parameter was set to 0, latency information will not be printed");
        return;
    }

    fprintf(stderr, "Total  : Frames = %i Duration = %.2fms FPS = %.2fframes\n",
        totalFrames,
        double(totalTime) / AMF_MILLISECOND,
        double(AMF_SECOND) * double(totalFrames) / double(totalTime));

    if (expectedLatency != INT64_MAX)
    {
        // Expected latency is an optional parameter. If not output by the program, it will be set INT64_MAX.
        fprintf(stderr, "Latency: Expected = %.2fms\n", double(expectedLatency) / AMF_MILLISECOND);
    }

    fprintf(stderr,
        "Latency: First,Min,Max = %.2fms, %.2fms, %.2fms\n" \
        "Latency: Average = %.2fms\n" \
        "First Slice Latency = %.2fms\n",
        double(firstFrameLatency) / AMF_MILLISECOND,
        double(minLatency) / AMF_MILLISECOND,
        double(maxLatency) / AMF_MILLISECOND,
        double(latencyTime) / AMF_MILLISECOND / totalFrames,
        double(firstSliceLatency) / AMF_MILLISECOND / totalFrames);

    fflush(stderr);
}

class EncPollingThread : public PollingThread
{
public:
    EncPollingThread(amf::AMFContext* pContext, amf::AMFComponent* pEncoder, const wchar_t* pFileName, const amf_wstring& enc_codec, amf_pts expectedLatency);
protected:
    virtual bool Init() override;
    void ProcessData(amf::AMFData* pData) override;
    void PrintResults() override;

    amf_pts m_StartTime;
    amf_pts m_FirstFrameLatency;
    amf_pts m_FrameTime;
    amf_pts m_MinLatency;
    amf_pts m_MaxLatency;
    amf_pts m_FirstSliceLatency;
    amf_pts m_ExpectedLatency;
    amf_bool m_IsFirstSlice;
    amf_wstring m_Codec;
};

EncPollingThread::EncPollingThread(amf::AMFContext* pContext, amf::AMFComponent* pEncoder, const wchar_t* pFileName, const amf_wstring& enc_codec, amf_pts expectedLatency)
    : PollingThread(pContext, pEncoder, pFileName, bWriteToFile),
    m_StartTime(0),
    m_FirstFrameLatency(0),
    m_FrameTime(0),
    m_MinLatency(INT64_MAX),
    m_MaxLatency(0),
    m_FirstSliceLatency(0),
    m_ExpectedLatency(expectedLatency),
    m_IsFirstSlice(true),
    m_Codec(enc_codec)
{}


bool EncPollingThread::Init()
{
    m_StartTime = amf_high_precision_clock();

    bool ret = PollingThread::Init();

    m_FirstFrameLatency = 0;
    m_FrameTime = 0;
    m_MinLatency = INT64_MAX;
    m_MaxLatency = 0;
    m_IsFirstSlice = true;
    m_FirstSliceLatency = 0;

    return ret;
}

void EncPollingThread::ProcessData(amf::AMFData* pData)
{
    amf_pts pollTime = amf_high_precision_clock();
    amf_pts startTime = 0;
    amf_uint64  bufType = AMF_VIDEO_ENCODER_OUTPUT_BUFFER_TYPE_FRAME;
    amf_bool lastOutput = false;

    pData->GetProperty(START_TIME_PROPERTY, &startTime);
    if (startTime < m_LastPollTime) // remove wait time if submission was faster then encode
    {
        startTime = m_LastPollTime;
    }

    m_LastPollTime = pollTime;
    m_FrameTime += pollTime - startTime;

    if (m_IsFirstSlice)
    {
        m_FirstSliceLatency += m_FrameTime;
    }

    if (codec == amf_wstring(AMFVideoEncoderVCE_AVC))
    {
        pData->GetProperty(AMF_VIDEO_ENCODER_OUTPUT_BUFFER_TYPE, &bufType);
        lastOutput = (bufType == AMF_VIDEO_ENCODER_OUTPUT_BUFFER_TYPE_SLICE_LAST) ||
                      (bufType == AMF_VIDEO_ENCODER_OUTPUT_BUFFER_TYPE_FRAME);
    }
    else if (codec == amf_wstring(AMFVideoEncoder_HEVC))
    {
        pData->GetProperty(AMF_VIDEO_ENCODER_HEVC_OUTPUT_BUFFER_TYPE, &bufType);
        lastOutput = (bufType == AMF_VIDEO_ENCODER_HEVC_OUTPUT_BUFFER_TYPE_SLICE_LAST) ||
                      (bufType == AMF_VIDEO_ENCODER_HEVC_OUTPUT_BUFFER_TYPE_FRAME);
    }
    else if (codec == amf_wstring(AMFVideoEncoder_AV1))
    {
        pData->GetProperty(AMF_VIDEO_ENCODER_AV1_OUTPUT_BUFFER_TYPE, &bufType);
        lastOutput = (bufType == AMF_VIDEO_ENCODER_AV1_OUTPUT_BUFFER_TYPE_TILE_LAST) ||
                      (bufType == AMF_VIDEO_ENCODER_AV1_OUTPUT_BUFFER_TYPE_FRAME);
    }

    // last output of a frame
    if (lastOutput)
    {
        if (m_FirstFrameLatency == 0)
        {
            m_FirstFrameLatency = m_FrameTime;
        }
        else
        {
            m_MinLatency = m_MinLatency < m_FrameTime ? m_MinLatency : m_FrameTime;
            m_MaxLatency = m_MaxLatency > m_FrameTime ? m_MaxLatency : m_FrameTime;
        }

        m_LatencyTime += m_FrameTime;
        m_FrameTime = 0;
        m_IsFirstSlice = true;
    }
    else
    {
        m_IsFirstSlice = false;
    }

    amf::AMFBufferPtr pBuffer(pData); // query for buffer interface
    if ((bWriteToFile == true) && (m_pFile != NULL))
    {
        m_pFile->Write(pBuffer->GetNative(), pBuffer->GetSize(), NULL);
        m_WriteDuration += amf_high_precision_clock() - pollTime;
    }
}

void EncPollingThread::PrintResults()
{
    amf_pts endTime = amf_high_precision_clock();

    PrintTime(frameCount, endTime - m_StartTime, m_FirstFrameLatency, m_MinLatency,
              m_MaxLatency, m_LatencyTime, m_FirstSliceLatency, m_ExpectedLatency);
}

void CheckAndRestartReader(RawStreamReader *pRawStreamReader)
{
    if ((frameCountPassedIn == true) && (pRawStreamReader->GetPosition() == 1.0))
    {
        pRawStreamReader->RestartReader();
    }
}

#if defined(_WIN32)
int _tmain(int /* argc */, _TCHAR* /* argv */[])
#else
int main(int argc, char* argv[])
#endif
{
    ParametersStorage params;
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

#if defined(_WIN32)
    {
        amf_int64 priorityClass = NORMAL_PRIORITY_CLASS;
        if (params.GetParam(PARAM_NAME_PRIORITY, priorityClass) == AMF_OK)
        {
            amf_bool bResult = SetPriorityClass(GetCurrentProcess(), static_cast<DWORD>(priorityClass));
            wprintf(L"SetPriorityClass(GetCurrentProcess(), %s) returned %s\n",
                    PriorityParam::ToString(priorityClass).c_str(),
                    (bResult == true ? L"true" : L"false"));
        }
    }
#endif

    res = g_AMFFactory.Init();
    if (res != AMF_OK)
    {
        wprintf(L"AMF Failed to initialize");
        return -1;
    }

    ::amf_increase_timer_precision();

    amf::AMFTraceEnableWriter(AMF_TRACE_WRITER_CONSOLE, true);
    amf::AMFTraceEnableWriter(AMF_TRACE_WRITER_DEBUG_OUTPUT, true);

    // adapterID
    amf_uint32 adapterID = 0;
    params.GetParam(PARAM_NAME_ADAPTERID, adapterID);

    // Device
#ifdef _WIN32
	DeviceDX9 deviceDX9{};
	DeviceDX11 deviceDX11{};
	DeviceDX12 deviceDX12{};
#endif
	DeviceVulkan deviceVulkan{};

    // initialize AMF
    amf::AMFContextPtr context;
    amf::AMFComponentPtr encoder;
    amf::AMFSurfacePtr surfaceIn;

    // context
    res = g_AMFFactory.GetFactory()->CreateContext(&context);
    AMF_RETURN_IF_FAILED(res, L"CreateContext() failed");

    if (memoryTypeIn == amf::AMF_MEMORY_VULKAN)
    {
        res = deviceVulkan.Init(adapterID, amf::AMFContext1Ptr(context));
        AMF_RETURN_IF_FAILED(res, L"deviceVulkan.Init() failed");

        res = amf::AMFContext1Ptr(context)->InitVulkan(deviceVulkan.GetDevice());
        AMF_RETURN_IF_FAILED(res, L"InitVulkan() failed");
        PrepareFillFromHost(context, memoryTypeIn, formatIn, widthIn, heightIn, false);
    }
#ifdef _WIN32
    else if (memoryTypeIn == amf::AMF_MEMORY_DX9)
    {
        res = deviceDX9.Init(true, adapterID, false, widthIn, heightIn);
        if (res != AMF_OK) {
            res = deviceDX9.Init(false, adapterID, false, widthIn, heightIn);
        }
        AMF_RETURN_IF_FAILED(res, L"deviceDX9.Init() failed");

        res = context->InitDX9(deviceDX9.GetDevice()); // can be DX9 or DX9Ex device
        AMF_RETURN_IF_FAILED(res, L"InitDX9() failed");
    }
    else if (memoryTypeIn == amf::AMF_MEMORY_DX11)
    {
        res = deviceDX11.Init(adapterID);
        AMF_RETURN_IF_FAILED(res, L"deviceDX11.Init() failed");

        res = context->InitDX11(deviceDX11.GetDevice()); // can be DX11 device
        AMF_RETURN_IF_FAILED(res, L"InitDX11() failed");
        PrepareFillFromHost(context, memoryTypeIn, formatIn, widthIn, heightIn, false);
    }
    else if (memoryTypeIn == amf::AMF_MEMORY_DX12)
    {
        res = deviceDX12.Init(adapterID);
        AMF_RETURN_IF_FAILED(res, L"deviceDX12.Init() failed");

        res = amf::AMFContext2Ptr(context)->InitDX12(deviceDX12.GetDevice()); // can be DX11 device
        AMF_RETURN_IF_FAILED(res, L"InitDX12() failed");
        PrepareFillFromHost(context, memoryTypeIn, formatIn, widthIn, heightIn, false);
    }
#endif

    // file reader if needed
    RawStreamReaderPtr fileReader;
    if (params.GetParam(PARAM_NAME_INPUT, NULL) == AMF_OK)
    {
        fileReader = RawStreamReaderPtr(new RawStreamReader());
        res = fileReader->Init(&params, context);
        AMF_RETURN_IF_FAILED(res, L"fileReader->Init() failed");

        // if width/height/format are not provided, reader will
        // try to figure out from the name of the file
        widthIn = fileReader->GetWidth();
        heightIn = fileReader->GetHeight();
        formatIn = fileReader->GetFormat();
    }
    PipelineElementPtr pipelineElPtr(fileReader);

    wprintf(L"Encoder: %s\n", codec.c_str());
    // component: encoder
    res = g_AMFFactory.GetFactory()->CreateComponent(context, codec.c_str(), &encoder);
    AMF_RETURN_IF_FAILED(res, L"CreateComponent(%s) failed", codec.c_str());

    res = SetEncoderDefaults(&params, encoder, codec);
    AMF_RETURN_IF_FAILED(res, L"Could not set default values in encoder.");

    res = encoder->Init(formatIn, widthIn, heightIn);
    AMF_RETURN_IF_FAILED(res, L"encoder->Init() failed");

    // if we want to use pre-rendered frames
    // start filling them now
    std::vector<amf::AMFSurfacePtr>  preRenderedSurf;
    for (amf_int i = 0; i < preRender; i++)
    {
        amf::AMFSurfacePtr  surfacePreRender;
        if (pipelineElPtr != NULL)
        {
            CheckAndRestartReader(fileReader.get());
            res = ReadSurface(pipelineElPtr, &surfacePreRender, memoryTypeIn);
            if (res == AMF_EOF)
            {
                break;
            }
        }
        else
        {
            FillSurface(context, &surfacePreRender, memoryTypeIn, formatIn, widthIn, heightIn, true);
        }
        preRenderedSurf.push_back(surfacePreRender);
    }

    // If the 'OutputExpectedLatency' argument is set, calculate and output an 'expected latency' value. 
    // This serves as the theoretical performance floor for the encoding of a single frame.
    amf_pts expectedLatency = INT64_MAX;

    if (outputExpectedLatency)
    {
#ifdef  _WIN32
    amf::AMFCapsPtr pCaps;
    amf::AMFContextPtr tempContext;
    amf::AMFComponentPtr tempEncoder;
    DeviceDX11 tempDeviceDX11 = {};

    if (memoryTypeIn != amf::AMF_MEMORY_DX11) 
    {
        // Initialize a temporary DX11 context and encoder, then query its capabilities.
        res = g_AMFFactory.GetFactory()->CreateContext(&tempContext);
        AMF_RETURN_IF_FAILED(res, L"CreateContext() failed");
        res = tempDeviceDX11.Init(adapterID);
        AMF_RETURN_IF_FAILED(res, L"deviceDX11.Init() failed");
        res = tempContext->InitDX11(tempDeviceDX11.GetDevice());
        AMF_RETURN_IF_FAILED(res, L"InitDX11() failed");
        res = g_AMFFactory.GetFactory()->CreateComponent(tempContext, codec.c_str(), &tempEncoder);
        AMF_RETURN_IF_FAILED(res, L"CreateComponent(%s) failed", codec.c_str());
        res = SetEncoderDefaults(&params, tempEncoder, codec);
        AMF_RETURN_IF_FAILED(res, L"Could not set default values in tempEncoder.");
        res = tempEncoder->Init(formatIn, widthIn, heightIn);
        AMF_RETURN_IF_FAILED(res, L"tempEncoder->Init() failed");
        res = tempEncoder->GetCaps(&pCaps);
    }
    else 
    {
        res = encoder->GetCaps(&pCaps);
    }

    if (res != AMF_OK)
    {
        AMFTraceWarning(AMF_FACILITY, L"Failed to query encoder capabilities, expected latency will be unavailable");
    }
    
    // Query maximum encoder throughput based on the specified codec.
    amf_int64 maxThroughput = 0;

    if (res == AMF_OK && codec == amf_wstring(AMFVideoEncoderVCE_AVC))
    {
        res = pCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_MAX_THROUGHPUT, &maxThroughput);
    }
    else if (res == AMF_OK && codec == amf_wstring(AMFVideoEncoder_HEVC))
    {
        res = pCaps->GetProperty(AMF_VIDEO_ENCODER_HEVC_CAP_MAX_THROUGHPUT, &maxThroughput);
    }
    else if (res == AMF_OK && codec == amf_wstring(AMFVideoEncoder_AV1))
    {
        res = pCaps->GetProperty(AMF_VIDEO_ENCODER_AV1_CAP_MAX_THROUGHPUT, &maxThroughput);
    }

    if (res == AMF_OK && maxThroughput != 0)
    {
        // Encoder throughput capabilities are reported in units of 16 x 16 macroblocks.
        amf_pts macroblocksPerFrame = ((widthIn) / 16) * ((heightIn) / 16);
        // Calculate expected latency: (macroblocks / frame) / (macroblocks / seconds) -> seconds / frame.
        expectedLatency = ((AMF_SECOND * macroblocksPerFrame) / maxThroughput);
    }
    else
    {
        AMFTraceWarning(AMF_FACILITY, L"Failure to query maximum throughput, expected latency will be unavailable");
    }

    if (memoryTypeIn != amf::AMF_MEMORY_DX11)
    {
        tempEncoder->Terminate();
        tempEncoder = NULL;
        tempContext->Terminate();
        tempContext = NULL;
        tempDeviceDX11.Terminate();
    }
    
    pCaps = NULL;
#else
    // Querying encoder throughput is only available on Windows.
    AMFTraceWarning(AMF_FACILITY, L"Expected latency estimate only available on Windows");
#endif //  _WIN32
    }

    if (workAlgorithm == L"ASAP")
    {
        EncPollingThread thread(context, encoder, fileNameOut.c_str(), codec, expectedLatency);
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
                if (pipelineElPtr != NULL)
                {
                    CheckAndRestartReader(fileReader.get());
                    res = ReadSurface(pipelineElPtr, &surfaceIn, memoryTypeIn);
                    if (res == AMF_EOF)
                    {
                        frameCount = submitted;
                        continue;
                    }
                }
                else
                {
                    FillSurface(context, &surfaceIn, memoryTypeIn, formatIn, widthIn, heightIn, false);
                }
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

        // Need to request stop before waiting for stop
        if (thread.RequestStop() == false)
        {
            AMFTraceError(AMF_FACILITY, L"thread.RequestStop() Failed");
        }

        if (thread.WaitForStop() == false)
        {
            AMFTraceError(AMF_FACILITY, L"thread.WaitForStop() Failed");
        }
    }
    else
    {
        // encode some frames
        amf_int32 submitted = 0;
        amf_pts   firstFrameLatency = 0;
        amf_pts   minLatency = INT64_MAX;
        amf_pts   maxLatency = 0;
        amf_pts   latencyTime = 0;
        amf_pts   writeDuration = 0;
        amf::AMFPreciseWaiter waiter;
        amf_pts firstSliceLatency = 0;

        // output file, if we have one
        amf::AMFDataStreamPtr pLogFile;
        if (bWriteToFile == true)
        {
            res = amf::AMFDataStream::OpenDataStream(fileNameOut.c_str(), amf::AMFSO_WRITE, amf::AMFFS_SHARE_READ, &pLogFile);
            AMF_ASSERT_OK(res, L"Failed to open file %s", fileNameOut.c_str());
        }

        amf_pts beginTime = amf_high_precision_clock();
        while (submitted < frameCount)
        {
            amf_pts beginFrame = amf_high_precision_clock();
            amf_bool    firstSliceOutput = true;
            amf_uint64  bufType = AMF_VIDEO_ENCODER_OUTPUT_BUFFER_TYPE_FRAME;

            if (preRenderedSurf.empty() == false)
            {
                surfaceIn = preRenderedSurf[submitted % preRenderedSurf.size()];
            }
            else
            {
                if (pipelineElPtr != NULL)
                {
                    CheckAndRestartReader(fileReader.get());
                    res = ReadSurface(pipelineElPtr, &surfaceIn, memoryTypeIn);
                    if (res == AMF_EOF)
                    {
                        frameCount = submitted;
                        continue;
                    }
                }
                else
                {
                    FillSurface(context, &surfaceIn, memoryTypeIn, formatIn, widthIn, heightIn, true);
                }
            }

            // encode
            amf_pts startTime = amf_high_precision_clock();

            // we're doing frame-in/frame-out so the input
            // should never be full
            res = encoder->SubmitInput(surfaceIn);
            AMF_RETURN_IF_FAILED(res, L"SubmitInput() failed");

            surfaceIn = NULL;
            submitted++;
            amf_pts lastPollTime = startTime;
            amf_pts pollTime = 0;  //the time data is ready
            amf_pts sliceTime = 0; // slice time (or slice latency)
            amf_pts frameTime = 0; // frame latency

            amf::AMFDataPtr data;
            amf_bool lastOutput = false;

            do { // get all encoded slices of a frame
                do
                {
                    res = encoder->QueryOutput(&data);
                    if (res == AMF_REPEAT)
                    {
                        amf_sleep(1);
                    }
                } while (res == AMF_REPEAT);

                pollTime = amf_high_precision_clock();
                sliceTime = pollTime - lastPollTime;

                lastPollTime = pollTime;

                if ((data != NULL) && (bWriteToFile == true) && (pLogFile != NULL))
                {
                    amf::AMFBufferPtr buffer(data); // query for buffer interface
                    pLogFile->Write(buffer->GetNative(), buffer->GetSize(), NULL);
                    writeDuration += amf_high_precision_clock() - pollTime;
                }

                frameTime += sliceTime;
                if (firstSliceOutput)
                {
                    firstSliceLatency += sliceTime;
                    firstSliceOutput = false;
                }

                if (codec == amf_wstring(AMFVideoEncoderVCE_AVC))
                {
                    data->GetProperty(AMF_VIDEO_ENCODER_OUTPUT_BUFFER_TYPE, &bufType);
                    lastOutput = (bufType == AMF_VIDEO_ENCODER_OUTPUT_BUFFER_TYPE_SLICE_LAST) ||
                                  (bufType == AMF_VIDEO_ENCODER_OUTPUT_BUFFER_TYPE_FRAME);
                }
                else if (codec == amf_wstring(AMFVideoEncoder_HEVC))
                {
                    data->GetProperty(AMF_VIDEO_ENCODER_HEVC_OUTPUT_BUFFER_TYPE, &bufType);
                    lastOutput = (bufType == AMF_VIDEO_ENCODER_HEVC_OUTPUT_BUFFER_TYPE_SLICE_LAST) ||
                                  (bufType == AMF_VIDEO_ENCODER_HEVC_OUTPUT_BUFFER_TYPE_FRAME);
                }
                else if (codec == amf_wstring(AMFVideoEncoder_AV1))
                {
                    data->GetProperty(AMF_VIDEO_ENCODER_AV1_OUTPUT_BUFFER_TYPE, &bufType);
                    lastOutput = (bufType == AMF_VIDEO_ENCODER_AV1_OUTPUT_BUFFER_TYPE_TILE_LAST) ||
                                  (bufType == AMF_VIDEO_ENCODER_AV1_OUTPUT_BUFFER_TYPE_FRAME);
                }
            } while (!lastOutput);

            if (firstFrameLatency == 0)
            {
                firstFrameLatency = frameTime;
            }
            else
            {
                minLatency = (frameTime < minLatency) ? frameTime : minLatency;
                maxLatency = (frameTime > maxLatency) ? frameTime : maxLatency;
            }
            latencyTime += frameTime;


            if (bRealTime == true)
            {
                amf_pts endFrame = amf_high_precision_clock();
                amf_pts timeToSleep;

                if (bRTModeFrameBase)
                {
                    timeToSleep = amf_pts(AMF_SECOND / fFrameRate) - (endFrame - beginFrame);
                }
                else
                {
                    timeToSleep = amf_pts(AMF_SECOND / fFrameRate) * submitted - (endFrame - beginTime);
                }
                if (timeToSleep > 0)
                {
                    waiter.Wait(timeToSleep);
                }
            }
        }

        amf_pts endTime = amf_high_precision_clock();

        PrintTime(frameCount, endTime - beginTime, firstFrameLatency, minLatency,
                  maxLatency, latencyTime, firstSliceLatency, expectedLatency);

        if (pLogFile != NULL)
        {
            pLogFile->Close();
            pLogFile = NULL;
        }
    }

    // clear any pre-rendered frames
    preRenderedSurf.clear();

    pColor1 = NULL;
    pColor2 = NULL;
    // cleanup in this order
    surfaceIn = NULL;
    encoder->Terminate();
    encoder = NULL;
    fileReader = NULL;
    pipelineElPtr = NULL;
    context->Terminate();
    context = NULL; // context is the last

#ifdef _WIN32
    deviceDX9.Terminate();
    deviceDX11.Terminate();
    deviceDX12.Terminate();
#endif
    deviceVulkan.Terminate();

    g_AMFFactory.Terminate();

    return 0;
}
