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
#include "chromakeyImpl.h"
#include "public/include/core/Context.h"
#include "public/include/core/Trace.h"
#include "public/common/TraceAdapter.h"
#include "public/common/AMFFactory.h"
#include "ChromaKeyCapsImpl.h"
#include "Programs/ChromakeyProcess.cl.h"
#include "public/include/Components/ColorSpace.h"
#include "public/include/Components/VideoConverter.h"

#if defined( _M_AMD64)
#include "ProgramsDX11/ChromaKeyProcess_64.h"
#include "ProgramsDX11/ChromaKeyHistUV_64.h"
#include "ProgramsDX11/ChromaKeyHistSort_64.h"
#include "ProgramsDX11/ChromaKeyErode_64.h"
#include "ProgramsDX11/ChromaKeyBlur_64.h"
#include "ProgramsDX11/ChromaKeyBlendYUV_64.h"
#include "ProgramsDX11/ChromaKeyBlendBKYUV_64.h"
#include "ProgramsDX11/ChromaKeyHistUV422_64.h"
#include "ProgramsDX11/ChromaKeyProcess422_64.h"
#include "ProgramsDX11/ChromaKeyBlendYUV422_64.h"
#include "ProgramsDX11/ChromaKeyBlendBKYUV422_64.h"
#include "ProgramsDX11/ChromaKeyHistUV444_64.h"
#include "ProgramsDX11/ChromaKeyProcess444_64.h"
#include "ProgramsDX11/ChromaKeyBlendYUV444_64.h"
#include "ProgramsDX11/ChromaKeyBlendBKYUV444_64.h"
#include "ProgramsDX11/ChromaKeyBlendRGB_64.h"
#include "ProgramsDX11/ChromaKeyBlendBKRGB_64.h"
#include "ProgramsDX11/ChromaKeyRGBtoYUV_64.h"
#include "ProgramsDX11/ChromaKeyV210toY210_64.h"
#else
#include "ProgramsDX11/ChromaKeyProcess_32.h"
#include "ProgramsDX11/ChromaKeyHistUV_32.h"
#include "ProgramsDX11/ChromaKeyHistSort_32.h"
#include "ProgramsDX11/ChromaKeyErode_32.h"
#include "ProgramsDX11/ChromaKeyBlur_32.h"
#include "ProgramsDX11/ChromaKeyBlendYUV_32.h"
#include "ProgramsDX11/ChromaKeyBlendBKYUV_32.h"
#include "ProgramsDX11/ChromaKeyHistUV422_32.h"
#include "ProgramsDX11/ChromaKeyProcess422_32.h"
#include "ProgramsDX11/ChromaKeyBlendYUV422_32.h"
#include "ProgramsDX11/ChromaKeyBlendBKYUV422_32.h"
#include "ProgramsDX11/ChromaKeyHistUV444_32.h"
#include "ProgramsDX11/ChromaKeyProcess444_32.h"
#include "ProgramsDX11/ChromaKeyBlendYUV444_32.h"
#include "ProgramsDX11/ChromaKeyBlendBKYUV444_32.h"
#include "ProgramsDX11/ChromaKeyBlendRGB_32.h"
#include "ProgramsDX11/ChromaKeyBlendBKRGB_32.h"
#include "ProgramsDX11/ChromaKeyRGBtoYUV_32.h"
#include "ProgramsDX11/ChromaKeyV210toY210_32.h"
#endif

#include <atlbase.h>
#include <fstream>

//define export declaration
#ifdef _WIN32
        #if defined(AMF_COMPONENT_VSTITCH_EXPORTS)
            #define AMF_COMPONENT_VSTITCH_LINK __declspec(dllexport)
        #else
            #define AMF_COMPONENT_VSTITCH_LINK __declspec(dllimport)
        #endif
#else // #ifdef _WIN32
    #define AMF_COMPONENT_VSTITCH_LINK
#endif // #ifdef _WIN32

extern "C"
{
    AMF_RESULT AMFCreateComponentChromaKey(amf::AMFContext* pContext, amf::AMFComponentEx** ppComponent)
    {
        *ppComponent = new amf::AMFInterfaceMultiImpl< amf::AMFChromaKeyImpl, amf::AMFComponentEx, amf::AMFContext*>(pContext);
        (*ppComponent)->Acquire();
        return AMF_OK;
    }
}

#define AMF_FACILITY L"AMFChromaKeyImpl"

using namespace amf;

static const AMFEnumDescriptionEntry AMF_OUTPUT_FORMATS_ENUM[] = 
{
    {AMF_SURFACE_UNKNOWN,       L"DEFAULT"},
    {AMF_SURFACE_BGRA,          L"BGRA"}, 
    {AMF_SURFACE_RGBA,          L"RGBA"}, 
    {AMF_SURFACE_ARGB,          L"ARGB"}, 
    {AMF_SURFACE_NV12,          L"NV12"},
    {AMF_SURFACE_YUV420P,       L"YUV420P"},
    {AMF_SURFACE_RGBA_F16,      L"RGBA_FP16"},
    {0,                         NULL}  // This is end of description mark
};

static const AMFEnumDescriptionEntry AMF_MEMORY_ENUM_DESCRIPTION[] = 
{
    {AMF_MEMORY_UNKNOWN,    L"Default"},
    {AMF_MEMORY_DX11,       L"DX11"},
    {AMF_MEMORY_OPENCL,     L"OpenCL"},
    {AMF_MEMORY_HOST,       L"CPU"},
    {0,                     NULL}  // This is end of description mark
};

enum ComputeDeviceEnum
{
    ComputeDeviceDefault,
    ComputeDeviceOpenCL,
    ComputeDeviceHost,
};

static const AMFEnumDescriptionEntry AMF_COMPUTE_DEVICE_ENUM_DESCRIPTION[] = 
{
    {ComputeDeviceDefault,    L"Default"},
    {ComputeDeviceOpenCL,     L"OpenCL"},
    {ComputeDeviceHost,       L"Host"},
    {0,                       NULL}  // This is end of description mark
};

//-------------------------------------------------------------------------------------------------
AMFChromaKeyImpl::AMFChromaKeyInputImpl::AMFChromaKeyInputImpl(AMFChromaKeyImpl* pHost, amf_int32 /* index */) :
m_pHost(pHost)
{
}

//-------------------------------------------------------------------------------------------------
AMFChromaKeyImpl::AMFChromaKeyInputImpl::~AMFChromaKeyInputImpl()
{
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL AMFChromaKeyImpl::AMFChromaKeyInputImpl::SubmitInput(AMFData* pData)
{
    AMF_RETURN_IF_FALSE(pData != NULL, AMF_INVALID_POINTER);
    AMFLock lock(&m_pHost->m_sync);

    if (m_pHost->m_bEof)
    {
        return AMF_EOF;
    }

    AMFSurfacePtr pSurfaceIn(pData);
    // get and validate surface atttributes
    AMF_RETURN_IF_FALSE(pSurfaceIn != NULL, AMF_INVALID_DATA_TYPE, L"Invalid input data, AMFSurface expected");

    if (m_pSurface != NULL)
    {
        return AMF_INPUT_FULL; // this channel already processed for this output - resubmit later
    }

    m_pSurface = pSurfaceIn; // store surface

    ///todo, workaround to map DX11 surface to OpenCL surface 
    if (m_pHost->m_deviceMemoryType == AMF_MEMORY_OPENCL)
    {
        AMF_RETURN_IF_FAILED(pSurfaceIn->Convert(AMF_MEMORY_HOST), L"Failed to interop input surface");
    }

    AMF_RETURN_IF_FAILED(m_pSurface->Convert(m_pHost->m_deviceMemoryType), L"Failed to interop input surface");

    //convert FP16_UNORM to FP16_FLOAT. CPU version is very slow
    AMFSurfacePtr pSurfaceOut;
    m_pHost->ConvertFormat(m_pSurface, &pSurfaceOut);
    m_pSurface = pSurfaceOut;

    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL AMFChromaKeyImpl::AMFChromaKeyInputImpl::OnPropertyChanged(const wchar_t* /* pName */)
{
    AMFLock lock(&m_pHost->m_sync);
}

//-------------------------------------------------------------------------------------------------
AMFChromaKeyImpl::AMFChromaKeyImpl(AMFContext* pContext)
    :m_pContext(pContext),
    m_outputMemoryType(AMF_MEMORY_DX11),
    m_deviceMemoryType(AMF_MEMORY_DX11),
    m_formatIn(AMF_SURFACE_NV12),
    m_formatOut(AMF_SURFACE_RGBA),
    m_iKeyColorCount(1),
    m_iInputCount(1),
    m_iFrameCount(0),
    m_iHistoMax(0),
    m_bUpdateKeyColor(false),
    m_bUpdateKeyColorAuto(true),
    m_bEof(false),
    m_iColorTransferSrc(0),
    m_iColorTransferBK(0),
    m_iColorTransferDst(0)
{
    g_AMFFactory.Init();
    m_sizeIn = { 0 };
    m_sizeOut = { 0 };
    AMFPrimitivePropertyInfoMapBegin
//        AMFPropertyInfoInt64(AMF_CHROMAKEY_COLOR,           AMF_CHROMAKEY_COLOR, 0x00C25D56, 0, INT_MAX, true), //YUV, 194, 135, 86
        AMFPropertyInfoInt64(AMF_CHROMAKEY_COLOR,           AMF_CHROMAKEY_COLOR, AMFChromaKeyInputImpl::KEYCOLORDEF, 0, INT_MAX, true), //YUV, 153, 42, 30, -->10bit
        AMFPropertyInfoInt64(AMF_CHROMAKEY_COLOR_EX,        AMF_CHROMAKEY_COLOR, AMFChromaKeyInputImpl::KEYCOLORDEF, 0, INT_MAX, true), //YUV, 153, 42, 30, -->10bit
        AMFPropertyInfoInt64(AMF_CHROMAKEY_RANGE_MIN,       AMF_CHROMAKEY_RANGE_MIN, 8, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AMF_CHROMAKEY_RANGE_MAX,       AMF_CHROMAKEY_RANGE_MAX, 10, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AMF_CHROMAKEY_RANGE_EXT,       AMF_CHROMAKEY_RANGE_EXT, 40, 0, INT_MAX, true), //disabled
        AMFPropertyInfoInt64(AMF_CHROMAKEY_LUMA_LOW,        AMF_CHROMAKEY_LUMA_LOW,  4, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AMF_CHROMAKEY_SPILL_MODE,      AMF_CHROMAKEY_SPILL_MODE,1, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AMF_CHROMAKEY_RANGE_SPILL,     AMF_CHROMAKEY_RANGE_SPILL, 5, 1, INT_MAX, true),
        AMFPropertyInfoInt64(AMF_CHROMAKEY_INPUT_COUNT,     AMF_CHROMAKEY_INPUT_COUNT, 2, 1, INT_MAX, true),
        AMFPropertyInfoInt64(AMF_CHROMAKEY_POSX,            AMF_CHROMAKEY_POSX, 0, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AMF_CHROMAKEY_POSY,            AMF_CHROMAKEY_POSY, 0, 0, INT_MAX, true),

        AMFPropertyInfoPoint(AMF_CHROMAKEY_COLOR_POS,       AMF_CHROMAKEY_COLOR_POS, 0, 0, true),
        AMFPropertyInfoEnum(AMF_CHROMAKEY_OUT_FORMAT,       AMF_CHROMAKEY_OUT_FORMAT, AMF_SURFACE_RGBA, AMF_OUTPUT_FORMATS_ENUM, false),
        AMFPropertyInfoEnum(AMF_CHROMAKEY_MEMORY_TYPE,      AMF_CHROMAKEY_MEMORY_TYPE, AMF_MEMORY_OPENCL, AMF_MEMORY_ENUM_DESCRIPTION, false),
        AMFPropertyInfoInt64(AMF_CHROMAKEY_COLOR_ADJ_THRE,  AMF_CHROMAKEY_COLOR_ADJ_THRE, 7, 0, 255, true),
        AMFPropertyInfoInt64(AMF_CHROMAKEY_COLOR_ADJ_THRE2, AMF_CHROMAKEY_COLOR_ADJ_THRE2, 52, 0, 255, true),
        AMFPropertyInfoInt64(AMF_CHROMAKEY_BOKEH_RADIUS,    AMF_CHROMAKEY_BOKEH_RADIUS, 5, 0, 255, true),
        AMFPropertyInfoInt64(AMF_CHROMAKEY_COLOR_ADJ,       AMF_CHROMAKEY_COLOR_ADJ, 0, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AMF_CHROMAKEY_EDGE,            AMF_CHROMAKEY_EDGE, 0, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AMF_CHROMAKEY_BOKEH,           AMF_CHROMAKEY_BOKEH, 0, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AMF_CHROMAKEY_BYPASS,          AMF_CHROMAKEY_BYPASS, 0, 0, INT_MAX, true),
        AMFPropertyInfoInt64(AMF_CHROMAKEY_DEBUG,           AMF_CHROMAKEY_DEBUG, 0, 0, INT_MAX, true),
        AMFPrimitivePropertyInfoMapEnd
}

//-------------------------------------------------------------------------------------------------
AMFChromaKeyImpl::~AMFChromaKeyImpl()
{
    Terminate();
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFChromaKeyImpl::Init(AMF_SURFACE_FORMAT /* format */, amf_int32 width, amf_int32 height)
{
    AMFLock lock(&m_sync);

    AMF_RESULT res = AMF_OK;
    m_sizeIn = AMFConstructSize(width, height);
    m_sizeOut = m_sizeIn;

    GetProperty(AMF_CHROMAKEY_MEMORY_TYPE, (amf_int32*)&m_deviceMemoryType);

    if ((m_deviceMemoryType == AMF_MEMORY_OPENCL) && (m_pContext->GetOpenCLContext() == NULL))
    {
        res = m_pContext->InitOpenCL(NULL);
        AMF_RETURN_IF_FAILED(res, L"InitOpenCL() failed!");
    }

    if (!m_Compute)
    {
        if(m_pContext->GetOpenCLContext() != NULL)
        {
            m_deviceMemoryType = amf::AMF_MEMORY_OPENCL;
        }
        else if(m_pContext->GetDX11Device() != NULL)
        {
            m_deviceMemoryType = amf::AMF_MEMORY_DX11;
        }
        else if(m_pContext->GetDX9Device() != NULL)
        {
            m_deviceMemoryType = amf::AMF_MEMORY_COMPUTE_FOR_DX9;
        }
        m_pContext->GetCompute(m_deviceMemoryType, &m_Compute);
    }

    amf_int64 value = 0;
    GetProperty(AMF_CHROMAKEY_COLOR, &value);
    m_iKeyColor[0] = (amf_int32)value;
    if ((m_iKeyColor[0] != 0) && (m_iKeyColor[0] != AMFChromaKeyInputImpl::KEYCOLORDEF)) //disable auto update, UE4
    {
        m_bUpdateKeyColorAuto = false;
    }

    GetProperty(AMF_CHROMAKEY_COLOR_EX, &value);
    m_iKeyColor[1] = (amf_int32)value;
    if ((m_iKeyColor[0] != 0) && (m_iKeyColor[0] != m_iKeyColor[1]))
    {
        m_iKeyColorCount = 2;
    }

    GetProperty(AMF_CHROMAKEY_RANGE_MIN, &value);
    m_iKeyColorRangeMin = (amf_int32)(value * value);
    GetProperty(AMF_CHROMAKEY_RANGE_MAX, &value);
    m_iKeyColorRangeMax = (amf_int32)(value * value);
    m_iKeyColorRangeMax = (m_iKeyColorRangeMax <= m_iKeyColorRangeMin) ? m_iKeyColorRangeMin+1 : m_iKeyColorRangeMax;
    GetProperty(AMF_CHROMAKEY_RANGE_EXT, &value);
    m_iKeyColorRangeExt = (amf_int32)(value * value);
    GetProperty(AMF_CHROMAKEY_RANGE_SPILL, &value);
    m_iSpillRange = (amf_int32)value;
    GetProperty(AMF_CHROMAKEY_LUMA_LOW, &value);
    m_iLumaLow = (amf_int32)value;

    GetProperty(AMF_CHROMAKEY_INPUT_COUNT, &m_iInputCount);
    m_Inputs.clear();

    for (amf_int32 i = 0; i < (amf_int32)m_iInputCount; i++)
    {
        m_Inputs.push_back(new AMFChromaKeyInputImpl(this, i));
    }
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFChromaKeyImpl::Optimize(AMFComponentOptimizationCallback* pCallback)
{
    class Callback : public AMFComponentOptimizationCallback
    {
        AMFComponentOptimizationCallback*   m_pCallback;
        amf_uint                            m_total;
        amf_uint                            m_completed;
        amf_uint                            m_lastTotalPercent;
    public:
        Callback(AMFComponentOptimizationCallback* pCallback, amf_uint total)
            :m_pCallback(pCallback), m_total(total), m_completed(0), m_lastTotalPercent(static_cast<amf_uint>(-1))
        {
        }
        virtual AMF_RESULT AMF_STD_CALL OnComponentOptimizationProgress(amf_uint percent)
        {
            amf_uint total_percent = (100*m_completed + percent) / m_total;
            if(m_lastTotalPercent != total_percent)
            {
                m_lastTotalPercent = total_percent;
                if(m_pCallback)
                {
                    m_pCallback->OnComponentOptimizationProgress(total_percent);
                }
            }
            if(percent == 100)
            {
                m_completed++;
            }
            return AMF_OK;
        }
    };

    amf_set<AMF_MEMORY_TYPE> deviceTypes;
    AMF_RESULT err = AMF_OK;
    bool mclDone = false;//we need mcl once so far
    if(m_pContext->GetDX9Device(amf::AMF_DX9) && !mclDone)
    {
        AMFComputePtr compute;
        err = m_pContext->GetCompute(AMF_MEMORY_COMPUTE_FOR_DX9, &compute);
        if(err == AMF_OK)
        {
            deviceTypes.insert(AMF_MEMORY_COMPUTE_FOR_DX9);
            mclDone = true;
        }
    }
    if(m_pContext->GetDX11Device(AMF_DX11_1) && !mclDone)
    {
        AMFComputePtr compute;
        err = m_pContext->GetCompute(AMF_MEMORY_COMPUTE_FOR_DX11, &compute);
        if(err == AMF_OK)
        {
            deviceTypes.insert(AMF_MEMORY_COMPUTE_FOR_DX11);
            mclDone = true;
        }
    }
    if(m_pContext->GetOpenGLContext())
    {
        m_pContext->InitOpenCL();

    }
    if(m_pContext->GetOpenCLContext())
    {
        deviceTypes.insert(AMF_MEMORY_OPENCL);
    }
    Callback callback(pCallback, (amf_uint)deviceTypes.size());
    for(amf_set<AMF_MEMORY_TYPE>::iterator it = deviceTypes.begin(); it != deviceTypes.end(); ++it)
    {
        AMF_MEMORY_TYPE memoryType = *it;

        amf::AMFComputePtr pDevice;
        err = m_pContext->GetCompute(memoryType, &pDevice);
    }
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFChromaKeyImpl::ReInit(amf_int32 /* width */, amf_int32 /* height */)
{
    return Flush();
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFChromaKeyImpl::Terminate()
{
    m_Inputs.clear();
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFChromaKeyImpl::Drain()
{
    AMFLock lock(&m_sync);
    m_bEof = true;
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFChromaKeyImpl::Flush()
{
    AMFLock lock(&m_sync);
    ReleaseResource();
    ResetInputs();
    m_bEof = false;
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::ReleaseResource()
{
    AMFLock lock(&m_sync);
    m_pSurfaceOut = NULL;
    m_pBufferHistoUV = NULL;
    m_pBufferLuma = NULL;
    m_pSurfaceMask = NULL;
    m_pSurfaceMaskSpill = NULL;
    m_pSurfaceMaskBlur = NULL;
    m_pSurfaceTemp = NULL;
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFChromaKeyImpl::GetInput(amf_int32 index, AMFInput** ppInput)
{
    AMF_RETURN_IF_FALSE(index >= 0 && index < (amf_int32)m_Inputs.size(), AMF_INVALID_ARG, L"Invalid index");
    AMF_RETURN_IF_FALSE(ppInput != NULL, AMF_INVALID_ARG, L"ppInput = NULL");
    *ppInput = m_Inputs[index];
    (*ppInput)->Acquire();
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFChromaKeyImpl::GetOutput(amf_int32 /* index */, AMFOutput** /* ppOutput */)
{
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFChromaKeyImpl::SubmitInput(AMFData* pData)
{
    AMF_RETURN_IF_FALSE(pData != NULL, AMF_INVALID_POINTER);
    AMFLock lock(&m_sync);

    if (m_bEof)
    {
        return AMF_EOF;
    }

    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFChromaKeyImpl::QueryOutput(AMFData** ppData)
{
    AMFLock lock(&m_sync);

    if (m_bEof)
    {
        return AMF_EOF;
    }

    for (amf_size ch = 0; ch < m_Inputs.size(); ch++)
    {
        if (m_Inputs[ch]->m_pSurface == NULL)
        {
            return AMF_REPEAT; // need more input
        }
    }
    AMF_RETURN_IF_FAILED(InitKernels());

    AMF_RESULT ret = AMF_OK;
    AMFSurfacePtr firstInSurface = m_Inputs[0]->m_pSurface;
    amf_int32 width = firstInSurface->GetPlane(AMF_PLANE_Y)->GetWidth();
    amf_int32 height = firstInSurface->GetPlane(AMF_PLANE_Y)->GetHeight();
    m_formatIn = firstInSurface->GetFormat();
    m_iColorTransferSrc = GetColorTransferMode(firstInSurface);

    if (m_iInputCount == 2)
    {
        amf_int32 width2 = m_Inputs[1]->m_pSurface->GetPlane(AMF_PLANE_Y)->GetWidth();
        amf_int32 height2 = m_Inputs[1]->m_pSurface->GetPlane(AMF_PLANE_Y)->GetHeight();
        if (width2 > m_sizeOut.width)
        {
            m_sizeOut.width = width2;
        }
        if (height2 > m_sizeOut.height)
        {
            m_sizeOut.height = height2;
        }

        m_iColorTransferBK = GetColorTransferMode(m_Inputs[1]->m_pSurface);
    }

    //check frame size change
    if ((m_sizeIn.width != width) || (m_sizeIn.height != height))
    {
        AMFTraceWarning(AMF_FACILITY, L"AMFChromaKeyImpl::QueryOutput, frame size changed, sizeOld(%d,%d), sizeNew(%d,%d)\n",
            m_sizeIn.width, m_sizeIn.height, width, height);
        m_sizeIn = AMFConstructSize(width, height);
        m_sizeOut = m_sizeIn;
        ReleaseResource();
    }

    amf_int64 iBypass = 0;
    GetProperty(AMF_CHROMAKEY_BYPASS, &iBypass);
    m_bAlphaFromSrc = (iBypass == 4) && (m_formatIn == AMF_SURFACE_RGBA_F16);
    iBypass = iBypass & 0x03;

    if ((m_formatIn == AMF_SURFACE_RGBA_F16)  &&//EXR etc.
         !m_bAlphaFromSrc)  //alpha from source
    {
        m_formatIn = AMF_SURFACE_Y416;

        if (!m_pSurfaceYUVTemp)
        {
            AMF_RETURN_IF_FAILED(AllocTempSurface(width, height, firstInSurface->GetPts(),
                firstInSurface->GetDuration(), m_formatIn,
                m_deviceMemoryType, firstInSurface->GetFrameType(), &m_pSurfaceYUVTemp),
                L"Failed to allocate output surface");
        }

        ConvertRGBtoYUV(firstInSurface, m_pSurfaceYUVTemp);
        firstInSurface = m_pSurfaceYUVTemp;
    }

    if (!(m_iFrameCount % 240)) //show info for every 10s
    {
        AMFTraceInfo(AMF_FACILITY, L"AMFChromaKeyImpl::QueryOutput, frameCount=%d, format=%d, width=%d, height=%d\n",
            m_iFrameCount, firstInSurface->GetFormat(), width, height);
    }
    amf_int64 formatOut = amf::AMF_SURFACE_NV12;
    if (GetProperty(AMF_CHROMAKEY_OUT_FORMAT, (amf_int32*)&formatOut) == AMF_OK)
    {
        m_formatOut = (AMF_SURFACE_FORMAT)formatOut;
    }

    if (!m_pSurfaceOut) //in case it's not allocated yet.
    {
        ret = AllocOutputSurface(m_sizeOut.width, m_sizeOut.height, firstInSurface->GetPts(), firstInSurface->GetDuration(),
                m_formatOut, m_deviceMemoryType, firstInSurface->GetFrameType(), &m_pSurfaceOut);
        AMF_RETURN_IF_FAILED(ret, L"Failed to allocate output surface");
    }

    if (!(m_iFrameCount % 240)) //reset for every 10s
    {
        m_iHistoMax = 0;
    }
    
    if (!(m_iFrameCount % 10) && m_bUpdateKeyColorAuto && !m_bAlphaFromSrc) //collect histogram every 10 frames
    {
        //using histogram to locate the Green key color
        if (!m_pBufferHistoUV) //in case it's not allocated yet.
        {
            ret = AMFContext1Ptr(m_pContext)->AllocBufferEx(m_deviceMemoryType, 128 * 128 * sizeof(amf_uint32),
                AMF_BUFFER_USAGE_SHADER_RESOURCE | AMF_BUFFER_USAGE_UNORDERED_ACCESS, AMF_MEMORY_CPU_NONE, &m_pBufferHistoUV);
            AMF_RETURN_IF_FAILED(ret, L"Failed to allocate m_pBufferHistoUV");
        }

        if (!m_pBufferHistoSort) //in case it's not allocated yet.
        {
            ret = AMFContext1Ptr(m_pContext)->AllocBufferEx(m_deviceMemoryType, 64 * sizeof(amf_uint32),
                AMF_BUFFER_USAGE_SHADER_RESOURCE | AMF_BUFFER_USAGE_UNORDERED_ACCESS, AMF_MEMORY_CPU_NONE, &m_pBufferHistoSort);
            AMF_RETURN_IF_FAILED(ret, L"Failed to allocate m_pBufferHistoSort");
        }

        if (!m_pBufferLuma) //in case it's not allocated yet.
        {
            ret = AMFContext1Ptr(m_pContext)->AllocBufferEx(m_deviceMemoryType, 64 * sizeof(amf_uint32),
                AMF_BUFFER_USAGE_SHADER_RESOURCE | AMF_BUFFER_USAGE_UNORDERED_ACCESS, AMF_MEMORY_CPU_NONE, &m_pBufferLuma);
            AMF_RETURN_IF_FAILED(ret, L"Failed to allocate m_pBufferHistoUV");
        }

        //histogram
        ret = HistoUV(firstInSurface, m_pBufferHistoUV);
        AMF_RETURN_IF_FAILED(ret);

        ret = HistoUVSort(m_pBufferHistoUV, m_pBufferHistoSort);
        AMF_RETURN_IF_FAILED(ret);
#if 0   //not used yet
        if (m_deviceMemoryType != AMF_MEMORY_DX11)//todo DX11 version
        {
            ret = HistoLocateLuma(firstInSurface, m_pBufferLuma, m_iKeyColor[0]);
            AMF_RETURN_IF_FAILED(ret);
        }
#endif
    }

    AMFSurfacePtr   pSurfaceMask;
    AMFSurfacePtr   pSurfaceMaskSpill;
    AMFSurfacePtr   pSurfaceMaskBlur;
    if (!m_pSurfaceMask)
    {
        AMF_RETURN_IF_FAILED(m_pContext->AllocSurface(m_deviceMemoryType, AMF_SURFACE_GRAY8, width, height, &m_pSurfaceMask));
    }

    if (!m_pSurfaceMaskSpill)
    {
        AMF_RETURN_IF_FAILED(m_pContext->AllocSurface(m_deviceMemoryType, AMF_SURFACE_GRAY8, width, height, &m_pSurfaceMaskSpill));
    }
    if (!m_pSurfaceMaskBlur)
    {
        AMF_RETURN_IF_FAILED(m_pContext->AllocSurface(m_deviceMemoryType, AMF_SURFACE_GRAY8, width, height, &m_pSurfaceMaskBlur));
    }

    if (!m_pSurfaceTemp)
    {
        AMF_RETURN_IF_FAILED(AllocTempSurface(width, height, firstInSurface->GetPts(), firstInSurface->GetDuration(), m_formatIn,
            m_deviceMemoryType, firstInSurface->GetFrameType(), &m_pSurfaceTemp),
            L"Failed to allocate output surface");
    }

    if (!m_bAlphaFromSrc)
    {
        AMF_RETURN_IF_FAILED(Process(firstInSurface, m_pSurfaceTemp, m_pSurfaceMask));
    }
    else
    {
        m_pSurfaceTemp = firstInSurface;
    }

    amf_int64 spillMode = 1;
    GetProperty(AMF_CHROMAKEY_SPILL_MODE, &spillMode);

    iBypass = (iBypass == 2 && m_iInputCount == 1) ? 1 : iBypass;

    m_iColorTransferDst = GetColorTransferModeDst(m_pSurfaceOut, iBypass);
    if (!m_bAlphaFromSrc)
    {
        if (spillMode > 0) //green spill suppression on
        {
            //create the bluring mask for spill
            AMF_RETURN_IF_FAILED(Erode(m_pSurfaceMask, m_pSurfaceMaskSpill, m_iSpillRange / 2, false));
            AMF_RETURN_IF_FAILED(Blur(m_pSurfaceMaskSpill, m_pSurfaceMaskBlur, m_iSpillRange));  //smooth the mask
#if 0
            SaveSurface(m_pSurfaceMask, L"0Mask.bmp");
            SaveSurface(m_pSurfaceMaskSpill, L"1Spill.bmp");
            SaveSurface(m_pSurfaceMaskBlur, L"2Blur.bmp");
#endif
            //spill mask
            AMF_RETURN_IF_FAILED(Erode(m_pSurfaceMask, m_pSurfaceMaskSpill, m_iSpillRange, true));
#if 0
            SaveSurface(m_pSurfaceMaskSpill, L"3Spill2.bmp");
#endif
            //    Dilate(m_pSurfaceMask, m_pSurfaceMaskSpill);
        }
        else
        {
            amf_size org[3] = { 0, 0, 0 };
            amf_size size[3] = { (amf_size)m_pSurfaceMaskSpill->GetPlaneAt(0)->GetWidth(), (amf_size)m_pSurfaceMaskSpill->GetPlaneAt(0)->GetHeight(), 1 };
            int fillColor[4] = { 0 };
            AMF_RETURN_IF_FAILED(m_Compute->FillPlane(m_pSurfaceMaskSpill->GetPlaneAt(0), org, size, fillColor));
            m_pSurfaceMaskBlur = m_pSurfaceMask;
        }
    }

    if (m_Inputs[0]->m_pSurface->GetFormat() == AMF_SURFACE_RGBA_F16)   //RGB blending
    {
        m_pSurfaceTemp = m_Inputs[0]->m_pSurface;
    }

    if (iBypass)
    {
        amf_size org[3] = { 0, 0, 0 };
        amf_size size[3] = { (amf_size)m_pSurfaceMaskBlur->GetPlaneAt(0)->GetWidth(), (amf_size)m_pSurfaceMaskBlur->GetPlaneAt(0)->GetHeight(), 1 };
        int fillColor[4] = { 0 };
        AMF_RETURN_IF_FAILED(m_Compute->FillPlane(m_pSurfaceMaskBlur->GetPlaneAt(0), org, size, fillColor));

        if (m_Inputs[0]->m_pSurface->GetFormat() == AMF_SURFACE_RGBA_F16)
        {
            firstInSurface = m_Inputs[0]->m_pSurface;
        }

        AMFSurfacePtr   pSurfaceSrc = (iBypass == 2) ? m_Inputs[1]->m_pSurface : firstInSurface;
        AMF_RETURN_IF_FAILED(Blend(pSurfaceSrc, m_pSurfaceMaskBlur, m_pSurfaceMaskBlur, m_pSurfaceOut));
    }
    else if (m_iInputCount == 2)
    {
        amf_int32 enableBokeh = 0;
        //two path process
//        GetProperty(AMF_CHROMAKEY_BOKEH, &enableBokeh);
        if (enableBokeh)
        {
            AMFSurfacePtr   pSurfaceBK = m_Inputs[1]->m_pSurface;
            AMFSurfacePtr   pSurfaceTempBK;
            AMF_RETURN_IF_FAILED(AllocTempSurface(m_sizeOut.width, m_sizeOut.height, pSurfaceBK->GetPts(), pSurfaceBK->GetDuration(), m_formatIn, m_deviceMemoryType, pSurfaceBK->GetFrameType(), &pSurfaceTempBK),
                L"Failed to allocate output surface");
            AMF_RETURN_IF_FAILED(Bokeh(pSurfaceBK, pSurfaceTempBK));
            AMF_RETURN_IF_FAILED(BlendBK(m_pSurfaceTemp, pSurfaceTempBK, m_pSurfaceMaskSpill, m_pSurfaceMaskBlur, m_pSurfaceOut));
        }
        else
        {
            AMF_RETURN_IF_FAILED(BlendBK(m_pSurfaceTemp, m_Inputs[1]->m_pSurface, m_pSurfaceMaskSpill, m_pSurfaceMaskBlur, m_pSurfaceOut));
        }
    }
    else
    {
        AMF_RETURN_IF_FAILED(Blend(m_pSurfaceTemp, m_pSurfaceMaskSpill, m_pSurfaceMaskBlur, m_pSurfaceOut));
    }

    firstInSurface = m_Inputs[0]->m_pSurface;
    ///todo, optimize 
    //copy openCL surface data to UE4 DX11 surface 
    if (m_pDataAllocatorCB && (m_outputMemoryType == AMF_MEMORY_DX11) && (m_deviceMemoryType == AMF_MEMORY_OPENCL))
    {
        AMFSurfacePtr   pSurfaceOut;
        ret = AllocOutputSurface(width, height, firstInSurface->GetPts(), firstInSurface->GetDuration(), m_formatOut, m_outputMemoryType, firstInSurface->GetFrameType(), &pSurfaceOut);
        AMF_RETURN_IF_FAILED(m_pSurfaceOut->Convert(m_outputMemoryType));

        amf_int32 width2 = std::min(m_pSurfaceOut->GetPlane(AMF_PLANE_Y)->GetWidth(), pSurfaceOut->GetPlane(AMF_PLANE_Y)->GetWidth());
        amf_int32 height2 = std::min(m_pSurfaceOut->GetPlane(AMF_PLANE_Y)->GetHeight(), pSurfaceOut->GetPlane(AMF_PLANE_Y)->GetHeight());

        AMF_RETURN_IF_FAILED(m_pSurfaceOut->CopySurfaceRegion(pSurfaceOut, 0, 0, 0, 0, width2, height2));
        pSurfaceOut->SetDuration(firstInSurface->GetDuration());
        pSurfaceOut->SetPts(firstInSurface->GetPts());
        firstInSurface->CopyTo(pSurfaceOut, false);   //properities
        (*ppData) = pSurfaceOut.Detach();
        pSurfaceOut.Release();
    }
    else
    {
        m_pSurfaceOut->SetDuration(firstInSurface->GetDuration());
        m_pSurfaceOut->SetPts(firstInSurface->GetPts());
        firstInSurface->CopyTo(m_pSurfaceOut, false);   //properities
        (*ppData) = m_pSurfaceOut.Detach();
    }

    for (amf_size ch = 0; ch < m_Inputs.size(); ch++)
    {
        m_Inputs[ch]->m_pSurface = NULL; // release the surface
    }

    m_pSurfaceOut.Release();

    ResetInputs();
    m_iFrameCount++;
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
void  AMFChromaKeyImpl::ResetInputs()
{
    for (amf_int32 i = 0; i < (amf_int32)m_Inputs.size(); i++)
    {
        m_Inputs[i]->m_pSurface = NULL;
    }

    m_bEof = false;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::AllocOutputSurface()
{
    AMF_RETURN_IF_FAILED(AllocOutputSurface(m_sizeOut.width, m_sizeOut.height, 0, 0, m_formatOut, m_outputMemoryType, amf::AMF_FRAME_PROGRESSIVE, &m_pSurfaceOut),
        L"Failed to allocate output surface");
    AMF_RETURN_IF_FAILED(m_pSurfaceOut->Convert(m_deviceMemoryType));
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::AllocTempSurface(amf_int32 width, amf_int32 height, amf_pts pts, amf_pts duration, AMF_SURFACE_FORMAT format,
    AMF_MEMORY_TYPE memoryType, AMF_FRAME_TYPE type, AMFSurface** ppSurface)
{
    AMFSurfacePtr pSurface;
    if (format == AMF_SURFACE_RGBA_F16) //work around to create RGBA_FP16_UNORM surface
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

        ATL::CComPtr<ID3D11Device> pD3D11Device = (ID3D11Device*)m_pContext->GetDX11Device();
        AMF_RETURN_IF_FALSE(pD3D11Device != NULL, AMF_NOT_INITIALIZED);

        ATL::CComPtr<ID3D11Texture2D> pD3D11Surface;
        HRESULT hr = pD3D11Device->CreateTexture2D(&desc, NULL, &pD3D11Surface);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateTexture2D() failed");
        m_pContext->CreateSurfaceFromDX11Native(pD3D11Surface, &pSurface, NULL);
    }
    else
    {
        AMF_RETURN_IF_FAILED(m_pContext->AllocSurface(memoryType, format, width, height, &pSurface));
    }

    pSurface->SetPts(pts);
    pSurface->SetDuration(duration);
    pSurface->SetFrameType(type);
    *ppSurface = pSurface.Detach();
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::AllocOutputSurface(amf_int32 width, amf_int32 height, amf_pts pts, amf_pts duration, AMF_SURFACE_FORMAT format,
    AMF_MEMORY_TYPE memoryType, AMF_FRAME_TYPE type, AMFSurface** ppSurface)
{
    AMFSurfacePtr pSurface;
    bool isYUV = (format == AMF_SURFACE_NV12) || (format == AMF_SURFACE_P010) || (format == AMF_SURFACE_UYVY);
    if (m_pDataAllocatorCB && (memoryType == m_outputMemoryType) && !isYUV)
    {
        AMF_RETURN_IF_FAILED(m_pDataAllocatorCB->AllocSurface(memoryType, format,
            width, height, 0, 0, &pSurface));
    }
    else
    {
        AMF_RETURN_IF_FAILED(m_pContext->AllocSurface(memoryType, format, width, height, &pSurface));
    }

    pSurface->SetPts(pts);
    pSurface->SetDuration(duration);
    pSurface->SetFrameType(type);
    *ppSurface = pSurface.Detach();
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL AMFChromaKeyImpl::OnPropertyChanged(const wchar_t* pName)
{
    AMFLock lock(&m_sync);

    amf_wstring name(pName);

    if (name == AMF_CHROMAKEY_COLOR_POS)
    {
        GetProperty(AMF_CHROMAKEY_COLOR_POS, &m_posKeyColor);
        m_bUpdateKeyColor = true;
        m_bUpdateKeyColorAuto = false;
    }
    else if (name == AMF_CHROMAKEY_RANGE_MAX)
    {
        amf_int64 value = 0;
        GetProperty(AMF_CHROMAKEY_RANGE_MAX, &value);
        m_iKeyColorRangeMax = (amf_int32)(value * value);

        if (m_iKeyColorRangeMax <= m_iKeyColorRangeMin)
        {
            value++;
            m_iKeyColorRangeMax = (amf_int32)(value * value);
            SetProperty(AMF_CHROMAKEY_RANGE_MAX, value);
        }
    }
    else if (name == AMF_CHROMAKEY_RANGE_MIN)
    {
        amf_int64 value = 0;
        GetProperty(AMF_CHROMAKEY_RANGE_MIN, &value);
        m_iKeyColorRangeMin = (amf_int32)(value * value);

        if (m_iKeyColorRangeMax <= m_iKeyColorRangeMin)
        {
            value++;
            m_iKeyColorRangeMax = (amf_int32)(value * value);
            SetProperty(AMF_CHROMAKEY_RANGE_MAX, value);
        }
    }
    else if (name == AMF_CHROMAKEY_RANGE_SPILL)
    {
        amf_int64 value = 0;
        GetProperty(AMF_CHROMAKEY_RANGE_SPILL, &value);
        m_iSpillRange = (amf_int32)value;
    }
    else if (name == AMF_CHROMAKEY_LUMA_LOW)
    {
        amf_int64 value = 0;
        GetProperty(AMF_CHROMAKEY_LUMA_LOW, &value);
        m_iLumaLow = (amf_int32)(value);
    }
    else if (name == AMF_CHROMAKEY_COLOR)
    {
        amf_int64 value = 0;
        GetProperty(AMF_CHROMAKEY_COLOR, &value);
        m_iKeyColor[0] = (amf_int32)value;
        if ((m_iKeyColor[0] != 0) && (m_iKeyColor[0] != AMFChromaKeyInputImpl::KEYCOLORDEF)) //disable auto update, UE4
        {
            m_bUpdateKeyColorAuto = false;
        }
    }
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFChromaKeyImpl::GetCaps(AMFCaps** ppCaps)
{
    AMFChromaKeyCapsImplPtr pCaps = new AMFChromaKeyCapsImpl();
    AMF_RETURN_IF_FAILED(pCaps->Init(AMFContextPtr(m_pContext)));
    *ppCaps = AMFCapsPtr(pCaps).Detach();
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::Process(AMFSurfacePtr pSurfaceIn, AMFSurfacePtr pSurfaceOut, AMFSurfacePtr pSurfaceMask)
{
    if (m_bUpdateKeyColor)
    {
        AMF_RETURN_IF_FAILED(UpdateKeyColor(pSurfaceIn));
    }
    amf_int32 width = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetWidth();
    amf_int32 height = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetHeight();

    amf_int32 keycolor0 = m_iKeyColor[0];
    amf_int32 keycolor1 = (m_iKeyColorCount > 1) ? m_iKeyColor[1] : m_iKeyColor[0];
    amf_int32 flagDebug = 0;
    GetProperty(AMF_CHROMAKEY_DEBUG, &flagDebug);
    amf_int32 flagEdge= 0;
    GetProperty(AMF_CHROMAKEY_EDGE, &flagEdge);

    amf_int32 flagAdvanced = 0;
    GetProperty(AMF_CHROMAKEY_COLOR_ADJ, &flagAdvanced);
    flagAdvanced = (flagAdvanced == 2) ? 1 : 0;
    bool isYUV422 = IsYUV422(pSurfaceIn);
    bool isYUV444 = (pSurfaceIn->GetFormat() == AMF_SURFACE_Y416) || (pSurfaceIn->GetFormat() == AMF_SURFACE_Y410);
    amf::AMFComputeKernelPtr  pKernelChromaKeyProcess = isYUV422 ? m_pKernelChromaKeyProcess422 : (isYUV444 ? m_pKernelChromaKeyProcess444 : m_pKernelChromaKeyProcess);

    amf_size index = 0;
    SetDX11Format(pSurfaceOut, AMF_PLANE_Y);
    AMF_RETURN_IF_FAILED(pKernelChromaKeyProcess->SetArgPlane(index++, pSurfaceOut->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_WRITE));
    if (!isYUV422 && !isYUV444)
    {
        SetDX11Format(pSurfaceOut, AMF_PLANE_UV);
        AMF_RETURN_IF_FAILED(pKernelChromaKeyProcess->SetArgPlane(index++, pSurfaceOut->GetPlane(AMF_PLANE_UV), AMF_ARGUMENT_ACCESS_WRITE));
    }

    AMF_RETURN_IF_FAILED(pKernelChromaKeyProcess->SetArgPlane(index++, pSurfaceMask->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_WRITE));
    SetDX11Format(pSurfaceIn, AMF_PLANE_Y);
    AMF_RETURN_IF_FAILED(pKernelChromaKeyProcess->SetArgPlane(index++, pSurfaceIn->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_READ));

    if (!isYUV422 && !isYUV444)
    {
        SetDX11Format(pSurfaceIn, AMF_PLANE_UV);
        AMF_RETURN_IF_FAILED(pKernelChromaKeyProcess->SetArgPlane(index++, pSurfaceIn->GetPlane(AMF_PLANE_UV), AMF_ARGUMENT_ACCESS_READ));
    }

    AMF_RETURN_IF_FAILED(pKernelChromaKeyProcess->SetArgInt32(index++, width));
    AMF_RETURN_IF_FAILED(pKernelChromaKeyProcess->SetArgInt32(index++, height));
    AMF_RETURN_IF_FAILED(pKernelChromaKeyProcess->SetArgInt32(index++, keycolor0));
    AMF_RETURN_IF_FAILED(pKernelChromaKeyProcess->SetArgInt32(index++, keycolor1));
    AMF_RETURN_IF_FAILED(pKernelChromaKeyProcess->SetArgFloat(index++, (float)m_iKeyColorRangeMin/255.f/255.f));          //rangeMin
    AMF_RETURN_IF_FAILED(pKernelChromaKeyProcess->SetArgFloat(index++, (float)m_iKeyColorRangeMax / 255.f / 255.f));      //rangeMax
    AMF_RETURN_IF_FAILED(pKernelChromaKeyProcess->SetArgFloat(index++, (float)m_iKeyColorRangeExt / 255.f / 255.f));      //rangeExt,
    AMF_RETURN_IF_FAILED(pKernelChromaKeyProcess->SetArgFloat(index++, (float)m_iLumaLow / 255.f));               //lumaMin,
    AMF_RETURN_IF_FAILED(pKernelChromaKeyProcess->SetArgInt32(index++, flagEdge));                 //edge
    AMF_RETURN_IF_FAILED(pKernelChromaKeyProcess->SetArgInt32(index++, flagAdvanced));             //advanced
    AMF_RETURN_IF_FAILED(pKernelChromaKeyProcess->SetArgInt32(index++, flagDebug));                //debug


    amf_size offset[3] = { 0, 0, 0 };
    amf_size size[3] = { 256, 512, 1 };
    amf_size localSize[3] = { 8, 8, 1 };

    size[0] = localSize[0] * ((width + localSize[0] - 1) / localSize[0]);
    size[1] = localSize[1] * ((height + localSize[1] - 1) / localSize[1]);

    AMF_RETURN_IF_FAILED(pKernelChromaKeyProcess->Enqueue(2, offset, size, localSize));
    m_Compute->FlushQueue();
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::Blur(AMFSurfacePtr pSurfaceIn, AMFSurfacePtr pSurfaceOut, amf_int32 iRadius)
{
    amf_int32 width = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetWidth();
    amf_int32 height = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetHeight();

    amf_size index = 0;
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyBlur->SetArgPlane(index++, pSurfaceOut->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_WRITE));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyBlur->SetArgPlane(index++, pSurfaceIn->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyBlur->SetArgInt32(index++, width));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyBlur->SetArgInt32(index++, height));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyBlur->SetArgInt32(index++, iRadius));


    amf_size offset[3] = { 0, 0, 0 };
    amf_size size[3] = { 256, 512, 1 };
    amf_size localSize[3] = { 8, 8, 1 };

    size[0] = localSize[0] * ((width + localSize[0] - 1) / localSize[0]);
    size[1] = localSize[1] * ((height + localSize[1] - 1) / localSize[1]);

    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyBlur->Enqueue(2, offset, size, localSize));
    m_Compute->FlushQueue();

    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::Erode(AMFSurfacePtr pSurfaceIn, AMFSurfacePtr pSurfaceOut, amf_int32 iErosionSize, bool bDiff)
{
    amf_int32 width = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetWidth();
    amf_int32 height = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetHeight();

    amf_size index = 0;
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyErode->SetArgPlane(index++, pSurfaceOut->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_WRITE));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyErode->SetArgPlane(index++, pSurfaceIn->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyErode->SetArgInt32(index++, width));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyErode->SetArgInt32(index++, height));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyErode->SetArgInt32(index++, 2 * iErosionSize + 1));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyErode->SetArgInt32(index++, bDiff ? 1 : 0));


    amf_size offset[3] = { 0, 0, 0 };
    amf_size size[3] = { 256, 512, 1 };
    amf_size localSize[3] = { 8, 8, 1 };

    size[0] = localSize[0] * ((width + localSize[0] - 1) / localSize[0]);
    size[1] = localSize[1] * ((height + localSize[1] - 1) / localSize[1]);

    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyErode->Enqueue(2, offset, size, localSize));
    m_Compute->FlushQueue();

    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::Dilate(AMFSurfacePtr pSurfaceIn, AMFSurfacePtr pSurfaceOut)
{
    amf_int32 width = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetWidth();
    amf_int32 height = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetHeight();

    amf_size index = 0;
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyDilate->SetArgPlane(index++, pSurfaceOut->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_WRITE));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyDilate->SetArgPlane(index++, pSurfaceIn->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyDilate->SetArgInt32(index++, width));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyDilate->SetArgInt32(index++, height));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyDilate->SetArgInt32(index++, 4*m_iSpillRange+1));


    amf_size offset[3] = { 0, 0, 0 };
    amf_size size[3] = { 256, 512, 1 };
    amf_size localSize[3] = { 8, 8, 1 };

    size[0] = localSize[0] * ((width + localSize[0] - 1) / localSize[0]);
    size[1] = localSize[1] * ((height + localSize[1] - 1) / localSize[1]);

    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyDilate->Enqueue(2, offset, size, localSize));
    m_Compute->FlushQueue();

    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::Blend(AMFSurfacePtr pSurfaceIn, AMFSurfacePtr pSurfaceMaskSpill, AMFSurfacePtr pSurfaceMaskBlur, AMFSurfacePtr pSurfaceOut)
{
    bool outputRGB = pSurfaceOut->GetFormat() != amf::AMF_SURFACE_NV12;
    amf_int32 greenReducing = 0;
    GetProperty(AMF_CHROMAKEY_COLOR_ADJ, &greenReducing);
    amf_int64 threshold = 0;
    GetProperty(AMF_CHROMAKEY_COLOR_ADJ_THRE, &threshold);
    amf_int64 threshold2 = 0;
    GetProperty(AMF_CHROMAKEY_COLOR_ADJ_THRE2, &threshold2);

    amf_int64 byPass = 0;
    GetProperty(AMF_CHROMAKEY_BYPASS, &byPass);

    if (byPass)
    {
        greenReducing = 0;
    }
    amf_int32 width = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetWidth();
    amf_int32 height = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetHeight();
    bool isRGB = pSurfaceIn->GetFormat() == AMF_SURFACE_RGBA_F16;
    bool isYUV422 = IsYUV422(pSurfaceIn);
    bool isYUV444 = (pSurfaceIn->GetFormat() == AMF_SURFACE_Y416) || (pSurfaceIn->GetFormat() == AMF_SURFACE_Y410);
    amf::AMFComputeKernelPtr  pKernelChromaKeyBlend = isYUV422? m_pKernelChromaKeyBlend422 :
            (isYUV444 ? m_pKernelChromaKeyBlend444 : (isRGB ? m_pKernelChromaKeyBlendRGB : m_pKernelChromaKeyBlend));

    amf_size index = 0;
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlend->SetArgPlane(index++, pSurfaceOut->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_WRITE));
    if (!outputRGB)
    {
        AMF_RETURN_IF_FAILED(pKernelChromaKeyBlend->SetArgPlane(index++, pSurfaceOut->GetPlane(AMF_PLANE_UV), AMF_ARGUMENT_ACCESS_WRITE));
    }

    SetDX11Format(pSurfaceIn, AMF_PLANE_Y);
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlend->SetArgPlane(index++, pSurfaceIn->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_READ));
    if (!isYUV422 && !isYUV444 && !isRGB)
    {
        SetDX11Format(pSurfaceIn, AMF_PLANE_UV);
        AMF_RETURN_IF_FAILED(pKernelChromaKeyBlend->SetArgPlane(index++, pSurfaceIn->GetPlane(AMF_PLANE_UV), AMF_ARGUMENT_ACCESS_READ));
    }

    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlend->SetArgPlane(index++, pSurfaceMaskSpill->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlend->SetArgPlane(index++, pSurfaceMaskBlur->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlend->SetArgInt32(index++, width));
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlend->SetArgInt32(index++, height));

    if (outputRGB)
    {
        AMF_RETURN_IF_FAILED(pKernelChromaKeyBlend->SetArgInt32(index++, greenReducing));
        AMF_RETURN_IF_FAILED(pKernelChromaKeyBlend->SetArgInt32(index++, (amf_uint32)threshold));
        AMF_RETURN_IF_FAILED(pKernelChromaKeyBlend->SetArgInt32(index++, (amf_uint32)threshold2));
        amf_int32 keycolor = m_iKeyColor[0];// (m_iKeyColor[0] & 0x00FF00) >> 8;
        AMF_RETURN_IF_FAILED(pKernelChromaKeyBlend->SetArgInt32(index++, keycolor));       //key color
    }
    //color transfer mode. map 8bit to linear space for FP16 rendering
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlend->SetArgInt32(index++, m_iColorTransferSrc));  //linear output for RGB_FP16
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlend->SetArgInt32(index++, m_iColorTransferDst));  //linear output for RGB_FP16
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlend->SetArgInt32(index++, m_bAlphaFromSrc ? 1 : 0));
    amf_int32 flagDebug = 0;
    GetProperty(AMF_CHROMAKEY_DEBUG, &flagDebug);
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlend->SetArgInt32(index++, flagDebug));       //debug

    amf_size offset[3] = { 0, 0, 0 };
    amf_size size[3] = { 256, 512, 1 };
    amf_size localSize[3] = { 8, 8, 1 };

    size[0] = localSize[0] * ((width + localSize[0] - 1) / localSize[0]);
    size[1] = localSize[1] * ((height + localSize[1] - 1) / localSize[1]);

    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlend->Enqueue(2, offset, size, localSize));
    m_Compute->FlushQueue();
//    DumpSurface(pSurfaceOut);
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::BlendBK(
    AMFSurfacePtr pSurfaceIn,
    AMFSurfacePtr pSurfaceInBK,
    AMFSurfacePtr pSurfaceMaskSpill,
    AMFSurfacePtr pSurfaceMaskBlur,
    AMFSurfacePtr pSurfaceOut)
{
    bool outputRGB = pSurfaceOut->GetFormat() != amf::AMF_SURFACE_NV12;
    amf_int32 greenReducing = 0;
    GetProperty(AMF_CHROMAKEY_COLOR_ADJ, &greenReducing);
    amf_int64 threshold = 0;
    GetProperty(AMF_CHROMAKEY_COLOR_ADJ_THRE, &threshold);
    amf_int64 threshold2 = 0;
    GetProperty(AMF_CHROMAKEY_COLOR_ADJ_THRE2, &threshold2);
    amf_int32 posX = 0;
    GetProperty(AMF_CHROMAKEY_POSX, &posX);
    amf_int32 posY = 0;
    GetProperty(AMF_CHROMAKEY_POSY, &posY);

    amf_int32 width = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetWidth();
    amf_int32 height = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetHeight();
    amf_int32 widthBK = pSurfaceInBK->GetPlane(AMF_PLANE_Y)->GetWidth();
    amf_int32 heightBK = pSurfaceInBK->GetPlane(AMF_PLANE_Y)->GetHeight();

    bool isRGB = pSurfaceIn->GetFormat() == AMF_SURFACE_RGBA_F16;
    bool isYUV422 = IsYUV422(pSurfaceIn);
    bool isYUV444 = (pSurfaceIn->GetFormat() == AMF_SURFACE_Y416) || (pSurfaceIn->GetFormat() == AMF_SURFACE_Y410);
    amf::AMFComputeKernelPtr  pKernelChromaKeyBlendBK = isYUV422 ? m_pKernelChromaKeyBlendBK422 :
        (isYUV444 ? m_pKernelChromaKeyBlendBK444 : (isRGB ? m_pKernelChromaKeyBlendBKRGB : m_pKernelChromaKeyBlendBK));

    amf_size index = 0;
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgPlane(index++, pSurfaceOut->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_WRITE));
    if (!outputRGB)
    {
        AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgPlane(index++, pSurfaceOut->GetPlane(AMF_PLANE_UV), AMF_ARGUMENT_ACCESS_WRITE));
    }

    SetDX11Format(pSurfaceIn, AMF_PLANE_Y);
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgPlane(index++, pSurfaceIn->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_READ));
    if (!isYUV422 && !isYUV444 && !isRGB)
    {
        SetDX11Format(pSurfaceIn, AMF_PLANE_UV);
        AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgPlane(index++, pSurfaceIn->GetPlane(AMF_PLANE_UV), AMF_ARGUMENT_ACCESS_READ));
    }

    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgPlane(index++, pSurfaceInBK->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgPlane(index++, pSurfaceInBK->GetPlane(AMF_PLANE_UV), AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgPlane(index++, pSurfaceMaskSpill->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgPlane(index++, pSurfaceMaskBlur->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgInt32(index++, width));
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgInt32(index++, height));
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgInt32(index++, widthBK));
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgInt32(index++, heightBK));

    if (outputRGB)
    {
        AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgInt32(index++, greenReducing));
        AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgInt32(index++, (amf_uint32)threshold));
        AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgInt32(index++, (amf_uint32)threshold2));
    }

    amf_int32 enableBokeh= 0;
    GetProperty(AMF_CHROMAKEY_BOKEH, &enableBokeh);
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgInt32(index++, enableBokeh));       //BK Bokeh

    amf_int32 bokehRadius = 7;
    GetProperty(AMF_CHROMAKEY_BOKEH_RADIUS, &bokehRadius);
    bokehRadius = (bokehRadius <= 0) ? 1 : bokehRadius;
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgInt32(index++, bokehRadius));

    amf_int32 keycolor = m_iKeyColor[0];// (m_iKeyColor[0] & 0x00FF00) >> 8;
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgInt32(index++, keycolor));       //key color
    
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgInt32(index++, posX));       //posX
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgInt32(index++, posY));       //posY
    //color transfer mode. map 8bit to linear space for FP16 rendering
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgInt32(index++, m_iColorTransferSrc));
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgInt32(index++, m_iColorTransferBK));
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgInt32(index++, m_iColorTransferDst));
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgInt32(index++, m_bAlphaFromSrc? 1 : 0));

    amf_int32 flagDebug = 0;
    GetProperty(AMF_CHROMAKEY_DEBUG, &flagDebug);
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->SetArgInt32(index++, flagDebug));       //debug

    amf_size offset[3] = { 0, 0, 0 };
    amf_size size[3] = { 256, 512, 1 };
    amf_size localSize[3] = {8, 8, 1 };

    size[0] = localSize[0] * ((widthBK + localSize[0] - 1) / localSize[0]);
    size[1] = localSize[1] * ((heightBK + localSize[1] - 1) / localSize[1]);
    pKernelChromaKeyBlendBK->GetCompileWorkgroupSize(localSize);
    AMF_RETURN_IF_FAILED(pKernelChromaKeyBlendBK->Enqueue(2, offset, size, localSize));
    m_Compute->FlushQueue();

    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::HistoUV(AMFSurfacePtr pSurfaceIn, AMFBufferPtr pBufferHistoUV)
{
    amf_uint8 nullData = 0;
    AMF_RETURN_IF_FAILED(m_Compute->FillBuffer(pBufferHistoUV, 0, pBufferHistoUV->GetSize(), &nullData, sizeof(nullData)));
    bool isYUV422 = IsYUV422(pSurfaceIn);
    bool isYUV444 = (pSurfaceIn->GetFormat() == AMF_SURFACE_Y416) || (pSurfaceIn->GetFormat() == AMF_SURFACE_Y410);
    AMF_PLANE_TYPE planeType = (isYUV422 || isYUV444) ? AMF_PLANE_PACKED : AMF_PLANE_UV;

    amf_int32 width = pSurfaceIn->GetPlane(planeType)->GetWidth();
    amf_int32 height = pSurfaceIn->GetPlane(planeType)->GetHeight();
    amf_int32 histoSize = 128;

    amf_size index = 0;
    amf::AMFComputeKernelPtr  pKernelChromaKeyHisto = isYUV422 ? m_pKernelChromaKeyHisto422 : (isYUV444 ? m_pKernelChromaKeyHisto444 : m_pKernelChromaKeyHisto);
    AMF_RETURN_IF_FAILED(pKernelChromaKeyHisto->SetArgBuffer(index++, pBufferHistoUV, AMF_ARGUMENT_ACCESS_WRITE));
    SetDX11Format(pSurfaceIn, planeType);
    AMF_RETURN_IF_FAILED(pKernelChromaKeyHisto->SetArgPlane(index++, pSurfaceIn->GetPlane(planeType), AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(pKernelChromaKeyHisto->SetArgInt32(index++, width));
    AMF_RETURN_IF_FAILED(pKernelChromaKeyHisto->SetArgInt32(index++, height));
    AMF_RETURN_IF_FAILED(pKernelChromaKeyHisto->SetArgInt32(index++, histoSize));


    amf_size offset[3] = { 0, 0, 0 };
    amf_size localSize[3] = { 8, 8, 1 };
    AMF_RETURN_IF_FAILED(pKernelChromaKeyHisto->GetCompileWorkgroupSize(localSize));

    amf_size size[3] = { 256, 512, 1 };
    size[0] = localSize[0] * ((width + localSize[0] - 1) / localSize[0]);
    size[1] = localSize[1] * ((height + localSize[1] - 1) / localSize[1]);

    AMF_RETURN_IF_FAILED(pKernelChromaKeyHisto->Enqueue(2, offset, size, localSize));
    m_Compute->FlushQueue();

    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::HistoUVSort(AMFBufferPtr pBufferHistoIn, AMFBufferPtr pBufferHistoOut)
{
    //128x128 --> 64x1
    amf_int32 histoSize = 128 * 128;
    amf_size index = 0;
    AMF_RETURN_IF_FAILED(pBufferHistoIn->Convert(m_deviceMemoryType));
    AMF_RETURN_IF_FAILED(pBufferHistoOut->Convert(m_deviceMemoryType));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyHistoSort->SetArgBuffer(index++, pBufferHistoOut, AMF_ARGUMENT_ACCESS_READWRITE));

    if (m_deviceMemoryType == AMF_MEMORY_DX11)
    {
        DXGI_FORMAT format = DXGI_FORMAT_R32_UINT;
        UINT size = sizeof(format);
        ((ID3D11Buffer*)pBufferHistoIn->GetNative())->SetPrivateData(AMFStructuredBufferFormatGUID, size, &format);
    }

    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyHistoSort->SetArgBuffer(index++, pBufferHistoIn, AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyHistoSort->SetArgInt32(index++, histoSize));
    amf_size offset[3] = { 0, 0, 0 };
    amf_size localSize[3] = {64, 1, 1 };
    amf_size size[3] = {static_cast<amf_size>(histoSize), 1, 1};
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyHistoSort->Enqueue(2, offset, size, localSize));
    m_Compute->FlushQueue();

    amf_uint32  histoData[2] = { 0 };
    ReadData(pBufferHistoOut, sizeof(amf_int32) * 2, (amf_uint8*)&histoData[0]);
    amf_uint32  posU = histoData[0] % 128;
    amf_uint32  posV = histoData[0] / 128;
    amf_uint32  iHistoMax = histoData[1];
//    AMFTraceInfo(AMF_FACILITY, L"HistoUVSort %06x, posU=%02x, , posU=%02x, max=%d, max_new=%d", m_iKeyColor[0], posU, posV, m_iHistoMax, iHistoMax);
    if (m_iHistoMax < iHistoMax)
    {
        m_iKeyColor[0] = (m_iKeyColor[0] & 0x3FF00000) | ((posU << 12) & 0x000FF000) | ((posV << 2) & 0x000003FC);
        m_iHistoMax = iHistoMax;
    }
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::HistoLocateLuma(AMFSurfacePtr pSurfaceIn, AMFBufferPtr pBufferLuma, amf_int32& keyColor)
{
    amf_int32 width = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetWidth();
    amf_int32 height = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetHeight();

    amf_size index = 0;
    AMF_RETURN_IF_FAILED(pBufferLuma->Convert(m_deviceMemoryType));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyHistoLocateLuma->SetArgBuffer(index++, pBufferLuma, AMF_ARGUMENT_ACCESS_READWRITE));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyHistoLocateLuma->SetArgPlane(index++, pSurfaceIn->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyHistoLocateLuma->SetArgPlane(index++, pSurfaceIn->GetPlane(AMF_PLANE_UV), AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyHistoLocateLuma->SetArgInt32(index++, width));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyHistoLocateLuma->SetArgInt32(index++, height));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyHistoLocateLuma->SetArgInt32(index++, keyColor & 0xFF));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyHistoLocateLuma->SetArgInt32(index++, (keyColor & 0xFF00) >> 8));


    amf_size offset[3] = { 0, 0, 0 };
    amf_size localSize[3] = {64, 1, 1 };
    //64 thread per line
    amf_size size[3] = {64, 1, 1 };
    size[1] = (height + localSize[1] - 1) / localSize[1];

    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyHistoLocateLuma->Enqueue(2, offset, size, localSize));
    m_Compute->FlushQueue();

    amf_uint32 iLuma[2] = { 0 };
    ReadData(pBufferLuma, 2 * sizeof(amf_int32), (amf_uint8*)&iLuma);
    if (iLuma[1] > 0)
    {
        iLuma[0] /= iLuma[1];
    }

    keyColor = (keyColor & 0x00FFFF) | ((iLuma[0] << 16) & 0xFF0000);
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::Bokeh(AMFSurfacePtr pSurfaceIn, AMFSurfacePtr pSurfaceOut)
{
    amf_int32 width = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetWidth();
    amf_int32 height = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetHeight();
    amf_int32 bokehRadius = 7;
    GetProperty(AMF_CHROMAKEY_BOKEH_RADIUS, &bokehRadius);

    amf_size index = 0;
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyBokeh->SetArgPlane(index++, pSurfaceOut->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_WRITE));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyBokeh->SetArgPlane(index++, pSurfaceOut->GetPlane(AMF_PLANE_UV), AMF_ARGUMENT_ACCESS_WRITE));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyBokeh->SetArgPlane(index++, pSurfaceIn->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyBokeh->SetArgPlane(index++, pSurfaceIn->GetPlane(AMF_PLANE_UV), AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyBokeh->SetArgInt32(index++, width));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyBokeh->SetArgInt32(index++, height));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyBokeh->SetArgInt32(index++, bokehRadius));


    amf_size offset[3] = { 0, 0, 0 };
    amf_size size[3] = { 256, 512, 1 };
    amf_size localSize[3] = { 8, 8, 1 };

    size[0] = localSize[0] * ((width + localSize[0] - 1) / localSize[0]);
    size[1] = localSize[1] * ((height + localSize[1] - 1) / localSize[1]);
    m_pKernelChromaKeyBokeh->GetCompileWorkgroupSize(localSize);

    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyBokeh->Enqueue(2, offset, size, localSize));
    m_Compute->FlushQueue();

    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::UpdateKeyColor(AMFSurfacePtr pSurfaceIn)
{
    AMF_RESULT res = AMF_OK;
    //copy a portion of the area and lock the small surface to read 
    amf_int32 widthIn = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetWidth();
    amf_int32 heightIn = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetHeight();
    AMFSurfacePtr pSurface;
    amf_int32 width  = 4;
    amf_int32 height = 2;
    res = m_pContext->AllocSurface(pSurfaceIn->GetMemoryType(), pSurfaceIn->GetFormat(), width, height, &pSurface);
    AMF_RETURN_IF_FAILED(res, L"AMFChromaKeyImpl::UpdateKeyColor, allocate surface failed!");
    //offset
    amf_int32 posX = 0;
    GetProperty(AMF_CHROMAKEY_POSX, &posX);
    amf_int32 posY = 0;
    GetProperty(AMF_CHROMAKEY_POSY, &posY);
    AMFPoint posKeyColor = m_posKeyColor;

    amf_int64 byPass = 0;
    GetProperty(AMF_CHROMAKEY_BYPASS, &byPass);

    if (!byPass)
    {
        posKeyColor.x -= posX;
        posKeyColor.y -= posY;
    }

    //make sure the 2x2 aera is valid aera 
    posKeyColor.x = std::max(0, std::min(widthIn - width, posKeyColor.x));
    posKeyColor.y = std::max(0, std::min(heightIn - height, posKeyColor.y));
    
    //NV12 needs to be aligned with 2
    posKeyColor.x = (posKeyColor.x / 2) * 2;
    posKeyColor.y = (posKeyColor.y / 2) * 2;
    //Y210
    if (pSurfaceIn->GetFormat() == AMF_SURFACE_Y210)  //mapped to RGBA_FP16
    {
        posKeyColor.x /= 2;
    }
 
    res = pSurfaceIn->CopySurfaceRegion(pSurface, 0, 0, posKeyColor.x, posKeyColor.y, width, height);
    AMF_RETURN_IF_FAILED(res, L"AMFChromaKeyImpl::UpdateKeyColor, CopySurfaceRegion failed!");

    res = pSurface->Convert(AMF_MEMORY_HOST);
    AMF_RETURN_IF_FAILED(res, L"AMFChromaKeyImpl::UpdateKeyColor, Convert failed!");

    amf_uint32  iKeyColor = 0;  //xx-Y-U-V, 10bit
    amf_uint32  keyColor[3] = {0}; //Y, U, V

    if (pSurface->GetFormat() == AMF_SURFACE_NV12)
    {
        amf_uint8*  pDataY = (amf_uint8*)pSurface->GetPlane(AMF_PLANE_Y)->GetNative();
        amf_uint8*  pDataUV = (amf_uint8*)pSurface->GetPlane(AMF_PLANE_UV)->GetNative();
        keyColor[0] = pDataY[0];
        keyColor[1] = pDataUV[0];
        keyColor[2] = pDataUV[1];
        iKeyColor = ((keyColor[0] << 22) & 0x3FC00000) | ((keyColor[1] << 12) & 0x000FF000) | ((keyColor[2] << 2) & 0x000003FC);
    }
    else if (pSurface->GetFormat() == AMF_SURFACE_P010)
    {
        amf_uint16*  pDataY = (amf_uint16*)pSurface->GetPlane(AMF_PLANE_Y)->GetNative();
        amf_uint16*  pDataUV = (amf_uint16*)pSurface->GetPlane(AMF_PLANE_UV)->GetNative();
        keyColor[0] = pDataY[0];
        keyColor[1] = pDataUV[0];
        keyColor[2] = pDataUV[1];
        iKeyColor = ((keyColor[0] << 14) & 0x3FF00000) | ((keyColor[1] << 4) & 0x000FFC00) | ((keyColor[2] >> 6) & 0x000003FF);
    }
    else if (pSurface->GetFormat() == AMF_SURFACE_UYVY)
    {
        amf_uint8*  pDataUYVY = (amf_uint8*)pSurface->GetPlane(AMF_PLANE_Y)->GetNative();
        keyColor[0] = pDataUYVY[1];
        keyColor[1] = pDataUYVY[0];
        keyColor[2] = pDataUYVY[2];
        iKeyColor = ((keyColor[0] << 22) & 0x3FC00000) | ((keyColor[1] << 12) & 0x000FF000) | ((keyColor[2] << 2) & 0x000003FC);
    }
    else if (pSurface->GetFormat() == AMF_SURFACE_Y210)
//    else if (pSurface->GetFormat() == AMF_SURFACE_RGBA_F16)
    {
        amf_uint16*  pDataUYVY = (amf_uint16*)pSurface->GetPlane(AMF_PLANE_Y)->GetNative();
        keyColor[0] = pDataUYVY[1];//Y
        keyColor[1] = pDataUYVY[0];//U
        keyColor[2] = pDataUYVY[2];//V
        iKeyColor = ((keyColor[0] << 14) & 0x3FF00000) | ((keyColor[1] << 4) & 0x000FFC00) | ((keyColor[2] >>6) & 0x000003FF);
    }
    else if (pSurface->GetFormat() == AMF_SURFACE_Y416)
    {
        amf_uint16*  pDataUYVY = (amf_uint16*)pSurface->GetPlane(AMF_PLANE_Y)->GetNative();
        keyColor[0] = pDataUYVY[1];//Y
        keyColor[1] = pDataUYVY[0];//U
        keyColor[2] = pDataUYVY[2];//V
        iKeyColor = ((keyColor[0] << 14) & 0x3FF00000) | ((keyColor[1] << 4) & 0x000FFC00) | ((keyColor[2] >> 6) & 0x000003FF);
    }

 
    pSurface.Release();
    bool ctrlDown = (GetKeyState(VK_CONTROL)  & (1 << 16)) != 0;
    bool altDown = (GetKeyState(VK_MENU)  & (1 << 16)) != 0;
    bool shiftDown = (GetKeyState(VK_SHIFT)  & (1 << 16)) != 0;

    if (altDown)    //reset
    {
        m_bUpdateKeyColorAuto = true;
        m_iKeyColorCount = 1;
        return AMF_OK;
    }

    if (ctrlDown)
    {
        m_iKeyColorCount = 2;
        m_iKeyColor[1] = iKeyColor;
    }
    else if (!shiftDown)
    {
        m_iKeyColor[0] = iKeyColor;
    }

    m_bUpdateKeyColor = false;
    {
        wchar_t buf[1000];
        swprintf(buf, L"Mouse Position : (%d, %d), keyColor : %08X\n", posKeyColor.x, posKeyColor.y, iKeyColor);
        ::OutputDebugStringW(buf);
    }
    SetProperty(AMF_CHROMAKEY_COLOR, m_iKeyColor[0]);
    SetProperty(AMF_CHROMAKEY_COLOR_EX, m_iKeyColor[1]);
    m_iHistoMax = 0;
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::ReadData(AMFBufferPtr pBufferIn, amf_uint32 length, amf_uint8* pData)
{
    AMF_RESULT res = AMF_OK;
    amf_uint8*  pDataIn = NULL;
    if (m_deviceMemoryType == AMF_MEMORY_DX11)
    {
        AMFBufferPtr pBuffer;
        res = m_pContext->AllocBuffer(pBufferIn->GetMemoryType(), length, &pBuffer);
        AMF_RETURN_IF_FAILED(res, L"AMFChromaKeyImpl::ReadData, AllocBuffer failed!");

        ATL::CComPtr<ID3D11Device> pDevice = (ID3D11Device*)m_pContext->GetDX11Device();
        ATL::CComPtr<ID3D11DeviceContext> pDeviceContext;
        pDevice->GetImmediateContext(&pDeviceContext);
        D3D11_BOX rect = { 0, 0, 0, length, 1, 1 };
        pDeviceContext->CopySubresourceRegion((ID3D11Resource *)pBuffer->GetNative(), 0, 0, 0, 0, (ID3D11Resource *)pBufferIn->GetNative(), 0, &rect);
        AMF_RETURN_IF_FAILED(res, L"AMFChromaKeyImpl::ReadData, CopySubresourceRegion failed!");

#if 1
        ATL::CComPtr<ID3D11DeviceContext> pContext;
        pDevice->GetImmediateContext(&pContext);
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = pContext->Map((ID3D11Resource *)pBuffer->GetNative(), 0, D3D11_MAP_READ, 0, &mapped);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"pContext->Map() failed");
        amf_int8* pDataSrc = (amf_int8*)mapped.pData;
        memcpy(pData, pDataSrc, length);
        pContext->Unmap((ID3D11Resource *)pBuffer->GetNative(), 0);
#else
        res = pBuffer->Convert(AMF_MEMORY_HOST);
        AMF_RETURN_IF_FAILED(res, L"AMFChromaKeyImpl::ReadData, Convert failed!");
        amf_uint8* pDataIn = (amf_uint8*)pBuffer->GetNative();
        memcpy(pData, pDataIn, length);
#endif
        pBuffer.Release();
    }
    else
    {
        AMF_MEMORY_TYPE memType = pBufferIn->GetMemoryType();
        res = pBufferIn->Convert(AMF_MEMORY_HOST);
        AMF_RETURN_IF_FAILED(res, L"AMFChromaKeyImpl::ReadData, Convert failed!");
        pDataIn = (amf_uint8*)pBufferIn->GetNative();
        memcpy(pData, pDataIn, length);
        res = pBufferIn->Convert(memType);
    }
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::InitKernels()
{
    if (m_deviceMemoryType != AMF_MEMORY_DX11)
    {
        return InitKernelsCL();
    }
        AMF_RESULT res = AMF_OK;
    AMFPrograms *pPrograms = NULL;
    amf::AMF_KERNEL_ID kernelID;
    AMF_RETURN_IF_FAILED(g_AMFFactory.GetFactory()->GetPrograms(&pPrograms));

    if (!m_pKernelChromaKeyProcess)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &kernelID, L"CSProcess", "CSProcess", sizeof(ChromaKeyProcess_CS), ChromaKeyProcess_CS, NULL));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyProcess));
    }

    if (!m_pKernelChromaKeyBlur)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &kernelID, L"CSBlur", "CSBlur", sizeof(ChromaKeyBlur_CS), ChromaKeyBlur_CS, NULL));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyBlur));
    }

    if (!m_pKernelChromaKeyErode)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &kernelID, L"CSErode", "CSErode", sizeof(ChromaKeyErode_CS), ChromaKeyErode_CS, NULL));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyErode));
    }

#if 0
    if (!m_pKernelChromaKeyDilate)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelSource(&kernelID, L"Dilate", "Dilate", ChromaKeyProcessCount, ChromaKeyProcess, 0));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyDilate));
    }
#endif

    if (!m_pKernelChromaKeyBlend)
    {
        //todo NV12
        const wchar_t* kernelIDName = (m_formatOut == amf::AMF_SURFACE_NV12) ? L"CSBlend" : L"CSBlendRGB";
        const  char*   kernelName = (m_formatOut == amf::AMF_SURFACE_NV12) ? "CSBlend" : "CSBlendRGB";
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &kernelID, kernelIDName, kernelName, sizeof(ChromaKeyBlendYUV_CS), ChromaKeyBlendYUV_CS, NULL));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyBlend));
    }

    if (!m_pKernelChromaKeyBlendBK)
    {
        //todo NV12
        const wchar_t* kernelIDName = (m_formatOut == amf::AMF_SURFACE_NV12) ? L"BlendBK" : L"CSBlendBKRGB";
        const  char*   kernelName = (m_formatOut == amf::AMF_SURFACE_NV12) ? "BlendBK" : "CSBlendBKRGB";
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &kernelID, kernelIDName, kernelName, sizeof(ChromaKeyBlendBKYUV_CS), ChromaKeyBlendBKYUV_CS, NULL));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyBlendBK));
    }

    if (!m_pKernelChromaKeyHisto)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &kernelID, L"CSHistUV", "CSHistUV", sizeof(ChromaKeyHistUV_CS), ChromaKeyHistUV_CS, NULL));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyHisto));
    }

    if (!m_pKernelChromaKeyHistoSort)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &kernelID, L"CSHistSort", "CSHistSort", sizeof(ChromaKeyHistSort_CS), ChromaKeyHistSort_CS, NULL));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyHistoSort));
    }

    if (!m_pKernelChromaKeyHisto422)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &kernelID, L"CSHistUV422", "CSHistUV422", sizeof(ChromaKeyHistUV422_CS), ChromaKeyHistUV422_CS, NULL));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyHisto422));
    }

    if (!m_pKernelChromaKeyProcess422)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &kernelID, L"CSProcess422", "CSProcess422", sizeof(ChromaKeyProcess422_CS), ChromaKeyProcess422_CS, NULL));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyProcess422));
    }

    if (!m_pKernelChromaKeyBlend422)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &kernelID, L"CSBlendYUV422", "CSBlendYUV422", sizeof(ChromaKeyBlendYUV422_CS), ChromaKeyBlendYUV422_CS, NULL));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyBlend422));
    }

    if (!m_pKernelChromaKeyBlendBK422)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &kernelID, L"CSBlendBKYUV422", "CSBlendBKYUV422", sizeof(ChromaKeyBlendBKYUV422_CS), ChromaKeyBlendBKYUV422_CS, NULL));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyBlendBK422));
    }

    if (!m_pKernelChromaKeyHisto444)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &kernelID, L"CSHistUV444", "CSHistUV444", sizeof(ChromaKeyHistUV444_CS), ChromaKeyHistUV444_CS, NULL));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyHisto444));
    }

    if (!m_pKernelChromaKeyProcess444)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &kernelID, L"CSProcess444", "CSProcess444", sizeof(ChromaKeyProcess444_CS), ChromaKeyProcess444_CS, NULL));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyProcess444));
    }

    if (!m_pKernelChromaKeyBlend444)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &kernelID, L"CSBlendYUV444", "CSBlendYUV444", sizeof(ChromaKeyBlendYUV444_CS), ChromaKeyBlendYUV444_CS, NULL));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyBlend444));
    }

    if (!m_pKernelChromaKeyBlendBK444)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &kernelID, L"CSBlendBKYUV444", "CSBlendBKYUV444", sizeof(ChromaKeyBlendBKYUV444_CS), ChromaKeyBlendBKYUV444_CS, NULL));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyBlendBK444));
    }

    if (!m_pKernelChromaKeyBlendRGB)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &kernelID, L"CSBlendRGB", "CSBlendRGB", sizeof(ChromaKeyBlendRGB_CS), ChromaKeyBlendRGB_CS, NULL));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyBlendRGB));
    }

    if (!m_pKernelChromaKeyBlendBKRGB)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &kernelID, L"CSBlendBKRGB", "CSBlendBKRGB", sizeof(ChromaKeyBlendBKRGB_CS), ChromaKeyBlendBKRGB_CS, NULL));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyBlendBKRGB));
    }

    if (!m_pKernelChromaKeyRGBtoYUV)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &kernelID, L"CSRGBtoYUV", "CSRGBtoYUV", sizeof(ChromaKeyRGBtoYUV_CS), ChromaKeyRGBtoYUV_CS, NULL));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyRGBtoYUV));
    }
    if (!m_pKernelChromaKeyV210toY210)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &kernelID, L"CSV210toY210", "CSV210toY210", sizeof(ChromaKeyV210toY210_CS), ChromaKeyV210toY210_CS, NULL));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyV210toY210));
    }

#if 0
    if (!m_pKernelChromaKeyHistoLocateLuma)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelSource(&kernelID, L"HistoLocateLuma", "HistoLocateLuma", ChromaKeyProcessCount, ChromaKeyProcess, 0));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyHistoLocateLuma));
    }
#endif

#if 0   //not implemented yet
    if (!m_pKernelChromaKeyBokeh)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &kernelID, L"CSBokeh", "CSBokeh", sizeof(ChromaKeyBokeh_CS), ChromaKeyBokeh_CS, NULL));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyBokeh));
    }
#endif
    return res;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::InitKernelsCL()
{
    AMF_RESULT res = AMF_OK;
    AMFPrograms *pPrograms = NULL;
    amf::AMF_KERNEL_ID kernelID;
    AMF_RETURN_IF_FAILED(g_AMFFactory.GetFactory()->GetPrograms(&pPrograms));

    if (!m_pKernelChromaKeyProcess)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelSource(&kernelID, L"Process", "Process", ChromaKeyProcessCount, ChromaKeyProcess, 0));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyProcess));
    }

    if (!m_pKernelChromaKeyBlur)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelSource(&kernelID, L"Blur", "Blur", ChromaKeyProcessCount, ChromaKeyProcess, 0));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyBlur));
    }

    if (!m_pKernelChromaKeyErode)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelSource(&kernelID, L"Erode", "Erode", ChromaKeyProcessCount, ChromaKeyProcess, 0));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyErode));
    }

    if (!m_pKernelChromaKeyDilate)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelSource(&kernelID, L"Dilate", "Dilate", ChromaKeyProcessCount, ChromaKeyProcess, 0));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyDilate));
    }

    if (!m_pKernelChromaKeyBlend)
    {
        const wchar_t* kernelIDName = (m_formatOut == amf::AMF_SURFACE_NV12) ? L"Blend" : L"BlendRGB";
        const  char*   kernelName = (m_formatOut == amf::AMF_SURFACE_NV12) ? "Blend" : "BlendRGB";
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelSource(&kernelID, kernelIDName, kernelName, ChromaKeyProcessCount, ChromaKeyProcess, 0));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyBlend));
    }

    if (!m_pKernelChromaKeyBlendBK)
    {
        const wchar_t* kernelIDName = (m_formatOut == amf::AMF_SURFACE_NV12) ? L"BlendBK" : L"BlendBKRGB";
        const  char*   kernelName = (m_formatOut == amf::AMF_SURFACE_NV12) ? "BlendBK" : "BlendBKRGB";
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelSource(&kernelID, kernelIDName, kernelName, ChromaKeyProcessCount, ChromaKeyProcess, 0));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyBlendBK));
    }


    if (!m_pKernelChromaKeyHisto)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelSource(&kernelID, L"HistoUV", "HistoUV", ChromaKeyProcessCount, ChromaKeyProcess, 0));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyHisto));
    }

    if (!m_pKernelChromaKeyHistoSort)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelSource(&kernelID, L"HistoSort", "HistoSort", ChromaKeyProcessCount, ChromaKeyProcess, 0));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyHistoSort));
    }


    if (!m_pKernelChromaKeyHistoLocateLuma)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelSource(&kernelID, L"HistoLocateLuma", "HistoLocateLuma", ChromaKeyProcessCount, ChromaKeyProcess, 0));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyHistoLocateLuma));
    }
    if (!m_pKernelChromaKeyBokeh)
    {
        AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelSource(&kernelID, L"Bokeh", "Bokeh", ChromaKeyProcessCount, ChromaKeyProcess, 0));
        AMF_RETURN_IF_FAILED(m_Compute->GetKernel(kernelID, &m_pKernelChromaKeyBokeh));
    }
    return res;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::SaveSurface(AMFSurfacePtr pSurfaceIn, std::wstring fileName)
{
    //-------------------------------------------------------------------------------------------------
        AMF_RESULT res = AMF_OK;
        //copy a portion of the area and lock the small surface to read 
        amf_int32 widthIn = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetWidth();
        amf_int32 heightIn = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetHeight();
        if (m_deviceMemoryType == AMF_MEMORY_DX11)
        {
            AMFSurfacePtr pSurface;
            amf_int32 width = widthIn;
            amf_int32 height = heightIn;// / 4;
            res = m_pContext->AllocSurface(pSurfaceIn->GetMemoryType(), pSurfaceIn->GetFormat(), width, height, &pSurface);
            AMF_RETURN_IF_FAILED(res, L"AMFChromaKeyImpl::DumpSurface, allocate surface failed!");
            res = pSurfaceIn->CopySurfaceRegion(pSurface, 0, 0, 0, 0, width, height);
            AMF_RETURN_IF_FAILED(res, L"AMFChromaKeyImpl::DumpSurface, CopySurfaceRegion failed!");

            res = pSurface->Convert(AMF_MEMORY_HOST);
            AMF_RETURN_IF_FAILED(res, L"AMFChromaKeyImpl::DumpSurface, Convert failed!");
            amf_uint8*  pDataY = (amf_uint8*)pSurface->GetPlane(AMF_PLANE_Y)->GetNative();
            amf_int32 pitch = pSurface->GetPlane(AMF_PLANE_Y)->GetHPitch();
            //    SaveToBmp(pDataY, L"Spill.bmp", widthIn, heightIn, 1, pitch);
            SaveToBmp(pDataY, fileName, widthIn, heightIn, 1, pitch);

            pSurface.Release();
        }
        else
        {
            AMF_MEMORY_TYPE memType = pSurfaceIn->GetMemoryType();
            res = pSurfaceIn->Convert(AMF_MEMORY_HOST);
            AMF_RETURN_IF_FAILED(res, L"AMFChromaKeyImpl::DumpSurface, Convert failed!");
            amf_uint8*  pDataY = (amf_uint8*)pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetNative();
            amf_int32 pitch = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetHPitch();
            SaveToBmp(pDataY, fileName, widthIn, heightIn, 1, pitch);
            res = pSurfaceIn->Convert(memType);
        }
        return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::SaveToBmp(amf_uint8* pData, std::wstring fileName, amf_int32 width, amf_int32 height, amf_int32 channels, amf_int32 pitch)
{
    AMF_RESULT res = AMF_OK;
    if (m_iFrameCount % 10) return res;
    if (!pData)  return AMF_FAIL;
    WCHAR pFileNameExt[_MAX_PATH];
    wsprintf(pFileNameExt, L"%03d_", m_iFrameCount);

    std::wstring fileNameFull = std::wstring(L"d://temp//Dump//") + std::wstring(pFileNameExt) + fileName;

    FILE*  fp = _wfopen(fileNameFull.c_str(), L"wb");

    if (fp)
    {
        amf_uint32 sizeImage = channels * width * height;

        BITMAPFILEHEADER bmHeader = { 0 };
        BITMAPINFOHEADER bmInfo = { 0 };
        bmHeader.bfType = MAKEWORD('B', 'M');
        bmHeader.bfSize = sizeImage + sizeof(bmHeader) + sizeof(bmInfo);
        bmHeader.bfOffBits = sizeof(bmHeader) + sizeof(bmInfo);

        bmInfo.biSize = sizeof(bmInfo);
        bmInfo.biWidth = width;
        bmInfo.biHeight = -height;
        bmInfo.biBitCount = (channels == 4) ? 32 : 24;
        bmInfo.biSizeImage = sizeImage;
        bmInfo.biPlanes = 1;

        fwrite(&bmHeader, 1, sizeof(bmHeader), fp);
        fwrite(&bmInfo, 1, sizeof(bmInfo), fp);

        if (channels == 1)
        {
            for (amf_int32 y = 0; y < height; y++, pData += pitch)
            {
                amf_uint8* pDataLine = pData;
                for (amf_int32 x = 0; x < width; x++, pDataLine++)
                {
                    fwrite(pDataLine, 1, 1, fp);
                    fwrite(pDataLine, 1, 1, fp);
                    fwrite(pDataLine, 1, 1, fp);
                }
            }
        }
        else
        {
            amf_uint32 lenLine = channels * width;
            for (amf_int32 y = 0; y < height; y++, pData += pitch)
            {
                fwrite(pData, 1, lenLine, fp);
            }
        }

        fclose(fp);
    }

    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::DumpSurface(AMFSurfacePtr pSurfaceIn)
{
    AMF_RESULT res = AMF_OK;
    //copy a portion of the area and lock the small surface to read 
    amf_int32 widthIn = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetWidth();
    amf_int32 heightIn = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetHeight();
    AMFSurfacePtr pSurface;
    amf_int32 width = widthIn;
    amf_int32 height = heightIn / 4;
    res = m_pContext->AllocSurface(pSurfaceIn->GetMemoryType(), pSurfaceIn->GetFormat(), width, height, &pSurface);
    AMF_RETURN_IF_FAILED(res, L"AMFChromaKeyImpl::DumpSurface, allocate surface failed!");
    res = pSurfaceIn->CopySurfaceRegion(pSurface, 0, 0, 0, 0, width, height);
    AMF_RETURN_IF_FAILED(res, L"AMFChromaKeyImpl::DumpSurface, CopySurfaceRegion failed!");

    res = pSurface->Convert(AMF_MEMORY_HOST);
    AMF_RETURN_IF_FAILED(res, L"AMFChromaKeyImpl::DumpSurface, Convert failed!");

    pSurface.Release();
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::DumpBuffer(AMFBufferPtr pBufferIn)
{
    AMF_RESULT res = AMF_OK;
    amf_size sizeIn = pBufferIn->GetSize();

    AMFBufferPtr pBuffer;
    res = m_pContext->AllocBuffer(pBufferIn->GetMemoryType(), sizeIn, &pBuffer);
    AMF_RETURN_IF_FAILED(res, L"AMFChromaKeyImpl::DumpBuffer, AllocBuffer failed!");

    ATL::CComPtr<ID3D11Device> pDevice = (ID3D11Device*)m_pContext->GetDX11Device();
    ATL::CComPtr<ID3D11DeviceContext> pDeviceContext;
    pDevice->GetImmediateContext(&pDeviceContext);
    pDeviceContext->CopyResource((ID3D11Resource*)pBuffer->GetNative(), (ID3D11Resource*)pBufferIn->GetNative());
    AMF_RETURN_IF_FAILED(res, L"AMFChromaKeyImpl::DumpBuffer, CopyResource failed!");

    res = pBuffer->Convert(AMF_MEMORY_HOST);
    AMF_RETURN_IF_FAILED(res, L"AMFChromaKeyImpl::DumpBuffer, Convert failed!");

    pBuffer.Release();
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
void AMFChromaKeyImpl::SetDX11Format(AMFSurfacePtr pSurface, AMF_PLANE_TYPE planeType)
{
    if (m_deviceMemoryType != AMF_MEMORY_DX11)
    {
        return;
    }
    AMF_SURFACE_FORMAT formatAMF = pSurface->GetFormat();
    DXGI_FORMAT formatDX11 = DXGI_FORMAT_R8_UNORM;
    switch (formatAMF)
    {
    case AMF_SURFACE_NV12:     formatDX11 = (planeType == AMF_PLANE_Y) ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8G8_UNORM; break;
    case AMF_SURFACE_P010:     formatDX11 = (planeType == AMF_PLANE_Y) ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R16G16_UNORM; break;
    case AMF_SURFACE_UYVY:     formatDX11 = DXGI_FORMAT_R8G8B8A8_UNORM; break;
    case AMF_SURFACE_Y210:     formatDX11 = DXGI_FORMAT_R16G16B16A16_UNORM; break;
    case AMF_SURFACE_Y416    : formatDX11 = DXGI_FORMAT_R16G16B16A16_UNORM; break;
    default: return;
    }

    UINT size = sizeof(formatDX11);
    ((ID3D11Texture2D*)pSurface->GetPlane(planeType)->GetNative())->SetPrivateData(AMFStructuredBufferFormatGUID, size, &formatDX11);
}

//-------------------------------------------------------------------------------------------------
bool AMFChromaKeyImpl::IsYUV422(AMFSurfacePtr pSurface)
{
    bool isYUV422 = false;
    if (m_deviceMemoryType == AMF_MEMORY_DX11)
    {
        if (pSurface->GetFormat() == AMF_SURFACE_UYVY)
        {
            isYUV422 = true;
        }
        else if (IsY210(pSurface))
        {
            isYUV422 = true;
        }
    }

    return isYUV422;
}

//-------------------------------------------------------------------------------------------------
bool AMFChromaKeyImpl::IsY210(AMFSurfacePtr pSurface)
{
    bool isY210 = false;
    if (pSurface->GetFormat() == AMF_SURFACE_Y210)
    {
        isY210 = true;
    }
    return isY210;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::ConvertRGBtoYUV(AMFSurfacePtr pSurfaceIn, AMFSurfacePtr pSurfaceOut)
{
    amf_int32 width = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetWidth();
    amf_int32 height = pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetHeight();

    amf_size index = 0;
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyRGBtoYUV->SetArgPlane(index++, pSurfaceOut->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_WRITE));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyRGBtoYUV->SetArgPlane(index++, pSurfaceIn->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyRGBtoYUV->SetArgInt32(index++, width));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyRGBtoYUV->SetArgInt32(index++, height));


    amf_size offset[3] = { 0, 0, 0 };
    amf_size size[3] = { 256, 512, 1 };
    amf_size localSize[3] = { 8, 8, 1 };

    size[0] = localSize[0] * ((width + localSize[0] - 1) / localSize[0]);
    size[1] = localSize[1] * ((height + localSize[1] - 1) / localSize[1]);
    m_pKernelChromaKeyRGBtoYUV->GetCompileWorkgroupSize(localSize);

    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyRGBtoYUV->Enqueue(2, offset, size, localSize));
    m_Compute->FlushQueue();
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::ConvertV210toY210(AMFSurfacePtr pSurfaceIn, AMFSurfacePtr pSurfaceOut)
{
    amf_int32 width = pSurfaceOut->GetPlane(AMF_PLANE_Y)->GetWidth();
    amf_int32 height = pSurfaceOut->GetPlane(AMF_PLANE_Y)->GetHeight();

    amf_size index = 0;
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyV210toY210->SetArgPlane(index++, pSurfaceOut->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_WRITE));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyV210toY210->SetArgPlane(index++, pSurfaceIn->GetPlane(AMF_PLANE_Y), AMF_ARGUMENT_ACCESS_READ));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyV210toY210->SetArgInt32(index++, width));
    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyV210toY210->SetArgInt32(index++, height));

    amf_size offset[3] = { 0, 0, 0 };
    amf_size size[3] = { 256, 512, 1 };
    amf_size localSize[3] = { 8, 8, 1 };

    size[0] = localSize[0] * (((width + localSize[0] - 1) / localSize[0]) + 2)/ 3;   //3 pixels per workitem
    size[1] = localSize[1] * ((height + localSize[1] - 1) / localSize[1]);
    m_pKernelChromaKeyV210toY210->GetCompileWorkgroupSize(localSize);

    AMF_RETURN_IF_FAILED(m_pKernelChromaKeyV210toY210->Enqueue(2, offset, size, localSize));
    m_Compute->FlushQueue();
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
bool AMFChromaKeyImpl::Is8bit(AMFSurfacePtr pSurface)
{
    bool bIs8bit = true;

    if ((pSurface->GetFormat() == AMF_SURFACE_Y210) ||
        (pSurface->GetFormat() == AMF_SURFACE_Y410) ||
        (pSurface->GetFormat() == AMF_SURFACE_Y416) ||
        (pSurface->GetFormat() == AMF_SURFACE_P010) ||
        (pSurface->GetFormat() == AMF_SURFACE_RGBA_F16))
    {
        bIs8bit = false;
    }

    return bIs8bit;
}

//-------------------------------------------------------------------------------------------------
//RGBA_FP16 rendering needs to be in linear
//RGBA rendering needs to be gamma adjusted
amf_uint32 AMFChromaKeyImpl::GetColorTransferMode(AMFSurfacePtr pSurfaceIn)
{
    //0=linear, 1=gamma 2.2, 2=PQ
    amf_uint32 iColorTransfer = 0;

    amf_int64 iTransferFunction = AMF_COLOR_TRANSFER_CHARACTERISTIC_UNDEFINED;
    AMF_RESULT ret = pSurfaceIn->GetProperty(AMF_VIDEO_COLOR_TRANSFER_CHARACTERISTIC, &iTransferFunction);

    if (ret == AMF_OK)
    {
        if (iTransferFunction == AMF_COLOR_TRANSFER_CHARACTERISTIC_BT709)
        {
            iColorTransfer = 1;
        }
        else if (iTransferFunction == AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE2084)
        {
            iColorTransfer = 2;
        }
    }
    else
    {
        bool bIs8bitSrc = Is8bit(pSurfaceIn);
        //todo, get this info from source 
        if (bIs8bitSrc ||
            (pSurfaceIn->GetFormat() == AMF_SURFACE_P010) ||
            (pSurfaceIn->GetFormat() == AMF_SURFACE_Y210))
        {
            iColorTransfer = 1;
        }
    }
    return iColorTransfer;
}

//-------------------------------------------------------------------------------------------------
//RGBA_FP16 rendering needs to be in linear
//RGBA rendering needs to be gamma adjusted
amf_uint32 AMFChromaKeyImpl::GetColorTransferModeDst(
    AMFSurfacePtr pSurfaceOut,    //render surface
    amf_int64 iBypass)
{
    //0=linear, 1=gamma 2.2, 2=PQ
    amf_uint32 iColorTransfer = 0;

    if (pSurfaceOut->GetFormat() == AMF_SURFACE_RGBA_F16)
    {
        //follow the tranfer functions of source
        if (iBypass == 2) //show BK
        {
            m_iColorTransferSrc = m_iColorTransferBK;
        }
    }
    else  //AMF_SURFACE_RGBA, gamm2.2 is needed
    {
        if (iBypass == 2) //show BK
        {
            m_iColorTransferSrc = m_iColorTransferBK;
        }
        else if ((m_iColorTransferBK == 1) &&    //PQ background video is not supported
            (m_iColorTransferSrc == 1))    //PQ background video is not supported
        {
            m_iColorTransferBK = 0;    //pass through
        }

        if (m_iColorTransferSrc == 1)
        {
            m_iColorTransferSrc = 0;    //pass through
        }
        else
        {
            iColorTransfer = 1;
        }
    }

    return iColorTransfer;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFChromaKeyImpl::ConvertFormat(AMFSurfacePtr pSurfaceIn, AMFSurface** ppSurface)
{
    AMF_RESULT res = AMF_OK;
    AMFSurfacePtr pSurfaceTemp;
    if (pSurfaceIn->GetFormat() == AMF_SURFACE_RGBA_F16)
    {
        ID3D11Texture2D *pTexture = (ID3D11Texture2D *)pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetNative();
        D3D11_TEXTURE2D_DESC desc;
        pTexture->GetDesc(&desc);

        //convert DXGI_FORMAT_R16G16B16A16_FLOAT to DXGI_FORMAT_R16G16B16A16_UNORM
        if (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
        {
            AMF_RETURN_IF_FAILED(AllocTempSurface(desc.Width, desc.Height, pSurfaceIn->GetPts(),
                pSurfaceIn->GetDuration(), pSurfaceIn->GetFormat(),
                m_deviceMemoryType, pSurfaceIn->GetFrameType(), &pSurfaceTemp), L"Failed to allocate output surface");

            ATL::CComPtr<ID3D11Device> pDevice = (ID3D11Device*)m_pContext->GetDX11Device();
            ATL::CComPtr<ID3D11DeviceContext> pDeviceContext;
            pDevice->GetImmediateContext(&pDeviceContext);
            pDeviceContext->CopyResource((ID3D11Resource *)pSurfaceTemp->GetPlane(AMF_PLANE_Y)->GetNative(),
                (ID3D11Resource *)pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetNative());
        }
    }
    else if (pSurfaceIn->GetFormat() == AMF_SURFACE_Y210)
    {
        ID3D11Texture2D *pTexture = (ID3D11Texture2D *)pSurfaceIn->GetPlane(AMF_PLANE_Y)->GetNative();
        D3D11_TEXTURE2D_DESC desc;
        pTexture->GetDesc(&desc);
        if (desc.Format == DXGI_FORMAT_R10G10B10A2_UINT)
        {
            UINT width = desc.Width * 3 / 2;
            AMF_RETURN_IF_FAILED(AllocTempSurface(width, desc.Height, pSurfaceIn->GetPts(),
                pSurfaceIn->GetDuration(), pSurfaceIn->GetFormat(),
                m_deviceMemoryType, pSurfaceIn->GetFrameType(), &pSurfaceTemp), L"Failed to allocate output surface");
            AMF_RETURN_IF_FAILED(InitKernels());
            ConvertV210toY210(pSurfaceIn, pSurfaceTemp);
        }
    }

    if (pSurfaceTemp)
    {
        pSurfaceTemp->SetDuration(pSurfaceIn->GetDuration());
        pSurfaceTemp->SetPts(pSurfaceIn->GetPts());
        pSurfaceIn->CopyTo(pSurfaceTemp, false);   //properities
        *ppSurface = pSurfaceTemp.Detach();
    }
    else
    {
        *ppSurface = pSurfaceIn.Detach();
    }
    return res;
}
