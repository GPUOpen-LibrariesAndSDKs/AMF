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
// Copyright (C) 2023 Advanced Micro Devices, Inc. All rights reserved.
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

#include "MiscHelpers.h"

#include "public/include/components/VideoEncoderVCE.h"
#include "public/include/components/VideoEncoderHEVC.h"
#include "public/include/components/VideoEncoderAV1.h"
#include "public/common/TraceAdapter.h"

#ifdef _WIN32
#include <d3d9.h>
#include <d3d11.h>
#endif

#define AMF_FACILITY L"MiscHelpers"


// Waits till decoder finishes decode the surface. Need for accurate profiling only. Do not use in the product!!!
void SyncSurfaceToCPU(amf::AMFContext* pContext, amf::AMFSurface* pSurface)
{
    // copy of four pixels will force DX to wait for UVD decoder and will not add a significant delay
    amf::AMFSurfacePtr pOutputSurface;
    pContext->AllocSurface(pSurface->GetMemoryType(), pSurface->GetFormat(), 2, 2, &pOutputSurface); // NV12 must be divisible by 2

    switch (pSurface->GetMemoryType())
    {
#ifdef _WIN32        
    case amf::AMF_MEMORY_DX9:
    {
        HRESULT hr = S_OK;

        IDirect3DDevice9* pDeviceDX9 = (IDirect3DDevice9*)pContext->GetDX9Device(); // no reference counting - do not Release()
        IDirect3DSurface9* pSurfaceDX9src = (IDirect3DSurface9*)pSurface->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()
        IDirect3DSurface9* pSurfaceDX9dst = (IDirect3DSurface9*)pOutputSurface->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()
        RECT rect = { 0, 0, 2, 2 };
        // a-sync copy
        hr = pDeviceDX9->StretchRect(pSurfaceDX9src, &rect, pSurfaceDX9dst, &rect, D3DTEXF_NONE);
        // wait
        pOutputSurface->Convert(amf::AMF_MEMORY_HOST);
    }
    break;
    case amf::AMF_MEMORY_DX11:
    {
        ID3D11Device* pDeviceDX11 = (ID3D11Device*)pContext->GetDX11Device(); // no reference counting - do not Release()
        ID3D11Texture2D* pTextureDX11src = (ID3D11Texture2D*)pSurface->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()
        ID3D11Texture2D* pTextureDX11dst = (ID3D11Texture2D*)pOutputSurface->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()
        ID3D11DeviceContext* pContextDX11 = NULL;
        pDeviceDX11->GetImmediateContext(&pContextDX11);
        D3D11_BOX srcBox = { 0, 0, 0, 2, 2, 1 };
        pContextDX11->CopySubresourceRegion(pTextureDX11dst, 0, 0, 0, 0, pTextureDX11src, 0, &srcBox);
        pContextDX11->Flush();
        // release temp objects
        pContextDX11->Release();
        pOutputSurface->Convert(amf::AMF_MEMORY_HOST);
    }
    break;
#endif
    case amf::AMF_MEMORY_VULKAN:
    {
        // release temp objects
        pOutputSurface->Convert(amf::AMF_MEMORY_HOST);
    }
    break;
    case amf::AMF_MEMORY_DX12:
    {
        pOutputSurface->Convert(amf::AMF_MEMORY_HOST);
    }
    break;
    default:
        break;
    }
}

AMF_RESULT SetEncoderDefaultsAVC(amf::AMFComponent* pEncoder, amf_int64 bitRate, amf_int32 frameRate, bool bMaximumSpeed, bool bEnable4K, bool bLowLatency)
{
    AMF_RESULT res = AMF_OK;

    res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_TRANSCODING);
    AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_TRANSCODING) failed");

    if (bMaximumSpeed)
    {
        res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0);
        // do not check error for AMF_VIDEO_ENCODER_B_PIC_PATTERN - can be not supported - check Capability Manager sample
        res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED) failed");
    }

    res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, bitRate);
    AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, %" LPRId64 L") failed", bitRate);
    res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, ::AMFConstructRate(frameRate, 1));
    AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, %dx%d) failed", frameRate, 1);

    if (bEnable4K)
    {
        res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_PROFILE, AMF_VIDEO_ENCODER_PROFILE_HIGH);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_PROFILE, AMF_VIDEO_ENCODER_PROFILE_HIGH) failed");

        res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_PROFILE_LEVEL, 51);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_PROFILE_LEVEL, 51)");
        res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0); 
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0)");
    }

    if (bLowLatency)
    {
        res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_LOWLATENCY_MODE, true);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_LOWLATENCY_MODE, true) failed");
    }

    return AMF_OK;
}

AMF_RESULT SetEncoderDefaultsHEVC(amf::AMFComponent* pEncoder, amf_int64 bitRate, amf_int32 frameRate, bool bEnable10bit, bool bMaximumSpeed, bool bEnable4K, bool bLowLatency)
{
    AMF_RESULT res = AMF_OK;

    res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCODING);
    AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCODING)");

    if (bMaximumSpeed)
    {
        res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED)");
    }

    res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, bitRate);
    AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, %" LPRId64 L") failed", bitRate);
    res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, ::AMFConstructRate(frameRate, 1));
    AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, %dx%d) failed", frameRate, 1);

    if (bEnable10bit)
    {
        res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_COLOR_BIT_DEPTH, AMF_COLOR_BIT_DEPTH_10);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_COLOR_BIT_DEPTH, AMF_COLOR_BIT_DEPTH_10) failed");
    }

    if (bEnable4K)
    {
        res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_TIER, AMF_VIDEO_ENCODER_HEVC_TIER_HIGH);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_TIER, AMF_VIDEO_ENCODER_HEVC_TIER_HIGH) failed");
        res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_PROFILE_LEVEL, AMF_LEVEL_5_1);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_PROFILE_LEVEL, AMF_LEVEL_5_1) failed");
    }

    if (bLowLatency)
    {
        res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_LOWLATENCY_MODE, true);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_LOWLATENCY_MODE, true) failed");
    }

    return AMF_OK;
}

AMF_RESULT SetEncoderDefaultsAV1(amf::AMFComponent* pEncoder, amf_int64 bitRate, amf_int32 frameRate, bool bEnable10bit, bool bMaximumSpeed, bool bEnable4K, bool bLowLatency)
{
    AMF_RESULT res = AMF_OK;

    res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_USAGE, AMF_VIDEO_ENCODER_AV1_USAGE_TRANSCODING);
    AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_USAGE, AMF_VIDEO_ENCODER_AV1_USAGE_TRANSCODING)");

    if (bMaximumSpeed)
    {
        res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET, AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_SPEED);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET, AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_SPEED)");
    }

    res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_TARGET_BITRATE, bitRate);
    AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_TARGET_BITRATE, %" LPRId64 L") failed", bitRate);
    res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_FRAMERATE, ::AMFConstructRate(frameRate, 1));
    AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_FRAMERATE, %dx%d) failed", frameRate, 1);

    if (bEnable10bit)
    {
        res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_COLOR_BIT_DEPTH, AMF_COLOR_BIT_DEPTH_10);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_COLOR_BIT_DEPTH, AMF_COLOR_BIT_DEPTH_10) failed");
    }

    if (bEnable4K)
    {
        //res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_PROFILE, AMF_VIDEO_ENCODER_AV1_PROFILE_HIGH);
        //AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_PROFILE, AMF_VIDEO_ENCODER_AV1_PROFILE_HIGH) failed");
        res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_LEVEL, AMF_VIDEO_ENCODER_AV1_LEVEL_5_1);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_LEVEL, AMF_VIDEO_ENCODER_AV1_LEVEL_5_1) failed");
    }

    if (bLowLatency)
    {
        res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE, AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE_LOWEST_LATENCY);
        AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE, AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE_LOWEST_LATENCY) failed");
    }

    res = pEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE, AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE_NO_RESTRICTIONS);
    AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE, AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE_NO_RESTRICTIONS) failed");

    return AMF_OK;
}
