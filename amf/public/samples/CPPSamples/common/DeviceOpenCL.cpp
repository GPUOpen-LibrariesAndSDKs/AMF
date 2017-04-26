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

#include "DeviceOpenCL.h"
#include "CmdLogger.h"
#include <CL/cl_d3d11.h>
#include <CL/cl_dx9_media_sharing.h>
#include <CL/cl_gl.h>
#include "public/common/Thread.h"

#pragma warning(disable: 4996)

#pragma warning(disable: 4996)

OpenCLLoader        DeviceOpenCL::m_Loader;

DeviceOpenCL::DeviceOpenCL() :
    m_hCommandQueue(0),
    m_hContext(0)
{
    m_Loader.Init();
}

DeviceOpenCL::~DeviceOpenCL()
{
    Terminate();
    m_Loader.Terminate();
}

AMF_RESULT DeviceOpenCL::Init(IDirect3DDevice9* pD3DDevice9, ID3D11Device* pD3DDevice11, HGLRC hContextOGL, HDC hDC)
{
    AMF_RESULT res = AMF_OK;
    cl_int status = 0;
    cl_platform_id platformID = 0;
    cl_device_id interoppedDeviceID = 0;
    // get AMD platform:
    res = FindPlatformID(platformID);
    CHECK_AMF_ERROR_RETURN(res, L"FindPlatformID() failed");

    std::vector<cl_context_properties> cps;
    
    // add devices if needed

    if(pD3DDevice11 != NULL)
    {
        clGetDeviceIDsFromD3D11KHR_fn pClGetDeviceIDsFromD3D11KHR = static_cast<clGetDeviceIDsFromD3D11KHR_fn>(GetLoader().clGetExtensionFunctionAddressForPlatform(platformID, "clGetDeviceIDsFromD3D11KHR"));
        if(pClGetDeviceIDsFromD3D11KHR == NULL)
        {
            LOG_ERROR(L"Cannot resolve ClGetDeviceIDsFromD3D11KHR function");
            return AMF_FAIL;
        }

        cl_device_id deviceDX11 = 0;
        status = (*pClGetDeviceIDsFromD3D11KHR)(platformID, CL_D3D11_DEVICE_KHR, (void*)pD3DDevice11, CL_PREFERRED_DEVICES_FOR_D3D11_KHR, 1,&deviceDX11,NULL);
        CHECK_OPENCL_ERROR_RETURN(status, L"pClGetDeviceIDsFromD3D11KHR() failed");
        interoppedDeviceID = deviceDX11;
        m_hDeviceIDs.push_back(deviceDX11);
        cps.push_back(CL_CONTEXT_D3D11_DEVICE_KHR);
        cps.push_back((cl_context_properties)pD3DDevice11);
    }
    if(pD3DDevice9 != NULL)
    {
        clGetDeviceIDsFromDX9MediaAdapterKHR_fn pclGetDeviceIDsFromDX9MediaAdapterKHR = static_cast<clGetDeviceIDsFromDX9MediaAdapterKHR_fn>(GetLoader().clGetExtensionFunctionAddressForPlatform(platformID, "clGetDeviceIDsFromDX9MediaAdapterKHR"));
        if(pclGetDeviceIDsFromDX9MediaAdapterKHR == NULL)
        {
            LOG_ERROR(L"Cannot resolve clGetDeviceIDsFromDX9MediaAdapterKHR function");
            return AMF_FAIL;
        }
        cl_dx9_media_adapter_type_khr mediaAdapterType = CL_ADAPTER_D3D9EX_KHR;
        cl_device_id deviceDX9 = 0;
        status = (*pclGetDeviceIDsFromDX9MediaAdapterKHR)(platformID, 1, &mediaAdapterType, &pD3DDevice9,CL_PREFERRED_DEVICES_FOR_DX9_MEDIA_ADAPTER_KHR, 1, &deviceDX9, NULL);
        CHECK_OPENCL_ERROR_RETURN(status, L"pclGetDeviceIDsFromDX9MediaAdapterKHR() failed");

        interoppedDeviceID = deviceDX9;
        m_hDeviceIDs.push_back(deviceDX9);

        cps.push_back(CL_CONTEXT_ADAPTER_D3D9EX_KHR);
        cps.push_back((cl_context_properties)pD3DDevice9);
    }
    if(hContextOGL != NULL)
    {
        clGetGLContextInfoKHR_fn pclGetGLContextInfoKHR = static_cast<clGetGLContextInfoKHR_fn>(GetLoader().clGetExtensionFunctionAddressForPlatform(platformID, "clGetGLContextInfoKHR"));
        if(pclGetGLContextInfoKHR == NULL)
        {
            LOG_ERROR(L"Cannot resolve clGetGLContextInfoKHR function");
            return AMF_FAIL;
        }
        std::vector<cl_context_properties> gl_cps;
        gl_cps.push_back(CL_CONTEXT_PLATFORM);
        gl_cps.push_back((cl_context_properties)platformID);
        gl_cps.push_back(CL_GL_CONTEXT_KHR);
        gl_cps.push_back((cl_context_properties)hContextOGL);
        gl_cps.push_back(CL_WGL_HDC_KHR);
        gl_cps.push_back((cl_context_properties)hDC);
        gl_cps.push_back(0);

        cl_device_id deviceGL = NULL;
        status = pclGetGLContextInfoKHR(&gl_cps[0], CL_CURRENT_DEVICE_FOR_GL_CONTEXT_KHR, sizeof(cl_device_id), &deviceGL, NULL);
        CHECK_OPENCL_ERROR_RETURN(status, L"clGetGLContextInfoKHR() failed");
        interoppedDeviceID = deviceGL;
        m_hDeviceIDs.push_back(deviceGL);
        
        cps.push_back(CL_GL_CONTEXT_KHR);
        cps.push_back((cl_context_properties)hContextOGL);

        cps.push_back(CL_WGL_HDC_KHR);
        cps.push_back((cl_context_properties)hDC);
    }
    cps.push_back(CL_CONTEXT_INTEROP_USER_SYNC);
    cps.push_back(CL_TRUE);

    cps.push_back(CL_CONTEXT_PLATFORM);
    cps.push_back((cl_context_properties)platformID);
    cps.push_back(0);

    if(interoppedDeviceID == NULL)
    {
        status = GetLoader().clGetDeviceIDs((cl_platform_id)platformID, CL_DEVICE_TYPE_GPU, 1, (cl_device_id*)&interoppedDeviceID, NULL);
        CHECK_OPENCL_ERROR_RETURN(status, L"clGetDeviceIDs() failed");
        m_hDeviceIDs.push_back(interoppedDeviceID);
    }
    if(hContextOGL != 0)
    {
        wglMakeCurrent(hDC, hContextOGL);
    }
    m_hContext = GetLoader().clCreateContext(&cps[0], 1, &interoppedDeviceID, NULL, NULL, &status);
    if(hContextOGL != 0)
    {
        wglMakeCurrent(NULL, NULL);
    }
    CHECK_OPENCL_ERROR_RETURN(status, L"clCreateContext() failed");

    m_hCommandQueue = GetLoader().clCreateCommandQueue(m_hContext, interoppedDeviceID, (cl_command_queue_properties)NULL, &status);
    CHECK_OPENCL_ERROR_RETURN(status, L"clCreateCommandQueue() failed");
    return AMF_OK;
}

AMF_RESULT DeviceOpenCL::Terminate()
{
    if(m_hCommandQueue != 0)
    {
        GetLoader().clReleaseCommandQueue(m_hCommandQueue);
        m_hCommandQueue = NULL;
    }
    if(m_hDeviceIDs.size() != 0)
    {
        for(std::vector<cl_device_id>::iterator it= m_hDeviceIDs.begin(); it != m_hDeviceIDs.end(); it++)
        {
            GetLoader().clReleaseDevice(*it);
        }
        m_hDeviceIDs.clear();;
    }
    if(m_hContext != 0)
    {
        GetLoader().clReleaseContext(m_hContext);
        m_hContext = NULL;
    }
    return AMF_OK;
}
AMF_RESULT DeviceOpenCL::FindPlatformID(cl_platform_id &platform)
{
    cl_int status = 0;
    cl_uint numPlatforms = 0;
    status = GetLoader().clGetPlatformIDs(0, NULL, &numPlatforms);
    CHECK_OPENCL_ERROR_RETURN(status, L"clGetPlatformIDs() failed");
    if(numPlatforms == 0)
    {
        LOG_ERROR("clGetPlatformIDs() returned 0 platforms: ");
        return AMF_FAIL;
    }
    std::vector<cl_platform_id> platforms;
    platforms.resize(numPlatforms);
    status = GetLoader().clGetPlatformIDs(numPlatforms, &platforms[0], NULL);
    CHECK_OPENCL_ERROR_RETURN(status, L"clGetPlatformIDs() failed");
    bool bFound = false;
    for(cl_uint i = 0; i < numPlatforms; ++i)
    {
        char pbuf[1000];
        status = GetLoader().clGetPlatformInfo(platforms[i], CL_PLATFORM_VENDOR, sizeof(pbuf), pbuf, NULL);
        CHECK_OPENCL_ERROR_RETURN(status, L"clGetPlatformInfo() failed");
        if(!strcmp(pbuf, "Advanced Micro Devices, Inc."))
        {
            platform = platforms[i];
            bFound = true;
            return AMF_OK;
        }
    }
    return AMF_FAIL;
}

OpenCLLoader& DeviceOpenCL::GetLoader()
{
    return m_Loader;
}

