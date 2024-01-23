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

#pragma once

#include "SwapChainDXGI.h"
#include <d3d11.h>


struct BackBufferDX11 : BackBufferBase
{
    ATL::CComPtr<ID3D11Texture2D>        pBuffer;
    ATL::CComPtr<ID3D11RenderTargetView> pRTV_L;
    ATL::CComPtr<ID3D11RenderTargetView> pRTV_R;

    virtual void*                   GetNative()                 const override { return pBuffer; }
    virtual amf::AMF_MEMORY_TYPE    GetMemoryType()             const override { return amf::AMF_MEMORY_DX11; }
    virtual AMFSize                 GetSize()                   const override;
};

class SwapChainDX11 : public SwapChainDXGI
{
public:
    typedef BackBufferDX11 BackBuffer;

    SwapChainDX11(amf::AMFContext* pContext);
    virtual                             ~SwapChainDX11();

    virtual AMF_RESULT                  Init(amf_handle hwnd, amf_handle hDisplay, amf::AMFSurface* pSurface, amf_int32 width, amf_int32 height,
                                             amf::AMF_SURFACE_FORMAT format, amf_bool fullscreen = false, amf_bool hdr = false, amf_bool stereo = false) override;

    virtual AMF_RESULT                  Terminate() override;

    virtual AMF_RESULT                  Present(amf_bool waitForVSync) override;

    virtual amf_uint                    GetBackBufferCount() const override             { return 1; }
    virtual amf_uint                    GetBackBuffersAcquireable() const override      { return 1; }
    virtual amf_uint                    GetBackBuffersAvailable()   const override      { return m_acquired ? 0 : 1; }

    virtual AMF_RESULT                  AcquireNextBackBufferIndex(amf_uint& index) override;
    virtual AMF_RESULT                  DropLastBackBuffer() override;
    virtual AMF_RESULT                  DropBackBufferIndex(amf_uint index) override;

    // Leave format as AMF_SURFACE_UNKNOWN to keep format
    // width
    virtual AMF_RESULT                  Resize(amf_int32 width, amf_int32 height, amf_bool fullscreen, 
                                               amf::AMF_SURFACE_FORMAT format=amf::AMF_SURFACE_UNKNOWN) override;

    static constexpr amf_uint BACK_BUFFER_COUNT = 4;

protected:
    AMF_RESULT                          GetDXGIInterface(amf_bool reinit = false) override;
    AMF_RESULT                          GetDXGIDeviceAdapter(IDXGIAdapter** ppDXGIAdapter) override;

    AMF_RESULT                          SetupSwapChain(amf_int32 width, amf_int32 height, amf::AMF_SURFACE_FORMAT format, amf_bool fullscreen, amf_bool hdr, amf_bool stereo);
    AMF_RESULT                          CreateFrameBuffers();
    AMF_RESULT                          DeleteFrameBuffers();

    virtual const BackBufferBasePtr*    GetBackBuffers() const override { return m_pSwapChain != nullptr ? &m_pBackBuffer : nullptr; }
    virtual AMF_RESULT                  BackBufferToSurface(const BackBufferBase* pBuffer, amf::AMFSurface** ppSurface) const override;


    ATL::CComPtr<ID3D11Device>          m_pDX11Device;
    BackBufferBasePtr                   m_pBackBuffer;
    amf_bool                            m_acquired;
};
