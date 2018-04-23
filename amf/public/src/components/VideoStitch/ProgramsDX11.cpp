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
#include "public/include/core/Trace.h"
#include "public/include/core/Compute.h"
#include "public/common/TraceAdapter.h"
#include "public/common/AMFFactory.h"

#if defined( _M_AMD64)
#include "ProgramsDX11/BuildLUT_64.h"
#include "ProgramsDX11/BuildLUTCenter_64.h"
#include "ProgramsDX11/BuildShifts_64.h"
#include "ProgramsDX11/Color_64.h"
#include "ProgramsDX11/Histogram_64.h"
#include "ProgramsDX11/NV12toRGB_64.h"
#else
#include "ProgramsDX11/BuildLUT_32.h"
#include "ProgramsDX11/BuildLUTCenter_32.h"
#include "ProgramsDX11/BuildShifts_32.h"
#include "ProgramsDX11/Color_32.h"
#include "ProgramsDX11/Histogram_32.h"
#include "ProgramsDX11/NV12toRGB_32.h"
#endif

extern amf::AMF_KERNEL_ID   m_KernelHistogramIdDX11;
extern amf::AMF_KERNEL_ID   m_KernelColorIdDX11;
extern amf::AMF_KERNEL_ID   m_KernelNV12toRGBIdDX11;
extern amf::AMF_KERNEL_ID   m_KernelBuildLUTIdDX11;
extern amf::AMF_KERNEL_ID   m_KernelBuildLUTCenterIdDX11;
extern amf::AMF_KERNEL_ID   m_KernelBuildShiftsIdDX11;

using namespace amf;

AMF_RESULT RegisterKernelsDX11()
{
    AMFPrograms *pPrograms = NULL;
    g_AMFFactory.GetFactory()->GetPrograms(&pPrograms);

    AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &m_KernelHistogramIdDX11,      L"main"    , "main"   , sizeof(Histogram), Histogram, NULL));
    AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &m_KernelColorIdDX11,          L"main"    , "main"   , sizeof(Color), Color, NULL));
    AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &m_KernelNV12toRGBIdDX11,      L"main"    , "main"   , sizeof(NV12toRGB), NV12toRGB, NULL));
    AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &m_KernelBuildLUTIdDX11,       L"main"    , "main"   , sizeof(BuildLUT), BuildLUT, NULL));
    AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &m_KernelBuildLUTCenterIdDX11, L"main"    , "main"   , sizeof(BuildLUTCenter), BuildLUTCenter, NULL));
    AMF_RETURN_IF_FAILED(pPrograms->RegisterKernelBinary1(AMF_MEMORY_DX11, &m_KernelBuildShiftsIdDX11,    L"main"    , "main"   , sizeof(BuildShifts), BuildShifts, NULL));

    return AMF_OK;
}
