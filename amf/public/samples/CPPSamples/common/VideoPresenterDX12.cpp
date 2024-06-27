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
#include "VideoPresenterDX12.h"
#include "public/common/TraceAdapter.h"

// Auto generated to build_dir from QuadDX12_vs.h and QuadDX12_ps.h
// and added to the include directory during compile
#include "QuadDX12_vs.h"
#include "QuadDX12_ps.h"

using namespace amf;
#pragma comment(lib, "d3d12.lib")

#define  AMF_FACILITY  L"VideoPresenterDX12"

template<class T>
inline static T AlignValue(T value, T alignment)
{
    return ((value + (alignment - 1)) & ~(alignment - 1));
}

VideoPresenterDX12::VideoPresenterDX12(amf_handle hwnd, AMFContext* pContext, amf_handle hDisplay) :
    VideoPresenter(hwnd, pContext, hDisplay),
    m_pContext2(pContext),
    m_currentSrvIndex(0),
    m_srvRootIndex(0),
    m_samplerRootIndex(0),
    m_viewProjectionRootIndex(0)
{
    m_pSwapChain = std::make_unique<SwapChainDX12>(pContext);
    memset(m_srvDescriptors, 0, sizeof(m_srvDescriptors));
}

VideoPresenterDX12::~VideoPresenterDX12()
{
    Terminate();
}

AMF_RESULT VideoPresenterDX12::Init(amf_int32 width, amf_int32 height, AMFSurface* /* pSurface */)
{
    AMF_RETURN_IF_FALSE(width > 0 && height > 0, AMF_INVALID_ARG, L"Init() - Bad width/height: %ux%u", width, height);
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_UNEXPECTED, L"Init() - m_pSwapChain is NULL");

    AMF_RETURN_IF_FALSE(m_hwnd != nullptr, AMF_NOT_INITIALIZED, L"Init() - m_hwnd is not initialized");
    //AMF_RETURN_IF_FALSE(m_hDisplay != nullptr, AMF_NOT_INITIALIZED, L"Init() - m_hDisplay is not initialized");

    m_pDX12Device = (ID3D12Device*)m_pContext2->GetDX12Device();
    AMF_RETURN_IF_INVALID_POINTER(m_pDX12Device, L"Init() - GetDX12Device() returned NULL");

    AMF_RESULT res = VideoPresenter::Init(width, height);
    AMF_RETURN_IF_FAILED(res, L"Init() - VideoPresenter::Init() failed");

    res = m_descriptorHeapPool.Init(m_pDX12Device);
    AMF_RETURN_IF_FAILED(res, L"Init() - m_descriptorHeapPool Init() failed");

    res = RegisterDescriptors();
    AMF_RETURN_IF_FAILED(res, L"Init() - RegisterHeapDescriptors() failed");

    res = m_pSwapChain->Init(m_hwnd, m_hDisplay, nullptr, width, height, GetInputFormat(), false, true);
    AMF_RETURN_IF_FAILED(res, L"Init() - SwapChainDX12::Init() failed");

    res = CreateSamplerStates();
    AMF_RETURN_IF_FAILED(res, L"Init() - CreateSamplerStates() failed");

    res = m_cmdBuffer.Init(m_pDX12Device, D3D12_COMMAND_LIST_TYPE_DIRECT, 
                           m_pSwapChain->GetBackBufferCount(), L"DX12PresenterCmdBuffer");
    AMF_RETURN_IF_FAILED(res, L"Init() - m_commandBuffer Init() failed");

    res = PrepareStates();
    AMF_RETURN_IF_FAILED(res, L"Init() - PrepareStates() failed");

    res = CreateQuadPipeline();
    AMF_RETURN_IF_FAILED(res, L"Init() - CreateQuadPipeline() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::RenderSurface(amf::AMFSurface* pSurface, const RenderTargetBase* pRenderTarget, RenderViewSizeInfo& renderView)
{
    AMF_RETURN_IF_FALSE(pSurface != nullptr, AMF_INVALID_ARG, L"RenderSurface() - pSrcSurface is NULL");
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"RenderSurface() - m_pSwapChain is not initialized");

    RenderTarget* pTarget = (RenderTarget*)pRenderTarget;
    AMF_RETURN_IF_FALSE(pTarget != nullptr, AMF_INVALID_ARG, L"RenderSurface() - pDstSurface is NULL");

    ID3D12Resource* pSrcDXSurface = GetPackedSurfaceDX12(pSurface);
    AMF_RETURN_IF_FALSE(pSrcDXSurface != nullptr, AMF_INVALID_ARG, L"RenderSurface() - pSrcSurface does not contain a packed plane");

    AMF_RETURN_IF_FALSE(m_pDX12Device != nullptr, AMF_NOT_INITIALIZED, L"RenderSurface() - DX12 device is not initialized");
    AMF_RETURN_IF_FALSE(m_cmdBuffer.GetList() != nullptr, AMF_NOT_INITIALIZED, L"AMF_NOT_INITIALIZED() - Command buffer is not initialized.");

    SwapChainDX12* pSwapChain = (SwapChainDX12*)m_pSwapChain.get();
    AMF_RETURN_IF_FALSE(pSwapChain->GetQueue() != nullptr, AMF_NOT_INITIALIZED, L"AMF_NOT_INITIALIZED() - Swapchain Graphics Queue is not initialized.");

    AMFContext2::AMFDX12Locker dxlock(m_pContext2);

    AMF_RESULT res = m_cmdBuffer.StartRecording(false);
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - ResetCommandBuffer() failed");

    res = UpdateStates(pSurface, pTarget, renderView);
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - UpdateStates() failed");

    res = TransitionResource(&m_cmdBuffer, pSrcDXSurface, D3D12_RESOURCE_STATE_COMMON, false);
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - Failed to transition pSrcDXSurface to D3D12_RESOURCE_STATE_COMMON");

    m_cmdBuffer.SyncResource(pTarget->pRtvBuffer);
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - SyncResource() failed to sync render target");

    res = TransitionResource(&m_cmdBuffer, pTarget->pRtvBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, false);
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - Failed to transition pRtvBuffer to D3D12_RESOURCE_STATE_RENDER_TARGET");

    // Descriptor Heap should only be binded once (expensive operation)
    amf_vector<ID3D12DescriptorHeap*> pHeaps;
    res = m_descriptorHeapPool.GetShaderDescriptorHeaps(pHeaps);
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - GetShaderDescriptorHeaps() failed");
    m_cmdBuffer.GetList()->SetDescriptorHeaps((UINT)pHeaps.size(), pHeaps.data());
  
    res = DrawBackground(pTarget);
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - DrawBackground() failed");

    res = SetStates(GROUP_QUAD_SURFACE);
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - SetStates() failed");

    res = DrawFrame(pSrcDXSurface, pTarget);
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - DrawFrame() failed");

    res = DrawOverlay(pSurface, pTarget);
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - DrawOverlay() failed");

    // PIP window
    if (m_enablePIP == true)
    {
        res = SetStates(GROUP_QUAD_PIP);
        AMF_RETURN_IF_FAILED(res, L"RenderSurface() - SetPIPStates() failed");

        res = DrawFrame(pSrcDXSurface, pTarget);
        AMF_RETURN_IF_FAILED(res, L"RenderSurface() - DrawFrame() PIP failed");
    }

    res = TransitionResource(&m_cmdBuffer, pSrcDXSurface, D3D12_RESOURCE_STATE_COMMON, false);
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - Failed to transition pSrcDXSurface to D3D12_RESOURCE_STATE_COMMON");

    res = m_cmdBuffer.Execute(pSwapChain->GetQueue(), false);
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - Command buffer Execute() failed");

    m_currentSrvIndex = (m_currentSrvIndex + 1) % amf_countof(m_srvDescriptors);

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::UpdateStates(AMFSurface* pSurface, const RenderTarget* pRenderTarget, RenderViewSizeInfo& renderView)
{
    AMF_RETURN_IF_FALSE(pSurface != nullptr, AMF_INVALID_ARG, L"UpdateStates() - pSrcSurface is NULL");
    AMF_RETURN_IF_FALSE(pRenderTarget != nullptr, AMF_INVALID_ARG, L"UpdateStates() - pDstSurface is NULL");

    AMF_RESULT res = ResizeRenderView(renderView);
    AMF_RETURN_IF_FAILED(res, L"UpdateStates() - ResizeRenderView() failed");

    const DescriptorDX12& srvDescriptor = m_srvDescriptors[m_currentSrvIndex];
    AMF_RETURN_IF_FALSE(VALID_DX12_DESCRIPTOR_CPU_HANDLE(srvDescriptor), AMF_NOT_INITIALIZED, L"UpdateStates() - SRV CPU handle is NULL");
    AMF_RETURN_IF_FALSE(VALID_DX12_DESCRIPTOR_GPU_HANDLE(srvDescriptor), AMF_NOT_INITIALIZED, L"UpdateStates() - SRV GPU handle is NULL");

    ID3D12Resource* pDXSurface = GetPackedSurfaceDX12(pSurface);
    AMF_RETURN_IF_FALSE(pDXSurface != nullptr, AMF_INVALID_ARG, L"UpdateStates() - pSurface does not contain a packed plane");

    res = m_quadPipeline.BindDescriptorTable(srvDescriptor.gpuHandle, &pDXSurface, m_srvRootIndex, GROUP_QUAD_SURFACE);
    AMF_RETURN_IF_FAILED(res, L"UpdateStates() - BindDescriptorTable(SRV, GROUP_QUAD_SURFACE) failed");

    if (m_enablePIP == true)
    {
        res = m_quadPipeline.BindDescriptorTable(srvDescriptor.gpuHandle, &pDXSurface, m_srvRootIndex, GROUP_QUAD_PIP);
        AMF_RETURN_IF_FAILED(res, L"RenderSurface() - BindDescriptorTable(SRV, GROUP_QUAD_PIP) failed");
    }

    res = CreateResourceView(pSurface, srvDescriptor);
    AMF_RETURN_IF_FAILED(res, L"UpdateStates() - CreateResourceView() failed");

    if (m_interpolation != m_lastBindedInterpolation)
    {
        BindSampler();
    }

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::DrawBackground(const RenderTarget* pRenderTarget)
{
    AMF_RETURN_IF_FALSE(m_cmdBuffer.GetList() != nullptr, AMF_NOT_INITIALIZED, L"DrawBackground() - Command buffer is not initialized.");
    AMF_RETURN_IF_FALSE(pRenderTarget != nullptr, AMF_INVALID_ARG, L"DrawBackground() - pRenderTarget is NULL");
    AMF_RETURN_IF_FALSE(VALID_DX12_DESCRIPTOR_CPU_HANDLE(pRenderTarget->rtvDescriptor), AMF_INVALID_ARG, L"DrawBackground() - pRenderTarget RTV cpu handle is not initialized");

    m_cmdBuffer.GetList()->ClearRenderTargetView(pRenderTarget->rtvDescriptor.cpuHandle, ClearColor, 0, nullptr);
    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::SetStates(amf_uint groupIndex)
{
    AMF_RETURN_IF_FALSE(m_cmdBuffer.GetList() != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - Command buffer is not initialized.");
    AMF_RETURN_IF_FALSE(m_vertexBuffer.pBuffer != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - Vertex buffer is not initialized");

    AMF_RESULT res = m_quadPipeline.SetStates(&m_cmdBuffer, groupIndex);
    AMF_RETURN_IF_FAILED(res, L"SetStates() - Render pipeline SetStates() failed");

    /// Draw area

    const AMFSize size = GetSwapchainSize();
    D3D12_VIEWPORT currentViewport = {};
    // Update viewport
    currentViewport.TopLeftX = 0;
    currentViewport.TopLeftY = 0;
    currentViewport.Width = FLOAT(size.width);
    currentViewport.Height = FLOAT(size.height);
    currentViewport.MinDepth = 0.0f;
    currentViewport.MaxDepth = 1.0f;
    m_cmdBuffer.GetList()->RSSetViewports(1, &currentViewport);
    D3D12_RECT rect = CD3DX12_RECT(0, 0, (LONG)currentViewport.Width, (LONG)currentViewport.Height);
    m_cmdBuffer.GetList()->RSSetScissorRects(1, &rect);

    /// Input Assembly
    // Vertex buffer must be in the VERTEX_AND_CONSTANT_BUFFER resource state when binding
    res = TransitionResource(&m_cmdBuffer, m_vertexBuffer.pBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    AMF_RETURN_IF_FAILED(res, L"SetStates() - Failed to transition vertex buffer to D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER");

    m_cmdBuffer.GetList()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    m_cmdBuffer.GetList()->IASetVertexBuffers(0, 1, &m_vertexBuffer.view);

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::DrawFrame(ID3D12Resource* pSrcSurface, const RenderTarget* pRenderTarget)
{
    AMF_RETURN_IF_FALSE(pSrcSurface != nullptr, AMF_INVALID_ARG, L"DrawFrame() - pSrcSurface is NULL");
    AMF_RETURN_IF_FALSE(pRenderTarget != nullptr, AMF_NOT_INITIALIZED, L"DrawFrame() - pRenderTarget is not initialized.");
    AMF_RETURN_IF_FALSE(VALID_DX12_DESCRIPTOR_CPU_HANDLE(pRenderTarget->rtvDescriptor), AMF_NOT_INITIALIZED, L"DrawFrame() - Render Target CPU Handle is not initialized.");

    AMF_RETURN_IF_FALSE(m_pDX12Device != nullptr, AMF_NOT_INITIALIZED, L"DrawFrame() - DX12 device is not initialized");
    AMF_RETURN_IF_FALSE(m_cmdBuffer.GetList() != nullptr, AMF_NOT_INITIALIZED, L"DrawFrame() - Command buffer is not initialized.");

    AMF_RESULT res = m_cmdBuffer.SyncResource(pSrcSurface);
    AMF_RETURN_IF_FAILED(res, L"DrawFrame() - SyncResource() failed to sync src surface");

    m_cmdBuffer.GetList()->OMSetRenderTargets(1, &pRenderTarget->rtvDescriptor.cpuHandle, FALSE, nullptr);

    // Draw Quad vertex count 4, instance count 1
    m_cmdBuffer.GetList()->DrawInstanced(4, 1, 0, 0);

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::Terminate()
{
    if (m_pSwapChain != nullptr)
    {
        m_pSwapChain->Terminate();
    }

    m_cmdBuffer.WaitForExecution();

    m_currentSrvIndex = 0;
    memset(m_srvDescriptors, 0, sizeof(m_srvDescriptors));
    m_samplerDescriptorMap.clear();

    m_vertexBuffer = {};

    m_viewProjectionBuffer = {};
    m_pipViewProjectionBuffer = {};

    m_quadPipeline.Terminate();
    m_cmdBuffer.Terminate();
    m_descriptorHeapPool.Terminate();

    m_resizeSwapChain = false;

    return VideoPresenter::Terminate();
}

AMF_RESULT VideoPresenterDX12::RegisterDescriptors()
{
    AMF_RESULT res = m_descriptorHeapPool.RegisterDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, amf_countof(m_srvDescriptors), m_srvDescriptors);
    AMF_RETURN_IF_FAILED(res, L"RegisterDescriptors() - RegisterDescriptors() failed to register SRV descriptors");

    DescriptorDX12* pSamplerDescriptors[INTERPOLATION_COUNT] = {};
    pSamplerDescriptors[0] = &m_samplerDescriptorMap[InterpolationLinear];
    pSamplerDescriptors[1] = &m_samplerDescriptorMap[InterpolationPoint];

    res = m_descriptorHeapPool.RegisterDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, INTERPOLATION_COUNT, pSamplerDescriptors);
    AMF_RETURN_IF_FAILED(res, L"RegisterDescriptors() - RegisterDescriptors() failed to register sampler heap descriptors");

    res = m_descriptorHeapPool.SetHeapDescriptorFlags(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
    AMF_RETURN_IF_FAILED(res, L"RegisterDescriptors() - SetHeapDescriptorFlags() failed to set shader visible flag for SRV/CBV heap");

    res = m_descriptorHeapPool.SetHeapDescriptorFlags(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
    AMF_RETURN_IF_FAILED(res, L"RegisterDescriptors() - SetHeapDescriptorFlags() failed to set shader visible flag for sampler heap");

    res = m_descriptorHeapPool.CreateDescriptorHeaps();
    AMF_RETURN_IF_FAILED(res, L"RegisterDescriptors() - CreateDescriptorHeaps() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::CreateSamplerStates()
{
    AMF_RETURN_IF_FALSE(m_pDX12Device != nullptr, AMF_NOT_INITIALIZED, L"CreateSamplerStates() - m_pDX12Device is not initialized");
    AMF_RETURN_IF_FALSE(VALID_DX12_DESCRIPTOR_CPU_HANDLE(m_samplerDescriptorMap[InterpolationLinear]), AMF_NOT_INITIALIZED, L"CreateSamplerStates() - Linear sampler descriptor is not initialized");
    AMF_RETURN_IF_FALSE(VALID_DX12_DESCRIPTOR_CPU_HANDLE(m_samplerDescriptorMap[InterpolationPoint]), AMF_NOT_INITIALIZED, L"CreateSamplerStates() - Point sampler descriptor is not initialized");

    D3D12_SAMPLER_DESC desc = {};
    desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;

    desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    m_pDX12Device->CreateSampler(&desc, m_samplerDescriptorMap[InterpolationLinear].cpuHandle);

    desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    m_pDX12Device->CreateSampler(&desc, m_samplerDescriptorMap[InterpolationPoint].cpuHandle);

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::SetupRootLayout(RootSignatureLayoutDX12& layout)
{
    CD3DX12_DESCRIPTOR_RANGE1 srvRange = {};
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE);
    m_srvRootIndex = layout.AddDescriptorRange(srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_DESCRIPTOR_RANGE1 samplerRange = {};
    samplerRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE);
    m_samplerRootIndex = layout.AddDescriptorRange(samplerRange, D3D12_SHADER_VISIBILITY_PIXEL);

    m_viewProjectionRootIndex = layout.AddConstantBuffer(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::GetQuadShaders(D3D12_SHADER_BYTECODE& vs, D3D12_SHADER_BYTECODE& ps) const
{
    // For child classes to change shaders
    vs = CD3DX12_SHADER_BYTECODE(QuadDX12_vs, sizeof(QuadDX12_vs));
    ps = CD3DX12_SHADER_BYTECODE(QuadDX12_ps, sizeof(QuadDX12_ps));
    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::CreateQuadPipeline()
{
    AMF_RETURN_IF_FALSE(m_pDX12Device != nullptr, AMF_NOT_INITIALIZED, L"CreateQuadPipeline() - DX12 Device is not initialized.");
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"CreateQuadPipeline() - m_pSwapChain is not initialized");

    RootSignatureLayoutDX12 layout = {};
    AMF_RESULT res = SetupRootLayout(layout);
    AMF_RETURN_IF_FAILED(res, L"CreateQuadPipeline() - SetupRootLayout() failed");

    // Define the vertex input layouts.
    constexpr D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, pos), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, tex), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // Describe and create the graphics pipeline state objects (PSOs).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;  // count of formats

    SwapChainDX12* pSwapChain = (SwapChainDX12*)m_pSwapChain.get();
    psoDesc.RTVFormats[0] = pSwapChain->GetDXGIFormat();
    psoDesc.SampleDesc.Count = 1;

    GetQuadShaders(psoDesc.VS, psoDesc.PS);

    // Create graphics pipeline state
    res = m_quadPipeline.Init(m_pDX12Device, layout, psoDesc, L"RenderingPipeline");
    AMF_RETURN_IF_FAILED(res, L"CreateQuadPipeline() - Render Pipeline Init() failed");

    res = BindPipelineResources();
    AMF_RETURN_IF_FAILED(res, L"CreateQuadPipeline() - BindPipelineResources() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::BindPipelineResources()
{
    // SRV will be bound later when rendering since we have
    // 4 SRVs and need to cycle between them

    AMF_RESULT res = m_quadPipeline.BindDescriptorTable(m_samplerDescriptorMap[m_interpolation].gpuHandle, nullptr, m_samplerRootIndex, GROUP_QUAD_SURFACE);
    AMF_RETURN_IF_FAILED(res, L"BindPipelineResources() - BindDescriptorTable() failed to bind sampler to quad group");

    m_lastBindedInterpolation = m_interpolation;

    res = m_quadPipeline.BindRootCBV(m_viewProjectionBuffer.pBuffer, m_viewProjectionRootIndex, GROUP_QUAD_SURFACE);
    AMF_RETURN_IF_FAILED(res, L"BindPipelineResources() - SetupRootLayout() failed to bind view projection buffer to quad group");

    res = m_quadPipeline.BindDescriptorTable(m_samplerDescriptorMap[InterpolationPoint].gpuHandle, nullptr, m_samplerRootIndex, GROUP_QUAD_PIP);
    AMF_RETURN_IF_FAILED(res, L"BindPipelineResources() - SetupRootLayout() failed to bind point sampler to PIP quad group");

    res = m_quadPipeline.BindRootCBV(m_pipViewProjectionBuffer.pBuffer, m_viewProjectionRootIndex, GROUP_QUAD_PIP);
    AMF_RETURN_IF_FAILED(res, L"BindPipelineResources() - SetupRootLayout() failed to bind PIP view projection buffer to PIP quad group");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::BindSampler()
{
    AMF_RESULT res = m_quadPipeline.BindDescriptorTable(m_samplerDescriptorMap[m_interpolation].gpuHandle, nullptr, m_samplerRootIndex, GROUP_QUAD_SURFACE);
    AMF_RETURN_IF_FAILED(res, L"BindSampler() - BindDescriptorTable() failed to bind sampler to quad group");
    m_lastBindedInterpolation = m_interpolation;

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::PrepareStates()
{
    AMF_RETURN_IF_FALSE(m_vertexBuffer.pBuffer == nullptr, AMF_ALREADY_INITIALIZED, L"PrepareStates() - m_vertexBuffer.pBuffer is already initialized");
    AMF_RETURN_IF_FALSE(m_vertexBuffer.pUpload == nullptr, AMF_ALREADY_INITIALIZED, L"PrepareStates() - m_vertexBuffer.pUpload is already initialized");
    AMF_RETURN_IF_FALSE(m_viewProjectionBuffer.pBuffer == nullptr, AMF_ALREADY_INITIALIZED, L"PrepareStates() - m_viewProjectionBuffer.pBuffer is already initialized");
    AMF_RETURN_IF_FALSE(m_viewProjectionBuffer.pUpload == nullptr, AMF_ALREADY_INITIALIZED, L"PrepareStates() - m_viewProjectionBuffer.pUpload is already initialized");
    AMF_RETURN_IF_FALSE(m_pipViewProjectionBuffer.pBuffer == nullptr, AMF_ALREADY_INITIALIZED, L"PrepareStates() - m_pipViewProjectionBuffer.pBuffer is already initialized");
    AMF_RETURN_IF_FALSE(m_pipViewProjectionBuffer.pUpload == nullptr, AMF_ALREADY_INITIALIZED, L"PrepareStates() - m_pipViewProjectionBuffer.pUpload is already initialized");

    AMF_RETURN_IF_FALSE(m_pDX12Device != nullptr, AMF_NOT_INITIALIZED, L"PrepareStates() - DX12 Device is not initialized.");

    // Create vertex buffer and view
    AMF_RESULT res = CreateVertexBuffer(m_vertexBuffer, sizeof(Vertex), amf_countof(QUAD_VERTICES_NORM));
    AMF_RETURN_IF_FAILED(res, L"PrepareStates() - CreateVertexBuffers() failed");
    NAME_D3D12_OBJECT(m_vertexBuffer.pBuffer);
    NAME_D3D12_OBJECT(m_vertexBuffer.pUpload);

    res = UpdateBuffer(m_vertexBuffer, QUAD_VERTICES_NORM, sizeof(QUAD_VERTICES_NORM));
    AMF_RETURN_IF_FAILED(res, L"PrepareStates() - UpdateBufferImmediate() failed to initialize vertex buffer");

    // Create the view projection constant buffer
    res = CreateConstantBuffer(m_viewProjectionBuffer, sizeof(ViewProjection));
    AMF_RETURN_IF_FAILED(res, L"PrepareStates() - CreateConstantBuffer() failed");
    NAME_D3D12_OBJECT(m_viewProjectionBuffer.pBuffer);
    NAME_D3D12_OBJECT(m_viewProjectionBuffer.pUpload);

    // Create PIP view projection constant buffer
    res = CreateConstantBuffer(m_pipViewProjectionBuffer, sizeof(ViewProjection));
    AMF_RETURN_IF_FAILED(res, L"PrepareStates() - CreateConstantBuffer() failed");
    NAME_D3D12_OBJECT(m_pipViewProjectionBuffer.pBuffer);
    NAME_D3D12_OBJECT(m_pipViewProjectionBuffer.pUpload);

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::CreateBuffer(ID3D12Resource** ppBuffer, ID3D12Resource** ppBufferUpload, const D3D12_RESOURCE_DESC& resourceDesc)
{
    AMF_RETURN_IF_FALSE(ppBuffer != nullptr, AMF_INVALID_ARG, L"CreateBuffer() - ppBuffer is NULL");
    AMF_RETURN_IF_FALSE(ppBufferUpload != nullptr, AMF_INVALID_ARG, L"CreateBuffer() - ppBufferUpload is NULL");
    AMF_RETURN_IF_FALSE(*ppBuffer == nullptr, AMF_ALREADY_INITIALIZED, L"CreateBuffer() - buffer is already initialized");
    AMF_RETURN_IF_FALSE(*ppBufferUpload == nullptr, AMF_ALREADY_INITIALIZED, L"CreateBuffer() - upload buffer is already initialized");

    AMF_RETURN_IF_FALSE(m_pDX12Device != nullptr, AMF_NOT_INITIALIZED, L"CreateBuffer() - DX12 Device is not initialized.");

    // We need to create two buffers, one default and one upload
    // 
    // The default heap type has the most bandwidth for the GPU to use
    // however it cannot provide CPU access for updating the buffer values
    // 
    // The upload heap type has CPU optimized access which is best for 
    // writing into from CPU and reading from GPU. 
    // 
    // To maximize efficiency, we create the main buffer as a default heap type
    // to maximize bandwidth. We then create a upload heap buffer which we can 
    // write to from the CPU therefore everytime we want to update the buffer,
    // we can first update the upload buffer and then copy from the upload buffer 
    // into the main default buffer on the GPU side.

    // Main (default) buffer
    {
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;

        CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
        HRESULT hr = m_pDX12Device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            initialState,
            nullptr,
            IID_PPV_ARGS(ppBuffer));

        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateVertexBuffers() - CreateCommittedResource() failed for vertex buffer");

        // Update the state in private data
        AMF_RESULT res = InitResourcePrivateData(*ppBuffer, initialState, 0);
        AMF_RETURN_IF_FAILED(res, L"CreateBuffer() - InitResourcePrivateData() failed");
    }

    // Upload buffer
    {
        CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_UPLOAD);
        HRESULT hr = m_pDX12Device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(ppBufferUpload));

        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateVertexBuffers() - CreateCommittedResource() failed for upload vertex buffer");
    }

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::CreateVertexBuffer(ID3D12Resource** ppVertexBuffer, ID3D12Resource** ppVertexBufferUpload, D3D12_VERTEX_BUFFER_VIEW& vertexBufferView, amf_size vertexSize, amf_uint count)
{
    AMF_RETURN_IF_FALSE(ppVertexBuffer != nullptr, AMF_INVALID_ARG, L"CreateVertexBuffer() - ppVertexBuffer is NULL");
    AMF_RETURN_IF_FALSE(ppVertexBufferUpload != nullptr, AMF_INVALID_ARG, L"CreateVertexBuffer() - ppVertexBufferUpload is NULL");

    const CD3DX12_RESOURCE_DESC vertexDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexSize * count);

    AMF_RESULT res = CreateBuffer(ppVertexBuffer, ppVertexBufferUpload, vertexDesc);
    AMF_RETURN_IF_FAILED(res, L"CreateVertexBuffer() - CreateBuffer() failed");

    // Initialize the vertex buffer views
    vertexBufferView.BufferLocation = (*ppVertexBuffer)->GetGPUVirtualAddress();
    vertexBufferView.StrideInBytes = (UINT)vertexSize;
    vertexBufferView.SizeInBytes = (UINT)vertexDesc.Width;

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::CreateConstantBuffer(ID3D12Resource** ppConstantBuffer, ID3D12Resource** ppConstantBufferUpload, size_t size)
{
    AMF_RETURN_IF_FALSE(ppConstantBuffer != nullptr, AMF_INVALID_ARG, L"CreateConstantBuffer() - ppVertexBuffer is NULL");
    AMF_RETURN_IF_FALSE(ppConstantBufferUpload != nullptr, AMF_INVALID_ARG, L"CreateConstantBuffer() - ppVertexBufferUpload is NULL");
    AMF_RETURN_IF_FALSE(size > 0, AMF_INVALID_ARG, L"CreateConstantBuffer() - Invalid buffer size (%zu)", size);
    AMF_RETURN_IF_FALSE(m_pDX12Device != nullptr, AMF_NOT_INITIALIZED, L"CreateConstantBuffer() - m_pDX12Device is not initialized");

    // Create constant buffer
    size_t bufferSize = AlignValue(size, (size_t)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

    const CD3DX12_RESOURCE_DESC cbDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    AMF_RESULT res = CreateBuffer(ppConstantBuffer, ppConstantBufferUpload, cbDesc);
    AMF_RETURN_IF_FAILED(res, L"CreateConstantBuffer() - CreateBuffer() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::UpdateBuffer(ID3D12Resource* pBuffer, ID3D12Resource* pBufferUpload, const void* pData, amf_size size, amf_size dstOffset, amf_bool immediate)
{
    AMF_RETURN_IF_FALSE(pBuffer != nullptr, AMF_INVALID_ARG, L"UpdateBuffer() - pBuffer is NULL");
    AMF_RETURN_IF_FALSE(pBufferUpload != nullptr, AMF_INVALID_ARG, L"UpdateBuffer() - pBufferUpload is NULL");
    AMF_RETURN_IF_FALSE(pData != nullptr, AMF_INVALID_ARG, L"UpdateBuffer() - pData is NULL");
    AMF_RETURN_IF_FALSE(size > 0, AMF_INVALID_ARG, L"UpdateBuffer() - size is 0");
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"UpdateBuffer() - m_pSwapChain is not initialized");

    AMF_RETURN_IF_FALSE(m_cmdBuffer.GetList() != nullptr, AMF_NOT_INITIALIZED, L"UpdateBuffer() - Command buffer is not initialized");

    SwapChainDX12* pSwapChain = (SwapChainDX12*)m_pSwapChain.get();
    AMF_RETURN_IF_FALSE(pSwapChain->GetQueue() != nullptr, AMF_NOT_INITIALIZED, L"UpdateBuffer() - Graphics queue is not initialized");

    UINT8* pMappedBuffer = nullptr;
    CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
    CD3DX12_RANGE writeRange(dstOffset, dstOffset + size);

    HRESULT hr = pBufferUpload->Map(0, &readRange, reinterpret_cast<void**>(&pMappedBuffer));

    if (SUCCEEDED(hr))
    {
        memcpy(pMappedBuffer + dstOffset, pData, size);
    }

    pBufferUpload->Unmap(0, &writeRange);

    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"UpdateBuffer() - Map() failed");

    AMF_RESULT res = m_cmdBuffer.StartRecording(false);
    AMF_RETURN_IF_FAILED(res, L"UpdateBuffer() - StartRecording() failed");

    res = m_cmdBuffer.SyncResource(pBuffer);
    AMF_RETURN_IF_FAILED(res, L"UpdateBuffer() - SyncResource() failed");

    res = TransitionResource(&m_cmdBuffer, pBuffer, D3D12_RESOURCE_STATE_COPY_DEST, false);
    AMF_RETURN_IF_FAILED(res, L"UpdateBuffer() - TransitionResource() failed");

    m_cmdBuffer.GetList()->CopyBufferRegion(pBuffer, 0, pBufferUpload, 0, size);

    if (immediate == true)
    {
        res = m_cmdBuffer.Execute(pSwapChain->GetQueue(), true);
        AMF_RETURN_IF_FAILED(res, L"UpdateBuffer() - ExecuteCommandBuffer() after udate failed");
    }

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::CreateResourceView(amf::AMFSurface* pSurface, const DescriptorDX12& descriptor)
{
    AMF_RETURN_IF_FALSE(pSurface != nullptr, AMF_INVALID_ARG, L"CreateResourceView() - pSurface was NULL");
    AMF_RETURN_IF_FALSE(VALID_DX12_DESCRIPTOR_CPU_HANDLE(descriptor), AMF_INVALID_ARG, L"CreateResourceView() - descriptor CPU handle was NULL");

    ID3D12Resource* pResource = GetPackedSurfaceDX12(pSurface);
    AMF_RETURN_IF_FALSE(pResource != nullptr, AMF_NOT_INITIALIZED, L"CreateResourceView() - surface packed plane native was NULL");

    D3D12_RESOURCE_DESC srcDesc = pResource->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = srcDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = srcDesc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    m_pDX12Device->CreateShaderResourceView(pResource, &srvDesc, descriptor.cpuHandle);

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::ResizeSwapChain()
{
    AMF_RESULT res = m_cmdBuffer.WaitForExecution();
    AMF_RETURN_IF_FAILED(res, L"ResizeSwapChain() - Command buffer WaitForExecution() failed");

    res = VideoPresenter::ResizeSwapChain();
    AMF_RETURN_IF_FAILED(res, L"ResizeSwapChain() - VideoPresenter::ResizeSwapChain() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::OnRenderViewResize(const RenderViewSizeInfo& newRenderView)
{
    AMF_RESULT res = VideoPresenter::OnRenderViewResize(newRenderView);
    AMF_RETURN_IF_FAILED(res, L"OnRenderViewResize() - VideoPresenter::OnRenderViewResize() failed");

    res = UpdateBuffer(m_viewProjectionBuffer, &m_viewProjection, sizeof(ViewProjection));
    AMF_RETURN_IF_FAILED(res, L"UpdateVertices() - UpdateBuffer() failed view projection");

    res = UpdateBuffer(m_pipViewProjectionBuffer, &m_pipViewProjection, sizeof(ViewProjection));
    AMF_RETURN_IF_FAILED(res, L"UpdateVertices() - UpdateBuffer() failed to update pip view projection");

    return AMF_OK;

}


//-------------------------------------------------------------------------------------------------
//------------------------------------- RenderingPipelineDX12 -------------------------------------
//-------------------------------------------------------------------------------------------------
#define ROOT_PARAMETER_TYPE_EMPTY ((D3D12_ROOT_PARAMETER_TYPE)-1)

RenderingPipelineDX12::RenderingPipelineDX12() :
    m_rootSignatureLayout{}
{
}

RenderingPipelineDX12::~RenderingPipelineDX12()
{
    Terminate();
}

AMF_RESULT RenderingPipelineDX12::Init(ID3D12Device* pDevice, const RootSignatureLayoutDX12& layout, D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc, const wchar_t* debugName)
{
    AMF_RETURN_IF_FALSE(pDevice != nullptr, AMF_INVALID_ARG, L"Init() - pDevice is NULL");
    m_pDevice = pDevice;

    AMF_RESULT res = CreatePipeline(layout, desc, debugName);
    if (res != AMF_OK)
    {
        Terminate();
    }
    AMF_RETURN_IF_FAILED(res, L"Init() - CreatePipeline() failed");

    return AMF_OK;
}

AMF_RESULT RenderingPipelineDX12::Terminate()
{
    m_pDevice = nullptr;
    m_pPipelineState = nullptr;
    m_pRootSignature = nullptr;
    m_rootSignatureLayout = {};
    m_groupMap.clear();

    return AMF_OK;
}

AMF_RESULT RenderingPipelineDX12::BindElementHelper(D3D12_ROOT_PARAMETER_TYPE type, amf_uint rootIndex, amf_uint groupIndex)
{
    AMF_RETURN_IF_FALSE(m_pPipelineState != nullptr, AMF_NOT_INITIALIZED, L"BindElement() - m_pPipelineState is not initialized");

    AMF_RETURN_IF_FALSE(rootIndex < m_rootSignatureLayout.rootParams.size(), AMF_OUT_OF_RANGE,
        L"BindElement() - root index (%u) out of range, must be < %zu", rootIndex, m_rootSignatureLayout.rootParams.size());

    const D3D12_ROOT_PARAMETER_TYPE requiredType = m_rootSignatureLayout.rootParams[rootIndex].ParameterType;
    AMF_RETURN_IF_FALSE(requiredType == type, AMF_INVALID_ARG, L"BindElement() - Root index %u cannot be used for constant buffer", rootIndex);

    if (m_groupMap[groupIndex].size() != m_rootSignatureLayout.rootParams.size())
    {
        RootElementDX12 defaultElem = {};
        defaultElem.type = ROOT_PARAMETER_TYPE_EMPTY;
        m_groupMap[groupIndex].resize(m_rootSignatureLayout.rootParams.size(), defaultElem);
    }

    m_groupMap[groupIndex][rootIndex] = {};
    m_groupMap[groupIndex][rootIndex].type = type;

    return AMF_OK;
}

AMF_RESULT RenderingPipelineDX12::BindResource(ID3D12Resource* pResource, D3D12_ROOT_PARAMETER_TYPE type, amf_uint rootIndex, amf_uint groupIndex)
{
    AMF_RETURN_IF_FALSE(m_pPipelineState != nullptr, AMF_NOT_INITIALIZED, L"BindResource() - m_pPipelineState is not initialized");
    AMF_RETURN_IF_FALSE(pResource != nullptr, AMF_INVALID_ARG, L"BindElement() - pResource is NULL");
    AMF_RETURN_IF_FALSE(type != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, AMF_INVALID_ARG, L"BindResource() - Type can only be  SRV, UAV and CBV");

    AMF_RESULT res = BindElementHelper(type, rootIndex, groupIndex);
    AMF_RETURN_IF_FAILED(res, L"BindElement() - failed to bind resource");

    m_groupMap[groupIndex][rootIndex].pResource = pResource;

    switch (type)
    {
    case D3D12_ROOT_PARAMETER_TYPE_CBV:
        m_groupMap[groupIndex][rootIndex].resourceMap[D3D12_DESCRIPTOR_RANGE_TYPE_CBV].insert(pResource);
        break;
    case D3D12_ROOT_PARAMETER_TYPE_SRV:
        m_groupMap[groupIndex][rootIndex].resourceMap[D3D12_DESCRIPTOR_RANGE_TYPE_SRV].insert(pResource);
        break;
    case D3D12_ROOT_PARAMETER_TYPE_UAV:
        m_groupMap[groupIndex][rootIndex].resourceMap[D3D12_DESCRIPTOR_RANGE_TYPE_UAV].insert(pResource);
        break;
    default:
        AMF_RETURN_IF_FALSE(false, AMF_UNEXPECTED, L"BindResource() - We should never get here, something went horribly wrong");
    }

    return AMF_OK;
}

AMF_RESULT RenderingPipelineDX12::BindDescriptorTable(D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle, ID3D12Resource** ppResources, amf_uint rootIndex, amf_uint groupIndex)
{
    AMF_RETURN_IF_FALSE(m_pPipelineState != nullptr, AMF_NOT_INITIALIZED, L"BindDescriptorTable() - m_pPipelineState is not initialized");
    AMF_RETURN_IF_FALSE(VALID_DX12_HEAP_HANDLE(gpuHandle), AMF_INVALID_ARG, L"BindDescriptorTable() - gpuHandle is NULL");

    AMF_RESULT res = BindElementHelper(D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, rootIndex, groupIndex);
    AMF_RETURN_IF_FAILED(res, L"BindDescriptorTable() - failed to bind descriptor");
    m_groupMap[groupIndex][rootIndex].baseDescriptorGPUHandle = gpuHandle;

    if (ppResources != nullptr)
    {
        const D3D12_ROOT_DESCRIPTOR_TABLE1& table = m_rootSignatureLayout.rootParams[rootIndex].DescriptorTable;
        ID3D12Resource* pResource = ppResources[0];
        for (amf_uint rangeIndex = 0; rangeIndex < table.NumDescriptorRanges; ++rangeIndex)
        {
            const D3D12_DESCRIPTOR_RANGE1& range = table.pDescriptorRanges[rangeIndex];
            AMF_RETURN_IF_FALSE(range.RangeType != D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, AMF_INVALID_ARG, L"BindDescriptorTable() - Cannot set resources for sampler tables");

            for (amf_uint descriptorIndex = 0; descriptorIndex < range.NumDescriptors; ++descriptorIndex)
            {
                if (pResource != nullptr)
                {
                    m_groupMap[groupIndex][rootIndex].resourceMap[range.RangeType].insert(pResource);
                }

                pResource++;
            }
        }
    }

    return AMF_OK;
}

AMF_RESULT RenderingPipelineDX12::BindRootCBV(ID3D12Resource* pConstantBuffer, amf_uint rootIndex, amf_uint groupIndex)
{
    return BindResource(pConstantBuffer, D3D12_ROOT_PARAMETER_TYPE_CBV, rootIndex, groupIndex);
}

AMF_RESULT RenderingPipelineDX12::BindRootSRV(ID3D12Resource* pShaderResource, amf_uint rootIndex, amf_uint groupIndex)
{
    return BindResource(pShaderResource, D3D12_ROOT_PARAMETER_TYPE_SRV, rootIndex, groupIndex);
}

AMF_RESULT RenderingPipelineDX12::BindRootUAV(ID3D12Resource* pUnorderedAccessResource, amf_uint rootIndex, amf_uint groupIndex)
{
    return BindResource(pUnorderedAccessResource, D3D12_ROOT_PARAMETER_TYPE_UAV, rootIndex, groupIndex);
}

AMF_RESULT RenderingPipelineDX12::DuplicateGroup(amf_uint srcGroupIndex, amf_uint dstGroupIndex)
{
    AMF_RETURN_IF_FALSE(m_pPipelineState != nullptr, AMF_NOT_INITIALIZED, L"BindDescriptorTable() - m_pPipelineState is not initialized");

    if (srcGroupIndex == dstGroupIndex)
    {
        return AMF_OK;
    }

    m_groupMap[dstGroupIndex] = m_groupMap[srcGroupIndex];
    return AMF_OK;
}

AMF_RESULT RenderingPipelineDX12::SetStates(CommandBufferDX12* pCmdBuffer, amf_uint groupIndex)
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - pDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pPipelineState != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - m_pPipelineState is not initialized");
    AMF_RETURN_IF_FALSE(pCmdBuffer != nullptr, AMF_INVALID_ARG, L"SetStates() - pCmdBuffer is NULL");
    AMF_RETURN_IF_FALSE(pCmdBuffer->GetList() != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - Command buffer is not initialized");
    AMF_RETURN_IF_FALSE(m_groupMap.find(groupIndex) != m_groupMap.end(), AMF_INVALID_ARG, L"SetStates() - group index not binded");

    pCmdBuffer->GetList()->SetPipelineState(m_pPipelineState);
    pCmdBuffer->GetList()->SetGraphicsRootSignature(m_pRootSignature);

    for (amf_uint rootIndex = 0; rootIndex < (amf_uint)m_rootSignatureLayout.rootParams.size(); ++rootIndex)
    {
        const RootElementDX12& element = m_groupMap[groupIndex][rootIndex];

        // Transition resource states
        for (const auto& item : element.resourceMap)
        {
            D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
            switch (item.first)
            {
            case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
                state = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
                break;
            case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                // Equivalent to D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE
                state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                break;
            case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                break;
            default:
                continue;
            }

            for (const ATL::CComPtr<ID3D12Resource>& pResource : item.second)
            {
                AMF_RESULT res = TransitionResource(pCmdBuffer, pResource, state);
                AMF_RETURN_IF_FAILED(res, L"SetStates() - Could not transition resource at group index %u root index %zu to state %d", groupIndex, rootIndex, state);
            }
        }

        // Set graphics root parameters
        switch ((amf_int)element.type)
        {
            // Unbinded root parameters, either left unbinded or binded externally. Up to caller to figure it out
        case ROOT_PARAMETER_TYPE_EMPTY:
            break;
        case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
            pCmdBuffer->GetList()->SetGraphicsRootDescriptorTable(rootIndex, element.baseDescriptorGPUHandle);
            break;
        case D3D12_ROOT_PARAMETER_TYPE_CBV:
        {
            pCmdBuffer->GetList()->SetGraphicsRootConstantBufferView(rootIndex, element.pResource->GetGPUVirtualAddress());
            break;
        }
        case D3D12_ROOT_PARAMETER_TYPE_SRV:
        {
            pCmdBuffer->GetList()->SetGraphicsRootShaderResourceView(rootIndex, element.pResource->GetGPUVirtualAddress());
            break;
        }
        case D3D12_ROOT_PARAMETER_TYPE_UAV:
        {
            pCmdBuffer->GetList()->SetGraphicsRootUnorderedAccessView(rootIndex, element.pResource->GetGPUVirtualAddress());
            break;
        }
        default:
            AMFTraceError(AMF_FACILITY, L"Invalid element type %d at group index %u, root index %zu", element.type, groupIndex, rootIndex);
            break;
        }
    }

    return AMF_OK;
}

AMF_RESULT RenderingPipelineDX12::CreatePipeline(const RootSignatureLayoutDX12& layout, D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc, const wchar_t* debugName)
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"CreatePipeline() - pDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pRootSignature == nullptr, AMF_ALREADY_INITIALIZED, L"CreatePipeline() - m_pRootSignature is already initialized");
    AMF_RETURN_IF_FALSE(m_pPipelineState == nullptr, AMF_ALREADY_INITIALIZED, L"CreatePipeline() - m_pPipelineState is already initialized");

    m_rootSignatureLayout = layout;

    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

    // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

    if (FAILED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
    {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    // Allow input layout and deny uneccessary access to certain pipeline stages.
    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;


    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1((UINT)layout.rootParams.size(), layout.rootParams.data(), 0, nullptr, rootSignatureFlags);

    CComPtr<ID3DBlob> signature;
    CComPtr<ID3DBlob> error;
    HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);

    if (error)
    {
        amf_string errorMessage = reinterpret_cast<const char*>(error->GetBufferPointer());
        amf_wstring wMessage = amf_from_multibyte_to_unicode(errorMessage);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreatePipeline() - D3DX12SerializeVersionedRootSignature() failed: %s", wMessage.c_str());
    }
    else
    {
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreatePipeline() - D3DX12SerializeVersionedRootSignature() failed");
    }

    hr = m_pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_pRootSignature));
    CHECK_HRESULT_ERROR_RETURN(hr, L"CreatePipeline() - CreateRootSignature() failed.");

    amf_wstring rootSignatureName = debugName;
    rootSignatureName.append(L":RootSignature");
    SetName(m_pRootSignature, rootSignatureName.c_str());

    // Create graphics pipeline state
    desc.pRootSignature = m_pRootSignature;
    hr = m_pDevice->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_pPipelineState));
    CHECK_HRESULT_ERROR_RETURN(hr, L"CreatePipeline() - CreateGraphicsPipelineState() failed.");

    amf_wstring pipelineName = debugName;
    pipelineName.append(L":PipelineState");
    SetName(m_pPipelineState, pipelineName.c_str());

    return AMF_OK;
}
