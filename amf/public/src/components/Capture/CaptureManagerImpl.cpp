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
// Copyright (c) 2019 Advanced Micro Devices, Inc. All rights reserved.
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
#include "CaptureManagerImpl.h"

#define AMF_FACILITY L"DeckLinkMediaImpl"

using namespace amf;


extern "C"
{
    AMF_RESULT AMF_CDECL_CALL AMFCreateCaptureManagerDeckLinkMedia(amf::AMFContext* pContext, amf::AMFCaptureManager** ppManager);
    AMF_RESULT AMF_CDECL_CALL AMFCreateCaptureManagerMediaFoundation(amf::AMFContext* pContext, amf::AMFCaptureManager** ppManager);
}


extern "C"
{
    AMF_RESULT AMF_CDECL_CALL AMFCreateCaptureManager(amf::AMFContext* pContext, amf::AMFCaptureManager** ppManager)
    {
        *ppManager = new amf::AMFInterfaceMultiImpl< amf::AMFCaptureManagerImpl, amf::AMFCaptureManager, amf::AMFContext*>(pContext);
        (*ppManager)->Acquire();
        return AMF_OK;
    }
}

//-------------------------------------------------------------------------------------------------
AMFCaptureManagerImpl::AMFCaptureManagerImpl(amf::AMFContext* pContext) :
m_pContext(pContext)
{
    AMFCaptureManagerPtr pDeckLinkMedia;
    AMFCreateCaptureManagerDeckLinkMedia(m_pContext, &pDeckLinkMedia);
    if(pDeckLinkMedia != NULL)
    {
        m_SatelliteManagers.push_back(pDeckLinkMedia);
    }
    AMFCaptureManagerPtr pMediaFoundation;
    AMFCreateCaptureManagerMediaFoundation(m_pContext, &pMediaFoundation);
    if(pMediaFoundation != NULL)
    {
        m_SatelliteManagers.push_back(pMediaFoundation);
    }
}
//-------------------------------------------------------------------------------------------------
AMFCaptureManagerImpl::~AMFCaptureManagerImpl()
{
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT          AMF_STD_CALL AMFCaptureManagerImpl::Update()
{
    for(std::vector<AMFCaptureManagerPtr>::iterator it = m_SatelliteManagers.begin(); it != m_SatelliteManagers.end(); it++)
    {
        (*it)->Update();
    }
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
amf_int32           AMF_STD_CALL AMFCaptureManagerImpl::GetDeviceCount()
{
    amf_int32 deviceCount = 0;
    for(std::vector<AMFCaptureManagerPtr>::iterator it = m_SatelliteManagers.begin(); it != m_SatelliteManagers.end(); it++)
    {
        deviceCount += (*it)->GetDeviceCount();
    }
    return deviceCount;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT          AMF_STD_CALL AMFCaptureManagerImpl::GetDevice(amf_int32 index,AMFCaptureDevice **pDevice)
{
    amf_int32 deviceCount = 0;
    for(std::vector<AMFCaptureManagerPtr>::iterator it = m_SatelliteManagers.begin(); it != m_SatelliteManagers.end(); it++)
    {
        deviceCount += (*it)->GetDeviceCount();
        if(deviceCount > index)
        {
            return (*it)->GetDevice(index - (deviceCount - (*it)->GetDeviceCount()),  pDevice);
        }
    }
    return AMF_NOT_FOUND;
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
