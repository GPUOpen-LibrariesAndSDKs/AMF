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

using namespace amf;

#define AMF_FACILITY L"SwapChainDX12"

SwapChainDX12::SwapChainDX12(amf::AMFContext* pContext) :
    SwapChainDXGI(pContext),
    m_pContext2(pContext),
    m_frameIndex(0),
    m_acquired(false),
    m_rtvHeapIndex(0),
    m_hFenceEvent(nullptr),
    m_fenceValue(0)
{
    for (amf_uint i = 0; i < amf_countof(m_pBackBuffers); ++i)
    {
        m_pBackBuffers[i] = std::make_unique<BackBufferDX12>();
    }
}
SwapChainDX12::~SwapChainDX12()
{
    Terminate();
}

AMF_RESULT SwapChainDX12::Init(amf_handle hwnd, amf_handle hDisplay, AMFSurface* pSurface, amf_int32 width, amf_int32 height,
                               AMF_SURFACE_FORMAT format, amf_bool fullscreen, amf_bool hdr, amf_bool stereo)
{
    AMF_RESULT res = SwapChainDXGI::Init(hwnd, hDisplay, pSurface, width, height, format, fullscreen, hdr, stereo);
    AMF_RETURN_IF_FAILED(res, L"Init() - SwapChainDXGI::Init() failed");

    res = SetupSwapChain(width, height, format, fullscreen, hdr, stereo);
    AMF_RETURN_IF_FAILED(res, L"Init() - CreateSwapChain() failed");

    m_cmdBuffer.Init(m_pDX12Device, D3D12_COMMAND_LIST_TYPE_DIRECT, 1, L"SwapChainDX12:CmdBuffer");

    return AMF_OK;
}

AMF_RESULT SwapChainDX12::Terminate()
{
    if(m_pDX12Device == nullptr)
    {
        return AMF_OK;
    }

    WaitForGpu();

    m_frameIndex = 0;
    m_acquired = false;
    m_pFence = nullptr;
    m_fenceValue = 0;
    if (m_hFenceEvent != nullptr)
    {
        CloseHandle(m_hFenceEvent);
        m_hFenceEvent = nullptr;
    }

    DeleteFrameBuffers();

    m_rtvHeapIndex = 0;

    m_pDX12Device = nullptr;
    m_pGraphicsQueue = nullptr;
    m_pDXGIFactory4 = nullptr;
    m_descriptorHeapPool.Terminate();

    m_cmdBuffer.Terminate();

    return SwapChainDXGI::Terminate();
}

AMF_RESULT SwapChainDX12::GetDXGIInterface(amf_bool reinit)
{
    // If we have the adapter, everything is already initialized
    if (m_pDXGIFactory4 != nullptr && reinit == false)
    {
        return AMF_OK;
    }

    m_pDX12Device = nullptr;
    m_pDXGIFactory4 = nullptr;
    m_pDXGIFactory2 = nullptr;
    m_pDXGIFactory = nullptr;

    m_pDX12Device = (ID3D12Device*)m_pContext2->GetDX12Device();
    AMF_RETURN_IF_INVALID_POINTER(m_pDX12Device, L"GetDXGIInterface() - GetDeviceDX12() returned NULL");

    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

    HRESULT hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_pDXGIFactory4));
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"GetDXGIInterface() - failed to create a dxgi factory");

    m_pDXGIFactory2 = m_pDXGIFactory4;
    m_pDXGIFactory = m_pDXGIFactory4;

    AMF_RESULT res = GetDXGIAdapters();
    AMF_RETURN_IF_FAILED(res, L"GetDXGIInterface() - GetDXGIAdapters() failed");

    return AMF_OK;
}

AMF_RESULT SwapChainDX12::GetDXGIDeviceAdapter(IDXGIAdapter** ppDXGIAdapter)
{
    if (m_pDXGIDevice != nullptr)
    {
        m_pDXGIDevice->GetAdapter( ppDXGIAdapter );
    }
    else if(m_pDXGIFactory4 != nullptr)
    {
        LUID luid = m_pDX12Device->GetAdapterLuid();
        m_pDXGIFactory4->EnumAdapterByLuid(luid, __uuidof(IDXGIAdapter), (void **)ppDXGIAdapter);
    }
    return AMF_OK;
}

AMF_RESULT SwapChainDX12::CreateDescriptorHeap()
{
    AMF_RESULT res = m_descriptorHeapPool.Init(m_pDX12Device);
    AMF_RETURN_IF_FAILED(res, L"CreateDescriptorHeap() - DescriptorPool Init() failed");

    DescriptorDX12* pDescriptors[BACK_BUFFER_COUNT] = {};
    for (amf_uint i = 0; i < BACK_BUFFER_COUNT; ++i)
    {
        pDescriptors[i] = &((BackBuffer*)m_pBackBuffers[i].get())->rtvDescriptor;
    }

    res = m_descriptorHeapPool.RegisterDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, BACK_BUFFER_COUNT, pDescriptors);
    AMF_RETURN_IF_FAILED(res, L"CreateDescriptorHeap() - RegisterDescriptor() failed to register RTV descriptors");

    m_descriptorHeapPool.CreateDescriptorHeaps();
    AMF_RETURN_IF_FAILED(res, L"CreateDescriptorHeap() - CreateDescriptorHeaps() failed");

    return AMF_OK;
}

AMF_RESULT SwapChainDX12::SetupSwapChain(amf_int32 width, amf_int32 height, AMF_SURFACE_FORMAT format, amf_bool fullscreen, amf_bool hdr, amf_bool stereo)
{
    AMF_RETURN_IF_FALSE(m_pGraphicsQueue == nullptr, AMF_ALREADY_INITIALIZED, L"SetupSwapChain() - m_pGraphicsQueue is already initialized");
    AMF_RETURN_IF_FALSE(m_pSwapChain == nullptr, AMF_ALREADY_INITIALIZED, L"SetupSwapChain() - m_pSwapChain is already initialized");

    AMF_RESULT res = GetDXGIInterface();
    AMF_RETURN_IF_FAILED(res, L"SetupSwapChain() - GetDXGIInterface() failed");
    AMF_RETURN_IF_FALSE(m_pDX12Device != nullptr, AMF_NOT_INITIALIZED, L"SetupSwapChain() - DX12 Device is not initialized.");

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    HRESULT hr = m_pDX12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_pGraphicsQueue));
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetupSwapChain() - CreateCommandQueue() failed");
    NAME_D3D12_OBJECT(m_pGraphicsQueue);

    res = CreateSwapChain(m_pGraphicsQueue, width, height, format, BACK_BUFFER_COUNT, fullscreen, hdr, stereo);
    AMF_RETURN_IF_FAILED(res, L"SetupSwapChain() - CreateSwapChain() failed");

    AMF_RETURN_IF_INVALID_POINTER(m_pSwapChain3, L"SetupSwapChain() - m_pSwapChain3 is NULL");
    m_frameIndex = m_pSwapChain3->GetCurrentBackBufferIndex();

    res = CreateDescriptorHeap();
    AMF_RETURN_IF_FAILED(res, L"SetupSwapChain() - CreateDescriptorHeap() failed");

    res = CreateFrameBuffers();
    AMF_RETURN_IF_FAILED(res, L"SetupSwapChain() - CreateFrameBuffers() failed");

    hr = m_pDX12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&m_pFence));
    AMF_RETURN_IF_FAILED(res, L"SetupSwapChain() - CreateFence() failed");

    m_hFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_hFenceEvent == nullptr)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"SetupSwapChain() - CreateEvent() failed");
    }

    return AMF_OK;
}

AMF_RESULT SwapChainDX12::CreateFrameBuffers()
{
    AMF_RETURN_IF_FALSE(m_pDX12Device != nullptr, AMF_NOT_INITIALIZED, L"CreateFrameBuffers() - DX12 Device is not initialized");
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"CreateFrameBuffers() - m_pSwapChain is not initialized");;

    const amf_uint bufferCount = BACK_BUFFER_COUNT;
    for (amf_uint i = 0; i < bufferCount; i++)
    {
        BackBuffer* pBackBuffer = (BackBuffer*)m_pBackBuffers[i].get();

        AMF_RETURN_IF_FALSE(pBackBuffer->pRtvBuffer == nullptr, AMF_ALREADY_INITIALIZED, L"CreateFrameBuffers() - m_backBuffers[%u].pRtvBuffer is already initialized", i);
        AMF_RETURN_IF_FALSE(VALID_DX12_DESCRIPTOR_CPU_HANDLE(pBackBuffer->rtvDescriptor), AMF_NOT_INITIALIZED, L"CreateFrameBuffers() - BackBuffers[%u] CPU Handle is not initialized", i);

        HRESULT hr = m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer->pRtvBuffer));
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateFrameBuffers() - failed to get swap chain back buffer %u", i);

        NAME_D3D12_OBJECT_N(pBackBuffer->pRtvBuffer, i);

        /// Create RTV for swapchain backbuffers
        m_pDX12Device->CreateRenderTargetView(pBackBuffer->pRtvBuffer, nullptr, pBackBuffer->rtvDescriptor.cpuHandle);

        AMF_RESULT res = InitResourcePrivateData(pBackBuffer->pRtvBuffer, D3D12_RESOURCE_STATE_COMMON, 0);
        AMF_RETURN_IF_FAILED(res, L"CreateFrameBuffers() - SetResourceStateDX12() failed to set init private data for buffer %u", i);
    }

    return AMF_OK;
}

AMF_RESULT SwapChainDX12::DeleteFrameBuffers(amf_bool keepDescriptors)
{
    for (amf_uint i = 0; i < BACK_BUFFER_COUNT; i++)
    {
        BackBuffer* pBackBuffer = (BackBuffer*)m_pBackBuffers[i].get();
        pBackBuffer->pRtvBuffer = nullptr;
        if (keepDescriptors == false)
        {
            pBackBuffer->rtvDescriptor = {};
        }
    }

    return AMF_OK;
}

AMF_RESULT SwapChainDX12::WaitForGpu()
{
    AMF_RETURN_IF_FALSE(m_pGraphicsQueue != nullptr, AMF_NOT_INITIALIZED, L"Resize() - m_pGraphicsQueue is not initialized");
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"Resize() - Swapchain is not initialized");

    // Wait for all back buffers to be signaled
    for (amf_uint i = 0; i < amf_countof(m_pBackBuffers); ++i)
    {
        AMF_RESULT res = WaitForResourceOnCpu(((BackBuffer*)m_pBackBuffers[i].get())->pRtvBuffer, m_hFenceEvent);
        AMF_RETURN_IF_FAILED(res, L"WaitForGPU() - WaitForResource() failed");
    }

    // Flush the queue
    // Todo: This might not be required since we already wait on the back buffers
    m_fenceValue++;
    m_pGraphicsQueue->Signal(m_pFence, m_fenceValue);
    WaitForFenceOnCpu(m_pFence, m_fenceValue, m_hFenceEvent);

    return AMF_OK;
}

AMF_RESULT SwapChainDX12::Resize(amf_int32 width, amf_int32 height, amf_bool fullscreen, amf::AMF_SURFACE_FORMAT format)
{
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"Resize() - Swapchain is not initialized");

    AMF_RESULT res = WaitForGpu();
    AMF_RETURN_IF_FAILED(res, L"Resize() - WaitForGPU() failed");

    res = DeleteFrameBuffers(true);
    AMF_RETURN_IF_FAILED(res, L"Resize() - DeleteFrameBuffers() failed");

    res = SwapChainDXGI::Resize(width, height, fullscreen, format);
    AMF_RETURN_IF_FAILED(res, L"Resize() - SwapChainDXGI::ResizeSwapChain() failed");

    // Reset the frame index to the current back buffer index.
    m_frameIndex = m_pSwapChain3->GetCurrentBackBufferIndex();

    res = CreateFrameBuffers();
    AMF_RETURN_IF_FAILED(res, L"Resize() - CreateFrameBuffers() failed");

    return AMF_OK;
}

AMF_RESULT SwapChainDX12::Present(amf_bool waitForVSync)
{
    AMF_RETURN_IF_FALSE(m_frameIndex < BACK_BUFFER_COUNT, AMF_INVALID_ARG, L"Present() - imageIndex(%u) is invalid, must be < %u", m_frameIndex, BACK_BUFFER_COUNT);

    BackBuffer* pBuffer = (BackBuffer*)m_pBackBuffers[m_frameIndex].get();
    AMF_RETURN_IF_FALSE(pBuffer->pRtvBuffer != nullptr, AMF_NOT_INITIALIZED, L"Present() - BackBuffers[%u] pRTVBuffer is not initialized", m_frameIndex);
    AMF_RETURN_IF_FALSE(VALID_DX12_DESCRIPTOR_CPU_HANDLE(pBuffer->rtvDescriptor), AMF_NOT_INITIALIZED, L"CreateFrameBuffers() - BackBuffers[%u] CPU Handle is not initialized", m_frameIndex);
    AMF_RETURN_IF_FALSE(m_pGraphicsQueue != nullptr, AMF_NOT_INITIALIZED, L"Present() - m_pGraphicsQueue is not initialized");
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"Present() - m_pSwapChain is not initialized");

    // Transition backbuffer to present state if not already
    AMF_RESULT res = m_cmdBuffer.StartRecording();
    AMF_RETURN_IF_FAILED(res, L"Present() - Command Buffer StartRecording() failed");

    res = TransitionResource(&m_cmdBuffer, pBuffer->pRtvBuffer, D3D12_RESOURCE_STATE_PRESENT, true);
    AMF_RETURN_IF_FAILED(res, L"Present() - TransitionResource() failed");

    res = m_cmdBuffer.Execute(m_pGraphicsQueue, false);
    AMF_RETURN_IF_FAILED(res, L"Present() - Command Buffer Execute() failed");

    res = SwapChainDXGI::Present(waitForVSync);
    AMF_RETURN_IF_FAILED(res, L"Present() - SwapChainDXGI::Present() failed");

    // The call to present adds operations to the queue which uses the current backbuffer
    // Signal the current backbuffer fence for the present operation to finish
    // Signal is required otherwise buffers might be deleted on resize while in queue
    CComPtr<ID3D12Fence> pFence;
    res = GetResourceFenceDX12(pBuffer->pRtvBuffer, &pFence);
    AMF_RETURN_IF_FAILED(res, L"Present() - GetResourceFenceDX12() failed to get backbuffer fence");

    UINT64 fenceValue = 0;
    res = IncrFenceValueDX12(pFence, fenceValue);
    AMF_RETURN_IF_FAILED(res, L"Present() - IncrFenceValue() failed to increment backbuffer fence view");

    HRESULT hr = m_pGraphicsQueue->Signal(pFence, fenceValue);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"Present() - failed to signal backbuffer fence");

    // Update the frame index.
    m_frameIndex = m_pSwapChain3->GetCurrentBackBufferIndex();
    AMF_RETURN_IF_FALSE(m_frameIndex < BACK_BUFFER_COUNT, AMF_UNEXPECTED, L"Present() - Swapchain index (%u) out of bounds, must be in [0, %u]", BACK_BUFFER_COUNT - 1);

    m_acquired = false;

    return AMF_OK;
}

AMF_RESULT SwapChainDX12::AcquireNextBackBufferIndex(amf_uint& index)
{
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"GetNextBufferIndex() - swapchain not initialized");

    if (m_acquired == true)
    {
        return AMF_NEED_MORE_INPUT;
    }

    index = m_pSwapChain3->GetCurrentBackBufferIndex();
    return AMF_OK;
}

AMF_RESULT SwapChainDX12::DropLastBackBuffer()
{
    m_acquired = false;
    return AMF_OK;
}

AMF_RESULT SwapChainDX12::DropBackBufferIndex(amf_uint index)
{
    if (index != m_frameIndex)
    {
        return AMF_NOT_FOUND;
    }

    m_acquired = false;
    return AMF_OK;
}

AMF_RESULT SwapChainDX12::BackBufferToSurface(const BackBufferBase* pBuffer, amf::AMFSurface** ppSurface) const
{
    return  m_pContext2->CreateSurfaceFromDX12Native(pBuffer->GetNative(), ppSurface, nullptr);
}

AMFSize BackBufferDX12::GetSize() const
{
    if (pRtvBuffer == nullptr)
    {
        return {};
    }

    D3D12_RESOURCE_DESC desc = pRtvBuffer->GetDesc();
    return AMFConstructSize((amf_int32)desc.Width, (amf_int32)desc.Height);
}

/*****************************************************************************/
/************************** DescriptorHeapPoolDX12 ***************************/
/*****************************************************************************/

DescriptorHeapPoolDX12::DescriptorHeapPoolDX12() :
    m_initialized(false)
{
}

DescriptorHeapPoolDX12::~DescriptorHeapPoolDX12()
{
    Terminate();
}

AMF_RESULT DescriptorHeapPoolDX12::Init(ID3D12Device* pDevice)
{
    AMF_RETURN_IF_FALSE(m_initialized == false, AMF_ALREADY_INITIALIZED, L"Init() - heap descriptor already initialized. Terminate first");

    Terminate(); // Just in case
    m_pDevice = pDevice;

    return AMF_OK;
}

AMF_RESULT DescriptorHeapPoolDX12::Terminate()
{
    DestroyHeapDescriptors();
    m_pDevice = nullptr;
    return AMF_OK;
}

AMF_RESULT DescriptorHeapPoolDX12::RegisterDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, amf_uint count, DescriptorDX12** ppDescriptors)
{
    // IMPORTANT: descriptor pointers SHOULD NOT go out of scope before CreateDescriptorHeaps is called

    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"RegisterDescriptors() - Device is not initialized");
    AMF_RETURN_IF_FALSE(type >= 0 && type < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, AMF_INVALID_ARG, L"RegisterDescriptors() - unsupported heap type %d", type);
    AMF_RETURN_IF_FALSE(count > 0, AMF_INVALID_ARG, L"RegisterDescriptors() - count must be > 0");
    AMF_RETURN_IF_FALSE(ppDescriptors != nullptr, AMF_INVALID_ARG, L"RegisterDescriptors() - ppDescriptors is NULL");

    AMF_RETURN_IF_FALSE(m_initialized == false, AMF_ALREADY_INITIALIZED, L"RegisterDescriptors() - heap descriptor already initialized. Terminate first to add more");

    for (amf_uint i = 0; i < count; ++i)
    {
        AMF_RETURN_IF_FALSE(ppDescriptors[i] != nullptr, AMF_INVALID_ARG, L"RegisterDescriptors() - ppDescriptors[%u] is NULL", i);

        ppDescriptors[i]->type = type;
        ppDescriptors[i]->index = m_heapMap[type].count++;
        m_pDescriptors.push_back(ppDescriptors[i]);
    }

    return AMF_OK;
}

AMF_RESULT DescriptorHeapPoolDX12::RegisterDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, amf_uint count, DescriptorDX12* pDescriptors)
{
    // IMPORTANT: descriptor pointers SHOULD NOT go out of scope before CreateDescriptorHeaps is called

    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"RegisterDescriptors() - Device is not initialized");
    AMF_RETURN_IF_FALSE(type >= 0 && type < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, AMF_INVALID_ARG, L"RegisterDescriptors() - unsupported heap type %d", type);
    AMF_RETURN_IF_FALSE(count > 0, AMF_INVALID_ARG, L"RegisterDescriptors() - count must be > 0");
    AMF_RETURN_IF_FALSE(pDescriptors != nullptr, AMF_INVALID_ARG, L"RegisterDescriptors() - pDescriptors is NULL");

    AMF_RETURN_IF_FALSE(m_initialized == false, AMF_ALREADY_INITIALIZED, L"RegisterDescriptors() - heap descriptor already initialized. Terminate first to add more");

    for (amf_uint i = 0; i < count; ++i)
    {
        pDescriptors[i].type = type;
        pDescriptors[i].index = m_heapMap[type].count++;
        m_pDescriptors.push_back(&pDescriptors[i]);
    }

    return AMF_OK;
}

AMF_RESULT DescriptorHeapPoolDX12::SetHeapDescriptorFlags(D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags, amf_bool replace)
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"SetHeapDescriptorFlags() - Device is not initialized");
    AMF_RETURN_IF_FALSE(type >= 0 && type < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES, AMF_INVALID_ARG, L"RegisterHeapDescriptor() - unsupported heap type %d", type);
    AMF_RETURN_IF_FALSE(m_initialized == false, AMF_ALREADY_INITIALIZED, L"RegisterHeapDescriptor() - heap descriptor already initialized. Terminate first");

    if (replace)
    {
        m_heapMap[type].flags = flags;
    }
    else
    {
        m_heapMap[type].flags |= flags;
    }

    return AMF_OK;
}

AMF_RESULT DescriptorHeapPoolDX12::CreateDescriptorHeap(DescriptorHeapDX12& heap)
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateDescriptorHeap() - Device is not initialized");
    AMF_RETURN_IF_FALSE(heap.pHeap == nullptr, AMF_ALREADY_INITIALIZED, L"CreateDescriptorHeap() - Heap of type %d is already initialized", heap.type);
    AMF_RETURN_IF_FALSE(heap.count > 0, AMF_UNEXPECTED, L"CreateDescriptorHeap() - Heap descriptor count should not be 0");

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = heap.count;
    desc.Type = heap.type;
    desc.Flags = heap.flags;
    HRESULT hr = m_pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap.pHeap));
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateDescriptorHeap() - m_Device->CreateDescriptorHeap() failed for heap type %d", heap.type);

    heap.incrementSize = m_pDevice->GetDescriptorHandleIncrementSize(heap.type);
    AMF_RETURN_IF_FALSE(heap.incrementSize > 0, AMF_UNEXPECTED, L"CreateDescriptorHeap() - Heap descriptor handle increment size should not be 0");

    heap.cpuHandle = heap.pHeap->GetCPUDescriptorHandleForHeapStart();
    AMF_RETURN_IF_FALSE(heap.cpuHandle.ptr != NULL, AMF_UNEXPECTED, L"CreateDescriptorHeap() - Heap CPU handle is NULL");

    if ((desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
    {
        heap.gpuHandle = heap.pHeap->GetGPUDescriptorHandleForHeapStart();
        AMF_RETURN_IF_FALSE(heap.gpuHandle.ptr != NULL, AMF_UNEXPECTED, L"CreateDescriptorHeap() - Heap GPU handle is NULL");
    }
    else
    {
        heap.gpuHandle = {};
    }


    return AMF_OK;
}

AMF_RESULT DescriptorHeapPoolDX12::CreateDescriptorHeaps()
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateDescriptorHeaps() - Device is not initialized");

    if (m_heapMap.empty())
    {
        AMFTraceWarning(AMF_FACILITY, L"CreateDescriptorHeaps() - No heap descriptors registered");
        return AMF_OK;
    }

    for (auto it = m_heapMap.begin(); it != m_heapMap.end();)
    {
        DescriptorHeapDX12& heap = it->second;
        if (heap.count == 0)
        {
            it = m_heapMap.erase(it);
            continue;
        }

        heap.type = it->first;

        AMF_RESULT res = CreateDescriptorHeap(heap);
        if (res != AMF_OK)
        {
            heap.pHeap = nullptr;
            heap.cpuHandle = {};
            heap.gpuHandle = {};
            AMF_RETURN_IF_FAILED(res, L"CreateDescriptorHeaps() - CreateDescriptorHeap() failed for heap type %d", heap.type);
        }

        it++;
    }

    m_initialized = true;

    // Set handles for descriptors registered
    for (DescriptorDX12* pDescriptor : m_pDescriptors)
    {
        AMF_RESULT res = GetCPUHandle(pDescriptor->type, pDescriptor->index, pDescriptor->cpuHandle);
        res = GetGPUHandle(pDescriptor->type, pDescriptor->index, pDescriptor->gpuHandle);
    }

    m_pDescriptors.clear();

    return AMF_OK;
}

AMF_RESULT DescriptorHeapPoolDX12::DestroyHeapDescriptors()
{
    m_heapMap.clear();
    m_pDescriptors.clear();
    m_initialized = false;
    return AMF_OK;
}

AMF_RESULT DescriptorHeapPoolDX12::GetCPUHandle(D3D12_DESCRIPTOR_HEAP_TYPE heapType, amf_uint index, D3D12_CPU_DESCRIPTOR_HANDLE& cpuHandle)
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"GetCPUHandle() - Device is not initialized");
    AMF_RETURN_IF_FALSE(m_initialized, AMF_NOT_INITIALIZED, L"GetCPUHandle() - Heap descriptors not created yet");
    AMF_RETURN_IF_FALSE(m_heapMap.find(heapType) != m_heapMap.end(), AMF_INVALID_ARG, L"GetCPUHandle() - Heap type not created");

    const DescriptorHeapDX12& heap = m_heapMap[heapType];

    AMF_RETURN_IF_FALSE(index < heap.count, AMF_OUT_OF_RANGE, L"GetCPUHandle() - Index (%u) out of range, must be in range [0, %u]", index, heap.count);
    AMF_RETURN_IF_FALSE(heap.incrementSize > 0, AMF_UNEXPECTED, L"GetCPUHandle() - Heap descriptor handle increment size should not be 0");
    AMF_RETURN_IF_FALSE(heap.cpuHandle.ptr != NULL, AMF_UNEXPECTED, L"GetCPUHandle() - Heap CPU handle is NULL");

    cpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(heap.cpuHandle, index, heap.incrementSize);
    return AMF_OK;
}

AMF_RESULT DescriptorHeapPoolDX12::GetGPUHandle(D3D12_DESCRIPTOR_HEAP_TYPE heapType, amf_uint index, D3D12_GPU_DESCRIPTOR_HANDLE& gpuHandle)
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"GetGPUHandle() - Device is not initialized");
    AMF_RETURN_IF_FALSE(m_initialized, AMF_NOT_INITIALIZED, L"GetGPUHandle() - Heap descriptors not created yet");
    AMF_RETURN_IF_FALSE(m_heapMap.find(heapType) != m_heapMap.end(), AMF_INVALID_ARG, L"GetGPUHandle() - Heap type not created");

    const DescriptorHeapDX12& heap = m_heapMap[heapType];

    AMF_RETURN_IF_FALSE(index < heap.count, AMF_OUT_OF_RANGE, L"GetGPUHandle() - Index (%u) out of range, must be in range [0, %u]", index, heap.count);
    AMF_RETURN_IF_FALSE(heap.incrementSize > 0, AMF_UNEXPECTED, L"GetGPUHandle() - Heap descriptor handle increment size should not be 0");

    if ((heap.flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
    {
        gpuHandle = {};
        return AMF_OK;
    }

    AMF_RETURN_IF_FALSE(heap.gpuHandle.ptr != NULL, AMF_UNEXPECTED, L"GetGPUHandle() - Heap GPU handle is NULL");
    gpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(heap.gpuHandle, index, heap.incrementSize);
    return AMF_OK;
}

AMF_RESULT DescriptorHeapPoolDX12::GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, ID3D12DescriptorHeap** ppHeap)
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"GetDescriptorHeap() - Device is not initialized");
    AMF_RETURN_IF_FALSE(m_initialized, AMF_NOT_INITIALIZED, L"GetDescriptorHeap() - Heap descriptors not created yet");
    AMF_RETURN_IF_FALSE(m_heapMap.find(heapType) != m_heapMap.end(), AMF_INVALID_ARG, L"GetDescriptorHeap() - Heap type not created");

    const DescriptorHeapDX12& heap = m_heapMap[heapType];

    AMF_RETURN_IF_FALSE(heap.pHeap != nullptr, AMF_UNEXPECTED, L"GetDescriptorHeap() - pHeap is NULL");

    *ppHeap = heap.pHeap;
    return AMF_OK;
}

AMF_RESULT DescriptorHeapPoolDX12::GetShaderDescriptorHeaps(amf_vector<ID3D12DescriptorHeap*>& pHeaps)
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"GetDescriptorHeap() - Device is not initialized");
    AMF_RETURN_IF_FALSE(m_initialized, AMF_NOT_INITIALIZED, L"GetDescriptorHeap() - Heap descriptors not created yet");

    pHeaps.clear();
    // According to docs for ID3D12GraphicsCommandList::SetDescriptorHeaps
    // Only shader visible CBV/SRV/UAV and SAMPLER descriptor heaps can be set
    for (auto it : m_heapMap)
    {
        const DescriptorHeapDX12& descriptor = it.second;

        if (descriptor.type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV && descriptor.type != D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
        {
            continue;
        }

        if ((descriptor.flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
        {
            pHeaps.push_back(descriptor.pHeap);
        }
    }

    return AMF_OK;
}


/*****************************************************************************/
/***************************** CommandBufferDX12 *****************************/
/*****************************************************************************/

CommandBufferDX12::CommandBufferDX12() :
    m_index(0),
    m_hFenceEvent(nullptr),
    m_closed(false)
{
}

CommandBufferDX12::~CommandBufferDX12()
{
    Terminate();
}

AMF_RESULT CommandBufferDX12::Init(ID3D12Device* pDevice, D3D12_COMMAND_LIST_TYPE type, amf_uint allocatorCount, const wchar_t* debugName)
{
    AMF_RETURN_IF_FALSE(pDevice != nullptr, AMF_INVALID_ARG, L"Init() - pDevice is NULL");

    m_pDevice = pDevice;
    AMF_RESULT res = CreateCommandBuffer(type, allocatorCount, debugName != nullptr ? L"CmdBuffer" : debugName);
    if (res != AMF_OK)
    {
        Terminate();
        AMF_RETURN_IF_FAILED(res, L"Init() - CreateCommandBuffer() failed");
    }

    return AMF_OK;
}

AMF_RESULT CommandBufferDX12::Terminate()
{
    if (m_pFence != nullptr && m_hFenceEvent != nullptr)
    {
        WaitForExecution();
    }

    m_pDevice = nullptr;
    m_pCmdList = nullptr;
    m_pCmdAllocators.clear();
    m_index = 0;

    m_pFence = nullptr;

    if (m_hFenceEvent != nullptr)
    {
        CloseHandle(m_hFenceEvent);
        m_hFenceEvent = nullptr;
    }

    m_fenceValues.clear();
    m_pSyncFences.clear();
    m_closed = false;

    return AMF_OK;
}

AMF_RESULT CommandBufferDX12::CreateCommandBuffer(D3D12_COMMAND_LIST_TYPE type, amf_uint allocatorCount, const wchar_t* debugName)
{
    AMF_RETURN_IF_FALSE(m_pCmdList == nullptr, AMF_ALREADY_INITIALIZED, L"CreateCommandBuffer() - m_pCmdList was already initialized");

    amf_wstring allocatorName = debugName;
    allocatorName.append(L":CmdAllocator");

    m_pCmdAllocators.assign(allocatorCount, nullptr);
    for (amf_uint i = 0; i < allocatorCount; i++)
    {
        HRESULT hr = m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&(m_pCmdAllocators[i])));
        SetNameIndexed(m_pCmdAllocators[i], allocatorName.c_str(), i);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateCommandBuffer() - CreateCommandAllocator() failed to create allocator[%u]", i);
    }

    HRESULT hr = m_pDevice->CreateCommandList(0, type, m_pCmdAllocators[0], nullptr, IID_PPV_ARGS(&m_pCmdList));
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateCommandBuffer() - CreateCommandList() failed");

    amf_wstring listName = debugName;
    listName.append(L":CmdList");
    SetName(m_pCmdList, listName.c_str());

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    hr = m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&m_pFence));
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateCommandBuffer() - CreateFence() failed.");

    amf_wstring fenceName = debugName;
    fenceName.append(L":Fence");
    SetName(m_pFence, fenceName.c_str());

    // Create an event handle to use for frame synchronization.
    m_hFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_hFenceEvent == nullptr)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"CreateCommandBuffer() - CreateEvent() failed.");
    }

    m_fenceValues.assign(allocatorCount, 0);

    return AMF_OK;
}

AMF_RESULT CommandBufferDX12::WaitForExecution(amf_uint index, amf_ulong timeout)
{
    AMF_RETURN_IF_FALSE(m_fenceValues.empty() == false, AMF_NOT_INITIALIZED, L"WaitForExecution() - m_fenceValues is not initialized");
    AMF_RETURN_IF_FALSE(index < m_fenceValues.size(), AMF_INVALID_ARG, L"WaitForExecution() - index (%u) is out of bounds, must be [0, %zu]", index, m_fenceValues.size());

    AMF_RESULT res = WaitForFenceOnCpu(m_pFence, m_fenceValues[index], m_hFenceEvent, timeout);
    AMF_RETURN_IF_FAILED(res, L"WaitForExecution() - WaitForFenceOnCpu() failed");

    return AMF_OK;
}

AMF_RESULT CommandBufferDX12::Reset()
{
    AMF_RETURN_IF_FALSE(m_pCmdList != nullptr, AMF_NOT_INITIALIZED, L"Reset() - m_pFence is not initialized");
    AMF_RETURN_IF_FALSE(m_pCmdAllocators.empty() == false, AMF_NOT_INITIALIZED, L"Reset() - m_pCmdAllocators is not initialized");
    AMF_RETURN_IF_FALSE(m_index < m_pCmdAllocators.size(), AMF_UNEXPECTED, L"Reset() - index (%u) is out of bounds, must be [0, %zu]", m_index, m_pCmdAllocators.size());

    AMF_RESULT res = WaitForExecution(m_index);
    AMF_RETURN_IF_FAILED(res, L"Reset() - WaitForExecution() failed");

    res = EndRecording();
    AMF_RETURN_IF_FAILED(res, L"Reset() - EndRecording() failed");

    HRESULT hr = m_pCmdAllocators[m_index]->Reset();
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"Reset() - Could not reset command allocator");

    hr = m_pCmdList->Reset(m_pCmdAllocators[m_index], nullptr);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"Reset() - Could not reset command list");

    m_closed = false;

    return AMF_OK;
}

AMF_RESULT CommandBufferDX12::WaitForFences(ID3D12CommandQueue* pQueue)
{
    AMF_RETURN_IF_FALSE(pQueue != nullptr, AMF_INVALID_ARG, L"WaitForFences() - pQueue is NULL");

    // For each fence, wait until the fence value is reached or exceeded
    for (auto it = m_pSyncFences.begin(); it != m_pSyncFences.end(); it++)
    {
        ATL::CComPtr<ID3D12Fence> pFence = *it;

        AMF_RESULT res = WaitForFenceOnGpu(pQueue, pFence);
        AMF_RETURN_IF_FAILED(res, L"WaitForFences() - WaitForFence() failed");
    }

    return AMF_OK;
}

AMF_RESULT CommandBufferDX12::SignalFences(ID3D12CommandQueue* pQueue)
{
    AMF_RETURN_IF_FALSE(pQueue != nullptr, AMF_INVALID_ARG, L"SignalFences() - pQueue is NULL");

    // For each fence, increment the fence value and signal for wait later
    for (auto it = m_pSyncFences.begin(); it != m_pSyncFences.end(); it++)
    {
        CComPtr<ID3D12Fence> pFence = *it;

        UINT64 fenceValue = 0;
        AMF_RESULT res = IncrFenceValueDX12(pFence, fenceValue);
        AMF_RETURN_IF_FAILED(res, L"SignalFences() - IncrFenceValueDX12() failed");

        HRESULT hr = pQueue->Signal(pFence, fenceValue);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SignalFences() - failed to signal fence");
    }

    return AMF_OK;
}

AMF_RESULT CommandBufferDX12::StartRecording(amf_bool reset)
{
    if (reset || m_closed)
    {
        AMF_RESULT res = Reset();
        AMF_RETURN_IF_FAILED(res, L"StartRecording() - Reset() failed");
        m_pSyncFences.clear();
    }

    return AMF_OK;
}

AMF_RESULT CommandBufferDX12::EndRecording()
{
    AMF_RETURN_IF_FALSE(m_pCmdList != nullptr, AMF_NOT_INITIALIZED, L"EndRecording() - m_pFence is not initialized");

    if (m_closed == false)
    {
        HRESULT hr = m_pCmdList->Close();
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"EndRecording() - Could not close command list");
    }

    m_closed = true;
    return AMF_OK;
}

AMF_RESULT CommandBufferDX12::Execute(ID3D12CommandQueue* pQueue, amf_bool blocking)
{
    AMF_RETURN_IF_FALSE(m_pCmdList != nullptr, AMF_NOT_INITIALIZED, L"Execute() - Command list is not initialized");
    AMF_RETURN_IF_FALSE(m_pFence != nullptr, AMF_NOT_INITIALIZED, L"Execute() - m_pFence is not initialized");
    AMF_RETURN_IF_FALSE(m_fenceValues.empty() == false, AMF_NOT_INITIALIZED, L"Execute() - m_fenceValues is not initialized");
    AMF_RETURN_IF_FALSE(m_index < m_fenceValues.size(), AMF_UNEXPECTED, L"Execute() - index (%u) is out of bounds, must be [0, %zu]", m_index, m_fenceValues.size());
    AMF_RETURN_IF_FALSE(pQueue != nullptr, AMF_INVALID_ARG, L"Execute() - pQueue is NULL");

    AMF_RESULT res = EndRecording();
    AMF_RETURN_IF_FAILED(res, L"Execute() - EndRecording() failed");
    AMF_RETURN_IF_FALSE(m_closed, AMF_UNEXPECTED, L"Execute() - Command buffer was not closed");

    // Wait for all synced resources to finish executing on other queues
    res = WaitForFences(pQueue);
    AMF_RETURN_IF_FAILED(res, L"Execute() - WaitForFences() failed");

    // Execute the command lists.
    ID3D12CommandList* ppCommandLists[] = { m_pCmdList };
    pQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Signal synced resources to sync with other queue execution
    res = SignalFences(pQueue);
    AMF_RETURN_IF_FAILED(res, L"Execute() - SignalFences() failed");

    m_pSyncFences.clear();

    // Signal command buffer fence so we can wait later on before reusing the command allocator
    m_fenceValues[m_index]++;
    HRESULT hr = pQueue->Signal(m_pFence, m_fenceValues[m_index]);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"Execute() - failed to signal command allocator fence");

    if (blocking)
    {
        res = WaitForExecution(m_index);
        AMF_RETURN_IF_FAILED(res, L"Execute() - WaitForExecution() failed");
    }

    // Move to next allocator
    // We don't need to do it if we block for execution completion
    // since the current allocator should be free to reset for next command
    m_index = (m_index + 1) % m_fenceValues.size();

    return AMF_OK;
}

AMF_RESULT CommandBufferDX12::WaitForExecution(amf_ulong timeout)
{
    for (amf_uint i = 0; i < m_fenceValues.size(); ++i)
    {
        WaitForExecution(i, timeout);
    }

    return AMF_OK;
}

AMF_RESULT CommandBufferDX12::SyncFence(ID3D12Fence* pFence)
{
    AMF_RETURN_IF_FALSE(m_pCmdList != nullptr, AMF_NOT_INITIALIZED, L"SyncFence() - Command list is not initialized");
    AMF_RETURN_IF_FALSE(pFence != nullptr, AMF_INVALID_ARG, L"SyncFence() - pFence is NULL");

    UINT64 fenceValue;
    AMF_RESULT res = GetFenceValueDX12(pFence, fenceValue);
    AMF_RETURN_IF_FAILED(res, L"SyncFence() - failed to get fence value, fence does not contain AMFFenceValueGUID private data");

    // If already synced, don't add it again
    if (std::find(m_pSyncFences.begin(), m_pSyncFences.end(), pFence) != m_pSyncFences.end())
    {
        return AMF_OK;
    }

    m_pSyncFences.push_back(pFence);
    return AMF_OK;
}

AMF_RESULT CommandBufferDX12::SyncResource(ID3D12Resource* pResource)
{
    AMF_RETURN_IF_FALSE(m_pCmdList != nullptr, AMF_NOT_INITIALIZED, L"SyncResource() - Command list is not initialized");
    AMF_RETURN_IF_FALSE(pResource != nullptr, AMF_INVALID_ARG, L"SyncResource() - pResource is NULL");

    CComPtr<ID3D12Fence> pFence;
    AMF_RESULT res = GetResourceFenceDX12(pResource, &pFence);
    // Commented out next line : interop from DX11 may doesn't have fences
    //AMF_RETURN_IF_FAILED(res, L"SyncResource() - Failed to get fence from resource private data");
    if (pFence != nullptr)
    {
        res = SyncFence(pFence);
        AMF_RETURN_IF_FAILED(res, L"SyncResource() - SyncFence() failed");
    }
    return AMF_OK;
}


/*****************************************************************************/
/***************************** Helper Functions ******************************/
/*****************************************************************************/

AMF_RESULT InitResourcePrivateData(ID3D12Resource* pResource, D3D12_RESOURCE_STATES newState, UINT64 fenceValue)
{
    AMF_RETURN_IF_FALSE(pResource != nullptr, AMF_INVALID_ARG, L"InitResourcePrivateData() - pResource is NULL");

    CComPtr<ID3D12Device> pDevice;
    HRESULT hr = pResource->GetDevice(IID_PPV_ARGS(&pDevice));
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"InitResourcePrivateData() - GetDevice() failed");

    AMF_RESULT res = SetResourceStateDX12(pResource, newState);
    AMF_RETURN_IF_FAILED(res, L"InitResourcePrivateData() - SetResourceStateDX12() failed to set state private data");

    CComPtr<ID3D12Fence> pFence;
    hr = pDevice->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&pFence));
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"InitResourcePrivateData() - CreateFence() failed");

    res = SetFenceValueDX12(pFence, fenceValue);
    AMF_RETURN_IF_FAILED(res, L"InitResourcePrivateData() - SetFenceValueDX12() failed to set fence value private data");

    res = SetResourceFenceDX12(pResource, pFence);
    AMF_RETURN_IF_FAILED(res, L"InitResourcePrivateData() - SetResourceFenceDX12() failed to set fence private data");

    return AMF_OK;
}

AMF_RESULT TransitionResource(CommandBufferDX12* pCmdBuffer, ID3D12Resource* pResource, D3D12_RESOURCE_STATES newState, amf_bool sync)
{
    AMF_RETURN_IF_FALSE(pCmdBuffer != nullptr, AMF_INVALID_ARG, L"TransitionResource() - pCmdBuffer is NULL");
    AMF_RETURN_IF_FALSE(pResource != nullptr, AMF_INVALID_ARG, L"TransitionResource() - pResource is NULL");
    AMF_RETURN_IF_FALSE(newState >= 0, AMF_INVALID_ARG, L"TransitionResource() - newState (%d) is NULL", newState);

    D3D12_RESOURCE_STATES beforeState = D3D12_RESOURCE_STATE_COMMON;
    AMF_RESULT res = GetResourceStateDX12(pResource, beforeState);
    AMF_RETURN_IF_FAILED(res, L"TransitionResource() - Failed to get previous state private data");

    if (beforeState == newState)
    {
        return AMF_OK;
    }

    res = SetResourceStateDX12(pResource, newState);
    AMF_RETURN_IF_FAILED(res, L"TransitionResource() - Failed to set new state private data");

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Transition.pResource = pResource;
    barrier.Transition.StateBefore = beforeState;
    barrier.Transition.StateAfter = newState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

    pCmdBuffer->GetList()->ResourceBarrier(1, &barrier);

    if (sync)
    {
        res = pCmdBuffer->SyncResource(pResource);
        AMF_RETURN_IF_FAILED(res, L"TransitionResource() - SyncResource() failed");
    }

    return AMF_OK;
}

AMF_RESULT WaitForFenceOnCpu(ID3D12Fence* pFence, UINT64 fenceValue, HANDLE hFenceEvent, amf_ulong timeout)
{
    AMF_RETURN_IF_FALSE(pFence != nullptr, AMF_INVALID_ARG, L"WaitForFenceOnCpu() - pFence is NULL");
    AMF_RETURN_IF_FALSE(hFenceEvent != nullptr, AMF_INVALID_ARG, L"WaitForFenceOnCpu() - hFenceEvent is NULL");

    if (fenceValue > pFence->GetCompletedValue())
    {
        HRESULT hr = pFence->SetEventOnCompletion(fenceValue, hFenceEvent);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"WaitForExecution() - SetEventOnCompletion() failed.");

        DWORD ret = WaitForSingleObjectEx(hFenceEvent, timeout, FALSE);
        AMF_RETURN_IF_FALSE(ret == 0, AMF_FAIL, L"WaitForExecution() - WaitForSingleObjectEx() failed with code %ul", ret);
    }

    return AMF_OK;
}

AMF_RESULT WaitForResourceOnCpu(ID3D12Resource* pResource, HANDLE hFenceEvent, amf_ulong timeout)
{
    AMF_RETURN_IF_FALSE(pResource != nullptr, AMF_INVALID_ARG, L"WaitForResourceOnCpu() - pResource is NULL");
    AMF_RETURN_IF_FALSE(hFenceEvent != nullptr, AMF_INVALID_ARG, L"WaitForResourceOnCpu() - hFenceEvent is NULL");

    CComPtr<ID3D12Fence> pFence;
    AMF_RESULT res = GetResourceFenceDX12(pResource, &pFence);
    AMF_RETURN_IF_FAILED(res, L"WaitForResourceOnCpu() - GetResourceFenceDX12() failed");

    UINT64 fenceValue = 0;
    res = GetFenceValueDX12(pFence, fenceValue);
    AMF_RETURN_IF_FAILED(res, L"WaitForResourceOnCpu() - GetFenceValueDX12() failed");

    return WaitForFenceOnCpu(pFence, fenceValue, hFenceEvent, timeout);
}

AMF_RESULT WaitForFenceOnGpu(ID3D12CommandQueue* pQueue, ID3D12Fence* pFence)
{
    AMF_RETURN_IF_FALSE(pQueue != nullptr, AMF_INVALID_ARG, L"WaitForFenceOnGpu() - pQueue is NULL");
    AMF_RETURN_IF_FALSE(pFence != nullptr, AMF_INVALID_ARG, L"WaitForFenceOnGpu() - pFence is NULL");

    UINT64 fenceValue = 0;
    AMF_RESULT res = GetFenceValueDX12(pFence, fenceValue);
    AMF_RETURN_IF_FAILED(res, L"WaitForFenceOnGpu() - GetFenceValueDX12() failed");

    HRESULT hr = pQueue->Wait(pFence, fenceValue);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"WaitForFenceOnGpu() - failed to make GPU wait for fence");

    return AMF_OK;
}

AMF_RESULT WaitForResourceOnGpu(ID3D12CommandQueue* pQueue, ID3D12Resource* pResource)
{
    AMF_RETURN_IF_FALSE(pQueue != nullptr, AMF_INVALID_ARG, L"WaitForResourceOnGpu() - pQueue is NULL");
    AMF_RETURN_IF_FALSE(pResource != nullptr, AMF_INVALID_ARG, L"WaitForResourceOnGpu() - pResource is NULL");

    CComPtr<ID3D12Fence> pFence;
    AMF_RESULT res = GetResourceFenceDX12(pResource, &pFence);
    AMF_RETURN_IF_FAILED(res, L"WaitForResourceOnGpu() - GetResourceFenceDX12() failed");

    return WaitForFenceOnGpu(pQueue, pFence);
}