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
// this sample converts frames from BGRA to NV12 and scales them down using AMF Video Converter and writes the frames into raw file
#include <stdio.h>
#include <tchar.h>
#include <d3d9.h>
#include <d3d11.h>
#include "public/common/AMFFactory.h"
#include "public/include/components/VideoConverter.h"
#include "public/common/Thread.h"

// On Win7 AMF Encoder can work on DX9 only
// The next line can be used to demo DX11 input and DX9 output from converter
static amf::AMF_MEMORY_TYPE memoryTypeIn = amf::AMF_MEMORY_DX11; 
//static amf::AMF_MEMORY_TYPE memoryTypeIn  = amf::AMF_MEMORY_DX9;
static amf::AMF_SURFACE_FORMAT formatIn   = amf::AMF_SURFACE_BGRA;
static amf_int32 widthIn                  = 1920;
static amf_int32 heightIn                 = 1080;
static amf_int32 frameCount               = 100;

static wchar_t *fileNameOut               = L"./output_%dx%d.nv12";
static amf::AMF_MEMORY_TYPE memoryTypeOut = amf::AMF_MEMORY_DX11;
static amf::AMF_SURFACE_FORMAT formatOut  = amf::AMF_SURFACE_NV12;
static amf_int32 widthOut                 = 1280;
static amf_int32 heightOut                = 720;

static void WritePlane(amf::AMFPlane *plane, FILE *f);
static void FillSurface(amf::AMFContext *context, amf::AMFSurface *surface, amf_int32 i);

#define MILLISEC_TIME     10000

class PollingThread : public amf::AMFThread
{
protected:
    amf::AMFComponentPtr    m_pConverter;
    FILE                    *m_pFile;
public:
    PollingThread(amf::AMFComponent *converter, const wchar_t *pFileName) : m_pConverter(converter), m_pFile(NULL)
    {
        m_pFile = _wfopen(pFileName, L"wb");
    }
    ~PollingThread()
    {
        if(m_pFile)
        {
            fclose(m_pFile);
        }
    }
    virtual void Run()
    {
        RequestStop();

        amf_pts start_time = amf_high_precision_clock();
        amf_pts latency_time = 0;
        amf_pts convert_duration = 0;
        amf_pts write_duration = 0;

        AMF_RESULT res = AMF_OK; // error checking can be added later
        amf_pts last_poll_time = 0;
        while(true)
        {
            amf::AMFDataPtr data;
            res = m_pConverter->QueryOutput(&data);
            if(res == AMF_EOF)
            {
                break; // Drain complete
            }
            if(data != NULL)
            {
                amf_pts poll_time = amf_high_precision_clock();
                if(latency_time == 0)
                {
                    latency_time = poll_time - start_time;
                }
               
                // this operation is slow nneed to remove it from stat
                res = data->Convert(amf::AMF_MEMORY_HOST); // convert to system memory

                amf_pts convert_time = amf_high_precision_clock();
                convert_duration += amf_high_precision_clock() - poll_time;

                amf::AMFSurfacePtr surface(data); // query for surface interface
        
                WritePlane(surface->GetPlane(amf::AMF_PLANE_Y), m_pFile); // get y-plane pixels
                WritePlane(surface->GetPlane(amf::AMF_PLANE_UV), m_pFile); // get uv-plane pixels
                
                write_duration += amf_high_precision_clock() - convert_time;
            }
            else
            {
                amf_sleep(1);
            }

        }
        amf_pts exec_duration = amf_high_precision_clock()- start_time;
        printf("latency           = %.4fms\nprocess per frame = %.4fms\nconvert per frame = %.4fms\nwrite per frame   = %.4fms\nexecute per frame = %.4fms\n", 
            double(latency_time)/MILLISEC_TIME,
            double(exec_duration)/MILLISEC_TIME/frameCount, 
            double(convert_duration )/MILLISEC_TIME/frameCount, 
            double(write_duration)/MILLISEC_TIME/frameCount, 
            double(exec_duration - write_duration - convert_duration)/MILLISEC_TIME/frameCount);

        m_pConverter = NULL;

    }
};

int _tmain(int argc, _TCHAR* argv[])
{
    AMF_RESULT res = AMF_OK; // error checking can be added later
    res = g_AMFFactory.Init();
    if(res != AMF_OK)
    {
        wprintf(L"AMF Failed to initialize");
        return 1;
    }

    amf_increase_timer_precision();

    // open output file with frame size in file name
    wchar_t fileNameOutWidthSize[2000];
    _swprintf(fileNameOutWidthSize, fileNameOut, widthOut, heightOut);

    // initialize AMF
    amf::AMFContextPtr context;
    amf::AMFComponentPtr converter;

    // context
    res = g_AMFFactory.GetFactory()->CreateContext(&context);

    // On Win7 AMF Encoder can work on DX9 only we initialize DX11 for input, DX9 fo output and OpenCL for CSC & Scale
    if(memoryTypeIn == amf::AMF_MEMORY_DX9 || memoryTypeOut == amf::AMF_MEMORY_DX9)
    {
        res = context->InitDX9(NULL); // can be DX9 or DX9Ex device
    }
    if(memoryTypeIn == amf::AMF_MEMORY_DX11 || memoryTypeOut == amf::AMF_MEMORY_DX11)
    {
        res = context->InitDX11(NULL); // can be DX11 device
    }
    if(memoryTypeIn != memoryTypeOut )
    {
        res = context->InitOpenCL(NULL); // forces use of OpenCL for converter - only when memory types are different
    }
    // component: converter
    res = g_AMFFactory.GetFactory()->CreateComponent(context, AMFVideoConverter, &converter);
    res = converter->SetProperty(AMF_VIDEO_CONVERTER_MEMORY_TYPE, memoryTypeOut);
    res = converter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_FORMAT, formatOut);
    res = converter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_SIZE, ::AMFConstructSize(widthOut, heightOut));
    res = converter->Init(formatIn, widthIn, heightIn);

    PollingThread thread(converter, fileNameOutWidthSize);
    thread.Start();

    // convert some frames
    for(amf_int32 i = 0; i < frameCount; i++)
    {
        // create input surface
        amf::AMFSurfacePtr surfaceIn;
        res = context->AllocSurface(memoryTypeIn, formatIn, widthIn, heightIn, &surfaceIn); // surfaces are cached in AMF
        FillSurface(context, surfaceIn, i);
        // convert to NV12 and Scale
        while(true)
        {
            res = converter->SubmitInput(surfaceIn);
            if(res == AMF_INPUT_FULL)
            {
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
    thread.WaitForStop();

    // cleanup in this order
    converter->Terminate();
    converter = NULL;
    context->Terminate();
    context = NULL; // context is the last

    g_AMFFactory.Terminate();
    return 0;
}

static void WritePlane(amf::AMFPlane *plane, FILE *f)
{
    // write NV12 surface removing offsets and alignments
    amf_uint8 *data     = reinterpret_cast<amf_uint8*>(plane->GetNative());
    amf_int32 offsetX   = plane->GetOffsetX();
    amf_int32 offsetY   = plane->GetOffsetY();
    amf_int32 pixelSize = plane->GetPixelSizeInBytes();
    amf_int32 height    = plane->GetHeight();
    amf_int32 width     = plane->GetWidth();
    amf_int32 pitchH    = plane->GetHPitch();

    for( amf_int32 y = 0; y < height; y++)
    {
        amf_uint8 *line = data + (y + offsetY) * pitchH;
        fwrite(line + offsetX * pixelSize, pixelSize, width, f);
    }
}
static void FillSurface(amf::AMFContext *context, amf::AMFSurface *surface, amf_int32 i)
{
    HRESULT hr = S_OK;
    // fill surface with something something useful. We fill with color 
    if(surface->GetMemoryType() == amf::AMF_MEMORY_DX9)
    {
        // get native DX objects
        IDirect3DDevice9 *deviceDX9 = (IDirect3DDevice9 *)context->GetDX9Device(); // no reference counting - do not Release()
        IDirect3DSurface9* surfaceDX9 = (IDirect3DSurface9*)surface->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()
        D3DCOLOR color1 = D3DCOLOR_RGBA (255, 0, 255, 255);
        D3DCOLOR color2 = D3DCOLOR_RGBA (255, 255, 0, 255);
        hr = deviceDX9->ColorFill(surfaceDX9, NULL, (i % 2) ? color1 : color2); // alternate colors
    }
    else if(surface->GetMemoryType() == amf::AMF_MEMORY_DX11)
    {
        ID3D11Device *deviceDX11 = (ID3D11Device*)context->GetDX11Device(); // no reference counting - do not Release()
        ID3D11Texture2D *textureDX11 = (ID3D11Texture2D*)surface->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()
        ID3D11DeviceContext *contextDX11 = NULL;
        ID3D11RenderTargetView *viewDX11 = NULL;
        deviceDX11->GetImmediateContext(&contextDX11);
        hr = deviceDX11->CreateRenderTargetView(textureDX11, NULL, &viewDX11);
        float color1[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
        float color2[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
        contextDX11->ClearRenderTargetView( viewDX11, (i % 2) ? color1 : color2 );
        contextDX11->Flush();
        // release temp objects
        viewDX11->Release();
        contextDX11->Release();
    }
}
