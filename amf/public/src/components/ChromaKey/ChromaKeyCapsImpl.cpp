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
#include "ChromaKeyCapsImpl.h"
#include "d3d11.h"

using namespace amf;

//-------------------------------------------------------------------------------------------------
AMFChromaKeyCapsImpl::AMFChromaKeyCapsImpl()
{
}
//-------------------------------------------------------------------------------------------------
AMFChromaKeyCapsImpl::~AMFChromaKeyCapsImpl()
{
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFChromaKeyCapsImpl::Init(AMFContext* pContext)
{
    m_pContext = pContext;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
//  AMFCaps methods
//-------------------------------------------------------------------------------------------------
AMF_ACCELERATION_TYPE AMF_STD_CALL AMFChromaKeyCapsImpl::GetAccelerationType() const
{
    return AMF_ACCEL_GPU;
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFChromaKeyCapsImpl::GetInputCaps(AMFIOCaps **input)
{
    if (input != NULL)
    {
        *input = new AMFChromaKeyInputCapsImpl(m_pContext);
        (*input)->Acquire();
        return AMF_OK;
    }
    else
    {
        return AMF_INVALID_ARG;
    }
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AMFChromaKeyCapsImpl::GetOutputCaps(AMFIOCaps **output)
{
    if (output != NULL)
    {
        *output = new AMFChromaKeyOutputCapsImpl(m_pContext);
        (*output)->Acquire();
        return AMF_OK;
    }
    else
    {
        return AMF_INVALID_ARG;
    }
}
//-------------------------------------------------------------------------------------------------
AMFChromaKeyInputCapsImpl::AMFChromaKeyInputCapsImpl(AMFContext* pContext)
{
    SetResolution(32, D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION, 32, D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION);
    SetVertAlign(2);

    if (pContext->GetDX11Device() != NULL)
    {
        static amf::AMF_MEMORY_TYPE nativeDX11MemoryTypes[] =
        {
            AMF_MEMORY_DX11
        };
        PopulateMemoryTypes(_countof(nativeDX11MemoryTypes), nativeDX11MemoryTypes, true);
    }

    static amf::AMF_MEMORY_TYPE nativeMemoryTypes[] =
    {
        AMF_MEMORY_OPENCL,
        AMF_MEMORY_OPENGL,
    };

    PopulateMemoryTypes(_countof(nativeMemoryTypes), nativeMemoryTypes, true);
    static amf::AMF_MEMORY_TYPE nonNativeMemoryTypes[] =
    {
        AMF_MEMORY_HOST,
    };
    PopulateMemoryTypes(_countof(nonNativeMemoryTypes), nonNativeMemoryTypes, false);


    static AMF_SURFACE_FORMAT nativeFormats[] =
    {
        AMF_SURFACE_NV12,               ///< 1 - planar Y width x height + packed UV width/2 x height/2 - 8 bit per component
        AMF_SURFACE_BGRA,               ///< 3 - packed - 8 bit per component
        AMF_SURFACE_ARGB,               ///< 4 - packed - 8 bit per component
        AMF_SURFACE_RGBA,               ///< 5 - packed - 8 bit per component
    };
    PopulateSurfaceFormats(_countof(nativeFormats), nativeFormats, true);
}
//-------------------------------------------------------------------------------------------------
AMFChromaKeyOutputCapsImpl::AMFChromaKeyOutputCapsImpl(AMFContext* pContext)
{
    SetResolution(32, 4096, 32, 4096);
    SetVertAlign(2);

    if (pContext->GetDX11Device() != NULL)
    {
        static amf::AMF_MEMORY_TYPE nativeDX11MemoryTypes[] =
        {
            AMF_MEMORY_DX11
        };
        PopulateMemoryTypes(_countof(nativeDX11MemoryTypes), nativeDX11MemoryTypes, true);
    }

    static amf::AMF_MEMORY_TYPE nativeMemoryTypes[] =
    {
        AMF_MEMORY_OPENCL,
        AMF_MEMORY_OPENGL,
    };

    PopulateMemoryTypes(_countof(nativeMemoryTypes), nativeMemoryTypes, true);
    static amf::AMF_MEMORY_TYPE nonNativeMemoryTypes[] =
    {
        AMF_MEMORY_HOST,
    };
    PopulateMemoryTypes(_countof(nonNativeMemoryTypes), nonNativeMemoryTypes, false);


    static AMF_SURFACE_FORMAT nativeFormats[] =
    {
        AMF_SURFACE_NV12,               ///< 1 - planar Y width x height + packed UV width/2 x height/2 - 8 bit per component
        AMF_SURFACE_BGRA,               ///< 3 - packed - 8 bit per component
        AMF_SURFACE_ARGB,               ///< 4 - packed - 8 bit per component
        AMF_SURFACE_RGBA,               ///< 5 - packed - 8 bit per component
    };
    PopulateSurfaceFormats(_countof(nativeFormats), nativeFormats, true);
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
