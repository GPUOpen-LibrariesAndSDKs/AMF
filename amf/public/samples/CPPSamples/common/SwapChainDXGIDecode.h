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

#include <atlbase.h>
#include <dxgi.h>
#include <dcomp.h>

class SwapChainDXGIDecode : public SwapChainDXGI
{
public:
    SwapChainDXGIDecode(amf::AMFContext* pContext, amf::AMF_MEMORY_TYPE memoryType);
    virtual                             ~SwapChainDXGIDecode();

    virtual AMF_RESULT                  Init(amf_handle hwnd, amf_handle hDisplay, amf::AMFSurface* pSurface, amf_int32 width, amf_int32 height,
                                             amf::AMF_SURFACE_FORMAT format, amf_bool fullscreen = false, amf_bool hdr = false, amf_bool stereo = false) override;
    virtual AMF_RESULT                  Terminate() override;

    virtual AMF_RESULT                  Submit(amf::AMFSurface* pSurface);
    virtual AMF_RESULT                  Present(amf_bool waitForVSync) override;

    virtual amf_uint                    GetBackBufferCount() const override                                                     { return 0; }
    virtual amf_uint                    GetBackBuffersAvailable() const override                                                { return 0; }

    virtual AMF_RESULT                  GetBackBufferIndex(amf::AMFSurface* /*pSurface*/, amf_uint& /*index*/) const override   { return AMF_NOT_FOUND; }
    virtual AMF_RESULT                  GetBackBuffer(amf_uint /*index*/, amf::AMFSurface** /*ppSurface*/) const override       { return AMF_NOT_SUPPORTED; }
    virtual AMF_RESULT                  AcquireNextBackBufferIndex(amf_uint& /*index*/) override                                { return AMF_NOT_SUPPORTED; }
    virtual AMF_RESULT                  DropLastBackBuffer() override                                                           { return AMF_OK; }
    virtual AMF_RESULT                  DropBackBufferIndex(amf_uint /*index*/) override                                        { return AMF_OK; }

    // Leave format as AMF_SURFACE_UNKNOWN to keep format
    virtual AMF_RESULT                  Resize(amf_int32 width, amf_int32 height, amf_bool fullscreen, amf::AMF_SURFACE_FORMAT format=amf::AMF_SURFACE_UNKNOWN) override;
    virtual AMFSize                     GetSize() override { return m_size; }

    virtual amf_bool                    FormatSupported(amf::AMF_SURFACE_FORMAT format) override;
    virtual amf_bool                    HDRSupported() override                                                                 { return false; }
    virtual amf_bool                    StereoSupported()  override                                                             { return false; }

protected:
    AMF_RESULT                          CreateSwapChain(amf::AMFSurface* pSurface, amf_int32 width, amf_int32 height, amf::AMF_SURFACE_FORMAT format);
    AMF_RESULT                          TerminateSwapChain();
    
    virtual AMF_RESULT                  GetDXGIInterface(amf_bool reinit = false) override;
    AMF_RESULT                          GetDXGIDeviceAdapter(IDXGIAdapter** ppDXGIAdapter) override;

    virtual AMF_RESULT                  SetFormat(amf::AMF_SURFACE_FORMAT format) override;

    amf::AMF_MEMORY_TYPE                m_memoryType;

    CComPtr<IUnknown>                   m_pDevice;
    CComPtr<IDXGIFactoryMedia>          m_pDXGIFactoryMedia;
    DXGI_FORMAT                         m_dxgiFormat;

    CComPtr<IDXGIDecodeSwapChain>       m_pSwapChainDecode;
    amf_handle                          m_hDcompDll;
    HANDLE                              m_hDCompositionSurfaceHandle;

    CComPtr<IDCompositionDesktopDevice> m_pDCompDevice;
    CComPtr<IDCompositionTarget>        m_pDCompTarget;
    CComPtr<IDCompositionVisual2>       m_pVisualSurfaceRoot;
    CComPtr<IDXGIResource>              m_pDecodeTexture;
    CComPtr<IDCompositionScaleTransform> m_pScaleTransform;
    CComPtr<IDCompositionTransform>      m_pTransformGroup;

    amf::AMFSurfacePtr                  m_pSurface;
};
