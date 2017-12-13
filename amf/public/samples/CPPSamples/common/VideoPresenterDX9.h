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
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
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

#include <comdef.h>
#include <d3d9.h>

class VideoPresenterDX9 : public BackBufferPresenter
{
public:
    VideoPresenterDX9(HWND hwnd, amf::AMFContext* pContext);
    virtual ~VideoPresenterDX9();

    virtual AMF_RESULT Present(amf::AMFSurface* pSurface);
    virtual amf::AMF_MEMORY_TYPE GetMemoryType() const { return amf::AMF_MEMORY_DX9; }
    virtual amf::AMF_SURFACE_FORMAT GetInputFormat() const { return amf::AMF_SURFACE_BGRA; }
    virtual AMF_RESULT              SetInputFormat(amf::AMF_SURFACE_FORMAT format)
    {
        return format == amf::AMF_SURFACE_BGRA ? AMF_OK : AMF_FAIL;
    }
    virtual AMF_RESULT              Flush();

    virtual AMF_RESULT Init(amf_int32 width, amf_int32 height);
    virtual AMF_RESULT Terminate();

    // amf::AMFDataAllocatorCB interface
    virtual AMF_RESULT AMF_STD_CALL AllocSurface(amf::AMF_MEMORY_TYPE type, amf::AMF_SURFACE_FORMAT format,
            amf_int32 width, amf_int32 height, amf_int32 hPitch, amf_int32 vPitch, amf::AMFSurface** ppSurface);
    // amf::AMFSurfaceObserver interface
    virtual void AMF_STD_CALL OnSurfaceDataRelease(amf::AMFSurface* pSurface);

private:
    AMF_RESULT BitBlt(IDirect3DSurface9* pSrcSurface, AMFRect* pSrcRect, IDirect3DSurface9* pDstSurface, AMFRect* pDstRect);

    AMF_RESULT CreatePresentationSwapChain();


    IDirect3DDevice9ExPtr       m_pDevice;
    IDirect3DSwapChain9ExPtr    m_pSwapChain;

    amf::AMFCriticalSection          m_sect;
    volatile UINT               m_uiAvailableBackBuffer;
    UINT                        m_uiBackBufferCount;
    std::vector<amf::AMFSurface*>    m_TrackSurfaces; // raw pointer  doent want keep references to ensure object is destroying
    bool                        m_bResizeSwapChain;
};