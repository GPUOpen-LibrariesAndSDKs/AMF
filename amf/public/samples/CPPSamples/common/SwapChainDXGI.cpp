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


#include "SwapChainDXGI.h"

#include <public/common/TraceAdapter.h>
#include <public/samples/CPPSamples/common/CmdLogger.h>

#define AMF_FACILITY L"SwapChainDXGI"

using namespace amf;

SwapChainDXGI::SwapChainDXGI(amf::AMFContext* pContext) :
    SwapChain(pContext),
    m_dxgiFormat(DXGI_FORMAT_UNKNOWN),
    m_currentHDREnableState(false)
{
    SetFormat(AMF_SURFACE_UNKNOWN); // Set default format
}

SwapChainDXGI::~SwapChainDXGI()
{
    Terminate();
}

AMF_RESULT SwapChainDXGI::Init(amf_handle hwnd, amf_handle hDisplay, AMFSurface* /*pSurface*/, amf_int32 width, amf_int32 height,
                               AMF_SURFACE_FORMAT /*format*/, amf_bool /*fullscreen*/, amf_bool /*hdr*/, amf_bool /*stereo*/)
{
    AMF_RETURN_IF_FALSE(hwnd != nullptr, AMF_INVALID_ARG, L"Init() - Window handle is NULL");
    AMF_RETURN_IF_FALSE(width >= 0 && height >= 0, AMF_INVALID_ARG, L"Init() - Invalid width/height: width=%d height=%d", width, height);

    m_hwnd = hwnd;
    m_hDisplay = hDisplay;

    return AMF_OK;
}

AMF_RESULT SwapChainDXGI::Terminate()
{
    if (m_pSwapChain != nullptr)
    {
        SetExclusiveFullscreenState(false);
    }

    m_pDXGIDevice = nullptr;
    m_pDXGIAdapters.clear();
    m_pDXGIFactory = nullptr;
    m_pDXGIFactory2 = nullptr;

    m_pSwapChain = nullptr;
    m_pSwapChain1 = nullptr;
    m_pSwapChain3 = nullptr;

    m_pDXGIAdapters.clear();
    m_pOutputs.clear();
    m_pCurrentOutput = nullptr;
    m_pCurrentOutput6 = nullptr;

    m_dxgiFormat = DXGI_FORMAT_UNKNOWN;
    m_currentHDREnableState = false;

    return SwapChain::Terminate();
}

AMF_RESULT SwapChainDXGI::CreateLegacySwapChain(IUnknown* pDevice, amf_int32 width, amf_int32 height, amf_uint bufferCount)
{
    AMF_RETURN_IF_FALSE(pDevice != nullptr, AMF_INVALID_ARG, L"CreateLegacySwapChain() - pDevice is NULL");
    AMF_RETURN_IF_FALSE(m_pSwapChain == nullptr, AMF_ALREADY_INITIALIZED, L"CreateLegacySwapChain() - m_pSwapChain is already initialized");
    AMF_RETURN_IF_FALSE(m_pDXGIFactory != nullptr, AMF_NOT_INITIALIZED, L"CreateLegacySwapChain() - m_pDXGIFactory is not initialized");
    AMF_RETURN_IF_FALSE(m_hwnd != nullptr, AMF_NOT_INITIALIZED, L"CreateLegacySwapChain() - m_hwnd is NULL");

    // Setup params
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = bufferCount;
    sd.BufferDesc.Width = width;
    sd.BufferDesc.Height = height;
    sd.BufferDesc.Format = GetDXGIFormat();
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    //        sd.BufferUsage = DXGI_USAGE_SHADER_INPUT | DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHARED;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

    sd.OutputWindow = (HWND)m_hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;

    // It is not recommended to create a fullscreen swapchain, just set to fullscreen state later
    sd.Windowed = FALSE; // fullscreen ? FALSE : TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    // sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    HRESULT hr = m_pDXGIFactory->CreateSwapChain(pDevice, &sd, &m_pSwapChain);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateLegacySwapChain() - CreateSwapChain() failed");

    m_pSwapChain.QueryInterface(&m_pSwapChain1);
    m_pSwapChain1->QueryInterface(&m_pSwapChain3);
    m_stereoEnabled = false;

    return AMF_OK;
}

AMF_RESULT SwapChainDXGI::CreateSwapChainForHwnd(IUnknown* pDevice, amf_int32 width, amf_int32 height, amf_uint bufferCount, amf_bool stereo)
{
    AMF_RETURN_IF_FALSE(pDevice != nullptr, AMF_INVALID_ARG, L"CreateSwapChainForHwnd() - pDevice is NULL");
    AMF_RETURN_IF_FALSE(m_pSwapChain == nullptr, AMF_ALREADY_INITIALIZED, L"CreateSwapChainForHwnd() - swap chain is already initialized");
    AMF_RETURN_IF_FALSE(m_pSwapChain1 == nullptr, AMF_ALREADY_INITIALIZED, L"CreateSwapChainForHwnd() - swap chain is already initialized");
    AMF_RETURN_IF_FALSE(m_pDXGIFactory2 != nullptr, AMF_NOT_INITIALIZED, L"CreateSwapChainForHwnd() - m_pDXGIFactory2 is not initialized");
    AMF_RETURN_IF_FALSE(m_hwnd != nullptr, AMF_NOT_INITIALIZED, L"CreateSwapChainForHwnd() - m_hwnd is NULL");

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = bufferCount;
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = GetDXGIFormat();
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; // DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    //swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    swapChainDesc.Scaling = DXGI_SCALING_NONE;
    swapChainDesc.Stereo = stereo ? TRUE : FALSE;

    HRESULT hr = m_pDXGIFactory2->CreateSwapChainForHwnd(
        pDevice,
        (HWND)m_hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &m_pSwapChain1);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateSwapChainForHwnd() - CreateSwapChainForHwnd() failed to create swapchain.");

    m_pSwapChain = m_pSwapChain1;
    m_pSwapChain1->QueryInterface(&m_pSwapChain3);
    m_stereoEnabled = stereo;

    return AMF_OK;
}

AMF_RESULT SwapChainDXGI::CreateSwapChain(IUnknown* pDevice, amf_int32 width, amf_int32 height,  amf::AMF_SURFACE_FORMAT format, amf_uint bufferCount, amf_bool fullscreen, amf_bool hdr, amf_bool stereo)
{
    AMF_RETURN_IF_FALSE(pDevice != nullptr, AMF_INVALID_ARG, L"CreateSwapChain() - pDevice is NULL");
    AMF_RETURN_IF_FALSE(m_pSwapChain == nullptr, AMF_ALREADY_INITIALIZED, L"CreateSwapChain() - swap chain is already initialized");

    AMF_RESULT res = GetDXGIInterface();
    AMF_RETURN_IF_FAILED(res, L"CreateSwapChain() - GetDXGIInterface() failed");
    AMF_RETURN_IF_FALSE(m_pDXGIFactory != nullptr, AMF_NOT_INITIALIZED, L"CreateSwapChain() - m_pDXGIFactory is NULL"); // Need at least DXGIFactory even if we can't get DXGIFactory2

    res = SetFormat(format);
    AMF_RETURN_IF_FAILED(res, L"CreateSwapChain() - SetFormat() failed");

    if (fullscreen)
    {
        const AMFRect outputRect = GetOutputRect();
        width = outputRect.Width();
        height = outputRect.Height();
    }

    // The IDXGIFactory2 interface includes methods to create a newer version
    // swap chain with more features than IDXGISwapChain and to monitor
    // stereoscopic 3D capabilities.
    // If available, we want to create the swap chain using the newer methods
    // provided by the DXGIFactory2 interface. This includes creating a swap chain
    // for the window using the CreateSwapChainForHwnd method.
    if (m_pDXGIFactory2 != nullptr)
    {
        res = CreateSwapChainForHwnd(pDevice, width, height, bufferCount, stereo);
        AMF_RETURN_IF_FALSE(res == AMF_OK || stereo == false, AMF_NOT_SUPPORTED, L"CreateSwapChain() - CreateSwapChainForHwnd() failed to create stereo swapchain");
    }

    if (m_pSwapChain == nullptr)
    {
        res = CreateLegacySwapChain(pDevice, width, height, bufferCount);
        AMF_RETURN_IF_FAILED(res, L"CreateSwapChain() - CreateLegacySwapChain() failed to create swapchain");
    }

    // When tearing support is enabled we will handle ALT+Enter key presses in the
    // window message loop rather than let DXGI handle it by calling SetFullscreenState.
    //m_pDXGIFactory->MakeWindowAssociation((HWND)m_hwnd, DXGI_MWA_NO_ALT_ENTER); MakeWindowAssociation requires adapter parent factory
    ATL::CComPtr<IDXGIAdapter> pDXGIAdapter;
    GetDXGIDeviceAdapter( &pDXGIAdapter );

    ATL::CComPtr<IDXGIFactory> pDXGIFactory;
    if (pDXGIAdapter != nullptr)
    {
        pDXGIAdapter->GetParent(__uuidof(IDXGIFactory), (void **)&pDXGIFactory);
    }
    // if no parent, use existing
    if (pDXGIFactory == nullptr)
    {
        pDXGIFactory = m_pDXGIFactory;
    }
    pDXGIFactory->MakeWindowAssociation((HWND)m_hwnd, DXGI_MWA_NO_ALT_ENTER);

    res = EnableHDR(hdr);
    AMF_RETURN_IF_FALSE(res == AMF_OK || res == AMF_NOT_SUPPORTED, res, L"CreateSwapChain() - EnableHDR() failed");

    res = UpdateCurrentOutput();
    AMF_RETURN_IF_FAILED(res, L"CreateSwapChain() - UpdateCurrentOutput() failed");

    return AMF_OK;
}

AMF_RESULT SwapChainDXGI::Present(amf_bool waitForVSync)
{
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"Present() - SwapChain is not initialized");

    UINT presentFlags = DXGI_PRESENT_RESTART;
    UINT syncInterval = 0;
    if (waitForVSync == false)
    {
        presentFlags |= DXGI_PRESENT_DO_NOT_WAIT;
    }

    // Present the frame.
    for (amf_int i = 0; i < 100; i++)
    {
        // If the GPU is busy at the moment present was called and if
        // it did not execute or schedule the operation, we get the
        // DXGI_ERROR_WAS_STILL_DRAWING error. Therefore we should try
        // presenting again after a delay
        HRESULT hr = m_pSwapChain->Present(syncInterval, presentFlags);
        if (hr != DXGI_ERROR_WAS_STILL_DRAWING)
        {
            ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"Present() - swapchain Present() failed");
            break;
        }
        if (waitForVSync == false)
        {
            // When input framerate exceeds presenter framerate, waiting for frames will
            // stall the pipeline.
            //
            // If presenter is set to not wait for vsync, and presenter busy, drop frame instead of waiting.
            break;
        }
        amf_sleep(1);
    }

    AMF_RESULT res = UpdateCurrentOutput();
    AMF_RETURN_IF_FAILED(res, L"Present() - UpdateCurrentOutput()")

    return AMF_OK;
}

AMF_RESULT SwapChainDXGI::Resize(amf_int32 width, amf_int32 height, amf_bool fullscreen, AMF_SURFACE_FORMAT format)
{
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"ResizeSwapChain() - m_pSwapChain is not initialized");

    // Keep original format if unknown passed in
    if (format != AMF_SURFACE_UNKNOWN)
    {
        AMF_RESULT res = SetFormat(format);
        AMF_RETURN_IF_FAILED(res, L"ResizeSwapChain() - SetFormat() failed");
    }

    AMF_RESULT res = UpdateCurrentOutput();
    AMF_RETURN_IF_FAILED(res, L"ResizeSwapChain() - UpdateCurrentOutput() failed");

    // Ignore passed width/height and take output size when fullscreen
    if (fullscreen == true)
    {
        const AMFRect rect = GetOutputRect();
        width = rect.Width();
        height = rect.Height();
    }

    HRESULT hr = m_pSwapChain->ResizeBuffers(0, width, height, GetDXGIFormat(), 0);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"Resize() - SwapChain ResizeBuffers() failed.");

    return AMF_OK;
}

AMFSize SwapChainDXGI::GetSize()
{
    if (m_pSwapChain == nullptr)
    {
        return AMFConstructSize(0, 0);
    }

    AMFSize size = {};
    DXGI_SWAP_CHAIN_DESC desc = {};
    m_pSwapChain->GetDesc(&desc);
    return AMFConstructSize(desc.BufferDesc.Width, desc.BufferDesc.Height);
}

amf_bool SwapChainDXGI::FormatSupported(amf::AMF_SURFACE_FORMAT format)
{
    return GetSupportedDXGIFormat(format) != DXGI_FORMAT_UNKNOWN;
}

AMF_RESULT SwapChainDXGI::SetFormat(amf::AMF_SURFACE_FORMAT format)
{
    DXGI_FORMAT dxgiFormat = GetSupportedDXGIFormat(format);
    AMF_RETURN_IF_FALSE(dxgiFormat != DXGI_FORMAT_UNKNOWN, AMF_NOT_SUPPORTED, L"SetFormat() - Format (%s) not supported", AMFSurfaceGetFormatName(format));

    m_format = format == AMF_SURFACE_UNKNOWN ? AMF_SURFACE_BGRA : format;
    m_dxgiFormat = dxgiFormat;
    return AMF_OK;
}

DXGI_FORMAT SwapChainDXGI::GetSupportedDXGIFormat(AMF_SURFACE_FORMAT format) const
{
    switch (format)
    {
    case amf::AMF_SURFACE_UNKNOWN:      return DXGI_FORMAT_B8G8R8A8_UNORM;      // Default swapchain preference
    case amf::AMF_SURFACE_BGRA:         return DXGI_FORMAT_B8G8R8A8_UNORM;
    case amf::AMF_SURFACE_RGBA:         return DXGI_FORMAT_R8G8B8A8_UNORM;
    case amf::AMF_SURFACE_RGBA_F16:     return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case amf::AMF_SURFACE_R10G10B10A2:  return DXGI_FORMAT_R10G10B10A2_UNORM;
    }

    return DXGI_FORMAT_UNKNOWN;
}

amf_bool SwapChainDXGI::GetExclusiveFullscreenState()
{
    if (m_hwnd == nullptr || m_pSwapChain == nullptr)
    {
        return false;
    }

    // Fullscreen is not allowed for swapchains targetting a child window
    // Only set the fullscreen state if we have a root window handle
    if (m_hwnd == GetAncestor((HWND)m_hwnd, GA_ROOT))
    {
        BOOL fullscreen = FALSE;
        HRESULT hr = m_pSwapChain->GetFullscreenState(&fullscreen, nullptr);
        if (FAILED(hr))
        {
            AMFTraceError(AMF_FACILITY, L"GetFullScreenState() - swapchain GetFullscreenState() failed");
        }

        return fullscreen == TRUE;
    }

    return false;
}

AMF_RESULT SwapChainDXGI::SetExclusiveFullscreenState(amf_bool fullscreen)
{
    if (m_pSwapChain == nullptr)
    {
        return AMF_NOT_INITIALIZED;
    }
    AMF_RETURN_IF_FALSE(m_hwnd != nullptr, AMF_NOT_INITIALIZED, L"SetFullscreenState() - m_hwnd is not initialized");
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"SetFullscreenState() - m_pSwapChain is not initialized");

    if (GetExclusiveFullscreenState() == fullscreen)
    {
        return AMF_OK;
    }

    // Fullscreen is not allowed for swapchains targetting a child window
    // Only set the fullscreen state if we have a root window handle
    if (m_hwnd == GetAncestor((HWND)m_hwnd, GA_ROOT))
    {
        HRESULT hr = m_pSwapChain->SetFullscreenState(fullscreen ? TRUE : FALSE, nullptr);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetFullscreenState() - SetFullscreenState failed");
    }

    return AMF_OK;
}

amf_bool SwapChainDXGI::HDRSupported()
{
    if (m_pSwapChain == nullptr)
    {
        AMFTraceWarning(AMF_FACILITY, L"DeviceHDRSupported() - Cannot check HDR support before initializing swapchain");
        return false;
    }

    return OSHDRSupported() && m_pSwapChain3 != nullptr;
}

AMF_RESULT SwapChainDXGI::SetHDRMetaData(const AMFHDRMetadata* pHDRMetaData)
{
    if (HDRSupported() == false)
    {
        return AMF_NOT_SUPPORTED;
    }

    ATL::CComQIPtr<IDXGISwapChain4> pSwapChain4(m_pSwapChain);

    if (pSwapChain4 == nullptr)
    {
        return AMF_NOT_SUPPORTED;
    }

    DXGI_HDR_METADATA_HDR10 metadata = {}; // These are already normalized to 500000 for primaries and 100000 for luminance
    metadata.WhitePoint[0]              = pHDRMetaData->whitePoint[0];
    metadata.WhitePoint[1]              = pHDRMetaData->whitePoint[1];
    metadata.RedPrimary[0]              = pHDRMetaData->redPrimary[0];
    metadata.RedPrimary[1]              = pHDRMetaData->redPrimary[1];
    metadata.GreenPrimary[0]            = pHDRMetaData->greenPrimary[0];
    metadata.GreenPrimary[1]            = pHDRMetaData->greenPrimary[1];
    metadata.BluePrimary[0]             = pHDRMetaData->bluePrimary[0];
    metadata.BluePrimary[1]             = pHDRMetaData->bluePrimary[1];
    metadata.MaxMasteringLuminance      = pHDRMetaData->maxMasteringLuminance;
    metadata.MinMasteringLuminance      = pHDRMetaData->minMasteringLuminance;
    metadata.MaxContentLightLevel       = pHDRMetaData->maxContentLightLevel;
    metadata.MaxFrameAverageLightLevel  = pHDRMetaData->maxFrameAverageLightLevel;

    HRESULT hr = pSwapChain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(metadata), &metadata);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetHDRMetaData() - Swapchain SetHDRMetaData() failed");

    return UpdateCurrentOutput();
}

amf_bool SwapChainDXGI::StereoSupported()
{
    AMF_RESULT res = GetDXGIInterface();
    if (res != AMF_OK)
    {
        AMFTraceError(AMF_FACILITY, L"StereoSupported() - Failed to get DXGI factory");
        return false;
    }

    return m_pDXGIFactory2 != nullptr;
}

template<typename Enumerator_T=branch, typename Interface_T>
static AMF_RESULT EnumDXGIInterface(Enumerator_T* pEnumerator, HRESULT (__stdcall Enumerator_T::*enumFunc)(UINT, Interface_T**), amf::amf_vector<CComPtr<Interface_T>>& pInterfaces)
{
    for (UINT i = 0;; ++i)
    {
        CComPtr<Interface_T> pInterface;
        HRESULT hr = (pEnumerator->*enumFunc)(i, &pInterface);
        if (FAILED(hr) || pInterface == nullptr)
        {
            break;
        }

        pInterfaces.push_back(pInterface);
    }

    return AMF_OK;
}

AMF_RESULT SwapChainDXGI::GetDXGIAdapters()
{
    AMF_RETURN_IF_FALSE(m_pDXGIFactory != nullptr, AMF_NOT_INITIALIZED, L"GetDXGIAdapters() - DXGI factor not initialized");

    m_pDXGIAdapters.clear();
    EnumDXGIInterface<IDXGIFactory, IDXGIAdapter>(m_pDXGIFactory, &IDXGIFactory::EnumAdapters, m_pDXGIAdapters);
    AMF_RETURN_IF_FALSE(m_pDXGIAdapters.empty() == false, AMF_FAIL, L"GetDXGIInterface() - Failed to find any adapters");
    return AMF_OK;
}

AMF_RESULT SwapChainDXGI::UpdateOutputs()
{
    AMF_RETURN_IF_FALSE(m_pDXGIAdapters.empty() == false, AMF_NOT_INITIALIZED, L"UpdateOutputs() - Adapters are not initialized");

    m_pOutputs.clear();

    for (IDXGIAdapter* pDXGIAdapter : m_pDXGIAdapters)
    {
        EnumDXGIInterface<IDXGIAdapter, IDXGIOutput>(pDXGIAdapter, &IDXGIAdapter::EnumOutputs, m_pOutputs);
    }

    return AMF_OK;
}

AMF_RESULT SwapChainDXGI::UpdateCurrentOutput()
{
    AMF_RETURN_IF_FALSE(m_hwnd != nullptr, AMF_NOT_INITIALIZED, L"UpdateCurrentOutput() - m_hwnd is not initialized");

    // When IsCurrent returns FALSE, then the DXGI adapter and factory are outdated
    // These interfaces store cache data on the output state and need to be reinitialized
    if (m_pDXGIFactory2->IsCurrent() == FALSE)
    {
        GetDXGIInterface(true);
        m_pOutputs.clear();
    }

    if (m_pOutputs.empty())
    {
        AMF_RESULT res = UpdateOutputs();
        AMF_RETURN_IF_FAILED(res, L"UpdateCurrentOutput() - UpdateOutputs() failed");
        m_pCurrentOutput = nullptr;
    }

    // Sets the current output by checking which output
    // corresponds to the monitor displaying the window
    const HMONITOR hMonitor = MonitorFromWindow((HWND)m_hwnd, MONITOR_DEFAULTTONEAREST);
    AMF_RETURN_IF_FALSE(hMonitor != NULL, AMF_FAIL, L"UpdateCurrentOutput() - hMonitor NULL");


    // Don't search for new output
    if (m_pCurrentOutput != nullptr)
    {
        DXGI_OUTPUT_DESC desc = {};
        HRESULT hr = m_pCurrentOutput->GetDesc(&desc);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"UpdateCurrentOutput() - GetDesc() failed");
        if (hMonitor != desc.Monitor)
        {
            m_pCurrentOutput = nullptr;
            m_pCurrentOutput6 = nullptr;
        }
    }

    amf_bool changed = false;

    if (m_pCurrentOutput == nullptr)
    {
        for (size_t i = 0; i < m_pOutputs.size(); ++i)
        {
            DXGI_OUTPUT_DESC outputDesc = {};
            m_pOutputs[i]->GetDesc(&outputDesc);

            if (outputDesc.Monitor == hMonitor)
            {
                m_pCurrentOutput = m_pOutputs[i];
                changed = true;
                break;
            }
        }
    }

    AMF_RETURN_IF_FALSE(m_pCurrentOutput != nullptr, AMF_NOT_FOUND, L"UpdateCurrentOutput() - Failed to find output");

    m_pCurrentOutput6 = nullptr;
    m_pCurrentOutput->QueryInterface(&m_pCurrentOutput6);

    if (changed || m_currentHDREnableState != HDREnabled())
    {
        AMF_RESULT res = UpdateColorSpace();
        AMF_RETURN_IF_FAILED(res, L"UpdateCurrentOutput() - UpdateColorSpace() failed");
        m_currentHDREnableState = HDREnabled();
    }

    return AMF_OK;
}

AMFRect SwapChainDXGI::GetOutputRect()
{
    AMF_RESULT res = UpdateCurrentOutput();
    if (res != AMF_OK || m_pCurrentOutput == nullptr)
    {
        LOG_AMF_ERROR(res, L"GetOutputMonitorRect() - UpdateCurrentOutput() failed");
        return AMFConstructRect(0, 0, 0, 0);
    }

    DXGI_OUTPUT_DESC outputDesc = {};
    HRESULT hr = m_pCurrentOutput->GetDesc(&outputDesc);

    if (FAILED(hr))
    {
        LOG_AMF_ERROR(AMF_DIRECTX_FAILED, L"GetOutputMonitorRect() - Failed to get output description");
        return AMFConstructRect(0, 0, 0, 0);
    }

    RECT& outputRect = outputDesc.DesktopCoordinates;
    return AMFConstructRect(outputRect.left, outputRect.top, outputRect.right, outputRect.bottom);
}

AMF_RESULT SwapChainDXGI::UpdateColorSpace()
{
    if (m_pCurrentOutput6 == nullptr || m_pSwapChain3 == nullptr)
    {
        m_colorSpace = {};
        return AMF_OK;
    }

    DXGI_OUTPUT_DESC1 desc = {};
    HRESULT hr = m_pCurrentOutput6->GetDesc1(&desc);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"UpdateColorSpace() - m_pCurrentOutput6->GetDesc1() failed");
    DXGI_COLOR_SPACE_TYPE dxgiColorSpace = DXGI_COLOR_SPACE_CUSTOM;

    // On Windows, the Swapchain can set whatever colorspace it wants as long as its supported
    // When in HDR mode, the desktop window manager (DWM) converts all app colorspaces to
    // CCCS using the scRGB colorspace (Linear gamma and BT.709/sRGB primaries) for all blending and
    // merging operations. The resulting framebuffers are then converted into the appropiate colorspace
    // for the monitor in the display kernel. For HDR, this is BT.2100 ST.2084 (DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
    //
    // To enable HDR support, there are two main combinations of colorspace/format supported by DXGI
    //
    // 1. RGBA_F16 format with the scRGB colorspace (CCCS). This is the default format used in the DWM and is always available
    // 2. R10G10B10A2 (UINT10) format with BT.2100 colorspace. This is only supported when HDR is enabled and blending isn't required

    amf_bool hdrMode = false;
    if (HDREnabled() && desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) // HDR
    {
        if (GetFormat() == amf::AMF_SURFACE_RGBA_F16)
        {
            dxgiColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
            hdrMode = true;
        }
        else if (GetFormat() == amf::AMF_SURFACE_R10G10B10A2)
        {
            dxgiColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
            hdrMode = true;
        }
    }

    if (dxgiColorSpace == DXGI_COLOR_SPACE_CUSTOM)
    {
        // G24 transfer isn't supported by AMF converter, need testing
        constexpr DXGI_COLOR_SPACE_TYPE supportedColorSpaces[] =
        {
            DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,       DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709,     DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709,
            DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020,    DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020,  DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020,
            DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020,      DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709,   DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020
        };

        for (DXGI_COLOR_SPACE_TYPE cs : supportedColorSpaces)
        {
            UINT support = 0;
            m_pSwapChain3->CheckColorSpaceSupport(cs, &support);

            if ((support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)
            {
                dxgiColorSpace = cs;
                break;
            }
        }
    }

    //AMFTraceInfo(AMF_FACILITY, L"Using colorspace: %s", GetDXGIColorSpaceName(dxgiColorSpace));

    if (dxgiColorSpace != DXGI_COLOR_SPACE_CUSTOM)
    {
        AMF_RESULT res = DXGIToAMFColorSpaceType(dxgiColorSpace, m_colorSpace);
        AMF_RETURN_IF_FAILED(res, L"UpdateColorSpace() - DXGIToAMFColorSpaceType() failed");

        if (hdrMode && GetFormat() == amf::AMF_SURFACE_RGBA_F16) // Used with RGBA_F16 HDR
        {
            // By default, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 is set to AMF_COLOR_PRIMARIES_BT709
            // For HDR however, we want scRGB or CCCS to allow primary coordinates outside the BT709 range
            m_colorSpace.primaries = AMF_COLOR_PRIMARIES_CCCS;
        }

        hr = m_pSwapChain3->SetColorSpace1(dxgiColorSpace);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"UpdateColorSpace() - SetColorSpace1() failed");
    }
    else
    {
        m_colorSpace.transfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_UNDEFINED;
        m_colorSpace.primaries = AMF_COLOR_PRIMARIES_UNDEFINED;
        m_colorSpace.range = AMF_COLOR_RANGE_UNDEFINED;
    }

    // Set HDR metadata

    // HDR metadata is available for both SDR and HDR displays
    // Display HDR metadata is normalized to 0-1.0. Need normalization to 50000 for primaries and 10000 for luminance
    m_outputHDRMetaData.redPrimary[0]               = amf_uint16(desc.RedPrimary[0]     * 50000.f);
    m_outputHDRMetaData.redPrimary[1]               = amf_uint16(desc.RedPrimary[1]     * 50000.f);

    m_outputHDRMetaData.greenPrimary[0]             = amf_uint16(desc.GreenPrimary[0]   * 50000.f);
    m_outputHDRMetaData.greenPrimary[1]             = amf_uint16(desc.GreenPrimary[1]   * 50000.f);

    m_outputHDRMetaData.bluePrimary[0]              = amf_uint16(desc.BluePrimary[0]    * 50000.f);
    m_outputHDRMetaData.bluePrimary[1]              = amf_uint16(desc.BluePrimary[1]    * 50000.f);

    m_outputHDRMetaData.whitePoint[0]               = amf_uint16(desc.WhitePoint[0]     * 50000.f);
    m_outputHDRMetaData.whitePoint[1]               = amf_uint16(desc.WhitePoint[1]     * 50000.f);

    m_outputHDRMetaData.maxMasteringLuminance       = amf_uint32(desc.MaxLuminance      * 10000.f);
    m_outputHDRMetaData.minMasteringLuminance       = amf_uint32(desc.MinLuminance      * 10000.f);
    m_outputHDRMetaData.maxContentLightLevel        = 0;
    m_outputHDRMetaData.maxFrameAverageLightLevel   = amf_uint16(desc.MaxFullFrameLuminance * 10000.f);

    return AMF_OK;
}

AMF_RESULT DXGIToAMFColorSpaceType(DXGI_COLOR_SPACE_TYPE dxgiColorSpace, SwapChain::ColorSpace& amfColorSpace)
{
    amfColorSpace.transfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_UNDEFINED;
    amfColorSpace.primaries = AMF_COLOR_PRIMARIES_UNDEFINED;
    amfColorSpace.range = AMF_COLOR_RANGE_UNDEFINED;

    // We don't support YCbCr colorspaces for swapchain
    switch (dxgiColorSpace)
    {
    case  DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
        amfColorSpace.transfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_GAMMA22;
        amfColorSpace.primaries = AMF_COLOR_PRIMARIES_BT709;
        amfColorSpace.range = AMF_COLOR_RANGE_FULL;
        break;

    case  DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:
        amfColorSpace.transfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_LINEAR;
        amfColorSpace.primaries = AMF_COLOR_PRIMARIES_BT709;
        amfColorSpace.range = AMF_COLOR_RANGE_FULL;
        break;

    case  DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709:
        amfColorSpace.transfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_GAMMA22;
        amfColorSpace.primaries = AMF_COLOR_PRIMARIES_BT709;
        amfColorSpace.range = AMF_COLOR_RANGE_STUDIO;
        break;

    case  DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020:
        amfColorSpace.transfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_GAMMA22;
        amfColorSpace.primaries = AMF_COLOR_PRIMARIES_BT2020;
        amfColorSpace.range = AMF_COLOR_RANGE_STUDIO;
        break;

//    case  DXGI_COLOR_SPACE_RESERVED:
//    case  DXGI_COLOR_SPACE_YCBCR_FULL_G22_NONE_P709_X601:
//    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601:
//    case  DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601:
//    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709:
//    case  DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709:
//    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020:
//    case  DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020:


    case  DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
        amfColorSpace.transfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE2084;
        amfColorSpace.primaries = AMF_COLOR_PRIMARIES_BT2020;
        amfColorSpace.range = AMF_COLOR_RANGE_FULL;
        break;

//    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020:

    case  DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020:
        amfColorSpace.transfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE2084;
        amfColorSpace.primaries = AMF_COLOR_PRIMARIES_BT2020;
        amfColorSpace.range = AMF_COLOR_RANGE_STUDIO;
        break;

//    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020:
//    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020:

    case  DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020:
        amfColorSpace.transfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_GAMMA22;
        amfColorSpace.primaries = AMF_COLOR_PRIMARIES_BT2020;
        amfColorSpace.range = AMF_COLOR_RANGE_FULL;
        break;

//    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020:
//    case  DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020:

 // Todo: AMF doesn't have 2.4 Gamma transfer, is it fine to leave as UNDEFINED?
    case  DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709:
//         amfColorSpace.transfer =
        amfColorSpace.primaries = AMF_COLOR_PRIMARIES_BT709;
        amfColorSpace.range = AMF_COLOR_RANGE_STUDIO;
        break;

    case  DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020:
//         amfColorSpace.transfer =
        amfColorSpace.primaries = AMF_COLOR_PRIMARIES_BT2020;
        amfColorSpace.range = AMF_COLOR_RANGE_STUDIO;
        break;

//    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P709:
//    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P2020:
//    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_TOPLEFT_P2020:
    }

    return AMF_OK;
}

const wchar_t* GetDXGIColorSpaceName(DXGI_COLOR_SPACE_TYPE colorSpace)
{
#define NAME_ENTRY(cs) \
    case cs:\
        return L#cs;

    switch (colorSpace)
    {

    NAME_ENTRY(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)
    NAME_ENTRY(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709)
    NAME_ENTRY(DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709)
    NAME_ENTRY(DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020)
    NAME_ENTRY(DXGI_COLOR_SPACE_RESERVED)
    NAME_ENTRY(DXGI_COLOR_SPACE_YCBCR_FULL_G22_NONE_P709_X601)
    NAME_ENTRY(DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601)
    NAME_ENTRY(DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601)
    NAME_ENTRY(DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709)
    NAME_ENTRY(DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709)
    NAME_ENTRY(DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020)
    NAME_ENTRY(DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020)
    NAME_ENTRY(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
    NAME_ENTRY(DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020)
    NAME_ENTRY(DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020)
    NAME_ENTRY(DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020)
    NAME_ENTRY(DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020)
    NAME_ENTRY(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020)
    NAME_ENTRY(DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020)
    NAME_ENTRY(DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020)
    NAME_ENTRY(DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709)
    NAME_ENTRY(DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020)
    NAME_ENTRY(DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P709)
    NAME_ENTRY(DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P2020)
    NAME_ENTRY(DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_TOPLEFT_P2020)

    default:
        return L"Undefined Color Space";
    }

#undef NAME_ENTRY
}
