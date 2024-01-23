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
#include "SwapChainDX11.h"

using namespace amf;

#define AMF_FACILITY L"SwapChainDX11"

SwapChainDX11::SwapChainDX11(amf::AMFContext* pContext) :
    SwapChainDXGI(pContext),
    m_acquired(false)
{
    m_pBackBuffer = std::make_unique<BackBufferDX11>();
}

SwapChainDX11::~SwapChainDX11()
{
    Terminate();
}

AMF_RESULT SwapChainDX11::Init(amf_handle hwnd, amf_handle hDisplay, amf::AMFSurface* pSurface, amf_int32 width, amf_int32 height, amf::AMF_SURFACE_FORMAT format, amf_bool fullscreen, amf_bool hdr, amf_bool stereo)
{
    AMF_RESULT res = SwapChainDXGI::Init(hwnd, hDisplay, pSurface, width, height, format, fullscreen, hdr, stereo);
    AMF_RETURN_IF_FAILED(res, L"Init() - SwapChainDXGI::Init() failed");

    res = SetupSwapChain(width, height, format, fullscreen, hdr, stereo);
    AMF_RETURN_IF_FAILED(res, L"Init() - CreateSwapChain() failed");

    return AMF_OK;
}

AMF_RESULT SwapChainDX11::Terminate()
{
    m_acquired = false;
    DeleteFrameBuffers();
    return SwapChainDXGI::Terminate();
}

AMF_RESULT SwapChainDX11::GetDXGIInterface(amf_bool reinit)
{
    if (m_pDXGIFactory != nullptr && reinit == false)
    {
        return AMF_OK;
    }

    m_pDX11Device = nullptr;
    m_pDXGIDevice = nullptr;
    m_pDXGIFactory = nullptr;
    m_pDXGIFactory2 = nullptr;

    // Get D3D device
    m_pDX11Device = static_cast<ID3D11Device*>(m_pContext->GetDX11Device());
    AMF_RETURN_IF_FALSE(m_pDX11Device != nullptr, AMF_NO_DEVICE, L"GetDXGIInterface() - Failed to get D3D device");

    // Get DXGI device
    m_pDXGIDevice = m_pDX11Device;
    AMF_RETURN_IF_INVALID_POINTER(m_pDXGIDevice, L"GetDXGIInterface() - Failed to get DXGI device");

    // Traditionally, we would use the DX11 device to query for the DXGI adapter and DXGI factory used
    // to create it using GetParent(). However the problem with this method is that the factory used
    // to create the device can go out of date and will need to be recreated. This can occur when there
    // are changes to the adapter such as docking/undocking, connecting/disconnecting outputs, driver changes, etc.
    // To make the swapchain responsive to these changes, we have to check m_pDXGIFactory2 IsCurrent and recreate
    // the factory. The selected adapter must also be refreshed.

    // Create the factory
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_pDXGIFactory));
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"GetDXGIInterface() - CreateDXGIFactory1() failed");

    hr = m_pDXGIFactory->QueryInterface(&m_pDXGIFactory2);

    AMF_RESULT res = GetDXGIAdapters();
    AMF_RETURN_IF_FAILED(res, L"GetDXGIInterface() - GetDXGIAdapters() failed");

    return AMF_OK;
}

AMF_RESULT SwapChainDX11::GetDXGIDeviceAdapter(IDXGIAdapter** ppDXGIAdapter)
{
    if (m_pDXGIDevice != nullptr)
    {
        m_pDXGIDevice->GetAdapter( ppDXGIAdapter );
    }
    return AMF_OK;
}

AMF_RESULT SwapChainDX11::SetupSwapChain(amf_int32 width, amf_int32 height, amf::AMF_SURFACE_FORMAT format, amf_bool fullscreen, amf_bool hdr, amf_bool stereo)
{
    AMF_RETURN_IF_FALSE(m_pSwapChain == nullptr, AMF_ALREADY_INITIALIZED, L"SetupSwapChain() - m_pSwapChain is already initialized");

    AMF_RESULT res = GetDXGIInterface();
    AMF_RETURN_IF_FAILED(res, L"SetupSwapChain() - GetDXGIInterface() failed");
    AMF_RETURN_IF_FALSE(m_pDX11Device != nullptr, AMF_NOT_INITIALIZED, L"SetupSwapChain() - DX11 Device is not initialized.");

    res = SwapChainDXGI::CreateSwapChain(m_pDXGIDevice, width, height, format, BACK_BUFFER_COUNT, fullscreen, hdr, stereo);
    AMF_RETURN_IF_FAILED(res, L"SetupSwapChain() - SwapChainDXGI::CreateSwapChain() failed");

    res = CreateFrameBuffers();
    AMF_RETURN_IF_FAILED(res, L"SetupSwapChain() - CreateFrameBuffers() failed");

    return AMF_OK;
}

AMF_RESULT SwapChainDX11::CreateFrameBuffers()
{
    AMF_RETURN_IF_FALSE(m_pDX11Device != nullptr, AMF_NOT_INITIALIZED, L"SetupSwapChain() - DX11 Device is not initialized.");
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"SetupSwapChain() - m_pSwapChain is not initialized.");

    BackBuffer* pBackBuffer = static_cast<BackBuffer*>(m_pBackBuffer.get());
    AMF_RETURN_IF_FALSE(pBackBuffer->pBuffer == nullptr, AMF_ALREADY_INITIALIZED, L"CreateFrameBuffers() - m_backBuffer.pBuffer is already initialized");
    AMF_RETURN_IF_FALSE(pBackBuffer->pRTV_L == nullptr, AMF_ALREADY_INITIALIZED, L"CreateFrameBuffers() - m_backBuffer.pRTV_L is already initialized");
    AMF_RETURN_IF_FALSE(pBackBuffer->pRTV_R == nullptr, AMF_ALREADY_INITIALIZED, L"CreateFrameBuffers() - m_backBuffer.pRTV_R is already initialized");

    HRESULT hr = m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer->pBuffer));
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"ResizeSwapChain() - Failed to get swap chain buffer");

    D3D11_RENDER_TARGET_VIEW_DESC RenderTargetViewDescription = {};
    RenderTargetViewDescription.Format = GetDXGIFormat();
    RenderTargetViewDescription.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;                 // render target view is a Texture2D array
    RenderTargetViewDescription.Texture2DArray.MipSlice = 0;                                   // each array element is one Texture2D
    RenderTargetViewDescription.Texture2DArray.ArraySize = 1;
    RenderTargetViewDescription.Texture2DArray.FirstArraySlice = 0;                            // first Texture2D of the array is the left eye view

    hr = m_pDX11Device->CreateRenderTargetView(pBackBuffer->pBuffer, &RenderTargetViewDescription, &pBackBuffer->pRTV_L);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateFrameBuffers() - CreateRenderTargetView%sfailed", (m_stereoEnabled ? L" for the left eye view " : L" "));

    if (m_stereoEnabled)
    {
        RenderTargetViewDescription.Texture2DArray.FirstArraySlice = 1;                        // second Texture2D of the array is the right eye view
        hr = m_pDX11Device->CreateRenderTargetView(pBackBuffer->pBuffer, &RenderTargetViewDescription, &pBackBuffer->pRTV_R);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateFrameBuffers() - CreateRenderTargetView for the right eye view failed");
    }

    return AMF_OK;
}

AMF_RESULT SwapChainDX11::DeleteFrameBuffers()
{
    CComPtr<ID3D11DeviceContext> spContext;
    m_pDX11Device->GetImmediateContext(&spContext);
    spContext->Flush();

    *((BackBuffer*)m_pBackBuffer.get()) = {};
    return AMF_OK;
}

AMF_RESULT SwapChainDX11::Present(amf_bool waitForVSync)
{
    AMF_RESULT res = SwapChainDXGI::Present(waitForVSync);
    AMF_RETURN_IF_FAILED(res, L"Present() failed");
    m_acquired = false;

    return AMF_OK;
}

AMF_RESULT SwapChainDX11::Resize(amf_int32 width, amf_int32 height, amf_bool fullscreen, amf::AMF_SURFACE_FORMAT format)
{
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"Resize() - Swapchain is not initialized");

    AMF_RESULT res = DeleteFrameBuffers();
    AMF_RETURN_IF_FAILED(res, L"Resize() - DeleteFrameBuffers() failed");

    res = SwapChainDXGI::Resize(width, height, fullscreen, format);
    AMF_RETURN_IF_FAILED(res, L"Resize() - SwapChainDXGI::ResizeSwapChain() failed");

    res = CreateFrameBuffers();
    AMF_RETURN_IF_FAILED(res, L"Resize() - CreateFrameBuffers() failed");

    return AMF_OK;
}

AMF_RESULT SwapChainDX11::AcquireNextBackBufferIndex(amf_uint& index)
{
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"AcquireNextBackBuffer() - m_pSwapChain is not initialized");
    index = 0;
    return AMF_OK;
}

AMF_RESULT SwapChainDX11::DropLastBackBuffer()
{
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"AcquireNextBackBuffer() - m_pSwapChain is not initialized");

    return DropBackBufferIndex(0U);
}

AMF_RESULT SwapChainDX11::DropBackBufferIndex(amf_uint index)
{
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"AcquireNextBackBuffer() - m_pSwapChain is not initialized");

    if (index > 0)
    {
        return AMF_OK;
    }

    m_acquired = false;
    return AMF_OK;
}

AMF_RESULT SwapChainDX11::BackBufferToSurface(const BackBufferBase* pBuffer, amf::AMFSurface** ppSurface) const
{
    return m_pContext->CreateSurfaceFromDX11Native(pBuffer->GetNative(), ppSurface, nullptr);
}

AMFSize BackBufferDX11::GetSize() const
{
    if (pBuffer == nullptr)
    {
        return {};
    }

    D3D11_TEXTURE2D_DESC desc = {};
    pBuffer->GetDesc(&desc);
    return AMFConstructSize(desc.Width, desc.Height);
}
