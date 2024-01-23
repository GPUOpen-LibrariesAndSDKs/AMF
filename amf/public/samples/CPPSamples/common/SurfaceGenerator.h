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
// Copyright (C) 2023 Advanced Micro Devices, Inc. All rights reserved.
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
#ifdef _WIN32
#include <tchar.h>
#include <d3d9.h>
#include <d3d11.h>
#include <d3d12.h>
#endif

#include "public/common/TraceAdapter.h"
#include "public/common/AMFFactory.h"
#include "../common/RawStreamReader.h"


void       PrepareFillFromHost(amf::AMFContext* pContext, amf::AMF_MEMORY_TYPE memoryType, amf::AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height, amf_bool isInject);
AMF_RESULT FillSurface(amf::AMFContextPtr pContext, amf::AMFSurface** ppSurfaceIn, amf::AMF_MEMORY_TYPE memoryType, amf::AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height, bool bWait);
AMF_RESULT ReadSurface(PipelineElementPtr pPipelineElPtr, amf::AMFSurface** ppSurface, amf::AMF_MEMORY_TYPE memoryType);
#ifdef _WIN32
AMF_RESULT FillSurfaceDX9(amf::AMFContext* pContext, amf::AMFSurface* pSurface, amf_bool isRefFrame);
AMF_RESULT FillSurfaceDX11(amf::AMFContext* pContext, amf::AMFSurface* pSurface, amf_bool isRefFrame);
AMF_RESULT FillSurfaceDX12(amf::AMFContext* pContext, amf::AMFSurface* pSurface, amf_bool isRefFrame);
#endif
AMF_RESULT FillSurfaceVulkan(amf::AMFContext* pContext, amf::AMFSurface* pSurface, amf_bool isRefFrame);
AMF_RESULT FillRGBASurfaceWithColor(amf::AMFSurface* pSurface, amf_uint8 R, amf_uint8 G, amf_uint8 B);
AMF_RESULT FillNV12SurfaceWithColor(amf::AMFSurface* pSurface, amf_uint8 Y, amf_uint8 U, amf_uint8 V);
AMF_RESULT FillBGRASurfaceWithColor(amf::AMFSurface* pSurface, amf_uint8 R, amf_uint8 G, amf_uint8 B);
AMF_RESULT FillR10G10B10A2SurfaceWithColor(amf::AMFSurface* pSurface, amf_uint8 R, amf_uint8 G, amf_uint8 B);
AMF_RESULT FillRGBA_F16SurfaceWithColor(amf::AMFSurface* pSurface, amf_uint8 R, amf_uint8 G, amf_uint8 B);
AMF_RESULT FillP010SurfaceWithColor(amf::AMFSurface* pSurface, amf_uint8 Y, amf_uint8 U, amf_uint8 V);
AMF_RESULT FillRGBA_RGBA16SurfaceWithColor(amf::AMFSurface* pSurface, amf_uint8 R, amf_uint8 G, amf_uint8 B);
