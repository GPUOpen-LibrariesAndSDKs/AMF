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

#include "BackBufferPresenter.h"
#include "SwapChainDX11.h"
#include "SwapChainDXGIDecode.h"

#include <atlbase.h>
#include <d3d11.h>
#include <dcomp.h>

#include <DirectXMath.h>
using namespace DirectX;

class VideoPresenterDX11 : public BackBufferPresenter
{
public:
    VideoPresenterDX11(amf_handle hwnd, amf::AMFContext* pContext);

    virtual ~VideoPresenterDX11();

    virtual AMF_RESULT                  Present(amf::AMFSurface* pSurface);

    virtual bool                        SupportAllocator() const { return true; }

    virtual amf::AMF_MEMORY_TYPE        GetMemoryType() const { return amf::AMF_MEMORY_DX11; }
//  virtual amf::AMF_SURFACE_FORMAT     GetInputFormat() { return amf::AMF_SURFACE_BGRA; }
    virtual amf::AMF_SURFACE_FORMAT     GetInputFormat() const{ return m_eInputFormat; }
    virtual AMF_RESULT                  SetInputFormat(amf::AMF_SURFACE_FORMAT format);
    virtual AMF_RESULT                  Flush();

    virtual AMF_RESULT                  Init(amf_int32 width, amf_int32 height, amf::AMFSurface* pSurface);
    virtual AMF_RESULT                  Terminate();
    virtual AMFSize                     GetSwapchainSize();
	virtual	AMFRate				        GetDisplayRefreshRate();

    // amf::AMFDataAllocatorCB interface
    virtual AMF_RESULT AMF_STD_CALL     AllocSurface(amf::AMF_MEMORY_TYPE type, amf::AMF_SURFACE_FORMAT format,
            amf_int32 width, amf_int32 height, amf_int32 hPitch, amf_int32 vPitch, amf::AMFSurface** ppSurface);
    // amf::AMFSurfaceObserver interface
    virtual void AMF_STD_CALL           OnSurfaceDataRelease(amf::AMFSurface* pSurface);

    virtual AMFPoint                    MapClientToSource(const AMFPoint& point) override;
    virtual AMFPoint                    MapSourceToClient(const AMFPoint& point) override;

    virtual AMF_RESULT                  SetViewTransform(amf_int iOffsetX, amf_int iOffsetY, amf_float fScale);
    virtual AMF_RESULT                  GetViewTransform(amf_int& iOffsetX, amf_int& iOffsetY, amf_float& fScale);

protected:
    
    struct SimpleVertex
    {
        XMFLOAT3 position;
        XMFLOAT2 texture;
    };

    struct CBNeverChanges
    {
        XMMATRIX mView;
    };

    using RenderTarget = SwapChainDX11::BackBuffer;

    virtual AMF_RESULT                  SetHDREnable(bool bEnable);
    virtual bool                        GetHDREnable() const;

    virtual AMF_RESULT                  DrawBackground(const RenderTarget* pRenderTarget);
    virtual AMF_RESULT                  SetStates();
    virtual AMF_RESULT                  DrawFrame(ID3D11Texture2D* pSrcSurface, const RenderTarget* pRenderTarget, amf_bool left);
    virtual AMF_RESULT                  DrawOverlay(amf::AMFSurface* /*pSurface*/, const RenderTarget* /*pRenderTarget*/) { return AMF_OK; }

    virtual void                        UpdateProcessor();
    virtual AMF_RESULT                  CustomDraw();
    virtual AMF_RESULT                  ResizeSwapChain();

    virtual AMF_RESULT                  DropFrame();

    CComPtr<ID3D11Device>               m_pDevice;
    std::unique_ptr<SwapChain>          m_pSwapChain;

private:
    AMF_RESULT PresentWithDC(amf::AMFSurface* pSurface);
    AMF_RESULT PresentWithSwapChain(amf::AMFSurface* pSurface);

    AMF_RESULT RenderSurface(amf::AMFSurface* pSurface, const RenderTarget* pRenderTarget);
    AMF_RESULT BitBlt(amf::AMF_FRAME_TYPE eFrameType, amf::AMFSurface* pSrcSurface, AMFRect* pSrcRect, const RenderTarget* pRenderTarget, AMFRect* pDstRect);
    AMF_RESULT BitBltRender(amf::AMF_FRAME_TYPE eFrameType, amf::AMFSurface* pSrcSurface, AMFRect* pSrcRect, const RenderTarget* pRenderTarget, AMFRect* pDstRect);
    AMF_RESULT BitBltCopy(amf::AMFSurface* pSrcSurface, AMFRect* pSrcRect, const RenderTarget* pRenderTarget, AMFRect* pDstRect);
    AMF_RESULT CreateShaders();
    AMF_RESULT PrepareStates();
    AMF_RESULT CheckForResize(amf_bool force);
    AMF_RESULT UpdateVertices(AMFRect *pSrcRect, AMFSize *pSrcSize, AMFRect *pDstRect, AMFSize *pDstSize);
    AMF_RESULT CopySurface(amf::AMF_FRAME_TYPE eFrameType, ID3D11Texture2D* pSrcSurface, AMFRect* pSrcRect);

    AMF_RESULT ApplyCSC(amf::AMFSurface* pSurface);

    amf::AMF_SURFACE_FORMAT             m_eInputFormat;

    CComPtr<ID3D11Texture2D>            m_pCopyTexture_L;
    CComPtr<ID3D11Texture2D>            m_pCopyTexture_R;

    CComPtr<ID3D11Buffer>               m_pVertexBuffer;
    CComPtr<ID3D11Buffer>               m_pCBChangesOnResize;
    CComPtr<ID3D11VertexShader>         m_pVertexShader;
    CComPtr<ID3D11PixelShader>          m_pPixelShader;
    CComPtr<ID3D11InputLayout>          m_pVertexLayout;
    CComPtr<ID3D11SamplerState>         m_pSampler;
    CComPtr<ID3D11DepthStencilState>    m_pDepthStencilState;
    CComPtr<ID3D11RasterizerState>      m_pRasterizerState;
    CComPtr<ID3D11BlendState>           m_pBlendState;
    D3D11_VIEWPORT                      m_CurrentViewport;
    XMFLOAT4X4                          m_mSrcToClientMatrix;
    XMFLOAT4X4                          m_mSrcToScreenMatrix;

    float                               m_fScale;
    float                               m_fPixelAspectRatio;
    amf_int                             m_iOffsetX;
    amf_int                             m_iOffsetY;
    bool                                m_bUpdateVertices;

    amf::AMFCriticalSection             m_sect;
    std::vector<amf::AMFSurface*>       m_TrackSurfaces; // raw pointer  doent want keep references to ensure object is destroying

    bool                                m_bResizeSwapChain;
    bool                                m_bFirstFrame; 
    bool                                m_bHDREnabled;
};