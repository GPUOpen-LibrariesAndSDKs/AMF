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
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
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

#include "VideoRenderOpenCL.h"
#include "../common/CmdLogger.h"
#include <CL/cl.h>

VideoRenderOpenCL::VideoRenderOpenCL(amf_int width, amf_int height, bool bInterlaced, amf_int frames, amf::AMFContext* pContext, amf::AMF_MEMORY_TYPE encodertype)
    :VideoRender(width, height, bInterlaced, frames, pContext),
    m_iAnimation(0),
    m_encoderType(encodertype)
{
}

VideoRenderOpenCL::~VideoRenderOpenCL()
{
    Terminate();
}

AMF_RESULT      VideoRenderOpenCL::Init(HWND hWnd, bool bFullScreen)
{
    return AMF_OK;
}
AMF_RESULT VideoRenderOpenCL::Terminate()
{
    return AMF_OK;
}

#define SQUARE_SIZE 50

AMF_RESULT VideoRenderOpenCL::Render(amf::AMFData** ppData)
{
    AMF_RESULT res = AMF_OK;
    amf::AMFSurfacePtr pSurface;
    res = m_pContext->AllocSurface(m_encoderType, amf::AMF_SURFACE_NV12, m_width, m_height, &pSurface); // allocate with encoder memory type
    CHECK_AMF_ERROR_RETURN(res, L"AMFSurfrace::AllocSurface() failed");
    res = pSurface->Convert(amf::AMF_MEMORY_OPENCL);
    CHECK_AMF_ERROR_RETURN(res, L"AMFSurfrace::Convert(amf::AMF_MEMORY_OPENCL) failed"); // interop to OpenCL - optimal
    

    amf::AMFContext::AMFOpenCLLocker locker(m_pContext);

    
    cl_command_queue            hCommandQueue = (cl_command_queue)m_pContext->GetOpenCLCommandQueue();

    cl_int status = 0;

    cl_mem planeY = (cl_mem)pSurface->GetPlane(amf::AMF_PLANE_Y)->GetNative();
    cl_mem planeUV = (cl_mem)pSurface->GetPlane(amf::AMF_PLANE_UV)->GetNative();
    float colorBlack[4] ={0.0f, 1.0f, 0.0f, 0.0f};
    size_t  originY[3]={ 0, 0, 0};
    size_t  regionY[3]={(size_t)pSurface->GetPlane(amf::AMF_PLANE_Y)->GetWidth(), (size_t)pSurface->GetPlane(amf::AMF_PLANE_Y)->GetHeight(), 1};

    status = m_Device.GetLoader().clEnqueueFillImage(hCommandQueue, planeY, colorBlack, originY, regionY, 0, NULL, NULL);
    CHECK_OPENCL_ERROR_RETURN(status, L"clEnqueueFillImage() failed");
    size_t  originUV[3]={ 0, 0, 0};
    size_t  regionUV[3]={(size_t)pSurface->GetPlane(amf::AMF_PLANE_UV)->GetWidth(), (size_t)pSurface->GetPlane(amf::AMF_PLANE_UV)->GetHeight(), 1};

    status = m_Device.GetLoader().clEnqueueFillImage(hCommandQueue, planeUV, colorBlack, originUV, regionUV, 0, NULL, NULL);
    CHECK_OPENCL_ERROR_RETURN(status, L"clEnqueueFillImage() failed");

    float color[4] ={1.0f, 0.0f, 0.0f, 0.0f};
    size_t  origin[3]={ (size_t)m_iAnimation, (size_t)m_iAnimation, 0};
    size_t  region[3]={SQUARE_SIZE, SQUARE_SIZE, 1};


    status = m_Device.GetLoader().clEnqueueFillImage(hCommandQueue, planeY, color,origin, region, 0, NULL, NULL);
    CHECK_OPENCL_ERROR_RETURN(status, L"clEnqueueFillImage() failed");

    origin[0] = m_iAnimation / 2;
    origin[1] = m_iAnimation / 2;
    region [0] = SQUARE_SIZE / 2;
    region [1] = SQUARE_SIZE / 2;
    status = m_Device.GetLoader().clEnqueueFillImage(hCommandQueue, planeUV, color,origin, region, 0, NULL, NULL);
    CHECK_OPENCL_ERROR_RETURN(status, L"clEnqueueFillImage() failed");

    m_Device.GetLoader().clFinish(hCommandQueue);

    m_iAnimation++;
    if(m_iAnimation + SQUARE_SIZE >= m_width  || m_iAnimation + SQUARE_SIZE >= m_height)
    {
        m_iAnimation = 0;
    }
    if(pSurface)
    {
        *ppData = pSurface.Detach();
    }
    return res;
}
