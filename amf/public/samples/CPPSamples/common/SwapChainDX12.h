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

#include "public/include/core/Context.h"
#include <vector>
#include <list>

#include <atlbase.h>
#include <d3d12.h>
#include <dxgi1_4.h>

class SwapChainDX12
{
public:
    SwapChainDX12(amf::AMFContext* pContext);
    virtual ~SwapChainDX12();

    virtual AMF_RESULT       Init(amf_handle hWnd, amf_handle hDisplay, bool bFullScreen, amf_int32 width, amf_int32 height, amf_uint32 format);
    virtual AMF_RESULT       Terminate();
    virtual AMF_RESULT       Present(amf_uint32 index);

protected:
	
	AMF_RESULT LoadPipeline(amf_handle hWnd, amf_handle hDisplay, amf_uint32 format);
	AMF_RESULT ResizeSwapChain(bool bFullScreen, amf_int32 width, amf_int32 height);
	AMF_RESULT CreateFrameBuffers();
	AMF_RESULT DeleteFrameBuffers();
	AMF_RESULT SyncResource(void* resource);
    AMF_RESULT TransitionResource(ID3D12Resource* surface, amf_int32 newState, bool bSync = false);

	AMF_RESULT WaitForGpu();
	AMF_RESULT MoveToNextFrame();
    DXGI_FORMAT GetDXGIFormat(amf::AMF_SURFACE_FORMAT format) const;

	enum CbvSrvDescriptorHeapIndex : UINT32
	{
		SrvFrameIndex0 = 0,
		SrvFrameIndex1,
		SrvFrameIndex2,
		SrvFrameIndex3,
		CbvViewMatrix,
		CbvSrvDescriptorCount
	};

	enum RtvDescriptorHeapIndex : UINT32
	{
		RtvFrameIndex0 = 0,
		RtvFrameIndex1,
		RtvFrameIndex2,
		RtvFrameIndex3,
		RtvDescriptorCount
	};

	enum GraphicsRootParameters : UINT32
	{
		GraphicsRootSRVTable = 0,
		GraphicsRootCBV,
		GraphicsRootParametersCount
	};

	static const UINT FrameCount = 4;

    amf::AMFContext2Ptr		        m_pContext;
    CComPtr<ID3D12Device>           m_pDX12Device;

	UINT				m_width;
	UINT				m_height;
    DXGI_FORMAT         m_format; 
    UINT				m_frameIndex;
   
	ATL::CComPtr<IDXGIFactory4>          m_dxgiFactory;
	ATL::CComPtr<IDXGIAdapter>           m_dxgiAdapter;
	ATL::CComPtr<ID3D12CommandQueue>     m_graphicsQueue;
	ATL::CComPtr<ID3D12CommandAllocator> m_cmdAllocator[FrameCount];
	ATL::CComPtr<ID3D12GraphicsCommandList> m_cmdListGraphics;

	ATL::CComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	UINT m_rtvDescriptorSize;

	ATL::CComPtr<ID3D12DescriptorHeap> m_cbvSrvHeap;
	UINT m_cbvSrvDescriptorSize;

	ATL::CComPtr<ID3D12RootSignature> m_rootSignature;
	ATL::CComPtr<ID3D12PipelineState> m_graphicsPipelineState;
	
	D3D12_STATIC_SAMPLER_DESC m_staticSampler;

    amf_int32                       m_eSwapChainImageFormat;
	ATL::CComPtr<IDXGISwapChain3>   m_pSwapChain;
 
    typedef struct _BackBuffer
    {
		ATL::CComPtr<ID3D12Resource> rtvBuffer;
    }BackBuffer;
    BackBuffer                      m_BackBuffers[FrameCount];
	std::vector<ATL::CComPtr<ID3D12Fence>>  m_SyncFences;

	ATL::CComPtr<ID3D12Fence>       m_fence;
	UINT64                          m_fenceValues[FrameCount];
    AMFSize                         m_SwapChainExtent;
	HANDLE							m_fenceEvent;

	GUID  AMFResourceStateGUID = { 0x452da9bf, 0x4ad7, 0x47a5, { 0xa6, 0x9b, 0x96, 0xd3, 0x23, 0x76, 0xf2, 0xf3 } };
	GUID  AMFFenceGUID = { 0x910a7928, 0x57bd, 0x4b04,{ 0x91, 0xa3, 0xe7, 0xb8, 0x4, 0x12, 0xcd, 0xa5 } };
    GUID  AMFFenceValueGUID = { 0x62a693d3, 0xbb4a, 0x46c9,{ 0xa5, 0x4, 0x9a, 0x8e, 0x97, 0xbf, 0xf0, 0x56 } };  
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