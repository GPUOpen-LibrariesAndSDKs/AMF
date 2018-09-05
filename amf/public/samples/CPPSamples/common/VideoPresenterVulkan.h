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
#include "SwapChainVulkan.h"


class VideoPresenterVulkan : public BackBufferPresenter, public SwapChainVulkan

{
public:
#if defined(METRO_APP)
    VideoPresenterVulkan(ISwapChainBackgroundPanelNative* pSwapChainPanel, AMFSize swapChainPanelSize, amf::AMFContext* pContext);
#else
    VideoPresenterVulkan(amf_handle hwnd, amf::AMFContext* pContext, amf_handle display);
#endif
    virtual ~VideoPresenterVulkan();

    virtual AMF_RESULT Present(amf::AMFSurface* pSurface);

    virtual bool                SupportAllocator() const { return true; }

    virtual amf::AMF_MEMORY_TYPE GetMemoryType() const { return amf::AMF_MEMORY_VULKAN; }
    virtual amf::AMF_SURFACE_FORMAT GetInputFormat() const{ return m_eInputFormat; }
    virtual AMF_RESULT              SetInputFormat(amf::AMF_SURFACE_FORMAT format);
    virtual AMF_RESULT              Flush();

    virtual AMF_RESULT Init(amf_int32 width, amf_int32 height);
    virtual AMF_RESULT Terminate();

    // amf::AMFDataAllocatorCB interface
    virtual AMF_RESULT AMF_STD_CALL AllocSurface(amf::AMF_MEMORY_TYPE type, amf::AMF_SURFACE_FORMAT format,
            amf_int32 width, amf_int32 height, amf_int32 hPitch, amf_int32 vPitch, amf::AMFSurface** ppSurface);
    // amf::AMFSurfaceObserver interface
    virtual void AMF_STD_CALL OnSurfaceDataRelease(amf::AMFSurface* pSurface);

    virtual void AMF_STD_CALL  CheckForResize(); // call from UI thread (for VulkanPresenter on Linux)

private:
    AMF_RESULT CreateDescriptorSetLayout();
    AMF_RESULT CreatePipeline();
    AMF_RESULT CreateCommandBuffer();
    AMF_RESULT CreateVertexBuffers();
    AMF_RESULT CreateDescriptorSetPool();
    AMF_RESULT ResetCommandBuffer();
    AMF_RESULT RecordCommandBuffer(amf_uint32 imageIndex);
    AMF_RESULT UpdateTextureDescriptorSet(amf::AMFVulkanView* pView);

    AMF_RESULT UpdateVertices(AMFRect *srcRect, AMFSize *srcSize, AMFRect *dstRect, AMFSize *dstSize);
    AMF_RESULT CheckForResize(bool bForce, bool *bResized);
    AMF_RESULT ResizeSwapChain();

    AMF_RESULT BitBlt(amf::AMFSurface* pSurface, AMFRect *srcRect, amf_uint32 imageIndex,AMFRect *outputRect);
    AMF_RESULT BitBltRender(amf::AMFSurface* pSurface, AMFRect *srcRect, amf_uint32 imageIndex,AMFRect *outputRect);
    AMF_RESULT BitBltCopy(amf::AMFSurface* pSurface, AMFRect *srcRect, amf_uint32 imageIndex,AMFRect *outputRect);

    amf::AMF_SURFACE_FORMAT             m_eInputFormat;

    VkCommandBuffer                 m_hCommandBuffer;
    VkFence                         m_hCommandBufferFence;

    VkDescriptorSetLayout            m_hDescriptorSetLayout;
    VkPipelineLayout                m_hPipelineLayout;
    VkPipeline                        m_hPipeline;
    VkSampler                       m_hSampler;


    amf::AMFVulkanBuffer            m_VertexBuffer;
    amf::AMFVulkanBuffer            m_IndexBuffer;
    amf::AMFVulkanBuffer            m_MVPBuffer;

    VkDescriptorSet                    m_hDescriptorSet;
    VkDescriptorPool                m_hDescriptorPool;

    /*
    AMF_RESULT BitBlt(amf::AMF_FRAME_TYPE eFrameType, ID3D11Texture2D* pSrcSurface, AMFRect* pSrcRect, ID3D11Texture2D* pDstSurface, AMFRect* pDstRect);
    AMF_RESULT BitBltRender(amf::AMF_FRAME_TYPE eFrameType, ID3D11Texture2D* pSrcSurface, AMFRect* pSrcRect, ID3D11Texture2D* pDstSurface, AMFRect* pDstRect);
    AMF_RESULT BitBltCopy(ID3D11Texture2D* pSrcSurface, AMFRect* pSrcRect, ID3D11Texture2D* pDstSurface, AMFRect* pDstRect);
    AMF_RESULT CompileShaders();
    AMF_RESULT PrepareStates();
    AMF_RESULT CheckForResize(bool bForce, bool *bResized);
    AMF_RESULT ResizeSwapChain();
    AMF_RESULT UpdateVertices(AMFRect srcRect, AMFSize srcSize, AMFRect dstRect);
    AMF_RESULT DrawFrame(ID3D11Texture2D* pSrcSurface, bool bLeft);
    AMF_RESULT CopySurface(amf::AMF_FRAME_TYPE eFrameType, ID3D11Texture2D* pSrcSurface, AMFRect* pSrcRect);

    AMF_RESULT CreatePresentationSwapChain();

    CComPtr<ID3D11Device>               m_pDevice;
    CComQIPtr<IDXGISwapChain>           m_pSwapChain;
    CComPtr<IDXGISwapChain1>            m_pSwapChain1;
    bool                                m_stereo;
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

    CComPtr<ID3D11RenderTargetView>     m_pRenderTargetView_L;
    CComPtr<ID3D11RenderTargetView>     m_pRenderTargetView_R;
#if defined(METRO_APP)
    CComPtr<ISwapChainBackgroundPanelNative> m_pSwapChainPanel;
    AMFSize                                     m_swapChainPanelSize;
#endif
    */

    float                               m_fScale;
    float                               m_fPixelAspectRatio;
    float                               m_fOffsetX;
    float                               m_fOffsetY;


    amf::AMFCriticalSection          m_sect;
    volatile amf_uint32              m_uiAvailableBackBuffer;
    std::vector<amf::AMFSurface*>    m_TrackSurfaces; // raw pointer  doent want keep references to ensure object is destroying

    bool                            m_bResizeSwapChain;
    AMFRect                         m_rectClientResize;
};