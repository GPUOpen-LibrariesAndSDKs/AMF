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

#include "DDAPISource.h"
//#include "CaptureStats.h"
#include "public/samples/CPPSamples/common/CmdLogger.h"

#include <VersionHelpers.h>

using namespace amf;

namespace
{
#ifdef USE_DUPLICATEOUTPUT1
	const DXGI_FORMAT DesktopFormats[] = { DXGI_FORMAT_R8G8B8A8_UNORM };
	const unsigned DesktopFormatsCounts = 1;
#endif
}

//-------------------------------------------------------------------------------------------------
AMFDDAPISourceImpl::AMFDDAPISourceImpl(AMFContext* pContext) :
m_pContext(pContext),
m_displayDuplicator(),
m_copyTexture(),
m_device(),
m_frameDuration(0),
m_firstPts(-1),
m_frameCount(0)
{
}

//-------------------------------------------------------------------------------------------------
AMFDDAPISourceImpl::~AMFDDAPISourceImpl()
{
	TerminateDisplayCapture();
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFDDAPISourceImpl::InitDisplayCapture(uint32_t displayMonitorIndex, amf_pts frameDuration)
{
	// Make sure we are in Windows 8 or higher
	if (!IsWindows8OrGreater())
	{
		return AMF_UNEXPECTED;
	}

	m_frameDuration = frameDuration;

	// Set the device
	m_device = static_cast<ID3D11Device*>(m_pContext->GetDX11Device());
	AMF_RETURN_IF_FALSE(m_device != NULL, AMF_NOT_INITIALIZED, L"Could not get m_device");
	m_device->GetImmediateContext(&m_context);

	// Get DXGI device
	ATL::CComPtr<IDXGIDevice> dxgiDevice;
	HRESULT hr = m_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Could not find dxgiDevice");

	// Get DXGI adapter
	ATL::CComPtr<IDXGIAdapter> dxgiAdapter;
	hr = dxgiDevice->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&dxgiAdapter));
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Could not find dxgiAdapter");

	// Get output
	ATL::CComPtr<IDXGIOutput> dxgiOutput;
	hr = dxgiAdapter->EnumOutputs(displayMonitorIndex, &dxgiOutput);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Could not find dxgiOutput when calling EnumOutputs");

	// m_OutputDescription will allow access to DesktopCoordinates
	hr = dxgiOutput->GetDesc(&m_outputDescription);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Could not find dxgiOutput description");

	// Basic checks for simple screen display
	bool supportedRotations = m_outputDescription.Rotation == DXGI_MODE_ROTATION_UNSPECIFIED ||
		m_outputDescription.Rotation == DXGI_MODE_ROTATION_IDENTITY;
	if (!supportedRotations)
	{
		AMF_RETURN_IF_FAILED(AMF_FAIL, L"Unsupported display rotation");
	}

	// Create desktop duplication
#ifdef USE_DUPLICATEOUTPUT1
	// Query the IDXGIOutput5 interface.  IDXGIOutput5 derives from IDXGIOutput and
	// has a few extra methods such as DuplicateOutput1 which we use below
	hr = dxgiOutput->QueryInterface(__uuidof(IDXGIOutput5), reinterpret_cast<void**>(&m_dxgiOutput5));
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Could not find dxgiOutput5");

	hr = m_dxgiOutput5->DuplicateOutput1(m_device, 0, DesktopFormatsCounts, DesktopFormats, &m_displayDuplicator);
#else
	// Query the IDXGIOutput1 interface.  IDXGIOutput1 derives from IDXGIOutput and
	// has a few extra methods such as DuplicateOutput which we use below
	hr = dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&m_dxgiOutput1));
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Could not find dxgiOutput1");

	hr = m_dxgiOutput1->DuplicateOutput(m_device, &m_displayDuplicator);
#endif
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Could not get display duplication object");

	return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFDDAPISourceImpl::TerminateDisplayCapture()
{
	AMFLock lock(&m_sync);
	//

	// Clear any active observers and then the surfaces
	for (amf_list< AMFSurface* >::iterator it = m_freeCopySurfaces.begin(); it != m_freeCopySurfaces.end(); it++)
	{
		(*it)->RemoveObserver(this);
	}
	m_freeCopySurfaces.clear();

	// Free the cache of textures
	m_freeCopyTextures.clear();

	m_displayDuplicator.Release();
#ifdef USE_DUPLICATEOUTPUT1
	m_dxgiOutput5.Release();
#else
	m_dxgiOutput1.Release();
#endif
	m_copyTexture.Release();
	m_device.Release();
	m_pContext.Release();

	return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFDDAPISourceImpl::AcquireSurface(amf::AMFSurface** ppSurface)
{
	AMFLock lock(&m_sync);

	AMF_RESULT res = AMF_OK;

	amf_pts startTime = amf_high_precision_clock();
	if (m_firstPts == -1)
	{
		m_firstPts = startTime;
	}

	amf_pts waitTime = m_firstPts + m_frameCount  * m_frameDuration + m_frameDuration - startTime;
	if (waitTime < 0)
	{
		waitTime = 0;
	}

	// AMF_RETURN_IF_FALSE(m_displayDuplicator != NULL, AMF_FAIL, L"AcquireSurface() has null display duplicator");
	if (!m_displayDuplicator)
	{
		res = GetNewDuplicator();
		AMF_RETURN_IF_FAILED(res, L"GetNewDuplicator() failed");
	}

	// Get new frame
	UINT kAcquireTimeout = UINT(waitTime / 10000); // to ms
	DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
	ATL::CComPtr<IDXGIResource> desktopResource;
	ATL::CComPtr<ID3D11Texture2D> displayTexture;
	ATL::CComPtr<ID3D11Texture2D> returnTexture;
	HRESULT hr = m_displayDuplicator->AcquireNextFrame(kAcquireTimeout, &frameInfo, &desktopResource);
	if (hr == S_OK)
	{
		hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&displayTexture));
		AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Could not get captured display texture");

		D3D11_TEXTURE2D_DESC displayDesc;
		displayTexture->GetDesc(&displayDesc);

		res = GetFreeTexture(&displayDesc, &returnTexture);
		AMF_RETURN_IF_FAILED(res, L"GetFreeTexture() failed");

		m_context->CopyResource(returnTexture, displayTexture);
		m_context->Flush();

		m_copyTexture = returnTexture;
	
		hr = m_displayDuplicator->ReleaseFrame();
		if (DXGI_ERROR_ACCESS_LOST == hr)
		{
			res = GetNewDuplicator();
			AMF_RETURN_IF_FAILED(res, L"GetNewDuplicator() failed after DXGI_ERROR_ACCESS_LOST 1");
		}
	}
	else if (DXGI_ERROR_WAIT_TIMEOUT == hr)
	{
		if (m_copyTexture == NULL)
		{
			return AMF_REPEAT;
		}

		D3D11_TEXTURE2D_DESC copyDesc;
		m_copyTexture->GetDesc(&copyDesc);

		res = GetFreeTexture(&copyDesc, &returnTexture);
		AMF_RETURN_IF_FAILED(res, L"GetFreeTexture() failed");

		m_context->CopyResource(returnTexture, m_copyTexture);
		m_context->Flush();
	}
	else if (DXGI_ERROR_ACCESS_LOST == hr)
	{
		res = GetNewDuplicator();
		AMF_RETURN_IF_FAILED(res, L"GetNewDuplicator() failed after DXGI_ERROR_ACCESS_LOST 2");
		//
		return AMF_REPEAT;
	}
	else 
	{
		// Check for other errors
		AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"Could not AcquireNextFrame");
	}

	res = m_pContext->CreateSurfaceFromDX11Native(returnTexture, ppSurface, this);
	AMF_RETURN_IF_FAILED(res, L"CreateSurfaceFromDX11Native() failed");
	m_freeCopySurfaces.push_back(*ppSurface);

	m_frameCount++;
	return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFDDAPISourceImpl::GetNewDuplicator()
{
	HRESULT hr = S_OK;

	m_displayDuplicator.Release();
	// Recreate desktop duplication ptr
#ifdef USE_DUPLICATEOUTPUT1
	hr = m_dxgiOutput5->DuplicateOutput1(m_device, 0, DesktopFormatsCounts, DesktopFormats, &m_displayDuplicator);
#else
	hr = m_dxgiOutput1->DuplicateOutput(m_device, &m_displayDuplicator);
#endif

	return (SUCCEEDED(hr) ? AMF_OK : AMF_FAIL);
}

//-------------------------------------------------------------------------------------------------
void AMFDDAPISourceImpl::GetTextureDim(unsigned& width, unsigned& height)
{
	AMFLock lock(&m_sync);
	//
	width = 0;
	height = 0;
	if (m_copyTexture)
	{
		D3D11_TEXTURE2D_DESC desc = {};
		m_copyTexture->GetDesc(&desc);
		// 
		width = desc.Width;
		height = desc.Height;
	}
}

//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL AMFDDAPISourceImpl::OnSurfaceDataRelease(amf::AMFSurface* pSurface)
{
	AMFLock lock(&m_sync);
	//
	if (m_freeCopyTextures.size() < 5)
	{
		m_freeCopyTextures.push_back((ID3D11Texture2D*)pSurface->GetPlaneAt(0)->GetNative());
	}
	//
	for (amf_list< AMFSurface* >::iterator it = m_freeCopySurfaces.begin(); it != m_freeCopySurfaces.end(); it++)
	{
		if (*it == pSurface)
		{
			m_freeCopySurfaces.erase(it);
			break;
		}
	}
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT    AMFDDAPISourceImpl::GetFreeTexture(D3D11_TEXTURE2D_DESC *desc, ID3D11Texture2D **ppTexture)
{
	AMFLock lock(&m_sync);
	//
	while (m_freeCopyTextures.size() > 0)
	{
		ATL::CComPtr < ID3D11Texture2D > texture = m_freeCopyTextures.front();
		m_freeCopyTextures.pop_front();
		//
		D3D11_TEXTURE2D_DESC freeDesc;
		texture->GetDesc(&freeDesc);
		if (desc->Width == freeDesc.Width && desc->Height == freeDesc.Height && desc->Format == freeDesc.Format)
		{
			*ppTexture = texture.Detach();
			return AMF_OK;
		}
	}
	// Reset some of the flags
	desc->BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc->BindFlags |= D3D11_BIND_RENDER_TARGET; // request renderer D3D11_BIND_RENDER_TARGET
	desc->Usage = D3D11_USAGE_DEFAULT;
	desc->CPUAccessFlags = 0;
	desc->MiscFlags = D3D11_RESOURCE_MISC_SHARED;
	//
	HRESULT hr = m_device->CreateTexture2D(desc, NULL, ppTexture);
	AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"CreateTexture2D() failed");
	return AMF_OK;
}

