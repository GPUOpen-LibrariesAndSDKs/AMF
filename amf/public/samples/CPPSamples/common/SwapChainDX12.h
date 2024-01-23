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

#pragma once

#include "SwapChainDXGI.h"
#include "public/common/AMFSTL.h"
#include "public/include/core/D3D12AMF.h"

#include <atlbase.h>
#include <d3d12.h>
#include <dxgi1_4.h>

struct DescriptorHeapDX12
{
    D3D12_DESCRIPTOR_HEAP_TYPE              type;
    D3D12_DESCRIPTOR_HEAP_FLAGS             flags;
    ATL::CComPtr<ID3D12DescriptorHeap>      pHeap;
    amf_uint                                count;
    amf_uint                                incrementSize;
    D3D12_CPU_DESCRIPTOR_HANDLE             cpuHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE             gpuHandle;
};

struct DescriptorDX12
{
    D3D12_DESCRIPTOR_HEAP_TYPE              type;
    amf_uint                                index;
    D3D12_CPU_DESCRIPTOR_HANDLE             cpuHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE             gpuHandle;
};

// In DX12 it is generally slow to change heap descriptors during command execution
// since it might cause a hardware flush and two heaps of the same type cannot be set.
// This class provides an interface for managing descriptor heaps. Descriptors are registered
// for creation with RegisterDescriptor call and then all descriptors are created when
// CreateDescriptorHeaps is called. Register descriptor will return an index to the first
// descriptor allocated for access.
class DescriptorHeapPoolDX12
{
public:
    DescriptorHeapPoolDX12();
    ~DescriptorHeapPoolDX12();

    AMF_RESULT          Init(ID3D12Device* pDevice);
    AMF_RESULT          Terminate();

    AMF_RESULT          RegisterDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE heapType, amf_uint count, DescriptorDX12** ppDescriptors); // Array of descriptor pointers (for descriptors not stored in array)
    AMF_RESULT          RegisterDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE heapType, amf_uint count, DescriptorDX12* pDescriptors); // Array of descriptors
    AMF_RESULT          SetHeapDescriptorFlags(D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags, amf_bool replace = false);

    AMF_RESULT          CreateDescriptorHeaps();
    AMF_RESULT          DestroyHeapDescriptors();

    AMF_RESULT          GetCPUHandle(D3D12_DESCRIPTOR_HEAP_TYPE heapType, amf_uint index, D3D12_CPU_DESCRIPTOR_HANDLE& cpuHandle);
    AMF_RESULT          GetGPUHandle(D3D12_DESCRIPTOR_HEAP_TYPE heapType, amf_uint index, D3D12_GPU_DESCRIPTOR_HANDLE& gpuHandle);

    AMF_RESULT          GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, ID3D12DescriptorHeap** ppHeap);
    AMF_RESULT          GetShaderDescriptorHeaps(amf::amf_vector<ID3D12DescriptorHeap*>& pHeaps);

private:

    AMF_RESULT          CreateDescriptorHeap(DescriptorHeapDX12& heap);

    ATL::CComPtr<ID3D12Device>          m_pDevice;

    typedef amf::amf_map<D3D12_DESCRIPTOR_HEAP_TYPE, DescriptorHeapDX12> DescriptorHeapMap;
    DescriptorHeapMap                   m_heapMap;
    amf_bool                            m_initialized;
    amf::amf_vector<DescriptorDX12*>    m_pDescriptors;
};

// This encapsulates a GraphicsCommandList but if only Regular CommandList is required, it can be templated later
class CommandBufferDX12
{
public:
    CommandBufferDX12();
    ~CommandBufferDX12();

    AMF_RESULT                  Init(ID3D12Device* pDevice, D3D12_COMMAND_LIST_TYPE type, amf_uint allocatorCount, const wchar_t* debugName);
    AMF_RESULT                  Terminate();

    AMF_RESULT                  StartRecording(amf_bool reset = true);
    AMF_RESULT                  EndRecording();
    AMF_RESULT                  Execute(ID3D12CommandQueue* pQueue, amf_bool blocking = false);

    AMF_RESULT                  WaitForExecution(amf_ulong timeout=10000UL);

    AMF_RESULT                  SyncFence(ID3D12Fence* pFence);
    AMF_RESULT                  SyncResource(ID3D12Resource* pResource);

    ID3D12GraphicsCommandList* GetList() { return m_pCmdList; }

private:
    AMF_RESULT                  CreateCommandBuffer(D3D12_COMMAND_LIST_TYPE type, amf_uint allocatorCount, const wchar_t* debugName);
    AMF_RESULT                  WaitForExecution(amf_uint index, amf_ulong timeout=10000UL); // milliseconds
    AMF_RESULT                  Reset();
    AMF_RESULT                  WaitForFences(ID3D12CommandQueue* pQueue);
    AMF_RESULT                  SignalFences(ID3D12CommandQueue* pQueue);


    ATL::CComPtr<ID3D12Device>                              m_pDevice;
    ATL::CComPtr<ID3D12GraphicsCommandList>                 m_pCmdList;
    amf::amf_vector<ATL::CComPtr<ID3D12CommandAllocator>>	m_pCmdAllocators;
    amf_uint                                                m_index;

    ATL::CComPtr<ID3D12Fence>                               m_pFence;
    HANDLE                                                  m_hFenceEvent;
    amf::amf_vector<UINT64>                                 m_fenceValues; // Signaled values
    amf::amf_list<ATL::CComPtr<ID3D12Fence>>                m_pSyncFences;

    amf_bool                                                m_closed;
};

struct BackBufferDX12 : public BackBufferBase
{
    ATL::CComPtr<ID3D12Resource>        pRtvBuffer;
    DescriptorDX12                      rtvDescriptor;

    virtual void*                       GetNative()                 const override      { return pRtvBuffer; }
    virtual amf::AMF_MEMORY_TYPE        GetMemoryType()             const override      { return amf::AMF_MEMORY_DX12; }
    virtual AMFSize                     GetSize()                   const override;
};



class SwapChainDX12 : public SwapChainDXGI
{
public:
    typedef BackBufferDX12 BackBuffer;

    SwapChainDX12(amf::AMFContext* pContext);
    virtual ~SwapChainDX12();

    virtual AMF_RESULT                      Init(amf_handle hwnd, amf_handle hDisplay, amf::AMFSurface* pSurface, amf_int32 width, amf_int32 height,
                                                 amf::AMF_SURFACE_FORMAT format, amf_bool fullscreen=false, amf_bool hdr=false, amf_bool stereo=false) override;

    virtual AMF_RESULT                      Terminate() override;
    virtual AMF_RESULT                      Present(amf_bool waitForVSync) override;

    virtual amf_uint                        GetBackBufferCount() const override             { return BACK_BUFFER_COUNT; }
    virtual amf_uint                        GetBackBuffersAcquireable() const override      { return 1; }
    virtual amf_uint                        GetBackBuffersAvailable()   const override      { return m_acquired ? 0 : 1; } 

    virtual AMF_RESULT                      AcquireNextBackBufferIndex(amf_uint& index) override;
    virtual AMF_RESULT                      DropLastBackBuffer() override;
    virtual AMF_RESULT                      DropBackBufferIndex(amf_uint index) override;


    virtual AMF_RESULT                      Resize(amf_int32 width, amf_int32 height, amf_bool fullscreen, amf::AMF_SURFACE_FORMAT format = amf::AMF_SURFACE_UNKNOWN) override;

    virtual amf_bool                        StereoSupported() override { return false; }

    virtual ID3D12CommandQueue*             GetQueue() { return m_pGraphicsQueue.p; }

    static constexpr amf_uint BACK_BUFFER_COUNT = 4;

protected:

    AMF_RESULT                              GetDXGIInterface(amf_bool reinit=false) override;
    AMF_RESULT                              GetDXGIDeviceAdapter(IDXGIAdapter** ppDXGIAdapter) override;
    AMF_RESULT                              SetupSwapChain(amf_int32 width, amf_int32 height, amf::AMF_SURFACE_FORMAT format, amf_bool fullscreen, amf_bool hdr, amf_bool stereo);

    AMF_RESULT                              CreateDescriptorHeap();
	AMF_RESULT                              CreateFrameBuffers();
	AMF_RESULT                              DeleteFrameBuffers(amf_bool keepDescriptors=false);
    AMF_RESULT                              WaitForGpu();

    virtual const BackBufferBasePtr*        GetBackBuffers() const override { return m_pSwapChain != nullptr ? m_pBackBuffers : nullptr; }
    virtual AMF_RESULT                      BackBufferToSurface(const BackBufferBase* pBuffer, amf::AMFSurface** ppSurface) const override;


    amf::AMFContext2Ptr                     m_pContext2;
    CComPtr<ID3D12Device>                   m_pDX12Device;
    CComPtr<IDXGIFactory4>                  m_pDXGIFactory4;

    ATL::CComPtr<ID3D12CommandQueue>        m_pGraphicsQueue;
    CommandBufferDX12                       m_cmdBuffer;

    DescriptorHeapPoolDX12                  m_descriptorHeapPool;
    amf_uint                                m_rtvHeapIndex;

    BackBufferBasePtr                       m_pBackBuffers[BACK_BUFFER_COUNT];
    UINT                                    m_frameIndex;
    amf_bool                                m_acquired;

    ATL::CComPtr<ID3D12Fence>               m_pFence;
    HANDLE                                  m_hFenceEvent;
    UINT64                                  m_fenceValue;
};

namespace {

    // Assign a name to the object to aid with debugging.
#if defined(_DEBUG) || defined(DBG)
    inline void SetName(ID3D12Object * pObject, LPCWSTR name)
    {
        pObject->SetName(name);
    }
    inline void SetNameIndexed(ID3D12Object* pObject, LPCWSTR name, UINT index)
    {
        WCHAR fullName[50];
        if (swprintf_s(fullName, L"%s[%u]", name, index) > 0)
        {
            pObject->SetName(fullName);
        }
    }
#else
    inline void SetName(ID3D12Object*, LPCWSTR) {}
    inline void SetNameIndexed(ID3D12Object*, LPCWSTR, UINT) {}
#endif

    // Naming helper for ComPtr<T>.
    // Assigns the name of the variable as the name of the object.
    // The indexed variant will include the index in the name of the object.
#define NAME_D3D12_OBJECT(x) SetName((x), L#x)
#define NAME_D3D12_OBJECT_N(x, n) SetNameIndexed((x), L#x, n)
#define NAME_D3D12_OBJECT_INDEXED(x, n) SetNameIndexed((x)[n], L#x, n)
}

#define VALID_DX12_HEAP_HANDLE(x) (x.ptr != NULL)
#define VALID_DX12_DESCRIPTOR_CPU_HANDLE(x) (VALID_DX12_HEAP_HANDLE(x.cpuHandle))
#define VALID_DX12_DESCRIPTOR_GPU_HANDLE(x) (VALID_DX12_HEAP_HANDLE(x.gpuHandle))

// Helper functions

template<typename _Ty>
AMF_RESULT GetPrivateDataDX12(ID3D12Object* pObject, const GUID& guid, _Ty* pData)
{
    if (pObject == nullptr) { return AMF_INVALID_POINTER; }

    UINT dataSize = sizeof(_Ty);
    HRESULT hr = pObject->GetPrivateData(guid, &dataSize, pData);
    if (FAILED(hr) || dataSize != sizeof(_Ty))
    {
        return AMF_NOT_FOUND;
    }

    return AMF_OK;
}

template<typename _Ty>
AMF_RESULT SetPrivateDataDX12(ID3D12Object* pObject, const GUID& guid, const _Ty* pData)
{
    static_assert(std::is_base_of<IUnknown, _Ty>::value == false, "Use SetPrivateDataInterfaceDX12 for setting IUnknown private data");
    if (pObject == nullptr) { return AMF_INVALID_POINTER; }

    HRESULT hr = pObject->SetPrivateData(guid, pData == nullptr ? 0 : sizeof(_Ty), pData);
    if (FAILED(hr))
    {
        return AMF_DIRECTX_FAILED;
    }

    return AMF_OK;
}

static AMF_RESULT SetPrivateDataInterfaceDX12(ID3D12Object* pObject, const GUID& guid, const IUnknown* pData)
{
    if (pObject == nullptr) { return AMF_INVALID_POINTER; }

    HRESULT hr = pObject->SetPrivateDataInterface(guid, pData);
    if (FAILED(hr))
    {
        return AMF_DIRECTX_FAILED;
    }

    return AMF_OK;
}

inline AMF_RESULT GetFenceValueDX12(ID3D12Fence* pFence, UINT64& fenceValue)
{
    return GetPrivateDataDX12(pFence, AMFFenceValueGUID, &fenceValue);
}

inline AMF_RESULT SetFenceValueDX12(ID3D12Fence* pFence, UINT64 fenceValue)
{
    return SetPrivateDataDX12(pFence, AMFFenceValueGUID, &fenceValue);
}

inline AMF_RESULT GetResourceFenceDX12(ID3D12Resource* pResource, ID3D12Fence** ppFence)
{
    return GetPrivateDataDX12(pResource, AMFFenceGUID, ppFence);
}

inline AMF_RESULT SetResourceFenceDX12(ID3D12Resource* pResource, const ID3D12Fence* pFence)
{
    return SetPrivateDataInterfaceDX12(pResource, AMFFenceGUID, pFence);
}

inline AMF_RESULT GetResourceStateDX12(ID3D12Resource* pResource, D3D12_RESOURCE_STATES& state)
{
    return GetPrivateDataDX12(pResource, AMFResourceStateGUID, &state);
}

inline AMF_RESULT SetResourceStateDX12(ID3D12Resource* pResource, D3D12_RESOURCE_STATES state)
{
    return SetPrivateDataDX12(pResource, AMFResourceStateGUID, &state);
}

inline AMF_RESULT IncrFenceValueDX12(ID3D12Fence* pFence, UINT64& newValue)
{
    if (pFence == nullptr) return AMF_INVALID_POINTER;

    UINT64 fenceValue = 0;
    AMF_RESULT res = GetFenceValueDX12(pFence, fenceValue);
    if (res != AMF_OK)
    {
        return res;
    }

    fenceValue++;

    res = SetFenceValueDX12(pFence, fenceValue);
    if (res != AMF_OK)
    {
        return res;
    }

    newValue = fenceValue;
    return AMF_OK;
}

AMF_RESULT InitResourcePrivateData(ID3D12Resource* pResource, D3D12_RESOURCE_STATES newState, UINT64 fenceValue);

AMF_RESULT TransitionResource(CommandBufferDX12* pCmdBuffer, ID3D12Resource* pResource, D3D12_RESOURCE_STATES newState, amf_bool sync=false);

AMF_RESULT WaitForFenceOnCpu(ID3D12Fence* pFence, UINT64 fenceValue, HANDLE hFenceEvent, amf_ulong timeout=10000UL); // Milliseconds

AMF_RESULT WaitForResourceOnCpu(ID3D12Resource* pResource, HANDLE hFenceEvent, amf_ulong timeout=10000UL); // Milliseconds

AMF_RESULT WaitForFenceOnGpu(ID3D12CommandQueue* pQueue, ID3D12Fence* pFence);

AMF_RESULT WaitForResourceOnGpu(ID3D12CommandQueue* pQueue, ID3D12Resource* pResource);

