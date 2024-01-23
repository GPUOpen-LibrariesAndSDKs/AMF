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

// this sample encodes NV12 frames using AMF Encoder and writes them to H.264,H.265, or AV1 elmentary stream

#include <stdio.h>
#ifdef _WIN32
#include <tchar.h>
#include <d3d9.h>
#include <d3d11.h>
#endif
#include "public/common/AMFFactory.h"
#include "public/include/components/VideoEncoderVCE.h"
#include "public/include/components/VideoEncoderHEVC.h"
#include "public/include/components/VideoEncoderAV1.h"
#include "public/common/Thread.h"
#include "public/common/AMFSTL.h"
#include "public/common/TraceAdapter.h"
#include "public/samples/CPPSamples/common/SurfaceGenerator.h"
#include "public/samples/CPPSamples/common/MiscHelpers.h"
#include "public/samples/CPPSamples/common/PollingThread.h"
#include <fstream>

#define AMF_FACILITY L"SimpleEncoder"

static const bool bEnable4K = false;
static const bool bEnable10bit = false;
static const bool bMaximumSpeed = true; // encoding speed preset

static const bool bEnableEfc = false; // color conversion inside encoder component. Will use GFX or HW if available
// Encoder Format Conversion (EFC) available for selected input formats
static const amf::AMF_SURFACE_FORMAT efcSurfaceFormat[] = {
    amf::AMF_SURFACE_RGBA,
    amf::AMF_SURFACE_BGRA,
    amf::AMF_SURFACE_R10G10B10A2,
    amf::AMF_SURFACE_RGBA_F16 };

static const int codecIndex = 0; // set to 0 for AVC, 1 for HEVC, 2 for AV1
static_assert(!bEnable10bit || codecIndex != 0, "HEVC or AV1 required for 10 bit");
static const wchar_t* pCodecNames[] = { AMFVideoEncoderVCE_AVC, AMFVideoEncoder_HEVC, AMFVideoEncoder_AV1 };
static const wchar_t* fileNames[] = { L"./output.h264", L"./output.h265", L"./output.av1" };

#ifdef _WIN32
//static amf::AMF_MEMORY_TYPE memoryTypeIn  = amf::AMF_MEMORY_DX11;
//static amf::AMF_MEMORY_TYPE memoryTypeIn = amf::AMF_MEMORY_DX12;
static amf::AMF_MEMORY_TYPE memoryTypeIn = amf::AMF_MEMORY_VULKAN;
#else
static amf::AMF_MEMORY_TYPE memoryTypeIn  = amf::AMF_MEMORY_VULKAN;
#endif

static amf::AMF_SURFACE_FORMAT formatIn   = (bEnableEfc ? efcSurfaceFormat[0] : amf::AMF_SURFACE_NV12);


static amf_int32 widthIn                  = (bEnable4K ? 1920 * 2 : 1920);
static amf_int32 heightIn                 = (bEnable4K ? 1080 * 2 : 1080);
static amf_int32 frameRateIn              = 30;
static amf_int64 bitRateIn                = 5000000L; // in bits, 5MBit
amf_int32 rectSize                        = 50;
static amf_int32 frameCount               = 500;

amf::AMFSurfacePtr pColor1;
amf::AMFSurfacePtr pColor2;

amf::AMFSurfacePtr pColor3;
amf::AMFSurfacePtr pColor4;


class EncPollingThread : public PollingThread
{
public:
    EncPollingThread(amf::AMFContext* pContext, amf::AMFComponent* pEncoder, const wchar_t* pFileName);
protected:
    void ProcessData(amf::AMFData* pData) override;
    void PrintResults() override;
};

AMF_RESULT simpleEncode(const wchar_t* pCodec, const wchar_t* pFileNameOut);

#ifdef _WIN32
int _tmain(int /* argc */, _TCHAR* /* argv */[])
#else
int main(int /* argc */, char* /* argv */ [])
#endif
{
    AMF_RESULT result = simpleEncode(pCodecNames[codecIndex], fileNames[codecIndex]);
    if (result != AMF_OK)
    {
        wprintf(L"%s encode failed", pCodecNames[codecIndex]);
    }
    return 0;
}

// creates encoder using pCodec component id, then outputs encoded elementary stream to pFileNameOut
AMF_RESULT simpleEncode(const wchar_t* pCodec, const wchar_t* pFileNameOut) {
    AMF_RESULT res = AMF_OK; // error checking can be added later
    res = g_AMFFactory.Init();
    if (res != AMF_OK)
    {
        wprintf(L"AMF Failed to initialize");
        return res;
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
        PrepareFillFromHost(context, memoryTypeIn, formatIn, widthIn, heightIn, false);
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
        PrepareFillFromHost(context, memoryTypeIn, formatIn, widthIn, heightIn, false);
    }
    else if (memoryTypeIn == amf::AMF_MEMORY_DX12)
    {
        res = amf::AMFContext2Ptr(context)->InitDX12(NULL); // can be DX12 device
        AMF_RETURN_IF_FAILED(res, L"InitDX12(NULL) failed");
        PrepareFillFromHost(context, memoryTypeIn, formatIn, widthIn, heightIn, false);
    }
#endif

    // component: encoder
    res = g_AMFFactory.GetFactory()->CreateComponent(context, pCodec, &encoder);
    AMF_RETURN_IF_FAILED(res, L"CreateComponent(%s) failed", pCodec);

    // set encoder params
    if (amf_wstring(pCodec) == amf_wstring(AMFVideoEncoderVCE_AVC))
    {
        res = SetEncoderDefaultsAVC(encoder, bitRateIn, frameRateIn, bMaximumSpeed, bEnable4K, false);
        AMF_RETURN_IF_FAILED(res, L"SetEncoderDefaultsAVC() failed");

        res = encoder->SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, ::AMFConstructSize(widthIn, heightIn));
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, %dx%d) failed", widthIn, heightIn);
    }
    else if (amf_wstring(pCodec) == amf_wstring(AMFVideoEncoder_HEVC))
    {
        res = SetEncoderDefaultsHEVC(encoder, bitRateIn, frameRateIn, bEnable10bit, bMaximumSpeed, bEnable4K, false);
        AMF_RETURN_IF_FAILED(res, L"SetEncoderDefaultsHEVC() failed");

        res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, ::AMFConstructSize(widthIn, heightIn));
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, %dx%d) failed", widthIn, heightIn);
    }
    else if (amf_wstring(pCodec) == amf_wstring(AMFVideoEncoder_AV1))
    {
        res = SetEncoderDefaultsAV1(encoder, bitRateIn, frameRateIn, bEnable10bit, bMaximumSpeed, bEnable4K, false);
        AMF_RETURN_IF_FAILED(res, L"SetEncoderDefaultsAV1() failed");

        res = encoder->SetProperty(AMF_VIDEO_ENCODER_AV1_FRAMESIZE, ::AMFConstructSize(widthIn, heightIn));
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_FRAMESIZE, %dx%d) failed", widthIn, heightIn);
    }

    res = encoder->Init(formatIn, widthIn, heightIn);
    AMF_RETURN_IF_FAILED(res, L"encoder->Init() failed");

    EncPollingThread thread(context, encoder, pFileNameOut);
    thread.Start();

    // encode some frames
    amf_int32 submitted = 0;
    while (submitted < frameCount)
    {
        if (surfaceIn == NULL)
        {
            surfaceIn = NULL;
            res = context->AllocSurface(memoryTypeIn, formatIn, widthIn, heightIn, &surfaceIn);
            AMF_RETURN_IF_FAILED(res, L"AllocSurface() failed");

            if (memoryTypeIn == amf::AMF_MEMORY_VULKAN)
            {
                FillSurfaceVulkan(context, surfaceIn, false);
            }
#ifdef _WIN32
            else if (memoryTypeIn == amf::AMF_MEMORY_DX9)
            {
                FillSurfaceDX9(context, surfaceIn, false);
            }
            else if (memoryTypeIn == amf::AMF_MEMORY_DX11)
            {
                FillSurfaceDX11(context, surfaceIn, false);
            }
            else if (memoryTypeIn == amf::AMF_MEMORY_DX12)
            {
                FillSurfaceDX12(context, surfaceIn, false);
            }
#endif
        }
        // encode
        amf_pts start_time = amf_high_precision_clock();
        surfaceIn->SetProperty(START_TIME_PROPERTY, start_time);

        res = encoder->SubmitInput(surfaceIn);
        if (res == AMF_NEED_MORE_INPUT) // handle full queue
        {
            // do nothing
        }
        else if (res == AMF_INPUT_FULL || res == AMF_DECODER_NO_FREE_SURFACES)
        {
            amf_sleep(1); // input queue is full: wait, poll and submit again
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

    pColor1 = NULL;
    pColor2 = NULL;

    // cleanup in this order
    surfaceIn = NULL;
    encoder->Terminate();
    encoder = NULL;
    context->Terminate();
    context = NULL; // context is the last

    g_AMFFactory.Terminate();
    return AMF_OK;
}

EncPollingThread::EncPollingThread(amf::AMFContext *pContext, amf::AMFComponent *pEncoder, const wchar_t *pFileName)
    : PollingThread(pContext, pEncoder, pFileName, true)
{}

void EncPollingThread::ProcessData(amf::AMFData* pData)
{
    AdjustTimes(pData);
    amf::AMFBufferPtr pBuffer(pData); // query for buffer interface
    if (m_pFile != NULL)
    {
        m_pFile->Write(pBuffer->GetNative(), pBuffer->GetSize(), NULL);
    }
    m_WriteDuration += amf_high_precision_clock() - m_LastPollTime;
}

void EncPollingThread::PrintResults()
{
    PrintTimes("encode ", frameCount);
}
