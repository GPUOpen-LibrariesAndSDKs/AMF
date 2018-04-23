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
#pragma once
#define _USE_MATH_DEFINES
#include "public/include/core/Context.h"
#include "public/include/core/Trace.h"
#include "public/common/InterfaceImpl.h"

#define DUMP_HISTOGRAM 0
//#define DUMP_HISTOGRAM 1

#define GPU_ACCELERATION 1
//#define GPU_ACCELERATION 0

#define DRAW_AREAS 0
//#define DRAW_AREAS 1

#if DUMP_HISTOGRAM
#include "public/common/DataStream.h"
#endif
#include "public/include/components/Component.h"
#include "public/common/AMFFactory.h"
#include <atlbase.h>
#include <d3d11.h>


#define HIST_SIZE   256
namespace amf
{

#pragma pack(push, 1)
struct Rib
{
    amf_int32 channel1;
    amf_int32 side1;        // 0 - left, 1, top, 2 - right, 3 - bottom
    amf_int32 channel2;
    amf_int32 side2;        // 0 - left, 1, top, 2 - right, 3 - bottom
    amf_int32 index;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct Corner
{
    amf_int32 count;
	amf_int32 align1[3];
    amf_int32 channel[4];
    amf_int32 corner[4];    // 0 - lt, 1 - rt, 2 - rb, 3 - lb
    amf_int32 index;
    float     pos[3];
};

#pragma pack(pop)

typedef std::vector<Rib> RibList;
typedef std::vector<Corner> CornerList;

typedef ATL::CComPtr<ID3D11Buffer> ID3D11BufferPtr;

class HistogramImpl : public AMFInterfaceImpl<AMFInterface>
{
public:
    HistogramImpl();
    virtual ~HistogramImpl();

    AMF_RESULT AMF_STD_CALL Init(AMFCompute* pDevice, AMFContext* pContext, amf_int32 count);
    AMF_RESULT AMF_STD_CALL Terminate();
    AMF_RESULT AMF_STD_CALL Build(amf_int32 channel, AMFSurface* pSrcSurface, AMFRect border, AMFSurface* pBorderMap);
    AMF_RESULT AMF_STD_CALL Adjust(amf_int32 count, RibList &ribs, CornerList &corners, float *correction);
    AMF_RESULT AMF_STD_CALL Convert(bool bColorBalance, amf_int32 channel, AMFSurface* pSrcSurface, float *borders, bool sRGB, AMFSurface* pDstSurface);

    static AMF_RESULT AMF_STD_CALL CompileKernels(AMFCompute* pDevice, AMFComponentOptimizationCallback* pCallback);

private:
    AMF_RESULT CreateBufferFromDX11Native(const D3D11_BUFFER_DESC* pDesc, D3D11_SUBRESOURCE_DATA* PInitData, ID3D11Buffer** ppBuffer);
    AMF_RESULT CreateBufferFromDX11Native(const D3D11_BUFFER_DESC* pDesc, D3D11_SUBRESOURCE_DATA* pInitData, amf_uint64 format, AMFBuffer** ppBuffer);
    AMF_RESULT ClearBuffer(ID3D11BufferPtr pBuffer);
    AMF_RESULT CopyBuffer(AMFBufferPtr pDst, ID3D11BufferPtr pSrc);

    AMFContextPtr         m_pContext;
    AMFComputePtr         m_pDevice;
    AMFComputeKernelPtr   m_pKernelHistogram;
    AMFComputeKernelPtr   m_pKernelColor;
    AMFComputeKernelPtr   m_pKernelNV12toRGB;
    AMFComputeKernelPtr   m_pKernelBuildLUT;
    AMFComputeKernelPtr   m_pKernelBuildLUTCenter;
    AMFComputeKernelPtr   m_pKernelBuildShifts;

    amf_int      m_iFrameCount;
    AMFBufferPtr m_pParams;
    AMFBufferPtr m_pCorners;

    AMFBufferPtr m_pHistograms;
    AMFBufferPtr m_pBufferLUT;
    AMFBufferPtr m_pBufferLUTPrev;
    AMFBufferPtr m_pBufferLUTNone;
    AMFBufferPtr m_pShifts;
    AMFBufferPtr m_pBrightness;

    //the following will not be needed once the context.h is updated.
    ID3D11BufferPtr m_pHistogramsDX11;
    ID3D11BufferPtr m_pBufferLUTDX11;
    ID3D11BufferPtr m_pBufferLUTPrevDX11;
    ID3D11BufferPtr m_pBufferLUTNoneDX11;
    ID3D11BufferPtr m_pShiftsDX11;
    ID3D11BufferPtr m_pBrightnessDX11;
    bool            m_bUseDX11NativeBuffer;

#if DUMP_HISTOGRAM
    AMFDataStreamPtr              m_pAllFile;
    std::vector<AMFDataStreamPtr> m_pInputFiles;
#endif
};
typedef AMFInterfacePtr_T<HistogramImpl> HistogramImplPtr;
}
