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
#include "public/include/components/VideoDecoderUVD.h"
#include "public/include/components/VideoConverter.h"
#include "d3dx12.h"
#include <DirectXMath.h>
using namespace DirectX;

#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#define USE_COLOR_TWITCH_IN_DISPLAY 0

#define  AMF_FACILITY  L"VideoPresenterDX12"

namespace {

    struct SimpleVertex
    {
        XMFLOAT3 position;
        XMFLOAT2 texture;
    };
    struct CBNeverChanges
    {
        XMMATRIX mView;
    };

    extern const char* s_DX12_FullScreenQuad;
}

template<class T>
inline static T AlignValue(T value, T alignment)
{
    return ((value + (alignment - 1)) & ~(alignment - 1));
}

const float VideoPresenterDX12::ClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };


VideoPresenterDX12::VideoPresenterDX12(amf_handle hwnd, amf::AMFContext* pContext) :
    BackBufferPresenter(hwnd, pContext),
    SwapChainDX12(pContext),
    m_CurrentViewport{0},
    m_rectClientResize{0},
    m_fScale(1.0f),
    m_fPixelAspectRatio(1.0f),
    m_fOffsetX(0.0f),
    m_fOffsetY(0.0f),
    m_uiAvailableBackBuffer(0),
    m_uiBackBufferCount(4),
    m_bResizeSwapChain(true),
    m_bFirstFrame(true),
    m_eInputFormat(amf::AMF_SURFACE_RGBA)
{
}

VideoPresenterDX12::~VideoPresenterDX12()
{
    Terminate();
}

AMF_RESULT VideoPresenterDX12::Init(amf_int32 width, amf_int32 height, amf::AMFSurface* /* pSurface */)
{
    DXGI_FORMAT format = GetDXGIFormat(m_eInputFormat);
    CHECK_RETURN(width != 0 && height != 0 && format != DXGI_FORMAT_UNKNOWN, AMF_FAIL, L"Bad width/height: width=" << width << L" height=" << height << L" format" << amf::AMFSurfaceGetFormatName(m_eInputFormat));

    AMF_RESULT res = AMF_OK;

    res = VideoPresenter::Init(width, height);
    CHECK_AMF_ERROR_RETURN(res, L"= VideoPresenter::Init() failed");

    res = SwapChainDX12::Init(m_hwnd, m_hDisplay, false, width, height, format);
    CHECK_AMF_ERROR_RETURN(res, L"SwapChainDX12::Init() failed");

    res = CompileShaders();
    CHECK_AMF_ERROR_RETURN(res, L"CompileShaders() failed");

    res = CreateCommandBuffer();
    CHECK_AMF_ERROR_RETURN(res, L"CreateCommandBuffer() failed");

    res = PrepareStates();
    CHECK_AMF_ERROR_RETURN(res, L"PrepareStates() failed");

    ResizeSwapChain();

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::Present(amf::AMFSurface* pSurface)
{
    CHECK_RETURN(m_pSwapChain != nullptr, AMF_FAIL, L"DX12 SwapChain is not initialized.");

    AMF_RESULT res = AMF_OK;

    if(m_pDX12Device == NULL)
    {
        return AMF_NO_DEVICE;
    }
    if(pSurface->GetFormat() != GetInputFormat())
    {
        return AMF_INVALID_FORMAT;
    }
    if( (res = pSurface->Convert(GetMemoryType())) != AMF_OK)
    {
        res;
    }

    ApplyCSC(pSurface);

    {
        amf::AMFContext2::AMFDX12Locker dxlock(SwapChainDX12::m_pContext);

        amf::AMFPlanePtr pPlane = pSurface->GetPlane(amf::AMF_PLANE_PACKED);
        CComPtr<ID3D12Resource> pSrcSurface = (ID3D12Resource*)pPlane->GetNative();
        if (pSrcSurface == NULL)
        {
            return AMF_INVALID_POINTER;
        }

        AMFRect rectClient;
        rectClient = GetClientRect();
        ResetCommandBuffer();

        bool bResized = false;
        CheckForResize(false, &bResized);

        if (bResized)
        {
            res = ResizeSwapChain();
            CHECK_AMF_ERROR_RETURN(res, L"BitBlt() failed to resize swapchain.");
        }

        AMFRect srcRect = { pPlane->GetOffsetX(), pPlane->GetOffsetY(), pPlane->GetOffsetX() + pPlane->GetWidth(), pPlane->GetOffsetY() + pPlane->GetHeight() };
        AMFRect outputRect;
        CalcOutputRect(&srcRect, &rectClient, &outputRect);

        CComPtr<ID3D12Resource> pDstSurface;
        if (FAILED(m_pSwapChain->GetBuffer(m_frameIndex, IID_PPV_ARGS(&pDstSurface))))
        {
            return AMF_DIRECTX_FAILED;
        }

        res = BitBlt(pSurface->GetFrameType(), pSrcSurface, &srcRect, pDstSurface, &outputRect);
        CHECK_AMF_ERROR_RETURN(res, L"BitBlt().");
    }

    WaitForPTS(pSurface->GetPts());

    amf::AMFLock lock(&m_sect);
    SwapChainDX12::Present(m_frameIndex);

    if(m_bRenderToBackBuffer)
    {
        m_uiAvailableBackBuffer--;
    }
    return res;
}

AMF_RESULT VideoPresenterDX12::ResetCommandBuffer()
{
    HRESULT hr = S_OK;
    AMF_RESULT res = AMF_OK;

    m_cmdAllocator[m_frameIndex]->Reset();
    hr = m_cmdListGraphics->Reset(m_cmdAllocator[m_frameIndex], m_graphicsPipelineState);
    CHECK_HRESULT_ERROR_RETURN(hr, L"GragphicsCommandList Reset() failed.");
    return res;
}

AMF_RESULT VideoPresenterDX12::BitBlt(amf::AMF_FRAME_TYPE eFrameType, ID3D12Resource* pSrcSurface, AMFRect* pSrcRect, ID3D12Resource* pDstSurface, AMFRect* pDstRect)
{
    CHECK_RETURN(pSrcSurface != nullptr, AMF_FAIL, L"Source surface should not be nullptr.");
    CHECK_RETURN(pDstSurface != nullptr, AMF_FAIL, L"Destination surface should not be nullptr.");
    return BitBltRender(eFrameType, pSrcSurface, pSrcRect, pDstSurface, pDstRect);
}

AMF_RESULT VideoPresenterDX12::BitBltRender(amf::AMF_FRAME_TYPE /* eFrameType */, ID3D12Resource* pSrcSurface, AMFRect* pSrcRect, ID3D12Resource* pDstSurface, AMFRect* pDstRect)
{
    CHECK_RETURN(m_pDX12Device != nullptr, AMF_FAIL, L"DX12 Device is not initialized.");

    BackBuffer& backBuffer = m_BackBuffers[m_frameIndex];

    D3D12_RESOURCE_DESC srcDesc = pSrcSurface->GetDesc();
    D3D12_RESOURCE_DESC dstDesc = pDstSurface->GetDesc();

    AMFRect  newSourceRect = *pSrcRect;
    AMFSize srcSize ={(amf_int32)srcDesc.Width, (amf_int32)srcDesc.Height};
    AMFSize dstSize = { (amf_int32)dstDesc.Width, (amf_int32)dstDesc.Height };

    UpdateVertices(&newSourceRect, &srcSize, pDstRect, &dstSize);


    // Populate command list to render to intermediate render target.
    {
        TransitionResource(pSrcSurface, D3D12_RESOURCE_STATE_COMMON, true);
        TransitionResource(pSrcSurface, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, false);
        TransitionResource(m_pVertexBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        TransitionResource(backBuffer.rtvBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);

        // Create a SRV for uav backbuffers.
        {

            CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_cbvSrvDescriptorSize);

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = srcDesc.Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = srcDesc.MipLevels;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.PlaneSlice = 0;
            srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
            m_pDX12Device->CreateShaderResourceView(pSrcSurface, &srvDesc, srvHandle);
        }

        m_cmdListGraphics->SetGraphicsRootSignature(m_rootSignature);

        /// Descriptor Heap
        {
            ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvHeap.p };
            m_cmdListGraphics->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

            CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), m_frameIndex, m_cbvSrvDescriptorSize);
            m_cmdListGraphics->SetGraphicsRootDescriptorTable(GraphicsRootSRVTable, srvHandle);

            m_cmdListGraphics->SetGraphicsRootConstantBufferView(GraphicsRootCBV, m_pCBChangesOnResize->GetGPUVirtualAddress());

            CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
            m_cmdListGraphics->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
            m_cmdListGraphics->ClearRenderTargetView(rtvHandle, ClearColor, 0, nullptr);
        }

        /// Input Assembly
        {
            m_cmdListGraphics->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            m_cmdListGraphics->IASetVertexBuffers(0, 1, &m_vertexBufferView);
        }

        /// Draw area
        {
            m_cmdListGraphics->RSSetViewports(1, &m_CurrentViewport);
            D3D12_RECT rect = CD3DX12_RECT(pDstRect->left, pDstRect->top, pDstRect->right, pDstRect->bottom);
            m_cmdListGraphics->RSSetScissorRects(1, &rect);
        }

        // Draw Quad vertex count 4, instance count 1
        m_cmdListGraphics->DrawInstanced(4, 1, 0, 0);

        // PIP window
        if (m_bEnablePIP)
        {
            m_cmdListGraphics->SetGraphicsRootSignature(m_rootSignatureNN);
            m_cmdListGraphics->SetPipelineState(m_graphicsPipelineStateNN);

            D3D12_VIEWPORT pipViewport = m_CurrentViewport;
            CD3DX12_RECT pipScissor = CD3DX12_RECT(pDstRect->left, pDstRect->top, pDstRect->right, pDstRect->bottom);

            pipViewport.Width = m_CurrentViewport.Width * c_pipSize;
            pipViewport.Height = m_CurrentViewport.Height * c_pipSize;
            pipScissor.right = static_cast<LONG>(pipScissor.right * c_pipSize);
            pipScissor.bottom = static_cast<LONG>(pipScissor.bottom * c_pipSize);

            m_cmdListGraphics->RSSetViewports(1, &pipViewport);
            m_cmdListGraphics->RSSetScissorRects(1, &pipScissor);
            m_cmdListGraphics->IASetVertexBuffers(0, 1, &m_PIPVertexBufferView);

            // Draw Quad vertex count 4, instance count 1
            m_cmdListGraphics->DrawInstanced(4, 1, 0, 0);
        }

        TransitionResource(pSrcSurface, D3D12_RESOURCE_STATE_COMMON, false);

    }

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::Terminate()
{
    m_hwnd = nullptr;

    m_pVertexShader = nullptr;
    m_pPixelShader = nullptr;

    m_pVertexBuffer = nullptr;
    m_pVertexBufferUpload = nullptr;
    m_pCBChangesOnResize = nullptr;

    m_vertexBufferView = {};
    m_CurrentViewport = {};

    m_pPIPVertexBuffer = nullptr;
    m_pPIPVertexBufferUpload = nullptr;
    m_PIPVertexBufferView = {};

    m_fScale = 1.0f;
    m_fPixelAspectRatio = 1.0f;
    m_fOffsetX = 0.f;
    m_fOffsetY = 0.f;

    m_uiAvailableBackBuffer = 0;
    m_uiBackBufferCount = 0;

    m_rectClientResize = {};

    SwapChainDX12::Terminate();
    return VideoPresenter::Terminate();
}

AMF_RESULT VideoPresenterDX12::CompileShaders()
{
    CHECK_RETURN(m_pDX12Device != nullptr, AMF_FAIL, L"DX12 Device is not initialized.");

    HRESULT hr = S_OK;

    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

    // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

    if (FAILED(m_pDX12Device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
    {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    // Create a root signature consisting of a descriptor table with a single CBV.
    {
        CD3DX12_DESCRIPTOR_RANGE1 ranges[1] = {};
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE);

        CD3DX12_ROOT_PARAMETER1 rootParameters[GraphicsRootParametersCount] = {};
        rootParameters[GraphicsRootSRVTable].InitAsDescriptorTable(_countof(ranges), &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
        rootParameters[GraphicsRootCBV].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);

        // Allow input layout and deny uneccessary access to certain pipeline stages.
        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        // Create static samplers.
        CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, rootSignatureFlags);

        CComPtr<ID3DBlob> signature;
        CComPtr<ID3DBlob> error;
        hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);
        if(error)
        {
            amf_string message = reinterpret_cast<const char*>(error->GetBufferPointer());
            amf_wstring wMessage = amf_from_multibyte_to_unicode(message);
        }
        CHECK_HRESULT_ERROR_RETURN(hr, L"D3DX12SerializeVersionedRootSignature() failed.");

        hr = m_pDX12Device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));
        CHECK_HRESULT_ERROR_RETURN(hr, L"CreateRootSignature() failed.");

        // Create static samplers for point sampling.
        CD3DX12_STATIC_SAMPLER_DESC samplerNN(0, D3D12_FILTER_MIN_MAG_MIP_POINT);
        samplerNN.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        samplerNN.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerNN.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerNN.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerNN.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;

        //CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &samplerNN, rootSignatureFlags);

        CComPtr<ID3DBlob> pSignatureNN;
        hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &pSignatureNN, &error);
        if (error)
        {
            amf_string message = reinterpret_cast<const char*>(error->GetBufferPointer());
            amf_wstring wMessage = amf_from_multibyte_to_unicode(message);
        }
        CHECK_HRESULT_ERROR_RETURN(hr, L"D3DX12SerializeVersionedRootSignature() failed.");

        hr = m_pDX12Device->CreateRootSignature(0, pSignatureNN->GetBufferPointer(), pSignatureNN->GetBufferSize(), IID_PPV_ARGS(&m_rootSignatureNN));
        CHECK_HRESULT_ERROR_RETURN(hr, L"CreateRootSignature() failed.");
    }

    // Create the pipeline state, which includes compiling and loading shaders.
    {
        CComPtr<ID3DBlob> error;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif
        hr = D3DCompile(s_DX12_FullScreenQuad, strlen(s_DX12_FullScreenQuad), nullptr, nullptr, nullptr, "VS", "vs_5_0", compileFlags, 0, &m_pVertexShader, &error);
        CHECK_HRESULT_ERROR_RETURN(hr, L"D3DCompile() vertex shader failed.");

        hr = D3DCompile(s_DX12_FullScreenQuad, strlen(s_DX12_FullScreenQuad), nullptr, nullptr, nullptr, "PS", "ps_5_0", compileFlags, 0, &m_pPixelShader, &error);
        CHECK_HRESULT_ERROR_RETURN(hr, L"D3DCompile() pixel shader failed.");

        // Define the vertex input layouts.
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        // Describe and create the graphics pipeline state objects (PSOs).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
        psoDesc.pRootSignature = m_rootSignature;
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_pVertexShader);
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_pPixelShader);
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;  // count of formats
        psoDesc.RTVFormats[0] = m_format;
        psoDesc.SampleDesc.Count = 1;

        hr = m_pDX12Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_graphicsPipelineState));
        CHECK_HRESULT_ERROR_RETURN(hr, L"CreateGraphicsPipelineState() failed.");

        // Create graphics pipeline state with point sampling (nearest neighbor) sampler.
        psoDesc.pRootSignature = m_rootSignatureNN;
        hr = m_pDX12Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_graphicsPipelineStateNN));
        CHECK_HRESULT_ERROR_RETURN(hr, L"CreateGraphicsPipelineState() failed.");
    }

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::PrepareStates()
{
    CHECK_RETURN(m_pDX12Device != nullptr, AMF_FAIL, L"DX12 Device is not initialized.");
    CHECK_RETURN(m_cmdListGraphics != nullptr, AMF_FAIL, L"DX12 graphics command list is not initialized.");

    HRESULT hr = S_OK;

    // Create vertex buffer & view
    {
        const CD3DX12_RESOURCE_DESC vertexDesc = CD3DX12_RESOURCE_DESC::Buffer(
            sizeof(SimpleVertex) * IndicesCount
        );

        {
            CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
            hr = m_pDX12Device->CreateCommittedResource(
                &heapProperties,
                D3D12_HEAP_FLAG_NONE,
                &vertexDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&m_pVertexBuffer));
        }

        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COPY_DEST;
        m_pVertexBuffer->SetPrivateData(AMFResourceStateGUID, sizeof(D3D12_RESOURCE_STATES), &initialState);

        {
            CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_UPLOAD);
            hr = m_pDX12Device->CreateCommittedResource(
                &heapProperties,
                D3D12_HEAP_FLAG_NONE,
                &vertexDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&m_pVertexBufferUpload));
        }
        // Initialize the vertex buffer views.
        m_vertexBufferView.BufferLocation = m_pVertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof(SimpleVertex);
        m_vertexBufferView.SizeInBytes = (UINT)vertexDesc.Width;
    }

    // Create PIP vertex buffer and view
    if (m_bEnablePIP)
    {
        PreparePIPStates();
    }

    // Initialize the view matrix
    {
        XMMATRIX worldViewProjection = XMMatrixIdentity();
        size_t bufferSize = AlignValue(sizeof(CBNeverChanges), (size_t)D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        const CD3DX12_RESOURCE_DESC cbDesc = CD3DX12_RESOURCE_DESC::Buffer(
            bufferSize
        );

        {
            CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
            hr = m_pDX12Device->CreateCommittedResource(
                &heapProperties,
                D3D12_HEAP_FLAG_NONE,
                &cbDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&m_pCBChangesOnResize));
        }
        NAME_D3D12_OBJECT(m_pCBChangesOnResize);


        CComPtr<ID3D12Resource> cbUpload;
        {
            CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_UPLOAD);
            hr = m_pDX12Device->CreateCommittedResource(
                &heapProperties,
                D3D12_HEAP_FLAG_NONE,
                &cbDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&cbUpload));
        }

        UINT8* pBuffer = nullptr;
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        hr = cbUpload->Map(0, &readRange, reinterpret_cast<void**>(&pBuffer));
        memcpy(pBuffer, &worldViewProjection, sizeof(CBNeverChanges));
        cbUpload->Unmap(0, nullptr);

        // CBV CPU handle
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_pCBChangesOnResize->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = (UINT)bufferSize;

        CD3DX12_CPU_DESCRIPTOR_HANDLE cbvHandle(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), CbvViewMatrix, m_cbvSrvDescriptorSize);
        m_pDX12Device->CreateConstantBufferView(&cbvDesc, cbvHandle);

        ResetCommandBuffer();

        m_cmdListGraphics->CopyBufferRegion(m_pCBChangesOnResize, 0, cbUpload, 0, cbDesc.Width);

        D3D12_RESOURCE_BARRIER barriers[] = {
         CD3DX12_RESOURCE_BARRIER::Transition(m_pCBChangesOnResize, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
        };

        m_cmdListGraphics->ResourceBarrier(_countof(barriers), barriers);

        m_cmdListGraphics->Close();

        ID3D12CommandList* ppCommandLists[] = { m_cmdListGraphics };
        m_graphicsQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

        SwapChainDX12::WaitForGpu();
    }
    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::PreparePIPStates()
{
    HRESULT hr = S_OK;

    const CD3DX12_RESOURCE_DESC vertexDesc = CD3DX12_RESOURCE_DESC::Buffer(
        sizeof(SimpleVertex) * IndicesCount
    );

    {
        CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
        hr = m_pDX12Device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &vertexDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_pPIPVertexBuffer));
    }

    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COPY_DEST;
    m_pPIPVertexBuffer->SetPrivateData(AMFResourceStateGUID, sizeof(D3D12_RESOURCE_STATES), &initialState);

    {
        CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_UPLOAD);
        hr = m_pDX12Device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &vertexDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_pPIPVertexBufferUpload));
    }

    // Initialize the vertex buffer views.
    m_PIPVertexBufferView.BufferLocation = m_pPIPVertexBuffer->GetGPUVirtualAddress();
    m_PIPVertexBufferView.StrideInBytes = sizeof(SimpleVertex);
    m_PIPVertexBufferView.SizeInBytes = (UINT)vertexDesc.Width;

    return AMF_OK;
}

AMF_RESULT   VideoPresenterDX12::CheckForResize(bool bForce, bool *bResized)
{
    CHECK_RETURN(m_pSwapChain != nullptr, AMF_FAIL, L"DX12 SwapChain is not initialized.");

    *bResized = false;
    HRESULT hr = S_OK;

    AMFRect client;
    if(m_hwnd != NULL)
    {
        client = GetClientRect();
    }
    else
    {
        DXGI_SWAP_CHAIN_DESC1 SwapDesc;
        m_pSwapChain->GetDesc1(&SwapDesc);
        client.left=0;
        client.top=0;
        client.right=SwapDesc.Width;
        client.bottom=SwapDesc.Height;
    }
    amf_int width=client.right-client.left;
    amf_int height=client.bottom-client.top;


    m_CurrentViewport.TopLeftX = 0;
    m_CurrentViewport.TopLeftY = 0;
    m_CurrentViewport.Width = FLOAT(width);
    m_CurrentViewport.Height = FLOAT(height);
    m_CurrentViewport.MinDepth = 0.0f;
    m_CurrentViewport.MaxDepth = 1.0f;

    CComPtr<ID3D12Resource> spBuffer;
    hr = m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&spBuffer));
    CHECK_HRESULT_ERROR_RETURN(hr, L"SwapChain GetBuffer() failed.");

    D3D12_RESOURCE_DESC bufferDesc = spBuffer->GetDesc();

    if(!bForce && ((width==(amf_int)bufferDesc.Width && height==(amf_int)bufferDesc.Height) || width == 0 || height == 0 ))
    {
        return AMF_OK;
    }

    *bResized = true;
    m_bResizeSwapChain = true;
    return AMF_OK;
}


AMF_RESULT VideoPresenterDX12::ResizeSwapChain()
{
    AMFRect clientRect;

    if(m_hwnd!=NULL)
    {

        clientRect = GetClientRect();
    }
    else
    {
        DXGI_SWAP_CHAIN_DESC1 SwapDesc;
        m_pSwapChain->GetDesc1(&SwapDesc);
        clientRect.left=0;
        clientRect.top=0;
        clientRect.right=SwapDesc.Width;
        clientRect.bottom=SwapDesc.Height;
    }
    amf_int width=clientRect.right-clientRect.left;
    amf_int height=clientRect.bottom-clientRect.top;

    AMF_RESULT res = SwapChainDX12::ResizeSwapChain(m_eInputFormat, width, height);

    m_rectClient = clientRect;
    return res;
}


AMF_RESULT VideoPresenterDX12::UpdateVertices(AMFRect *srcRect, AMFSize *srcSize, AMFRect *dstRect, AMFSize *dstSize)
{
    CHECK_RETURN(m_pDX12Device != nullptr, AMF_FAIL, L"DX12 Device is not initialized.");
    CHECK_RETURN(m_cmdListGraphics != nullptr, AMF_FAIL, L"DX12 graphics command list is not initialized.");

    if(!m_bEnablePIP && *srcRect == m_sourceVertexRect  &&  *dstRect == m_destVertexRect)
    {
        return AMF_OK;
    }
    m_sourceVertexRect = *srcRect;
    m_destVertexRect = *dstRect;

    HRESULT hr=S_OK;

    SimpleVertex vertices[IndicesCount];

    // stretch video rect to back buffer
    FLOAT  w=2.f;
    FLOAT  h=2.f;

    w *= m_fScale;
    h *= m_fScale;

    w *= (float)dstRect->Width() / dstSize->width;
    h *= (float)dstRect->Height() / dstSize->height;

    FLOAT centerX = m_fOffsetX * 2.f / dstRect->Width();
    FLOAT centerY = - m_fOffsetY * 2.f/ dstRect->Height();

    FLOAT leftDst = centerX - w / 2;
    FLOAT rightDst = leftDst + w;
    FLOAT topDst = centerY - h / 2;
    FLOAT bottomDst = topDst + h;

    centerX = (FLOAT)(srcRect->left + srcRect->right) / 2.f / srcRect->Width();
    centerY = (FLOAT)(srcRect->top + srcRect->bottom) / 2.f / srcRect->Height();

    w = (FLOAT)srcRect->Width() / srcSize->width;
    h = (FLOAT)srcRect->Height() / srcSize->height;

    FLOAT leftSrc = centerX - w/2;
    FLOAT rightSrc = leftSrc + w;
    FLOAT topSrc = centerY - h/2;
    FLOAT bottomSrc = topSrc + h;

    vertices[0].position = XMFLOAT3(leftDst, bottomDst, 0.0f);
    vertices[0].texture = XMFLOAT2(leftSrc, topSrc);

    vertices[1].position = XMFLOAT3(rightDst, bottomDst, 0.0f);
    vertices[1].texture = XMFLOAT2(rightSrc, topSrc);

    vertices[2].position = XMFLOAT3(leftDst, topDst, 0.0f);
    vertices[2].texture = XMFLOAT2(leftSrc, bottomSrc);

    // Second triangle.
    vertices[3].position = XMFLOAT3(rightDst, topDst, 0.0f);
    vertices[3].texture = XMFLOAT2(rightSrc, bottomSrc);

    UINT8* pVertexData;
    CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
    hr = m_pVertexBufferUpload->Map(0, &readRange, reinterpret_cast<void**>(&pVertexData));
    memcpy(pVertexData, vertices, sizeof(vertices));
    m_pVertexBufferUpload->Unmap(0, nullptr);

    TransitionResource(m_pVertexBuffer, D3D12_RESOURCE_STATE_COPY_DEST);

    m_cmdListGraphics->CopyBufferRegion(m_pVertexBuffer, 0, m_pVertexBufferUpload, 0, sizeof(vertices));

    // PIP vertex data
    if (m_bEnablePIP)
    {
        UpdatePIPVertices(srcRect, srcSize, dstRect, dstSize);
    }

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX12::UpdatePIPVertices(AMFRect* srcRect, AMFSize* srcSize, AMFRect* dstRect, AMFSize* dstSize)
{
    if (m_pPIPVertexBuffer == nullptr || m_pPIPVertexBufferUpload == nullptr)
    {
        PreparePIPStates();
    }

    HRESULT hr = S_OK;
    SimpleVertex vertices[IndicesCount];

    memset(vertices, 0, sizeof(vertices));

    // stretch video rect to back buffer
    FLOAT  w=2.f;
    FLOAT  h=2.f;

    w *= m_fScale;
    h *= m_fScale;

    w *= (float)dstRect->Width() / dstSize->width;
    h *= (float)dstRect->Height() / dstSize->height;

    FLOAT centerX = m_fOffsetX * 2.f / dstRect->Width();
    FLOAT centerY = -m_fOffsetY * 2.f / dstRect->Height();

    FLOAT leftDst = centerX - w / 2;
    FLOAT rightDst = leftDst + w * 1;
    FLOAT topDst = centerY - h / 2;
    FLOAT bottomDst = topDst + h * 1;

    centerX = (FLOAT)(srcRect->left + srcRect->right) / 2.f / srcRect->Width();
    centerY = (FLOAT)(srcRect->top + srcRect->bottom) / 2.f / srcRect->Height();

    w = (FLOAT)srcRect->Width() / srcSize->width;
    h = (FLOAT)srcRect->Height() / srcSize->height;
    FLOAT leftSrc = m_fPIPFocusPos.x + centerX - w / 2;
    FLOAT rightSrc = leftSrc + w * c_pipSize / m_iPIPZoomFactor;
    FLOAT topSrc = m_fPIPFocusPos.y + centerY - h / 2;
    FLOAT bottomSrc = topSrc + h * c_pipSize / m_iPIPZoomFactor;

    vertices[0].position = XMFLOAT3(leftDst, bottomDst, 0.0f);
    vertices[0].texture = XMFLOAT2(leftSrc, topSrc);

    vertices[1].position = XMFLOAT3(rightDst, bottomDst, 0.0f);
    vertices[1].texture = XMFLOAT2(rightSrc, topSrc);

    vertices[2].position = XMFLOAT3(leftDst, topDst, 0.0f);
    vertices[2].texture = XMFLOAT2(leftSrc, bottomSrc);

    vertices[3].position = XMFLOAT3(rightDst, topDst, 0.0f);
    vertices[3].texture = XMFLOAT2(rightSrc, bottomSrc);

    UINT8* pVertexData = NULL;
    CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
    hr = m_pPIPVertexBufferUpload->Map(0, &readRange, reinterpret_cast<void**>(&pVertexData));
    memcpy(pVertexData, vertices, sizeof(vertices));
    m_pPIPVertexBufferUpload->Unmap(0, nullptr);

    TransitionResource(m_pPIPVertexBuffer, D3D12_RESOURCE_STATE_COPY_DEST);

    m_cmdListGraphics->CopyBufferRegion(m_pPIPVertexBuffer, 0, m_pPIPVertexBufferUpload, 0, sizeof(vertices));

    return AMF_OK;
}


AMF_RESULT              VideoPresenterDX12::SetInputFormat(amf::AMF_SURFACE_FORMAT format)
{
    if(format != amf::AMF_SURFACE_BGRA && format != amf::AMF_SURFACE_RGBA  && format != amf::AMF_SURFACE_RGBA_F16)
    {
        return AMF_FAIL;
    }
    m_eInputFormat = format;
    return AMF_OK;
}

AMF_RESULT  VideoPresenterDX12::Flush()
{
    m_uiAvailableBackBuffer = 0;
    return BackBufferPresenter::Flush();
}


AMF_RESULT VideoPresenterDX12::CreateCommandBuffer()
{
    CHECK_RETURN(m_pDX12Device != nullptr, AMF_FAIL, L"DX12 Device is not initialized.");

    HRESULT hr = S_OK;

    // Create the command lists.
    {
        for (int i = 0; i < FrameCount; i++)
        {
            hr = m_pDX12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_cmdAllocator[i].p));
            NAME_D3D12_OBJECT_INDEXED(m_cmdAllocator, i);
            CHECK_HRESULT_ERROR_RETURN(hr, L"CreateCommandAllocator() failed.");
        }

        hr = m_pDX12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_cmdAllocator[0], m_graphicsPipelineState, IID_PPV_ARGS(&m_cmdListGraphics));
        CHECK_HRESULT_ERROR_RETURN(hr, L"CreateCommandList() failed.");
        NAME_D3D12_OBJECT(m_cmdListGraphics);

        // Close the command lists.
        m_cmdListGraphics->Close();
    }

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        hr = m_pDX12Device->CreateFence(m_fenceValue, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&m_fence));
        NAME_D3D12_OBJECT(m_fence);
        m_fenceValue++;

        // Create an event handle to use for frame synchronization.
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr)
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
        }
    }
    return AMF_OK;
}

// to enble this code switch to Windows 10 SDK for Windows 10 RS2 (10.0.15063.0) or later.
#if defined(NTDDI_WIN10_RS2)
#include <DXGI1_6.h>
#endif


void        VideoPresenterDX12::UpdateProcessor()
{
    VideoPresenter::UpdateProcessor();
#if defined(NTDDI_WIN10_RS2)
    if(m_pProcessor != NULL && m_pDX12Device != NULL)
    {
#if USE_COLOR_TWITCH_IN_DISPLAY
        ATL::CComQIPtr<IDXGISwapChain3> pSwapChain3(m_pSwapChain);
        if(pSwapChain3 != NULL)
        {
            UINT supported[20];
            for(int i = 0; i < 20; i++)
            {
                pSwapChain3->CheckColorSpaceSupport((DXGI_COLOR_SPACE_TYPE)i, &supported[i]);
            }
            if(GetInputFormat() == amf::AMF_SURFACE_RGBA_F16 &&
                (supported[DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709] & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
            {

                DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
                {


                    pSwapChain3->SetColorSpace1(colorSpace);
                    m_pProcessor->SetProperty(AMF_VIDEO_CONVERTER_USE_DECODER_HDR_METADATA, false);
                }
            }
        }
#endif
        // check and set color space and HDR support

        UINT adapterIndex = 0;
        for(;;adapterIndex++)
        {
            ATL::CComPtr<IDXGIOutput> spDXGIOutput;
            m_dxgiAdapter->EnumOutputs(adapterIndex, &spDXGIOutput);
            if(spDXGIOutput == NULL)
            {
                break;
            }
            ATL::CComQIPtr<IDXGIOutput6> spDXGIOutput6(spDXGIOutput);
            if(spDXGIOutput6 != NULL)
            {
                DXGI_OUTPUT_DESC1 desc = {};
                spDXGIOutput6->GetDesc1(&desc);
                if (desc.MaxLuminance != 0)
                {
                    amf::AMFBufferPtr pBuffer;
                    SwapChainDX12::m_pContext->AllocBuffer(amf::AMF_MEMORY_HOST, sizeof(AMFHDRMetadata), &pBuffer);
                    AMFHDRMetadata* pHDRData = (AMFHDRMetadata*)pBuffer->GetNative();

                    pHDRData->redPrimary[0] = amf_uint16(desc.RedPrimary[0] * 50000.f);
                    pHDRData->redPrimary[1] = amf_uint16(desc.RedPrimary[1] * 50000.f);

                    pHDRData->greenPrimary[0] = amf_uint16(desc.GreenPrimary[0] * 50000.f);
                    pHDRData->greenPrimary[1] = amf_uint16(desc.GreenPrimary[1] * 50000.f);

                    pHDRData->bluePrimary[0] = amf_uint16(desc.BluePrimary[0] * 50000.f);
                    pHDRData->bluePrimary[1] = amf_uint16(desc.BluePrimary[1] * 50000.f);

                    pHDRData->whitePoint[0] = amf_uint16(desc.WhitePoint[0] * 50000.f);
                    pHDRData->whitePoint[1] = amf_uint16(desc.WhitePoint[1] * 50000.f);

                    pHDRData->maxMasteringLuminance = amf_uint32(desc.MaxLuminance * 10000.f);
                    pHDRData->minMasteringLuminance = amf_uint32(desc.MinLuminance * 10000.f);
                    pHDRData->maxContentLightLevel = 0;
                    pHDRData->maxFrameAverageLightLevel = amf_uint16(desc.MaxFullFrameLuminance * 10000.f);


                    m_pProcessor->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_HDR_METADATA, pBuffer);
                }
                AMF_COLOR_TRANSFER_CHARACTERISTIC_ENUM colorTransfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_BT709;
                AMF_COLOR_PRIMARIES_ENUM primaries = AMF_COLOR_PRIMARIES_UNDEFINED;

                switch (desc.ColorSpace)
                {
                case  DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
                    colorTransfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_GAMMA22;
                    primaries = AMF_COLOR_PRIMARIES_BT709;
                    AMFTraceInfo(AMF_FACILITY, L"DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709");
                    break;
                case  DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:
                    colorTransfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_LINEAR;
                    primaries = AMF_COLOR_PRIMARIES_BT709;
                    AMFTraceInfo(AMF_FACILITY, L"DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709");
                    break;
                case  DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709:
                    colorTransfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_GAMMA22;
                    primaries = AMF_COLOR_PRIMARIES_BT709;
                    AMFTraceInfo(AMF_FACILITY, L"DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709");
                    break;
                case  DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020:
                    colorTransfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_GAMMA22;
                    primaries = AMF_COLOR_PRIMARIES_BT2020;
                    AMFTraceInfo(AMF_FACILITY, L"DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020");
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
                    colorTransfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE2084;
                    primaries = AMF_COLOR_PRIMARIES_BT2020;
                    AMFTraceInfo(AMF_FACILITY, L"DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020");
                    break;
                    //    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020:
                case  DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020:
                    colorTransfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE2084;
                    primaries = AMF_COLOR_PRIMARIES_BT2020;
                    AMFTraceInfo(AMF_FACILITY, L"DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020");
                    break;
                    //    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020:
            //    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020:
                case  DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020:
                    colorTransfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_GAMMA22;
                    primaries = AMF_COLOR_PRIMARIES_BT2020;
                    AMFTraceInfo(AMF_FACILITY, L"DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020");
                    break;
                    //    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020:
            //    case  DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020:
                case  DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709:
                    //        colorTransfer =
                    primaries = AMF_COLOR_PRIMARIES_BT709;
                    AMFTraceInfo(AMF_FACILITY, L"DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709");
                    break;
                case  DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020:
                    //        colorTransfer =
                    primaries = AMF_COLOR_PRIMARIES_BT2020;
                    AMFTraceInfo(AMF_FACILITY, L"DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020");
                    break;
                    //    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P709:
                    //    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P2020:
                    //    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_TOPLEFT_P2020:

                }
                if (GetInputFormat() == amf::AMF_SURFACE_RGBA_F16)
                {
                    colorTransfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_LINEAR;
                }

                m_pProcessor->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_TRANSFER_CHARACTERISTIC, colorTransfer);
                m_pProcessor->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_COLOR_PRIMARIES, primaries);

            }
        }
    }
#endif
}
AMF_RESULT VideoPresenterDX12::ApplyCSC(amf::AMFSurface* pSurface)
{
#if defined(NTDDI_WIN10_RS2)
#ifdef USE_COLOR_TWITCH_IN_DISPLAY
    if(m_bFirstFrame)
    {
        amf::AMFVariant varBuf;
        if(pSurface->GetProperty(AMF_VIDEO_DECODER_HDR_METADATA, &varBuf) == AMF_OK)
        {
            amf::AMFBufferPtr pBuffer(varBuf.pInterface);
            if(pBuffer != NULL)
            {
                AMFHDRMetadata* pHDRData = (AMFHDRMetadata*)pBuffer->GetNative();
                // check and set color space
                ATL::CComQIPtr<IDXGISwapChain4> pSwapChain4(m_pSwapChain);
                if(pSwapChain4 != NULL)
                {

                    DXGI_HDR_METADATA_HDR10 metadata = {};
                    metadata.WhitePoint[0] = pHDRData->whitePoint[0];
                    metadata.WhitePoint[1] = pHDRData->whitePoint[1];
                    metadata.RedPrimary[0] = pHDRData->redPrimary[0];
                    metadata.RedPrimary[1] = pHDRData->redPrimary[1];
                    metadata.GreenPrimary[0] = pHDRData->greenPrimary[0];
                    metadata.GreenPrimary[1] = pHDRData->greenPrimary[1];
                    metadata.BluePrimary[0] = pHDRData->bluePrimary[0];
                    metadata.BluePrimary[1] = pHDRData->bluePrimary[1];
                    metadata.MaxMasteringLuminance = pHDRData->maxMasteringLuminance;
                    metadata.MinMasteringLuminance = pHDRData->minMasteringLuminance;
                    metadata.MaxContentLightLevel = pHDRData->maxContentLightLevel;
                    metadata.MaxFrameAverageLightLevel = pHDRData->maxFrameAverageLightLevel;

                    pSwapChain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(metadata), &metadata);
                }
                m_bFirstFrame = false;
            }
        }
    }
#endif
#endif
    return AMF_OK;
}

namespace {

const char* s_DX12_FullScreenQuad =
"//--------------------------------------------------------------------------------------\n"
"// Constant Buffer Variables                                                            \n"
"//--------------------------------------------------------------------------------------\n"
"Texture2D txDiffuse : register( t0 );                                                   \n"
"SamplerState samplerState									                             \n"
"{															                             \n"
"    Filter = MIN_MAG_MIP_LINEAR;					     	                             \n"
"    AddressU = Clamp;								     	                             \n"
"    AddressV = Clamp;								     	                             \n"
"};															                             \n"
"//--------------------------------------------------------------------------------------\n"
"cbuffer cbNeverChanges : register( b0 )                                                 \n"
"{                                                                                       \n"
"    matrix View;                                                                        \n"
"};                                                                                      \n"
"//--------------------------------------------------------------------------------------\n"
"struct VS_INPUT                                                                         \n"
"{                                                                                       \n"
"    float4 Pos : POSITION;                                                              \n"
"    float2 Tex : TEXCOORD;                                                              \n"
"};                                                                                      \n"
"//--------------------------------------------------------------------------------------\n"
"struct PS_INPUT                                                                         \n"
"{                                                                                       \n"
"    float4 Pos : SV_POSITION;                                                           \n"
"    float2 Tex : TEXCOORD;                                                              \n"
"};                                                                                      \n"
"//--------------------------------------------------------------------------------------\n"
"// Vertex Shader                                                                        \n"
"//--------------------------------------------------------------------------------------\n"
"PS_INPUT VS( VS_INPUT input )                                                           \n"
"{                                                                                       \n"
"    PS_INPUT output = (PS_INPUT)0;                                                      \n"
"    output.Pos = mul( input.Pos, View );                                                \n"
"//    output.Pos = mul( output.Pos, Projection );                                       \n"
"    output.Tex = input.Tex;                                                             \n"
"                                                                                        \n"
"    return output;                                                                      \n"
"}                                                                                       \n"
"//--------------------------------------------------------------------------------------\n"
"// Pixel Shader passing texture color                                                   \n"
"//--------------------------------------------------------------------------------------\n"
"float4 PS( PS_INPUT input) : SV_Target                                                  \n"
"{                                                                                       \n"
"   float4 color = txDiffuse.Sample( samplerState, input.Tex );                          \n"
"   color.w = 1.0f;                                                                      \n"
"   return color;                                                                        \n"
"}                                                                                       \n"
;
}