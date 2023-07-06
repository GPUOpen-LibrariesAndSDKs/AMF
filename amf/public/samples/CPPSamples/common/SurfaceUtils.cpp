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

#include "SurfaceUtils.h"

#include "public/common/TraceAdapter.h"

#include <fstream>
#include <iostream>

#define AMF_FACILITY L"SurfaceUtils"


AMF_RESULT WritePlane(amf::AMFPlane* pPlane, amf::AMFDataStream *pDataStr)
{
    AMF_RETURN_IF_INVALID_POINTER(pPlane);
    AMF_RETURN_IF_INVALID_POINTER(pDataStr);

    // write NV12 surface removing offsets and alignments
    amf_uint8* pData = reinterpret_cast<amf_uint8*>(pPlane->GetNative());
    amf_int32 offsetX = pPlane->GetOffsetX();
    amf_int32 offsetY = pPlane->GetOffsetY();
    amf_int32 pixelSize = pPlane->GetPixelSizeInBytes();
    amf_int32 height = pPlane->GetHeight();
    amf_int32 width = pPlane->GetWidth();
    amf_int32 pitchH = pPlane->GetHPitch();

    for (amf_int32 y = 0; y < height; y++)
    {
        AMF_RESULT res = AMF_OK;
        amf_uint8* pLine = pData + (y + offsetY) * pitchH;
        amf_size toWrite = (amf_size)pixelSize * (amf_size)width;
        res = pDataStr->Write(pLine + offsetX * pixelSize, toWrite, NULL);
        AMF_RETURN_IF_FAILED(res);
    }

    return AMF_OK;
}

AMF_RESULT FillBitmapWithColor(amf_uint8* data, amf_int32 width, amf_int32 height, amf_int32 pitch, AMFColor color)
{
    for (amf_int32 y = 0; y < height; ++y)
    {
        amf_uint8* line = data + y * pitch;
        for (amf_int32 x = 0; x < width; ++x)
        {
            line[0] = color.r;
            line[1] = color.g;
            line[2] = color.b;
            line[3] = color.a;
            line += 4;
        }
    }

    return AMF_OK;
}

bool GetStrideBySurfaceFormat(amf::AMF_SURFACE_FORMAT eFormat, amf_int32 width, amf_int32& stride)
{
    stride = 0;
    switch (eFormat)
    {
    case amf::AMF_SURFACE_NV12:
        stride = width % 2 ? width + 1 : width;
        break;
    case amf::AMF_SURFACE_YUY2:
    case amf::AMF_SURFACE_UYVY:
        stride = width * 2;
        break;
    case amf::AMF_SURFACE_P010:
        stride = (width % 2 ? width + 1 : width) * 2;
        break;
    case amf::AMF_SURFACE_YUV420P:
    case amf::AMF_SURFACE_YV12:
        stride = width;
        break;
    case amf::AMF_SURFACE_BGRA:
    case amf::AMF_SURFACE_RGBA:
        stride = width * 4;
        break;
    case amf::AMF_SURFACE_R10G10B10A2:
        stride = width * 4;
        break;
    case amf::AMF_SURFACE_RGBA_F16:
        stride = width * 8;
        break;

    default:
        return false;
    }
    return true;
}
