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
// Copyright (c) 2024 Advanced Micro Devices, Inc. All rights reserved.
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
#ifdef _WIN32
#include <tchar.h>
#include <d3d11.h>
#endif

#include "public/common/AMFFactory.h"
#include "public/common/ByteArray.h"
#include "public/include/components/FRC.h"
#include "public/common/Thread.h"
#include "public/common/AMFSTL.h"
#include "public/common/TraceAdapter.h"
#include "public/samples/CPPSamples/common/PipelineDefines.h"
#include "public/samples/CPPSamples/common/RawStreamReader.h"
#include "public/samples/CPPSamples/common/SurfaceGenerator.h"
#include "public/samples/CPPSamples/common/SurfaceUtils.h"
#include "public/samples/CPPSamples/common/PollingThread.h"
#include "public/samples/CPPSamples/common/ParametersStorage.h"
#include "public/samples/CPPSamples/common/CmdLineParser.h"

#define AMF_FACILITY L"SimpleFRC"

static amf_int32 widthIn = 1920;
static amf_int32 heightIn = 1080;
static amf_int32 frameCount = 5000;
static amf::AMF_SURFACE_FORMAT formatIn = amf::AMF_SURFACE_NV12;
#ifdef _WIN32
static amf::AMF_MEMORY_TYPE memoryTypeIn = amf::AMF_MEMORY_DX11;
#else
static amf::AMF_MEMORY_TYPE memoryTypeIn = amf::AMF_MEMORY_VULKAN;
#endif
static amf_int64 frcEngineType = FRC_ENGINE_DX11;
static amf_int64 frcMode = FRC_x2_PRESENT;
static amf_int64 frcFallbackFlag = 0;
static amf_int64 frcIndicator = false;
static amf_int64 frcProfile = FRC_PROFILE_HIGH;
static amf_int64 frcMVSearchMode = FRC_MV_SEARCH_NATIVE;
static const wchar_t* outfileName = L"FRCout.yuv";
static const wchar_t* infileName = L"ducks_take_off_1080p50.yuv";
amf::AMFDataStreamPtr inFile = NULL;
amf_int32 rectSize = 50;
amf::AMFSurfacePtr pColor1;
amf::AMFSurfacePtr pColor2;
amf::AMFSurfacePtr pColor3;
amf::AMFSurfacePtr pColor4;

static const wchar_t* PARAM_NAME_COMPAREFILE = L"compareFile";
static const wchar_t* PARAM_NAME_MEMORYTYPE = L"memoryType";
static const wchar_t* PARAM_NAME_FRC_ENGINE = L"frcengine";
static const wchar_t* PARAM_NAME_FRC_MODE = L"frcmode";
static const wchar_t* PARAM_NAME_FRC_ENABLE_FALLBACK = L"frcfallback";
static const wchar_t* PARAM_NAME_FRC_INDICATOR = L"frcindicator";
static const wchar_t* PARAM_NAME_FRC_PROFILE = L"frcprofile";
static const wchar_t* PARAM_NAME_FRC_MV_SEARCH_MODE = L"frcsearchmode";

AMF_RESULT RegisterParams(ParametersStorage* pParams)
{
    pParams->SetParamDescription(PARAM_NAME_INPUT, ParamCommon, L"input yuv name[manadatory]", NULL);
    pParams->SetParamDescription(PARAM_NAME_OUTPUT, ParamCommon, L"output YUV name[manadatory]", NULL);
    pParams->SetParamDescription(PARAM_NAME_INPUT_WIDTH, ParamCommon, L"input YUV width, default 1920", ParamConverterInt64);
    pParams->SetParamDescription(PARAM_NAME_INPUT_HEIGHT, ParamCommon, L"input YUV height default 1080", ParamConverterInt64);
    pParams->SetParamDescription(PARAM_NAME_INPUT_FORMAT, ParamCommon, L"input format, support NV12/RGBA/BGRA/ARGB/RGBA_16/R10G10B10", ParamConverterFormat);
    pParams->SetParamDescription(PARAM_NAME_MEMORYTYPE, ParamCommon, L"DX11/DX12/Vulkan, default value is DX11", ParamConverterMemoryType);
    pParams->SetParamDescription(PARAM_NAME_FRC_ENGINE, ParamCommon, L"Frame Rate Conversion, (DX11, DX12) default = DX11", ParamConverterFRCEngine);
    pParams->SetParamDescription(PARAM_NAME_FRC_MODE, ParamCommon, L"FRC Mode, (OFF, ON, INTERPOLATED, x2_PRESENT) default = x2_PRESENT ", ParamConverterFRCMode);
    pParams->SetParamDescription(PARAM_NAME_FRC_ENABLE_FALLBACK, ParamCommon, L"FRC enable fallback, (true, false), default = false", ParamConverterBoolean);
    pParams->SetParamDescription(PARAM_NAME_FRC_INDICATOR, ParamCommon, L"FRC Indicator, (true, false),  default = false", ParamConverterBoolean);
    pParams->SetParamDescription(PARAM_NAME_FRC_PROFILE, ParamCommon, L"FRC Profile,  default High", ParamConverterFRCProfile);
    pParams->SetParamDescription(PARAM_NAME_FRC_MV_SEARCH_MODE, ParamCommon, L"FRC Performance,  (NATIVE, PERFORMANCE) default = NATIVE", ParamConverterFRCPerformance);

    return AMF_OK;
}

void* dumpSurface(amf::AMFDataStreamPtr file, amf::AMFSurfacePtr pSurface)
{
    AMF_RESULT res = pSurface->Convert(amf::AMF_MEMORY_HOST);
    AMF_ASSERT_OK(res, L"Failed to convert surface to host.");

    switch (pSurface->GetFormat())
    {
    case amf::AMF_SURFACE_NV12:
        WritePlane(pSurface->GetPlane(amf::AMF_PLANE_Y), file); // get y-plane pixels
        WritePlane(pSurface->GetPlane(amf::AMF_PLANE_UV), file); // get uv-plane pixels
        break;
    case amf::AMF_SURFACE_ARGB:
    case amf::AMF_SURFACE_RGBA:
    case amf::AMF_SURFACE_BGRA:
    case amf::AMF_SURFACE_RGBA_F16:
    case amf::AMF_SURFACE_R10G10B10A2:
        WritePlane(pSurface->GetPlane(amf::AMF_PLANE_PACKED), file); // get packed pixels
        break;
    default:
        wprintf(L"Not supported format in dumpSurface\n");
        break;
    }
    return NULL;
}

AMF_RESULT SimpleFRC(ParametersStorage &params, amf_bool useDefaultFrames, amf_bool skipOutputDump)
{
    AMF_RESULT res = AMF_OK; // error checking can be added later
    res = g_AMFFactory.Init();
    if (res != AMF_OK)
    {
        wprintf(L"AMF Failed to initialize");
        return res;
    }

    ::amf_increase_timer_precision();

    // initialize AMF
    amf::AMFContextPtr pContext;
    amf::AMFComponentPtr pFRC;
    RawStreamReaderPtr pFileReader;
    amf::AMFSurfacePtr pSurfaceIn;

    // context
    res = g_AMFFactory.GetFactory()->CreateContext(&pContext);
    AMF_RETURN_IF_FAILED(res, L"CreateContext() failed");

    if (memoryTypeIn == amf::AMF_MEMORY_VULKAN)
    {
        res = amf::AMFContext1Ptr(pContext)->InitVulkan(NULL);
        AMF_RETURN_IF_FAILED(res, L"InitVulkan(NULL) failed");
    }
#ifdef _WIN32
    else if (memoryTypeIn == amf::AMF_MEMORY_DX9)
    {
        res = pContext->InitDX9(NULL); // can be DX9 or DX9Ex device
        AMF_RETURN_IF_FAILED(res, L"InitDX9(NULL) failed");
    }
    else if (memoryTypeIn == amf::AMF_MEMORY_DX11)
    {
        res = pContext->InitDX11(NULL); // can be DX11 device
        AMF_RETURN_IF_FAILED(res, L"InitDX11(NULL) failed");
    }
    else if (memoryTypeIn == amf::AMF_MEMORY_DX12)
    {
        res = amf::AMFContext2Ptr(pContext)->InitDX12(NULL); // can be DX12 device
        AMF_RETURN_IF_FAILED(res, L"InitDX12(NULL) failed");
    }
#endif

    if (useDefaultFrames == true
        && (memoryTypeIn == amf::AMF_MEMORY_VULKAN
#ifdef _WIN32
        || memoryTypeIn == amf::AMF_MEMORY_DX11
        || memoryTypeIn == amf::AMF_MEMORY_DX12
#endif
        ))
    {
        PrepareFillFromHost(pContext, memoryTypeIn, formatIn, widthIn, heightIn, false);
    }

    res = g_AMFFactory.GetFactory()->CreateComponent(pContext, AMFFRC, &pFRC);
    CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << AMFFRC << L") failed");

    amf::AMFDataStreamPtr outfile = NULL;
    if (skipOutputDump == false)
    {
        res = amf::AMFDataStream::OpenDataStream(outfileName, amf::AMFSO_WRITE, amf::AMFFS_SHARE_READ, &outfile);
        AMF_ASSERT_OK(res, L"Failed to open file %s", outfileName);
    }

    // initialize
    if (pContext != nullptr)
    {
        if (useDefaultFrames == false)
        {
	        pFileReader = RawStreamReaderPtr(new RawStreamReader());
            res = pFileReader->Init(&params, pContext);
            AMF_RETURN_IF_FAILED(res, L"pFileReader->Init() failed with err=%d");
        }

        pFRC->SetProperty(AMF_FRC_ENGINE_TYPE, frcEngineType);

        pFRC->SetProperty(AMF_FRC_MODE, frcMode);

        pFRC->SetProperty(AMF_FRC_ENABLE_FALLBACK, frcFallbackFlag);

        pFRC->SetProperty(AMF_FRC_INDICATOR, frcIndicator);

        pFRC->SetProperty(AMF_FRC_PROFILE, frcProfile);

        pFRC->SetProperty(AMF_FRC_MV_SEARCH_MODE, frcMVSearchMode);

        res = pFRC->Init(formatIn, widthIn, heightIn);
        AMF_RETURN_IF_FAILED(res, L"pFRC->Init() failed with err=%d");

        amf_int32 submitted = 0;
        while (useDefaultFrames == false || submitted < frameCount)
        {
            if (pSurfaceIn == NULL)
            {
                if (useDefaultFrames == false)
                {
                    res = ReadSurface(pFileReader, &pSurfaceIn, memoryTypeIn);
                    if (res == AMF_EOF)
                    {
                        // drain output queue; input queue can be full
                        res = pFRC->Drain();
                        break;
                    }
                    else if (res != AMF_OK)
                    {
                        break;
                    }
                }
                else
                {
                    res = pContext->AllocSurface(memoryTypeIn, formatIn, widthIn, heightIn, &pSurfaceIn);
                    AMF_RETURN_IF_FAILED(res, L"AllocSurface() failed");

                    if (memoryTypeIn == amf::AMF_MEMORY_VULKAN)
                    {
                        FillSurfaceVulkan(pContext, pSurfaceIn, false);
                    }
#ifdef _WIN32
                    else if (memoryTypeIn == amf::AMF_MEMORY_DX9)
                    {
                        FillSurfaceDX9(pContext, pSurfaceIn, false);
                    }
                    else if (memoryTypeIn == amf::AMF_MEMORY_DX11)
                    {
                        FillSurfaceDX11(pContext, pSurfaceIn, false);
                    }
                    else if (memoryTypeIn == amf::AMF_MEMORY_DX12)
                    {
                        FillSurfaceDX12(pContext, pSurfaceIn, false);
                    }
#endif
                }
            }

            amf_pts start_time = amf_high_precision_clock();
            pSurfaceIn->SetProperty(START_TIME_PROPERTY, start_time);

            res = pFRC->SubmitInput(pSurfaceIn);
            if (res == AMF_NEED_MORE_INPUT || res == AMF_INPUT_FULL)
            {
                // do nothing
            }
            else
            {
                AMF_RETURN_IF_FAILED(res, L"SubmitInput() failed");
                pSurfaceIn = NULL;
                submitted++;
            }

            while (true)
            {
                amf::AMFSurfacePtr pSurfaceOut;
                res = pFRC->QueryOutput((amf::AMFData**)&pSurfaceOut);
                if (res == AMF_EOF)
                {
                    break; // Drain complete
                }
                if ((res != AMF_OK) && (res != AMF_REPEAT))
                {
                    // trace possible error message
                    break; // Drain complete
                }
                if (pSurfaceOut != NULL)
                {
                    if (skipOutputDump == false && outfile != NULL)
                    {
                        dumpSurface(outfile, pSurfaceOut);
                    }
                }
                else
                {
                    break;
                }
            }
        }

        pColor1 = NULL;
        pColor2 = NULL;
        pColor3 = NULL;
        pColor4 = NULL;

        // cleanup in this order
        pSurfaceIn = NULL;
        pFileReader = NULL; // pFileReader->Terminate() is private, it is automatically called inside its destructor
        pFRC->Terminate();
        pFRC = NULL;
        pContext->Terminate();
        pContext = NULL; // context is the last

        g_AMFFactory.Terminate();
    }

    return AMF_OK;
}

#ifdef _WIN32
int _tmain(int /*argc*/, _TCHAR* /*argv*/[])
#else
int main(int argc, char* argv[])
#endif
{
    ParametersStorage params;
    RegisterParams(&params);

#if defined(_WIN32)
    if (!parseCmdLineParameters(&params))
#else
    if (!parseCmdLineParameters(&params, argc, argv))
#endif        
    {
        return -1;
    }
    
    amf_bool useDefaultFrames = false;
    amf_wstring tempin;
    if (params.GetParamWString(PARAM_NAME_INPUT, tempin) == AMF_OK)
    {
        infileName = tempin.c_str();
    }
    else
    {
        wprintf(L"No input file, using default surfaces\n");
        useDefaultFrames = true;
    }

    amf_bool skipOutputDump = false;
    amf_wstring tempout;
    if (params.GetParamWString(PARAM_NAME_OUTPUT, tempout) == AMF_OK)
    {
        outfileName = tempout.c_str();
    }
    else
    {
        wprintf(L"No output file, skipping output dump\n");
        skipOutputDump = true;
    }

    params.GetParam(PARAM_NAME_INPUT_WIDTH, widthIn);
    params.GetParam(PARAM_NAME_INPUT_HEIGHT, heightIn);
    amf_int64 surfaceformat;
    if (params.GetParam(PARAM_NAME_INPUT_FORMAT, surfaceformat) == AMF_OK)
    {
        formatIn = (amf::AMF_SURFACE_FORMAT)surfaceformat;
    }

    amf_int64 tempMemoryType;
    if (params.GetParam(PARAM_NAME_MEMORYTYPE, tempMemoryType) == AMF_OK)
    {
        memoryTypeIn = (amf::AMF_MEMORY_TYPE)tempMemoryType;
    }

    params.GetParam(PARAM_NAME_FRC_ENGINE, frcEngineType);
    params.GetParam(PARAM_NAME_FRC_MODE, frcMode);
    params.GetParam(PARAM_NAME_FRC_ENABLE_FALLBACK, frcFallbackFlag);
    params.GetParam(PARAM_NAME_FRC_INDICATOR, frcIndicator);
    params.GetParam(PARAM_NAME_FRC_PROFILE, frcProfile);
    params.GetParam(PARAM_NAME_FRC_MV_SEARCH_MODE, frcMVSearchMode);

    AMF_RESULT result = SimpleFRC(params, useDefaultFrames, skipOutputDump);
    if (result != AMF_OK)
    {
        wprintf(L"%s FRC failed\n", infileName);
        return -1;
    }

    return 0;
}
