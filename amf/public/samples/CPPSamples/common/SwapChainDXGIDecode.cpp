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

#include "SwapChain.h"
#include "public/common/TraceAdapter.h"
#include "SwapChainDXGIDecode.h"

#include <d3d11.h>
#include <d3d12.h>

using namespace amf;

#define AMF_FACILITY L"SwapChainDXGIDecode"

typedef     HRESULT(WINAPI* DCompositionCreateSurfaceHandle_Fn)(DWORD desiredAccess, SECURITY_ATTRIBUTES* securityAttributes, HANDLE* surfaceHandle);
typedef     HRESULT(WINAPI* DCompositionCreateDevice_Fn)(IDXGIDevice* dxgiDevice, REFIID iid, void** dcompositionDevice);
typedef     HRESULT(WINAPI* DCompositionCreateDevice2_Fn)(IUnknown* renderingDevice, REFIID iid, void** dcompositionDevice);
typedef     HRESULT(WINAPI* DCompositionCreateDevice3_Fn)(IUnknown* renderingDevice, REFIID iid, void** dcompositionDevice);
typedef     HRESULT(WINAPI* DCompositionAttachMouseDragToHwnd_Fn)(IDCompositionVisual* visual, HWND hwnd, BOOL enable);


SwapChainDXGIDecode::SwapChainDXGIDecode(AMFContext* pContext, AMF_MEMORY_TYPE memoryType) :
    SwapChainDXGI(pContext),
    m_memoryType(memoryType),
    m_hDcompDll(0),
    m_hDCompositionSurfaceHandle(nullptr)
{
}

SwapChainDXGIDecode::~SwapChainDXGIDecode()
{
    Terminate();
}

AMF_RESULT SwapChainDXGIDecode::GetDXGIInterface(amf_bool reinit)
{
    if (m_pDXGIFactory != nullptr && reinit == false)
    {
        return AMF_OK;
    }

    m_pDevice = nullptr;
    m_pDXGIDevice = nullptr;
    m_pDXGIFactoryMedia = nullptr;
    m_pDXGIFactory = nullptr;
    m_pDXGIFactory2 = nullptr;

    switch (m_memoryType)
    {
    case AMF_MEMORY_DX11:
    {
        m_pDevice = static_cast<ID3D11Device*>(m_pContext->GetDX11Device());
        m_pDXGIDevice = m_pDevice;
        break;
    }
    case AMF_MEMORY_DX12:
    {
        AMFContext2Ptr pContext2(m_pContext);
        m_pDevice = static_cast<ID3D12Device*>(pContext2->GetDX12Device());
        m_pDXGIDevice = m_pDevice;
        break;
    }
    default:
        AMF_RETURN_IF_FALSE(false, AMF_NOT_SUPPORTED, L"Init() - Invalid engine %s", AMFGetMemoryTypeName(m_memoryType));
    }

    // Factory media interface creates swap chains that use
    // direct composition surfaces to decode and display
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_pDXGIFactoryMedia));
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"Init() - CreateDXGIFactory1() failed to create DXGIFactoryMedia");

    hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_pDXGIFactory));
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"Init() - CreateDXGIFactory1() failed to create DXGIFactory");
    m_pDXGIFactory->QueryInterface(&m_pDXGIFactory2);

    AMF_RESULT res = GetDXGIAdapters();
    AMF_RETURN_IF_FAILED(res, L"GetDXGIInterface() - GetDXGIAdapters() failed");

    return AMF_OK;
}

AMF_RESULT SwapChainDXGIDecode::GetDXGIDeviceAdapter(IDXGIAdapter** ppDXGIAdapter)
{
    if (m_pDXGIDevice != nullptr)
    {
        m_pDXGIDevice->GetAdapter( ppDXGIAdapter );
    }
    return AMF_OK;
}

AMF_RESULT SwapChainDXGIDecode::Init(amf_handle hwnd, amf_handle hDisplay, amf::AMFSurface* pSurface, amf_int32 width, amf_int32 height, amf::AMF_SURFACE_FORMAT format, amf_bool /*fullscreen*/, amf_bool hdr, amf_bool stereo)
{
    AMF_RETURN_IF_FALSE(hwnd != nullptr, AMF_INVALID_ARG, L"Init() - Window handle is NULL");
    AMF_RETURN_IF_FALSE(width >= 0 && height >= 0, AMF_INVALID_ARG, L"Init() - Invalid width/height: width=%d height=%d", width, height);

    if (stereo || hdr)
    {
        return AMF_NOT_SUPPORTED;
    }

    m_hwnd = hwnd;
    m_hDisplay = hDisplay;

    if (pSurface != nullptr)
    {
        AMF_RESULT res = CreateSwapChain(pSurface, width, height, format);
        AMF_RETURN_IF_FAILED(res, L"Init() - CreateSwapChain() failed");
    }

    return AMF_OK;
}

AMF_RESULT SwapChainDXGIDecode::Terminate()
{
    TerminateSwapChain();
    if (m_hDcompDll != 0)
    {
        amf_free_library(m_hDcompDll);
        m_hDcompDll = 0;
    }

    m_pSurface = nullptr;
    m_pDXGIFactoryMedia = nullptr;
    m_pDevice = nullptr;

    return SwapChainDXGI::Terminate();
}

AMF_RESULT SwapChainDXGIDecode::TerminateSwapChain()
{
    m_pSwapChainDecode = nullptr;

    m_pDecodeTexture = nullptr;
    m_pVisualSurfaceRoot = nullptr;
    m_pScaleTransform = nullptr;
    m_pTransformGroup = nullptr;

    m_pDCompTarget = nullptr;
    m_pDCompDevice = nullptr;

    if (m_hDCompositionSurfaceHandle != nullptr)
    {
        CloseHandle(m_hDCompositionSurfaceHandle);
        m_hDCompositionSurfaceHandle = nullptr;
    }

    return AMF_OK;
}

AMF_RESULT SwapChainDXGIDecode::CreateSwapChain(AMFSurface* pSurface, amf_int32 width, amf_int32 height, amf::AMF_SURFACE_FORMAT format)
{
    AMF_RETURN_IF_FALSE(pSurface != nullptr, AMF_INVALID_ARG, L"CreateSwapChain() - pSurface is NULL");
    AMF_RETURN_IF_FALSE(m_pSwapChainDecode == nullptr, AMF_ALREADY_INITIALIZED, L"CreateSwapChain() - m_pSwapChainDecode is already initialized");

    AMF_RESULT res = GetDXGIInterface();
    AMF_RETURN_IF_FAILED(res, L"CreateSwapChain() - GetDXGIInterface() failed");

    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateSwapChain() - m_pDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pDXGIDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateSwapChain() - m_pDXGIDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pDXGIFactoryMedia != nullptr, AMF_NOT_INITIALIZED, L"CreateSwapChain() - m_pDXGIFactoryMedia is not initialized");
    AMF_RETURN_IF_FALSE(m_hwnd != nullptr, AMF_NOT_INITIALIZED, L"CreateSwapChain() - m_hwnd is not initialized");

    res = SetFormat(format);
    AMF_RETURN_IF_FAILED(res, L"Init() - SetFormat() failed");

    AMFPlane* pPlane = pSurface->GetPlaneAt(0);
    AMF_RETURN_IF_FALSE(pPlane != nullptr, AMF_INVALID_ARG, L"CreateSwapChain() - Surface does not contain packed surface");

    void* pPlaneData = pPlane->GetNative();
    AMF_RETURN_IF_FALSE(pPlane != nullptr, AMF_INVALID_ARG, L"CreateSwapChain() - Surface packed plane native data was NULL");

    m_pSurface = pSurface;
    switch (m_memoryType)
    {
    case AMF_MEMORY_DX11:
    {
        CComPtr<ID3D11Texture2D> pDX11Surface = (ID3D11Texture2D*)pPlaneData;
        m_pDecodeTexture = pDX11Surface;
        break;
    }
    case AMF_MEMORY_DX12:
    {
        CComPtr<ID3D12Resource> pDX12Surface = (ID3D12Resource*)pPlaneData;
        m_pDecodeTexture = pDX12Surface;
        break;
    }
    default:
        AMF_RETURN_IF_FALSE(false, AMF_NOT_SUPPORTED, L"CreateSwapChain() - Invalid engine %s", AMFGetMemoryTypeName(m_memoryType));
    }

    // Load direct composition dll
    if (m_hDcompDll == 0)
    {
#ifdef _WIN32
        m_hDcompDll = amf_load_library(L"Dcomp.dll");
#else
        m_hDcompDll = amf_load_library1(L"Dcomp.dll", true); //global flag set to true
#endif
        AMF_RETURN_IF_FALSE(m_hDcompDll != nullptr, AMF_DIRECTX_FAILED, L"CreateSwapChain() - Dcomp.dll is not available");
    }

    // Get direct composition function handles
    DCompositionCreateSurfaceHandle_Fn fDCompositionCreateSurfaceHandle = (DCompositionCreateSurfaceHandle_Fn)amf_get_proc_address(m_hDcompDll, "DCompositionCreateSurfaceHandle");
    AMF_RETURN_IF_FALSE(fDCompositionCreateSurfaceHandle != nullptr, AMF_DIRECTX_FAILED, L"CreateSwapChain() - DCompositionCreateSurfaceHandle is not available");

    DCompositionCreateDevice_Fn fDCompositionCreateDevice = (DCompositionCreateDevice_Fn)amf_get_proc_address(m_hDcompDll, "DCompositionCreateDevice");
    AMF_RETURN_IF_FALSE(fDCompositionCreateDevice != nullptr, AMF_DIRECTX_FAILED, L"CreateSwapChain() - DCompositionCreateDevice is not available");

    DCompositionCreateDevice3_Fn fDCompositionCreateDevice3 = (DCompositionCreateDevice3_Fn)amf_get_proc_address(m_hDcompDll, "DCompositionCreateDevice3");
    AMF_RETURN_IF_FALSE(fDCompositionCreateDevice3 != nullptr, AMF_DIRECTX_FAILED, L"CreateSwapChain() - DCompositionCreateDevice3 is not available");

    DCompositionAttachMouseDragToHwnd_Fn fDCompositionAttachMouseDragToHwnd = (DCompositionAttachMouseDragToHwnd_Fn)amf_get_proc_address(m_hDcompDll, "DCompositionAttachMouseDragToHwnd");
    AMF_RETURN_IF_FALSE(fDCompositionAttachMouseDragToHwnd != nullptr, AMF_DIRECTX_FAILED, L"CreateSwapChain() - DCompositionAttachMouseDragToHwnd is not available");

    // Get Composition Device
    HRESULT hr = fDCompositionCreateDevice3(m_pDXGIDevice, __uuidof(IDCompositionDesktopDevice), (void**)&m_pDCompDevice);
    //            hr = fDCompositionCreateDevice(m_pDXGIDevice, __uuidof(IDCompositionDevice), (void**)&m_pDCompDevice);
    AMF_RETURN_IF_FALSE(m_pDCompDevice != nullptr, AMF_DIRECTX_FAILED, L"CreateSwapChain() - DCompositionCreateDevice() failed");

    // Create composition target bound to window
    hr = m_pDCompDevice->CreateTargetForHwnd((HWND)m_hwnd, TRUE, &m_pDCompTarget);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateSwapChain() - DCompDevice->CreateTargetForHwnd() failed");

    // Create composition surface
#define COMPOSITIONSURFACE_ALL_ACCESS  0x0003L
    hr = fDCompositionCreateSurfaceHandle(COMPOSITIONSURFACE_ALL_ACCESS, nullptr, &m_hDCompositionSurfaceHandle);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateSwapChain() - DCompositionCreateSurfaceHandle() failed");

    // Create composition visual
    hr = m_pDCompDevice->CreateVisual(&m_pVisualSurfaceRoot);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateSwapChain() - CreateVisual() failed");

    // Set visual created as root of visual tree
    hr = m_pDCompTarget->SetRoot(m_pVisualSurfaceRoot);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateSwapChain() - SetRoot() failed");

    // Create swap chain for composition surface
    DXGI_DECODE_SWAP_CHAIN_DESC descVideoSwap = {};
    CComQIPtr<IDXGIResource> spDXGResource(m_pDecodeTexture);
    hr = m_pDXGIFactoryMedia->CreateDecodeSwapChainForCompositionSurfaceHandle(m_pDevice, m_hDCompositionSurfaceHandle, &descVideoSwap, spDXGResource, nullptr, &m_pSwapChainDecode);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"CreateSwapChain() - CreateDecodeSwapChainForCompositionSurfaceHandle() failed");

    // Init swap chain
    hr = m_pSwapChainDecode->SetColorSpace(DXGI_MULTIPLANE_OVERLAY_YCbCr_FLAG_BT709);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"CreateSwapChain() - SetColorSpace() failed");

    RECT sourceRect = { 0, 0, pPlane->GetWidth(), pPlane->GetHeight() };
    hr = m_pSwapChainDecode->SetSourceRect(&sourceRect);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"CreateSwapChain() - SetSourceRect() failed");

    UINT destWidth = 0;
    UINT destHeight = 0;
    m_pSwapChainDecode->GetDestSize(&destWidth, &destHeight);

    float scaleWidth = 1.0;
    float scaleHeight = 1.0;
    if (width > 0 && height > 0 && destWidth > 0 && destHeight > 0)
    {
        scaleWidth = (float)width / destWidth;
        scaleHeight = (float)height / destHeight;
    }

    RECT targetRect = { 0, 0, (LONG)destWidth, (LONG)destHeight };
    hr = m_pSwapChainDecode->SetTargetRect(&targetRect);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"CreateSwapChain() - SetTargetRect() failed");
    m_size = AMFConstructSize(width, height);

//  CComPtr<IUnknown> pUnknownSurface;
//  hr = m_pDCompDevice->CreateSurfaceFromHandle(m_hDCompositionSurfaceHandle, &pUnknownSurface);
//  ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateSurfaceFromHandle() failed");
//
//  CComPtr<IDCompositionVisual2>        pVisualSurfaceRoot;
//  hr = m_pDCompDevice->CreateVisual(&pVisualSurfaceRoot);
//  ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateVisual() failed");
//
//  hr = m_pVisualSurface->SetContent(pUnknownSurface);
//  hr = pVisualSurfaceRoot->AddVisual(m_pVisualSurface, TRUE, nullptr);

    hr = m_pVisualSurfaceRoot->SetContent(m_pSwapChainDecode);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateDCSwapChain() - SetContent(m_pSwapChainVideo) failed");

    CComQIPtr<IDCompositionVisual3>   pVisualSurface3(m_pVisualSurfaceRoot);
    if (pVisualSurface3 != nullptr)
    {
        pVisualSurface3->SetVisible(TRUE);
    }
    fDCompositionAttachMouseDragToHwnd(m_pVisualSurfaceRoot, (HWND)m_hwnd, TRUE);

    if (false)
    {
        // scale to full window
        hr = m_pDCompDevice->CreateScaleTransform(&m_pScaleTransform);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateDCSwapChain() - CreateScaleTransform() failed");

        // Set the scaling origin to the upper-right corner of the visual.
        hr = m_pScaleTransform->SetCenterX(0.0f);
        hr = m_pScaleTransform->SetCenterY(0.0f);
        // Set the scaling factor to three for both the width and height. 
        hr = m_pScaleTransform->SetScaleX(scaleWidth);
        hr = m_pScaleTransform->SetScaleY(scaleHeight);

        // Create the transform group.
        IDCompositionTransform* pTransforms[] = { m_pScaleTransform };
        hr = m_pDCompDevice->CreateTransformGroup(pTransforms, amf_countof(pTransforms), &m_pTransformGroup);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateDCSwapChain() - CreateTransformGroup() failed");

        // Apply the transform group to the visual.
        hr = m_pVisualSurfaceRoot->SetTransform(m_pTransformGroup);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateDCSwapChain() - SetTransform() failed");
    }

    // Commit all direct composition commands
    hr = m_pDCompDevice->Commit();
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateDCSwapChain() - Commit() failed");

    ShowCursor(TRUE);

    return AMF_OK;
}

AMF_RESULT SwapChainDXGIDecode::Submit(amf::AMFSurface* pSurface)
{
    AMF_RETURN_IF_FALSE(pSurface != nullptr, AMF_INVALID_ARG, L"m_pSwapChainDecode() - pSurface is NULL");

    AMFPlane* pPlane = pSurface->GetPlane(AMF_PLANE_PACKED);
    AMF_RETURN_IF_FALSE(pPlane != nullptr, AMF_INVALID_ARG, L"CreateSwapChain() - Surface does not contain packed surface");

    void* pPlaneData = pPlane->GetNative();
    AMF_RETURN_IF_FALSE(pPlane != nullptr, AMF_INVALID_ARG, L"CreateSwapChain() - Surface packed plane native data was NULL");

    void* pCurrentPlaneData = m_pSurface != nullptr ? m_pSurface->GetPlaneAt(0)->GetNative() : nullptr;

    if (pPlaneData != pCurrentPlaneData)
    {
        TerminateSwapChain();
        AMF_RESULT res = CreateSwapChain(pSurface, m_size.width, m_size.height, GetFormat());
        AMF_RETURN_IF_FAILED(res, L"Submit() - CreateSwapChain() failed");
    }

    m_pSurface = pSurface;
    return AMF_OK;
}

AMF_RESULT SwapChainDXGIDecode::Present(amf_bool waitForVSync)
{
    AMF_RETURN_IF_FALSE(m_pSwapChainDecode != nullptr, AMF_NOT_INITIALIZED, L"m_pSwapChainDecode() - m_pDXGIDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pSurface != nullptr, AMF_NOT_INITIALIZED, L"m_pSwapChainDecode() - m_pSurface is not initialized, call SUBMIT");

    amf::AMFContext::AMFDX11Locker dxlock(m_pContext);

    UINT syncInterval = 1;
    UINT presentFlags = DXGI_PRESENT_RESTART;
    if (waitForVSync == false)
    {
        presentFlags |= DXGI_PRESENT_DO_NOT_WAIT;
    }

    amf_int64 index = 0;
    AMF_RESULT err = m_pSurface->GetProperty(L"TextureArrayIndex", &index);
    AMF_RETURN_IF_FAILED(err, L"Present() - Failed to get surface TextureArrayIndex property")

    for (amf_int i = 0; i < 100; i++)
    {
        HRESULT hr = m_pSwapChainDecode->PresentBuffer((UINT)index, syncInterval, presentFlags);

        // If the GPU is busy at the moment present was called and if
        // it did not execute or schedule the operation, we get the
        // DXGI_ERROR_WAS_STILL_DRAWING error. Therefore we should try
        // presenting again after a delay
        if (hr != DXGI_ERROR_WAS_STILL_DRAWING)
        {
            //ASSERT_RETURN_IF_HR_FAILED(hr,AMF_DIRECTX_FAILED,L"Present() - Present() failed");
            break;
        }
        amf_sleep(1);
    }

    return AMF_OK;
}

AMF_RESULT SwapChainDXGIDecode::Resize(amf_int32 width, amf_int32 height, amf_bool fullscreen, amf::AMF_SURFACE_FORMAT format)
{
    AMF_RETURN_IF_FALSE(m_pSwapChainDecode != nullptr, AMF_NOT_INITIALIZED, L"CreateSwapChain() - m_pSwapChainDecode is not initialized");

    if (fullscreen)
    {
        AMFRect rect = GetOutputRect();
        width = rect.Width();
        height = rect.Height();
    }

    TerminateSwapChain();
    AMF_RESULT res = CreateSwapChain(m_pSurface, width, height, format);
    AMF_RETURN_IF_FAILED(res, L"Resize() - CreateSwapChain() failed");

    return AMF_OK;
}

amf_bool SwapChainDXGIDecode::FormatSupported(amf::AMF_SURFACE_FORMAT format)
{
    return format == AMF_SURFACE_NV12 || format == AMF_SURFACE_UNKNOWN;
}

AMF_RESULT SwapChainDXGIDecode::SetFormat(amf::AMF_SURFACE_FORMAT format)
{
    if (format == AMF_SURFACE_UNKNOWN)
    {
        format = AMF_SURFACE_NV12;
    }

    if (format != AMF_SURFACE_NV12)
    {
        return AMF_NOT_SUPPORTED;
    }

    if (m_hwnd != nullptr)
    {
        AMF_RESULT res = UpdateCurrentOutput();
        AMF_RETURN_IF_FAILED(res, L"SetFormat() - UpdateCurrentOutput() failed");

        CComQIPtr<IDXGIOutput3> spOutput3(m_pCurrentOutput);
        UINT flagsOverlay = 0;
        HRESULT hr = spOutput3->CheckOverlaySupport(DXGI_FORMAT_NV12, m_pDevice, &flagsOverlay);
        if (FAILED(hr) || flagsOverlay == 0)
        {
            return AMF_NOT_SUPPORTED;
        }
    }

    m_format = format;
    m_dxgiFormat = DXGI_FORMAT_NV12;
    return AMF_OK;
}