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

//#include "stdafx.h"
#include "SwapChainDX12.h"
#include "d3dx12.h"
#include "public/common/TraceAdapter.h"
#include "public/common/Thread.h"

#define AMF_FACILITY L"SwapChainDX12"

SwapChainDX12::SwapChainDX12(amf::AMFContext* pContext) :
    m_pContext(pContext),
    m_rtvDescriptorSize(0),
    m_pSwapChain(nullptr),
    m_frameIndex(0),
    m_format(DXGI_FORMAT_UNKNOWN),
    m_fenceValues{ 0 },
    m_fenceEvent(nullptr),
    m_eSwapChainImageFormat(0),
    m_cbvSrvDescriptorSize(0),
    m_SwapChainExtent{ 0 },
    m_staticSampler{}
{
}
SwapChainDX12::~SwapChainDX12()
{
    Terminate();
}

AMF_RESULT SwapChainDX12::Init(amf_handle hWnd, amf_handle hDisplay, bool bFullScreen, amf_int32 width, amf_int32 height, amf_uint32 format)
{
    AMF_RESULT res = AMF_OK;

    AMF_RETURN_IF_FALSE(width != 0 && height != 0, AMF_FAIL, L"Bad width/height: width=%d height=%d", width, height);
    m_SwapChainExtent = AMFConstructSize(width, height);

    m_pDX12Device = (ID3D12Device*)m_pContext->GetDX12Device();
    AMF_RETURN_IF_FALSE(m_pDX12Device != NULL, AMF_FAIL, L"GetDX12Device() returned NULL");

    res = LoadPipeline(hWnd, hDisplay, format);
    AMF_RETURN_IF_FAILED(res, L"CreateSwapChain() failed");
    return AMF_OK;
}

AMF_RESULT SwapChainDX12::Terminate()
{
    if(m_pDX12Device == NULL)
    {
        return AMF_OK;
    }

    WaitForGpu();

    m_SyncFences.clear();

    m_frameIndex = 0;
    m_format = DXGI_FORMAT_UNKNOWN;

    m_rtvHeap = nullptr;
    m_rtvDescriptorSize = 0;
    m_cbvSrvHeap = nullptr;
    m_cbvSrvDescriptorSize = 0;

    m_staticSampler = {};
    m_eSwapChainImageFormat = 0;

    m_rootSignature = nullptr;
    m_graphicsPipelineState = nullptr;

    m_cmdListGraphics = nullptr;
    m_pSwapChain = nullptr;

    m_SwapChainExtent = {};
    m_fence = nullptr;
    CloseHandle(m_fenceEvent);
    m_fenceEvent = NULL;

    DeleteFrameBuffers();

    for (int i = 0; i < FrameCount; i++)
    {
        m_fenceValues[i] = 0;
        m_cmdAllocator[i] = nullptr;
    }

    m_pDX12Device = nullptr;
    m_pContext = nullptr;

    m_graphicsQueue = nullptr;
    m_dxgiAdapter = nullptr;
    m_dxgiFactory = nullptr;

    return AMF_OK;
}

AMF_RESULT SwapChainDX12::LoadPipeline(amf_handle hWnd, amf_handle hDisplay, amf_uint32 format)
{
    AMF_RETURN_IF_FALSE(m_pDX12Device != nullptr, AMF_FAIL, L"DX12 Device is not initialized.");

    m_format = (DXGI_FORMAT)format;

    AMF_RESULT res = AMF_OK;
    HRESULT hr = S_OK;

    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
    hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_dxgiFactory));
    //	CHECK_HRESULT_ERROR_RETURN(hr, L"CreateSwapChain() failed to create a dxgi factory.");

    {
        /// TEST if adapter could be found using orginal device LUID.
        LUID deviceUID = m_pDX12Device->GetAdapterLuid();
        AMF_RETURN_IF_FALSE(deviceUID.HighPart != 0 || deviceUID.LowPart != 0, AMF_FAIL, L"CreateSwapChain() failed to retrieve the LUID from dx12 device.");

        hr = m_dxgiFactory->EnumAdapterByLuid(deviceUID, IID_PPV_ARGS(&m_dxgiAdapter.p));
        (hr, L"CreateSwapChain() failed to retrieve a matching dxgi adapter from a dxgi factory using LUID.");
    }

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    hr = m_pDX12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_graphicsQueue.p));
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateSwapChain() failed to retrieve a dxgi device from dx12 device.");

    // Describe and create the swap chain.
    // The resolution of the swap chain buffers will match the resolution of the window, enabling the
    // app to enter iFlip when in fullscreen mode. We will also keep a separate buffer that is not part
    // of the swap chain as an intermediate render target, whose resolution will control the rendering
    // resolution of the scene.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_SwapChainExtent.width;
    swapChainDesc.Height = m_SwapChainExtent.height;
    swapChainDesc.Format = m_format;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;


    CComPtr<IDXGISwapChain1> swapChain;
    hr = m_dxgiFactory->CreateSwapChainForHwnd(
            m_graphicsQueue,
            (HWND)hWnd,
            &swapChainDesc,
            nullptr,
            nullptr,
            &swapChain.p);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateSwapChainForHwnd() failed to create a swap chain.");

    // When tearing support is enabled we will handle ALT+Enter key presses in the
    // window message loop rather than let DXGI handle it by calling SetFullscreenState.
    //factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER);

    hr = swapChain->QueryInterface(IID_PPV_ARGS(&m_pSwapChain.p));
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"Failed to query a extended swap chain interface.");

    m_frameIndex = m_pSwapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount + 1;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = m_pDX12Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap));
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateDescriptorHeap() RTV failed.");

        m_rtvDescriptorSize = m_pDX12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        // Describe and create a constant buffer view (CBV) and shader resource view (SRV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC cbvSrvHeapDesc = {};
        cbvSrvHeapDesc.NumDescriptors = FrameCount + 1; // One CBV per frame and one SRV for the intermediate render target.
        cbvSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cbvSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = m_pDX12Device->CreateDescriptorHeap(&cbvSrvHeapDesc, IID_PPV_ARGS(&m_cbvSrvHeap));
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateDescriptorHeap() CBV_SRV_UAV failed.");

        m_cbvSrvDescriptorSize = m_pDX12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    CreateFrameBuffers();

    return AMF_OK;
}

AMF_RESULT SwapChainDX12::CreateFrameBuffers()
{
    AMF_RETURN_IF_FALSE(m_pDX12Device != nullptr, AMF_FAIL, L"DX12 Device is not initialized.");

    for (int i = 0; i < FrameCount; i++)
    {
        BackBuffer& backBuffer = m_BackBuffers[i];

        if (FAILED(m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer.rtvBuffer))))
        {
            return AMF_DIRECTX_FAILED;
        }
        NAME_D3D12_OBJECT_N(backBuffer.rtvBuffer, i);

        /// Create RTV for swapchain backbuffers
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), i, m_rtvDescriptorSize);
        m_pDX12Device->CreateRenderTargetView(backBuffer.rtvBuffer, nullptr, rtvHandle);

        D3D12_RESOURCE_STATES initialRTV = D3D12_RESOURCE_STATE_COMMON;
        backBuffer.rtvBuffer->SetPrivateData(AMFResourceStateGUID, sizeof(D3D12_RESOURCE_STATES), &initialRTV);
    }
    return AMF_OK;
}

AMF_RESULT SwapChainDX12::DeleteFrameBuffers()
{
    for (int i = 0; i < FrameCount; i++)
    {
        m_BackBuffers[i].rtvBuffer.Release();

        m_fenceValues[i] = m_fenceValues[m_frameIndex];
    }
    return AMF_OK;
}

AMF_RESULT SwapChainDX12::ResizeSwapChain(bool bFullScreen, amf_int32 width, amf_int32 height)
{
    HRESULT hr = S_OK;

    WaitForGpu();

    DeleteFrameBuffers();

    hr = m_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SwapChain ResizeBuffers() failed.");
    // Reset the frame index to the current back buffer index.
    m_frameIndex = m_pSwapChain->GetCurrentBackBufferIndex();

    CreateFrameBuffers();

    return AMF_OK;
}

AMF_RESULT              SwapChainDX12::Present(amf_uint32 imageIndex)
{
    AMF_RESULT res = AMF_OK;
    HRESULT hr = S_OK;

    TransitionResource(m_BackBuffers[imageIndex].rtvBuffer, D3D12_RESOURCE_STATE_PRESENT);
    m_cmdListGraphics->Close();

    for (std::vector<ATL::CComPtr<ID3D12Fence>>::iterator it = m_SyncFences.begin(); it != m_SyncFences.end(); it++)
    {
        ATL::CComPtr<ID3D12Fence> pFence = *it;
        UINT64 fenceValue = 0;
        UINT dataSize = sizeof(fenceValue);
        pFence->GetPrivateData(AMFFenceValueGUID, &dataSize, &fenceValue);

        if (fenceValue != 0)
        {
            m_graphicsQueue->Wait(pFence, fenceValue);
            m_graphicsQueue->Signal(pFence, 0);

            //UINT64 val = pFence->GetCompletedValue();
            //AMFTraceWarning(AMF_FACILITY, L"Graphics Queue wait for fence: %p, wait val: %d, cur val: %d", pFence.p, (int)fenceValue, val);
        }
    }

    // Execute the command lists.
    ID3D12CommandList* ppCommandLists[] = { m_cmdListGraphics };
    m_graphicsQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    for (std::vector<ATL::CComPtr<ID3D12Fence>>::iterator it = m_SyncFences.begin(); it != m_SyncFences.end(); it++)
    {
        ATL::CComPtr<ID3D12Fence> pFence = *it;

        UINT64 fenceValue = 1;
        UINT dataSize = sizeof(fenceValue);
        m_graphicsQueue->Signal(pFence, fenceValue);
        pFence->SetPrivateData(AMFFenceValueGUID, sizeof(fenceValue), &fenceValue);

        //AMFTraceWarning(AMF_FACILITY, L"Graphics Queue signal fence: %p, val: %d", pFence.p, (int)fenceValue);
    }

    m_SyncFences.clear();

    // Present the frame.
    for (int i = 0; i < 100; i++)
    {
        hr = m_pSwapChain->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
        if (hr != DXGI_ERROR_WAS_STILL_DRAWING)
        {
            //ASSERT_RETURN_IF_HR_FAILED(hr,AMF_DIRECTX_FAILED,L"Present() - Present() failed");
            break;
        }
        amf_sleep(1);
    }

    MoveToNextFrame();
    return AMF_OK;

}

AMF_RESULT SwapChainDX12::TransitionResource(ID3D12Resource* surface, amf_int32 newState, bool bSync)
{
    if (bSync)
    {
        SyncResource(surface);
    }

    CComPtr<ID3D12GraphicsCommandList> pList = (ID3D12GraphicsCommandList*)m_cmdListGraphics;
    CComPtr<ID3D12Resource> pResource = (ID3D12Resource *)surface;
    UINT stateSize = sizeof(D3D12_RESOURCE_STATES);
    UINT beforeState = D3D12_RESOURCE_STATE_COMMON;
    pResource->GetPrivateData(AMFResourceStateGUID, &stateSize, &beforeState);
    if (beforeState == newState)
    {
        return AMF_OK;
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Transition.pResource = pResource;
    barrier.Transition.StateBefore = (D3D12_RESOURCE_STATES)beforeState;
    barrier.Transition.StateAfter = (D3D12_RESOURCE_STATES)newState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

    pResource->SetPrivateData(AMFResourceStateGUID, stateSize, &newState);

    pList->ResourceBarrier(1, &barrier);

    return AMF_OK;

}

AMF_RESULT SwapChainDX12::SyncResource(void* resource)
{
    CComPtr<ID3D12Resource> pResource = (ID3D12Resource*)resource;
    CComPtr<IUnknown> pFenceUnk;
    UINT sizeofData = sizeof(IUnknown*);
    pResource->GetPrivateData(AMFFenceGUID, &sizeofData, &pFenceUnk);
    CComQIPtr<ID3D12Fence> pFence(pFenceUnk);
    //    AMF_RETURN_IF_FALSE(pFence != nullptr, AMF_INVALID_ARG, L"No fence");
    if (pFence == nullptr)
    {
        return AMF_OK;
    }

    for (std::vector<ATL::CComPtr<ID3D12Fence>>::iterator it = m_SyncFences.begin(); it != m_SyncFences.end(); it++)
    {
        if (*(it) == pFence)
        {
            return AMF_OK;
        }
    }
    m_SyncFences.push_back(pFence);
    return AMF_OK;
}

AMF_RESULT SwapChainDX12::WaitForGpu()
{
    CComPtr<ID3D12Device> deviceDX12 = static_cast<ID3D12Device*>(m_pDX12Device);

    AMF_RESULT res = AMF_OK;
    HRESULT hr = S_OK;

    // Schedule a Signal command in the queue.
    hr = m_graphicsQueue->Signal(m_fence, m_fenceValues[m_frameIndex]);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"Graphics queue DX12 Signal() failed.");

    // Wait until the fence has been processed.
    hr = m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetEventOnCompletion() failed.");

    WaitForSingleObjectEx(m_fenceEvent, 1000000000LL, FALSE);

    // Increment the fence value for the current frame.
    m_fenceValues[m_frameIndex]++;

    return AMF_OK;
}

AMF_RESULT SwapChainDX12::MoveToNextFrame()
{
    AMF_RESULT res = AMF_OK;
    HRESULT hr = S_OK;

    // Schedule a Signal command in the queue.
    const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
    hr = m_graphicsQueue->Signal(m_fence, currentFenceValue);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"Graphics queue DX12 Signal() failed.");

    // Update the frame index.
    m_frameIndex = m_pSwapChain->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it is ready.
    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
    {
        m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent);
        WaitForSingleObjectEx(m_fenceEvent, 1000000000LL, FALSE);
    }

    // Set the fence value for the next frame.
    m_fenceValues[m_frameIndex] = currentFenceValue + 1;

    return AMF_OK;
}

DXGI_FORMAT SwapChainDX12::GetDXGIFormat(amf::AMF_SURFACE_FORMAT format) const
{
    DXGI_FORMAT dxgiFormat = DXGI_FORMAT_UNKNOWN;
    switch (format)
    {
    case amf::AMF_SURFACE_BGRA:
        dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
        break;
    case amf::AMF_SURFACE_RGBA:
        dxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    case amf::AMF_SURFACE_RGBA_F16:
        dxgiFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        break;
    case amf::AMF_SURFACE_R10G10B10A2:
        dxgiFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
        break;
    }
    return dxgiFormat;
}
