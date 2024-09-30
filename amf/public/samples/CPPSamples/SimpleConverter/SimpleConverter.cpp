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
// this sample converts frames from BGRA to NV12 and scales them down using AMF Video Converter and writes the frames into raw file
#include <stdio.h>

#ifdef WIN32
#include <tchar.h>
#include <d3d9.h>
#include <d3d11.h>
#endif

#include "public/common/AMFSTL.h"
#include "public/common/AMFFactory.h"
#include "public/include/components/VideoConverter.h"
#include "public/include/core/Dump.h"
#include "public/common/Thread.h"
#include "public/samples/CPPSamples/common/SurfaceUtils.h"
#include "public/samples/CPPSamples/common/PollingThread.h"
#include "public/common/TraceAdapter.h"
#include <fstream>
#include <iostream>

#define AMF_FACILITY L"SimpleConverter"


#define COMMON_MEMORY_TYPE amf::AMF_MEMORY_DX11

// On Win7 AMF Encoder can work on DX9 only
// The next line can be used to demo DX11 input and DX9 output from converter
#ifdef WIN32
static amf::AMF_MEMORY_TYPE memoryTypeIn = COMMON_MEMORY_TYPE;
static amf::AMF_MEMORY_TYPE memoryTypeOut = COMMON_MEMORY_TYPE;
static amf::AMF_MEMORY_TYPE memoryTypeCompute = COMMON_MEMORY_TYPE;
#else
static amf::AMF_MEMORY_TYPE memoryTypeIn = amf::AMF_MEMORY_VULKAN;
static amf::AMF_MEMORY_TYPE memoryTypeOut = amf::AMF_MEMORY_VULKAN;
static amf::AMF_MEMORY_TYPE memoryTypeCompute = amf::AMF_MEMORY_VULKAN;
#endif
static amf::AMF_SURFACE_FORMAT formatIn   = amf::AMF_SURFACE_BGRA;
static amf_int32 widthIn                  = 1920;
static amf_int32 heightIn                 = 1080;
static amf_int32 frameCount               = 100;

static const wchar_t *fileNameOut           = L"./output_%dx%d.nv12";
static amf::AMF_SURFACE_FORMAT formatOut  = amf::AMF_SURFACE_NV12;
//static amf_int32 widthOut                 = 1280;
//static amf_int32 heightOut                = 720;
static amf_int32 widthOut                 = 1920;
static amf_int32 heightOut                = 1080;

static void FillSurface(amf::AMFContext *context, amf::AMFSurface *surface, amf_int32 i);

class ConverterPollingThread : public PollingThread
{
public:
    ConverterPollingThread(amf::AMFComponent *pConverter, const wchar_t *pFileName)
        : PollingThread(NULL, pConverter, pFileName, true), m_StartTime(0)
    {}
protected:
    bool Init() override
    {
        m_StartTime = amf_high_precision_clock();
        return PollingThread::Init();
    }
    void ProcessData(amf::AMFData* pData) override
    {
        AMF_RESULT res = AMF_OK;
        amf_pts poll_time = amf_high_precision_clock();
        if (m_LatencyTime == 0)
        {
            m_LatencyTime = poll_time - m_StartTime;
        }

        // this operation is slow nneed to remove it from stat
        res = pData->Convert(amf::AMF_MEMORY_HOST); // convert to system memory

        amf_pts convert_time = amf_high_precision_clock();
        m_ComponentDuration += amf_high_precision_clock() - poll_time;

        amf::AMFSurfacePtr pSurface(pData); // query for surface interface

        res = WritePlane(pSurface->GetPlane(amf::AMF_PLANE_Y), m_pFile); // get y-plane pixels
        res = WritePlane(pSurface->GetPlane(amf::AMF_PLANE_UV), m_pFile); // get uv-plane pixels

        m_WriteDuration += amf_high_precision_clock() - convert_time;
    }
    void PrintResults() override
    {
        amf_pts exec_duration = amf_high_precision_clock() - m_StartTime;
        PrintTimes("convert", frameCount);
        printf("process per frame = %.4fms\nexecute per frame = %.4fms\n",
            double(exec_duration) / AMF_MILLISECOND / frameCount,
            double(exec_duration - m_WriteDuration - m_ComponentDuration) / AMF_MILLISECOND / frameCount);
    }
    amf_pts m_StartTime;
};

#ifdef _WIN32
int _tmain(int /* argc */, _TCHAR* /* argv */[])
#else
int main(int /* argc */, char* /* argv */[])
#endif
{
    AMF_RESULT res = AMF_OK; // error checking can be added later
    res = g_AMFFactory.Init();
    if(res != AMF_OK)
    {
        std::cout << "AMF Failed to initialize";
        return 1;
    }

    amf_increase_timer_precision();

    amf::AMFTraceSetGlobalLevel(AMF_TRACE_INFO);
    amf::AMFTraceSetWriterLevel(AMF_TRACE_WRITER_DEBUG_OUTPUT, AMF_TRACE_INFO);

    // open output file with frame size in file name
    wchar_t fileNameOutWidthSize[2000];
    swprintf(fileNameOutWidthSize, amf_countof(fileNameOutWidthSize), fileNameOut, widthOut, heightOut);

    // initialize AMF
    amf::AMFContextPtr context;
    amf::AMFComponentPtr converter;

    // context
    res = g_AMFFactory.GetFactory()->CreateContext(&context);

    // On Win7 AMF Encoder can work on DX9 only we initialize DX11 for input, DX9 fo output and OpenCL for CSC & Scale
#ifdef _WIN32
    if(memoryTypeIn == amf::AMF_MEMORY_DX9 || memoryTypeOut == amf::AMF_MEMORY_DX9 || memoryTypeCompute == amf::AMF_MEMORY_DX9)
    {
        res = context->InitDX9(NULL); // can be DX9 or DX9Ex device
    }
    if(memoryTypeIn == amf::AMF_MEMORY_DX11 || memoryTypeOut == amf::AMF_MEMORY_DX11 || memoryTypeCompute == amf::AMF_MEMORY_DX11)
    {
        res = context->InitDX11(NULL); // can be DX11 device
    }
    if (memoryTypeIn == amf::AMF_MEMORY_DX12 || memoryTypeOut == amf::AMF_MEMORY_DX12 || memoryTypeCompute == amf::AMF_MEMORY_DX12)
    {
        res = amf::AMFContext2Ptr(context)->InitDX12(NULL); // can be DX12 device
    }
#endif
    if(memoryTypeIn == amf::AMF_MEMORY_VULKAN || memoryTypeOut == amf::AMF_MEMORY_VULKAN || memoryTypeCompute == amf::AMF_MEMORY_VULKAN)
    {
        res = amf::AMFContext1Ptr(context)->InitVulkan(NULL);
    }
    if(memoryTypeIn != memoryTypeOut )
    {
        res = context->InitOpenCL(NULL); // forces use of OpenCL for converter - only when memory types are different
    }
    // component: converter
    res = g_AMFFactory.GetFactory()->CreateComponent(context, AMFVideoConverter, &converter);
    res = converter->SetProperty(AMF_VIDEO_CONVERTER_MEMORY_TYPE, memoryTypeOut);
    res = converter->SetProperty(AMF_VIDEO_CONVERTER_COMPUTE_DEVICE, memoryTypeCompute);
    res = converter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_FORMAT, formatOut);
    res = converter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_SIZE, ::AMFConstructSize(widthOut, heightOut));

    res = converter->Init(formatIn, widthIn, heightIn);

    ConverterPollingThread thread(converter, fileNameOutWidthSize);
    thread.Start();

    // create input surfaces
    amf::AMFSurfacePtr surfaceIn1;
    res = context->AllocSurface(memoryTypeIn, formatIn, widthIn, heightIn, &surfaceIn1); // surfaces are cached in AMF
    FillSurface(context, surfaceIn1, 0);

    amf::AMFSurfacePtr surfaceIn2;
    res = context->AllocSurface(memoryTypeIn, formatIn, widthIn, heightIn, &surfaceIn2); // surfaces are cached in AMF
    FillSurface(context, surfaceIn2, 1);

    // convert some frames
    for(amf_int32 i = 0; i < frameCount; i++)
    {
        // convert to NV12 and Scale
        while(true)
        {
            res = converter->SubmitInput((i%2 == 0) ? surfaceIn1 : surfaceIn2);
            if(res == AMF_NEED_MORE_INPUT)
            {
                // do nothing
            }
			else if (res == AMF_INPUT_FULL || res == AMF_DECODER_NO_FREE_SURFACES)
			{ // queue is full; sleep, try to get ready surfaces in polling thread and repeat submissions
				amf_sleep(1);
			}
            else
            {
                break;
            }
        }
    }
    // drain it
    res = converter->Drain();

    // Need to request stop before waiting for stop
    if (thread.RequestStop() == false)
    {
        AMFTraceError(AMF_FACILITY, L"thread.RequestStop() Failed");
    }

    if (thread.WaitForStop() == false)
    {
        AMFTraceError(AMF_FACILITY, L"thread.WaitForStop() Failed");
    }
    // cleanup in this order
    converter->Terminate();
    converter = NULL;

    surfaceIn1 = nullptr;
    surfaceIn2 = nullptr;

    context->Terminate();
    context = NULL; // context is the last

    g_AMFFactory.Terminate();
    return 0;
}

static void FillSurface(amf::AMFContext *context, amf::AMFSurface *surface, amf_int32 i)
{
#ifdef _WIN32
    // fill surface with something something useful. We fill with color
    if(surface->GetMemoryType() == amf::AMF_MEMORY_DX9)
    {
        HRESULT hr = S_OK;

        // get native DX objects
        IDirect3DDevice9 *deviceDX9 = (IDirect3DDevice9 *)context->GetDX9Device(); // no reference counting - do not Release()
        IDirect3DSurface9* surfaceDX9 = (IDirect3DSurface9*)surface->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()
        D3DCOLOR color1 = D3DCOLOR_RGBA (255, 0, 255, 255);
        D3DCOLOR color2 = D3DCOLOR_RGBA (0, 255, 0, 255);
        hr = deviceDX9->ColorFill(surfaceDX9, NULL, (i % 2) ? color1 : color2); // alternate colors
    }
    else
#endif
    if (surface->GetMemoryType() == amf::AMF_MEMORY_HOST)
    {
        AMFColor color = (i % 2) ? AMFConstructColor(255, 0, 0, 255) : AMFConstructColor(0, 255, 0, 255);
        FillBitmapWithColor((amf_uint8*)surface->GetPlaneAt(0)->GetNative(), surface->GetPlaneAt(0)->GetWidth(), surface->GetPlaneAt(0)->GetHeight(), surface->GetPlaneAt(0)->GetHPitch(), color);
    }
    else
    {
        amf::AMFComputePtr compute;
        context->GetCompute(surface->GetMemoryType(), &compute);

        amf_uint8 color1[4] ={255, 0, 0, 255};
        amf_uint8  color2[4] = {0, 255, 0, 255};
        amf_uint8  *color = (i % 2) ? color1 : color2;
        amf::AMFPlane *plane = surface->GetPlaneAt(0);
        amf_size region[3] = {(amf_size)plane->GetWidth(), (amf_size)plane->GetHeight(), (amf_size)1};
        amf_size origin[3] = {0, 0 , 0};
        compute->FillPlane(plane, origin, region, color);
    }
}
