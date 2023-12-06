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

#include <d3dcompiler.h>
#include "public/common/TraceAdapter.h"
#include "public/include/components/VideoDecoderUVD.h"
#include "public/include/components/VideoConverter.h"

#include "QuadDX11_vs.h"
#include "QuadDX11_ps.h"

using namespace amf;

#define  AMF_FACILITY  L"VideoPresenterDX11"

VideoPresenterDX11::VideoPresenterDX11(amf_handle hwnd, amf::AMFContext* pContext) :
    BackBufferPresenter(hwnd, pContext),
    m_pSwapChain(nullptr),
    m_fScale(1.0f),
    m_fPixelAspectRatio(1.0f),
    m_iOffsetX(0),
    m_iOffsetY(0),
    m_bUpdateVertices(false),
    m_bResizeSwapChain(false),
    m_bFirstFrame(true),
    m_bHDREnabled(true),
//    m_eInputFormat(amf::AMF_SURFACE_BGRA)
    m_eInputFormat(amf::AMF_SURFACE_RGBA)
//     m_eInputFormat(amf::AMF_SURFACE_RGBA_F16)
//    m_eInputFormat(amf::AMF_SURFACE_R10G10B10A2)

{
    m_CurrentViewport = {};
}

VideoPresenterDX11::~VideoPresenterDX11()
{
    Terminate();
    m_pContext = NULL;
    m_hwnd = NULL;

}

AMF_RESULT VideoPresenterDX11::PresentWithDC(amf::AMFSurface* pSurface)
{
    SwapChainDXGIDecode* pSwapChainDecode = (SwapChainDXGIDecode*)m_pSwapChain.get();

    AMF_RESULT res = pSwapChainDecode->Submit(pSurface);
    AMF_RETURN_IF_FAILED(res, L"PresentWithDC() - Submit() failed");

    res = pSwapChainDecode->Present(m_bWaitForVSync);
    AMF_RETURN_IF_FAILED(res, L"m_swapChainDecode() - Present() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::PresentWithSwapChain(amf::AMFSurface* pSurface)
{
    AMF_RETURN_IF_INVALID_POINTER(pSurface, L"PresentWithSwapChain() - pSurface is NULL");
    AMF_RETURN_IF_INVALID_POINTER(m_pDevice, L"PresentWithSwapChain() - m_pDevice is NULL");

    // Will set m_bResizeSwapChain if window was resized
    // m_bResizeSwapChain is checked in either before rendering
    // or before allocating surface to processor
    AMF_RESULT res = CheckForResize(false);
    AMF_RETURN_IF_FAILED(res, L"PresentWithSwapChain() - CheckForResize() failed");

    res = pSurface->Convert(AMF_MEMORY_DX11);
    AMF_RETURN_IF_FAILED(res, L"PresentWithSwapChain() - Convert() failed");

    amf_uint index = 0;
    res = m_pSwapChain->GetBackBufferIndex(pSurface, index);
    AMF_RETURN_IF_FALSE(res == AMF_OK || res == AMF_NOT_FOUND, res, L"PresentWithSwapChain() - GetBackBufferIndex() failed");

    // If no processor is available (not rendering to back buffer),
    // then we must use the graphics pipeline with the shaders to
    // render into the render target buffer.
    //
    // In some cases, Present gets called before AllocSurface when 
    // switching to full screen in which case m_iBackBufferIndex is 0 
    // (AllocSurface increases it to 1) which can result into a negative 
    // m_iBackBufferIndex, which wraps causing long delay during resize
    // For this case just render the surface using the DX rendering pipeline
    if (m_bRenderToBackBuffer == false || res == AMF_NOT_FOUND)
    {
        if (m_bResizeSwapChain)
        {
            res = ResizeSwapChain();
            AMF_RETURN_IF_FAILED(res, L"PresentWithSwapChain() - ResizeSwapChain() failed");
            m_bResizeSwapChain = false;
        }

        const BackBufferBase* pBuffer = nullptr;
        res = m_pSwapChain->AcquireNextBackBuffer(&pBuffer);
        AMF_RETURN_IF_FAILED(res, L"RenderSurface() - AcquireNextBackBuffer() failed");

        const RenderTarget* pRenderTarget = (RenderTarget*)pBuffer;

        res = RenderSurface(pSurface, pRenderTarget);
        if (res != AMF_OK)
        {
            m_pSwapChain->DropBackBuffer(pRenderTarget);
        }
        AMF_RETURN_IF_FAILED(res, L"RenderSurface() - RenderSurface() failed");

        CComPtr<ID3D11DeviceContext> spContext;
        m_pDevice->GetImmediateContext(&spContext);
        spContext->OMSetRenderTargets(1, &pRenderTarget->pRTV_L.p, NULL);

        CustomDraw();
    }

    WaitForPTS(pSurface->GetPts());

    amf::AMFLock lock(&m_sect);

    res = m_pSwapChain->Present(m_bWaitForVSync);
    AMF_RETURN_IF_FAILED(res, L"PresentWithSwapChain() - SwapChain Present() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::Present(amf::AMFSurface* pSurface)
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NO_DEVICE, L"Present() - m_pDevice is NULL");
    AMF_RETURN_IF_INVALID_POINTER(pSurface, L"Present() - pSurface is NULL");
    AMF_RETURN_IF_FALSE(pSurface->GetFormat() == GetInputFormat(), AMF_INVALID_FORMAT, L"Surface format (%s)"
        "does not match input format (%s)", amf::AMFSurfaceGetFormatName(pSurface->GetFormat()),
        amf::AMFSurfaceGetFormatName(GetInputFormat()));

    AMF_RESULT res = pSurface->Convert(GetMemoryType());
    AMF_RETURN_IF_FAILED(res, L"Present() - Failed to convert surface to DX11 memory");

    res = ApplyCSC(pSurface);
    AMF_RETURN_IF_FAILED(res, L"Present() - ApplyCSC() failed");

    if (GetInputFormat() == AMF_SURFACE_NV12)
    {
        res = PresentWithDC(pSurface);
        AMF_RETURN_IF_FAILED(res, L"Present() - PresentWithDC() failed");
    }
    else
    {
        res = PresentWithSwapChain(pSurface);
        AMF_RETURN_IF_FAILED(res, L"Present() - PresentWithSwapChain() failed");
    }

    if (m_bResizeSwapChain)
    {
        return AMF_RESOLUTION_UPDATED;
    }

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::RenderSurface(amf::AMFSurface* pSurface, const RenderTarget* pRenderTarget)
{
    AMF_RETURN_IF_INVALID_POINTER(pSurface, L"RenderSurface() - pSurface is NULL");
    AMF_RETURN_IF_INVALID_POINTER(pRenderTarget, L"RenderSurface() - pRenderTarget is NULL");

    amf::AMFContext::AMFDX11Locker dxlock(m_pContext);

    AMFRect rectClient = GetClientRect();

    amf::AMFPlane* pPlane = pSurface->GetPlaneAt(0);
    CComPtr<ID3D11Texture2D> pSrcDxSurface = (ID3D11Texture2D*)pPlane->GetNative();
    AMF_RETURN_IF_INVALID_POINTER(pSrcDxSurface, L"RenderSurface() - pSrcDxSurface is NULL");

    AMFRect srcRect = { pPlane->GetOffsetX(),
                        pPlane->GetOffsetY(),
                        pPlane->GetOffsetX() + pPlane->GetWidth(),
                        pPlane->GetOffsetY() + pPlane->GetHeight() };

    AMFRect outputRect;
    AMF_RESULT err = CalcOutputRect(&srcRect, &rectClient, &outputRect);
    AMF_RETURN_IF_FAILED(err, L"RenderSurface() - CalcOutputRect failed");

    // In case of ROI we should specify SrcRect
    err = BitBlt(pSurface->GetFrameType(), pSurface, &srcRect, pRenderTarget, &outputRect);
    AMF_RETURN_IF_FAILED(err, L"RenderSurface() - BitBlt() failed to render");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::BitBlt(amf::AMF_FRAME_TYPE eFrameType, AMFSurface* pSrcSurface, AMFRect* pSrcRect, const RenderTarget* pRenderTarget, AMFRect* pDstRect)
{
    //if (GetInputFormat() == amf::AMF_SURFACE_NV12)
    //{
    //    return BitBltCopy(pSrcSurface, pSrcRect, pRenderTarget, pDstRect);
    //}
    return BitBltRender(eFrameType, pSrcSurface, pSrcRect, pRenderTarget, pDstRect);
}

AMF_RESULT VideoPresenterDX11::BitBltCopy(AMFSurface* pSrcSurface, AMFRect* pSrcRect, const RenderTarget* pRenderTarget, AMFRect* pDstRect)
{
    AMF_RETURN_IF_INVALID_POINTER(pSrcSurface, L"BitBltCopy() - pSrcSurface is NULL");
    AMF_RETURN_IF_INVALID_POINTER(pSrcRect, L"BitBltCopy() - pSrcRect is NULL");
    AMF_RETURN_IF_INVALID_POINTER(pRenderTarget, L"BitBltCopy() - pRenderTarget is NULL");
    AMF_RETURN_IF_INVALID_POINTER(pRenderTarget->pBuffer, L"BitBltCopy() - pRenderTarget->pBuffer is NULL");
    AMF_RETURN_IF_INVALID_POINTER(pDstRect, L"BitBltCopy() - pDstRect is NULL");
    AMF_RETURN_IF_INVALID_POINTER(m_pDevice, L"BitBltCopy() - m_pDevice is NULL");

    AMFPlane* pPlane = pSrcSurface->GetPlane(AMF_PLANE_PACKED);
    AMF_RETURN_IF_FALSE(pPlane != nullptr, AMF_INVALID_ARG, L"BitBltCopy() - Src surface does not have a packed plane");
    
    ID3D11Texture2D* pSrcDXSurface = (ID3D11Texture2D*)pPlane->GetNative();
    AMF_RETURN_IF_FALSE(pSrcDXSurface != nullptr, AMF_INVALID_ARG, L"BitBltCopy() - Src surface native is NULL");

    D3D11_BOX box;
    box.left = pSrcRect->left;
    box.top = pSrcRect->top;
    box.right = pSrcRect->right;
    box.bottom = pSrcRect->bottom;
    box.front=0;
    box.back=1;

    CComPtr<ID3D11DeviceContext> spContext;
    m_pDevice->GetImmediateContext(&spContext);
    spContext->CopySubresourceRegion(pRenderTarget->pBuffer, 0, pDstRect->left, pDstRect->top, 0, pSrcDXSurface, 0, &box);
    spContext->Flush();

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::BitBltRender(amf::AMF_FRAME_TYPE eFrameType, AMFSurface* pSrcSurface, AMFRect* pSrcRect, const RenderTarget* pRenderTarget, AMFRect* pDstRect)
{
    AMF_RETURN_IF_INVALID_POINTER(pSrcSurface, L"BitBltRender() - pSrcSurface is NULL");
    AMF_RETURN_IF_INVALID_POINTER(pSrcRect, L"BitBltRender() - pSrcRect is NULL");
    AMF_RETURN_IF_INVALID_POINTER(pRenderTarget, L"BitBltRender() - pRenderTarget is NULL");
    AMF_RETURN_IF_INVALID_POINTER(pDstRect, L"BitBltRender() - pDstRect is NULL");
    AMF_RETURN_IF_INVALID_POINTER(m_pDevice, L"BitBltRender() - m_pDevice is NULL");

    AMFPlane* pPlane = pSrcSurface->GetPlane(AMF_PLANE_PACKED);
    AMF_RETURN_IF_FALSE(pPlane != nullptr, AMF_INVALID_ARG, L"BitBltRender() - Src surface does not have a packed plane");

    ID3D11Texture2D* pSrcDXSurface = (ID3D11Texture2D*)pPlane->GetNative();
    AMF_RETURN_IF_FALSE(pSrcDXSurface != nullptr, AMF_INVALID_ARG, L"BitBltRender() - Src surface native is NULL");

    D3D11_TEXTURE2D_DESC srcDesc;
    pSrcDXSurface->GetDesc(&srcDesc);

    D3D11_TEXTURE2D_DESC dstDesc;
    pRenderTarget->pBuffer->GetDesc(&dstDesc);

    // Describes the area of the surface we want to render
    AMFRect newSourceRect = *pSrcRect;

    // Describes the total size of the surface
    AMFSize srcSize = { (amf_int32)srcDesc.Width, (amf_int32)srcDesc.Height };

    // If the surface is not bound to a shader, we have to copy it to another texture and bind that one
    if ((srcDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) != D3D11_BIND_SHADER_RESOURCE || (srcDesc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE))
    {
        AMF_RESULT err = CopySurface(eFrameType, pSrcDXSurface, pSrcRect);
        AMF_RETURN_IF_FAILED(err, L"BitBltRender() - CopySurface() failed");

        // When we copy the surface, we only copy the area
        // given by pSrcRect therefore the copied source size
        // becomes the same as the pSrcRect size
        srcSize.width = pSrcRect->Width();
        srcSize.height = pSrcRect->Height();
    }

    AMFSize dstSize = { (amf_int32)dstDesc.Width, (amf_int32)dstDesc.Height };
    AMF_RESULT err = UpdateVertices(&newSourceRect, &srcSize, pDstRect, &dstSize);
    AMF_RETURN_IF_FAILED(err, L"BitBltRender() - CopySurface() failed");

    err = DrawBackground(pRenderTarget); // Clears the background
    AMF_RETURN_IF_FAILED(err, L"BitBltRender() - DrawBackground() failed");

    err = SetStates();
    AMF_RETURN_IF_FAILED(err, L"BitBltRender() - SetStates() failed");

    // render left
    if ((eFrameType & amf::AMF_FRAME_LEFT_FLAG) == amf::AMF_FRAME_LEFT_FLAG || (eFrameType & amf::AMF_FRAME_STEREO_FLAG) == 0 || eFrameType == amf::AMF_FRAME_UNKNOWN)
    {
        err = DrawFrame(m_pCopyTexture_L != NULL ? m_pCopyTexture_L : pSrcDXSurface, pRenderTarget, true);
        AMF_RETURN_IF_FAILED(err, L"BitBltRender() - DrawFrame(left) failed");
    }

    // render right
    if ((eFrameType & amf::AMF_FRAME_RIGHT_FLAG) == amf::AMF_FRAME_RIGHT_FLAG && eFrameType != amf::AMF_FRAME_UNKNOWN)
    {
        err = DrawFrame(m_pCopyTexture_R != NULL ? m_pCopyTexture_R : pSrcDXSurface, pRenderTarget, false);
        AMF_RETURN_IF_FAILED(err, L"BitBltRender() - DrawFrame(right) failed");
    }

    err = DrawOverlay(pSrcSurface, pRenderTarget);
    AMF_RETURN_IF_FAILED(err, L"BitBltRender() - DrawOverlay() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::CopySurface(amf::AMF_FRAME_TYPE eFrameType, ID3D11Texture2D* pSrcSurface, AMFRect* pSrcRect)
{
    AMF_RETURN_IF_INVALID_POINTER(pSrcSurface, L"CopySurface() - pSrcSurface is NULL");
    AMF_RETURN_IF_INVALID_POINTER(pSrcRect, L"CopySurface() - pSrcRect is NULL");
    AMF_RETURN_IF_INVALID_POINTER(m_pDevice, L"CopySurface() - m_pDevice is NULL");

    D3D11_TEXTURE2D_DESC srcDesc;
    pSrcSurface->GetDesc(&srcDesc);

    // Create textures if not available
    if(m_pCopyTexture_L == NULL)
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

        Desc.Width = pSrcRect->Width();
        Desc.Height = pSrcRect->Height();
        Desc.MipLevels = 1;
        Desc.SampleDesc.Count = 1;
        Desc.CPUAccessFlags = 0;

        HRESULT hr = m_pDevice->CreateTexture2D(&Desc, NULL, &m_pCopyTexture_L);    
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CopySurface() - Failed to create%scopy texture", (m_pSwapChain->StereoEnabled() == true ? " the left " : " "));

        if(m_pSwapChain->StereoEnabled())
        {
            hr = m_pDevice->CreateTexture2D(&Desc, NULL, &m_pCopyTexture_R);
            ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CopySurface() - Failed to create the right copy texture");
        }
    }

    D3D11_BOX box;
    box.left = pSrcRect->left;
    box.top = pSrcRect->top;
    box.right = pSrcRect->right;
    box.bottom= pSrcRect->bottom;
    box.front=0;
    box.back=1;

    CComPtr<ID3D11DeviceContext> spContext;
    m_pDevice->GetImmediateContext(&spContext);

    switch(eFrameType)
    {
    case amf::AMF_FRAME_STEREO_RIGHT:
        spContext->CopySubresourceRegion(m_pCopyTexture_R,0,0,0,0,pSrcSurface,0,&box); // we expect that texture comes as a single slice
        break;
    case amf::AMF_FRAME_STEREO_BOTH:
        spContext->CopySubresourceRegion(m_pCopyTexture_L,0,0,0,0,pSrcSurface,0,&box); // we expect that texture comes as array of two silces
        spContext->CopySubresourceRegion(m_pCopyTexture_R,0,0,0,0,pSrcSurface,1,&box);
        break;
    default:
        if(srcDesc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE)
        {
            spContext->CopySubresourceRegion(m_pCopyTexture_L,0,0,0,0,pSrcSurface,m_iSubresourceIndex,&box); // TODO get subresource property
        }
        else
        {
            spContext->CopySubresourceRegion(m_pCopyTexture_L,0,0,0,0,pSrcSurface,0,&box);
        }
        break;
    }

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::DrawBackground(const RenderTarget* pRenderTarget)
{
    CComPtr<ID3D11DeviceContext> spContext;
    m_pDevice->GetImmediateContext(&spContext);

    const float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    spContext->ClearRenderTargetView(pRenderTarget->pRTV_L, ClearColor);

    if (pRenderTarget->pRTV_R != nullptr)
    {
        spContext->ClearRenderTargetView(pRenderTarget->pRTV_R, ClearColor);
    }

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::SetStates()
{
    AMF_RETURN_IF_INVALID_POINTER(m_pDevice, L"SetStates() - m_pDevice is NULL");
    AMF_RETURN_IF_INVALID_POINTER(m_pVertexShader, L"SetStates() - m_pVertexShader is NULL");
    AMF_RETURN_IF_INVALID_POINTER(m_pPixelShader, L"SetStates() - m_pPixelShader is NULL");
    AMF_RETURN_IF_INVALID_POINTER(m_pCBChangesOnResize, L"SetStates() - m_pCBChangesOnResize is NULL");
    AMF_RETURN_IF_INVALID_POINTER(m_pSampler, L"SetStates() - m_pSampler is NULL");
    AMF_RETURN_IF_INVALID_POINTER(m_pDepthStencilState, L"SetStates() - m_pDepthStencilState is NULL");
    AMF_RETURN_IF_INVALID_POINTER(m_pRasterizerState, L"SetStates() - m_pRasterizerState is NULL");
    AMF_RETURN_IF_INVALID_POINTER(m_pVertexLayout, L"SetStates() - m_pVertexLayout is NULL");
    AMF_RETURN_IF_INVALID_POINTER(m_pBlendState, L"SetStates() - m_pBlendState is NULL");
    AMF_RETURN_IF_INVALID_POINTER(m_pVertexBuffer, L"SetStates() - m_pVertexBuffer is NULL");

    // setup all states
    CComPtr<ID3D11DeviceContext> spContext;
    m_pDevice->GetImmediateContext(&spContext);

    // setup shaders
    spContext->VSSetShader(m_pVertexShader, NULL, 0);                               // Vertex Shader
    spContext->VSSetConstantBuffers(0, 1, &m_pCBChangesOnResize.p);                 // Vertex Shader resize matrix CB

    spContext->PSSetShader(m_pPixelShader, NULL, 0); // Pixel shader
    spContext->PSSetConstantBuffers(0, 1, &m_pCBChangesOnResize.p);                 // Pixel shader resize matrix CB
    spContext->PSSetSamplers(0, 1, &m_pSampler.p);                                  // Pixel shader sampler state

    spContext->OMSetDepthStencilState(m_pDepthStencilState, 1);                     // Depth stencil
    spContext->RSSetState(m_pRasterizerState);                                      // Rasterizer state
    spContext->IASetInputLayout(m_pVertexLayout);                                   // Vertex Layout

    // Render vertices as a bunch of connected triangles (sharing 2 vertices)
    // 4 vertices in vertex buffer create two connected triangles for the rectangular video frame
    spContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    spContext->RSSetViewports(1, &m_CurrentViewport);                               // Viewport
    float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // No blending
    spContext->OMSetBlendState(m_pBlendState, blendFactor, 0xFFFFFFFF);             // Blend state

    // Set vertex buffer
    UINT stride = sizeof(SimpleVertex);
    UINT offset = 0;
    spContext->IASetVertexBuffers(0, 1, &m_pVertexBuffer.p, &stride, &offset);

    // Index buffer not required

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::DrawFrame(ID3D11Texture2D* pSrcSurface, const RenderTarget* pRenderTarget, amf_bool left)
{
    AMF_RETURN_IF_INVALID_POINTER(pSrcSurface, L"DrawFrame() - pSrcSurface is NULL");
    AMF_RETURN_IF_INVALID_POINTER(m_pDevice, L"DrawFrame() - m_pDevice is NULL");

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
        viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY; // we expect that texture comes as array of two silces
        viewDesc.Texture2DArray.ArraySize = srcDesc.ArraySize;
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
    spContext->OMSetRenderTargets(1, &pRenderTargetView.p, NULL);
    spContext->Draw(4, 0);

    // Clear render target and texture set
    ID3D11RenderTargetView *pNULLRT = NULL;
    spContext->OMSetRenderTargets(1, &pNULLRT, NULL);

    ID3D11ShaderResourceView* pNULL = NULL;
    spContext->PSSetShaderResources(0, 1, &pNULL);
    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::Init(amf_int32 width, amf_int32 height, amf::AMFSurface* pSurface)
{
    // pSurface is allowed to be null so don't need to error check

    AMF_RESULT res = VideoPresenter::Init(width, height);
    AMF_RETURN_IF_FAILED(res, L"Init() - VideoPresenter::Init() failed");

    // Get DX device
    m_pDevice = static_cast<ID3D11Device*>(m_pContext->GetDX11Device());
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NO_DEVICE, L"Init() - Failed to get D3D device");
    

    // If not using the DC swap chain, we need to setup
    // shaders and graphics pipeline for rendering
    if (GetInputFormat() != AMF_SURFACE_NV12)
    {
        m_pSwapChain = std::make_unique<SwapChainDX11>(m_pContext);

        // Setup swap chain
        m_pSwapChain->Init(m_hwnd, m_hDisplay, pSurface, width, height, GetInputFormat(), false, m_bHDREnabled);
        AMF_RETURN_IF_FAILED(res, L"Init() - m_pSwapChain->Init() failed");

        // CreateShaders
        res = CreateShaders();
        AMF_RETURN_IF_FAILED(res, L"Init() - Failed to compile shaders");

        // Setup pipeline buffers and states
        res = PrepareStates();
        AMF_RETURN_IF_FAILED(res, L"Init() - Failed to prepare graphics pipeline states");
    }
    else
    {
        m_pSwapChain = std::make_unique<SwapChainDXGIDecode>(m_pContext, GetMemoryType());

        res = m_pSwapChain->Init(m_hwnd, m_hDisplay, pSurface, width, height, GetInputFormat());
        AMF_RETURN_IF_FAILED(res, L"Init() - m_swapChainDecode.Init() failed");
    }

    m_bFirstFrame = true;
    ResizeSwapChain();
    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::Terminate()
{
    m_pVertexBuffer = NULL;
    m_pCBChangesOnResize = NULL;
    m_pVertexShader = NULL;
    m_pPixelShader = NULL;
    m_pVertexLayout = NULL;
    m_pSampler = NULL;
    m_pDepthStencilState = NULL;
    m_pRasterizerState = NULL;
    m_pBlendState = NULL;

    m_pCopyTexture_L = NULL;
    m_pCopyTexture_R = NULL;

    m_pDevice = NULL;

    for (std::vector<amf::AMFSurface*>::iterator it = m_TrackSurfaces.begin(); it != m_TrackSurfaces.end(); it++)
    {
        (*it)->RemoveObserver(this);
    }
    m_TrackSurfaces.clear();
    m_bResizeSwapChain = false;

    if (m_pSwapChain != nullptr)
    {
        m_pSwapChain->Terminate();
        m_pSwapChain = nullptr;
    }

    return VideoPresenter::Terminate();
}

AMF_RESULT VideoPresenterDX11::CreateShaders()
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateShaders() - m_pDevice is not initialized");

    HRESULT hr = m_pDevice->CreateVertexShader(QuadDX11_vs, sizeof(QuadDX11_vs), nullptr, &m_pVertexShader);
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"CreateShaders() - CreateVertexShader() failed");

    // Define the input layout
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    UINT numElements = ARRAYSIZE(layout);

    // Create the input layout
    hr = m_pDevice->CreateInputLayout(layout, numElements, QuadDX11_vs, sizeof(QuadDX11_vs), &m_pVertexLayout);
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"CompileShaders() - CreateInputLayout() failed");

    hr = m_pDevice->CreatePixelShader(QuadDX11_ps, sizeof(QuadDX11_ps), nullptr, &m_pPixelShader);
    AMF_RETURN_IF_FALSE(SUCCEEDED(hr), AMF_FAIL, L"CreateShaders() - CreatePixelShader() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::PrepareStates()
{
    AMF_RETURN_IF_INVALID_POINTER(m_pDevice, L"PrepareStates() - m_pDevice is NULL");

    // Create vertex buffer (quad will be set later)
    D3D11_BUFFER_DESC bd;
    ZeroMemory(&bd, sizeof(bd));
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof( SimpleVertex ) * 4; // 4 - for video frame
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = 0;

    HRESULT hr = m_pDevice->CreateBuffer(&bd, NULL, &m_pVertexBuffer);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"PrepareStates() - Failed to create vertex buffer");

    // Create constant buffer for vertex shader
    // This buffer stores a transformation matrix 
    // for converting the vertex positions
    // which is updated when the window is resized
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(CBNeverChanges);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA InitData={0};
    InitData.pSysMem = &m_mSrcToScreenMatrix; // m_mSrcToScreenMatrix is updated on window resize in UpdateVertex

    hr = m_pDevice->CreateBuffer(&bd, &InitData, &m_pCBChangesOnResize);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"PrepareStates() - Failed to create constant buffer");

    // Create the sampler state
    D3D11_SAMPLER_DESC sampDesc;
    ZeroMemory( &sampDesc, sizeof(sampDesc) );
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    hr = m_pDevice->CreateSamplerState( &sampDesc, &m_pSampler );
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"PrepareStates() - Failed to create sampler state");

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

AMF_RESULT VideoPresenterDX11::CheckForResize(amf_bool force)
{
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"CheckForResize() - m_pSwapChain is not initialized");

    if (force == true)
    {
        m_bResizeSwapChain = true;
        return AMF_OK;
    }

    // Check if we have changed the entered or exited fullscreen
    if (m_pSwapChain->FullscreenEnabled() != m_bFullScreen)
    {
        m_bResizeSwapChain = true;
        return AMF_OK;
    }

    AMFRect client = GetClientRect();
    amf_int width = client.Width();
    amf_int height = client.Height();

    AMFSize swapchainSize = GetSwapchainSize();

    if ((width == swapchainSize.width && height == swapchainSize.height) || width == 0 || height == 0)
    {
        return AMF_OK;
    }

    m_bResizeSwapChain = true;
    return AMF_OK;
}

AMFSize VideoPresenterDX11::GetSwapchainSize()
{
    if (m_pSwapChain == nullptr)
    {
        return AMFConstructSize(0, 0);
    }

    return m_pSwapChain->GetSize();
}

AMF_RESULT VideoPresenterDX11::ResizeSwapChain()
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"ResizeSwapChain() - m_pDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"ResizeSwapChain() - m_pSwapChain is not initialized");

    AMF_RESULT res = m_pSwapChain->Resize(0, 0, m_bFullScreen);
    AMF_RETURN_IF_FAILED(res, L"ResizeSwapChain() - SwapChain Resize() failed");

    const AMFSize size = GetSwapchainSize();

    m_CurrentViewport.TopLeftX = 0;
    m_CurrentViewport.TopLeftY = 0;
    m_CurrentViewport.Width = FLOAT(size.width);
    m_CurrentViewport.Height = FLOAT(size.height);
    m_CurrentViewport.MinDepth = 0.0f;
    m_CurrentViewport.MaxDepth = 1.0f;

    AMFRect client;
    if (m_bFullScreen)
    {
        client.left = 0;
        client.top = 0;
        client.right = size.width;
        client.bottom = size.height;
    }
    else
    {
        client = GetClientRect();
    }

    m_rectClient = AMFConstructRect(client.left, client.top, client.right, client.bottom);

    UpdateProcessor();
    m_bResizeSwapChain = false;

    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::DropFrame()
{
    if (m_pSwapChain == nullptr || GetInputFormat() == AMF_SURFACE_NV12)
    {
        return AMF_OK;
    }

    return m_pSwapChain->DropLastBackBuffer();
}

AMF_RESULT VideoPresenterDX11::UpdateVertices(AMFRect * pSrcRect, AMFSize *pSrcSize, AMFRect *pDstRect, AMFSize *pDstSize)
{
    AMF_RETURN_IF_INVALID_POINTER(pSrcRect, L"UpdateVertices() - pSrcRect is NULL");
    AMF_RETURN_IF_INVALID_POINTER(pSrcSize, L"UpdateVertices() - pSrcSize is NULL");
    AMF_RETURN_IF_INVALID_POINTER(pDstRect, L"UpdateVertices() - pDstRect is NULL");
    AMF_RETURN_IF_INVALID_POINTER(pDstSize, L"UpdateVertices() - pDstSize is NULL");
    AMF_RETURN_IF_INVALID_POINTER(m_pDevice, L"UpdateVertices() - m_pDevice is NULL");

    // Updates the vertex buffer and src to screen matrix for vertex/pixel constant buffer

    // Don't need to update vertices if size hasn't changed
    if(m_bUpdateVertices == false && *pSrcRect == m_sourceVertexRect  &&  *pDstRect == m_destVertexRect)
    {
        return AMF_OK;   
    }

    m_sourceVertexRect = *pSrcRect;
    m_destVertexRect = *pDstRect;
    m_bUpdateVertices = false;

    FLOAT srcCenterX = (FLOAT)(pSrcRect->left + pSrcRect->right) / 2.f;
    FLOAT srcCenterY = (FLOAT)(pSrcRect->top + pSrcRect->bottom) / 2.f;

    FLOAT srcWidth = (FLOAT)pSrcRect->Width() / pSrcSize->width;
    FLOAT srcHeight = (FLOAT)pSrcRect->Height() / pSrcSize->height;

    // Normalize texture coordinates from 0-1 in both axis for sampling
    FLOAT leftSrcTex = srcCenterX / pSrcRect->Width() - srcWidth / 2;
    FLOAT rightSrcTex = leftSrcTex + srcWidth;
    FLOAT topSrcTex = srcCenterY / pSrcRect->Height() - srcHeight / 2;
    FLOAT bottomSrcTex = topSrcTex + srcHeight;

    FLOAT leftSrc = (FLOAT)pSrcRect->left;
    FLOAT rightSrc = (FLOAT)pSrcRect->right;
    FLOAT topSrc = (FLOAT)pSrcRect->top;
    FLOAT bottomSrc = (FLOAT)pSrcRect->bottom;

    SimpleVertex vertices[4];

    srcWidth = (FLOAT)pSrcRect->Width();
    srcHeight = (FLOAT)pSrcRect->Height();

    if (m_iOrientation % 2 == 1)
    {
        std::swap(srcWidth, srcHeight);
        std::swap(rightSrcTex, leftSrcTex);
        std::swap(bottomSrcTex, topSrcTex);
    }

    // Flip texture coords in y-axis since textures(pixels) y-coords are
    // positive in the down direction and vertices are positive going up
    // Add the vertices in clockwise order so it doesn't get culled

    // Top Left
    vertices[0].position = XMFLOAT3(leftSrc, bottomSrc, 0.0f);
    vertices[0].texture = XMFLOAT2(leftSrcTex, topSrcTex);

    // Top Right
    vertices[1].position = XMFLOAT3(rightSrc, bottomSrc, 0.0f);
    vertices[1].texture = XMFLOAT2(rightSrcTex, topSrcTex);

    // Bottom Left
    vertices[2].position = XMFLOAT3(leftSrc, topSrc, 0.0f);
    vertices[2].texture = XMFLOAT2(leftSrcTex, bottomSrcTex);

    // Bottom right - Adds second triangle with TriangleStrip topology
    vertices[3].position = XMFLOAT3(rightSrc, topSrc, 0.0f);
    vertices[3].texture = XMFLOAT2(rightSrcTex, bottomSrcTex);

    FLOAT clientCenterX = (FLOAT)(m_rectClient.left + m_rectClient.right) / 2.f;
    FLOAT clientCenterY = (FLOAT)(m_rectClient.top + m_rectClient.bottom) / 2.f;

    FLOAT scaleX = (FLOAT)m_rectClient.Width() / (FLOAT)srcWidth;
    FLOAT scaleY = (FLOAT)m_rectClient.Height() / (FLOAT)srcHeight;
    // Maintains original surface aspect ratio and ensures it fits inside client space
    FLOAT scaleMin = AMF_MIN(scaleX, scaleY);

    // Transform surface to client by centering it in the client viewport and 
    // scaling to maximum size in either direction (maximize size with original aspect ratio)
    XMMATRIX srcToClientMatrix = XMMatrixTranslation(-srcCenterX, -srcCenterY, 0.0f)
        * XMMatrixRotationZ(::XMConvertToRadians(90.0f * m_iOrientation))
        * XMMatrixScaling(scaleMin, scaleMin, 1.0f)
        * XMMatrixTranslation(clientCenterX, clientCenterY, 0.0f);
    XMStoreFloat4x4(&m_mSrcToClientMatrix, srcToClientMatrix);

    // Transforms client coordinates to -1 to 1 (x and y) normalized coordinate space 
    // Direct3D assumes viewport clipping ranges from -1.0 to 1.0
    XMMATRIX screenMatrix = XMMatrixScaling(1.0f / pDstSize->width, 1.0f / pDstSize->height, 1.0f)
        * XMMatrixTranslation(-0.5f, -0.5f, 1.0f)
        * XMMatrixScaling(m_fScale, m_fScale, 1.0f)
        * XMMatrixTranslation((float)m_iOffsetX / pDstSize->width, -(float)m_iOffsetY / pDstSize->height, 0.0f)
        * XMMatrixScaling(2.0f, 2.0f, 1.0f);

    XMStoreFloat4x4(&m_mSrcToScreenMatrix, srcToClientMatrix * screenMatrix);

    CComPtr<ID3D11DeviceContext> spContext;
    m_pDevice->GetImmediateContext(&spContext);
    spContext->UpdateSubresource(m_pVertexBuffer, 0, NULL, vertices, 0, 0 );
    spContext->UpdateSubresource(m_pCBChangesOnResize, 0, NULL, &m_mSrcToScreenMatrix, 0, 0);
    
    return AMF_OK;
}

AMFPoint VideoPresenterDX11::MapClientToSource(const AMFPoint& point)
{
    if (m_pProcessor == nullptr)
    {
        XMVECTOR pt = XMVectorSet((FLOAT)point.x, (FLOAT)point.y, 0.0f, 1.0f);
        XMMATRIX inverse = XMMatrixInverse(nullptr, XMLoadFloat4x4(&m_mSrcToClientMatrix));
        pt = XMVector4Transform(pt, inverse);

        return AMFConstructPoint((amf_int32)XMVectorGetX(pt), (amf_int32)XMVectorGetY(pt));
    }
    return point;
}

AMFPoint VideoPresenterDX11::MapSourceToClient(const AMFPoint& point)
{
    if (m_pProcessor == nullptr)
    {
        XMVECTOR pt = XMVectorSet((FLOAT)point.x, (FLOAT)point.y, 0.0f, 1.0f);
        pt = XMVector4Transform(pt, XMLoadFloat4x4(&m_mSrcToClientMatrix));

        return AMFConstructPoint((amf_int32)XMVectorGetX(pt), (amf_int32)XMVectorGetY(pt));
    }
    return point;
}

AMF_RESULT VideoPresenterDX11::SetHDREnable(bool bEnable)
{
    m_bHDREnabled = bEnable;
    return AMF_OK;
}

bool VideoPresenterDX11::GetHDREnable() const
{
    if (m_pSwapChain == nullptr)
    {
        return false;
    }

    return m_pSwapChain->HDREnabled();
}

AMF_RESULT VideoPresenterDX11::SetViewTransform(amf_int iOffsetX, amf_int iOffsetY, amf_float fScale)
{
    m_iOffsetX = iOffsetX;
    m_iOffsetY = iOffsetY;
    m_fScale = fScale;

    m_bUpdateVertices = true;
    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::GetViewTransform(amf_int& iOffsetX, amf_int& iOffsetY, amf_float& fScale)
{
    iOffsetX = m_iOffsetX;
    iOffsetY = m_iOffsetY;
    fScale   = m_fScale  ;

    return AMF_OK;
}


AMF_RESULT AMF_STD_CALL VideoPresenterDX11::AllocSurface(amf::AMF_MEMORY_TYPE /* type */, amf::AMF_SURFACE_FORMAT /* format */,
            amf_int32 /* width */, amf_int32 /* height */, amf_int32 /* hPitch */, amf_int32 /* vPitch */, amf::AMFSurface** ppSurface)
{
    // Creates a surface from a swapchain buffer for rendering

    if(m_bRenderToBackBuffer == false)
    {
        return AMF_NOT_IMPLEMENTED;
    }
    // wait till buffers are released
    while(m_pSwapChain->GetBackBuffersAvailable() == 0)
    {
        if(m_bFrozen)
        {
            return AMF_INPUT_FULL;
        }
        amf_sleep(1);
    }

    if(m_bResizeSwapChain)
    {
        // Wait till all buffers are released
        // since resizing swapchain involves
        // recreating the buffers
        while(m_pSwapChain->GetBackBuffersAcquired() > 0 || m_TrackSurfaces.empty() == false)
        {
            amf_sleep(1);
        }

        AMF_RESULT err = ResizeSwapChain();
        AMF_RETURN_IF_FAILED(err, L"AllocSurface() - ResizeSwapChain() failed");
    }

    amf::AMFLock lock(&m_sect);
    // Ignore sizes and return back buffer

    AMF_RESULT res = m_pSwapChain->AcquireNextBackBuffer(ppSurface);
    AMF_RETURN_IF_FAILED(res, L"AllocSurface() - AcquireNextBackBuffer() failed");

    (*ppSurface)->AddObserver(this);
    m_TrackSurfaces.push_back(*ppSurface);
    return AMF_OK;
}

void AMF_STD_CALL VideoPresenterDX11::OnSurfaceDataRelease(amf::AMFSurface* pSurface)
{
    if (pSurface == nullptr)
    {
        return;
    }

    amf::AMFLock lock(&m_sect);
    for(std::vector<amf::AMFSurface*>::iterator it = m_TrackSurfaces.begin(); it != m_TrackSurfaces.end(); it++)
    {
        if( *it == pSurface)
        {
            pSurface->RemoveObserver(this);
            m_TrackSurfaces.erase(it);
            break;
        }
    }
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

    m_eInputFormat = format;
    return AMF_OK;
}

AMFRate VideoPresenterDX11::GetDisplayRefreshRate()
{
	AMFRate rate = VideoPresenter::GetDisplayRefreshRate();

    if (m_hwnd == nullptr)
    {
        return rate;
    }

    const HMONITOR hMonitor = MonitorFromWindow((HWND)m_hwnd, MONITOR_DEFAULTTONEAREST);

	MONITORINFOEX monitorInfo = {};
	monitorInfo.cbSize = sizeof(MONITORINFOEX);
	GetMonitorInfo(hMonitor, &monitorInfo);

	DEVMODE devMode = {};
	devMode.dmSize = sizeof(DEVMODE);
	devMode.dmDriverExtra = 0;
	EnumDisplaySettings(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &devMode);

	if (1 == devMode.dmDisplayFrequency || 0 == devMode.dmDisplayFrequency)
	{
		return rate;
	}
	rate.num = (amf_int32)devMode.dmDisplayFrequency;
	rate.den = 1;
	return rate;
}

AMF_RESULT VideoPresenterDX11::Flush()
{
    return BackBufferPresenter::Flush();
}

void VideoPresenterDX11::UpdateProcessor()
{
    VideoPresenter::UpdateProcessor();

    if (m_pProcessor == nullptr || m_pDevice == nullptr)
    {
        return;
    }

    // check and set color space and HDR support
    SwapChain::ColorSpace colorSpace;
    AMF_RESULT res = m_pSwapChain->GetColorSpace(colorSpace);
    if (res != AMF_OK)
    {
        AMFTraceError(AMF_FACILITY, L"UpdateProcessor() - GetColorSpace() failed");
        return;
    }

    m_pProcessor->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_TRANSFER_CHARACTERISTIC, colorSpace.transfer);
    m_pProcessor->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_COLOR_PRIMARIES, colorSpace.primaries);
    m_pProcessor->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_COLOR_RANGE, colorSpace.range);


    AMFHDRMetadata hdrMetaData = {};
    res = m_pSwapChain->GetOutputHDRMetaData(hdrMetaData);
    if (res != AMF_OK)
    {
        AMFTraceError(AMF_FACILITY, L"UpdateProcessor() - GetOutputHDRMetaData() failed");
        return;
    }

    if (hdrMetaData.maxMasteringLuminance != 0)
    {
        amf::AMFBufferPtr pHDRMetaDataBuffer;
        res = m_pContext->AllocBuffer(amf::AMF_MEMORY_HOST, sizeof(AMFHDRMetadata), &pHDRMetaDataBuffer);
        if (res != AMF_OK)
        {
            AMFTraceError(AMF_FACILITY, L"UpdateProcessor() - AllocBuffer() failed to allocate HDR metadata buffer");
            return;
        }

        AMFHDRMetadata* pData = (AMFHDRMetadata*)pHDRMetaDataBuffer->GetNative();
        memcpy(pData, &hdrMetaData, sizeof(AMFHDRMetadata));

        m_pProcessor->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_HDR_METADATA, pHDRMetaDataBuffer);
    }
}

AMF_RESULT VideoPresenterDX11::CustomDraw()
{
    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::ApplyCSC(amf::AMFSurface* pSurface)
{
    pSurface; // Suppress unreferenced parameter warnings
#ifdef USE_COLOR_TWITCH_IN_DISPLAY
    AMF_RETURN_IF_FALSE(pSurface != nullptr, AMF_INVALID_ARG, L"ApplyCSC() - pSurface is NULL");

    if (m_bFirstFrame == false || m_pSwapChain == nullptr)
    {
        return AMF_OK;
    }

    amf::AMFVariant varBuf;
    AMF_RESULT res = pSurface->GetProperty(AMF_VIDEO_DECODER_HDR_METADATA, &varBuf);
    if (res != AMF_OK || varBuf.type != AMF_VARIANT_INTERFACE)
    {
        return AMF_OK;
    }

    amf::AMFBufferPtr pBuffer(varBuf.pInterface);
    if (pBuffer == nullptr)
    {
        return AMF_OK;
    }

    AMFHDRMetadata* pHDRData = (AMFHDRMetadata*)pBuffer->GetNative();

    res = m_pSwapChain->SetHDRMetaData(pHDRData);
    if (res == AMF_NOT_SUPPORTED)
    {
        return AMF_OK;
    }
    AMF_RETURN_IF_FAILED(res, L"ApplyCSC() - SetHDRMetaData() failed");

#endif
    return AMF_OK;
}

