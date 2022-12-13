#include "SurfaceGenerator.h"


#ifdef _WIN32
#include <atlbase.h>
using namespace ATL;
#endif

extern amf::AMF_MEMORY_TYPE memoryTypeIn;
extern amf::AMF_SURFACE_FORMAT formatIn;
extern amf_int32 widthIn;
extern amf_int32 heightIn;
extern amf_int32 rectSize;

extern amf::AMFSurfacePtr pColor1;
extern amf::AMFSurfacePtr pColor2;

static amf_int32 xPos = 0;
static amf_int32 yPos = 0;


#ifdef _WIN32
void FillSurfaceDX9(amf::AMFContext* context, amf::AMFSurface* surface)
{
    // fill surface with something something useful. We fill with color and color rect
    D3DCOLOR color1 = D3DCOLOR_XYUV(128, 255, 128);
    D3DCOLOR color2 = D3DCOLOR_XYUV(128, 0, 128);
    // get native DX objects
    IDirect3DDevice9* deviceDX9 = (IDirect3DDevice9*)context->GetDX9Device(); // no reference counting - do not Release()
    IDirect3DSurface9* surfaceDX9 = (IDirect3DSurface9*)surface->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()
    HRESULT hr = deviceDX9->ColorFill(surfaceDX9, NULL, color1);

    if (xPos + rectSize > widthIn)
    {
        xPos = 0;
    }
    if (yPos + rectSize > heightIn)
    {
        yPos = 0;
    }
    RECT rect = { xPos, yPos, xPos + rectSize, yPos + rectSize };
    hr = deviceDX9->ColorFill(surfaceDX9, &rect, color2);

    xPos += 2; //DX9 NV12 surfaces do not accept odd positions - do not use ++
    yPos += 2; //DX9 NV12 surfaces do not accept odd positions - do not use ++
}

void FillSurfaceDX11(amf::AMFContext* context, amf::AMFSurface* surface)
{
    // fill surface with something something useful. We fill with color and color rect
    // get native DX objects
    ID3D11Device* deviceDX11 = (ID3D11Device*)context->GetDX11Device(); // no reference counting - do not Release()
    ID3D11Texture2D* surfaceDX11 = (ID3D11Texture2D*)surface->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()

    CComQIPtr<ID3D11DeviceContext> deviceContextDX11;
    deviceDX11->GetImmediateContext(&deviceContextDX11);

    ID3D11Texture2D* surfaceDX11Color1 = (ID3D11Texture2D*)pColor1->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()
    deviceContextDX11->CopyResource(surfaceDX11, surfaceDX11Color1);

    if (xPos + rectSize > widthIn)
    {
        xPos = 0;
    }
    if (yPos + rectSize > heightIn)
    {
        yPos = 0;
    }
    D3D11_BOX rect = { 0, 0, 0, (UINT)rectSize, (UINT)rectSize, 1 };

    ID3D11Texture2D* surfaceDX11Color2 = (ID3D11Texture2D*)pColor2->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()

    deviceContextDX11->CopySubresourceRegion(surfaceDX11, 0, xPos, yPos, 0, surfaceDX11Color2, 0, &rect);

    xPos += 2; //DX9 NV12 surfaces do not accept odd positions - do not use ++
    yPos += 2; //DX9 NV12 surfaces do not accept odd positions - do not use ++
}
#endif

void FillNV12SurfaceWithColor(amf::AMFSurface* surface, amf_uint8 Y, amf_uint8 U, amf_uint8 V)
{
    amf::AMFPlane* pPlaneY = surface->GetPlaneAt(0);
    amf::AMFPlane* pPlaneUV = surface->GetPlaneAt(1);

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
}

void FillRGBASurfaceWithColor(amf::AMFSurface* surface, amf_uint8 R, amf_uint8 G, amf_uint8 B)
{
    amf::AMFPlane* pPlane = surface->GetPlaneAt(0);

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
}

void FillBGRASurfaceWithColor(amf::AMFSurface* surface, amf_uint8 R, amf_uint8 G, amf_uint8 B)
{
    amf::AMFPlane* pPlane = surface->GetPlaneAt(0);

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
}

class AMFHalfFloat
{
public:
    AMFHalfFloat();
    AMF_FORCEINLINE static amf_uint16 ToHalfFloat(amf_float value)
    {
        union FloatBits
        {
            amf_float f;
            amf_uint32 u;
        };

        FloatBits val;
        val.f = value;

        return amf_uint16(m_basetable[(val.u >> 23) & 0x1ff] + ((val.u & 0x007fffff) >> m_shifttable[(val.u >> 23) & 0x1ff]));
    }

    AMF_FORCEINLINE static float FromHalfFloat(amf_uint16 value)
    {
        uint32_t mantissa = (uint32_t)(value & 0x03FF);

        uint32_t exponent = (value & 0x7C00);
        if (exponent == 0x7C00) // INF/NAN
        {
            exponent = (uint32_t)0x8f;
        }
        else if (exponent != 0)  // The value is normalized
        {
            exponent = (uint32_t)((value >> 10) & 0x1F);
        }
        else if (mantissa != 0)     // The value is denormalized
        {
            // Normalize the value in the resulting float
            exponent = 1;

            do
            {
                exponent--;
                mantissa <<= 1;
            } while ((mantissa & 0x0400) == 0);

            mantissa &= 0x03FF;
        }
        else                        // The value is zero
        {
            exponent = (uint32_t)-112;
        }

        uint32_t result = ((value & 0x8000) << 16) | // Sign
            ((exponent + 112) << 23) | // exponent
            (mantissa << 13);          // mantissa

        return reinterpret_cast<float*>(&result)[0];
    }
private:

    static amf_uint16 m_basetable[512];
    static amf_uint8 m_shifttable[512];

    static void GenerateHalfFloatConversionTables();

};
AMFHalfFloat::AMFHalfFloat()
{
    GenerateHalfFloatConversionTables();
}

static AMFHalfFloat s_InitHalfFLoat;
void AMFHalfFloat::GenerateHalfFloatConversionTables()
{
    for (unsigned int i = 0; i < 256; i++)
    {
        int e = i - 127;

        // map very small numbers to 0
        if (e < -24)
        {
            m_basetable[i | 0x000] = 0x0000;
            m_basetable[i | 0x100] = 0x8000;
            m_shifttable[i | 0x000] = 24;
            m_shifttable[i | 0x100] = 24;
        }
        // map small numbers to denorms
        else if (e < -14)
        {
            m_basetable[i | 0x000] = (0x0400 >> (-e - 14));
            m_basetable[i | 0x100] = (0x0400 >> (-e - 14)) | 0x8000;
            m_shifttable[i | 0x000] = amf_uint8(-e - 1);
            m_shifttable[i | 0x100] = amf_uint8(-e - 1);
        }
        // normal numbers lose precision
        else if (e <= 15)
        {
            m_basetable[i | 0x000] = amf_uint16((e + 15) << 10);
            m_basetable[i | 0x100] = amf_uint16(((e + 15) << 10) | 0x8000);
            m_shifttable[i | 0x000] = 13;
            m_shifttable[i | 0x100] = 13;
        }
        // large numbers map to infinity
        else if (e < 128)
        {
            m_basetable[i | 0x000] = 0x7C00;
            m_basetable[i | 0x100] = 0xFC00;
            m_shifttable[i | 0x000] = 24;
            m_shifttable[i | 0x100] = 24;
        }
        // infinity an NaN stay so
        else
        {
            m_basetable[i | 0x000] = 0x7C00;
            m_basetable[i | 0x100] = 0xFC00;
            m_shifttable[i | 0x000] = 13;
            m_shifttable[i | 0x100] = 13;
        }
    }
}
amf_uint16 AMFHalfFloat::m_basetable[512];
amf_uint8 AMFHalfFloat::m_shifttable[512];

void FillRGBA_F16SurfaceWithColor(amf::AMFSurface* surface, amf_uint8 R, amf_uint8 G, amf_uint8 B)
{
    amf::AMFPlane* pPlane = surface->GetPlaneAt(0);

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
}

void FillR10G10B10A2SurfaceWithColor(amf::AMFSurface* surface, amf_uint8 R, amf_uint8 G, amf_uint8 B)
{
    amf::AMFPlane* pPlane = surface->GetPlaneAt(0);

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
}

void FillP010SurfaceWithColor(amf::AMFSurface* surface, amf_uint8 Y, amf_uint8 U, amf_uint8 V)
{
    amf::AMFPlane* pPlaneY = surface->GetPlaneAt(0);
    amf::AMFPlane* pPlaneUV = surface->GetPlaneAt(1);

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
}

void FillSurfaceVulkan(amf::AMFContext* context, amf::AMFSurface* surface)
{
    amf::AMFComputePtr compute;
    context->GetCompute(amf::AMF_MEMORY_VULKAN, &compute);

    if (xPos + rectSize > widthIn)
    {
        xPos = 0;
    }
    if (yPos + rectSize > heightIn)
    {
        yPos = 0;
    }

    for (int p = 0; p < 2; p++)
    {
        amf::AMFPlane* plane = pColor1->GetPlaneAt(p);
        amf_size origin1[3] = { 0, 0 , 0 };
        amf_size region1[3] = { (amf_size)plane->GetWidth() , (amf_size)plane->GetHeight(), (amf_size)1 };
        compute->CopyPlane(plane, origin1, region1, surface->GetPlaneAt(p), origin1);


        plane = pColor2->GetPlaneAt(p);

        amf_size region2[3] = { (amf_size)plane->GetWidth(), (amf_size)plane->GetHeight(), (amf_size)1 };
        amf_size origin2[3] = { (amf_size)xPos / (p + 1), (amf_size)yPos / (p + 1) ,     0 };

        compute->CopyPlane(plane, origin1, region2, surface->GetPlaneAt(0), origin2);

    }
    xPos += 2; // NV12 surfaces do not accept odd positions - do not use ++
    yPos += 2; // NV12 surfaces do not accept odd positions - do not use ++
}


void PrepareFillFromHost(amf::AMFContext* context)
{
    AMF_RESULT res = AMF_OK; // error checking can be added later
    res = context->AllocSurface(amf::AMF_MEMORY_HOST, formatIn, widthIn, heightIn, &pColor1);
    res = context->AllocSurface(amf::AMF_MEMORY_HOST, formatIn, rectSize, rectSize, &pColor2);

    switch (formatIn)
    {
    case amf::AMF_SURFACE_NV12:
        FillNV12SurfaceWithColor(pColor2, 128, 0, 128);
        FillNV12SurfaceWithColor(pColor1, 128, 255, 128);
        break;
    case amf::AMF_SURFACE_RGBA:
        FillRGBASurfaceWithColor(pColor2, 255, 0, 0);
        FillRGBASurfaceWithColor(pColor1, 0, 0, 255);
        break;
    case amf::AMF_SURFACE_BGRA:
        FillBGRASurfaceWithColor(pColor2, 255, 0, 0);
        FillBGRASurfaceWithColor(pColor1, 0, 0, 255);
        break;
    case amf::AMF_SURFACE_R10G10B10A2:
        FillR10G10B10A2SurfaceWithColor(pColor2, 255, 0, 0);
        FillR10G10B10A2SurfaceWithColor(pColor1, 0, 0, 255);
        break;
    case amf::AMF_SURFACE_RGBA_F16:
        FillRGBA_F16SurfaceWithColor(pColor2, 255, 0, 0);
        FillRGBA_F16SurfaceWithColor(pColor1, 0, 0, 255);
        break;
    case amf::AMF_SURFACE_P010:
        FillP010SurfaceWithColor(pColor2, 255, 0, 0);
        FillP010SurfaceWithColor(pColor1, 0, 0, 255);
        break;
    default:
        break;
    }
    pColor1->Convert(memoryTypeIn);
    pColor2->Convert(memoryTypeIn);
}

AMF_RESULT FillSurface(amf::AMFContextPtr context, amf::AMFSurface** surfaceIn, bool bWait)
{
    amf::AMF_MEMORY_TYPE ComputeMemoryType = amf::AMF_MEMORY_DX11;

    if (*surfaceIn == NULL)
    {
        AMF_RESULT res = context->AllocSurface(memoryTypeIn, formatIn, widthIn, heightIn, surfaceIn);
        AMF_RETURN_IF_FAILED(res, L"AllocSurface() failed");

        if (memoryTypeIn == amf::AMF_MEMORY_VULKAN)
        {
            FillSurfaceVulkan(context, *surfaceIn);
            ComputeMemoryType = amf::AMF_MEMORY_VULKAN;
        }
#ifdef _WIN32
        else if (memoryTypeIn == amf::AMF_MEMORY_DX9)
        {
            FillSurfaceDX9(context, *surfaceIn);
            ComputeMemoryType = amf::AMF_MEMORY_COMPUTE_FOR_DX9;
        }
        else
        {
            FillSurfaceDX11(context, *surfaceIn);
        }
#endif
        amf::AMFComputePtr pCompute;
        context->GetCompute(ComputeMemoryType, &pCompute);
        if (pCompute != nullptr)
        {
            pCompute->FlushQueue();
            if (bWait)
            {
                pCompute->FinishQueue();
            }
        }
    }

    return AMF_OK;
}

AMF_RESULT ReadSurface(PipelineElementPtr pipelineElPtr, amf::AMFSurface** surface, amf::AMF_MEMORY_TYPE memoryType)
{
    AMF_RESULT res = pipelineElPtr->QueryOutput((amf::AMFData**)surface);
    if (res == AMF_EOF)
    {
        return res;
    }
    AMF_RETURN_IF_FAILED(res);
    return (surface && *surface) ? (*surface)->Convert(memoryType) : AMF_FAIL;
}