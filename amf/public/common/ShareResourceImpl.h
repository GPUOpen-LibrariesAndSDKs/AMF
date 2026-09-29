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
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
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
///-------------------------------------------------------------------------
///  @file   ShareResourceImpl.h
///  @brief  Shared resource classes header
///-------------------------------------------------------------------------
#ifndef AMF_ShareResourceImpl_h
#define AMF_ShareResourceImpl_h
#pragma once

#if !defined(_WIN32)
#include <unistd.h>
#endif
#include "public/include/core/Context.h"

#if defined(__cplusplus)
namespace amf
{

#if defined(_WIN32)
    static constexpr amf_handle AMF_RESOURCE_INVALID = nullptr;
#else
    static constexpr int AMF_RESOURCE_INVALID = -1;
#endif

    //---------------------------------------------------------------------------------------------
    // AMFSurface share handle class
    //----------------------------------------------------------------------------------------------
    class AMFSharedSurfaceResourceImpl : public AMFSharedSurfaceResource
    {
    public:
        //-------------------------------------------------------------------------------------------------
        AMFSharedSurfaceResourceImpl()
        {
            memset(hPlanes, 0, sizeof(hPlanes));
            memset(hHeap, 0, sizeof(hHeap));
            memset(planeSize, 0, sizeof(planeSize));
            memset(rowPitch, 0, sizeof(rowPitch));
            memset(Sync, 0, sizeof(Sync));
            for (amf_int i = 0; i < AMF_SURFACE_MAX_PLANES; i++)
            {
                hPlanes[i] = AMF_RESOURCE_INVALID;
                hHeap[i] = AMF_RESOURCE_INVALID;
                Sync[i].hFence = AMF_RESOURCE_INVALID;
            }
            bCloseSurface = true;
            bUserControl = false;
            eSourceFormat = AMF_SURFACE_UNKNOWN;
            eSourceUsage = AMF_SURFACE_USAGE_NONE;
            eSourceAccess = (AMF_MEMORY_CPU_ACCESS)AMF_MEMORY_CPU_DEFAULT;
            bEmulatedFormat = false;
            pNext = nullptr;
        }
        //-------------------------------------------------------------------------------------------------
        ~AMFSharedSurfaceResourceImpl()
        {
            for (amf_int i = 0; i < AMF_SURFACE_MAX_PLANES; i++)
            {
                if (!bUserControl)
                {
#if defined(_WIN32)
                    if (hPlanes[i] != AMF_RESOURCE_INVALID && bCloseSurface)
                    {
                        CloseHandle(hPlanes[i]);
                    }
                    if (hHeap[i] != AMF_RESOURCE_INVALID)
                    {
                        CloseHandle(hHeap[i]);
                    }
                    if (Sync[i].hFence != AMF_RESOURCE_INVALID)
                    {
                        CloseHandle(Sync[i].hFence);
                    }
#else
                    if (hPlanes[i] != AMF_RESOURCE_INVALID && bCloseSurface)
                    {
                        close(hPlanes[i]);
                    }
                    if (hHeap[i] != AMF_RESOURCE_INVALID)
                    {
                        close(hHeap[i]);
                    }
                    if (Sync[i].hFence != AMF_RESOURCE_INVALID)
                    {
                        close(Sync[i].hFence);
                    }
#endif // defined(_WIN32)
                }
            }
        }
        //---------------------------------------------------------------------------------------------
    };
    //---------------------------------------------------------------------------------------------
}
#endif // #if defined(__cplusplus)
#endif // AMF_ShareResourceImpl_h