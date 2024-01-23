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

#include "VideoPresenter.h"
#include "SwapChainDX12.h"
#include "d3dx12.h"

struct RootSignatureLayoutDX12
{
    amf::amf_vector<D3D12_ROOT_PARAMETER1>          rootParams;

    typedef std::shared_ptr<D3D12_DESCRIPTOR_RANGE1[]> RangeListPtr;
    amf::amf_vector<RangeListPtr>                   descriptorRanges;

    inline amf_uint AddRootParam(const D3D12_ROOT_PARAMETER1& rootParam)
    {
        rootParams.push_back(rootParam);
        return (amf_uint)rootParams.size() - 1;
    }

    inline amf_uint AddDescriptorRange(const D3D12_DESCRIPTOR_RANGE1& range, D3D12_SHADER_VISIBILITY visibility)
    {
        return AddDescriptorTable(&range, 1, visibility);
    }

    inline amf_uint AddDescriptorTable(const D3D12_DESCRIPTOR_RANGE1* pRanges, amf_uint count, D3D12_SHADER_VISIBILITY visibility)
    {
        // make_shared() for arrays is introduced in c++20
        descriptorRanges.push_back(RangeListPtr(new D3D12_DESCRIPTOR_RANGE1[count]));

        // pRanges could be passed in as CD3DX12_DESCRIPTOR_RANGE1 array but that is safe to memcpy (check with std::is_trivially_copyable)
        memcpy(descriptorRanges.back().get(), pRanges, sizeof(D3D12_DESCRIPTOR_RANGE1) * count);

        CD3DX12_ROOT_PARAMETER1 param = {};
        param.InitAsDescriptorTable(count, descriptorRanges.back().get(), visibility);
        return AddRootParam(param);
    }

    inline amf_uint AddConstantBuffer(amf_uint shaderRegister, amf_uint registerSpace, D3D12_ROOT_DESCRIPTOR_FLAGS flags, D3D12_SHADER_VISIBILITY visibility)
    {
        CD3DX12_ROOT_PARAMETER1 param = {};
        param.InitAsConstantBufferView(shaderRegister, registerSpace, flags, visibility);
        return AddRootParam(param);
    }

    inline amf_uint AddShaderResource(amf_uint shaderRegister, amf_uint registerSpace, D3D12_ROOT_DESCRIPTOR_FLAGS flags, D3D12_SHADER_VISIBILITY visibility)
    {
        CD3DX12_ROOT_PARAMETER1 param = {};
        param.InitAsShaderResourceView(shaderRegister, registerSpace, flags, visibility);
        return AddRootParam(param);
    }

    inline amf_uint AddUnorderedAccessResource( amf_uint shaderRegister, amf_uint registerSpace, D3D12_ROOT_DESCRIPTOR_FLAGS flags, D3D12_SHADER_VISIBILITY visibility)
    {
        CD3DX12_ROOT_PARAMETER1 param = {};
        param.InitAsUnorderedAccessView(shaderRegister, registerSpace, flags, visibility);
        return AddRootParam(param);
    }
};


class RenderingPipelineDX12
{
public:
    RenderingPipelineDX12();
    ~RenderingPipelineDX12();

    AMF_RESULT      Init(ID3D12Device* pDevice, const RootSignatureLayoutDX12& layout, D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc, const wchar_t* debugName = L"RenderingPipeline");
    AMF_RESULT      Terminate();
    amf_bool        Initialized() { return m_pPipelineState != nullptr; }

    AMF_RESULT      BindDescriptorTable(D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle, ID3D12Resource** ppResource, amf_uint rootIndex, amf_uint groupIndex);
    AMF_RESULT      BindRootCBV(ID3D12Resource* pConstantBuffer, amf_uint rootIndex, amf_uint groupIndex);
    AMF_RESULT      BindRootSRV(ID3D12Resource* pShaderResource, amf_uint rootIndex, amf_uint groupIndex);
    AMF_RESULT      BindRootUAV(ID3D12Resource* pUnorderedAccessResource, amf_uint rootIndex, amf_uint groupIndex);
    AMF_RESULT      DuplicateGroup(amf_uint srcGroupIndex, amf_uint dstGroupIndex);

    AMF_RESULT      SetStates(CommandBufferDX12* pCmdBuffer, amf_uint groupIndex);

private:
    AMF_RESULT      CreatePipeline(const RootSignatureLayoutDX12& layout, D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc, const wchar_t* debugName);

    AMF_RESULT      BindResource(ID3D12Resource* pConstantBuffer, D3D12_ROOT_PARAMETER_TYPE type, amf_uint rootIndex, amf_uint groupIndex);
    AMF_RESULT      BindElementHelper(D3D12_ROOT_PARAMETER_TYPE type, amf_uint rootIndex, amf_uint groupIndex);

    struct RootElementDX12
    {
        D3D12_ROOT_PARAMETER_TYPE                       type;
        D3D12_GPU_DESCRIPTOR_HANDLE                     baseDescriptorGPUHandle;    // For table
        ATL::CComPtr<ID3D12Resource>                    pResource;                  // For root CBV/UAV/SRV

        amf::amf_map<D3D12_DESCRIPTOR_RANGE_TYPE, amf::amf_set<ATL::CComPtr<ID3D12Resource>>> resourceMap;
    };

    ATL::CComPtr<ID3D12Device>          m_pDevice;

    ATL::CComPtr<ID3D12PipelineState>   m_pPipelineState;
    ATL::CComPtr<ID3D12RootSignature>   m_pRootSignature;
    RootSignatureLayoutDX12             m_rootSignatureLayout;

    typedef amf::amf_map<amf_uint, amf::amf_vector<RootElementDX12>> RootSignatureGroupMap;
    RootSignatureGroupMap               m_groupMap;
};

struct BufferDX12
{
    ATL::CComPtr<ID3D12Resource>    pBuffer;
    ATL::CComPtr<ID3D12Resource>    pUpload;
};

struct VertexBufferDX12 : public BufferDX12
{
    D3D12_VERTEX_BUFFER_VIEW        view;
};

typedef BufferDX12 ConstantBufferDX12;


class VideoPresenterDX12 : public VideoPresenter
{
public:
    VideoPresenterDX12(amf_handle hwnd, amf::AMFContext* pContext, amf_handle hDisplay = nullptr);

    virtual                             ~VideoPresenterDX12();

    virtual bool                        SupportAllocator() const { return false; } // DX12 cannot support this, see AllocSurface for more info
    virtual amf::AMF_MEMORY_TYPE        GetMemoryType() const { return amf::AMF_MEMORY_DX12; }

    virtual AMF_RESULT                  Init(amf_int32 width, amf_int32 height, amf::AMFSurface* pSurface) override;
    virtual AMF_RESULT                  Terminate() override;

protected:
    typedef SwapChainDX12::BackBuffer RenderTarget;


    virtual AMF_RESULT                  RegisterDescriptors();
    virtual AMF_RESULT                  SetupRootLayout(RootSignatureLayoutDX12& layout);
    virtual AMF_RESULT                  GetQuadShaders(D3D12_SHADER_BYTECODE& vs, D3D12_SHADER_BYTECODE& ps) const;
    virtual AMF_RESULT                  BindPipelineResources();
    virtual AMF_RESULT                  BindSampler();

    AMF_RESULT                          CreateBuffer(ID3D12Resource** ppBuffer, ID3D12Resource** ppBufferUpload, const D3D12_RESOURCE_DESC& resourceDesc);
    AMF_RESULT                          CreateVertexBuffer(ID3D12Resource** ppVertexBuffer, ID3D12Resource** ppVertexBufferUpload, D3D12_VERTEX_BUFFER_VIEW& vertexBufferView, amf_size vertexSize, amf_uint count);
    AMF_RESULT                          CreateVertexBuffer(VertexBufferDX12& buffer, amf_size vertexSize, amf_uint count) { return CreateVertexBuffer(&buffer.pBuffer, &buffer.pUpload, buffer.view, vertexSize, count); }
    AMF_RESULT                          CreateConstantBuffer(ID3D12Resource** ppConstantBuffer, ID3D12Resource** ppConstantBufferUpload, size_t size);
    AMF_RESULT                          CreateConstantBuffer(ConstantBufferDX12& buffer, size_t size) { return CreateConstantBuffer(&buffer.pBuffer, &buffer.pUpload, size); }

    AMF_RESULT                          UpdateBuffer(ID3D12Resource* pBuffer, ID3D12Resource* pBufferUpload, const void* pData, amf_size size, amf_size dstOffset = 0, amf_bool immediate = false);
    AMF_RESULT                          UpdateBuffer(BufferDX12& buffer, const void* pData, amf_size size, amf_size dstOffset = 0, amf_bool immediate = false)
                                                     { return UpdateBuffer(buffer.pBuffer, buffer.pUpload, pData, size, dstOffset, immediate); }

    AMF_RESULT                          CreateResourceView(amf::AMFSurface* pSurface, const DescriptorDX12& descriptor);
    
    virtual AMF_RESULT                  RenderSurface(amf::AMFSurface* pSurface, const RenderTargetBase* pRenderTarget, RenderViewSizeInfo& renderView) override;
    virtual AMF_RESULT                  UpdateStates(amf::AMFSurface* pSurface, const RenderTarget* pRenderTarget, RenderViewSizeInfo& renderView);
    virtual AMF_RESULT                  DrawBackground(const RenderTarget* pRenderTarget);
    virtual AMF_RESULT                  SetStates(amf_uint groupIndex);
    virtual AMF_RESULT                  DrawFrame(ID3D12Resource* pSrcSurface, const RenderTarget* pRenderTarget);
    virtual AMF_RESULT                  DrawOverlay(amf::AMFSurface* /*pSurface*/, const RenderTarget* /*pRenderTarget*/) { return AMF_OK; }
    
    virtual AMF_RESULT                  OnRenderViewResize(const RenderViewSizeInfo& newRenderView) override;
    virtual AMF_RESULT                  ResizeSwapChain() override;

    static constexpr amf_uint           GROUP_QUAD_SURFACE = 0;
    static constexpr amf_uint           GROUP_QUAD_PIP = 1;

    amf::AMFContext2Ptr                 m_pContext2;
    CComPtr<ID3D12Device>               m_pDX12Device;
    DescriptorHeapPoolDX12              m_descriptorHeapPool;
    CommandBufferDX12                   m_cmdBuffer;
    RenderingPipelineDX12               m_quadPipeline;

    ConstantBufferDX12                  m_viewProjectionBuffer;
    ConstantBufferDX12                  m_pipViewProjectionBuffer;

    amf_uint                            m_srvRootIndex;
    amf_uint                            m_viewProjectionRootIndex;
    amf_uint                            m_samplerRootIndex;
    SamplerMap<DescriptorDX12>          m_samplerDescriptorMap;
    Interpolation                       m_lastBindedInterpolation;

private:
    AMF_RESULT                          CreateSamplerStates();
    AMF_RESULT                          CreateQuadPipeline();
    AMF_RESULT                          PrepareStates();

    DescriptorDX12                      m_srvDescriptors[SwapChainDX12::BACK_BUFFER_COUNT];

    VertexBufferDX12                    m_vertexBuffer;

    amf_uint                            m_currentSrvIndex;
};
