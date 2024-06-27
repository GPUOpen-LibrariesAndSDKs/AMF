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
#include "VideoPresenterDX9.h"
#include <public/common/TraceAdapter.h>

// Auto generated to build_dir from QuadDX9_vs.h and QuadDX9_ps.h
// and added to the include directory during compile
#include "QuadDX9_vs.h"
#include "QuadDX9_ps.h"

using namespace amf;

#define AMF_FACILITY L"VideoPresenterDX9"

// Register numbers
#define TEXTURE_REG                    0 // (QuadDX9_ps.hlsl - Texture2D txDiffuse : register(t0);)
#define VERTEX_TRANSFORM_MATRIX_REG    0 // (QuadDX9_vs.hlsl - float4x4 vertexTransform : register (c0);)
#define TEXTURE_TRANSFORM_MATRIX_REG   4 // (QuadDX9_vs.hlsl - float4x4 textureTransform : register (c4);)

VideoPresenterDX9::VideoPresenterDX9(amf_handle hwnd, AMFContext* pContext, amf_handle hDisplay) :
    VideoPresenter(hwnd, pContext, hDisplay)
{
    m_pSwapChain = std::make_unique<SwapChainDX9>(pContext);
}

VideoPresenterDX9::~VideoPresenterDX9()
{
    Terminate();
}

AMF_RESULT VideoPresenterDX9::Init(amf_int32 width, amf_int32 height, AMFSurface* pSurface)
{
    AMF_RETURN_IF_FALSE(width > 0 && height > 0, AMF_INVALID_ARG, L"Init() - Invalid width/height: %ux%u", width, height);
    AMF_RETURN_IF_FALSE(m_hwnd != nullptr, AMF_NOT_INITIALIZED, L"Init() - m_hwnd is not initialized");
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_UNEXPECTED, L"Init() - m_pSwapChain is NULL");

    AMF_RESULT res = VideoPresenter::Init(width, height);
    AMF_RETURN_IF_FAILED(res, L"Init() - VideoPresenter::Init() failed");

    m_pDevice = static_cast<IDirect3DDevice9Ex*>(m_pContext->GetDX9Device());
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NO_DEVICE, L"Init() - Failed to get DX9 device");

    res = m_pSwapChain->Init(m_hwnd, m_hDisplay, pSurface, width, height, GetInputFormat());
    AMF_RETURN_IF_FAILED(res, L"Init() - m_pSwapChain->Init() failed");

    res = CreateShaders();
    AMF_RETURN_IF_FAILED(res, L"Init() - CreateShaders() failed");

    res = PrepareStates();
    AMF_RETURN_IF_FAILED(res, L"Init() - PrepareStates() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX9::Terminate()
{    
    m_pDevice = nullptr;

    m_pVertexShader = nullptr;
    m_pPixelShader = nullptr;

    m_pVertexBuffer = nullptr;
    m_pVertexLayout = nullptr;

    m_pDepthStencilState = nullptr;
    m_pRasterizerState = nullptr;
    m_pBlendState = nullptr;
    m_pDefaultState = nullptr;
    m_pSamplerStateMap.clear();

    return VideoPresenter::Terminate();
}

AMF_RESULT VideoPresenterDX9::CreateShaders()
{
    return CreateShaders(&m_pVertexShader, &m_pPixelShader);
}

AMF_RESULT VideoPresenterDX9::CreateShaders(IDirect3DVertexShader9** ppVertexShader, IDirect3DPixelShader9** ppPixelShader)
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateShaders() - m_pDevice is not initialized");
    AMF_RETURN_IF_FALSE(ppVertexShader != nullptr, AMF_INVALID_ARG, L"CreateShaders() - pShaderStr is NULL");
    AMF_RETURN_IF_FALSE(ppPixelShader != nullptr, AMF_INVALID_ARG, L"CreateShaders() - pEntryPoint is NULL");

    HRESULT hr = m_pDevice->CreateVertexShader((DWORD*)QuadDX9_vs, ppVertexShader);
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"CreateShaders() - CreateVertexShader() failed to create vertex shader");

    hr = m_pDevice->CreatePixelShader((DWORD*)QuadDX9_ps, ppPixelShader);
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"CreateShaders() - CreatePixelShader() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX9::PrepareStates()
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_INVALID_ARG, L"PrepareStates() - m_pDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pVertexBuffer == nullptr, AMF_ALREADY_INITIALIZED, L"PrepareStates() - m_pVertexBuffer is already initialized");
    AMF_RETURN_IF_FALSE(m_pDepthStencilState == nullptr, AMF_ALREADY_INITIALIZED, L"PrepareStates() - m_pDepthStencilState is already initialized");
    AMF_RETURN_IF_FALSE(m_pRasterizerState == nullptr, AMF_ALREADY_INITIALIZED, L"PrepareStates() - m_pRasterizerState is already initialized");
    AMF_RETURN_IF_FALSE(m_pBlendState == nullptr, AMF_ALREADY_INITIALIZED, L"PrepareStates() - m_pBlendState is already initialized");

    // Create vertex buffer (quad will be set later)
    constexpr UINT size = sizeof(QUAD_VERTICES_NORM);
    constexpr DWORD usage = D3DUSAGE_WRITEONLY;
    constexpr D3DPOOL pool = D3DPOOL_DEFAULT;
    HRESULT hr = m_pDevice->CreateVertexBuffer(size, usage, 0, pool, &m_pVertexBuffer, nullptr);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"PrepareStates() - Failed to create vertex buffer");

    D3DVERTEXELEMENT9 layout[] =
    {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        D3DDECL_END()
    };

    // Create the vertex input layout
    hr = m_pDevice->CreateVertexDeclaration(layout, &m_pVertexLayout);
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"PrepareStates() - CreateVertexDeclaration() failed");

    // Update vertex buffer
    // After updating it once, we don't need to touch the vertex since we use
    // projection matrices for changing the vertex positions dynamically
    AMF_RESULT res = UpdateVertexBuffer(m_pVertexBuffer, (void*)QUAD_VERTICES_NORM, sizeof(QUAD_VERTICES_NORM));
    AMF_RETURN_IF_FAILED(res, L"UpdateVertices() - UpdateVertexBuffer() failed");

    // Capture the default state
    m_pDevice->CreateStateBlock(D3DSBT_ALL, &m_pDefaultState);
    m_pDefaultState->Capture();

    // We use a static sampler defined in the shader so no need to create one here

#define SetRenderStateCheck(state, value) \
{\
    HRESULT err = m_pDevice->SetRenderState(state, value);\
    ASSERT_RETURN_IF_HR_FAILED(err, AMF_DIRECTX_FAILED, L"PrepareStates() - Failed to set render state " L#state " to %ul", value);\
}

    // Create depth stencil state
    // set Z-buffer off
    hr = m_pDevice->BeginStateBlock();
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"PrepareStates() - BeginStateBlock() failed to record depth stencil state");

    SetRenderStateCheck(D3DRS_ZENABLE, D3DZB_FALSE);
    SetRenderStateCheck(D3DRS_ZWRITEENABLE, TRUE);
    SetRenderStateCheck(D3DRS_ZFUNC, D3DCMP_LESS);

    SetRenderStateCheck(D3DRS_STENCILENABLE, TRUE);
    SetRenderStateCheck(D3DRS_STENCILMASK, 0xFF);
    SetRenderStateCheck(D3DRS_STENCILWRITEMASK, 0xFF);

    SetRenderStateCheck(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
    SetRenderStateCheck(D3DRS_STENCILZFAIL, D3DSTENCILOP_INCRSAT);
    SetRenderStateCheck(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
    SetRenderStateCheck(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);

    SetRenderStateCheck(D3DRS_CCW_STENCILFAIL, D3DSTENCILOP_KEEP);
    SetRenderStateCheck(D3DRS_CCW_STENCILZFAIL, D3DSTENCILOP_INCRSAT);
    SetRenderStateCheck(D3DRS_CCW_STENCILPASS, D3DSTENCILOP_KEEP);
    SetRenderStateCheck(D3DRS_CCW_STENCILFUNC, D3DCMP_ALWAYS);

    hr = m_pDevice->EndStateBlock(&m_pDepthStencilState);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"PrepareStates() - EndStateBlock() failed to record depth stencil state");

    // Create the rasterizer state which will determine how and what polygons will be drawn.
    hr = m_pDevice->BeginStateBlock();
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"PrepareStates() - BeginStateBlock() failed to record rasterizer state");

    SetRenderStateCheck(D3DRS_ANTIALIASEDLINEENABLE, FALSE);
    SetRenderStateCheck(D3DRS_CULLMODE, D3DCULL_CCW);
    SetRenderStateCheck(D3DRS_DEPTHBIAS, 0);
    SetRenderStateCheck(D3DRS_CLIPPING, TRUE);
    SetRenderStateCheck(D3DRS_FILLMODE, D3DFILL_SOLID);
    SetRenderStateCheck(D3DRS_MULTISAMPLEANTIALIAS, FALSE);
    SetRenderStateCheck(D3DRS_SLOPESCALEDEPTHBIAS, 0);

    hr = m_pDevice->EndStateBlock(&m_pRasterizerState);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"PrepareStates() - EndStateBlock() failed to record rasterizer state");

    // Create blend state
    hr = m_pDevice->BeginStateBlock();
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"PrepareStates() - BeginStateBlock() failed to record blend state");

    SetRenderStateCheck(D3DRS_ALPHABLENDENABLE, TRUE);
    SetRenderStateCheck(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    SetRenderStateCheck(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    SetRenderStateCheck(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    SetRenderStateCheck(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);
    SetRenderStateCheck(D3DRS_SRCBLENDALPHA, D3DBLEND_ZERO);
    SetRenderStateCheck(D3DRS_DESTBLENDALPHA, D3DBLEND_ZERO);
    SetRenderStateCheck(D3DRS_BLENDFACTOR, D3DCOLOR_ARGB(0,0,0,0));
    SetRenderStateCheck(D3DRS_MULTISAMPLEMASK, 0xFFFFFFFF);

    hr = m_pDevice->EndStateBlock(&m_pBlendState);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"PrepareStates() - EndStateBlock() failed to record blend state");

#undef SetRenderStateCheck

    hr = m_pDevice->BeginStateBlock();

    hr = m_pDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetSamplerState() failed to set D3DSAMP_MAGFILTER to D3DTEXF_LINEAR");
    hr = m_pDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetSamplerState() failed to set D3DSAMP_MINFILTER to D3DTEXF_LINEAR");
    hr = m_pDevice->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetSamplerState() failed to set D3DSAMP_MIPFILTER to D3DTEXF_LINEAR");

    hr = m_pDevice->EndStateBlock(&m_pSamplerStateMap[InterpolationLinear]);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"PrepareStates() - EndStateBlock() failed to record linear sampler state");

    hr = m_pDevice->BeginStateBlock();

    hr = m_pDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetSamplerState() failed to set D3DSAMP_MAGFILTER to D3DTEXF_POINT");
    hr = m_pDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetSamplerState() failed to set D3DSAMP_MINFILTER to D3DTEXF_POINT");
    hr = m_pDevice->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetSamplerState() failed to set D3DSAMP_MIPFILTER to D3DTEXF_POINT");

    hr = m_pDevice->EndStateBlock(&m_pSamplerStateMap[InterpolationPoint]);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"PrepareStates() - EndStateBlock() failed to record point sampler state");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX9::RenderSurface(AMFSurface* pSurface, const RenderTargetBase* pRenderTarget, RenderViewSizeInfo& renderView)
{
    AMF_RETURN_IF_FALSE(pSurface != nullptr, AMF_INVALID_ARG, L"RenderSurface() - pSrcSurface is NULL");

    RenderTarget* pTarget = (RenderTarget*)pRenderTarget;
    AMF_RETURN_IF_FALSE(pTarget != nullptr, AMF_INVALID_ARG, L"RenderSurface() - pRenderTarget is NULL");

    AMF_RESULT res = UpdateStates(pSurface, pTarget, renderView);
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - UpdateStates() failed");

    HRESULT hr = m_pDevice->BeginScene();
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"RenderSurface() - BeginScene() failed");

    res = RenderScene(pSurface, pTarget);

    hr = m_pDevice->EndScene();
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"RenderSurface() - EndScene() failed");

    // Need to end scene no matter what so error check after
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - RenderScene() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX9::UpdateStates(AMFSurface* /*pSurface*/, const RenderTarget* /*pRenderTarget*/, RenderViewSizeInfo& renderView)
{
    AMF_RESULT res = ResizeRenderView(renderView);
    AMF_RETURN_IF_FAILED(res, L"UpdateStates() - ResizeRenderView() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX9::RenderScene(AMFSurface* pSrcSurface, const RenderTarget* pRenderTarget)
{
    AMF_RETURN_IF_FALSE(pSrcSurface != nullptr, AMF_INVALID_ARG, L"RenderScene() - pSrcSurface is NULL");
    AMF_RETURN_IF_FALSE(pRenderTarget != nullptr, AMF_INVALID_ARG, L"RenderScene() - pRenderTarget is NULL");

    IDirect3DSurface9* pDXSurface = GetPackedSurfaceDX9(pSrcSurface);
    AMF_RETURN_IF_FALSE(pDXSurface != nullptr, AMF_INVALID_ARG, L"RenderScene() - surface has NULL packed plane native");

    AMF_RESULT res = DrawBackground(pRenderTarget); // Clears the background
    AMF_RETURN_IF_FAILED(res, L"RenderScene() - DrawBackground() failed");

    res = SetStates();
    AMF_RETURN_IF_FAILED(res, L"RenderScene() - SetStates() failed");

    res = DrawFrame(pDXSurface, pRenderTarget);
    AMF_RETURN_IF_FAILED(res, L"RenderScene() - DrawFrame() failed");

    res = DrawOverlay(pSrcSurface, pRenderTarget);
    AMF_RETURN_IF_FAILED(res, L"RenderScene() - DrawOverlay() failed");

    if (m_enablePIP == true)
    {
        res = SetPipStates();
        AMF_RETURN_IF_FAILED(res, L"RenderScene() - SetPipStates() failed");

        res = DrawFrame(pDXSurface, pRenderTarget);
        AMF_RETURN_IF_FAILED(res, L"RenderScene() - DrawFrame() PIP failed");
    }

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX9::DrawBackground(const RenderTarget* pRenderTarget)
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"DrawBackground() - m_pDevice is not initialized");
    AMF_RETURN_IF_FALSE(pRenderTarget != nullptr, AMF_INVALID_ARG, L"DrawBackground() - pRenderTarget is NULL");

    HRESULT hr = m_pDevice->SetRenderTarget(0, pRenderTarget->pBuffer);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"DrawBackground() - SetRenderTarget() failed");

    hr = m_pDevice->Clear(0, nullptr, D3DCLEAR_TARGET, D3DClearColor, 0, 0);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"DrawBackground() - Clear() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX9::SetStates()
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - m_pDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pVertexShader != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - m_pVertexShader is not initialized");
    AMF_RETURN_IF_FALSE(m_pPixelShader != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - m_pPixelShader is not initialized");
    AMF_RETURN_IF_FALSE(m_pDepthStencilState != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - m_pDepthStencilState is not initialized");
    AMF_RETURN_IF_FALSE(m_pRasterizerState != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - m_pRasterizerState is not initialized");
    AMF_RETURN_IF_FALSE(m_pBlendState != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - m_pBlendState is not initialized");
    AMF_RETURN_IF_FALSE(m_pSamplerStateMap[m_interpolation] != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - m_pSamplerStateMap[m_interpolation] is not initialized");
    AMF_RETURN_IF_FALSE(m_pVertexBuffer != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - m_pVertexBuffer is not initialized");
    AMF_RETURN_IF_FALSE(m_pVertexLayout != nullptr, AMF_NOT_INITIALIZED, L"SetStates() - m_pVertexLayout is not initialized");

    // Reset all states
    HRESULT hr = m_pDefaultState->Apply();
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetStates() - m_pDefaultState->Apply() failed");

    // setup shaders
    hr = m_pDevice->SetVertexShader(m_pVertexShader);                               // Vertex Shader
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetStates() - SetVertexShader() failed");

    hr = m_pDevice->SetPixelShader(m_pPixelShader);                                 // Pixel shader
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetStates() - SetPixelShader() failed");

    // Render states
    hr = m_pDepthStencilState->Apply();                                             // Depth stencil
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetStates() - m_pDepthStencilState->Apply() failed");

    hr = m_pRasterizerState->Apply();                                               // Rasterizer state
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetStates() - m_pRasterizerState->Apply() failed");

    hr = m_pBlendState->Apply();                                                    // Blend state
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetStates() - m_pBlendState->Apply() failed");

    hr = m_pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetStates() - SetRenderState() failed to set D3DRS_LIGHTING");

    // Sampler
    hr = m_pSamplerStateMap[m_interpolation]->Apply();
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetStates() - m_pSamplerState->Apply() failed");

    // Set vertex buffer
    hr = m_pDevice->SetStreamSource(0, m_pVertexBuffer, 0, sizeof(Vertex));
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetStates() - SetStreamSource() failed");

    hr = m_pDevice->SetVertexDeclaration(m_pVertexLayout);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetStates() - SetVertexDeclaration() failed");

    hr = m_pDevice->SetVertexShaderConstantF(VERTEX_TRANSFORM_MATRIX_REG, (amf_float*)m_viewProjection.vertexTransform, 4);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetStates() - SetVertexShaderConstantF() failed to set vertex transform matrix");

    hr = m_pDevice->SetVertexShaderConstantF(TEXTURE_TRANSFORM_MATRIX_REG, (amf_float*)m_viewProjection.texTransform, 4);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetStates() - SetVertexShaderConstantF() failed to set texture transform matrix");

    // Viewport
    const AMFSize size = GetSwapchainSize();
    D3DVIEWPORT9 currentViewport = {};
    currentViewport.X = 0;
    currentViewport.Y = 0;
    currentViewport.Width = DWORD(size.width);
    currentViewport.Height = DWORD(size.height);
    currentViewport.MinZ = 0.0f;
    currentViewport.MaxZ = 1.0f;
    hr = m_pDevice->SetViewport(&currentViewport);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetStates() - SetViewport() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX9::SetPipStates()
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"SetPipStates() - m_pDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pSamplerStateMap[InterpolationPoint] != nullptr, AMF_NOT_INITIALIZED, L"SetPipStates() - PIP sampler state is not initialized");

    HRESULT hr = m_pDevice->SetVertexShaderConstantF(VERTEX_TRANSFORM_MATRIX_REG, (amf_float*)m_pipViewProjection.vertexTransform, 4);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetPipStates() - SetVertexShaderConstantF() failed to set vertex transform matrix");

    hr = m_pDevice->SetVertexShaderConstantF(TEXTURE_TRANSFORM_MATRIX_REG, (amf_float*)m_pipViewProjection.texTransform, 4);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetPipStates() - SetVertexShaderConstantF() failed to set texture transform matrix");

    hr = m_pSamplerStateMap[PIP_INTERPOLATION]->Apply();
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetPipStates() - m_pSamplerState->Apply() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX9::DrawFrame(IDirect3DSurface9* pSrcSurface, const RenderTarget* pRenderTarget)
{
    AMF_RETURN_IF_FALSE(pSrcSurface != nullptr, AMF_INVALID_ARG, L"DrawFrame() - pSrcSurface is NULL");
    AMF_RETURN_IF_FALSE(pRenderTarget != nullptr, AMF_INVALID_ARG, L"DrawFrame() - pRenderTarget is NULL");
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"DrawFrame() - m_pDevice is initialized");

    // Renders to the viewport. The graphics pipeline
    // (shaders, vertex buffers, etc...) should be setup by now

    D3DSURFACE_DESC desc;
    pSrcSurface->GetDesc(&desc);

    // Create texture to pass in
    IDirect3DTexture9Ptr pSrcTexture;
    HRESULT hr = m_pDevice->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format, desc.Pool, &pSrcTexture, nullptr);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"DrawFrame() - CreateTexture() failed");

    // Get top most surface of created texture
    IDirect3DSurface9Ptr pSrcTexSurface;
    hr = pSrcTexture->GetSurfaceLevel(0, &pSrcTexSurface);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"DrawFrame() - GetSurfaceLevel() failed");

    // Copy input surface to texture surface
    hr = m_pDevice->StretchRect(pSrcSurface, nullptr, pSrcTexSurface, nullptr, D3DTEXF_NONE);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"DrawFrame() - StretchRect() failed");
    
    AMF_RESULT res = DrawFrame(pSrcTexture, pRenderTarget);
    AMF_RETURN_IF_FAILED(res, L"DrawFrame() - DrawFrame(Texture) failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX9::DrawFrame(IDirect3DTexture9* pSrcTexture, const RenderTarget* pRenderTarget)
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"DrawFrame() - m_pDevice is initialized");
    AMF_RETURN_IF_FALSE(pRenderTarget != nullptr, AMF_INVALID_ARG, L"DrawFrame() - pRenderTarget is NULL");

    HRESULT hr = m_pDevice->SetRenderTarget(0, pRenderTarget->pBuffer);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetStates() - SetRenderTarget() failed");

    if (pSrcTexture != nullptr)
    {
        hr = m_pDevice->SetTexture(TEXTURE_REG, pSrcTexture);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"DrawFrame() - SetTexture() failed");
    }

    // D3DPT_TRIANGLESTRIP: Render vertices as a bunch of connected triangles (sharing 2 vertices)
    // 4 vertices in vertex buffer create two connected triangles for the rectangular video frame
    hr = m_pDevice->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"DrawFrame() - DrawPrimitive() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX9::UpdateVertexBuffer(IDirect3DVertexBuffer9* buffer, void* pData, size_t size)
{
    AMF_RETURN_IF_FALSE(buffer != nullptr, AMF_INVALID_ARG, L"UpdateVertexBuffer() - buffer is NULL");
    AMF_RETURN_IF_FALSE(pData != nullptr, AMF_INVALID_ARG, L"UpdateVertexBuffer() - pData is NULL");
    AMF_RETURN_IF_FALSE(size > 0, AMF_INVALID_ARG, L"UpdateVertexBuffer() - Invalid size == %zu", size);

    void* pDstVertices;
    HRESULT hr = buffer->Lock(0, (UINT)size, (void**)&pDstVertices, 0);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"UpdateVertexBuffer() - Failed to lock vertex buffer");

    memcpy(pDstVertices, pData, size);

    hr = buffer->Unlock();
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"UpdateVertexBuffer() - Failed to unlock vertex buffer");

    return AMF_OK;
}
