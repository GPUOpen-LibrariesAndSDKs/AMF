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

#include "SurfaceGenerator.h"
#include "AMFHalfFloat.h"


#ifdef _WIN32
#include <atlbase.h>
using namespace ATL;
#endif

#define AMF_FACILITY L"SurfaceGenerator"

extern amf_int32 rectSize;

extern amf::AMFSurfacePtr pColor1;
extern amf::AMFSurfacePtr pColor2;

// Colors used for frame injection
extern amf::AMFSurfacePtr pColor3;
extern amf::AMFSurfacePtr pColor4;

static amf_int32 xPos = 0;
static amf_int32 yPos = 0;


#ifdef _WIN32
AMF_RESULT FillSurfaceDX9(amf::AMFContext* pContext, amf::AMFSurface* pSurface, amf_bool isRefFrame)
{
    // fill surface with something something useful. We fill with color and color rect
    D3DCOLOR color1 = D3DCOLOR_XYUV(128, 255, 128);
    D3DCOLOR color2 = D3DCOLOR_XYUV(128, 0, 128);
    D3DCOLOR color3 = D3DCOLOR_XYUV(192, 64, 192);
    D3DCOLOR color4 = D3DCOLOR_XYUV(192, 0, 192);

    amf::AMFPlane* pPlane = pSurface->GetPlaneAt(0);
    AMF_RETURN_IF_INVALID_POINTER(pPlane);

    // get native DX objects
    IDirect3DDevice9* pDeviceDX9 = (IDirect3DDevice9*)pContext->GetDX9Device(); // no reference counting - do not Release()
    IDirect3DSurface9* pSurfaceDX9 = (IDirect3DSurface9*)pPlane->GetNative(); // no reference counting - do not Release()
    HRESULT hr = pDeviceDX9->ColorFill(pSurfaceDX9, NULL, (isRefFrame) ? color3 : color1);

    // get surface dimensions
    amf_int32 width = pPlane->GetWidth();
    amf_int32 height = pPlane->GetHeight();

    if (xPos + rectSize > width)
    {
        xPos = 0;
    }
    if (yPos + rectSize > height)
    {
        yPos = 0;
    }
    RECT rect = { xPos, yPos, xPos + rectSize, yPos + rectSize };
    hr = pDeviceDX9->ColorFill(pSurfaceDX9, &rect, (isRefFrame) ? color4 : color2);

    xPos += 2; //DX9 NV12 surfaces do not accept odd positions - do not use ++
    yPos += 2; //DX9 NV12 surfaces do not accept odd positions - do not use ++

    return AMF_OK;
}

AMF_RESULT FillSurfaceDX11(amf::AMFContext* pContext, amf::AMFSurface* pSurface, amf_bool isRefFrame)
{
    amf::AMFPlane* pPlane = pSurface->GetPlaneAt(0);
    AMF_RETURN_IF_INVALID_POINTER(pPlane);

    // get surface dimensions
    amf_int32 width = pPlane->GetWidth();
    amf_int32 height = pPlane->GetHeight();

    // fill surface with something something useful. We fill with color and color rect
    // get native DX objects
    ID3D11Device* pDeviceDX11 = (ID3D11Device*)pContext->GetDX11Device(); // no reference counting - do not Release()
    ID3D11Texture2D* pSurfaceDX11 = (ID3D11Texture2D*)pPlane->GetNative(); // no reference counting - do not Release()

    CComQIPtr<ID3D11DeviceContext> pDeviceContextDX11;
    pDeviceDX11->GetImmediateContext(&pDeviceContextDX11);

    pPlane = (isRefFrame) ? pColor3->GetPlaneAt(0) : pColor1->GetPlaneAt(0);
    AMF_RETURN_IF_INVALID_POINTER(pPlane);

    ID3D11Texture2D* pSurfaceDX11Color1 = (ID3D11Texture2D*)pPlane->GetNative(); // no reference counting - do not Release()
    pDeviceContextDX11->CopyResource(pSurfaceDX11, pSurfaceDX11Color1);

    if (xPos + rectSize > width)
    {
        xPos = 0;
    }
    if (yPos + rectSize > height)
    {
        yPos = 0;
    }
    D3D11_BOX rect = { 0, 0, 0, (UINT)rectSize, (UINT)rectSize, 1 };

    pPlane = (isRefFrame) ? pColor4->GetPlaneAt(0) : pColor2->GetPlaneAt(0);
    AMF_RETURN_IF_INVALID_POINTER(pPlane);

    ID3D11Texture2D* pSurfaceDX11Color2 = (ID3D11Texture2D*)pPlane->GetNative(); // no reference counting - do not Release()

    pDeviceContextDX11->CopySubresourceRegion(pSurfaceDX11, 0, xPos, yPos, 0, pSurfaceDX11Color2, 0, &rect);
    pDeviceContextDX11->Flush();

    xPos += 2; //DX9 NV12 surfaces do not accept odd positions - do not use ++
    yPos += 2; //DX9 NV12 surfaces do not accept odd positions - do not use ++

    return AMF_OK;
}
AMF_RESULT FillSurfaceDX12(amf::AMFContext* pContext, amf::AMFSurface* pSurface, amf_bool isRefFrame)
{
    AMF_RESULT res = AMF_OK;

    amf::AMFComputePtr pCompute;
    pContext->GetCompute(amf::AMF_MEMORY_DX12, &pCompute);
    AMF_RETURN_IF_INVALID_POINTER(pCompute);

    for (amf_uint32 i = 0; i < pSurface->GetPlanesCount(); i++)
    {
        amf::AMFPlane* pPlaneDst = pSurface->GetPlaneAt(i);
        AMF_RETURN_IF_INVALID_POINTER(pPlaneDst);

        amf_size origin[3] = { 0, 0, 0 };
        amf_size region[3] = { (amf_size)pPlaneDst->GetWidth(), (amf_size)pPlaneDst->GetHeight(), 1 };

        amf::AMFPlane* pPlaneSrc = (isRefFrame) ? pColor3->GetPlaneAt(i) : pColor1->GetPlaneAt(i);

        res = pCompute->CopyPlane(pPlaneSrc, origin, region, pPlaneDst, origin);
        AMF_RETURN_IF_FAILED(res, L"Background AMFCompute::CopyPlane() falied");

        pPlaneSrc = (isRefFrame) ? pColor4->GetPlaneAt(i) : pColor2->GetPlaneAt(i);

        //MM looks like there is a bug in D3D12 CopyTextureRegion copying UV-plane - copies W & H x2  compare to requested regionColor

        amf_int32 srcWidth = pPlaneSrc->GetWidth();
        amf_int32 srcHeight = pPlaneSrc->GetHeight();

        amf_size regionColor[3] = { (amf_size)srcWidth, (amf_size)srcHeight, 1 };
        amf_size originColor[3] = { (amf_size)xPos / (i + 1), (amf_size)yPos / (i + 1), 0 };

        res = pCompute->CopyPlane(pPlaneSrc, origin, regionColor, pSurface->GetPlaneAt(i), originColor);

        AMF_RETURN_IF_FAILED(res, L"Rectangle AMFCompute::CopyPlane() falied");

    }

    pCompute->FlushQueue();

    xPos += 2; //NV12 surfaces do not accept odd positions - do not use ++
    yPos += 2; //NV12 surfaces do not accept odd positions - do not use ++

    amf::AMFPlane* pPlaneDst = pSurface->GetPlaneAt(0);
    AMF_RETURN_IF_INVALID_POINTER(pPlaneDst);

    // get surface dimensions
    amf_int32 width = pPlaneDst->GetWidth();
    amf_int32 height = pPlaneDst->GetHeight();

    if (xPos + rectSize > width)
    {
        xPos = 0;
    }
    if (yPos + rectSize > height)
    {
        yPos = 0;
    }

    return AMF_OK;
}
#endif

AMF_RESULT FillNV12SurfaceWithColor(amf::AMFSurface* pSurface, amf_uint8 Y, amf_uint8 U, amf_uint8 V)
{
    amf::AMFPlane* pPlaneY = pSurface->GetPlane(amf::AMF_PLANE_Y);
    amf::AMFPlane* pPlaneUV = pSurface->GetPlane(amf::AMF_PLANE_UV);
    AMF_RETURN_IF_INVALID_POINTER(pPlaneY);
    AMF_RETURN_IF_INVALID_POINTER(pPlaneUV);

    amf_int32 widthY = pPlaneY->GetWidth();
    amf_int32 heightY = pPlaneY->GetHeight();
    amf_int32 lineY = pPlaneY->GetHPitch();

    amf_uint8* pDataY = (amf_uint8*)pPlaneY->GetNative();

    for (amf_int32 y = 0; y < heightY; y++)
    {
        amf_uint8* pDataLine = pDataY + y * lineY;
        memset(pDataLine, Y, widthY);
    }

    amf_int32 widthUV = pPlaneUV->GetWidth();
    amf_int32 heightUV = pPlaneUV->GetHeight();
    amf_int32 lineUV = pPlaneUV->GetHPitch();

    amf_uint8* pDataUV = (amf_uint8*)pPlaneUV->GetNative();

    for (amf_int32 y = 0; y < heightUV; y++)
    {
        amf_uint8* pDataLine = pDataUV + y * lineUV;
        for (amf_int32 x = 0; x < widthUV; x++)
        {
            *pDataLine++ = U;
            *pDataLine++ = V;
        }
    }

    return AMF_OK;
}

AMF_RESULT FillRGBASurfaceWithColor(amf::AMFSurface* pSurface, amf_uint8 R, amf_uint8 G, amf_uint8 B)
{
    amf::AMFPlane* pPlane = pSurface->GetPlaneAt(0);
    AMF_RETURN_IF_INVALID_POINTER(pPlane);

    amf_int32 width = pPlane->GetWidth();
    amf_int32 height = pPlane->GetHeight();
    amf_int32 line = pPlane->GetHPitch();

    amf_uint8* pData = (amf_uint8*)pPlane->GetNative();

    for (amf_int32 y = 0; y < height; y++)
    {
        amf_uint8* pDataLine = pData + y * line;
        for (amf_int32 x = 0; x < width; x++)
        {
            *pDataLine++ = R;
            *pDataLine++ = G;
            *pDataLine++ = B;
            *pDataLine++ = 255; //A
        }
    }

    return AMF_OK;
}

AMF_RESULT FillBGRASurfaceWithColor(amf::AMFSurface* pSurface, amf_uint8 R, amf_uint8 G, amf_uint8 B)
{
    amf::AMFPlane* pPlane = pSurface->GetPlaneAt(0);
    AMF_RETURN_IF_INVALID_POINTER(pPlane);

    amf_int32 width = pPlane->GetWidth();
    amf_int32 height = pPlane->GetHeight();
    amf_int32 line = pPlane->GetHPitch();

    amf_uint8* pData = (amf_uint8*)pPlane->GetNative();

    for (amf_int32 y = 0; y < height; y++)
    {
        amf_uint8* pDataLine = pData + y * line;
        for (amf_int32 x = 0; x < width; x++)
        {
            *pDataLine++ = B;
            *pDataLine++ = G;
            *pDataLine++ = R;
            *pDataLine++ = 255; //A
        }
    }

    return AMF_OK;
}

amf_uint16 AMFHalfFloat::m_basetable[512];
amf_uint8 AMFHalfFloat::m_shifttable[512];

AMF_RESULT FillRGBA_F16SurfaceWithColor(amf::AMFSurface* pSurface, amf_uint8 R, amf_uint8 G, amf_uint8 B)
{
    amf::AMFPlane* pPlane = pSurface->GetPlaneAt(0);
    AMF_RETURN_IF_INVALID_POINTER(pPlane);

    amf_int32 width = pPlane->GetWidth();
    amf_int32 height = pPlane->GetHeight();
    amf_int32 line = pPlane->GetHPitch();

    amf_uint8* pData = (amf_uint8*)pPlane->GetNative();

    amf_uint16 fR = AMFHalfFloat::ToHalfFloat((float)R / 255.f);
    amf_uint16 fG = AMFHalfFloat::ToHalfFloat((float)G / 255.f);
    amf_uint16 fB = AMFHalfFloat::ToHalfFloat((float)B / 255.f);
    amf_uint16 fA = AMFHalfFloat::ToHalfFloat((float)255.f / 255.f);

    for (amf_int32 y = 0; y < height; y++)
    {
        amf_uint16* pDataLine = (amf_uint16*)(pData + y * line);
        for (amf_int32 x = 0; x < width; x++)
        {
            *pDataLine++ = fR;
            *pDataLine++ = fG;
            *pDataLine++ = fB;
            *pDataLine++ = fA; //A
        }
    }

    return AMF_OK;
}

AMF_RESULT FillR10G10B10A2SurfaceWithColor(amf::AMFSurface* pSurface, amf_uint8 R, amf_uint8 G, amf_uint8 B)
{
    amf::AMFPlane* pPlane = pSurface->GetPlaneAt(0);
    AMF_RETURN_IF_INVALID_POINTER(pPlane);

    amf_int32 width = pPlane->GetWidth();
    amf_int32 height = pPlane->GetHeight();
    amf_int32 line = pPlane->GetHPitch();

    amf_uint8* pData = (amf_uint8*)pPlane->GetNative();

    amf_uint32 r10 = ((amf_uint32)R * 0x3FF / 0xFF) << 2;
    amf_uint32 g10 = ((amf_uint32)G * 0x3FF / 0xFF) << 12;
    amf_uint32 b10 = ((amf_uint32)B * 0x3FF / 0xFF) << 22;
    amf_uint32 a2 = 0x3;

    amf_uint32 color = r10 | g10 | b10 | a2;

    for (amf_int32 y = 0; y < height; y++)
    {
        amf_uint32* pDataLine = (amf_uint32*)(pData + y * line);
        for (amf_int32 x = 0; x < width; x++)
        {
            *pDataLine++ = color;
        }
    }

    return AMF_OK;
}

AMF_RESULT FillP010SurfaceWithColor(amf::AMFSurface* pSurface, amf_uint8 Y, amf_uint8 U, amf_uint8 V)
{
    amf::AMFPlane* pPlaneY = pSurface->GetPlane(amf::AMF_PLANE_Y);
    amf::AMFPlane* pPlaneUV = pSurface->GetPlane(amf::AMF_PLANE_UV);
    AMF_RETURN_IF_INVALID_POINTER(pPlaneY);
    AMF_RETURN_IF_INVALID_POINTER(pPlaneUV);

    amf_int32 widthY = pPlaneY->GetWidth();
    amf_int32 heightY = pPlaneY->GetHeight();
    amf_int32 lineY = pPlaneY->GetHPitch();

    amf_uint8* pDataY = (amf_uint8*)pPlaneY->GetNative();

    for (amf_int32 y = 0; y < heightY; y++)
    {
        amf_uint8* pDataLine = pDataY + y * lineY;
        memset(pDataLine, Y, widthY);
    }

    amf_int32 widthUV = pPlaneUV->GetWidth();
    amf_int32 heightUV = pPlaneUV->GetHeight();
    amf_int32 lineUV = pPlaneUV->GetHPitch();

    amf_uint8* pDataUV = (amf_uint8*)pPlaneUV->GetNative();

    for (amf_int32 y = 0; y < heightUV; y++)
    {
        amf_uint8* pDataLine = pDataUV + y * lineUV;
        for (amf_int32 x = 0; x < widthUV; x++)
        {
            *pDataLine++ = U;
            *pDataLine++ = V;
        }
    }

    return AMF_OK;
}

AMF_RESULT FillRGBA_RGBA16SurfaceWithColor(amf::AMFSurface* pSurface, amf_uint8 R, amf_uint8 G, amf_uint8 B)
{
    amf::AMFPlane* pPlane = pSurface->GetPlaneAt(0);
    AMF_RETURN_IF_INVALID_POINTER(pPlane);

    amf_int32 width = pPlane->GetWidth();
    amf_int32 height = pPlane->GetHeight();
    amf_int32 line = pPlane->GetHPitch();

    amf_uint8* pData = (amf_uint8*)pPlane->GetNative();

    amf_uint16 fR = R << 8;
    amf_uint16 fG = G << 8;
    amf_uint16 fB = B << 8;
    amf_uint16 fA = 255 << 8;

    for (amf_int32 y = 0; y < height; y++)
    {
        amf_uint16* pDataLine = (amf_uint16*)(pData + y * line);
        for (amf_int32 x = 0; x < width; x++)
        {
            *pDataLine++ = fR;
            *pDataLine++ = fG;
            *pDataLine++ = fB;
            *pDataLine++ = fA; //A
        }
    }

    return AMF_OK;
}

AMF_RESULT FillSurfaceVulkan(amf::AMFContext* pContext, amf::AMFSurface* pSurface, amf_bool isRefFrame)
{
    amf::AMFPlane* pPlane = pSurface->GetPlaneAt(0);
    AMF_RETURN_IF_INVALID_POINTER(pPlane);

    amf::AMFComputePtr pCompute;
    pContext->GetCompute(amf::AMF_MEMORY_VULKAN, &pCompute);

    // get surface dimensions
    amf_int32 width = pPlane->GetWidth();
    amf_int32 height = pPlane->GetHeight();

    if (xPos + rectSize > width)
    {
        xPos = 0;
    }
    if (yPos + rectSize > height)
    {
        yPos = 0;
    }

    for (amf_size p = 0; p < pColor1->GetPlanesCount(); p++)
    {
        pPlane = (isRefFrame) ? pColor3->GetPlaneAt(p) : pColor1->GetPlaneAt(p);
        AMF_RETURN_IF_INVALID_POINTER(pPlane);

        amf_size origin1[3] = { 0, 0 , 0 };
        amf_size region1[3] = { (amf_size)pPlane->GetWidth() , (amf_size)pPlane->GetHeight(), (amf_size)1 };
        pCompute->CopyPlane(pPlane, origin1, region1, pSurface->GetPlaneAt(p), origin1);

        pPlane = (isRefFrame) ? pColor4->GetPlaneAt(p) : pColor2->GetPlaneAt(p);
        AMF_RETURN_IF_INVALID_POINTER(pPlane);

        amf_size region2[3] = { (amf_size)pPlane->GetWidth(), (amf_size)pPlane->GetHeight(), (amf_size)1 };
        amf_size origin2[3] = { (amf_size)xPos / (p + 1), (amf_size)yPos / (p + 1) ,     0 };

        pCompute->CopyPlane(pPlane, origin1, region2, pSurface->GetPlaneAt(p), origin2);

    }
    xPos += 2; // NV12 surfaces do not accept odd positions - do not use ++
    yPos += 2; // NV12 surfaces do not accept odd positions - do not use ++

    return AMF_OK;
}


void PrepareFillFromHost(amf::AMFContext* pContext, amf::AMF_MEMORY_TYPE memoryType, amf::AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height, amf_bool isInject)
{
    AMF_RESULT res = AMF_OK; // error checking can be added later
    res = pContext->AllocSurface(amf::AMF_MEMORY_HOST, format, width, height, &pColor1);
    res = pContext->AllocSurface(amf::AMF_MEMORY_HOST, format, rectSize, rectSize, &pColor2);
    if (isInject)
    {
        res = pContext->AllocSurface(amf::AMF_MEMORY_HOST, format, width, height, &pColor3);
        res = pContext->AllocSurface(amf::AMF_MEMORY_HOST, format, rectSize, rectSize, &pColor4);
    }

    switch (format)
    {
    case amf::AMF_SURFACE_NV12:
        FillNV12SurfaceWithColor(pColor2, 128, 0, 128);
        FillNV12SurfaceWithColor(pColor1, 128, 255, 128);
        if (isInject)
        {
            FillNV12SurfaceWithColor(pColor4, 0, 128, 128); // black color
            FillNV12SurfaceWithColor(pColor3, 0, 128, 128); // black color
        }
        break;
    case amf::AMF_SURFACE_RGBA:
        FillRGBASurfaceWithColor(pColor2, 255, 0, 0);
        FillRGBASurfaceWithColor(pColor1, 0, 0, 255);
        if (isInject)
        {
            FillRGBASurfaceWithColor(pColor4, 128, 0, 0);
            FillRGBASurfaceWithColor(pColor3, 0, 0, 128);
        }
        break;
    case amf::AMF_SURFACE_BGRA:
        FillBGRASurfaceWithColor(pColor2, 255, 0, 0);
        FillBGRASurfaceWithColor(pColor1, 0, 0, 255);
        if (isInject)
        {
            FillBGRASurfaceWithColor(pColor4, 128, 0, 0);
            FillBGRASurfaceWithColor(pColor3, 0, 0, 128);
        }
        break;
    case amf::AMF_SURFACE_R10G10B10A2:
        FillR10G10B10A2SurfaceWithColor(pColor2, 255, 0, 0);
        FillR10G10B10A2SurfaceWithColor(pColor1, 0, 0, 255);
        if (isInject)
        {
            FillR10G10B10A2SurfaceWithColor(pColor4, 128, 0, 0);
            FillR10G10B10A2SurfaceWithColor(pColor3, 0, 0, 128);
        }
        break;
    case amf::AMF_SURFACE_RGBA_F16:
        FillRGBA_F16SurfaceWithColor(pColor2, 255, 0, 0);
        FillRGBA_F16SurfaceWithColor(pColor1, 0, 0, 255);
        if (isInject)
        {
            FillRGBA_F16SurfaceWithColor(pColor4, 128, 0, 0);
            FillRGBA_F16SurfaceWithColor(pColor3, 0, 0, 128);
        }
        break;
    case amf::AMF_SURFACE_P010:
        FillP010SurfaceWithColor(pColor2, 255, 0, 0);
        FillP010SurfaceWithColor(pColor1, 0, 0, 255);
        break;
    default:
        break;
    }
    pColor1->Convert(memoryType);
    pColor2->Convert(memoryType);
    if (isInject)
    {
        pColor3->Convert(memoryType);
        pColor4->Convert(memoryType);
    }
}

AMF_RESULT FillSurface(amf::AMFContextPtr pContext, amf::AMFSurface** ppSurfaceIn, amf::AMF_MEMORY_TYPE memoryType, amf::AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height, bool bWait)
{
    if (*ppSurfaceIn == NULL)
    {
        AMF_RESULT res = pContext->AllocSurface(memoryType, format, width, height, ppSurfaceIn);
        AMF_RETURN_IF_FAILED(res, L"AllocSurface() failed");

        if (memoryType == amf::AMF_MEMORY_VULKAN)
        {
            FillSurfaceVulkan(pContext, *ppSurfaceIn, false);
        }
#ifdef _WIN32
        else if (memoryType == amf::AMF_MEMORY_DX9)
        {
            FillSurfaceDX9(pContext, *ppSurfaceIn, false);
        }
        else if (memoryType == amf::AMF_MEMORY_DX11)
        {
            FillSurfaceDX11(pContext, *ppSurfaceIn, false);
        }
        else if (memoryType == amf::AMF_MEMORY_DX12)
        {
            FillSurfaceDX12(pContext, *ppSurfaceIn, false);
        }
#endif
        if (memoryType != amf::AMF_MEMORY_DX9)
        {
            amf::AMFComputePtr pCompute;
            pContext->GetCompute(memoryType, &pCompute);
            if (pCompute != nullptr)
            {
                pCompute->FlushQueue();
                if (bWait)
                {
                    pCompute->FinishQueue();
                }
            }
        }
    }

    return AMF_OK;
}

AMF_RESULT ReadSurface(PipelineElementPtr pPipelineElPtr, amf::AMFSurface** ppSurface, amf::AMF_MEMORY_TYPE memoryType)
{
    AMF_RESULT res = pPipelineElPtr->QueryOutput((amf::AMFData**)ppSurface);
    if (res == AMF_EOF)
    {
        return res;
    }
    AMF_RETURN_IF_FAILED(res);
    return (ppSurface && *ppSurface) ? (*ppSurface)->Convert(memoryType) : AMF_FAIL;
}