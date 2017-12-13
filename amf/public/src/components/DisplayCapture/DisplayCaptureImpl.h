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

#pragma once
#include "public/include/components/Component.h"
#include "public/include/components/DisplayCapture.h"
#include "public/common/PropertyStorageExImpl.h"
#include "public/include/core/Context.h"
#include "public/common/ByteArray.h"
#include "public/common/CurrentTime.h"
#include "public/src/components/DisplayCapture/DDAPISource.h"

using namespace amf;


namespace amf
{
	//-------------------------------------------------------------------------------
	typedef AMFPropertyStorageExImpl <AMFComponent> baseclassCompositorProperty;
	//-------------------------------------------------------------------------------
	//-------------------------------------------------------------------------------------------------

	class AMFDisplayCaptureImpl : 
		public AMFInterfaceBase,
		public AMFPropertyStorageExImpl <AMFComponent>
	{
	public:
		// Interface access
		AMF_BEGIN_INTERFACE_MAP
			AMF_INTERFACE_MULTI_ENTRY(AMFComponent)
			AMF_INTERFACE_CHAIN_ENTRY(baseclassCompositorProperty)
		AMF_END_INTERFACE_MAP


		AMFDisplayCaptureImpl(AMFContext* pContext);
		virtual ~AMFDisplayCaptureImpl();

		// AMFComponent interface
		virtual AMF_RESULT  AMF_STD_CALL  Init(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height);
		virtual AMF_RESULT  AMF_STD_CALL  ReInit(amf_int32 width, amf_int32 height);
		virtual AMF_RESULT  AMF_STD_CALL  Terminate();
		virtual AMF_RESULT  AMF_STD_CALL  Drain();
		virtual AMF_RESULT  AMF_STD_CALL  Flush();

		virtual AMF_RESULT  AMF_STD_CALL  SubmitInput(AMFData* pData);
		virtual AMF_RESULT  AMF_STD_CALL  QueryOutput(AMFData** ppData);
		virtual AMFContext* AMF_STD_CALL  GetContext()                                              {  return m_pContext;  };
		virtual AMF_RESULT  AMF_STD_CALL  SetOutputDataAllocatorCB(AMFDataAllocatorCB* callback)    {  return AMF_NOT_SUPPORTED;  };

		virtual AMF_RESULT  AMF_STD_CALL GetCaps(AMFCaps** ppCaps);

		virtual AMF_RESULT  AMF_STD_CALL Optimize(AMFComponentOptimizationCallback* pCallback);

		virtual void        AMF_STD_CALL OnPropertyChanged(const wchar_t* pName);

		amf_pts GetCurrentPts() const;

		void SetEOF(bool eof) { m_eof = eof; }

		bool GetEOF() const { return m_eof;  }

	private:
		mutable AMFCriticalSection				m_sync;

		AMFContextPtr							m_pContext;

		AMFDDAPISourceImplPtr                   m_pDesktopDuplication;

		AMFCurrentTimePtr						m_pCurrentTime;

		bool									m_eof;

		amf_pts									m_lastStartPts;

		amf_int32								m_frameRate;

		// No copy or assign
		AMFDisplayCaptureImpl(const AMFDisplayCaptureImpl&);
		AMFDisplayCaptureImpl& operator=(const AMFDisplayCaptureImpl&);
	};
	
}

