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

// This sample analyses NV12 frames using AMF Pre Analysis and writes the importance map to a file.
// In actual use the importance map can aid an encoder of some type.

#include <stdio.h>
#ifdef _WIN32
#include <tchar.h>
#include <d3d9.h>
#include <d3d11.h>
#endif
#include "public/common/AMFFactory.h"
#include "public/include/components/PreAnalysis.h"
#include "public/common/Thread.h"
#include "public/common/AMFSTL.h"
#include "public/common/TraceAdapter.h"
#include "public/samples/CPPSamples/common/SurfaceGenerator.h"
#include "public/samples/CPPSamples/common/PollingThread.h"
#include <fstream>

#define AMF_FACILITY L"AMFSamplePA"

static const wchar_t *pCodec = AMFPreAnalysis;

 //#define ENABLE_4K

#ifdef _WIN32
static amf::AMF_MEMORY_TYPE memoryTypeIn = amf::AMF_MEMORY_DX11;
#else
static amf::AMF_MEMORY_TYPE memoryTypeIn = amf::AMF_MEMORY_VULKAN;
#endif
static amf::AMF_SURFACE_FORMAT formatIn = amf::AMF_SURFACE_NV12;

#if defined(ENABLE_4K)
static amf_int32 widthIn = 1920 * 2;
static amf_int32 heightIn = 1080 * 2;
#else
static amf_int32 widthIn = 1920;
static amf_int32 heightIn = 1080;
#endif
static amf_int32 frameRateIn = 30;
static amf_int64 bitRateIn = 5000000L; // in bits, 5MBit
amf_int32 rectSize = 50;
static amf_int32 frameCount = 500;
static bool bMaximumSpeed = true;

static const wchar_t *fileNameOut = L"./output.immp";

amf::AMFSurfacePtr pColor1;
amf::AMFSurfacePtr pColor2;

amf::AMFSurfacePtr pColor3;
amf::AMFSurfacePtr pColor4;

class PAPollingThread : public PollingThread
{
public:
	PAPollingThread(amf::AMFContext *pContext, amf::AMFComponent *pPreAnalysis, const wchar_t *pFileName);
protected:
	void ProcessData(amf::AMFData* pData) override;
	void PrintResults() override;
};

#ifdef _WIN32
int _tmain(int /* argc */, _TCHAR* /* argv */[])
#else
int main(int /* argc */, char* /* argv */[])
#endif
{
	AMF_RESULT res = AMF_OK; // error checking can be added later
	res = g_AMFFactory.Init();
	if (res != AMF_OK)
	{
		wprintf(L"AMF Failed to initialize");
		return 1;
	}

	::amf_increase_timer_precision();

	amf::AMFTraceEnableWriter(AMF_TRACE_WRITER_CONSOLE, true);
	amf::AMFTraceEnableWriter(AMF_TRACE_WRITER_DEBUG_OUTPUT, true);

	// initialize AMF
	amf::AMFContextPtr context;
	amf::AMFComponentPtr pre_analysis;
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
#endif

	// Load the PreAnalysis component
	res = g_AMFFactory.GetFactory()->CreateComponent(context, AMFPreAnalysis, &pre_analysis);
	AMF_RETURN_IF_FAILED(res, L"Create PreAnalysis failed");

	// Enable Scene change detection and set sensitivity to medium
    res = pre_analysis->SetProperty(AMF_PA_SCENE_CHANGE_DETECTION_ENABLE, true);
    AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_PA_SCENE_CHANGE_DETECTION_ENABLE, true) failed");
    res = pre_analysis->SetProperty(AMF_PA_SCENE_CHANGE_DETECTION_SENSITIVITY, AMF_PA_SCENE_CHANGE_DETECTION_SENSITIVITY_MEDIUM);
    AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_PA_SCENE_CHANGE_DETECTION_SENSITIVITY, %d) failed", AMF_PA_SCENE_CHANGE_DETECTION_SENSITIVITY_MEDIUM);

	res = pre_analysis->Init(formatIn, widthIn, heightIn);
	AMF_RETURN_IF_FAILED(res, L"pre_analysis->Init() failed");


	PAPollingThread thread(context, pre_analysis, fileNameOut);
	thread.Start();

	// Analyze some frames
	amf_int32 submitted = 0;
	while (submitted < frameCount)
	{
		if (surfaceIn == NULL)
		{
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
			else
			{
				FillSurfaceDX11(context, surfaceIn, false);
			}
#endif
		}
		// Analyze
		amf_pts start_time = amf_high_precision_clock();
		surfaceIn->SetProperty(START_TIME_PROPERTY, start_time);

		res = pre_analysis->SubmitInput(surfaceIn);
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
	// drain Pre Analysis; input queue can be full
	while (true)
	{
		res = pre_analysis->Drain();
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
	pre_analysis->Terminate();
	pre_analysis = NULL;
	context->Terminate();
	context = NULL; // context is the last

	g_AMFFactory.Terminate();
	return 0;
}

PAPollingThread::PAPollingThread(amf::AMFContext *pContext, amf::AMFComponent *pPre_analysis, const wchar_t *pFileName)
	: PollingThread(pContext, pPre_analysis, pFileName, true)
{}

void PAPollingThread::ProcessData(amf::AMFData* pData)
{
	AMF_RESULT res = AMF_OK;

	AdjustTimes(pData);

	amf::AMFVariant  activityMapVar;
	res = pData->GetProperty(AMF_PA_ACTIVITY_MAP, &activityMapVar);

	// Get Activity Map out of data
	amf::AMFSurfacePtr spSurface(activityMapVar.ToInterface());
	amf::AMFPlane*     pPlane   = spSurface->GetPlaneAt(0);
	const amf_int32    xBlocks  = pPlane->GetWidth();
	const amf_int32    yBlocks  = pPlane->GetHeight();
	const amf_uint32*  pToWrite = static_cast<amf_uint32*>(pPlane->GetNative());
	const amf_int32    hPitch   = pPlane->GetHPitch() / pPlane->GetPixelSizeInBytes();
	const amf_int32    hWidth   = xBlocks * sizeof(amf_uint32);

	if (m_pFile != NULL)
	{
		for (amf_int32 y = 0; y < yBlocks; y++)
		{
			m_pFile->Write(pToWrite, hWidth, NULL);
			pToWrite += hPitch;
		}
	}

	m_WriteDuration += amf_high_precision_clock() - m_LastPollTime;
}

void PAPollingThread::PrintResults()
{
	PrintTimes("PA ", frameCount);
}
