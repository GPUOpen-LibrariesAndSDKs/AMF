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

#include "VideoStitchImpl.h"
#include "public/include/core/Context.h"
#include "public/include/core/Trace.h"
#include "public/common/TraceAdapter.h"
#include "VideoStitchCapsImpl.h"
#include <sdkddkver.h>
#include <VersionHelpers.h>

#include "DirectX11/StitchEngineDX11.h"
#include "math.h"

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
    AMF_COMPONENT_VSTITCH_LINK AMF_RESULT AMF_CDECL_CALL AMFCreateComponentInt(amf::AMFContext* pContext, void* /* reserved */ , amf::AMFComponent** ppComponent)
    {
        *ppComponent = new amf::AMFInterfaceMultiImpl< amf::AMFVideoStitchImpl, amf::AMFComponent, amf::AMFContext* >(pContext);
        (*ppComponent)->Acquire();
        return AMF_OK;
    }
}

#define AMF_FACILITY L"AMFVideoStitchImpl"

using namespace amf;

static const AMFEnumDescriptionEntry AMF_OUTPUT_FORMATS_ENUM[] = 
{
    {AMF_SURFACE_UNKNOWN,       L"DEFAULT"},
    {AMF_SURFACE_BGRA,          L"BGRA"}, 
    {AMF_SURFACE_RGBA,          L"RGBA"}, 
    {AMF_SURFACE_RGBA_F16,      L"RGBA_F16"}, 
    { 0,                        NULL }  // This is end of description mark
};

static const AMFEnumDescriptionEntry AMF_OUTPUT_MODE_ENUM[] = 
{
    {AMF_VIDEO_STITCH_OUTPUT_MODE_PREVIEW,         L"Preview"},
    {AMF_VIDEO_STITCH_OUTPUT_MODE_EQUIRECTANGULAR, L"Equrectangular"},
    {AMF_VIDEO_STITCH_OUTPUT_MODE_CUBEMAP,         L"Cubemap"},
    {0,                                            NULL}      // This is end of description mark
};

static const AMFEnumDescriptionEntry AMF_LENS_MODE_ENUM[] = 
{
    { AMF_VIDEO_STITCH_LENS_RECTILINEAR,       L"Normal" },
    { AMF_VIDEO_STITCH_LENS_FISHEYE_FULLFRAME, L"FishEye" },
    { AMF_VIDEO_STITCH_LENS_FISHEYE_CIRCULAR,  L"CircularFishEye" },
    {0,                                        NULL}      // This is end of description mark
};

static const AMFEnumDescriptionEntry AMF_MEMORY_ENUM_DESCRIPTION[] = 
{
    {AMF_MEMORY_UNKNOWN,            L"Default"},
    {AMF_MEMORY_DX11,               L"DX11"},
    { AMF_MEMORY_OPENCL,            L"OpenCL" },
    { AMF_MEMORY_HOST,              L"CPU" },
    {0,                             NULL}  // This is end of description mark
};

static const AMFEnumDescriptionEntry AMF_COMPUTE_DEVICE_ENUM_DESCRIPTION[] = 
{
    {AMF_MEMORY_UNKNOWN,            L"Default"},
    {AMF_MEMORY_DX11,               L"DX11"},
    {AMF_MEMORY_OPENCL,             L"OpenCL"},
    {0,                             NULL}  // This is end of description mark
};

//-------------------------------------------------------------------------------------------------
AMFVideoStitchImpl::AMFVideoStitchImpl(AMFContext* pContext)
    :m_pContext(pContext),
    m_formatIn(AMF_SURFACE_NV12),
    m_formatOut(AMF_SURFACE_NV12),
    m_width(0),
    m_height(0),
    m_outputMemoryType(AMF_MEMORY_UNKNOWN),
    m_deviceMemoryType(AMF_MEMORY_HOST),
    m_deviceHistogramMemoryType(AMF_MEMORY_UNKNOWN),
    m_eof(false),
    m_frameInd(0),
    m_bColorBalance(true)
{
    g_AMFFactory.Init();
    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoEnum(AMF_VIDEO_STITCH_OUTPUT_FORMAT,     L"Output Format" , AMF_SURFACE_NV12, AMF_OUTPUT_FORMATS_ENUM, false),
        AMFPropertyInfoEnum(AMF_VIDEO_STITCH_MEMORY_TYPE ,      L"Output Memory Type", AMF_MEMORY_UNKNOWN, AMF_MEMORY_ENUM_DESCRIPTION, false),
        AMFPropertyInfoEnum(AMF_VIDEO_STITCH_COMPUTE_DEVICE ,   L"Compute Device", AMF_MEMORY_UNKNOWN, AMF_COMPUTE_DEVICE_ENUM_DESCRIPTION, false),
        AMFPropertyInfoInt64(AMF_VIDEO_STITCH_INPUTCOUNT,       L"Number of inputs", 1, 0, 100, true),
        AMFPropertyInfoSize(AMF_VIDEO_STITCH_OUTPUT_SIZE,       L"Output Size", AMFConstructSize(0, 0), AMFConstructSize(1, 1), AMFConstructSize(0x7fffffff, 0x7fffffff), false),
        AMFPropertyInfoDouble(AMF_VIDEO_STITCH_VIEW_ROTATE_X,   L"Angle X", 0.0, -DBL_MAX, DBL_MAX, true),
        AMFPropertyInfoDouble(AMF_VIDEO_STITCH_VIEW_ROTATE_Y,   L"Angle Y", 0.0, -DBL_MAX, DBL_MAX, true),
        AMFPropertyInfoDouble(AMF_VIDEO_STITCH_VIEW_ROTATE_Z,   L"Angle Z", 0.0, -DBL_MAX, DBL_MAX, true),
        AMFPropertyInfoBool(AMF_VIDEO_STITCH_WIRE_RENDER,       L"Fill", false, true),
        AMFPropertyInfoBool(AMF_VIDEO_STITCH_COLOR_BALANCE,     L"ColorBalance", true, true),
        AMFPropertyInfoEnum(AMF_VIDEO_STITCH_OUTPUT_MODE,       L"Output Mode" , AMF_VIDEO_STITCH_OUTPUT_MODE_PREVIEW, AMF_OUTPUT_MODE_ENUM, true),
        AMFPropertyInfoBool(AMF_VIDEO_STITCH_COMBINED_SOURCE, AMF_VIDEO_STITCH_COMBINED_SOURCE, false, false),
    AMFPrimitivePropertyInfoMapEnd
}
//-------------------------------------------------------------------------------------------------
AMFVideoStitchImpl::~AMFVideoStitchImpl()
{
    Terminate();
    g_AMFFactory.Terminate();
}

static bool amf_is_win7()
{
    bool  bVer = false;

#if !defined(METRO_APP)
    if (IsWindowsVersionOrGreater(10, 0, 0))
    {
        bVer = false;
    }
    else if (IsWindowsVersionOrGreater(6, 3, 0))
    {
        bVer = false;
    }
    else if (IsWindowsVersionOrGreater(6, 2, 0))
    {
        bVer = false;
    }
    else if (IsWindowsVersionOrGreater(6, 1, 0))
    {
        bVer = true;
    }
#endif
    return bVer;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFVideoStitchImpl::Init(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height)
{
    AMFLock lock(&m_sync);

    AMF_RESULT res = AMF_OK;

    m_width = width;
    m_height = height;
    m_formatIn = format;

    GetProperty(AMF_VIDEO_STITCH_COMBINED_SOURCE, &m_bCombinedSource);

    amf_int64 formatOut = AMF_SURFACE_UNKNOWN;
    if(GetProperty(AMF_VIDEO_STITCH_OUTPUT_FORMAT, (amf_int32*)&formatOut) == AMF_OK)
    {
        m_formatOut = (AMF_SURFACE_FORMAT)formatOut;
    }

    m_eof = false;
    m_frameInd = 0;

    GetProperty(AMF_VIDEO_STITCH_OUTPUT_SIZE, &m_outputSize);
    if(m_outputSize.width == 0 || m_outputSize.height == 0)
    {
        m_outputSize.width = m_width;
        m_outputSize.height = m_height;
    }

    amf_int32 computeDevice = AMF_MEMORY_UNKNOWN;
    AMFPropertyStorage::GetProperty(AMF_VIDEO_STITCH_COMPUTE_DEVICE, &computeDevice);
    m_deviceHistogramMemoryType = (AMF_MEMORY_TYPE)computeDevice;

    amf_int32 outputMemoryType = AMF_MEMORY_UNKNOWN;
    GetProperty(AMF_VIDEO_STITCH_MEMORY_TYPE, &outputMemoryType);
    m_outputMemoryType = (AMF_MEMORY_TYPE)outputMemoryType;

    if(m_outputMemoryType == AMF_MEMORY_UNKNOWN)
    {
        if(m_pContext->GetDX11Device(AMF_DX11_0) != NULL && !amf_is_win7())
        {
            m_outputMemoryType = AMF_MEMORY_DX11;
        }
        else if(m_pContext->GetDX9Device(amf::AMF_DX9) != NULL)
        {
            m_outputMemoryType = AMF_MEMORY_DX9;
        }
        else
        {
            m_outputMemoryType = AMF_MEMORY_OPENCL;
        }
    }

    switch((int)m_outputMemoryType)
    {
    case AMF_MEMORY_DX11:
        AMF_RETURN_IF_FALSE(m_formatIn == AMF_SURFACE_NV12 || m_formatIn == AMF_SURFACE_BGRA || m_formatIn == AMF_SURFACE_RGBA, AMF_INVALID_FORMAT, 
            L"Invalid DX11 input format. Expected AMF_SURFACE_NV12 or AMF_SURFACE_BGRA or AMF_SURFACE_RGBA");
        AMF_RETURN_IF_FALSE(m_formatOut == AMF_SURFACE_NV12 || m_formatOut == AMF_SURFACE_BGRA || m_formatOut == AMF_SURFACE_RGBA || m_formatOut == AMF_SURFACE_RGBA_F16, AMF_INVALID_FORMAT, 
            L"Invalid DX11 output format. Expected AMF_SURFACE_NV12 or AMF_SURFACE_BGRA or AMF_SURFACE_RGBA or AMF_SURFACE_RGBA_F16");
        if(m_pContext->GetDX11Device(AMF_DX11_0) == NULL)
        {
            AMF_RETURN_IF_FAILED(m_pContext->InitDX11(NULL));
        }
        m_deviceMemoryType = AMF_MEMORY_DX11;
        if(m_deviceHistogramMemoryType == AMF_MEMORY_UNKNOWN)
        {
            if(m_pContext->GetOpenCLContext() != 0 )
            {
                m_deviceHistogramMemoryType = AMF_MEMORY_OPENCL;
            }
            else
            {
                m_deviceHistogramMemoryType = AMF_MEMORY_DX11;
            }
        }
        break;
    case AMF_MEMORY_OPENGL:
        AMF_RETURN_IF_FALSE(m_formatIn == AMF_SURFACE_NV12 || m_formatIn == AMF_SURFACE_BGRA || m_formatIn == AMF_SURFACE_RGBA, AMF_INVALID_FORMAT, 
            L"Invalid OpenGL input format. Expected AMF_SURFACE_BGRA or AMF_SURFACE_RGBA");
        AMF_RETURN_IF_FALSE(m_formatOut == AMF_SURFACE_BGRA || m_formatOut == AMF_SURFACE_RGBA, AMF_INVALID_FORMAT, 
            L"Invalid OpenGL output format. Expected AMF_SURFACE_BGRA or AMF_SURFACE_RGBA");
        if(m_pContext->GetOpenGLContext() == NULL)
        {
            return AMF_NO_DEVICE;
        }
        m_deviceMemoryType = AMF_MEMORY_OPENGL;
        m_deviceHistogramMemoryType = AMF_MEMORY_OPENCL;
        break;
    case AMF_MEMORY_HOST:
        m_deviceMemoryType = AMF_MEMORY_HOST;
        m_deviceHistogramMemoryType = AMF_MEMORY_OPENCL;
        break;
    case AMF_MEMORY_COMPUTE_FOR_DX9:
        m_deviceMemoryType = AMF_MEMORY_COMPUTE_FOR_DX9;
        m_deviceHistogramMemoryType = AMF_MEMORY_COMPUTE_FOR_DX9;
        break;
    case AMF_MEMORY_COMPUTE_FOR_DX11:
        m_deviceMemoryType = AMF_MEMORY_COMPUTE_FOR_DX11;
        if(m_deviceHistogramMemoryType == AMF_MEMORY_UNKNOWN)
        {
            m_deviceHistogramMemoryType = AMF_MEMORY_COMPUTE_FOR_DX11;
        }
        break;
    case AMF_MEMORY_OPENCL:
    default:
        m_deviceMemoryType = AMF_MEMORY_OPENCL;
        m_deviceHistogramMemoryType = AMF_MEMORY_OPENCL;
        break;
    }

    // init OCL/COMPUTE if needed
    AMFComputePtr compute;
   
    res = m_pContext->GetCompute(m_deviceHistogramMemoryType, &compute);
    if(res != AMF_OK)
    {
        m_deviceHistogramMemoryType = AMF_MEMORY_OPENCL;
        AMF_RETURN_IF_FAILED(m_pContext->GetCompute(m_deviceHistogramMemoryType, &compute));
        
    }

    std::vector<AMFPropertyStorage*> inputs;
    inputs.resize(m_InputStatus.size());
    for(size_t i = 0; i< m_InputStatus.size(); i++)
    {
        inputs[i] = AMFPropertyStoragePtr(m_InputStatus[i]);
    }

    m_pEngine = new StitchEngineDX11(GetContext());
    res = m_pEngine->Init(m_formatIn, m_width, m_height, m_formatOut, m_outputSize.width, m_outputSize.height, this, &inputs[0]);

    m_pContext->GetCompute(m_deviceHistogramMemoryType, &m_pDevice);
    AMF_RETURN_IF_FALSE(m_pDevice != NULL, AMF_NO_DEVICE, L"Wrong device for %d", AMFGetMemoryTypeName(m_deviceHistogramMemoryType));

    m_pHistogram = new HistogramImpl();
    res = m_pHistogram->Init(m_pDevice, m_pContext, (amf_int32)m_InputStatus.size());
    AMF_RETURN_IF_FAILED(res, L"HistogramImpl::Init() failed");

    GetProperty(AMF_VIDEO_STITCH_COLOR_BALANCE, &m_bColorBalance);
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFVideoStitchImpl::Optimize(AMFComponentOptimizationCallback* pCallback)
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
            HistogramImpl::CompileKernels(pDevice, &callback);
        }
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFVideoStitchImpl::ReInit(amf_int32 width,amf_int32 height)
{
    AMF_SURFACE_FORMAT format = m_formatIn;
    Terminate();
    return Init( format, width, height);
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFVideoStitchImpl::Terminate()
{
    if(m_pHistogram  != NULL)
    {
        m_pHistogram->Terminate();
        m_pHistogram = NULL;
    }

    m_pTempSurface = NULL;
    m_tmpOutputSurface = NULL;
    m_InputStatus.clear();

    if(m_pEngine != NULL)
    {
        m_pEngine->Terminate();
        m_pEngine = NULL;
    }

    m_pDevice = NULL;
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFVideoStitchImpl::Drain()
{
    AMFLock lock(&m_sync);
    m_eof = true;
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFVideoStitchImpl::Flush()
{
    AMFLock lock(&m_sync);
    for(amf_int32 i = 0; i < (amf_int32)m_InputStatus.size(); i++)
    {
        m_InputStatus[i]->m_pSurface = NULL;
    }
    m_eof = false;
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFVideoStitchImpl::SubmitInput(AMFData* /* pData */)
{
    return AMF_UNEXPECTED;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFVideoStitchImpl::Compose(AMFSurface **ppSurfaceOut)
{
    AMF_RESULT res = AMF_OK;
    // allocate output
    GetProperty(AMF_VIDEO_STITCH_OUTPUT_SIZE, &m_outputSize);
    if(m_outputSize.width == 0 || m_outputSize.height == 0)
    {
        m_outputSize.width = m_width;
       m_outputSize.height = m_height;
    }
    AMFSurfacePtr firstInSurface = m_InputStatus[0]->m_pSurface;

    AMF_RETURN_IF_FAILED(AllocOutputSurface(firstInSurface->GetPts(), firstInSurface->GetDuration(), firstInSurface->GetFrameType(), ppSurfaceOut), L"Failed to allocate output surface");
    AMF_RETURN_IF_FAILED((*ppSurfaceOut)->Interop(m_deviceMemoryType), L"Failed to interop output surface");
    firstInSurface->CopyTo(*ppSurfaceOut, false);
    
    amf::AMFContext::AMFDX11Locker dxlock(m_pContext);
    m_pEngine->StartFrame(*ppSurfaceOut);

    std::vector<float> correction;
    correction.resize(m_InputStatus.size() * 3, 0);

    if(m_bColorBalance)
    {
        res = m_pHistogram->Adjust((amf_int32)m_InputStatus.size(), m_pEngine->GetRibs(), m_pEngine->GetCorners(), &correction[0]);
        AMF_RETURN_IF_FAILED(res, L"HistogramImpl::Adjust() failed");
    }

    // submit all cashed inputs for composition - order is important 
    for(amf_size ch = 0; ch < m_InputStatus.size(); ch++)
    {
        AMFSurfacePtr surfaceIn = m_InputStatus[ch]->m_pSurface;
        AMFSurfacePtr surfaceOut;

        bool sRGB = m_formatOut == AMF_SURFACE_RGBA_F16;
        AMF_SURFACE_FORMAT  formatIntermediate = sRGB ? AMF_SURFACE_RGBA : m_formatOut;


        res = m_pContext->AllocSurface(m_deviceMemoryType, formatIntermediate, surfaceIn->GetPlaneAt(0)->GetWidth(), surfaceIn->GetPlaneAt(0)->GetHeight(), &surfaceOut);
        AMF_RETURN_IF_FAILED(res, L"AllocSurface() failed");
        if(m_deviceHistogramMemoryType == AMF_MEMORY_OPENCL)
        {
            m_InputStatus[ch]->m_pSurfaceOut = surfaceOut;
        }
        res = m_pHistogram->Convert(m_bColorBalance, (amf_int32)ch, m_InputStatus[ch]->m_pSurface, m_pEngine->GetTexRect((amf_int32)ch), sRGB, surfaceOut);
        AMF_RETURN_IF_FAILED(res, L"HistogramImpl::Convert() failed");
        // submit kernel
        res = m_pEngine->ProcessStream((int)ch, surfaceOut);
        
    }

    m_pEngine->EndFrame(m_pDataAllocatorCB == NULL);

    for(amf_size ch = 0; ch < m_InputStatus.size(); ch++)
    {
        m_InputStatus[ch]->m_pSurface = NULL; // release the surface
    }    
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFVideoStitchImpl::QueryOutput(AMFData** ppData)
{
    AMFLock lock(&m_sync);

    if(m_eof)
    {
        return AMF_EOF;
    }
    for(amf_size ch = 0; ch < m_InputStatus.size(); ch++)
    {
        if(m_InputStatus[ch]->m_pSurface == NULL)
        {
            return AMF_REPEAT; // need more input
        }
    }

    AMFSurfacePtr       pSurfaceOut;
    Compose(&pSurfaceOut);
    pSurfaceOut->Convert(m_outputMemoryType);
    (*ppData) = pSurfaceOut.Detach();
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFVideoStitchImpl::AllocOutputSurface(amf_pts pts, amf_pts duration, AMF_FRAME_TYPE type, AMFSurface** ppSurface)
{
    AMFSurfacePtr pSurface;
    
    amf_int64 outputMode = AMF_VIDEO_STITCH_OUTPUT_MODE_PREVIEW;
    GetProperty(AMF_VIDEO_STITCH_OUTPUT_MODE, &outputMode);

    if(m_pDataAllocatorCB)
    {
        AMF_RETURN_IF_FAILED(m_pDataAllocatorCB->AllocSurface(m_outputMemoryType, m_formatOut, 
            m_outputSize.width, m_outputSize.height, 0, 0, &pSurface));
    }
    else if(outputMode == AMF_VIDEO_STITCH_OUTPUT_MODE_CUBEMAP)
    {
        AMF_RETURN_IF_FAILED(m_pEngine->AllocCubeMap(m_formatOut, m_outputSize.width, m_outputSize.height, &pSurface));
    }
    else
    {
#if defined(USE_RENDER_TARGET_POOL)
        AMF_RETURN_IF_FAILED(m_pSurfacePool->AcquireSurface(&pSurface));
#else
        AMF_RETURN_IF_FAILED(m_pContext->AllocSurface(m_outputMemoryType, m_formatOut, m_outputSize.width, m_outputSize.height, &pSurface));
#endif
    }
    pSurface->SetPts(pts);
    pSurface->SetDuration(duration);
    pSurface->SetFrameType(type);
    *ppSurface = pSurface.Detach();
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL AMFVideoStitchImpl::OnPropertyChanged(const wchar_t* pName)
{
    AMFLock lock(&m_sync);
    amf_wstring name(pName);

    if(name == AMF_VIDEO_STITCH_INPUTCOUNT)
    {
        amf_int64 inputCount=0;
        GetProperty(AMF_VIDEO_STITCH_INPUTCOUNT, &inputCount);
        m_InputStatus.clear();
        for(amf_int32 i = 0; i < (amf_int32)inputCount; i++)
        {
            m_InputStatus.push_back(new AMFInputStitchImpl(this, i));
        }
    }
    else if(name == AMF_VIDEO_STITCH_VIEW_ROTATE_X || name == AMF_VIDEO_STITCH_VIEW_ROTATE_Y || name == AMF_VIDEO_STITCH_VIEW_ROTATE_Z)
    {
        if(m_pEngine != NULL)
        {
            m_pEngine->UpdateFOV(m_width, m_height, m_outputSize.width, m_outputSize.height, this);
        }
        m_PropertiesInfo[name]->value = 0.0;
    }
    else if(name == AMF_VIDEO_STITCH_WIRE_RENDER)
    {
        if(m_pEngine != NULL)
        {
            m_pEngine->UpdateFOV(m_width, m_height, m_outputSize.width, m_outputSize.height, this);
        }
    }
    else if(name == AMF_VIDEO_STITCH_COLOR_BALANCE)
    {
        GetProperty(AMF_VIDEO_STITCH_COLOR_BALANCE, &m_bColorBalance);
    }
    else if(name == AMF_VIDEO_STITCH_OUTPUT_MODE)
    {
        if(m_pEngine != NULL)
        {
            m_pEngine->Terminate();
            std::vector<AMFPropertyStorage*> inputs;
            inputs.resize(m_InputStatus.size());
            for(size_t i = 0; i< m_InputStatus.size(); i++)
            {
                inputs[i] = AMFPropertyStoragePtr(m_InputStatus[i]);
            }

            m_pEngine->Init(m_formatIn, m_width, m_height, m_formatOut, m_outputSize.width, m_outputSize.height, this, &inputs[0]);
            m_pEngine->UpdateFOV(m_width, m_height, m_outputSize.width, m_outputSize.height, this);
        }
    }
    
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL AMFVideoStitchImpl::GetInput(amf_int32 index, AMFInput** ppInput)
{
    AMF_RETURN_IF_FALSE(index >= 0 && index < (amf_int32)m_InputStatus.size(), AMF_INVALID_ARG, L"Invalid index");
    AMF_RETURN_IF_FALSE(ppInput != NULL, AMF_INVALID_ARG, L"ppInput = NULL");
    *ppInput = m_InputStatus[index];
    (*ppInput)->Acquire();
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
amf_int32   AMF_STD_CALL AMFVideoStitchImpl::GetInputCount()
{
    return (amf_int32)m_InputStatus.size();
}

//-------------------------------------------------------------------------------------------------
AMFVideoStitchImpl::AMFInputStitchImpl::AMFInputStitchImpl(AMFVideoStitchImpl* pHost, amf_int32 index) :
m_pHost(pHost),
m_iIndex(index)
{
    memset(m_fColorCorrection, 0 , sizeof(m_fColorCorrection));

    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoDouble(AMF_VIDEO_STITCH_LENS_CORR_K1, AMF_VIDEO_STITCH_LENS_CORR_K1, 0.0, -DBL_MAX, DBL_MAX, true),
        AMFPropertyInfoDouble(AMF_VIDEO_STITCH_LENS_CORR_K2, AMF_VIDEO_STITCH_LENS_CORR_K2, 0.0, -DBL_MAX, DBL_MAX, true),
        AMFPropertyInfoDouble(AMF_VIDEO_STITCH_LENS_CORR_K3, AMF_VIDEO_STITCH_LENS_CORR_K3, 0.0, -DBL_MAX, DBL_MAX, true),
        AMFPropertyInfoDouble(AMF_VIDEO_STITCH_LENS_CORR_OFFX, AMF_VIDEO_STITCH_LENS_CORR_OFFX, 0.0, -DBL_MAX, DBL_MAX, true),
        AMFPropertyInfoDouble(AMF_VIDEO_STITCH_LENS_CORR_OFFY, AMF_VIDEO_STITCH_LENS_CORR_OFFY, 0.0, -DBL_MAX, DBL_MAX, true),
        AMFPropertyInfoEnum(AMF_VIDEO_STITCH_LENS_MODE, L"Lens mode", AMF_VIDEO_STITCH_LENS_RECTILINEAR, AMF_LENS_MODE_ENUM, false),
        AMFPropertyInfoDouble(AMF_VIDEO_CAMERA_OFFSET_X, L"Camera Offset X", 0.0, -DBL_MAX, DBL_MAX, true),
        AMFPropertyInfoDouble(AMF_VIDEO_CAMERA_OFFSET_Y, L"Camera Offset Y", 0.0, -DBL_MAX, DBL_MAX, true),
        AMFPropertyInfoDouble(AMF_VIDEO_CAMERA_SCALE, L"Camera Scale", 0.0, -DBL_MAX, DBL_MAX, true),
        AMFPropertyInfoDouble(AMF_VIDEO_CAMERA_ANGLE_PITCH, AMF_VIDEO_CAMERA_ANGLE_PITCH, 0.0, -DBL_MAX, DBL_MAX, true),
        AMFPropertyInfoDouble(AMF_VIDEO_CAMERA_ANGLE_YAW, AMF_VIDEO_CAMERA_ANGLE_YAW, 0.0, -DBL_MAX, DBL_MAX, true),
        AMFPropertyInfoDouble(AMF_VIDEO_CAMERA_ANGLE_ROLL, AMF_VIDEO_CAMERA_ANGLE_ROLL, 0.0, -DBL_MAX, DBL_MAX, true),
        AMFPropertyInfoDouble(AMF_VIDEO_CAMERA_HFOV, L"Horizontal Filed of View", M_PI / 2.0, -DBL_MAX, DBL_MAX, true),
        AMFPropertyInfoRect(AMF_VIDEO_STITCH_CROP, L"Crop video", 0,0,0,0, false),
    AMFPrimitivePropertyInfoMapEnd
}

//-------------------------------------------------------------------------------------------------
AMFVideoStitchImpl::AMFInputStitchImpl::~AMFInputStitchImpl()
{
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL AMFVideoStitchImpl::AMFInputStitchImpl::SubmitInput(AMFData* pData)
{
    AMF_RETURN_IF_FALSE(pData != NULL, AMF_INVALID_POINTER);
    AMFLock lock(&m_pHost->m_sync);

    if(m_pHost->m_eof)
    {
        return AMF_EOF;
    }

    AMFSurfacePtr pSurfaceIn(pData);
    // get and validate surface atttributes
    AMF_RETURN_IF_FALSE(pSurfaceIn != NULL, AMF_INVALID_DATA_TYPE, L"Invalid input data, AMFSurface expected");
    AMF_RETURN_IF_FALSE(m_pHost->m_formatIn == pSurfaceIn->GetFormat(), AMF_INVALID_ARG, L"Invalid input surface format %s. Expected %s", 
                    AMFSurfaceGetFormatName(pSurfaceIn->GetFormat()), AMFSurfaceGetFormatName(m_pHost->m_formatIn));

    // parameters are OK
    if(m_pSurface != NULL)
    {
        return AMF_INPUT_FULL; // this channel already processed for this output - resubmit later
    }

    // interop input
    AMF_RETURN_IF_FAILED(pSurfaceIn->Convert(m_pHost->m_deviceMemoryType),L"Failed to interop input surface");

    AMFSurfacePtr pSurfaceRight;
    if (m_pHost->m_bCombinedSource && m_iIndex == 0)  //split the source into two
    {
        pSurfaceRight = pSurfaceIn; // store surface
        m_pSurface = pSurfaceIn; // store surface
    }
    else
    {
        m_pSurface = pSurfaceIn; // store surface
    }

    // build histogram
    AMFRect rect;

    if(m_pHost->m_bColorBalance)
    {
        m_pHost->m_pEngine->GetBorderRect(m_iIndex, rect);
        m_pHost->m_pHistogram->Build(m_iIndex, m_pSurface, rect, m_pHost->m_pEngine->GetBorderMap(m_iIndex));
    }

    if (pSurfaceRight)
    {
        m_pHost->SubmitInput(1, pSurfaceRight);
    }
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL AMFVideoStitchImpl::AMFInputStitchImpl::OnPropertyChanged(const wchar_t* pName)
{
    AMFLock lock(&m_pHost->m_sync);
    amf_wstring name(pName);
    if(name == AMF_VIDEO_STITCH_LENS_CORR_K1 || name == AMF_VIDEO_STITCH_LENS_CORR_K2 || name == AMF_VIDEO_STITCH_LENS_CORR_K3 || 
        name == AMF_VIDEO_STITCH_LENS_CORR_OFFX || name == AMF_VIDEO_STITCH_LENS_CORR_OFFY || 
        name == AMF_VIDEO_CAMERA_OFFSET_X || name == AMF_VIDEO_CAMERA_OFFSET_Y || name == AMF_VIDEO_CAMERA_SCALE ||
        name == AMF_VIDEO_CAMERA_ANGLE_PITCH || name == AMF_VIDEO_CAMERA_ANGLE_YAW || name == AMF_VIDEO_CAMERA_ANGLE_ROLL
        )
    {
        if(m_pHost->m_pEngine != NULL)
        {
            m_pHost->m_pEngine->UpdateMesh(m_iIndex, m_pHost->m_width, m_pHost->m_height, m_pHost->m_outputSize.width, m_pHost->m_outputSize.height, this, m_pHost);
        }
    }
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFVideoStitchImpl::GetCaps(AMFCaps** ppCaps)
{
    AMFVideoStitchCapsImplPtr pCaps = new AMFVideoStitchCapsImpl();
    AMF_RETURN_IF_FAILED(pCaps->Init(AMFContextPtr(m_pContext)));
    *ppCaps = AMFCapsPtr(pCaps).Detach();
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFVideoStitchImpl::SubmitInput(amf_int32 index, AMFData* pData)
{
    return m_InputStatus[index]->SubmitInput(pData);
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

