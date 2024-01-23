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
#include "SwapChainDX11.h"
#include "SwapChainDXGIDecode.h"

#include <atlbase.h>
#include <d3d11.h>
#include <dcomp.h>

class VideoPresenterDX11 : public VideoPresenter
{
public:
    VideoPresenterDX11(amf_handle hwnd, amf::AMFContext* pContext, amf_handle hDisplay=nullptr);

    virtual ~VideoPresenterDX11();

    virtual bool                            SupportAllocator() const { return true; }
    virtual amf::AMF_MEMORY_TYPE            GetMemoryType() const { return amf::AMF_MEMORY_DX11; }
    virtual AMF_RESULT                      SetInputFormat(amf::AMF_SURFACE_FORMAT format) override;

    virtual AMF_RESULT                      Init(amf_int32 width, amf_int32 height, amf::AMFSurface* pSurface);
    virtual AMF_RESULT                      Terminate();

protected:
    typedef SwapChainDX11::BackBuffer RenderTarget;

    virtual AMF_RESULT                      RenderToSwapChain(amf::AMFSurface* pSurface) override;
    virtual AMF_RESULT                      RenderSurface(amf::AMFSurface* pSurface, const RenderTargetBase* pRenderTarget, RenderViewSizeInfo& renderView) override;
    virtual AMF_RESULT                      UpdateStates(amf::AMFSurface* pSurface, const RenderTarget* pRenderTarget, RenderViewSizeInfo& renderView);
    virtual AMF_RESULT                      DrawBackground(const RenderTarget* pRenderTarget);
    virtual AMF_RESULT                      SetStates();
    virtual AMF_RESULT                      SetPipStates();
    virtual AMF_RESULT                      DrawFrame(ID3D11Texture2D* pSrcSurface, const RenderTarget* pRenderTarget, amf_bool left);
    virtual AMF_RESULT                      DrawOverlay(amf::AMFSurface* /*pSurface*/, const RenderTarget* /*pRenderTarget*/) { return AMF_OK; }

    virtual AMF_RESULT                      CreateShaders(ID3D11VertexShader** ppVertexShader, ID3D11InputLayout** ppInputLayout, ID3D11PixelShader** ppPixelShader);

    AMF_RESULT                              CreateBuffer(D3D11_BIND_FLAG bind, D3D11_USAGE usage, const void* pData, amf_size size, ID3D11Buffer** ppBuffer);
    AMF_RESULT                              CreateConstantBuffer(D3D11_USAGE usage, const void* pData, amf_size size, ID3D11Buffer** ppBuffer);
    AMF_RESULT                              CreateVertexBuffer(D3D11_USAGE usage, const void* pData, amf_size size, ID3D11Buffer** ppBuffer);
    AMF_RESULT                              UpdateBuffer(ID3D11Buffer* pBuffer, const void* pData, amf_size size, amf_size offset);

    virtual AMF_RESULT                      CustomDraw() { return AMF_OK; }
    virtual AMF_RESULT                      OnRenderViewResize(const RenderViewSizeInfo& newRenderView) override;
    virtual AMF_RESULT                      DropFrame();

    CComPtr<ID3D11Device>                   m_pDevice;
    CComPtr<ID3D11Buffer>                   m_pViewProjectionBuffer;

private:
    AMF_RESULT                              CopySurface(amf::AMFSurface* pSurface);
    AMF_RESULT                              CreateShaders();
    AMF_RESULT                              PrepareStates();

    CComPtr<ID3D11Texture2D>                m_pCopyTexture_L;
    CComPtr<ID3D11Texture2D>                m_pCopyTexture_R;

    // Quad pipeline
    CComPtr<ID3D11VertexShader>             m_pQuadVertexShader;
    CComPtr<ID3D11PixelShader>              m_pQuadPixelShader;
    CComPtr<ID3D11InputLayout>              m_pQuadVertexLayout;

    CComPtr<ID3D11Buffer>                   m_pVertexBuffer;
    CComPtr<ID3D11Buffer>                   m_pPIPViewProjectionBuffer;
    SamplerMap<CComPtr<ID3D11SamplerState>> m_pSamplerMap;


    CComPtr<ID3D11DepthStencilState>        m_pDepthStencilState;
    CComPtr<ID3D11RasterizerState>          m_pRasterizerState;
    CComPtr<ID3D11BlendState>               m_pBlendState;
};
