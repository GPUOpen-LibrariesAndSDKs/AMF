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
#include "SwapChainDX12.h"


class VideoPresenterDX12 : public BackBufferPresenter, public SwapChainDX12
{
public:
    VideoPresenterDX12(amf_handle hwnd, amf::AMFContext* pContext);

    virtual                         ~VideoPresenterDX12();

    virtual AMF_RESULT              Present(amf::AMFSurface* pSurface);

    virtual bool                    SupportAllocator() const { return false; }

    virtual amf::AMF_MEMORY_TYPE    GetMemoryType() const { return amf::AMF_MEMORY_DX12; }
	virtual amf::AMF_SURFACE_FORMAT GetInputFormat() const{ return m_eInputFormat; }
    virtual AMF_RESULT              SetInputFormat(amf::AMF_SURFACE_FORMAT format);
    virtual AMF_RESULT              Flush();

    virtual AMF_RESULT              Init(amf_int32 width, amf_int32 height, amf::AMFSurface* pSurface);
    virtual AMF_RESULT              Terminate();

protected:
    virtual void                    UpdateProcessor();

private:
    AMF_RESULT                      CompileShaders();
	AMF_RESULT                      CreateCommandBuffer();
	AMF_RESULT                      ResetCommandBuffer();
	AMF_RESULT                      PrepareStates();
    AMF_RESULT                      PreparePIPStates();
	AMF_RESULT                      UpdateVertices(AMFRect *srcRect, AMFSize *srcSize, AMFRect *dstRect, AMFSize *dstSize);
    AMF_RESULT                      UpdatePIPVertices(AMFRect* srcRect, AMFSize* srcSize, AMFRect* dstRect, AMFSize* dstSize);
	AMF_RESULT                      CheckForResize(bool bForce, bool *bResized);
	AMF_RESULT                      ResizeSwapChain();
	AMF_RESULT                      BitBlt(amf::AMF_FRAME_TYPE eFrameType, ID3D12Resource* pSrcSurface, AMFRect* pSrcRect, ID3D12Resource* pDstSurface, AMFRect* pDstRect);
	AMF_RESULT                      BitBltRender(amf::AMF_FRAME_TYPE eFrameType, ID3D12Resource* pSrcSurface, AMFRect* pSrcRect, ID3D12Resource* pDstSurface, AMFRect* pDstRect);
    AMF_RESULT                      ApplyCSC(amf::AMFSurface* pSurface);
	
    amf::AMF_SURFACE_FORMAT         m_eInputFormat;
									 
	static const unsigned int       IndicesCount = 4;
    ATL::CComPtr<ID3D12Resource>    m_pVertexBuffer;
	ATL::CComPtr<ID3D12Resource>    m_pVertexBufferUpload;
    ATL::CComPtr<ID3D12Resource>    m_pCBChangesOnResize;

    ATL::CComPtr<ID3DBlob>          m_pVertexShader;
    ATL::CComPtr<ID3DBlob>          m_pPixelShader;
	
	D3D12_VERTEX_BUFFER_VIEW        m_vertexBufferView;
	D3D12_VIEWPORT                  m_CurrentViewport;

    const float                     c_pipSize = 0.3f;   // Relative size of the picture-in-picture window
    ATL::CComPtr<ID3D12Resource>    m_pPIPVertexBuffer;
    ATL::CComPtr<ID3D12Resource>    m_pPIPVertexBufferUpload;
    D3D12_VERTEX_BUFFER_VIEW        m_PIPVertexBufferView;

    float                           m_fScale;
    float                           m_fPixelAspectRatio;
    float                           m_fOffsetX;
    float                           m_fOffsetY;

    amf::AMFCriticalSection         m_sect;
    volatile UINT                   m_uiAvailableBackBuffer;
    UINT                            m_uiBackBufferCount;

    bool                            m_bResizeSwapChain;
	AMFRect                         m_rectClientResize;

	bool                            m_bFirstFrame;
	static const float              ClearColor[4];

};