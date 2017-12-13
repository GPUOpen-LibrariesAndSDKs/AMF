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
#define WIN32_LEAN_AND_MEAN

#include "public/include/core/Context.h"
#include "public/common/InterfaceImpl.h"
#include "public/common/PropertyStorageImpl.h"

//#define USE_DUPLICATEOUTPUT1

#include <d3d11.h>
#ifdef USE_DUPLICATEOUTPUT1
// Requires Windows SDK 10.0.10586
#include <dxgi1_5.h>
#else
#include <dxgi1_2.h>
#endif
#include <atlbase.h>


namespace amf
{
	class AMFDDAPISourceImpl : public 
		AMFInterfaceImpl< AMFInterface >,
		public amf::AMFSurfaceObserver
	{
	public:
		AMFDDAPISourceImpl(AMFContext* pContext);
		~AMFDDAPISourceImpl();

		AMF_RESULT                      InitDisplayCapture(uint32_t displayMonitorIndex, amf_pts frameDuration);
		AMF_RESULT                      TerminateDisplayCapture();

		AMF_RESULT                      AcquireSurface(amf::AMFSurface **pSurface);

		// Only valid between Start() and Done() calls
		void                            GetTextureDim(unsigned& width, unsigned& height);

		// AMFSurfaceObserver interface
		virtual void        AMF_STD_CALL OnSurfaceDataRelease(AMFSurface* pSurface);

	private:
		// Utility methods
		AMF_RESULT                              GetFreeTexture(D3D11_TEXTURE2D_DESC *desc, ID3D11Texture2D **ppTexture);

		AMF_RESULT                              GetNewDuplicator();

		// When we are done with a texture, we push it onto a free list
		// for re-use
		amf_list<ATL::CComPtr<ID3D11Texture2D>> m_freeCopyTextures;
		// We must track the AMF surfaces in case Terminate() is called
		// before the surface is released
		amf_list< AMFSurface* >                 m_freeCopySurfaces;

		AMFContextPtr                           m_pContext;

		mutable AMFCriticalSection              m_sync;

		ATL::CComPtr<IDXGIOutputDuplication>    m_displayDuplicator;
		ATL::CComPtr < ID3D11Texture2D >        m_copyTexture;
		ATL::CComPtr<ID3D11Device>              m_device;
		ATL::CComPtr<ID3D11DeviceContext>       m_context;
		DXGI_OUTPUT_DESC                        m_outputDescription;

		amf_pts                                 m_frameDuration; // in 100 of nanosec
		amf_pts                                 m_firstPts;
		amf_int64                               m_frameCount;

#ifdef USE_DUPLICATEOUTPUT1
		ATL::CComPtr<IDXGIOutput5>              m_dxgiOutput5;
#else
		ATL::CComPtr<IDXGIOutput1>              m_dxgiOutput1;
#endif
	};
	typedef AMFInterfacePtr_T<AMFDDAPISourceImpl>    AMFDDAPISourceImplPtr;
} //namespace amf
