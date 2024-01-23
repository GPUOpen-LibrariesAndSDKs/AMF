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

#include "SwapChain.h"
#include "public/include/core/Surface.h"

#include <atlbase.h>
#include <dxgi1_4.h>
#include <DXGI1_6.h>

class SwapChainDXGI : public SwapChain
{
public:
    virtual                                 ~SwapChainDXGI();

    virtual AMF_RESULT                      Init(amf_handle hwnd, amf_handle hDisplay, amf::AMFSurface* pSurface, amf_int32 width, amf_int32 height,
                                                 amf::AMF_SURFACE_FORMAT format, amf_bool fullscreen=false, amf_bool hdr=false, amf_bool stereo=false) override;

    virtual AMF_RESULT                      Terminate() override;

    virtual AMF_RESULT                      Resize(amf_int32 width, amf_int32 height, amf_bool fullscreen, amf::AMF_SURFACE_FORMAT format=amf::AMF_SURFACE_UNKNOWN) override;
    virtual AMFSize                         GetSize() override;

    // Formats
    virtual amf_bool                        FormatSupported(amf::AMF_SURFACE_FORMAT format) override;
    virtual DXGI_FORMAT                     GetDXGIFormat() const { return m_dxgiFormat; }

    // HDR
    virtual amf_bool                        HDRSupported() override;
    virtual AMF_RESULT                      SetHDRMetaData(const AMFHDRMetadata* pHDRMetaData) override;

    virtual amf_bool                        StereoSupported() override;

protected:
    SwapChainDXGI(amf::AMFContext* pContext);

    virtual AMF_RESULT                      CreateLegacySwapChain(IUnknown* pDevice, amf_int32 width, amf_int32 height, amf_uint bufferCount);
    virtual AMF_RESULT                      CreateSwapChainForHwnd(IUnknown* pDevice, amf_int32 width, amf_int32 height, amf_uint bufferCount, amf_bool stereo);
    virtual AMF_RESULT                      CreateSwapChain(IUnknown* pDevice, amf_int32 width, amf_int32 height, amf::AMF_SURFACE_FORMAT format, amf_uint bufferCount, amf_bool fullscreen, amf_bool hdr, amf_bool stereo);

    virtual AMF_RESULT                      Present(amf_bool waitForVSync) override;

    virtual AMF_RESULT                      GetDXGIInterface(amf_bool reinit=false) = 0;
    virtual AMF_RESULT                      GetDXGIDeviceAdapter(IDXGIAdapter** ppDXGIAdapter) = 0;
    virtual AMF_RESULT                      GetDXGIAdapters();

    virtual AMF_RESULT                      SetFormat(amf::AMF_SURFACE_FORMAT format) override;
    virtual DXGI_FORMAT                     GetSupportedDXGIFormat(amf::AMF_SURFACE_FORMAT format) const;
    amf_bool                                GetExclusiveFullscreenState() override;
    AMF_RESULT                              SetExclusiveFullscreenState(amf_bool fullscreen) override;

    virtual AMF_RESULT                      UpdateOutputs();
    virtual AMF_RESULT                      UpdateCurrentOutput() override;
    virtual AMFRect                         GetOutputRect();

    virtual AMF_RESULT                      UpdateColorSpace();

    // Store the most commonly used DXGI interfaces so we don't have to query everytime
    ATL::CComPtr<IDXGIDevice>               m_pDXGIDevice;
    amf::amf_vector<ATL::CComPtr<IDXGIAdapter>> m_pDXGIAdapters;     
    ATL::CComPtr<IDXGIFactory>              m_pDXGIFactory;     // Legacy swapchain
    ATL::CComPtr<IDXGIFactory2>             m_pDXGIFactory2;    // swapchain for hwnd 

    ATL::CComPtr<IDXGISwapChain>            m_pSwapChain;       // Legacy swapchain
    ATL::CComPtr<IDXGISwapChain1>           m_pSwapChain1;      // Swapchain creation
    ATL::CComPtr<IDXGISwapChain3>           m_pSwapChain3;      // For setting colorspace

    amf::amf_vector<ATL::CComPtr<IDXGIOutput>>  m_pOutputs;
    ATL::CComPtr<IDXGIOutput>               m_pCurrentOutput;
    ATL::CComPtr<IDXGIOutput6>              m_pCurrentOutput6;

    DXGI_FORMAT                             m_dxgiFormat;

    amf_bool                                m_currentHDREnableState;
};

AMF_RESULT DXGIToAMFColorSpaceType(DXGI_COLOR_SPACE_TYPE colorSpace, SwapChain::ColorSpace& amfColorSpace);
const wchar_t* GetDXGIColorSpaceName(DXGI_COLOR_SPACE_TYPE colorSpace);
