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
#include "SwapChainDX9.h"

#include <comdef.h>
#include <d3d9.h>
#include <DirectXMath.h>

class VideoPresenterDX9 : public BackBufferPresenter
{
public:
    VideoPresenterDX9(amf_handle hwnd, amf::AMFContext* pContext);

    virtual                             ~VideoPresenterDX9();

    virtual AMF_RESULT                  Present(amf::AMFSurface* pSurface);
    virtual amf::AMF_MEMORY_TYPE        GetMemoryType() const { return amf::AMF_MEMORY_DX9; }
    virtual amf::AMF_SURFACE_FORMAT     GetInputFormat() const { return m_eInputFormat; }
    virtual AMF_RESULT                  SetInputFormat(amf::AMF_SURFACE_FORMAT format);

    virtual bool                        SupportAllocator() const { return true; }
    virtual AMF_RESULT                  Flush();

    virtual AMF_RESULT                  Init(amf_int32 width, amf_int32 height, amf::AMFSurface* pSurface);
    virtual AMF_RESULT                  Terminate();

    // amf::AMFDataAllocatorCB interface
    virtual AMF_RESULT AMF_STD_CALL     AllocSurface(amf::AMF_MEMORY_TYPE type, amf::AMF_SURFACE_FORMAT format,
        amf_int32 width, amf_int32 height, amf_int32 hPitch, amf_int32 vPitch, amf::AMFSurface** ppSurface);
    // amf::AMFSurfaceObserver interface
    virtual void AMF_STD_CALL           OnSurfaceDataRelease(amf::AMFSurface* pSurface);

    virtual AMFSize                     GetSwapchainSize() { return m_swapChain.GetSize(); }

protected:

    struct SimpleVertex
    {
        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT2 texture;
    };

    typedef SwapChainDX9::BackBuffer RenderTarget;

    virtual AMF_RESULT                  CreateShaders(IDirect3DVertexShader9** ppVertexShader, IDirect3DPixelShader9** ppPixelShader);

    AMF_RESULT                          RenderSurface(amf::AMFSurface* pSurface, const RenderTarget* pRenderTarget);
    AMF_RESULT                          RenderSurface(amf::AMFSurface* pSrcSurface, const RenderTarget* pRenderTarget, const RenderViewSizeInfo& renderView);
    virtual AMF_RESULT                  DrawBackground(const RenderTarget* pRenderTarget);
    virtual AMF_RESULT                  SetStates();
    virtual AMF_RESULT                  DrawFrame(IDirect3DSurface9* pSrcSurface, const RenderTarget* pRenderTarget);
    virtual AMF_RESULT                  DrawFrame(IDirect3DTexture9* pSrcTexture, const RenderTarget* pRenderTarget);
    AMF_RESULT                          DropFrame();
    virtual AMF_RESULT                  DrawOverlay(amf::AMFSurface* /* pSurface */, const RenderTarget* /*pRenderTarget*/) { return AMF_OK; }

    AMF_RESULT                          UpdateVertexBuffer(IDirect3DVertexBuffer9* buffer, void* pData, size_t size);

    IDirect3DDevice9ExPtr               m_pDevice;
    SwapChainDX9                        m_swapChain;
private:
    AMF_RESULT                          CreateShaders();
    AMF_RESULT                          PrepareStates();

    AMF_RESULT                          CheckForResize(bool bForce);
    AMF_RESULT                          ResizeSwapChain();

    AMF_RESULT                          UpdateVertices(const AMFRect& srcRect, const AMFSize& srcSize, const AMFRect& dstRect, const AMFSize& dstSize, amf_float rotation);
    AMF_RESULT                          RenderScene(amf::AMFSurface* pSrcSurface, const RenderTarget* pRenderTarget);

    amf::AMFCriticalSection             m_sect;


    IDirect3DVertexShader9Ptr           m_pVertexShader;
    IDirect3DPixelShader9Ptr            m_pPixelShader;

    IDirect3DVertexBuffer9Ptr           m_pVertexBuffer;
    IDirect3DVertexDeclaration9Ptr      m_pVertexLayout;

    IDirect3DStateBlock9Ptr             m_pDepthStencilState;
    IDirect3DStateBlock9Ptr             m_pRasterizerState;
    IDirect3DStateBlock9Ptr             m_pBlendState;
    IDirect3DStateBlock9Ptr             m_pDefaultState;

    D3DVIEWPORT9                        m_currentViewport;

    amf::AMF_SURFACE_FORMAT             m_eInputFormat;

    amf_float                           m_fScale;
    amf_int                             m_iOffsetX;
    amf_int                             m_iOffsetY;
    amf_bool                            m_bUpdateVertices;

    DirectX::XMFLOAT4X4                 m_srcToClientMatrixInverse;
    
    std::vector<amf::AMFSurface*>       m_TrackSurfaces; // raw pointer  doent want keep references to ensure object is destroying
    amf_bool                            m_bResizeSwapChain;
};