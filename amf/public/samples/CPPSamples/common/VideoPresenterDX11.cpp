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
#include "VideoPresenterDX11.h"
#include "public/common/TraceAdapter.h"

// Auto generated to build_dir from QuadDX11_vs.h and QuadDX11_ps.h
// and added to the include directory during compile
#include "QuadDX11_vs.h"
#include "QuadDX11_ps.h"

using namespace amf;

#define  AMF_FACILITY  L"VideoPresenterDX11"

VideoPresenterDX11::VideoPresenterDX11(amf_handle hwnd, AMFContext* pContext, amf_handle hDisplay) :
    VideoPresenter(hwnd, pContext, hDisplay)
{
    SetInputFormat(AMF_SURFACE_RGBA);
}

VideoPresenterDX11::~VideoPresenterDX11()
{
    Terminate();
}

AMF_RESULT VideoPresenterDX11::RenderToSwapChain(amf::AMFSurface* pSurface)
{
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"RenderToSwapChain() - m_pSwapChain is not initialized");

    if (GetInputFormat() == AMF_SURFACE_NV12)
    {
        SwapChainDXGIDecode* pSwapChain = (SwapChainDXGIDecode*)m_pSwapChain.get();

        AMF_RESULT res = pSwapChain->Submit(pSurface);
        AMF_RETURN_IF_FAILED(res, L"RenderToSwapChain() - SwapChainDecode Submit() failed");
        return AMF_OK;
    }

    return VideoPresenter::RenderToSwapChain(pSurface);
}

AMF_RESULT VideoPresenterDX11::RenderSurface(amf::AMFSurface* pSurface, const RenderTargetBase* pRenderTarget, RenderViewSizeInfo& renderView)
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"RenderSurface() - m_pDevice is not initialized");
    AMF_RETURN_IF_FALSE(pSurface != nullptr, AMF_INVALID_ARG, L"RenderSurface() - pSrcSurface is NULL");

    RenderTarget* pTarget = (RenderTarget*)pRenderTarget;
    AMF_RETURN_IF_FALSE(pTarget != nullptr, AMF_INVALID_ARG, L"RenderSurface() - pTarget is NULL");

    ID3D11Texture2D* pSrcDXSurface = GetPackedSurfaceDX11(pSurface);
    AMF_RETURN_IF_FALSE(pSrcDXSurface != nullptr, AMF_INVALID_ARG, L"RenderSurface() - Src surface native is NULL");

    D3D11_TEXTURE2D_DESC srcDesc = {};
    pSrcDXSurface->GetDesc(&srcDesc);

    // If the surface is not bound to a shader, we have to copy it to another texture and bind that one
    if ((srcDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) != D3D11_BIND_SHADER_RESOURCE || (srcDesc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE))
    {
        AMF_RESULT res = CopySurface(pSurface);
        AMF_RETURN_IF_FAILED(res, L"RenderSurface() - CopySurface() failed");

        // When we copy the surface, we only copy the area
        // given by pSrcRect therefore the copied source size
        // becomes the same as the pSrcRect size
        renderView.srcSize.width = renderView.srcRect.Width();
        renderView.srcSize.height = renderView.srcRect.Height();
    }

    AMF_RESULT res = UpdateStates(pSurface, pTarget, renderView);
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - UpdateStates() failed");

    res = DrawBackground(pTarget); // Clears the background
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - DrawBackground() failed");

    res = SetStates();
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - SetStates() failed");

    ID3D11Texture2D* pSurfaceL = m_pCopyTexture_L != nullptr ? m_pCopyTexture_L.p : pSrcDXSurface;
    ID3D11Texture2D* pSurfaceR = m_pCopyTexture_R != nullptr ? m_pCopyTexture_R.p : pSrcDXSurface;

    // render left
    if (LeftFrameExists(pSurface->GetFrameType()))
    {
        res = DrawFrame(pSurfaceL, pTarget, true);
        AMF_RETURN_IF_FAILED(res, L"RenderSurface() - DrawFrame(left) failed");
    }

    // render right
    if (m_pSwapChain->StereoEnabled() && RightFrameExists(pSurface->GetFrameType()))
    {
        res = DrawFrame(pSurfaceR, pTarget, false);
        AMF_RETURN_IF_FAILED(res, L"RenderSurface() - DrawFrame(right) failed");
    }

    res = DrawOverlay(pSurface, pTarget);
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - DrawOverlay() failed");

    if (m_enablePIP)
    {
        res = SetPipStates();
        AMF_RETURN_IF_FAILED(res, L"RenderSurface() - SetPipStates() failed");

        res = DrawFrame(pSurfaceL, pTarget, true);
        AMF_RETURN_IF_FAILED(res, L"RenderSurface() - DrawFrame() PIP failed");
    }

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::UpdateStates(AMFSurface* /*pSurface*/, const RenderTarget* /*pRenderTarget*/, RenderViewSizeInfo& renderView)
{
    AMF_RESULT res = ResizeRenderView(renderView);
    AMF_RETURN_IF_FAILED(res, L"UpdateStates() - ResizeRenderView() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::CopySurface(AMFSurface* pSurface)
{
    AMF_RETURN_IF_FALSE(pSurface != nullptr, AMF_INVALID_ARG, L"CopySurface() - pSurface is NULL");
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"CopySurface() - m_pDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"CopySurface() - m_pSwapChain is not initialized");

    ID3D11Texture2D* pDXSurface = GetPackedSurfaceDX11(pSurface);
    AMF_RETURN_IF_FALSE(pDXSurface != nullptr, AMF_INVALID_ARG, L"RenderSurface() - surface native is NULL");

    AMFPlane* pPlane = pSurface->GetPlane(AMF_PLANE_PACKED);

    D3D11_TEXTURE2D_DESC srcDesc;
    pDXSurface->GetDesc(&srcDesc);

    // Create textures if not available
    if(m_pCopyTexture_L == nullptr)
    {
        D3D11_TEXTURE2D_DESC Desc={0};
        Desc.ArraySize = 1;
        Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Desc.Usage = D3D11_USAGE_DEFAULT;

        // Default typeless formats to unsigned normalized
        switch(srcDesc.Format)
        {
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            break;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            break;
        default:
            Desc.Format = srcDesc.Format;
            break;
        }

        Desc.Width = pPlane->GetWidth();
        Desc.Height = pPlane->GetHeight();
        Desc.MipLevels = 1;
        Desc.SampleDesc.Count = 1;
        Desc.CPUAccessFlags = 0;

        HRESULT hr = m_pDevice->CreateTexture2D(&Desc, nullptr, &m_pCopyTexture_L);    
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CopySurface() - Failed to create%scopy texture", (m_pSwapChain->StereoEnabled() == true ? " the left " : " "));

        if(m_pSwapChain->StereoEnabled())
        {
            hr = m_pDevice->CreateTexture2D(&Desc, nullptr, &m_pCopyTexture_R);
            ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CopySurface() - Failed to create the right copy texture");
        }
    }

    D3D11_BOX box;
    box.left = pPlane->GetOffsetX();
    box.top = pPlane->GetOffsetY();
    box.right = box.left + pPlane->GetWidth();
    box.bottom = box.top + pPlane->GetHeight();
    box.front=0;
    box.back=1;

    CComPtr<ID3D11DeviceContext> spContext;
    m_pDevice->GetImmediateContext(&spContext);

    switch(pSurface->GetFrameType())
    {
    case amf::AMF_FRAME_STEREO_RIGHT:
        spContext->CopySubresourceRegion(m_pCopyTexture_R, 0, 0, 0, 0, pDXSurface, 0, &box); // we expect that texture comes as a single slice
        break;                                                                        
    case amf::AMF_FRAME_STEREO_BOTH:                                                  
        spContext->CopySubresourceRegion(m_pCopyTexture_L, 0, 0, 0, 0, pDXSurface, 0, &box); // we expect that texture comes as array of two silces
        spContext->CopySubresourceRegion(m_pCopyTexture_R, 0, 0, 0, 0, pDXSurface, 1, &box);
        break;
    default:
        if(srcDesc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE)
        {
            spContext->CopySubresourceRegion(m_pCopyTexture_L, 0, 0, 0, 0, pDXSurface, m_subresourceIndex, &box); // TODO get subresource property
        }                                                                  
        else                                                               
        {                                                                  
            spContext->CopySubresourceRegion(m_pCopyTexture_L, 0, 0, 0, 0, pDXSurface,0,&box);
        }
        break;
    }

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::DrawBackground(const RenderTarget* pRenderTarget)
{
    CComPtr<ID3D11DeviceContext> spContext;
    m_pDevice->GetImmediateContext(&spContext);

    spContext->ClearRenderTargetView(pRenderTarget->pRTV_L, ClearColor);

    if (pRenderTarget->pRTV_R != nullptr)
    {
        spContext->ClearRenderTargetView(pRenderTarget->pRTV_R, ClearColor);
    }

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::SetStates()
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - m_pDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pQuadVertexShader != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - m_pQuadVertexShader is not initialized");
    AMF_RETURN_IF_FALSE(m_pQuadPixelShader != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - m_pQuadPixelShader is not initialized");
    AMF_RETURN_IF_FALSE(m_pViewProjectionBuffer != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - m_pViewProjectionBuffer is not initialized");
    AMF_RETURN_IF_FALSE(m_pSamplerMap[m_interpolation] != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - Sampler is not initialized");
    AMF_RETURN_IF_FALSE(m_pDepthStencilState != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - m_pDepthStencilState is not initialized");
    AMF_RETURN_IF_FALSE(m_pRasterizerState != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - m_pRasterizerState is not initialized");
    AMF_RETURN_IF_FALSE(m_pQuadVertexLayout != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - m_pQuadVertexLayout is not initialized");
    AMF_RETURN_IF_FALSE(m_pBlendState != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - m_pBlendState is not initialized");
    AMF_RETURN_IF_FALSE(m_pVertexBuffer != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - m_pVertexBuffer is not initialized");

    // setup all states
    CComPtr<ID3D11DeviceContext> spContext;
    m_pDevice->GetImmediateContext(&spContext);

    // setup shaders
    spContext->VSSetShader(m_pQuadVertexShader, nullptr, 0);                            // Vertex Shader
    spContext->VSSetConstantBuffers(0, 1, &m_pViewProjectionBuffer.p);              // Vertex Shader view projection

    spContext->PSSetShader(m_pQuadPixelShader, nullptr, 0);                             // Pixel shader
    spContext->PSSetSamplers(0, 1, &m_pSamplerMap[m_interpolation].p);              // Pixel shader sampler state

    spContext->OMSetDepthStencilState(m_pDepthStencilState, 1);                     // Depth stencil
    spContext->RSSetState(m_pRasterizerState);                                      // Rasterizer state
    spContext->IASetInputLayout(m_pQuadVertexLayout);                                   // Vertex Layout

    // Render vertices as a bunch of connected triangles (sharing 2 vertices)
    // 4 vertices in vertex buffer create two connected triangles for the rectangular video frame
    spContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    const AMFSize size = GetSwapchainSize();
    D3D11_VIEWPORT currentViewport = {};
    currentViewport.TopLeftX = 0;
    currentViewport.TopLeftY = 0;
    currentViewport.Width = FLOAT(size.width);
    currentViewport.Height = FLOAT(size.height);
    currentViewport.MinDepth = 0.0f;
    currentViewport.MaxDepth = 1.0f;
    spContext->RSSetViewports(1, &currentViewport); // Viewport
    constexpr amf_float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // No blending
    spContext->OMSetBlendState(m_pBlendState, blendFactor, 0xFFFFFFFF);             // Blend state

    // Set vertex buffer
    constexpr UINT stride = sizeof(Vertex);
    constexpr UINT offset = 0;
    spContext->IASetVertexBuffers(0, 1, &m_pVertexBuffer.p, &stride, &offset);

    // Index buffer not required

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::SetPipStates()
{
    AMF_RETURN_IF_FALSE(m_pPIPViewProjectionBuffer != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - m_pViewProjectionBuffer is not initialized");
    AMF_RETURN_IF_FALSE(m_pSamplerMap[PIP_INTERPOLATION] != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - PIP sampler is not initialized");

    AMF_RESULT res = SetStates();
    AMF_RETURN_IF_FAILED(res, L"SetPipSates() - SetStates() failed");

    CComPtr<ID3D11DeviceContext> spContext;
    m_pDevice->GetImmediateContext(&spContext);
    spContext->VSSetConstantBuffers(0, 1, &m_pPIPViewProjectionBuffer.p);
    spContext->PSSetSamplers(0, 1, &m_pSamplerMap[PIP_INTERPOLATION].p);

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::DrawFrame(ID3D11Texture2D* pSrcSurface, const RenderTarget* pRenderTarget, amf_bool left)
{
    AMF_RETURN_IF_FALSE(pSrcSurface != nullptr, AMF_INVALID_ARG, L"DrawFrame() - pSrcSurface is NULL");
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"DrawFrame() - m_pDevice is not initialized");

    // Renders to the viewport. The graphics pipeline
    // (shaders, vertex buffers, etc...) should be setup by now

    D3D11_TEXTURE2D_DESC srcDesc;
    pSrcSurface->GetDesc(&srcDesc);

    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc;
    ZeroMemory(&viewDesc, sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC));
    D3D11_SHADER_RESOURCE_VIEW_DESC* pViewDesc = nullptr;

    if(srcDesc.ArraySize != 1)
    {
        viewDesc.Format = srcDesc.Format;
        // For stereo support
        viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; // we expect that texture comes as array of two silces
        viewDesc.Texture2DArray.ArraySize = 1;
        viewDesc.Texture2DArray.FirstArraySlice = left ? 0 : 1; // left or right
        viewDesc.Texture2DArray.MipLevels = 1;
        viewDesc.Texture2DArray.MostDetailedMip = 0;
        pViewDesc = &viewDesc;
    }

    CComPtr<ID3D11ShaderResourceView> pTextureRV;
    HRESULT hr = m_pDevice->CreateShaderResourceView(pSrcSurface, pViewDesc, &pTextureRV);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"DrawFrame() - CreateShaderResourceView() failed");

    CComPtr<ID3D11DeviceContext> spContext;
    m_pDevice->GetImmediateContext(&spContext);

    spContext->PSSetShaderResources(0, 1, &pTextureRV.p);

    CComPtr<ID3D11RenderTargetView> pRenderTargetView = left ? pRenderTarget->pRTV_L : pRenderTarget->pRTV_R;
    spContext->OMSetRenderTargets(1, &pRenderTargetView.p, nullptr);
    spContext->Draw(4, 0);

    CustomDraw();

    // Clear render target and texture set
    ID3D11RenderTargetView *pNULLRT = nullptr;
    spContext->OMSetRenderTargets(1, &pNULLRT, nullptr);

    ID3D11ShaderResourceView* pNULL = nullptr;
    spContext->PSSetShaderResources(0, 1, &pNULL);
    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::Init(amf_int32 width, amf_int32 height, amf::AMFSurface* pSurface)
{
    // pSurface is allowed to be null so don't need to error check

    if (GetInputFormat() != AMF_SURFACE_NV12)
    {
        m_pSwapChain = std::make_unique<SwapChainDX11>(m_pContext);
    }
    else
    {
        m_pSwapChain = std::make_unique<SwapChainDXGIDecode>(m_pContext, GetMemoryType());
    }

    AMF_RESULT res = VideoPresenter::Init(width, height);
    AMF_RETURN_IF_FAILED(res, L"Init() - VideoPresenter::Init() failed");

    // Get DX device
    m_pDevice = static_cast<ID3D11Device*>(m_pContext->GetDX11Device());
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NO_DEVICE, L"Init() - Failed to get D3D device");


    // If not using the DC swap chain, we need to setup
    // shaders and graphics pipeline for rendering
    if (GetInputFormat() != AMF_SURFACE_NV12)
    {
        // Setup swap chain
        m_pSwapChain->Init(m_hwnd, m_hDisplay, pSurface, width, height, GetInputFormat(), false, true);
        AMF_RETURN_IF_FAILED(res, L"Init() - m_pSwapChain->Init() failed");

        // Create vertex + pixel shader
        res = CreateShaders();
        AMF_RETURN_IF_FAILED(res, L"Init() - Failed to compile shaders");

        // Setup pipeline buffers and states
        res = PrepareStates();
        AMF_RETURN_IF_FAILED(res, L"Init() - Failed to prepare graphics pipeline states");
    }
    else
    {
        res = m_pSwapChain->Init(m_hwnd, m_hDisplay, pSurface, width, height, GetInputFormat());
        AMF_RETURN_IF_FAILED(res, L"Init() - m_swapChainDecode.Init() failed");
    }

    m_firstFrame = true;

    ResizeIfNeeded();

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::Terminate()
{
    if (m_pSwapChain != nullptr)
    {
        m_pSwapChain->Terminate();
    }

    m_pVertexBuffer = nullptr;
    m_pViewProjectionBuffer = nullptr;
    m_pPIPViewProjectionBuffer = nullptr;
    m_pQuadVertexShader = nullptr;
    m_pQuadPixelShader = nullptr;
    m_pQuadVertexLayout = nullptr;
    m_pSamplerMap.clear();
    m_pDepthStencilState = nullptr;
    m_pRasterizerState = nullptr;
    m_pBlendState = nullptr;

    m_pCopyTexture_L = nullptr;
    m_pCopyTexture_R = nullptr;

    if (m_pDevice != nullptr)
    {
        // ClearState to clean up ReportLiveDeviceObjects dump
        CComPtr<ID3D11DeviceContext> spContext;
        m_pDevice->GetImmediateContext(&spContext);
        if(spContext != nullptr)
        {
            spContext->ClearState();
            spContext->Flush();
        }
    }
    m_pDevice = nullptr;

    return VideoPresenter::Terminate();
}

AMF_RESULT VideoPresenterDX11::CreateShaders()
{
    return CreateShaders(&m_pQuadVertexShader, &m_pQuadVertexLayout, &m_pQuadPixelShader);
}

AMF_RESULT VideoPresenterDX11::CreateShaders(ID3D11VertexShader** ppVertexShader, ID3D11InputLayout** ppInputLayout, ID3D11PixelShader** ppPixelShader)
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateShaders() - m_pDevice is not initialized");
    AMF_RETURN_IF_FALSE(ppVertexShader != nullptr, AMF_INVALID_ARG, L"CreateShaders() - ppVertexShader is NULL");
    AMF_RETURN_IF_FALSE(ppInputLayout != nullptr, AMF_INVALID_ARG, L"CreateShaders() - ppInputLayout is NULL");
    AMF_RETURN_IF_FALSE(ppPixelShader != nullptr, AMF_INVALID_ARG, L"CreateShaders() - ppPixelShader is NULL");
    
    HRESULT hr = m_pDevice->CreateVertexShader(QuadDX11_vs, sizeof(QuadDX11_vs), nullptr, ppVertexShader);
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"CreateShaders() - CreateVertexShader() failed");

    // Define the input layout
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    UINT numElements = ARRAYSIZE(layout);

    // Create the input layout
    hr = m_pDevice->CreateInputLayout(layout, numElements, QuadDX11_vs, sizeof(QuadDX11_vs), ppInputLayout);
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"CompileShaders() - CreateInputLayout() failed");

    hr = m_pDevice->CreatePixelShader(QuadDX11_ps, sizeof(QuadDX11_ps), nullptr, ppPixelShader);
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"CreateShaders() - CreatePixelShader() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::CreateBuffer(D3D11_BIND_FLAG bind, D3D11_USAGE usage, const void* pData, amf_size size, ID3D11Buffer** ppBuffer)
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateBuffer() - m_pDevice is not initialized");
    AMF_RETURN_IF_FALSE(ppBuffer != nullptr, AMF_INVALID_ARG, L"CreateBuffer() - ppBuffer is NULL");
    AMF_RETURN_IF_FALSE(*ppBuffer == nullptr, AMF_INVALID_ARG, L"CreateBuffer() - ppBuffer is already initialized");

    D3D11_BUFFER_DESC desc = {};
    desc.Usage = usage;
    desc.ByteWidth = (UINT)size;
    desc.BindFlags = bind;

    switch (usage)
    {
    case D3D11_USAGE_DYNAMIC:
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        break;
    case D3D11_USAGE_STAGING:
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ;
        break;

    case D3D11_USAGE_DEFAULT:
    case D3D11_USAGE_IMMUTABLE:
    default:
        desc.CPUAccessFlags = 0;
        break;
    }

    D3D11_SUBRESOURCE_DATA initData = { 0 };
    initData.pSysMem = pData;

    HRESULT hr = m_pDevice->CreateBuffer(&desc, pData != nullptr ? &initData : nullptr, ppBuffer);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateBuffer() - CreateBuffer() failed to create vertex buffer");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::CreateConstantBuffer(D3D11_USAGE usage, const void* pData, amf_size size, ID3D11Buffer** ppBuffer)
{
    return CreateBuffer(D3D11_BIND_CONSTANT_BUFFER, usage, pData, size, ppBuffer);
}

AMF_RESULT VideoPresenterDX11::CreateVertexBuffer(D3D11_USAGE usage, const void* pData, amf_size size, ID3D11Buffer** ppBuffer)
{
    return CreateBuffer(D3D11_BIND_VERTEX_BUFFER, usage, pData, size, ppBuffer);
}

AMF_RESULT VideoPresenterDX11::UpdateBuffer(ID3D11Buffer* pBuffer, const void* pData, amf_size size, amf_size offset)
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_INVALID_ARG, L"UpdateBuffer() - m_pDevice is NULL");
    AMF_RETURN_IF_FALSE(pBuffer != nullptr, AMF_INVALID_ARG, L"UpdateBuffer() - pBuffer is NULL");
    AMF_RETURN_IF_FALSE(pData != nullptr, AMF_INVALID_ARG, L"UpdateBuffer() - pData is NULL");
    AMF_RETURN_IF_FALSE(size > 0, AMF_INVALID_ARG, L"UpdateBuffer() - size must be greater than 0");

    D3D11_BUFFER_DESC desc;
    pBuffer->GetDesc(&desc);

    AMF_RETURN_IF_FALSE(size + offset <= desc.ByteWidth, AMF_OUT_OF_RANGE, 
        L"UpdateBuffer() - Invalid range, size (%zu) + offset (%zu) must be <= %u", size, offset, desc.ByteWidth);

    AMF_RETURN_IF_FALSE(desc.Usage != D3D11_USAGE_IMMUTABLE, AMF_INVALID_ARG, L"UpdateBuffer() - Buffer is immutable");

    CComPtr<ID3D11DeviceContext> spContext;
    m_pDevice->GetImmediateContext(&spContext);

    // If buffer was created with USAGE_DYNAMIC and we have CPU access,
    // its better to use map/unmap. With buffers that don't have CPU access,
    // we need to use UpdateSubresource instead
    if ((desc.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE) == D3D11_CPU_ACCESS_WRITE)
    {
        D3D11_MAP mapType = D3D11_MAP_WRITE;
        switch (desc.Usage)
        {
        case D3D11_USAGE_DYNAMIC:
            mapType = D3D11_MAP_WRITE_DISCARD;
            break;
        default:
            break;
        }

        D3D11_MAPPED_SUBRESOURCE mappedBuffer;
        HRESULT hr = spContext->Map(pBuffer, 0, mapType, 0, &mappedBuffer);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"UpdateBuffer() - Map() failed");

        memcpy((amf_uint8*)mappedBuffer.pData + offset, pData, size);

        spContext->Unmap(pBuffer, 0);
    }
    else
    {
        D3D11_BOX box = {};
        box.left = (UINT)offset;
        box.right = UINT(size + offset);
        // Even though we are copying 1D data, bottom and back must be 1 otherwise nothing will be copied
        box.top = 0;
        box.bottom = 1;
        box.front = 0;
        box.back = 1;

        spContext->UpdateSubresource(pBuffer, 0, nullptr, pData, 0, 0);
    }
 
    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::PrepareStates()
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"PrepareStates() - m_pDevice is not initialized");

    AMF_RESULT res = CreateVertexBuffer(D3D11_USAGE_IMMUTABLE, QUAD_VERTICES_NORM, sizeof(QUAD_VERTICES_NORM), &m_pVertexBuffer);
    AMF_RETURN_IF_FAILED(res, L"PrepareStates() - CreateVertexBuffer() failed");

    // Create constant buffer for vertex shader
    // This buffer stores a transformation matrix
    // for converting the vertex positions
    // which is updated when the window is resized
    res = CreateConstantBuffer(D3D11_USAGE_DYNAMIC, &m_viewProjection, sizeof(ViewProjection), &m_pViewProjectionBuffer);
    AMF_RETURN_IF_FAILED(res, L"PrepareStates() - CreateConstantBuffer() failed to create view projection buffer");

    res = CreateConstantBuffer(D3D11_USAGE_DYNAMIC, &m_pipViewProjection, sizeof(ViewProjection), &m_pPIPViewProjectionBuffer);
    AMF_RETURN_IF_FAILED(res, L"PrepareStates() - CreateConstantBuffer() failed to create PIP view projection buffer");

    // Create the sampler state
    D3D11_SAMPLER_DESC sampDesc;
    ZeroMemory( &sampDesc, sizeof(sampDesc) );
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    HRESULT hr = m_pDevice->CreateSamplerState( &sampDesc, &m_pSamplerMap[InterpolationLinear] );
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"PrepareStates() - Failed to create linear sampler state");

    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    hr = m_pDevice->CreateSamplerState(&sampDesc, &m_pSamplerMap[InterpolationPoint]);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"PrepareStates() - Failed to create point sampler state");

    // Create depth stencil
    // set Z-buffer off
    D3D11_DEPTH_STENCIL_DESC depthDisabledStencilDesc={0};
    depthDisabledStencilDesc.DepthEnable = FALSE;
    depthDisabledStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthDisabledStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;
    depthDisabledStencilDesc.StencilEnable = TRUE;
    depthDisabledStencilDesc.StencilReadMask = 0xFF;
    depthDisabledStencilDesc.StencilWriteMask = 0xFF;
    depthDisabledStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    depthDisabledStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
    depthDisabledStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    depthDisabledStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    depthDisabledStencilDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    depthDisabledStencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
    depthDisabledStencilDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    depthDisabledStencilDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

    hr = m_pDevice->CreateDepthStencilState(&depthDisabledStencilDesc, &m_pDepthStencilState);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"PrepareStates() - Failed to create depth stencil state");

    // Create the rasterizer state which will determine how and what polygons will be drawn.
    D3D11_RASTERIZER_DESC rasterDesc;
    memset(&rasterDesc,0,sizeof(rasterDesc));
    rasterDesc.AntialiasedLineEnable = false;
    rasterDesc.CullMode = D3D11_CULL_BACK;
    rasterDesc.DepthBias = 0;
    rasterDesc.DepthBiasClamp = 0.0f;
    rasterDesc.DepthClipEnable = true;
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.FrontCounterClockwise = false;
    rasterDesc.MultisampleEnable = false;
    rasterDesc.ScissorEnable = false;
    rasterDesc.SlopeScaledDepthBias = 0.0f;

    // Create the rasterizer state from the description we just filled out.
    hr = m_pDevice->CreateRasterizerState(&rasterDesc, &m_pRasterizerState);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"PrepareStates() - Failed to create rasterizer state");

    // Create blend state
    D3D11_BLEND_DESC blendDesc;
    ZeroMemory( &blendDesc, sizeof(blendDesc));
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = 0x0F;

    hr = m_pDevice->CreateBlendState( &blendDesc, &m_pBlendState);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"PrepareStates() - Failed to create blend state");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::DropFrame()
{
    if (m_pSwapChain == nullptr || GetInputFormat() == AMF_SURFACE_NV12)
    {
        return AMF_OK;
    }

    return VideoPresenter::DropFrame();
}

AMF_RESULT VideoPresenterDX11::OnRenderViewResize(const RenderViewSizeInfo& newRenderView)
{
    AMF_RESULT res = VideoPresenter::OnRenderViewResize(newRenderView);
    AMF_RETURN_IF_FAILED(res, L"OnRenderViewResize() - VideoPresenter::OnRenderViewResize() failed");

    if (m_pViewProjectionBuffer == nullptr)
    {
        // not using shaders
        return AMF_OK;
    }

    res = UpdateBuffer(m_pViewProjectionBuffer, &m_viewProjection, sizeof(ViewProjection), 0);
    AMF_RETURN_IF_FAILED(res, L"OnRenderViewResize() - UpdateBuffe() failed to update view projection buffer");

    res = UpdateBuffer(m_pPIPViewProjectionBuffer, &m_pipViewProjection, sizeof(ViewProjection), 0);
    AMF_RETURN_IF_FAILED(res, L"OnRenderViewResize() - UpdateBuffe() failed to update pip view projection buffer");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::SetInputFormat(amf::AMF_SURFACE_FORMAT format)
{
    switch (format)
    {
    case AMF_SURFACE_UNKNOWN:
    case AMF_SURFACE_RGBA:
    case AMF_SURFACE_BGRA:
    case AMF_SURFACE_RGBA_F16:
    case AMF_SURFACE_R10G10B10A2:
    case AMF_SURFACE_NV12:
        break;
    default:
        return AMF_NOT_SUPPORTED;
    }

    m_inputFormat = format;
    return AMF_OK;
}
