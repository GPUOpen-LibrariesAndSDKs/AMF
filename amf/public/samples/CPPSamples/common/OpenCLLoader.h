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
#pragma once
#define CL_USE_DEPRECATED_OPENCL_2_0_APIS
#include <CL/cl.h>
#include "public/include/core/Result.h"

typedef CL_API_ENTRY cl_int (CL_API_CALL *
clGetPlatformIDs_Fn)(cl_uint          /* num_entries */,
                 cl_platform_id * /* platforms */,
                 cl_uint *        /* num_platforms */) CL_API_SUFFIX__VERSION_1_0;

typedef CL_API_ENTRY cl_int (CL_API_CALL * 
clGetPlatformInfo_Fn)(cl_platform_id   /* platform */, 
                  cl_platform_info /* param_name */,
                  size_t           /* param_value_size */, 
                  void *           /* param_value */,
                  size_t *         /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;

/* Device APIs */
typedef CL_API_ENTRY cl_int (CL_API_CALL *
clGetDeviceIDs_Fn)(cl_platform_id   /* platform */,
               cl_device_type   /* device_type */, 
               cl_uint          /* num_entries */, 
               cl_device_id *   /* devices */, 
               cl_uint *        /* num_devices */) CL_API_SUFFIX__VERSION_1_0;

typedef CL_API_ENTRY cl_int (CL_API_CALL *
clGetDeviceInfo_Fn)(cl_device_id    /* device */,
                cl_device_info  /* param_name */, 
                size_t          /* param_value_size */, 
                void *          /* param_value */,
                size_t *        /* param_value_size_ret */) CL_API_SUFFIX__VERSION_1_0;

typedef CL_API_ENTRY cl_context (CL_API_CALL *
clCreateContext_Fn)(const cl_context_properties * /* properties */,
                cl_uint                 /* num_devices */,
                const cl_device_id *    /* devices */,
                void (CL_CALLBACK * /* pfn_notify */)(const char *, const void *, size_t, void *),
                void *                  /* user_data */,
                cl_int *                /* errcode_ret */) CL_API_SUFFIX__VERSION_1_0;

typedef CL_API_ENTRY CL_EXT_PREFIX__VERSION_2_0_DEPRECATED cl_command_queue (CL_API_CALL *
clCreateCommandQueue_Fn)(cl_context                     /* context */,
                     cl_device_id                   /* device */,
                     cl_command_queue_properties    /* properties */,
                     cl_int *                       /* errcode_ret */) CL_EXT_SUFFIX__VERSION_2_0_DEPRECATED;
typedef CL_API_ENTRY cl_int (CL_API_CALL *
clReleaseCommandQueue_Fn)(cl_command_queue /* command_queue */) CL_API_SUFFIX__VERSION_1_0;
typedef CL_API_ENTRY cl_int (CL_API_CALL *
clReleaseDevice_Fn)(cl_device_id /* device */) CL_API_SUFFIX__VERSION_1_2;
typedef CL_API_ENTRY cl_int (CL_API_CALL *
clReleaseContext_Fn)(cl_context /* context */) CL_API_SUFFIX__VERSION_1_0;

typedef CL_API_ENTRY cl_int (CL_API_CALL *
clEnqueueFillImage_Fn)(cl_command_queue   /* command_queue */,
                   cl_mem             /* image */, 
                   const void *       /* fill_color */, 
                   const size_t *     /* origin[3] */, 
                   const size_t *     /* region[3] */, 
                   cl_uint            /* num_events_in_wait_list */, 
                   const cl_event *   /* event_wait_list */, 
                   cl_event *         /* event */) CL_API_SUFFIX__VERSION_1_2;
typedef CL_API_ENTRY void * (CL_API_CALL *
clGetExtensionFunctionAddressForPlatform_Fn)(cl_platform_id /* platform */,
                                         const char *   /* func_name */) CL_API_SUFFIX__VERSION_1_2;

typedef CL_API_ENTRY cl_int (CL_API_CALL *
clFlush_Fn)(cl_command_queue /* command_queue */) CL_API_SUFFIX__VERSION_1_0;

typedef CL_API_ENTRY cl_int (CL_API_CALL *
clFinish_Fn)(cl_command_queue /* command_queue */) CL_API_SUFFIX__VERSION_1_0;

class OpenCLLoader
{
public:
    AMF_RESULT Init();
    AMF_RESULT Terminate();
    // OpenCl functions;
    clGetPlatformIDs_Fn             clGetPlatformIDs;       
    clGetPlatformInfo_Fn            clGetPlatformInfo;
    clGetDeviceIDs_Fn               clGetDeviceIDs;
    clGetDeviceInfo_Fn              clGetDeviceInfo;
    clCreateContext_Fn              clCreateContext;
    clCreateCommandQueue_Fn         clCreateCommandQueue;
    clReleaseCommandQueue_Fn        clReleaseCommandQueue;
    clReleaseDevice_Fn              clReleaseDevice;
    clReleaseContext_Fn             clReleaseContext;
    clEnqueueFillImage_Fn           clEnqueueFillImage;
    clGetExtensionFunctionAddressForPlatform_Fn clGetExtensionFunctionAddressForPlatform;
    clFlush_Fn                      clFlush;
    clFinish_Fn                     clFinish;

    OpenCLLoader();
    ~OpenCLLoader();
protected:

    amf_long    m_iRefCount;
    amf_handle  m_hDLL;
};
