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

#include "OpenCLLoader.h"
#include "CmdLogger.h"
#include "public/common/Thread.h"

#pragma warning(disable: 4996)

#pragma warning(disable: 4996)


OpenCLLoader::OpenCLLoader() :
m_iRefCount(0),
m_hDLL(NULL)
{
}
OpenCLLoader::~OpenCLLoader()
{
    Terminate();
}

AMF_RESULT OpenCLLoader::Init()
{
    if(m_hDLL != NULL)
    {
        amf_atomic_inc(&m_iRefCount);
        return AMF_OK;
    }
#ifdef _WIN32
    m_hDLL = amf_load_library(L"OpenCL.dll");
#else
    m_hDLL = amf_load_library(L"libOpenCL.so");
#endif
    CHECK_RETURN(m_hDLL != NULL, AMF_FAIL, L"Cannot load OpenCL library");

    // load OpenCL functions
    clGetPlatformIDs        = (clGetPlatformIDs_Fn     )amf_get_proc_address(m_hDLL, "clGetPlatformIDs");       
    clGetPlatformInfo       = (clGetPlatformInfo_Fn    )amf_get_proc_address(m_hDLL, "clGetPlatformInfo");      
    clGetDeviceIDs          = (clGetDeviceIDs_Fn       )amf_get_proc_address(m_hDLL, "clGetDeviceIDs");          
    clGetDeviceInfo         = (clGetDeviceInfo_Fn      )amf_get_proc_address(m_hDLL, "clGetDeviceInfo");        
    clCreateContext         = (clCreateContext_Fn      )amf_get_proc_address(m_hDLL, "clCreateContext");         
    clCreateCommandQueue    = (clCreateCommandQueue_Fn )amf_get_proc_address(m_hDLL, "clCreateCommandQueue");    
    clReleaseCommandQueue   = (clReleaseCommandQueue_Fn)amf_get_proc_address(m_hDLL, "clReleaseCommandQueue");   
    clReleaseDevice         = (clReleaseDevice_Fn      )amf_get_proc_address(m_hDLL, "clReleaseDevice");         
    clReleaseContext        = (clReleaseContext_Fn     )amf_get_proc_address(m_hDLL, "clReleaseContext");        
    clEnqueueFillImage      = (clEnqueueFillImage_Fn   )amf_get_proc_address(m_hDLL, "clEnqueueFillImage");
    clGetExtensionFunctionAddressForPlatform = (clGetExtensionFunctionAddressForPlatform_Fn   )amf_get_proc_address(m_hDLL, "clGetExtensionFunctionAddressForPlatform");
    clFlush                 = (clFlush_Fn   )amf_get_proc_address(m_hDLL, "clFlush");
    clFinish                = (clFinish_Fn   )amf_get_proc_address(m_hDLL, "clFinish");

    amf_atomic_inc(&m_iRefCount);

    return AMF_OK;
}
AMF_RESULT OpenCLLoader::Terminate()
{
    if(m_hDLL == NULL)
    {
        return AMF_OK;
    }
    amf_atomic_dec(&m_iRefCount);
    if(m_iRefCount == 0)
    {
        amf_free_library(m_hDLL);
        m_hDLL = NULL;
    }
    return AMF_OK;
}
