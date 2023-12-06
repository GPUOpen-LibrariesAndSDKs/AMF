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
// Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.
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
#include "HistogramImpl.h"
#include "public/include/core/Compute.h"
#include "public/common/TraceAdapter.h"
#include "public/include/core/Context.h"
#define _ATL_DISABLE_NOTHROW_NEW


extern const amf_uint8 Histogram[];
extern const amf_size HistogramCount;

#include <DirectXMath.h>

using namespace amf;
using namespace DirectX; 
//TODO - implement - portable code

#define AMF_FACILITY L"Histogram"
#define MAX_CORNERS     100

#define __global
#define __local
#define barrier(x)

inline static amf_uint32 AlignValue(amf_uint32 value, amf_uint32 alignment)
{
    return ((value + (alignment - 1)) & ~(alignment - 1));
}

static amf::AMF_KERNEL_ID   m_KernelHistogramId = static_cast<amf::AMF_KERNEL_ID>(-1);
static amf::AMF_KERNEL_ID   m_KernelColorId = static_cast<amf::AMF_KERNEL_ID>(-1);
static amf::AMF_KERNEL_ID   m_KernelNV12toRGBId = static_cast<amf::AMF_KERNEL_ID>(-1);
static amf::AMF_KERNEL_ID   m_KernelBuildLUTId = static_cast<amf::AMF_KERNEL_ID>(-1);
static amf::AMF_KERNEL_ID   m_KernelBuildLUTCenterId = static_cast<amf::AMF_KERNEL_ID>(-1);
static amf::AMF_KERNEL_ID   m_KernelBuildShiftsId = static_cast<amf::AMF_KERNEL_ID>(-1);

static amf::AMF_KERNEL_ID   m_KernelHistogramIdBin = static_cast<amf::AMF_KERNEL_ID>(-1);
static amf::AMF_KERNEL_ID   m_KernelColorIdBin = static_cast<amf::AMF_KERNEL_ID>(-1);
static amf::AMF_KERNEL_ID   m_KernelNV12toRGBIdBin = static_cast<amf::AMF_KERNEL_ID>(-1);
static amf::AMF_KERNEL_ID   m_KernelBuildLUTIdBin = static_cast<amf::AMF_KERNEL_ID>(-1);
static amf::AMF_KERNEL_ID   m_KernelBuildLUTCenterIdBin = static_cast<amf::AMF_KERNEL_ID>(-1);
static amf::AMF_KERNEL_ID   m_KernelBuildShiftsIdBin = static_cast<amf::AMF_KERNEL_ID>(-1);


amf::AMF_KERNEL_ID   m_KernelHistogramIdDX11 = static_cast<amf::AMF_KERNEL_ID>(-1);
amf::AMF_KERNEL_ID   m_KernelColorIdDX11 = static_cast<amf::AMF_KERNEL_ID>(-1);
amf::AMF_KERNEL_ID   m_KernelNV12toRGBIdDX11 = static_cast<amf::AMF_KERNEL_ID>(-1);
amf::AMF_KERNEL_ID   m_KernelBuildLUTIdDX11 = static_cast<amf::AMF_KERNEL_ID>(-1);
amf::AMF_KERNEL_ID   m_KernelBuildLUTCenterIdDX11 = static_cast<amf::AMF_KERNEL_ID>(-1);
amf::AMF_KERNEL_ID   m_KernelBuildShiftsIdDX11 = static_cast<amf::AMF_KERNEL_ID>(-1);

//#define RGB_COLORSPACE
#define LUT_CURVE_TABLE_SIZE    20

extern AMF_RESULT  RegisterKernelsDX11();

#pragma pack(push, 1)
struct HistogramParameters
{
    amf_int32 maxDistanceBetweenPeaks[3];
    amf_int32 whiteCutOffY;
    amf_int32 blackCutOffY;
    amf_int32 borderHistWidth; //TODO make image size - dependent
    float   alphaLUT;
    float   lutCurveTable[LUT_CURVE_TABLE_SIZE]; // curve from 0 to 1
    amf_int32 align1;
};
#pragma pack(pop)

static amf_int32 CrossCorrelation(amf_int32 *data1, amf_int32 *data2, amf_int32 maxdelay);
static float OneCrossCorrelation(
    __global amf_int32 *data1,    // [in]
    __global amf_int32 *data2,    // [in]
    amf_int32 delay,
    amf_int32 maxdelay,
    __global HistogramParameters *params
    );

// static parameters
static HistogramParameters histogramParams = {
    {90 , 20, 20},     // maxDistanceBetweenPeaks[3]
    217,                // whiteCutOffY
    20,                 // blackCutOffY
    100,                // borderHistWidth 
    0.1f,               // alphaLUT
};

static amf_int32 minStretchValue = 10000;
static amf_int32 borderForStretch = 0;

static void BuildOneLUT(amf_int32 col, float brightness, float *lut, float *prev, HistogramParameters *params, amf_int32 frameCount);
static void FilterData(amf_int32 *data,amf_int32 *dataPrev,  amf_int32 count, amf_int32 frameCount);
static void FilterDataInplace(amf_int32 *data, amf_int32 count);

//-------------------------------------------------------------------------------------------------
HistogramImpl::HistogramImpl()
  : m_iFrameCount(0)
  , m_bUseDX11NativeBuffer(false)
{
}
//-------------------------------------------------------------------------------------------------
HistogramImpl::~HistogramImpl()
{
    Terminate();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL HistogramImpl::CompileKernels(AMFCompute* pDevice, AMFComponentOptimizationCallback* /* pCallback */)
{
    static int registered = 0;
    if(!registered)
    {        
         AMFPrograms *pPrograms = NULL;
         g_AMFFactory.GetFactory()->GetPrograms(&pPrograms);
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelSource(&m_KernelHistogramId,       L"StitchHistogramMapNV12"        , "StitchHistogramMapNV12"          , HistogramCount, Histogram, 0));
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelSource(&m_KernelColorId,           L"WriteBorderRGB"        , "WriteBorderRGB"          , HistogramCount, Histogram, 0));
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelSource(&m_KernelNV12toRGBId,       L"StitchConvertNV12ToRGB"        , "StitchConvertNV12ToRGB"          , HistogramCount, Histogram, 0));
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelSource(&m_KernelBuildLUTId,        L"BuildLUT"        , "BuildLUT"          , HistogramCount, Histogram, 0));
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelSource(&m_KernelBuildLUTCenterId,  L"BuildLUTCenter"        , "BuildLUTCenter"          , HistogramCount, Histogram, 0));
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelSource(&m_KernelBuildShiftsId,     L"BuildShifts"        , "BuildShifts"          , HistogramCount, Histogram, 0));
        registered = 1;
    }

    if(pDevice->GetMemoryType() == AMF_MEMORY_DX11)
    {
        static int registeredDX11 = 0;
        if(!registeredDX11)
        {
            RegisterKernelsDX11();
            registeredDX11 = 1;
        }
    }
    return AMF_OK;
}

AMF_RESULT AMF_STD_CALL HistogramImpl::Init(AMFCompute* pDevice, AMFContext* pContext, amf_int32 count)
{
    AMF_RESULT res = AMF_OK;
    CompileKernels(pDevice, NULL);
    m_pContext = pContext;
    m_pDevice = pDevice;

    if(m_pDevice->GetMemoryType() == AMF_MEMORY_DX11)
    {
        AMF_RETURN_IF_FAILED(pDevice->GetKernel(m_KernelHistogramIdDX11, &m_pKernelHistogram));
        AMF_RETURN_IF_FAILED(pDevice->GetKernel(m_KernelColorIdDX11, &m_pKernelColor));
        AMF_RETURN_IF_FAILED(pDevice->GetKernel(m_KernelNV12toRGBIdDX11, &m_pKernelNV12toRGB));
        AMF_RETURN_IF_FAILED(pDevice->GetKernel(m_KernelBuildLUTIdDX11, &m_pKernelBuildLUT));
        AMF_RETURN_IF_FAILED(pDevice->GetKernel(m_KernelBuildLUTCenterIdDX11, &m_pKernelBuildLUTCenter));
        AMF_RETURN_IF_FAILED(pDevice->GetKernel(m_KernelBuildShiftsIdDX11, &m_pKernelBuildShifts));
    }
    else
    {
        AMF_RETURN_IF_FAILED(pDevice->GetKernel(m_KernelHistogramId, &m_pKernelHistogram));
        AMF_RETURN_IF_FAILED(pDevice->GetKernel(m_KernelColorId, &m_pKernelColor));
        AMF_RETURN_IF_FAILED(pDevice->GetKernel(m_KernelNV12toRGBId, &m_pKernelNV12toRGB));
        AMF_RETURN_IF_FAILED(pDevice->GetKernel(m_KernelBuildLUTId, &m_pKernelBuildLUT));
        AMF_RETURN_IF_FAILED(pDevice->GetKernel(m_KernelBuildLUTCenterId, &m_pKernelBuildLUTCenter));
        AMF_RETURN_IF_FAILED(pDevice->GetKernel(m_KernelBuildShiftsId, &m_pKernelBuildShifts));
    }
    for(int i = 0; i < _countof(histogramParams.lutCurveTable); i++)
    {
        histogramParams.lutCurveTable[i] = float((sin(M_PI_2 + i * M_PI / _countof(histogramParams.lutCurveTable)) + 1.0 ) / 2.0 );
    }

    amf_size histogramSize = count * (HIST_SIZE * 3 * 4 ) * sizeof(amf_uint32);
    amf_size brightnessSize = count * ( 3 * 4 ) * sizeof(float);
    amf_size lutSize = count * (HIST_SIZE * 5 * 3) * sizeof(float);

    //init unit LUT
    AMFBufferPtr pBufferLUTNone;
    res = m_pContext->AllocBuffer(AMF_MEMORY_HOST, lutSize, &pBufferLUTNone);
    AMF_RETURN_IF_FAILED(res, L"AllocBuffer() failed");

    for (amf_int32 channel = 0; channel < count; channel++)
    {
        for (amf_int32 side = 0; side < 5; side++)
        {
            for (amf_int32 col = 0; col < 3; col++)
            {
                float* lut = (float*)pBufferLUTNone->GetNative() + channel * 5 * 3 * HIST_SIZE + side * 3 * HIST_SIZE + col * HIST_SIZE;

                BuildOneLUT(col, 0.0f, lut, NULL, &histogramParams, 0);
            }
        }
    }

    m_bUseDX11NativeBuffer = false;
#if GPU_ACCELERATION
    if (m_pDevice->GetMemoryType() == AMF_MEMORY_DX11)
    {
        m_bUseDX11NativeBuffer = true;
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = (UINT)histogramSize;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = 0;
        desc.StructureByteStride = 0;

        res = CreateBufferFromDX11Native(&desc, NULL, &m_pHistogramsDX11);
        AMF_RETURN_IF_FAILED(res, L"CreateBufferFromDX11Native() failed");

        desc.ByteWidth = (UINT)brightnessSize;
        res = CreateBufferFromDX11Native(&desc, NULL, &m_pBrightnessDX11);
        AMF_RETURN_IF_FAILED(res, L"CreateBufferFromDX11Native() failed");

        desc.ByteWidth = (UINT)lutSize;
        res = CreateBufferFromDX11Native(&desc, NULL, &m_pBufferLUTDX11);
        AMF_RETURN_IF_FAILED(res, L"CreateBufferFromDX11Native() failed");

        res = CreateBufferFromDX11Native(&desc, NULL, &m_pBufferLUTPrevDX11);
        AMF_RETURN_IF_FAILED(res, L"CreateBufferFromDX11Native() failed");

        D3D11_SUBRESOURCE_DATA InitData = { 0 };
        InitData.pSysMem = pBufferLUTNone->GetNative();
        res = CreateBufferFromDX11Native(&desc, &InitData, &m_pBufferLUTNoneDX11);
        AMF_RETURN_IF_FAILED(res, L"CreateBufferFromDX11Native() failed");

        res = ClearBuffer(m_pHistogramsDX11);
        AMF_RETURN_IF_FAILED(res, L"ZeroBuffer() failed");
    }
    else
    {
        res = m_pContext->AllocBuffer(m_pDevice->GetMemoryType(), histogramSize, &m_pHistograms);
        AMF_RETURN_IF_FAILED(res, L"AllocBuffer() failed");

        res = m_pContext->AllocBuffer(m_pDevice->GetMemoryType(), brightnessSize, &m_pBrightness);
        AMF_RETURN_IF_FAILED(res, L"AllocBuffer() failed");
        AMF_MEMORY_TYPE memoryType = m_pDevice->GetMemoryType();
        res = m_pContext->AllocBuffer(memoryType, lutSize, &m_pBufferLUT);
        AMF_RETURN_IF_FAILED(res, L"AllocBuffer() failed");

        res = m_pContext->AllocBuffer(memoryType, lutSize, &m_pBufferLUTPrev);
        AMF_RETURN_IF_FAILED(res, L"AllocBuffer() failed");
    
        m_pBufferLUTNone = pBufferLUTNone;
        m_pBufferLUTNone->Convert(m_pDevice->GetMemoryType());

        amf_uint8 nullData = 0;
        res = m_pDevice->FillBuffer(m_pHistograms, 0, m_pHistograms->GetSize(), &nullData, sizeof(nullData));
        AMF_RETURN_IF_FAILED(res, L"ZeroBuffer() failed");
    }
#else
    if (m_pDevice->GetMemoryType() == AMF_MEMORY_DX11)
    {
        m_bUseDX11NativeBuffer = true;
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = (UINT)histogramSize;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = 0;
        desc.StructureByteStride = 0;

        res = CreateBufferFromDX11Native(&desc, NULL, &m_pHistogramsDX11);  //no cpu version for histogram creation
        AMF_RETURN_IF_FAILED(res, L"CreateBufferFromDX11Native() failed");
        res = ClearBuffer(m_pHistogramsDX11);
        AMF_RETURN_IF_FAILED(res, L"AllocBuffer() failed");

        res = m_pContext->AllocBuffer(AMF_MEMORY_HOST, histogramSize, &m_pHistograms);
        AMF_RETURN_IF_FAILED(res, L"AllocBuffer() failed");

        desc.ByteWidth = (UINT)lutSize;
        D3D11_SUBRESOURCE_DATA InitData = { 0 };
        InitData.pSysMem = pBufferLUTNone->GetNative();
        res = CreateBufferFromDX11Native(&desc, &InitData, &m_pBufferLUTNoneDX11);
        AMF_RETURN_IF_FAILED(res, L"CreateBufferFromDX11Native() failed");
    }
    else
    {
        res = m_pContext->AllocBuffer(m_pDevice->GetMemoryType(), histogramSize, &m_pHistograms);
        amf_uint8 nullData = 0;
        res = m_pDevice->FillBuffer(m_pHistograms, 0, m_pHistograms->GetSize(), &nullData, sizeof(nullData));
        AMF_RETURN_IF_FAILED(res, L"AllocBuffer() failed");

        m_pBufferLUTNone = pBufferLUTNone;
        m_pBufferLUTNone->Convert(m_pDevice->GetMemoryType());
    }

    AMF_MEMORY_TYPE memoryType = AMF_MEMORY_HOST;

    res = m_pContext->AllocBuffer(memoryType, brightnessSize, &m_pBrightness);
    AMF_RETURN_IF_FAILED(res, L"AllocBuffer() failed");

    res = m_pContext->AllocBuffer(memoryType, lutSize, &m_pBufferLUT);
    AMF_RETURN_IF_FAILED(res, L"AllocBuffer() failed");

    res = m_pContext->AllocBuffer(memoryType, lutSize, &m_pBufferLUTPrev);
    AMF_RETURN_IF_FAILED(res, L"AllocBuffer() failed");
#endif

    res = m_pContext->AllocBuffer(AMF_MEMORY_HOST, sizeof(HistogramParameters) , &m_pParams);
    AMF_RETURN_IF_FAILED(res, L"AllocBuffer() failed");

    memcpy(m_pParams->GetNative(), &histogramParams, m_pParams->GetSize());
    res = m_pParams->Convert(m_pDevice->GetMemoryType());
    AMF_RETURN_IF_FAILED(res, L"Convert() failed");

    m_iFrameCount = 0;
#if DUMP_HISTOGRAM
    amf::AMFDataStream::OpenDataStream(L"HistogramData.csv", AMFSO_WRITE, AMFFS_EXCLUSIVE, &m_pAllFile);

    m_pInputFiles.resize(count);
    for(amf_int32 i = 0; i < count; i++)
    {
        wchar_t buf[256];
        swprintf(buf, _countof(buf), L"HistogramInput%d.csv", i);
        amf::AMFDataStream::OpenDataStream(buf, AMFSO_WRITE, AMFFS_EXCLUSIVE, &m_pInputFiles[i]);
    }
#endif
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL HistogramImpl::Terminate()
{
    m_pBufferLUT = NULL;
    m_pBufferLUTPrev = NULL;
    m_pCorners = NULL;
    m_pShifts = NULL;
    m_pParams = NULL;
    m_pHistograms = NULL;
    m_pBrightness = NULL;
    m_pHistogramsDX11 = NULL;
    m_pBrightnessDX11 = NULL;
    m_pBufferLUTDX11 = NULL;
    m_pBufferLUTPrevDX11 = NULL;
    m_pShiftsDX11 = NULL;

    m_pKernelHistogram = NULL;
    m_pKernelColor = NULL;
    m_pKernelNV12toRGB = NULL;
    m_pKernelBuildLUT = NULL;
    m_pKernelBuildLUTCenter = NULL;
    m_pKernelBuildShifts = NULL;
    m_pDevice = NULL;
    m_pContext = NULL;
#if DUMP_HISTOGRAM
    m_pAllFile = NULL; 
    m_pInputFiles.clear(); 
#endif
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL HistogramImpl::Build(amf_int32 channel, AMFSurface* pSrcSurface, AMFRect /* border */, AMFSurface* pBorderMap)
{
    AMF_RESULT res = AMF_OK;
    // convert to OpenCL /MCL
    res = pSrcSurface->Convert(m_pDevice->GetMemoryType());
    AMF_RETURN_IF_FAILED(res, L"Convert(AMF_MEMORY_OPENCL) failed");

    res = pBorderMap->Convert(m_pDevice->GetMemoryType());
    AMF_RETURN_IF_FAILED(res, L"Convert(AMF_MEMORY_OPENCL) failed");

    // run kernal
    AMFPlanePtr planeY = pSrcSurface->GetPlane(AMF_PLANE_Y);
    AMFPlanePtr planeUV = pSrcSurface->GetPlane(AMF_PLANE_UV);
    AMFPlanePtr planeMap = pBorderMap->GetPlaneAt(0);

    if(m_pDevice->GetMemoryType() == AMF_MEMORY_DX11)
    {
        DXGI_FORMAT format = DXGI_FORMAT_R8_UINT;
        UINT size = sizeof(format);
        ((ID3D11Texture2D*)planeY->GetNative())->SetPrivateData(AMFStructuredBufferFormatGUID, size, &format);
    }

    amf_size index = 0;
    if (m_bUseDX11NativeBuffer)
    {
        DXGI_FORMAT formatDX11 = DXGI_FORMAT_R32_UINT;
        AMF_RETURN_IF_FAILED(m_pKernelHistogram->SetArgBufferNative(index++, m_pHistogramsDX11, AMF_ARGUMENT_ACCESS_WRITE));
        AMF_RETURN_IF_FAILED(m_pHistogramsDX11->SetPrivateData(AMFStructuredBufferFormatGUID, sizeof(DXGI_FORMAT), &formatDX11));
    }
    else
    {
        AMF_RETURN_IF_FAILED(m_pKernelHistogram->SetArgBuffer(index++, m_pHistograms, AMF_ARGUMENT_ACCESS_WRITE));
    }

    AMF_RETURN_IF_FAILED(m_pKernelHistogram->SetArgPlane(index++, planeY, AMF_ARGUMENT_ACCESS_READ));

    if(m_pDevice->GetMemoryType() == AMF_MEMORY_DX11)
    {
        DXGI_FORMAT format = DXGI_FORMAT_R8_UINT;
        UINT size = sizeof(format);
        ((ID3D11Texture2D*)planeUV->GetNative())->SetPrivateData(AMFStructuredBufferFormatGUID, size, &format);
    }
    AMF_RETURN_IF_FAILED(m_pKernelHistogram->SetArgPlane(index++, planeUV, AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(m_pKernelHistogram->SetArgPlane(index++, planeMap, AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(m_pKernelHistogram->SetArgBuffer(index++, m_pParams, AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(m_pKernelHistogram->SetArgInt32(index++, channel));

    amf_size sizeLocal[3] = {8, 8, 1};
    AMF_RETURN_IF_FAILED(m_pKernelHistogram->GetCompileWorkgroupSize(sizeLocal));

    amf_size sizeGlobal[3] = {0, 0, 1};
    sizeGlobal[0] = AlignValue(planeY->GetWidth() / 2, static_cast<amf_uint32>(sizeLocal[0]));
    sizeGlobal[1] = AlignValue(planeY->GetHeight() / 2, static_cast<amf_uint32>(sizeLocal[1]));
    amf_size offset[3] = {0, 0, 0};

    AMF_RETURN_IF_FAILED(m_pKernelHistogram->Enqueue(2, offset, sizeGlobal, sizeLocal));
    AMF_RETURN_IF_FAILED(m_pDevice->FlushQueue());
    // draw border on surface
#if DRAW_AREAS
//    if(channel == 0 )
    {
        index = 0;
        AMF_RETURN_IF_FAILED(m_pKernelColor->SetArgPlane(index++, planeY, AMF_ARGUMENT_ACCESS_WRITE));
        AMF_RETURN_IF_FAILED(m_pKernelColor->SetArgPlane(index++, planeUV, AMF_ARGUMENT_ACCESS_WRITE));
        AMF_RETURN_IF_FAILED(m_pKernelColor->SetArgPlane(index++, planeMap, AMF_ARGUMENT_ACCESS_READ));

        AMF_RETURN_IF_FAILED(m_pKernelColor->Enqueue(2, offset, sizeGlobal, sizeLocal));
        AMF_RETURN_IF_FAILED(m_pDevice->FlushQueue());
    }
#endif    
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL HistogramImpl::Convert(bool bColorBalance, amf_int32 channel, AMFSurface* pSrcSurface, float *borders, bool sRGB, AMFSurface* pDstSurface)
{
    AMF_RESULT res = AMF_OK;

    // convert to OpenCL
    AMF_MEMORY_TYPE oldTypeDst = pDstSurface->GetMemoryType();
    res = pSrcSurface->Convert(m_pDevice->GetMemoryType());
    AMF_RETURN_IF_FAILED(res, L"Convert(AMF_MEMORY_OPENCL) failed");
    res = pDstSurface->Convert(m_pDevice->GetMemoryType());
    AMF_RETURN_IF_FAILED(res, L"Convert(AMF_MEMORY_OPENCL) failed");
   
    // run kernal
    AMFPlanePtr planeY = pSrcSurface->GetPlane(AMF_PLANE_Y);
    AMFPlanePtr planeUV = pSrcSurface->GetPlane(AMF_PLANE_UV);
    AMFPlanePtr planeRGB = pDstSurface->GetPlane(AMF_PLANE_PACKED);

    amf_size index = 0;
    AMF_RETURN_IF_FAILED(m_pKernelNV12toRGB->SetArgPlane(index++, planeRGB, AMF_ARGUMENT_ACCESS_WRITE));

    if(m_pDevice->GetMemoryType() == AMF_MEMORY_DX11)
    {
        DXGI_FORMAT format = DXGI_FORMAT_R8_UINT;
        UINT size = sizeof(format);
        ((ID3D11Texture2D*)planeY->GetNative())->SetPrivateData(AMFStructuredBufferFormatGUID, size, &format);
    }

    AMF_RETURN_IF_FAILED(m_pKernelNV12toRGB->SetArgPlane(index++, planeY, AMF_ARGUMENT_ACCESS_READ));
    if(m_pDevice->GetMemoryType() == AMF_MEMORY_DX11)
    {
        DXGI_FORMAT format = DXGI_FORMAT_R8_UINT;
        UINT size = sizeof(format);
        ((ID3D11Texture2D*)planeUV->GetNative())->SetPrivateData(AMFStructuredBufferFormatGUID, size, &format);
    }
    AMF_RETURN_IF_FAILED(m_pKernelNV12toRGB->SetArgPlane(index++, planeUV, AMF_ARGUMENT_ACCESS_READ));

    if(bColorBalance)
    {
#if GPU_ACCELERATION
        if (m_bUseDX11NativeBuffer)
        {
            DXGI_FORMAT formatDX11 = DXGI_FORMAT_R32_FLOAT;
            AMF_RETURN_IF_FAILED(m_pKernelNV12toRGB->SetArgBufferNative(index++, m_pBufferLUTDX11, AMF_ARGUMENT_ACCESS_READ));
            AMF_RETURN_IF_FAILED(m_pBufferLUTDX11->SetPrivateData(AMFStructuredBufferFormatGUID, sizeof(DXGI_FORMAT), &formatDX11));
        }
        else
        {
            AMF_RETURN_IF_FAILED(m_pKernelNV12toRGB->SetArgBuffer(index++, m_pBufferLUT, AMF_ARGUMENT_ACCESS_READ));
        }
#else
        //work around with the Direct Compute using CPU filled data buffer
        if (m_pDevice->GetMemoryType() == AMF_MEMORY_DX11)
        {
            HRESULT hr = S_OK;
            ID3D11Device* pDevice = (ID3D11Device*)m_pDevice->GetNativeDeviceID();
            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth = (UINT)m_pBufferLUT->GetSize();
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
            desc.MiscFlags = 0;
            desc.StructureByteStride = 0;

            m_pBufferLUT->Convert(AMF_MEMORY_HOST);
            D3D11_SUBRESOURCE_DATA InitData = { 0 };
            InitData.pSysMem = m_pBufferLUT->GetNative();

            ID3D11BufferPtr pBufferLut;
            res = CreateBufferFromDX11Native(&desc, &InitData, &pBufferLut);
            AMF_RETURN_IF_FAILED(res, L"CreateBufferFromDX11Native() failed");

            DXGI_FORMAT formatDX11 = DXGI_FORMAT_R32_FLOAT;
            AMF_RETURN_IF_FAILED(m_pKernelNV12toRGB->SetArgBufferNative(index++, pBufferLut, AMF_ARGUMENT_ACCESS_READ));
            AMF_RETURN_IF_FAILED(pBufferLut->SetPrivateData(AMFStructuredBufferFormatGUID, sizeof(DXGI_FORMAT), &formatDX11));
        }
        else
        {
            AMF_RETURN_IF_FAILED(m_pKernelNV12toRGB->SetArgBuffer(index++, m_pBufferLUT, AMF_ARGUMENT_ACCESS_READ));
        }
#endif
    }
    else
    {
        if (m_bUseDX11NativeBuffer)
        {
            DXGI_FORMAT formatDX11 = DXGI_FORMAT_R32_FLOAT;
            AMF_RETURN_IF_FAILED(m_pKernelNV12toRGB->SetArgBufferNative(index++, m_pBufferLUTNoneDX11, AMF_ARGUMENT_ACCESS_READ));
            AMF_RETURN_IF_FAILED(m_pBufferLUTNoneDX11->SetPrivateData(AMFStructuredBufferFormatGUID, sizeof(DXGI_FORMAT), &formatDX11));
        }
        else
        {
            AMF_RETURN_IF_FAILED(m_pKernelNV12toRGB->SetArgBuffer(index++, m_pBufferLUTNone, AMF_ARGUMENT_ACCESS_READ));
        }
    }

    AMF_RETURN_IF_FAILED(m_pKernelNV12toRGB->SetArgFloat(index++, (borders[0] - 0.5f) * 2.0f));
    AMF_RETURN_IF_FAILED(m_pKernelNV12toRGB->SetArgFloat(index++, (borders[1] - 0.5f) * 2.0f));
    AMF_RETURN_IF_FAILED(m_pKernelNV12toRGB->SetArgFloat(index++, (borders[2] - 0.5f) * 2.0f));
    AMF_RETURN_IF_FAILED(m_pKernelNV12toRGB->SetArgFloat(index++, (borders[3] - 0.5f) * 2.0f));
    AMF_RETURN_IF_FAILED(m_pKernelNV12toRGB->SetArgInt32(index++, channel));
    amf_int32 linearRGB = sRGB ? 1 : 0;
    AMF_RETURN_IF_FAILED(m_pKernelNV12toRGB->SetArgInt32(index++, linearRGB));

    amf_size sizeLocal[3] = {8, 8, 1};
    AMF_RETURN_IF_FAILED(m_pKernelNV12toRGB->GetCompileWorkgroupSize(sizeLocal));

    amf_size sizeGlobal[3] = {0, 0, 1};
    sizeGlobal[0] = AlignValue(planeY->GetWidth() / 2, static_cast<amf_uint32>(sizeLocal[0]));
    sizeGlobal[1] = AlignValue(planeY->GetHeight() /2, static_cast<amf_uint32>(sizeLocal[1]));

    amf_size offset[3] = {0, 0, 0};
    AMF_RETURN_IF_FAILED(m_pKernelNV12toRGB->Enqueue(2, offset, sizeGlobal, sizeLocal));
    AMF_RETURN_IF_FAILED(m_pDevice->FlushQueue());
    // convert memory back
    res = pDstSurface->Convert(oldTypeDst);
    AMF_RETURN_IF_FAILED(res, L"Convert(AMF_MEMORY_OPENCL) failed");
    return AMF_OK;
}

//--------------------------------------------------------------------------------------------------------------------
void BuildOneLUT(amf_int32 col, float brightness, __global float *lut, __global float *prev, __global struct HistogramParameters *params, amf_int32 frameCount)
{
    for(amf_int32 k=0; k < HIST_SIZE; k++)
    {
        float lutCurr = brightness;
        if(col == 0)
        {
            if(k < params->blackCutOffY)
            {
                lutCurr = 0.0f;
            }
            else if(k < LUT_CURVE_TABLE_SIZE + params->blackCutOffY)
            {
                lutCurr *= 1.0f - params->lutCurveTable[k - params->blackCutOffY];
            }
            else if(k > params->whiteCutOffY)
            {
                lutCurr = 0.0f;
            }
            else if(k >= params->whiteCutOffY - LUT_CURVE_TABLE_SIZE)
            {
                lutCurr *= params->lutCurveTable[k - (params->whiteCutOffY - LUT_CURVE_TABLE_SIZE) ];
            }
        }

        lutCurr += (float)k /255.f;

        if(prev != NULL)
        {
            float lutPrev = prev[k];
            if(frameCount != 0 )
            {
                lutCurr = lutPrev + params->alphaLUT * ( lutCurr - lutPrev);
            }
            prev[k] = lutCurr;
        }
        lut[k] = lutCurr;
   }
}

#define MY_SIGN(a) (a == 0 ? 0 : (a < 0 ? -1 : 1)  )
//-------------------------------------------------------------------------------------------------
void BuildShifts(
    __global float  *pShifts,
    __global amf_int32 *pHistogram,
    __global int *pCorners,
    __global int *pParams,
    amf_int32 corners,
    amf_int32 /* frameCount */
)
{
    __global struct HistogramParameters *params = (__global struct HistogramParameters *)pParams;
    for(amf_int32 corner = 0; corner < corners; corner++)
    {
        __global struct Corner* it_corner = (__global struct Corner*)pCorners + corner;

        for(amf_int32 col = 0; col < 3; col++)
        {
            for(amf_int32 side = 0; side < it_corner->count; side++)
            {
                __local float corrs[HIST_SIZE * 2];

                for(amf_int32 i = 0; i < HIST_SIZE *2; i++)
                {
                    corrs[i] = 0;
                }
                __global amf_int32* pHist[3];

                for(amf_int32 i = 0; i < it_corner->count; i++)
                {
                    pHist[i] = pHistogram + it_corner->channel[i] * 4 * 3 * HIST_SIZE  + it_corner->corner[i] * HIST_SIZE * 3 +  col * HIST_SIZE;
                }

                int h1 = 0;
                int h2 = 0;
                if(it_corner->count == 3)
                { 
                    if(side == 0)
                    {
                        h1 = 0;
                        h2 = 1;
                    }
                    else if(side == 1)
                    {
                        h1 = 1;
                        h2 = 2;
                    }
                    else if(side == 2)
                    {
                        h1 = 2;
                        h2 = 0;
                    }
                }
                if(it_corner->count == 2)
                {
                    if(side == 0)
                    { 
                        h1 = 0;
                        h2 = 1;
                    }
                    else 
                    { 
                        h1 = 1;
                        h2 = 0;
                    }
                }

                for(amf_int32 delay = 0; delay < params->maxDistanceBetweenPeaks[col] * 2; delay++)
                {
                    barrier  (CLK_LOCAL_MEM_FENCE);
                    float shift =  OneCrossCorrelation(pHist[h1], pHist[h2], delay, params->maxDistanceBetweenPeaks[col], params);;
                    float weight = 1.0f;
                    corrs[delay] = shift * weight;
                    barrier  (CLK_LOCAL_MEM_FENCE);
                }

                float corrMax = -1.0e5f;
                for(amf_int32 i = 0; i < params->maxDistanceBetweenPeaks[col] * 2; i++)
                {
                    if(corrMax < corrs[i])
                    {
                        corrMax = corrs[i];
                        pShifts[corner * 3 * it_corner->count + col * it_corner->count + side] = (float)i - params->maxDistanceBetweenPeaks[col];
                    }
                }
            }
        }
    }
}

//-------------------------------------------------------------------------------------------------
static void BuildLUT(
    __global float *pLUT,        ///< [out] histogram data  - 256 x 3 for RG, G and B
    __global float *pLUTPrev,    ///< [in] histogram data  - 256 x 3 for RG, G and B
    __global float *pBrightness,
    __global float  *pShifts,
    __global int *pCorners,
    __global int *pParams,
    amf_int32 corners,
    amf_int32 frameCount
)
{
    __global struct HistogramParameters *params = (__global struct HistogramParameters *)pParams;
    for(amf_int32 corner = 0; corner < corners; corner++)
    {
        __global struct Corner* it_corner = (__global struct Corner*)pCorners + corner;

        for(int col = 0; col < 3; col++)
        {
            float       brightness[3];
            brightness[0] = 0;
            brightness[1] = 0;
            brightness[2] = 0;
            int shiftOffset = corner * 3 * it_corner->count + col * it_corner->count;

            if(it_corner->count == 2)
            {
                brightness[0] =  pShifts[shiftOffset + 0] / 2.0f;
                brightness[1] =  pShifts[shiftOffset + 1] / 2.0f;
            }
            else if(it_corner->count == 3)
            {

                int shiftMaxIndex = 0;
                int shiftMinIndex = HIST_SIZE *2;
                float shiftMin = 10000.0f;
                float shiftMax = 0.0f;


                for(int side = 0; side < it_corner->count; side++)
                {
                    if(fabs(shiftMin) > fabs(pShifts[shiftOffset + side]))
                    {
                        shiftMin = pShifts[shiftOffset + side];
                        shiftMinIndex = side;
                    }
                    if(fabs(shiftMax) < fabs(pShifts[shiftOffset + side]))
                    {
                        shiftMax = pShifts[shiftOffset + side];
                        shiftMaxIndex = side;
                    }
                }
                int shiftMinIndexSecond = 0;
                for(int side = 0; side < it_corner->count; side++)
                {
                    if(side != shiftMinIndex && side != shiftMaxIndex)
                    {
                        shiftMinIndexSecond = side;
                        break;
                    }
                }

                shiftMin = pShifts[shiftOffset + shiftMinIndex];
                float shiftMinSecond = pShifts[shiftOffset + shiftMinIndexSecond];

                if(shiftMinIndex == 0 )
                {
                    brightness[0] =  shiftMin/ 2.0f;
                    brightness[1] =  - shiftMin / 2.0f;

                    if(shiftMinIndexSecond == 1)
                    {
                        brightness[2] = -shiftMinSecond *2.0f / 3.0f - shiftMin/ 2.0f;

                        brightness[0] += shiftMinSecond / 3.0f;
                        brightness[1] += shiftMinSecond / 3.0f;
                    }
                    else
                    {
                        brightness[2] = shiftMinSecond *2.0f / 3.0f + shiftMin/ 2.0f;

                        brightness[0] -= shiftMinSecond / 3.0f;
                        brightness[1] -= shiftMinSecond / 3.0f;
                    }

                }
                else  if(shiftMinIndex == 1 )
                {
                    brightness[1] =  shiftMin/ 2.0f;
                    brightness[2] =  -shiftMin / 2.0f;

                    if(shiftMinIndexSecond == 0)
                    {
                        brightness[0] = shiftMinSecond *2.0f / 3.0f + shiftMin/ 2.0f;

                        brightness[1] -=  shiftMinSecond / 3.0f;
                        brightness[2] -=  shiftMinSecond / 3.0f;
                    }
                    else
                    {
                        brightness[0] = -shiftMinSecond *2.0f / 3.0f - shiftMin/ 2.0f;

                        brightness[1] +=  shiftMinSecond / 3.0f;
                        brightness[2] +=  shiftMinSecond / 3.0f;
                    }
                }
                else if(shiftMinIndex ==  2)
                {
                    brightness[2] =  shiftMin/ 2.0f;
                    brightness[0] =  - shiftMin / 2.0f;

                    if(shiftMinIndexSecond == 0)
                    {
                        brightness[1] = -shiftMinSecond *2.0f / 3.0f - shiftMin/ 2.0f;

                        brightness[2] +=  shiftMinSecond / 3.0f;
                        brightness[0] +=  shiftMinSecond / 3.0f;
                    }
                    else // 1
                    {
                        brightness[1] = shiftMinSecond *2.0f / 3.0f + shiftMin/ 2.0f;

                        brightness[2] -=  shiftMinSecond / 3.0f;
                        brightness[0] -=  shiftMinSecond / 3.0f;
                    }
                }
            }

            for(amf_int32 i = 0; i < it_corner->count; i++)
            { 
                __global float *lut  = pLUT + it_corner->channel[i] * 3 *5 * HIST_SIZE + it_corner->corner[i] * 3* HIST_SIZE + col * HIST_SIZE;
                __global float *lutPrev = pLUTPrev + it_corner->channel[i] * 3 *5 * HIST_SIZE + it_corner->corner[i] * 3* HIST_SIZE + col * HIST_SIZE;
                BuildOneLUT( col, brightness[i] / 255.f,  lut, lutPrev, params, frameCount);
                pBrightness[it_corner->channel[i] * 4 *3 + it_corner->corner[i] *3 + col]  = brightness[i];
            }
        }
    }
}

//--------------------------------------------------------------------------------------------------------------------
static void BuildLUTCenter(
    __global float *pLUT,        ///< [out] histogram data  - 256 x 3 for RG, G and B
    __global float *pLUTPrev,    ///< [in] histogram data  - 256 x 3 for RG, G and B
    __global float *pBrightness,
    __global int *pParams,
    amf_int32 channels,
    amf_int32 frameCount
)
{
    __global struct HistogramParameters *params = (__global struct HistogramParameters *)pParams;
    for(amf_int32 channel = 0; channel < channels; channel++)
    {
        for(amf_int32 col = 0; col < 3; col++)
        {
            float brightnessCenter = 0;
            for( amf_int32 side = 0; side < 4; side++)
            {
                brightnessCenter +=  pBrightness[channel * 4 * 3 + side *3 + col];
            }
            brightnessCenter/=4.0f;

            __global float *lutCenter  = pLUT + channel * 3 *5 * HIST_SIZE + 4 * 3* HIST_SIZE + col * HIST_SIZE;
            __global float *lutPrevCenter = pLUTPrev + channel * 3 *5 * HIST_SIZE + 4 * 3* HIST_SIZE + col * HIST_SIZE;
            BuildOneLUT( col, brightnessCenter / 255.f,  lutCenter, lutPrevCenter, params, frameCount);
        }
    }
}

//--------------------------------------------------------------------------------------------------------------------
static amf_int32 CrossCorrelation(amf_int32 *data1, amf_int32 *data2, amf_int32 maxdelay)
{
    double x[HIST_SIZE];
    double y[HIST_SIZE];

    for(amf_int32 i = 0; i < HIST_SIZE; i++)
    {
        x[i]  = (double)data1[i];
        y[i]  = (double)data2[i];
    }

   double sx,sy,sxy,denom;

   /* Calculate the mean of the two series x[], y[] */
   double mx = 0;
   double my = 0;   
   for (amf_int32 i=0 ; i < HIST_SIZE; i++) 
   {
      mx += x[i];
      my += y[i];
   }
   mx /= HIST_SIZE;
   my /= HIST_SIZE;

   /* Calculate the denominator */
   sx = 0;
   sy = 0;
   for (amf_int32 i=0; i < HIST_SIZE; i++) 
   {
      sx += (x[i] - mx) * (x[i] - mx);
      sy += (y[i] - my) * (y[i] - my);
   }
   denom = sqrt(sx*sy);

   /* Calculate the correlation series */
   double rMax = 0;
   amf_int32 delayMax = 0;
   for (amf_int32 delay=-maxdelay; delay < maxdelay; delay++) 
   {
      sxy = 0;
      for (int i=0;i<HIST_SIZE;i++) 
      {
         int j = i + delay;
         if (j < 0 || j >= HIST_SIZE)
         {
             continue;
         }
         else
         {
             sxy += (x[i] - mx) * (y[j] - my);
         }
      }
      double r = sxy / denom;
      
      /* r is the correlation coefficient at "delay" */
      if(r > rMax)
      {
          rMax = r; 
          delayMax = delay;
      }
   }
   return delayMax;
}

//--------------------------------------------------------------------------------------------------------------------
#if DUMP_HISTOGRAM
static AMF_RESULT WriteHistogramInt(AMFDataStream* file, amf_int32 *data, amf_int32 count)
{
   if(file != NULL)
   {
       char buf[100];
       for(amf_int32 k=0; k < count; k++)
       {
           int pos = sprintf(buf, "%d", (int)data[k]);
           if(k + 1 != count)
           {
               pos += sprintf(buf + pos, ",");
           }
           else
           {
               pos += sprintf(buf + pos, "\n");
           }
           file->Write(buf, pos, NULL);
       }
   }
    return AMF_OK;
}
static AMF_RESULT WriteHistogramFloat(AMFDataStream* file, amf_int32 *data, amf_int32 count)
{
   if(file != NULL)
   {
       char buf[100];
       for(amf_int32 k=0; k < count; k++)
       {
           int pos = sprintf(buf, "%f", ((float*)data)[k]);
           if(k + 1 != count)
           {
               pos += sprintf(buf + pos, ",");
           }
           else
           {
               pos += sprintf(buf + pos, "\n");
           }
           file->Write(buf, pos, NULL);
       }
   }
    return AMF_OK;
}
#endif
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL HistogramImpl::Adjust(amf_int32 count, RibList & /* ribs */, CornerList& corners, float* /* correction */)
{
    AMF_RESULT res = AMF_OK;

    if(corners.size() == 0 )
    {
        return AMF_OK;
    }

    if(m_pCorners == NULL)
    {

        if(m_pDevice->GetMemoryType() == AMF_MEMORY_DX11)
        {
            res = m_pContext->AllocBuffer(AMF_MEMORY_HOST, MAX_CORNERS * sizeof(Corner) , &m_pCorners);
        }
        else
        {
            res = m_pContext->AllocBuffer(AMF_MEMORY_HOST, corners.size() * sizeof(Corner) , &m_pCorners);
        }
        AMF_RETURN_IF_FAILED(res, L"AllocBuffer() failed");
        Corner * corner = (Corner *)m_pCorners->GetNative();
        for(CornerList::iterator it_corner = corners.begin(); it_corner != corners.end(); it_corner++)
        {
            *corner++ = *it_corner;
        }
#if GPU_ACCELERATION
        res = m_pCorners->Convert(m_pDevice->GetMemoryType());
        AMF_RETURN_IF_FAILED(res, L"Convert() failed");
#endif
    }
    if ((m_pShifts == NULL) || (m_pShiftsDX11 == NULL))
    {
#if GPU_ACCELERATION
        if (m_bUseDX11NativeBuffer)
        {
            if (m_pShiftsDX11 == NULL)
            {
                D3D11_BUFFER_DESC desc = {};
                desc.ByteWidth = (UINT)corners.size() * 3 * corners[0].count * sizeof(float);
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
                desc.CPUAccessFlags = 0;
                desc.MiscFlags = 0;
                desc.StructureByteStride = 0;

                res = CreateBufferFromDX11Native(&desc, NULL, &m_pShiftsDX11);
                AMF_RETURN_IF_FAILED(res, L"CreateBufferFromDX11Native() failed");
            }
        }
        else
        {
            res = m_pContext->AllocBuffer(m_pDevice->GetMemoryType(), corners.size() * 3 * corners[0].count * sizeof(float), &m_pShifts);
            AMF_RETURN_IF_FAILED(res, L"AllocBuffer() failed");
        }
#else
        amf_int32 len = (UINT)corners.size() * 3 * corners[0].count * sizeof(float);
        res = m_pContext->AllocBuffer(AMF_MEMORY_HOST, len, &m_pShifts);
#endif
    }
#if DUMP_HISTOGRAM
    if(m_iFrameCount == 0 && m_pAllFile != NULL)
    {
        AMFDataPtr pData;
        m_pHistograms->Duplicate(AMF_MEMORY_HOST, &pData);
        AMFBufferPtr pHist(pData);
        amf_int32* data = (amf_int32*)pHist->GetNative();

        for(CornerList::iterator it_corner = corners.begin(); it_corner != corners.end(); it_corner++)
        {
            for( amf_int32 col = 0; col < 3; col ++)
            {
                
                char buf[1000];

                int pos = sprintf(buf, "index=,%d,col=,%d,",it_corner->index, col);

                for(amf_int32  i = 0; i < it_corner->count; i++)
                {
                    pos += sprintf(buf + pos, "channel%d=,%d,", i, it_corner->channel[i]);
                }
                for(amf_int32  i = 0; i < it_corner->count; i++)
                {
                    pos += sprintf(buf + pos, "corner%d=,%d,", i, it_corner->corner[i]);
                }
                pos += sprintf(buf + pos, "\n");

                m_pAllFile->Write(buf, pos, NULL);

                for(amf_int32  i = 0; i < it_corner->count; i++)
                {
                    WriteHistogramInt(m_pAllFile, data + it_corner->channel[i] * 4 * 3 * HIST_SIZE  + it_corner->corner[i] * HIST_SIZE * 3 +  col * HIST_SIZE, HIST_SIZE);
                }
            }
        }
    }
#endif
#if GPU_ACCELERATION
    amf_size index = 0;
    amf_size sizeLocal[3] = {8, 8, 1};
    amf_size sizeGlobal[3] = {0, 0, 1};
    amf_size offset[3] = {0, 0, 0};

    if (m_bUseDX11NativeBuffer)
    {
        DXGI_FORMAT formatDX11 = DXGI_FORMAT_R32_FLOAT;
        AMF_RETURN_IF_FAILED(m_pKernelBuildShifts->SetArgBufferNative(index++, m_pShiftsDX11, AMF_ARGUMENT_ACCESS_WRITE));
        AMF_RETURN_IF_FAILED(m_pShiftsDX11->SetPrivateData(AMFStructuredBufferFormatGUID, sizeof(DXGI_FORMAT), &formatDX11));

        formatDX11 = DXGI_FORMAT_R32_UINT;
        AMF_RETURN_IF_FAILED(m_pKernelBuildShifts->SetArgBufferNative(index++, m_pHistogramsDX11, AMF_ARGUMENT_ACCESS_READ));
        AMF_RETURN_IF_FAILED(m_pHistogramsDX11->SetPrivateData(AMFStructuredBufferFormatGUID, sizeof(DXGI_FORMAT), &formatDX11));
    }
    else
    {
        AMF_RETURN_IF_FAILED(m_pKernelBuildShifts->SetArgBuffer(index++, m_pShifts, AMF_ARGUMENT_ACCESS_WRITE));
        AMF_RETURN_IF_FAILED(m_pKernelBuildShifts->SetArgBuffer(index++, m_pHistograms, AMF_ARGUMENT_ACCESS_READ));
    }

    AMF_RETURN_IF_FAILED(m_pKernelBuildShifts->SetArgBuffer(index++, m_pCorners, AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(m_pKernelBuildShifts->SetArgBuffer(index++, m_pParams, AMF_ARGUMENT_ACCESS_READ));  
    AMF_RETURN_IF_FAILED(m_pKernelBuildShifts->SetArgInt32(index++,  (amf_int32)corners.size()));
    AMF_RETURN_IF_FAILED(m_pKernelBuildShifts->SetArgInt32(index++,  (amf_int32)m_iFrameCount));

    amf_int32 maxDist = 0;
    for(int i = 0; i < 3; i++)
    {
        if(maxDist < histogramParams.maxDistanceBetweenPeaks[i])
        {
            maxDist = histogramParams.maxDistanceBetweenPeaks[i];
        }
    }

    AMF_RETURN_IF_FAILED(m_pKernelBuildShifts->GetCompileWorkgroupSize(sizeLocal));
    sizeGlobal[0] = AlignValue(maxDist * 2, static_cast<amf_uint32>(sizeLocal[0]));                     // by delay
    sizeGlobal[1] = AlignValue((amf_int32)corners.size() * _countof(corners[0].channel), static_cast<amf_uint32>(sizeLocal[1]));   // corners x 3 sides
    sizeGlobal[2] = AlignValue(3, static_cast<amf_uint32>(sizeLocal[2]));                               // by color

    AMF_RETURN_IF_FAILED(m_pKernelBuildShifts->Enqueue(3, offset, sizeGlobal, sizeLocal));

    index = 0;
    if (m_bUseDX11NativeBuffer)
    {
        DXGI_FORMAT formatDX11 = DXGI_FORMAT_R32_FLOAT;
        AMF_RETURN_IF_FAILED(m_pKernelBuildLUT->SetArgBufferNative(index++, m_pBufferLUTDX11, AMF_ARGUMENT_ACCESS_WRITE));
        AMF_RETURN_IF_FAILED(m_pBufferLUTDX11->SetPrivateData(AMFStructuredBufferFormatGUID, sizeof(DXGI_FORMAT), &formatDX11));

        AMF_RETURN_IF_FAILED(m_pKernelBuildLUT->SetArgBufferNative(index++, m_pBufferLUTPrevDX11, AMF_ARGUMENT_ACCESS_READWRITE));
        AMF_RETURN_IF_FAILED(m_pBufferLUTPrevDX11->SetPrivateData(AMFStructuredBufferFormatGUID, sizeof(DXGI_FORMAT), &formatDX11));

        AMF_RETURN_IF_FAILED(m_pKernelBuildLUT->SetArgBufferNative(index++, m_pBrightnessDX11, AMF_ARGUMENT_ACCESS_WRITE));
        AMF_RETURN_IF_FAILED(m_pBrightnessDX11->SetPrivateData(AMFStructuredBufferFormatGUID, sizeof(DXGI_FORMAT), &formatDX11));

        AMF_RETURN_IF_FAILED(m_pKernelBuildLUT->SetArgBufferNative(index++, m_pShiftsDX11, AMF_ARGUMENT_ACCESS_READ));
        AMF_RETURN_IF_FAILED(m_pShiftsDX11->SetPrivateData(AMFStructuredBufferFormatGUID, sizeof(DXGI_FORMAT), &formatDX11));
    }
    else
    {
        AMF_RETURN_IF_FAILED(m_pKernelBuildLUT->SetArgBuffer(index++, m_pBufferLUT, AMF_ARGUMENT_ACCESS_WRITE));
        AMF_RETURN_IF_FAILED(m_pKernelBuildLUT->SetArgBuffer(index++, m_pBufferLUTPrev, AMF_ARGUMENT_ACCESS_READWRITE));
        AMF_RETURN_IF_FAILED(m_pKernelBuildLUT->SetArgBuffer(index++, m_pBrightness, AMF_ARGUMENT_ACCESS_WRITE));
        AMF_RETURN_IF_FAILED(m_pKernelBuildLUT->SetArgBuffer(index++, m_pShifts, AMF_ARGUMENT_ACCESS_READ));
    }

    AMF_RETURN_IF_FAILED(m_pKernelBuildLUT->SetArgBuffer(index++, m_pCorners, AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(m_pKernelBuildLUT->SetArgBuffer(index++, m_pParams, AMF_ARGUMENT_ACCESS_READ));  
    AMF_RETURN_IF_FAILED(m_pKernelBuildLUT->SetArgInt32(index++,  (amf_int32)corners.size()));
    AMF_RETURN_IF_FAILED(m_pKernelBuildLUT->SetArgInt32(index++,  (amf_int32)m_iFrameCount));

    AMF_RETURN_IF_FAILED(m_pKernelBuildLUT->GetCompileWorkgroupSize(sizeLocal));
    sizeGlobal[0] = AlignValue((amf_int32)corners.size(), static_cast<amf_uint32>(sizeLocal[0]));   //by  corner
    sizeGlobal[1] = AlignValue(4, static_cast<amf_uint32>(sizeLocal[1]));                           // by side + center
    sizeGlobal[2] = AlignValue(3, static_cast<amf_uint32>(sizeLocal[2]));                           // by color
    AMF_RETURN_IF_FAILED(m_pKernelBuildLUT->Enqueue(3, offset, sizeGlobal, sizeLocal));

    index = 0;
    if (m_bUseDX11NativeBuffer)
    {
        DXGI_FORMAT formatDX11 = DXGI_FORMAT_R32_FLOAT;
        AMF_RETURN_IF_FAILED(m_pKernelBuildLUTCenter->SetArgBufferNative(index++, m_pBufferLUTDX11, AMF_ARGUMENT_ACCESS_WRITE));
        AMF_RETURN_IF_FAILED(m_pBufferLUTDX11->SetPrivateData(AMFStructuredBufferFormatGUID, sizeof(DXGI_FORMAT), &formatDX11));

        AMF_RETURN_IF_FAILED(m_pKernelBuildLUTCenter->SetArgBufferNative(index++, m_pBufferLUTPrevDX11, AMF_ARGUMENT_ACCESS_READWRITE));
        AMF_RETURN_IF_FAILED(m_pBufferLUTPrevDX11->SetPrivateData(AMFStructuredBufferFormatGUID, sizeof(DXGI_FORMAT), &formatDX11));

        AMF_RETURN_IF_FAILED(m_pKernelBuildLUTCenter->SetArgBufferNative(index++, m_pBrightnessDX11, AMF_ARGUMENT_ACCESS_WRITE));
        AMF_RETURN_IF_FAILED(m_pBrightnessDX11->SetPrivateData(AMFStructuredBufferFormatGUID, sizeof(DXGI_FORMAT), &formatDX11));
    }
    else
    {
        AMF_RETURN_IF_FAILED(m_pKernelBuildLUTCenter->SetArgBuffer(index++, m_pBufferLUT, AMF_ARGUMENT_ACCESS_WRITE));
        AMF_RETURN_IF_FAILED(m_pKernelBuildLUTCenter->SetArgBuffer(index++, m_pBufferLUTPrev, AMF_ARGUMENT_ACCESS_READWRITE));
        AMF_RETURN_IF_FAILED(m_pKernelBuildLUTCenter->SetArgBuffer(index++, m_pBrightness, AMF_ARGUMENT_ACCESS_WRITE));
    }
    AMF_RETURN_IF_FAILED(m_pKernelBuildLUTCenter->SetArgBuffer(index++, m_pParams, AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(m_pKernelBuildLUTCenter->SetArgInt32(index++, (amf_int32)count));
    AMF_RETURN_IF_FAILED(m_pKernelBuildLUTCenter->SetArgInt32(index++,  (amf_int32)m_iFrameCount));

    AMF_RETURN_IF_FAILED(m_pKernelBuildLUTCenter->GetCompileWorkgroupSize(sizeLocal));
    sizeGlobal[0] = AlignValue((amf_int32)count, static_cast<amf_uint32>(sizeLocal[0]));   //by  corner
    sizeGlobal[1] = AlignValue(3, static_cast<amf_uint32>(sizeLocal[1]));                           // by color
    AMF_RETURN_IF_FAILED(m_pKernelBuildLUTCenter->Enqueue(2, offset, sizeGlobal, sizeLocal));

    if (m_bUseDX11NativeBuffer)
    {
        res = ClearBuffer(m_pHistogramsDX11);
    }
    else
    {
        amf_uint8 nullData = 0;
        res = m_pDevice->FillBuffer(m_pHistograms, 0, m_pHistograms->GetSize(), &nullData, sizeof(nullData));
    }

    AMF_RETURN_IF_FAILED(res, L"ZeroBuffer() failed");

    AMF_RETURN_IF_FAILED(m_pDevice->FlushQueue());

#else // #if GPU_ACCELERATION
    if (m_pDevice->GetMemoryType() == AMF_MEMORY_DX11)
    {
        res = CopyBuffer(m_pHistograms, m_pHistogramsDX11);
        AMF_RETURN_IF_FAILED(res, L"CopyBuffer() failed");
    }

    AMFDataPtr pData;
    res = m_pHistograms->Duplicate(AMF_MEMORY_HOST, &pData);
    AMF_RETURN_IF_FAILED(res, L"Convert(AMF_MEMORY_HOST) failed");
    AMFBufferPtr pHistograms(pData);
    AMFDataPtr pDataPrev;

    AMFDataPtr pDataShifts;
    res = m_pShifts->Duplicate(AMF_MEMORY_HOST, &pDataShifts);
    AMF_RETURN_IF_FAILED(res, L"Convert(AMF_MEMORY_HOST) failed");
    AMFBufferPtr pDataShiftsHost(pDataShifts);

    AMFDataPtr pBrightnessData;
    res = m_pBrightness->Duplicate(AMF_MEMORY_HOST, &pBrightnessData);
    AMF_RETURN_IF_FAILED(res, L"Convert(AMF_MEMORY_HOST) failed");
    AMFBufferPtr pBrightnessHost(pBrightnessData);
    
    

    amf_int32  *pOutHist = (amf_int32*)pHistograms->GetNative();
    float      *pShifts = (float *)pDataShiftsHost->GetNative();
    float      *pBrightness = (float *)pBrightnessHost->GetNative();
    

    m_pBufferLUT->Convert(AMF_MEMORY_HOST);
    m_pBufferLUTPrev->Convert(AMF_MEMORY_HOST);

    float* pLUT = (float*)m_pBufferLUT->GetNative();
    float* pLUTPrev = (float*)m_pBufferLUTPrev->GetNative();

    BuildShifts(pShifts,pOutHist, (amf_int32 *)m_pCorners->GetNative(), (amf_int32 *)&histogramParams, (amf_int32)corners.size(), (amf_int32)m_iFrameCount);
    BuildLUT(pLUT, pLUTPrev, pBrightness, pShifts, (amf_int32 *)m_pCorners->GetNative(), (amf_int32 *)&histogramParams, (amf_int32)corners.size(), (amf_int32)m_iFrameCount);
    BuildLUTCenter(pLUT,pLUTPrev, pBrightness, (amf_int32 *)&histogramParams, count, (amf_int32)m_iFrameCount);

    m_pBufferLUT->Convert(m_pDevice->GetMemoryType());

#if DUMP_HISTOGRAM
//    m_pAllFile = NULL;
    m_pInputFiles.clear();
#endif
    if(m_pDevice->GetMemoryType() == AMF_MEMORY_DX11)
    {
        res = ClearBuffer(m_pHistogramsDX11);
    }
    else
    {
        amf_uint8 pattern = 0;
        res = m_pDevice->FillBuffer(m_pHistograms, 0, m_pHistograms->GetSize(), &pattern, sizeof(pattern));
    }
    AMF_RETURN_IF_FAILED(res, L"ZeroBuffer() failed");
    AMF_RETURN_IF_FAILED(m_pDevice->FlushQueue());

#endif // #if GPU_ACCELERATION
    m_iFrameCount++;
    return AMF_OK;
}


//-------------------------------------------------------------------------------------------------
static float OneCrossCorrelation(
    __global amf_int32 *data1,    // [in]
    __global amf_int32 *data2,    // [in]
    amf_int32 delay,
    amf_int32 maxdelay,
    __global HistogramParameters *params
    )
{
    float r = 0;
    float x[HIST_SIZE];
    float y[HIST_SIZE];

    for(amf_int32 i = 0; i < HIST_SIZE; i++)
    {
        if(i > params->blackCutOffY || i < params->whiteCutOffY)
        {
            x[i]  = (float)data1[i];
            y[i]  = (float)data2[i];
        }
        else
        {
            x[i]  = 0;
            y[i]  = 0;
        }
    }

    if(delay  - maxdelay < maxdelay)
    {
        // Calculate the mean of the two series x[], y[]
        float mx = 0;
        float my = 0;   
        for (amf_int32 i=0 ; i < HIST_SIZE; i++) 
        {
            mx += x[i];
            my += y[i];
        }
        mx /= HIST_SIZE;
        my /= HIST_SIZE;
        for (amf_int32 i=0; i < HIST_SIZE; i++) 
        {
            x[i] -= mx;
            y[i] -= my;
        }
        // Calculate the denominator
        float sx = 0;
        float sy = 0;
        for (amf_int32 i = 0; i < HIST_SIZE; i++) 
        {
            sx += x[i] * x[i];
            sy += y[i] * y[i];
        }
        float denom = sqrtf(sx*sy);

        // Calculate the correlation
        float sxy = 0;
        for (int i = 0; i < HIST_SIZE; i++) 
        {
            int j = i + delay  - maxdelay;
            
            if (j < 0 || j >= HIST_SIZE)
            {
                continue;
            }
            else
            {
                sxy += x[i] * y[j];
            }
                
        }
        r = (float)(sxy / denom);
    }
    return r;
}

//-------------------------------------------------------------------------------------------------
static void FilterDataInplace(amf_int32 *data,amf_int32 count)
{
    // first element is left as is
    for(amf_int32 i = 1; i < count; i++)
    {
        data[i] = data[i-1] + amf_int32(0.3 * (data[i] - data[i-1]));
    }
}
//-------------------------------------------------------------------------------------------------
static void FilterData(amf_int32 *data,amf_int32 *dataPrev,  amf_int32 count, amf_int32 frameCount)
{
    // first element is left as is
    for(amf_int32 i = 0; i < count; i++)
    {
        if(frameCount != 0 )
        {
            data[i] = dataPrev[i] + amf_int32(0.3 * (data[i] - dataPrev[i]));
        }
        dataPrev[i] = data[i];
    }
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT HistogramImpl::CreateBufferFromDX11Native(
    const D3D11_BUFFER_DESC* pDesc,
    D3D11_SUBRESOURCE_DATA* pInitData,
    ID3D11Buffer** ppBuffer)
{
    ID3D11Device* pDevice = (ID3D11Device*)m_pDevice->GetNativeDeviceID();
    HRESULT hr = pDevice->CreateBuffer(pDesc, pInitData, ppBuffer);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"CreateBuffer() failed");
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT HistogramImpl::CreateBufferFromDX11Native(
    const D3D11_BUFFER_DESC* /* pDesc */,
    D3D11_SUBRESOURCE_DATA* /* pInitData */,
    amf_uint64 /* format */,
    AMFBuffer** /* ppBuffer */)
{
//this code will be enabled once the context.h is updated.
#if 0
    ID3D11Device* pDevice = (ID3D11Device*)m_pDevice->GetNativeDeviceID();
    ATL::CComPtr<ID3D11Buffer> pBuffer;
    HRESULT hr = pDevice->CreateBuffer(pDesc, NULL, &pBuffer);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"CreateBuffer() failed");
    AMF_RESULT res = AMFContextExPtr(m_pContext)->CreateBufferFromDX11Native(pBuffer, ppBuffer, NULL);
    AMF_RETURN_IF_FAILED(res, L"CreateBufferFromDX11Native() failed");
    (*ppBuffer)->SetProperty(AMF_STRUCTURED_BUFFER_FORMAT, format);
#endif
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT HistogramImpl::ClearBuffer(ID3D11BufferPtr pBuffer)
{
    //init unit Histo
    D3D11_BUFFER_DESC desc = { 0 };
    pBuffer->GetDesc(&desc);

    ID3D11Device* pDevice = (ID3D11Device*)m_pDevice->GetNativeDeviceID();
    ATL::CComPtr<ID3D11DeviceContext> pContext;
    pDevice->GetImmediateContext(&pContext);
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = pContext->Map(pBuffer, 0, D3D11_MAP_WRITE, 0, &mapped);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"pContext->Map() failed");
    amf_int8* data = (amf_int8*)mapped.pData;
    memset(data, 0x00, desc.ByteWidth);
    pContext->Unmap(pBuffer, 0);

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT HistogramImpl::CopyBuffer(AMFBufferPtr pDst, ID3D11BufferPtr pSrc)
{
    AMF_RETURN_IF_FALSE(pSrc && pDst, AMF_INVALID_POINTER);

    //init unit Histo
    D3D11_BUFFER_DESC desc = { 0 };
    pSrc->GetDesc(&desc);

    ID3D11Device* pDevice = (ID3D11Device*)m_pDevice->GetNativeDeviceID();
    ATL::CComPtr<ID3D11DeviceContext> pContext;
    pDevice->GetImmediateContext(&pContext);
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = pContext->Map(pSrc, 0, D3D11_MAP_READ, 0, &mapped);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"pContext->Map() failed");
    amf_int8* pDataSrc = (amf_int8*)mapped.pData;
    amf_int8* pDataDst = (amf_int8*)pDst->GetNative();
    memcpy(pDataDst, pDataSrc, desc.ByteWidth);
    pContext->Unmap(pSrc, 0);

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
#include "Programs/Histogram.cl.h"
