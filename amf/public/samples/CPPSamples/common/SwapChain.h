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

#include "public/include/core/Context.h"
#include <public/include/components/ColorSpace.h>
#include <public/common/Thread.h>
#include <memory>

#ifdef _WIN32
#include <public/common/Windows/UtilsWindows.h>
#endif

inline amf_bool OSHDRSupported()
{
#ifdef _WIN32
    
    // HDR added in Windows 10 version 1703 ("redstone 2"), build version 10.0.15063
    const static amf_bool supported = OSIsVersionOrGreater(10, 0, 15063);
    return supported;

#else
    return false; // No support on linux currently
#endif
}

struct BackBufferBase
{
    virtual ~BackBufferBase() {}

    virtual void* GetNative() const = 0;
    virtual amf::AMF_MEMORY_TYPE GetMemoryType() const = 0;
    virtual AMFSize GetSize() const = 0;

    virtual amf_bool operator==(const BackBufferBase& other) const
    {
        return GetNative() == other.GetNative();
    }
};

typedef std::unique_ptr<BackBufferBase> BackBufferBasePtr;

class SwapChain
{
public:
    virtual                             ~SwapChain();

    virtual AMF_RESULT                  Init(amf_handle hwnd, amf_handle hDisplay, amf::AMFSurface* pSurface, amf_int32 width, amf_int32 height,
                                             amf::AMF_SURFACE_FORMAT format, amf_bool fullscreen = false, amf_bool hdr = false, amf_bool stereo = false) = 0;

    virtual AMF_RESULT                  Terminate();

    virtual AMF_RESULT                  Present(amf_bool waitForVSync) = 0;

    virtual amf_uint                    GetBackBufferCount() const                                                      { return 0; } // Total back buffers in list (Not necessarily the number created)
    virtual amf_uint                    GetBackBuffersAcquireable() const                                               { return 0; } // Num Back buffers thats actually usuable at a time
    virtual amf_uint                    GetBackBuffersAvailable() const                                                 { return 0; } // Num back buffers left to acquire
    virtual amf_uint                    GetBackBuffersAcquired() const                                                  { return GetBackBuffersAcquireable() - GetBackBuffersAvailable(); }
    
    virtual AMF_RESULT                  GetBackBufferIndex(const BackBufferBase* pBuffer, amf_uint& index) const;
    virtual AMF_RESULT                  GetBackBufferIndex(amf::AMFSurface* pSurface, amf_uint& index) const;

    virtual AMF_RESULT                  GetBackBuffer(amf_uint index, const BackBufferBase** ppBuffer) const;
    virtual AMF_RESULT                  GetBackBuffer(amf_uint index, amf::AMFSurface** ppSurface) const;

    virtual AMF_RESULT                  AcquireNextBackBufferIndex(amf_uint& /*index*/)                                 { return AMF_NOT_IMPLEMENTED; }
    virtual AMF_RESULT                  AcquireNextBackBuffer(const BackBufferBase** ppBuffer);
    virtual AMF_RESULT                  AcquireNextBackBuffer(amf::AMFSurface** ppSurface);

    virtual AMF_RESULT                  DropLastBackBuffer()                                                            { return AMF_NOT_IMPLEMENTED; }
    virtual AMF_RESULT                  DropBackBufferIndex(amf_uint /*index*/)                                         { return AMF_NOT_IMPLEMENTED; }
    virtual AMF_RESULT                  DropBackBuffer(const BackBufferBase* pBuffer);
    virtual AMF_RESULT                  DropBackBuffer(amf::AMFSurface* pSurface);


    // Leave format as AMF_SURFACE_UNKNOWN to keep format
    // width
    virtual AMF_RESULT                  Resize(amf_int32 /*width*/, amf_int32 /*height*/, amf_bool /*fullscreen*/, 
                                               amf::AMF_SURFACE_FORMAT /*format*/=amf::AMF_SURFACE_UNKNOWN)             { return AMF_NOT_IMPLEMENTED; }
    virtual AMFSize                     GetSize()                                                                       { return m_size; }

    // Formats
    virtual amf_bool                    FormatSupported(amf::AMF_SURFACE_FORMAT /*format*/)                             { return false; }
    virtual amf::AMF_SURFACE_FORMAT     GetFormat() const                                                               { return m_format; }

    // Fullscreen
    virtual amf_bool                    GetExclusiveFullscreenState()                                                   { return false; }
    virtual AMF_RESULT                  SetExclusiveFullscreenState(amf_bool /*fullscreen*/)                            { return AMF_NOT_IMPLEMENTED; }

    // HDR
    virtual amf_bool                    HDRSupported()                                                                  { return false; }
    virtual AMF_RESULT                  EnableHDR(amf_bool enable);
    virtual amf_bool                    HDREnabled()                                                                    { return m_hdrEnabled; }
    virtual AMF_RESULT                  SetHDRMetaData(const AMFHDRMetadata* /*pHDRMetaData*/)                          { return AMF_NOT_IMPLEMENTED; }

    // Colorspace
    struct ColorSpace
    {
        AMF_COLOR_TRANSFER_CHARACTERISTIC_ENUM      transfer;
        AMF_COLOR_PRIMARIES_ENUM                    primaries;
        AMF_COLOR_RANGE_ENUM                        range;
    };

    virtual AMF_RESULT                  GetColorSpace(ColorSpace& colorSpace);
    virtual AMF_RESULT                  GetOutputHDRMetaData(AMFHDRMetadata& hdrMetaData);

    // Stereo 3D
    virtual amf_bool                    StereoSupported()                                                               { return false; }
    virtual amf_bool                    StereoEnabled()                                                                 { return m_stereoEnabled; }

    // To check if Init has been called
    amf_bool                            IsInitialized()                                                                 { return m_hwnd != nullptr; }

protected:
    SwapChain(amf::AMFContext* pContext);

    virtual AMF_RESULT                  SetFormat(amf::AMF_SURFACE_FORMAT /*format*/)                                   { return AMF_NOT_IMPLEMENTED; }

    // Should check for changes to the current output
    // and set the swapchain colorspace based on HDR Settings
    virtual AMF_RESULT                  UpdateCurrentOutput()                                                           { return AMF_NOT_IMPLEMENTED; }

    // Used to get back buffers from child classes, must be an array of length >= GetBackBufferCountTotal()
    virtual const BackBufferBasePtr*    GetBackBuffers()  const                                                         { return nullptr; }
    virtual AMF_RESULT                  BackBufferToSurface(const BackBufferBase* /*pBuffer*/, amf::AMFSurface** /*ppSurface*/) const { return AMF_NOT_SUPPORTED; }
    virtual void*                       GetSurfaceNative(amf::AMFSurface* pSurface) const;

    amf::AMFContextPtr                  m_pContext;
    amf::AMFCriticalSection             m_sync;

    amf_handle                          m_hwnd;
    amf_handle                          m_hDisplay;

    AMFSize                             m_size;
    amf::AMF_SURFACE_FORMAT             m_format;
    amf_bool                            m_hdrEnabled;
    AMFHDRMetadata                      m_outputHDRMetaData;
    ColorSpace                          m_colorSpace;
    amf_bool                            m_stereoEnabled;
};


template <typename Native_T>
Native_T* GetNativePackedSurface(amf::AMFSurface* pSurface, amf::AMF_MEMORY_TYPE memoryType)
{
    if (pSurface == nullptr)
    {
        //AMFTraceError("GetNativeSurface() - pSurface was NULL");
        return nullptr;
    }

    if (memoryType != amf::AMF_MEMORY_UNKNOWN)
    {
        AMF_RESULT res = pSurface->Convert(memoryType);
        if (res != AMF_OK)
        {
            //AMFTraceError("GetNativeSurface() - Convert(%s) failed", AMFGetMemoryTypeName(memoryType));
            return nullptr;
        }
    }

    amf::AMFPlane* pPlane = pSurface->GetPlane(amf::AMF_PLANE_PACKED);
    if (pPlane == nullptr)
    {
        return nullptr;
    }

    Native_T* pNativeSurface = (Native_T*)pPlane->GetNative();
    return pNativeSurface;
}

#define GetPackedSurfaceDX9(pSurface)       GetNativePackedSurface<IDirect3DSurface9>   (pSurface, amf::AMF_MEMORY_DX9)
#define GetPackedSurfaceDX11(pSurface)      GetNativePackedSurface<ID3D11Texture2D>     (pSurface, amf::AMF_MEMORY_DX11)
#define GetPackedSurfaceDX12(pSurface)      GetNativePackedSurface<ID3D12Resource>      (pSurface, amf::AMF_MEMORY_DX12)
#define GetPackedSurfaceVulkan(pSurface)    GetNativePackedSurface<amf::AMFVulkanView>  (pSurface, amf::AMF_MEMORY_VULKAN)
#define GetPackedSurfaceOpenGL(pSurface)    GetNativePackedSurface<void>                (pSurface, amf::AMF_MEMORY_OPENGL)

AMFRect GetClientRect(amf_handle hwnd, amf_handle hDisplay = nullptr);
