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
// Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.
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

#include "DisplayCaptureImpl.h"
//#include "CaptureStats.h"

#include "public/include/core/Context.h"
#include "public/include/core/Trace.h"
#include "public/common/TraceAdapter.h"
#include "public/common/AMFFactory.h"
#include "public/common/TraceAdapter.h"

extern "C"
{
	// Function called from application code to create the component
	AMF_RESULT AMF_CDECL_CALL AMFCreateComponentDisplayCapture(amf::AMFContext* pContext, amf::AMFComponent** ppComponent)
	{
		*ppComponent = new amf::AMFInterfaceMultiImpl< amf::AMFDisplayCaptureImpl, amf::AMFComponent, amf::AMFContext* >(pContext);
		(*ppComponent)->Acquire();
		return AMF_OK;
	}

#ifdef WANT_CAPTURE_STATS
	CaptureStats gCaptureStats;
#endif
}

#define AMF_FACILITY L"AMFDisplayCaptureImpl"

using namespace amf;

//
//
// AMFDisplayCaptureImpl
//
//

//-------------------------------------------------------------------------------------------------
AMFDisplayCaptureImpl::AMFDisplayCaptureImpl(AMFContext* pContext)
  : m_pContext(pContext)
  , m_pDesktopDuplication()
  , m_pCurrentTime()
  , m_eof(false)
  , m_lastStartPts(-1)
  , m_frameRate(60)
{
	// Add display dvr properties
	AMFPrimitivePropertyInfoMapBegin
		// Assume a max of 4 display adapters
		AMFPropertyInfoInt64(AMF_DISPLAYCAPTURE_MONITOR_INDEX, L"Index of the display monitor", 0, 0, 4, false),
		AMFPropertyInfoInt64(AMF_DISPLAYCAPTURE_FRAMERATE, L"Capture frame rate", 60, 24, 60, false),
		AMFPropertyInfoInterface(AMF_DISPLAYCAPTURE_CURRENT_TIME_INTERFACE, L"Interface object for getting current time", NULL, false),
		AMFPropertyInfoInt64(AMF_DISPLAYCAPTURE_FORMAT, L"Capture surface format", AMF_SURFACE_BGRA, AMF_SURFACE_FIRST, AMF_SURFACE_LAST, true),
	AMFPrimitivePropertyInfoMapEnd
}

//-------------------------------------------------------------------------------------------------
AMFDisplayCaptureImpl::~AMFDisplayCaptureImpl()
{
	m_pCurrentTime.Release();
	//
	Terminate();
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFDisplayCaptureImpl::Init(AMF_SURFACE_FORMAT /*format*/, amf_int32 /*width*/, amf_int32 /*height*/)
{
	AMF_RESULT res = AMF_OK;
	// clean up any information we might previously have
	res = Terminate();
	AMF_RETURN_IF_FAILED(res, L"Terminate() failed");

	m_pDesktopDuplication = new AMFDDAPISourceImpl(m_pContext);
	AMF_RETURN_IF_INVALID_POINTER(m_pDesktopDuplication);

	// Get the display adapter index
	uint32_t displayMonitorIndex = 0;
	GetProperty(AMF_DISPLAYCAPTURE_MONITOR_INDEX, &displayMonitorIndex);
	// Get the frame rate
	GetProperty(AMF_DISPLAYCAPTURE_FRAMERATE, &m_frameRate);
	amf_pts frameDuration = amf_pts(AMF_SECOND / m_frameRate);
	// Get the current time interface property if it has been set
	AMFInterfacePtr pTmp;
	GetProperty(AMF_DISPLAYCAPTURE_CURRENT_TIME_INTERFACE, &pTmp);
	m_pCurrentTime = (AMFCurrentTimePtr)pTmp.GetPtr();

	res = m_pDesktopDuplication->InitDisplayCapture(displayMonitorIndex, frameDuration);
	AMF_RETURN_IF_FAILED(res, L"InitDisplayCapture() failed");

#ifdef WANT_CAPTURE_STATS
	gCaptureStats.Init("./displaycapture-queryoutput-log.txt", "Display Capture Statistics");
#endif

	return res;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFDisplayCaptureImpl::ReInit(amf_int32 width, amf_int32 height)
{
	AMFLock lock(&m_sync);
	Terminate();
#ifdef WANT_CAPTURE_STATS
	gCaptureStats.Reinit();
#endif
	return Init(AMF_SURFACE_UNKNOWN, width, height);
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFDisplayCaptureImpl::Terminate()
{
	AMFLock lock(&m_sync);

	AMF_RESULT res = AMF_OK;
	if (m_pDesktopDuplication)
	{
		res = m_pDesktopDuplication->TerminateDisplayCapture();
		AMF_RETURN_IF_FAILED(res, L"TerminateDisplayCapture() failed");

		m_pDesktopDuplication.Release();
	}
#ifdef WANT_CAPTURE_STATS
	gCaptureStats.Terminate();
#endif
	return res;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFDisplayCaptureImpl::Drain()
{
	AMFLock lock(&m_sync);
	SetEOF(true);
	return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFDisplayCaptureImpl::Flush()
{
	AMFLock lock(&m_sync);

	return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFDisplayCaptureImpl::SubmitInput(AMFData* pData)
{
	AMFLock lock(&m_sync);

	return AMF_FAIL;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFDisplayCaptureImpl::QueryOutput(AMFData** ppData)
{
	AMFLock lock(&m_sync);

	AMF_RETURN_IF_FALSE(m_pDesktopDuplication != NULL, AMF_FAIL, L"GetOutput() - m_pDesktopDuplication == NULL");

	// Check to see if we are done
	if (GetEOF())
	{
		return AMF_EOF;
	}

	AMF_RESULT res = AMF_OK;
	// check some required parameters
	AMF_RETURN_IF_FALSE(ppData != NULL, AMF_INVALID_ARG, L"GetOutput() - ppData == NULL");
	
	// initialize output
	*ppData = NULL;

	amf::AMFSurfacePtr surfPtr;
	res = m_pDesktopDuplication->AcquireSurface(&surfPtr);
	if (AMF_REPEAT == res)
	{	// Specal case for repeat
			return res;
	}
	AMF_RETURN_IF_FAILED(res, L"AcquireSurface() failed");

	// Update the surface format in case it has changed
	AMF_SURFACE_FORMAT surfFormat = surfPtr->GetFormat();
	res = SetProperty(AMF_DISPLAYCAPTURE_FORMAT, surfFormat);

	amf_pts currentPts = GetCurrentPts();
	// Duration
	if (m_lastStartPts < 0)
	{
		m_lastStartPts = currentPts;
	}
	amf_pts duration = currentPts - m_lastStartPts;
	surfPtr->SetDuration(duration);
	m_lastStartPts = currentPts;
	// Pts
	surfPtr->SetPts(currentPts);
	//
	*ppData = surfPtr.Detach();

#ifdef WANT_CAPTURE_STATS
	gCaptureStats.addDuration(duration);
#endif

	return AMF_OK;
}

//-------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFDisplayCaptureImpl::GetCaps(AMFCaps** ppCaps)
{
	AMFLock lock(&m_sync);
	///////////TODO:///////////////////////////////
	return AMF_NOT_IMPLEMENTED;
}

//-------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFDisplayCaptureImpl::Optimize(AMFComponentOptimizationCallback* pCallback)
{
	///////////TODO:///////////////////////////////
	return AMF_NOT_IMPLEMENTED;
}

//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL  AMFDisplayCaptureImpl::OnPropertyChanged(const wchar_t* pName)
{
	// const amf_wstring  name(pName);
}

//-------------------------------------------------------------------------------------------------
amf_pts AMFDisplayCaptureImpl::GetCurrentPts() const
{
	amf_pts result = 0;
	if (m_pCurrentTime)
	{
		result = m_pCurrentTime->Get();
	}
	else
	{
		result = amf_high_precision_clock();
	}
	return result;
}

