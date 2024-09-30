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

#include "VideoRenderDX12.h"
#include "../common/CmdLogger.h"
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <float.h>

using namespace amf;
#pragma comment(lib, "d3d12.lib")

const char DX12_Shader[] =
"//--------------------------------------------------------------------------------------\n"
"// Constant Buffer Variables                                                            \n"
"//--------------------------------------------------------------------------------------\n"
"cbuffer ConstantBuffer : register( b0 )                                                 \n"
"{                                                                                       \n"
"    matrix World;                                                                       \n"
"    matrix View;                                                                        \n"
"    matrix Projection;                                                                  \n"
"}                                                                                       \n"
"                                                                                        \n"
"//--------------------------------------------------------------------------------------\n"
"struct VS_OUTPUT                                                                        \n"
"{                                                                                       \n"
"    float4 Pos : SV_POSITION;                                                           \n"
"    float4 Color : COLOR0;                                                              \n"
"};                                                                                      \n"
"                                                                                        \n"
"//--------------------------------------------------------------------------------------\n"
"// Vertex Shader                                                                        \n"
"//--------------------------------------------------------------------------------------\n"
"VS_OUTPUT VS( float3 Pos : POSITION, float4 Color : COLOR )                             \n"
"{                                                                                       \n"
"    VS_OUTPUT output = (VS_OUTPUT)0;                                                    \n"
"    output.Pos = mul( float4(Pos, 1.0f), World );                                       \n"
"    output.Pos = mul( output.Pos, View );                                               \n"
"    output.Pos = mul( output.Pos, Projection );                                         \n"
"    //output.Pos = Pos;                                                                 \n"
"    output.Color = Color;                                                               \n"
"    return output;                                                                      \n"
"}                                                                                       \n"
"                                                                                        \n"
"                                                                                        \n"
"//--------------------------------------------------------------------------------------\n"
"// Pixel Shader                                                                         \n"
"//--------------------------------------------------------------------------------------\n"
"float4 PS( VS_OUTPUT input ) : SV_Target                                                \n"
"{                                                                                       \n"
"    return input.Color;                                                                 \n"
"}                                                                                       \n"
;
using namespace DirectX;

struct Vertex
{
    XMFLOAT3 Pos;
    XMFLOAT4 Col;
};

struct ConstantBuffer
{
    XMMATRIX mWorld;
    XMMATRIX mView;
    XMMATRIX mProjection;
};

// Create vertex buffer and view
static Vertex vertices[] =
{
    { XMFLOAT3(-1.0f, 1.0f, -1.0f),   XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f) },
    { XMFLOAT3(1.0f, 1.0f, -1.0f),    XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f) },
    { XMFLOAT3(1.0f, 1.0f, 1.0f),     XMFLOAT4(0.0f, 1.0f, 1.0f, 1.0f) },
    { XMFLOAT3(-1.0f, 1.0f, 1.0f),    XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f) },
    { XMFLOAT3(-1.0f, -1.0f, -1.0f),  XMFLOAT4(1.0f, 0.0f, 1.0f, 1.0f) },
    { XMFLOAT3(1.0f, -1.0f, -1.0f),   XMFLOAT4(1.0f, 1.0f, 0.0f, 1.0f) },
    { XMFLOAT3(1.0f, -1.0f, 1.0f),    XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f) },
    { XMFLOAT3(-1.0f, -1.0f, 1.0f),   XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f) },
};
// Create index buffer
static WORD indices[] =
{ 3,1,0,  2,1,3,
    0,5,4,  1,5,0,
    3,4,7,  0,4,3,
    1,6,5,  2,6,1,
    2,7,6,  3,7,2,
    6,4,5,  7,4,6,
};
VideoRenderDX12::VideoRenderDX12(amf_int width, amf_int height, bool bInterlaced, amf_int frames, amf::AMFContext* pContext)
    :VideoRender(width, height, bInterlaced, frames, pContext),
    SwapChainDX12(pContext),
    m_fAnimation(0),
    m_bWindow(false)
{
}

VideoRenderDX12::~VideoRenderDX12()
{
    Terminate();
}

AMF_RESULT VideoRenderDX12::Init(amf_handle hWnd, amf_handle hDisplay, bool bFullScreen)
{
    AMF_RESULT res = AMF_OK;

    m_fAnimation = 0;

    if(m_width == 0 || m_height == 0)
    {
        LOG_ERROR(L"Bad width/height: width=" << m_width << L"height=" << m_height);
        return AMF_FAIL;
    }

    m_bWindow = hWnd != ::GetDesktopWindow();

    m_pDX12Device = static_cast<ID3D12Device*>(m_pContext2->GetDX12Device());


    res = m_RenderdesHeapPool.Init(m_pDX12Device);
    CHECK_AMF_ERROR_RETURN(res, L"m_RenderdesHeapPool Init() failed");

    res = RegisterDescriptors();
    CHECK_AMF_ERROR_RETURN(res, L"RegisterHeapDescriptors() failed");

    res = SwapChainDX12::Init(hWnd, hDisplay, nullptr, m_width, m_height, GetFormat(), bFullScreen);
    CHECK_AMF_ERROR_RETURN(res, L"SwapChainDX12::Init() failed");

    res = m_RenderCmdBuffer.Init(m_pDX12Device, D3D12_COMMAND_LIST_TYPE_DIRECT,
        SwapChainDX12::GetBackBufferCount(), L"DX12RenderCmdBuffer");
    CHECK_AMF_ERROR_RETURN(res, L"CommandBufferDX12::Init() failed");

    res = PrepareStates();
    CHECK_AMF_ERROR_RETURN(res, L"PrepareStates() failed");

    res = CreatePipeline();
    CHECK_AMF_ERROR_RETURN(res, L"CreatePipeline() failed");

    return AMF_OK;
}

AMF_RESULT VideoRenderDX12::Terminate()
{
    m_RenderCmdBuffer.WaitForExecution();

    m_VertexBuffer = {};
    m_IndexBuffer = {};
    m_ConstantBuffer= {};
    m_RenderCmdBuffer.Terminate();
    m_RenderdesHeapPool.Terminate();
    m_pPipelineState = nullptr;
    m_pRootSignature = nullptr;

    m_pDX12Device.Release();
    return AMF_OK;
}

AMF_RESULT VideoRenderDX12::RegisterDescriptors()
{
    DescriptorDX12 pHeaps = {};
    AMF_RESULT res = m_RenderdesHeapPool.RegisterDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, &pHeaps);
    CHECK_AMF_ERROR_RETURN(res, L"RegisterDescriptors() - RegisterDescriptors() failed to register SRV descriptors");

    res = m_RenderdesHeapPool.SetHeapDescriptorFlags(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
    CHECK_AMF_ERROR_RETURN(res, L"RegisterDescriptors() - SetHeapDescriptorFlags() failed to set shader visible flag for SRV/CBV heap");

    res = m_RenderdesHeapPool.CreateDescriptorHeaps();
    CHECK_AMF_ERROR_RETURN(res, L"RegisterDescriptors() - CreateDescriptorHeaps() failed");

    return AMF_OK;
}
AMF_RESULT VideoRenderDX12::CreateBuffer(ID3D12Resource** ppBuffer, ID3D12Resource** ppBufferUpload, const D3D12_RESOURCE_DESC& resourceDesc)
{
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

        CHECK_HRESULT_ERROR_RETURN(hr, L"CreateBuffer() - CreateCommittedResource() failed for vertex buffer");

        // Update the state in private data
        AMF_RESULT res = InitResourcePrivateData(*ppBuffer, initialState, 0);
        CHECK_AMF_ERROR_RETURN(res, L"CreateBuffer() - InitResourcePrivateData() failed");
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

        CHECK_HRESULT_ERROR_RETURN(hr, L"CreateBuffer() - CreateCommittedResource() failed for upload vertex buffer");
    }
    return AMF_OK;
}

AMF_RESULT VideoRenderDX12::UpdateBuffer(ID3D12Resource* pBuffer, ID3D12Resource* pBufferUpload, const void* pData, amf_size size, amf_size dstOffset, amf_bool immediate)
{
    UINT8* pMappedBuffer = nullptr;
    CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
    CD3DX12_RANGE writeRange(dstOffset, dstOffset + size);

    HRESULT hr = pBufferUpload->Map(0, &readRange, reinterpret_cast<void**>(&pMappedBuffer));

    if (SUCCEEDED(hr))
    {
        memcpy(pMappedBuffer + dstOffset, pData, size);
    }

    pBufferUpload->Unmap(0, &writeRange);

    CHECK_HRESULT_ERROR_RETURN(hr, L"UpdateBuffer() - Map() failed");

    AMF_RESULT res = m_RenderCmdBuffer.StartRecording(false);
    CHECK_AMF_ERROR_RETURN(res, L"UpdateBuffer() - StartRecording() failed");

    res = m_RenderCmdBuffer.SyncResource(pBuffer);
    CHECK_AMF_ERROR_RETURN(res, L"UpdateBuffer() - SyncResource() failed");

    res = TransitionResource(&m_RenderCmdBuffer, pBuffer, D3D12_RESOURCE_STATE_COPY_DEST, false);
    CHECK_AMF_ERROR_RETURN(res, L"UpdateBuffer() - TransitionResource() failed");

    m_RenderCmdBuffer.GetList()->CopyBufferRegion(pBuffer, 0, pBufferUpload, 0, size);

    if (immediate == true)
    {
        res = m_RenderCmdBuffer.Execute(SwapChainDX12::GetQueue(), true);
        CHECK_AMF_ERROR_RETURN(res, L"UpdateBuffer() - ExecuteCommandBuffer() after udate failed");
    }

    return AMF_OK;
}

AMF_RESULT VideoRenderDX12::PrepareStates()
{
    const CD3DX12_RESOURCE_DESC vertexDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(vertices));
    AMF_RESULT res = CreateBuffer(&m_VertexBuffer.pBuffer, &m_VertexBuffer.pUpload, vertexDesc);
    CHECK_AMF_ERROR_RETURN(res, L"PrepareStates() - VertexBuffer CreateBuffer() failed");
    // Initialize the vertex buffer views
    m_VertexBuffer.view.BufferLocation = m_VertexBuffer.pBuffer->GetGPUVirtualAddress();
    m_VertexBuffer.view.StrideInBytes = (UINT)sizeof(Vertex);
    m_VertexBuffer.view.SizeInBytes = (UINT)vertexDesc.Width;

    NAME_D3D12_OBJECT(m_VertexBuffer.pBuffer);
    NAME_D3D12_OBJECT(m_VertexBuffer.pUpload);
    res = UpdateBuffer(m_VertexBuffer.pBuffer, m_VertexBuffer.pUpload, vertices, sizeof(vertices), 0, false);
    CHECK_AMF_ERROR_RETURN(res, L"PrepareStates() - VertexBuffer UpdateBuffer() failed");

    // Create index buffer
    const CD3DX12_RESOURCE_DESC IndexDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(indices));
    res = CreateBuffer(&m_IndexBuffer.pBuffer, &m_IndexBuffer.pUpload, IndexDesc);
    CHECK_AMF_ERROR_RETURN(res, L"PrepareStates() - IndexBuffer CreateBuffer() failed");
    NAME_D3D12_OBJECT(m_IndexBuffer.pBuffer);
    NAME_D3D12_OBJECT(m_IndexBuffer.pUpload);
    // Initialize the index buffer views
    m_IndexBuffer.view.BufferLocation = m_IndexBuffer.pBuffer->GetGPUVirtualAddress();
    m_IndexBuffer.view.Format = DXGI_FORMAT_R16_UINT;
    m_IndexBuffer.view.SizeInBytes = (UINT)IndexDesc.Width;

    res = UpdateBuffer(m_IndexBuffer.pBuffer, m_IndexBuffer.pUpload, indices, sizeof(indices), 0, false);
    CHECK_AMF_ERROR_RETURN(res, L"PrepareStates() - IndexBuffer UpdateBuffer() failed");

    // Create constant buffer
    size_t bufferSize =
        ((sizeof(ConstantBuffer) + (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)) & ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1));
    const CD3DX12_RESOURCE_DESC cbDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    res = CreateBuffer(&m_ConstantBuffer.pBuffer, &m_ConstantBuffer.pUpload, cbDesc);
    CHECK_AMF_ERROR_RETURN(res, L"PrepareStates() - ConstantBuffer CreateBuffer() failed");
    NAME_D3D12_OBJECT(m_ConstantBuffer.pBuffer);
    NAME_D3D12_OBJECT(m_ConstantBuffer.pUpload);

    return AMF_OK;
}

AMF_RESULT VideoRenderDX12::CreatePipeline()
{
    // Define the vertex input layouts.
    constexpr D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;  // count of formats

    psoDesc.RTVFormats[0] = GetDXGIFormat();//DXGI_FORMAT_R8G8B8A8_UNORM ?
    psoDesc.SampleDesc.Count = 1;

    // Compile the vertex shader
    ATL::CComPtr<ID3DBlob> pVSBlob;
    AMF_RESULT res = CreateShader("VS", "vs_4_0", &pVSBlob);
    CHECK_AMF_ERROR_RETURN(res, L"CreateShaderFromResource(VS) failed");

    // Compile the pixel shader
    ATL::CComPtr<ID3DBlob> pPSBlob;
    res = CreateShader("PS", "ps_4_0", &pPSBlob);
    CHECK_AMF_ERROR_RETURN(res, L"CreateShaderFromResource(VS) failed");

    psoDesc.VS = { pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize() };
    psoDesc.PS = { pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize() };

    // Create root signature

    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

    // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

    if (FAILED(m_pDX12Device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
    {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    //Creating a descriptor table - for this we need to specify the range of descriptors -- maybe we don't need this at all
    CD3DX12_DESCRIPTOR_RANGE1 srvRange = {};
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE);

    CD3DX12_ROOT_PARAMETER1 cDescriptorParam = {};
    cDescriptorParam.InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

    //root descriptor for vertex shader's constant buffer b0 register
    CD3DX12_ROOT_PARAMETER1 cConstantParam = {};
    cConstantParam.InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);//D3D12_ROOT_PARAMETER_TYPE_CBV

    std::vector<D3D12_ROOT_PARAMETER1> rootParams{ cDescriptorParam, cConstantParam };// ?????????????????

    // Allow input layout and deny uneccessary access to certain pipeline stages.
    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;


    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1((UINT)rootParams.size(), rootParams.data(), 0, nullptr, rootSignatureFlags);

    CComPtr<ID3DBlob> signature;
    CComPtr<ID3DBlob> error;
    HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);

    if (error)
    {
        amf_string errorMessage = reinterpret_cast<const char*>(error->GetBufferPointer());
        amf_wstring wMessage = amf_from_multibyte_to_unicode(errorMessage);
        CHECK_HRESULT_ERROR_RETURN(hr, wMessage.c_str());
    }
    else
    {
        CHECK_HRESULT_ERROR_RETURN(hr, L"CreatePipeline() - D3DX12SerializeVersionedRootSignature() failed");
    }
    hr = m_pDX12Device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_pRootSignature));
    CHECK_HRESULT_ERROR_RETURN(hr, L"CreatePipeline() - CreateRootSignature() failed.");

    amf_wstring rootSignatureName = L"RenderingPipeline";
    rootSignatureName.append(L":RootSignature");
    SetName(m_pRootSignature, rootSignatureName.c_str());

    // Create graphics pipeline state
    psoDesc.pRootSignature = m_pRootSignature;
    hr = m_pDX12Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pPipelineState));
    CHECK_HRESULT_ERROR_RETURN(hr, L"CreatePipeline() - CreateGraphicsPipelineState() failed.");

    amf_wstring pipelineName = L"RenderingPipeline";
    pipelineName.append(L":PipelineState");


    return AMF_OK;
}

AMF_RESULT VideoRenderDX12::CreateShader(LPCSTR szEntryPoint, LPCSTR szModel, ID3DBlob** ppBlobOut)
{
    HRESULT hr = S_OK;
    DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
    ATL::CComPtr<ID3DBlob> pErrorBlob;

    hr = D3DCompile((LPCSTR)DX12_Shader, sizeof(DX12_Shader),
                    NULL, NULL, NULL, szEntryPoint, szModel,
                    dwShaderFlags, 0, ppBlobOut, &pErrorBlob);

    CHECK_HRESULT_ERROR_RETURN(hr, L"D3DCompile() failed");

    return AMF_OK;
}

AMF_RESULT VideoRenderDX12::RenderScene(BackBufferDX12* pTarget)
{
    amf::AMFContext2::AMFDX12Locker lock(m_pContext2);
    // Animate the cube
    m_fAnimation += XM_2PI / 240;
    XMMATRIX    world = XMMatrixTranspose(XMMatrixRotationRollPitchYaw(m_fAnimation, -m_fAnimation, 0));

    XMVECTOR Eye = XMVectorSet(0.0f, 1.0f, -5.0f, 0.0f);
    XMVECTOR At = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMVECTOR Up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX view = XMMatrixTranspose(XMMatrixLookAtLH(Eye, At, Up));

    // Update variables and constant buffer
    ConstantBuffer cb;
    cb.mWorld = world;
    cb.mView = view;
    cb.mProjection = XMMatrixTranspose(XMMatrixPerspectiveFovLH(XM_PIDIV2, m_width / (FLOAT)m_height, 0.01f, 100.0f));

    AMF_RESULT res = UpdateBuffer(m_ConstantBuffer.pBuffer, m_ConstantBuffer.pUpload, &cb, sizeof(cb), 0, false);
    CHECK_AMF_ERROR_RETURN(res, L"RenderScene() - StartRecording() failed");

    res = m_RenderCmdBuffer.StartRecording(false);
    CHECK_AMF_ERROR_RETURN(res, L"RenderScene() - ConstantBuffer UpdateBuffer() failed");

    m_RenderCmdBuffer.SyncResource(pTarget->pRtvBuffer);
    res = TransitionResource(&m_RenderCmdBuffer, pTarget->pRtvBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, false);
    CHECK_AMF_ERROR_RETURN(res, L"RenderScene() - Failed to transition pRtvBuffer to D3D12_RESOURCE_STATE_RENDER_TARGET");


    // Descriptor Heap should only be binded once (expensive operation)
    amf_vector<ID3D12DescriptorHeap*> pHeaps;
    res = m_RenderdesHeapPool.GetShaderDescriptorHeaps(pHeaps);
    CHECK_AMF_ERROR_RETURN(res, L"RenderScene() - GetShaderDescriptorHeaps() failed");
    m_RenderCmdBuffer.GetList()->SetDescriptorHeaps((UINT)pHeaps.size(), pHeaps.data());

    //Draw Background
    static constexpr amf_float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };//black
    m_RenderCmdBuffer.GetList()->ClearRenderTargetView(pTarget->rtvDescriptor.cpuHandle, ClearColor, 0, nullptr);

    //Set up pipeline
    m_RenderCmdBuffer.GetList()->SetPipelineState(m_pPipelineState);
    m_RenderCmdBuffer.GetList()->SetGraphicsRootSignature(m_pRootSignature);

    res = TransitionResource(&m_RenderCmdBuffer, m_ConstantBuffer.pBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    CHECK_AMF_ERROR_RETURN(res, L"RenderScene() - TransitionResource() failed");
    m_RenderCmdBuffer.GetList()->SetGraphicsRootConstantBufferView(1, m_ConstantBuffer.pBuffer->GetGPUVirtualAddress());

    // Update viewport
    const AMFSize size = SwapChainDX12::GetSize();
    D3D12_VIEWPORT currentViewport = {};
    currentViewport.TopLeftX = 0;
    currentViewport.TopLeftY = 0;
    currentViewport.Width = FLOAT(size.width);
    currentViewport.Height = FLOAT(size.height);
    currentViewport.MinDepth = 0.0f;
    currentViewport.MaxDepth = 1.0f;
    m_RenderCmdBuffer.GetList()->RSSetViewports(1, &currentViewport);
    D3D12_RECT rect = CD3DX12_RECT(0, 0, (LONG)currentViewport.Width, (LONG)currentViewport.Height);
    m_RenderCmdBuffer.GetList()->RSSetScissorRects(1, &rect);

    // Vertex buffer must be in the VERTEX_AND_CONSTANT_BUFFER resource state when binding
    res = TransitionResource(&m_RenderCmdBuffer, m_VertexBuffer.pBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    CHECK_AMF_ERROR_RETURN(res, L"RenderScene() - Failed to transition vertex buffer to D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER");
    m_RenderCmdBuffer.GetList()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_RenderCmdBuffer.GetList()->IASetVertexBuffers(0, 1, &m_VertexBuffer.view);

    // Index buffer must be in the INDEX_BUFFER resource state when binding
    res = TransitionResource(&m_RenderCmdBuffer, m_IndexBuffer.pBuffer, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    CHECK_AMF_ERROR_RETURN(res, L"RenderScene() - Failed to transition index buffer to D3D12_RESOURCE_STATE_INDEX_BUFFER");
    m_RenderCmdBuffer.GetList()->IASetIndexBuffer(&m_IndexBuffer.view);


    m_RenderCmdBuffer.GetList()->OMSetRenderTargets(1, &pTarget->rtvDescriptor.cpuHandle, FALSE, nullptr);
    m_RenderCmdBuffer.GetList()->DrawIndexedInstanced(amf_countof(indices), 1, 0, 0, 0);        // 36 vertices needed for 12 triangles in a triangle list


    res = m_RenderCmdBuffer.Execute(SwapChainDX12::GetQueue(), false);
    CHECK_AMF_ERROR_RETURN(res, L"RenderSurface() - Command buffer Execute() failed");

    return AMF_OK;
}


#define SQUARE_SIZE 50

AMF_RESULT VideoRenderDX12::Render(amf::AMFData** ppData)
{
#if !defined(_WIN64 )
// this is done to get identical results on 32 nad 64 bit builds
    _controlfp(_PC_24, MCW_PC);
#endif
    AMF_RESULT res = AMF_OK;

    CHECK_RETURN(SwapChainDX12::GetQueue() != nullptr, AMF_NOT_INITIALIZED, L"AMF_NOT_INITIALIZED() - Swapchain Graphics Queue is not initialized.");

    amf_uint32 imageIndex = 0;
    res = AcquireNextBackBufferIndex(imageIndex);
    CHECK_AMF_ERROR_RETURN(res, L"AcquireNextBackBufferIndex() failed");

    BackBufferDX12* pBackBuffer = (BackBufferDX12*)m_pBackBuffers[imageIndex].get();
    res = RenderScene(pBackBuffer);
    CHECK_AMF_ERROR_RETURN(res, L"RenderScene() failed");

    amf::AMFSurfacePtr pTmpSurface;
    res = m_pContext2->CreateSurfaceFromDX12Native(pBackBuffer->GetNative(), &pTmpSurface, nullptr);

    // Transition backbuffer to copy source state
    res = m_cmdBuffer.StartRecording();
    CHECK_AMF_ERROR_RETURN(res, L"Render() - Command Buffer StartRecording() failed");
    res = TransitionResource(&m_cmdBuffer, pBackBuffer->pRtvBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, true);
    CHECK_AMF_ERROR_RETURN(res, L"Render() - TransitionResource() failed");
    res = m_cmdBuffer.Execute(SwapChainDX12::GetQueue(), false);
    CHECK_AMF_ERROR_RETURN(res, L"Render() - Command Buffer Execute() failed");

    amf::AMFDataPtr pDuplicated;
    res = pTmpSurface->Duplicate(pTmpSurface->GetMemoryType(), &pDuplicated);
    CHECK_AMF_ERROR_RETURN(res, L"Render() - Failed to duplicate from backbuffer");

    *ppData = pDuplicated.Detach();
    if(m_bWindow)
    {
        res = SwapChainDX12::Present(false);
        CHECK_AMF_ERROR_RETURN(res, L"Render() - SwapChainDX12::Present() failed");
    }
    return res;
}
