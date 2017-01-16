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

#include "VideoRenderHost.h"
#include "../common/CmdLogger.h"

VideoRenderHost::VideoRenderHost(amf_int width, amf_int height, bool bInterlaced, amf_int frames, amf::AMFContext* pContext)
    :VideoRender(width, height, bInterlaced, frames, pContext),
    m_iAnimation(0)
{
}

VideoRenderHost::~VideoRenderHost()
{
    Terminate();
}

AMF_RESULT      VideoRenderHost::Init(HWND hWnd, bool bFullScreen)
{
    return AMF_OK;
}
AMF_RESULT VideoRenderHost::Terminate()
{
    return AMF_OK;
}

#define SQUARE_SIZE 50

AMF_RESULT VideoRenderHost::Render(amf::AMFData** ppData)
{
    AMF_RESULT res = AMF_OK;
    amf::AMFSurfacePtr pSurface;
    res = m_pContext->AllocSurface(amf::AMF_MEMORY_HOST, GetFormat(), m_width, m_height, &pSurface);
    CHECK_AMF_ERROR_RETURN(res, L"AMFSurfrace::AllocSurface() failed");

    if(GetFormat() == amf::AMF_SURFACE_NV12)
    {

        // black
        amf_uint8 BlackY = 16;
        amf_uint8 BlackU = 128;
        amf_uint8 BlackV= 128;

        amf::AMFPlanePtr plane = pSurface->GetPlane(amf::AMF_PLANE_Y);
        amf_uint8 *data = (amf_uint8*)plane->GetNative();
        for(amf_int32 y = 0; y < m_height; y++)
        {
            amf_uint8 *dataLine = data + y * plane->GetHPitch();
            for(amf_int32 x = 0; x < m_width; x++)
            {
                *dataLine++ = BlackY;
            }
        }
        plane = pSurface->GetPlane(amf::AMF_PLANE_UV);
        data = (amf_uint8*)plane->GetNative();
        for(amf_int32 y = 0; y < m_height/2; y++)
        {
            amf_uint8 *dataLine = data + y * plane->GetHPitch();
            for(amf_int32 x = 0; x < m_width/2; x++)
            {
                *dataLine++ = BlackU;
                *dataLine++ = BlackV;
            }
        }

        // red
        amf_uint8 RedY = 81;
        amf_uint8 RedU = 90;
        amf_uint8 RedV= 240;

        plane = pSurface->GetPlane(amf::AMF_PLANE_Y);
        data = (amf_uint8*)plane->GetNative();


        for(amf_int32 y = m_iAnimation; y < m_height && y < m_iAnimation + SQUARE_SIZE; y++)
        {
            amf_uint8 *dataLine = data + y * plane->GetHPitch() + m_iAnimation*1;
            for(amf_int32 x = 0; x+ m_iAnimation < m_width && x < SQUARE_SIZE; x++)
            {
                *dataLine++ = RedY;
            }
        }
        plane = pSurface->GetPlane(amf::AMF_PLANE_UV);
        data = (amf_uint8*)plane->GetNative();

        for(amf_int32 y = m_iAnimation /2; y < m_height/2 && y < m_iAnimation/2 + SQUARE_SIZE/2; y++)
        {
            amf_uint8 *dataLine = data + y * plane->GetHPitch() + m_iAnimation/2*2;
            for(amf_int32 x = 0; x+ m_iAnimation/2 < m_width/2 && x < SQUARE_SIZE/2; x++)
            {
                *dataLine++ = RedU;
                *dataLine++ = RedV;
            }
        }
    }
    else
    {
        amf::AMFPlanePtr plane = pSurface->GetPlane(amf::AMF_PLANE_PACKED);

        amf_uint8 *data = (amf_uint8*)plane->GetNative();
        for(amf_int32 y = 0; y < m_height; y++)
        {
            amf_uint8 *dataLine = data + y * plane->GetHPitch();
            for(amf_int32 x = 0; x < m_width; x++)
            {
                *dataLine++ = 0; // B
                *dataLine++ = 0; // G
                *dataLine++ = 0xFF; // R
                *dataLine++ = 0; // A
            }
        }
        // make black rectangle
        for(amf_int32 y = m_iAnimation; y < m_height && y < m_iAnimation + SQUARE_SIZE; y++)
        {
            amf_uint8 *dataLine = data + y * plane->GetHPitch() + m_iAnimation*4;
            for(amf_int32 x = 0; x+ m_iAnimation < m_width && x < SQUARE_SIZE; x++)
            {
                *dataLine++ = 0; // B
                *dataLine++ = 0; // G
                *dataLine++ = 0; // R
                *dataLine++ = 0; // A
            }
        }
    }
    m_iAnimation++;
    if(m_iAnimation + SQUARE_SIZE >= m_height)
    {
        m_iAnimation = 0;
    }
    if(pSurface)
    {
        *ppData = pSurface.Detach();
    }
    return res;
}
