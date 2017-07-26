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

// this sample encodes NV12 frames using AMF Encoder and writes them to H.264 elmentary stream 

#include <stdio.h>
#include <tchar.h>
#include <d3d9.h>
#include <d3d11.h>
#if defined(_WIN32)
#include <process.h>
#else
#include <pthread.h>
#endif

#include "../common/AMFFactoryC.h"
#include "public/include/components/VideoEncoderVCE.h"
#include "public/include/components/VideoEncoderHEVC.h"
#include "../common/ThreadC.h"
#include "../common/TraceAdapterC.h"

#define AMF_FACILITY L"SimpleEncoderC"

static wchar_t *pCodec = AMFVideoEncoderVCE_AVC;
//static wchar_t *pCodec = AMFVideoEncoder_HEVC;

 //#define ENABLE_4K

static AMF_MEMORY_TYPE memoryTypeIn  = AMF_MEMORY_DX11;
static AMF_SURFACE_FORMAT formatIn   = AMF_SURFACE_NV12;

#if defined(ENABLE_4K)
static amf_int32 widthIn                  = 1920*2;
static amf_int32 heightIn                 = 1080*2;
#else
static amf_int32 widthIn                  = 1920;
static amf_int32 heightIn                 = 1080;
#endif
static amf_int32 frameRateIn              = 30;
static amf_int64 bitRateIn                = 5000000L; // in bits, 5MBit
static amf_int32 rectSize                 = 50;
static amf_int32 frameCount               = 500;
static amf_bool bMaximumSpeed = true;

#define START_TIME_PROPERTY L"StartTimeProperty" // custom property ID to store submission time in a frame - all custom properties are copied from input to output

static wchar_t *fileNameOut = L"./output.h264";

#define MILLISEC_TIME     10000

static amf_int32 xPos = 0;
static amf_int32 yPos = 0;
//-------------------------------------------------------------------------------------------------
static void FillSurfaceDX9(AMFContext *context, AMFSurface *surface);
static void FillSurfaceDX11(AMFContext *context, AMFSurface *surface);
static void PrepareFillDX11(AMFContext *context);

AMFSurface* pColor1;
AMFSurface* pColor2;

//-------------------------------------------------------------------------------------------------
typedef struct PollingThread
{
    AMFContext*      m_pContext;
    AMFComponent*    m_pEncoder;
    FILE                    *m_pFile;
    uintptr_t               m_pThread;
} PollingThread;
PollingThread s_thread;
amf_bool PollingThread_Run(PollingThread *pThread, AMFContext *context, AMFComponent *encoder, const wchar_t *pFileName);
amf_bool PollingThread_Stop(PollingThread *pThread);

//-------------------------------------------------------------------------------------------------

int _tmain(int argc, _TCHAR* argv[])
{
    AMF_RESULT res = AMF_OK; // error checking can be added later
    AMFContext* context = NULL;
    AMFComponent* encoder = NULL;
    AMFSurface* surfaceIn = NULL;
    AMFVariantStruct var;

    res = AMFFactoryHelper_Init();
    if(res != AMF_OK)
    {
        wprintf(L"AMF Failed to initialize");
        return 1;
    }

    amf_increase_timer_precision();
    
    AMFTraceEnableWriter(AMF_TRACE_WRITER_CONSOLE, true);
    AMFTraceEnableWriter(AMF_TRACE_WRITER_DEBUG_OUTPUT, true);

    // initialize AMF

    // context
    res = AMFFactoryHelper_GetFactory()->pVtbl->CreateContext(AMFFactoryHelper_GetFactory(), &context);
    AMF_RETURN_IF_FAILED(res, L"CreateContext() failed");

    if(memoryTypeIn == AMF_MEMORY_DX9)
    {
        res = context->pVtbl->InitDX9(context, NULL); // can be DX9 or DX9Ex device
        AMF_RETURN_IF_FAILED(res, L"InitDX9(NULL) failed");
    }
    if(memoryTypeIn == AMF_MEMORY_DX11)
    {
		res = context->pVtbl->InitDX11(context, NULL, AMF_DX11_0); // can be DX11 device
        AMF_RETURN_IF_FAILED(res, L"InitDX11(NULL) failed");
		PrepareFillDX11(context);
    }

    // component: encoder
    res = AMFFactoryHelper_GetFactory()->pVtbl->CreateComponent(AMFFactoryHelper_GetFactory(), context, pCodec, &encoder);
    AMF_RETURN_IF_FAILED(res, L"CreateComponent(%s) failed", pCodec);
    AMFSize size = AMFConstructSize(widthIn, heightIn);
    AMFRate framerate = AMFConstructRate(frameRateIn, 1);

    if(wcscmp(pCodec, AMFVideoEncoderVCE_AVC) == 0)
    { 
        AMF_ASSIGN_PROPERTY_INT64(res, encoder, AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_TRANSCONDING);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_TRANSCONDING) failed");

        if(bMaximumSpeed)
        {
            AMF_ASSIGN_PROPERTY_INT64(res, encoder, AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0);
            // do not check error for AMF_VIDEO_ENCODER_B_PIC_PATTERN - can be not supported - check Capability Manager sample
            AMF_ASSIGN_PROPERTY_INT64(res, encoder, AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED) failed");
        }
        AMF_ASSIGN_PROPERTY_INT64(res, encoder, AMF_VIDEO_ENCODER_TARGET_BITRATE, bitRateIn);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, %" LPRId64 L") failed", bitRateIn);
        AMF_ASSIGN_PROPERTY_SIZE(res, encoder, AMF_VIDEO_ENCODER_FRAMESIZE, size);

        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, %dx%d) failed", widthIn, heightIn);
        AMF_ASSIGN_PROPERTY_RATE(res, encoder, AMF_VIDEO_ENCODER_FRAMERATE, framerate);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, %dx%d) failed", frameRateIn, 1);

#if defined(ENABLE_4K)
        AMF_ASSIGN_PROPERTY_INT64(res, encoder, AMF_VIDEO_ENCODER_PROFILE, AMF_VIDEO_ENCODER_PROFILE_HIGH);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_PROFILE, AMF_VIDEO_ENCODER_PROFILE_HIGH) failed");
        AMF_ASSIGN_PROPERTY_INT64(res, encoder, AMF_VIDEO_ENCODER_PROFILE_LEVEL, 51);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_PROFILE_LEVEL, 51)");
        AMF_ASSIGN_PROPERTY_INT64(res, encoder, AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0)");
#endif
    }
    else
    {
        AMF_ASSIGN_PROPERTY_INT64(res, encoder, AMF_VIDEO_ENCODER_HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCONDING);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCONDING)");

        if(bMaximumSpeed)
        {
            AMF_ASSIGN_PROPERTY_INT64(res, encoder, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED);
            AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED)");
        }
        AMF_ASSIGN_PROPERTY_INT64(res, encoder, AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, bitRateIn);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, %" LPRId64 L") failed", bitRateIn);
        AMF_ASSIGN_PROPERTY_SIZE(res, encoder, AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, size);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, %dx%d) failed", widthIn, heightIn);
        AMF_ASSIGN_PROPERTY_RATE(res, encoder, AMF_VIDEO_ENCODER_HEVC_FRAMERATE, framerate);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, %dx%d) failed", frameRateIn, 1);

#if defined(ENABLE_4K)
        AMF_ASSIGN_PROPERTY_INT64(res, encoder, AMF_VIDEO_ENCODER_HEVC_TIER, AMF_VIDEO_ENCODER_HEVC_TIER_HIGH);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_TIER, AMF_VIDEO_ENCODER_HEVC_TIER_HIGH) failed");
        AMF_ASSIGN_PROPERTY_INT64(res, encoder, AMF_VIDEO_ENCODER_HEVC_PROFILE_LEVEL, AMF_LEVEL_5_1);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_PROFILE_LEVEL, AMF_LEVEL_5_1) failed");
#endif
    }
    res = encoder->pVtbl->Init(encoder, formatIn, widthIn, heightIn);
    AMF_RETURN_IF_FAILED(res, L"encoder->Init() failed");

    PollingThread_Run(&s_thread, context, encoder, fileNameOut);

    // encode some frames
    amf_int32 submitted = 0;
    while(submitted < frameCount)
    {
        if(surfaceIn == NULL)
        {
            surfaceIn = NULL;
            res = context->pVtbl->AllocSurface(context, memoryTypeIn, formatIn, widthIn, heightIn, &surfaceIn);
            AMF_RETURN_IF_FAILED(res, L"AllocSurface() failed");
            
			if (memoryTypeIn == AMF_MEMORY_DX9)
			{
				FillSurfaceDX9(context, surfaceIn);
			}
			else
			{
				FillSurfaceDX11(context, surfaceIn);
			}
        }
        // encode
        amf_pts start_time = amf_high_precision_clock();
        AMFVariantAssignInt64(&var, start_time);
        surfaceIn->pVtbl->SetProperty(surfaceIn, START_TIME_PROPERTY, var);

        res = encoder->pVtbl->SubmitInput(encoder, (AMFData*)surfaceIn);
        if(res == AMF_INPUT_FULL) // handle full queue
        {
            amf_sleep(1); // input queue is full: wait, poll and submit again
        }
        else
        {
            surfaceIn->pVtbl->Release(surfaceIn);
            surfaceIn = NULL;
            AMF_RETURN_IF_FAILED(res, L"SubmitInput() failed");
            submitted++;
        }
    }
    // drain encoder; input queue can be full
    while(true)
    {
        res = encoder->pVtbl->Drain(encoder);
        if(res != AMF_INPUT_FULL) // handle full queue
        {
            break;
        }
        amf_sleep(1); // input queue is full: wait and try again
    }
    PollingThread_Stop(&s_thread);

	if (pColor1 != NULL)
	{
		pColor1->pVtbl->Release(pColor1);
	}
	if (pColor2 != NULL)
	{
		pColor2->pVtbl->Release(pColor2);
	}

    // cleanup in this order
    if(surfaceIn != NULL)
    { 
        surfaceIn->pVtbl->Release(surfaceIn);
        surfaceIn = NULL;
    }
    encoder->pVtbl->Terminate(encoder);
    encoder->pVtbl->Release(encoder);
    encoder = NULL;
    context->pVtbl->Terminate(context);
    context ->pVtbl->Release(context);
    context = NULL; // context is the last

    AMFFactoryHelper_Terminate();
    return 0;
}
//-------------------------------------------------------------------------------------------------
static void FillSurfaceDX9(AMFContext *context, AMFSurface *surface)
{
    HRESULT hr = S_OK;
    // fill surface with something something useful. We fill with color and color rect
    D3DCOLOR color1 = D3DCOLOR_XYUV (128, 255, 128);
    D3DCOLOR color2 = D3DCOLOR_XYUV (128, 0, 128);
    // get native DX objects
    IDirect3DDevice9 *deviceDX9 = (IDirect3DDevice9 *)context->pVtbl->GetDX9Device(context, AMF_DX9); // no reference counting - do not Release()
    AMFPlane *plane = surface->pVtbl->GetPlaneAt(surface, 0);
    IDirect3DSurface9* surfaceDX9 = (IDirect3DSurface9*)plane->pVtbl->GetNative(plane); // no reference counting - do not Release()
    hr = deviceDX9->lpVtbl->ColorFill(deviceDX9, surfaceDX9, NULL, color1);

    if(xPos + rectSize > widthIn)
    {
        xPos = 0;
    }
    if(yPos + rectSize > heightIn)
    {
        yPos = 0;
    }
    RECT rect = {xPos, yPos, xPos + rectSize, yPos + rectSize};
    hr = deviceDX9->lpVtbl->ColorFill(deviceDX9, surfaceDX9, &rect, color2);

    xPos+=2; //DX9 NV12 surfaces do not accept odd positions - do not use ++
    yPos+=2; //DX9 NV12 surfaces do not accept odd positions - do not use ++
}

static void FillNV12SurfaceWithColor(AMFSurface* surface, amf_uint8 Y, amf_uint8 U, amf_uint8 V)
{
	AMFPlane *pPlaneY = surface->pVtbl->GetPlaneAt(surface, 0);
	AMFPlane *pPlaneUV = surface->pVtbl->GetPlaneAt(surface, 1);

	amf_int32 widthY = pPlaneY->pVtbl->GetWidth(pPlaneY);
	amf_int32 heightY = pPlaneY->pVtbl->GetHeight(pPlaneY);
	amf_int32 lineY = pPlaneY->pVtbl->GetHPitch(pPlaneY);

	amf_uint8 *pDataY = (amf_uint8 *)pPlaneY->pVtbl->GetNative(pPlaneY);

	for (amf_int32 y = 0; y < heightY; y++)
	{
		amf_uint8 *pDataLine = pDataY + y * lineY;
		memset(pDataLine, Y, widthY);
	}

	amf_int32 widthUV = pPlaneUV->pVtbl->GetWidth(pPlaneUV);
	amf_int32 heightUV = pPlaneUV->pVtbl->GetHeight(pPlaneUV);
	amf_int32 lineUV = pPlaneUV->pVtbl->GetHPitch(pPlaneUV);

	amf_uint8 *pDataUV = (amf_uint8 *)pPlaneUV->pVtbl->GetNative(pPlaneUV);

	for (amf_int32 y = 0; y < heightUV; y++)
	{
		amf_uint8 *pDataLine = pDataUV + y * lineUV;
		for (amf_int32 x = 0; x < widthUV; x++)
		{
			*pDataLine++ = U;
			*pDataLine++ = V;
		}
	}

}

static void PrepareFillDX11(AMFContext *context)
{
	AMF_RESULT res = AMF_OK; // error checking can be added later
	res = context->pVtbl->AllocSurface(context, AMF_MEMORY_HOST, formatIn, widthIn, heightIn, &pColor1);
	res = context->pVtbl->AllocSurface(context, AMF_MEMORY_HOST, formatIn, rectSize, rectSize, &pColor2);

	FillNV12SurfaceWithColor(pColor2, 128, 0, 128);
	FillNV12SurfaceWithColor(pColor1, 128, 255, 128);

	pColor1->pVtbl->Convert(pColor1, memoryTypeIn);
	pColor2->pVtbl->Convert(pColor2, memoryTypeIn);
}

static void FillSurfaceDX11(AMFContext *context, AMFSurface *surface)
{
	HRESULT hr = S_OK;
	// fill surface with something something useful. We fill with color and color rect
	// get native DX objects
	ID3D11Device* deviceDX11 = (ID3D11Device *)context->pVtbl->GetDX11Device(context, AMF_DX11_0); // no reference counting - do not Release()
	AMFPlane *plane = surface->pVtbl->GetPlaneAt(surface, 0);
	ID3D11Texture2D* surfaceDX11 = (ID3D11Texture2D*)plane->pVtbl->GetNative(plane); // no reference counting - do not Release()
	
	ID3D11DeviceContext *deviceContextDX11 = NULL;
	deviceDX11->lpVtbl->GetImmediateContext(deviceDX11, &deviceContextDX11);

	AMFPlane *planeColor1 = pColor1->pVtbl->GetPlaneAt(pColor1, 0);
	ID3D11Texture2D* surfaceDX11Color1 = (ID3D11Texture2D*)planeColor1->pVtbl->GetNative(planeColor1); // no reference counting - do not Release()
	deviceContextDX11->lpVtbl->CopyResource(deviceContextDX11, (ID3D11Resource *)surfaceDX11, (ID3D11Resource *)surfaceDX11Color1);

	if (xPos + rectSize > widthIn)
	{
		xPos = 0;
	}
	if (yPos + rectSize > heightIn)
	{
		yPos = 0;
	}
	D3D11_BOX rect = { 0, 0, 0, rectSize, rectSize, 1 };

	AMFPlane *planeColor2 = pColor2->pVtbl->GetPlaneAt(pColor2, 0);
	ID3D11Texture2D* surfaceDX11Color2 = (ID3D11Texture2D*)planeColor2->pVtbl->GetNative(planeColor2); // no reference counting - do not Release()

	deviceContextDX11->lpVtbl->CopySubresourceRegion(deviceContextDX11, (ID3D11Resource *)surfaceDX11, 0, xPos, yPos, 0, (ID3D11Resource *)surfaceDX11Color2, 0, &rect);
	deviceContextDX11->lpVtbl->Flush(deviceContextDX11);

	xPos += 2; //DX9 NV12 surfaces do not accept odd positions - do not use ++
	yPos += 2; //DX9 NV12 surfaces do not accept odd positions - do not use ++

	deviceContextDX11->lpVtbl->Release(deviceContextDX11);
}

//----------------------------------------------------------------------------
void AMF_CDECL_CALL AMFThreadProc(void* pThis);
//-------------------------------------------------------------------------------------------------
amf_bool PollingThread_Run(PollingThread *pThread, AMFContext *context, AMFComponent *encoder, const wchar_t *pFileName)
{
    pThread->m_pFile = _wfopen(pFileName, L"wb");
    pThread->m_pContext = context;
    pThread->m_pEncoder = encoder;
    pThread->m_pThread = _beginthread(AMFThreadProc, 0, (void* )pThread);

    return pThread->m_pThread != (uintptr_t)-1L;
}
//-------------------------------------------------------------------------------------------------
amf_bool PollingThread_Stop(PollingThread *pThread)
{
    while(pThread->m_pThread != (uintptr_t)-1)
    {
        amf_sleep(1);
    }

    if(pThread->m_pFile)
    {
        fclose(pThread->m_pFile);
    }
    return true;
}
//-------------------------------------------------------------------------------------------------
void AMF_CDECL_CALL AMFThreadProc(void* pThis)
{
    PollingThread* pT = (PollingThread*)pThis;

    amf_pts latency_time = 0;
    amf_pts write_duration = 0;
    amf_pts encode_duration = 0;
    amf_pts last_poll_time = 0;

    AMF_RESULT res = AMF_OK; // error checking can be added later
    while(true)
    {
        AMFData* data;
        res = pT->m_pEncoder->pVtbl->QueryOutput(pT->m_pEncoder, &data);
        if(res == AMF_EOF)
        {
            break; // Drain complete
        }
        if(data != NULL)
        {
            amf_pts poll_time = amf_high_precision_clock();
            AMFVariantStruct var;

            data->pVtbl->GetProperty(data, START_TIME_PROPERTY, &var);
            amf_pts start_time = var.int64Value;
            if(start_time < last_poll_time ) // remove wait time if submission was faster then encode
            {
                start_time = last_poll_time;
            }
            last_poll_time = poll_time;

            encode_duration += poll_time - start_time;

            if(latency_time == 0)
            {
                latency_time = poll_time - start_time;
            }

            AMFBuffer* buffer;
            AMFGuid guid = IID_AMFBuffer();
            data->pVtbl->QueryInterface(data, &guid, (void**)&buffer); // query for buffer interface
            fwrite(buffer->pVtbl->GetNative(buffer), 1, buffer->pVtbl->GetSize(buffer), pT->m_pFile);
            
            write_duration += amf_high_precision_clock() - poll_time;
            buffer->pVtbl->Release(buffer);
            data->pVtbl->Release(data);
        }
        else
        {
            amf_sleep(1);
        }
    }
    printf("latency           = %.4fms\nencode  per frame = %.4fms\nwrite per frame   = %.4fms\n", 
        (double)latency_time/MILLISEC_TIME,
        (double)encode_duration/MILLISEC_TIME/frameCount, 
        (double)write_duration/MILLISEC_TIME/frameCount);

    pT->m_pEncoder = NULL;
    pT->m_pContext = NULL;
    pT->m_pThread = (uintptr_t)-1;

}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
