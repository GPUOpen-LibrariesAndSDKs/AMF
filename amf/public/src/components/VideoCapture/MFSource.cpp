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
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <propsys.h>
#include "MFSource.h"
#include "public/common/TraceAdapter.h"

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")

#pragma warning(disable: 4996)
using namespace amf;

#define REFTIMES_PER_SEC  10000000
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFMFSourceImpl::CaptureOnePacket(char** ppData, UINT& lenData)
{
    m_pSample.Release();
    m_pMediaBuf.Release();
    HRESULT hr = S_OK;
    while (1)
    {
        DWORD streamIndex, flags;
        LONGLONG llTimeStamp;
        hr = m_pReader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_ANY_STREAM), 0, &streamIndex, &flags, &llTimeStamp, &m_pSample);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"ReadSample() failed");

        if (!m_pSample)
        {
            Sleep(1);
            continue;
        }

        hr = m_pSample->GetBufferByIndex(0, &m_pMediaBuf);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"GetBufferByIndex() failed");

        if (m_pMediaBuf)
        {
            m_pMediaBuf->GetCurrentLength((DWORD*)&lenData);
            m_pMediaBuf->Lock((BYTE**)ppData, 0, 0);
        }
        break;
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFMFSourceImpl::CaptureOnePacketDone()
{
    if (m_pMediaBuf)
    {
        m_pMediaBuf->Unlock();
    }
    m_pMediaBuf.Release();
    m_pSample.Release();
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMFMFSourceImpl::AMFMFSourceImpl() :
m_pMediaSource(NULL),
m_pReader(NULL),
m_pSample(NULL),
m_pMediaBuf(NULL),
m_width(1920),
m_height(1080)
{
}
//-------------------------------------------------------------------------------------------------
AMFMFSourceImpl::~AMFMFSourceImpl()
{
    Close();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFMFSourceImpl::Close()
{
    AMF_RESULT err = AMF_OK;
    m_deviceList.clear();
    m_pMediaSource.Release();
    m_pReader.Release();
    return err;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFMFSourceImpl::Init(std::wstring& deviceNameActive)
{
    if (deviceNameActive.length() <= 0)
    {
        return CreateDeviceList();
    }

    if (m_deviceList.size() <= 0)
    {
        CreateDeviceList();
    }

    CComPtr<IMFAttributes> pAttributes;
    HRESULT hr = MFCreateAttributes(&pAttributes, 1);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"MFCreateAttributes() failed");

    hr = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"SetGUID() failed");

    IMFActivate** ppActivateSources;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(pAttributes, &ppActivateSources, &count);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"MFEnumDeviceSources() failed");

    CComPtr<IMFActivate> pActivateSource;

    for (UINT32 i = 0; SUCCEEDED(hr) && (i < count); i++)
    {
        LPWSTR pName = 0;
        hr = ppActivateSources[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &pName, 0);
        AMF_ASSERT(SUCCEEDED(hr), L"GetAllocatedString() failed");

        if (deviceNameActive == pName)
        {
            hr = ppActivateSources[i]->ActivateObject(IID_IMFMediaSource, (void**)&pActivateSource);
            AMF_ASSERT(SUCCEEDED(hr), L"ActivateObject() failed");
        }
        CoTaskMemFree(pName);
        ppActivateSources[i]->Release();
    }
    CoTaskMemFree(ppActivateSources);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"ActivateObject failed");

    CComPtr<IMFCollection> pCollection;
    hr = MFCreateCollection(&pCollection);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"MFCreateCollection() failed");

    hr = pCollection->AddElement((IUnknown*)pActivateSource);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"AddElement() failed");

    hr = MFCreateAggregateSource(pCollection, &m_pMediaSource);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"MFCreateAggregateSource() failed");

    hr = SelectSource(m_pMediaSource);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"SelectSource() failed");
 
    pAttributes.Release();
    pActivateSource.Release();
    pCollection.Release();

    hr = MFCreateSourceReaderFromMediaSource(m_pMediaSource, NULL, &m_pReader);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"SelectSource() failed");

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFMFSourceImpl::CreateDeviceList()
{
    HRESULT hr = S_OK;
    CComPtr<IMFAttributes> pAttributes;
    hr = MFCreateAttributes(&pAttributes, 1);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"MFCreateAttributes() failed");

    hr = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"SetGUID() failed");

    IMFActivate** ppActivateSources;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(pAttributes, &ppActivateSources, &count);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"MFEnumDeviceSources() failed");

    // List names
    for (UINT32 i = 0; i < count; i++)
    {
        LPWSTR pName = 0;
        hr = ppActivateSources[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &pName, 0);

        if (!SUCCEEDED(hr))
        {
            AMFTraceError(L"AMFMFSourceImpl", L"GetAllocatedString() failed!");
            break;
        }

        amf_wstring name(pName);
        amf_string nameNew(amf::amf_from_unicode_to_utf8(name));
        m_deviceList.push_back(nameNew.c_str());
        CoTaskMemFree(pName);
    }

    if (ppActivateSources)
    {
        // Release array elements
        for (UINT32 i = 0; i < count; i++)
        {
            ppActivateSources[i]->Release();
            ppActivateSources[i] = NULL;
        }
    }

    CoTaskMemFree(ppActivateSources);
    ppActivateSources = NULL;
    m_selected = 0;
    pAttributes.Release();
    return SUCCEEDED(hr) ? AMF_OK : AMF_FAIL;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFMFSourceImpl::SelectSource(CComPtr<IMFMediaSource> pMediaSource)
{
    CComPtr<IMFPresentationDescriptor> pPresentationDesc;
    HRESULT hr = pMediaSource->CreatePresentationDescriptor(&pPresentationDesc);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"CreatePresentationDescriptor() failed");

    UINT32 widthMax = 0;
    UINT32 heightMax = 0;
    UINT32 selection = 0;

    DWORD streamCount = 0;
    pPresentationDesc->GetStreamDescriptorCount(&streamCount);
    CComPtr<IMFStreamDescriptor> pStreamDesc(NULL);

    for (UINT32 i = 0; i < streamCount && SUCCEEDED(hr); i++)
    {
        BOOL selected(FALSE);
        hr = pPresentationDesc->GetStreamDescriptorByIndex(i, &selected, &pStreamDesc);

        if (!SUCCEEDED(hr))
        {
            AMFTraceError(L"AMFMFSourceImpl", L"GetStreamDescriptorByIndex() failed!");
            break;
        }

        if (!selected)
        {
            continue;
        }

        CComPtr<IMFMediaTypeHandler> pHandler(NULL);
        hr = pStreamDesc->GetMediaTypeHandler(&pHandler);

        if (!SUCCEEDED(hr))
        {
            AMFTraceError(L"AMFMFSourceImpl", L"GetMediaTypeHandler() failed!");
            break;
        }

        DWORD types = 0;
        hr = pHandler->GetMediaTypeCount(&types);
        if (!SUCCEEDED(hr))
        {
            AMFTraceError(L"AMFMFSourceImpl", L"GetMediaTypeCount() failed!");
            break;
        }

        float  frameRate = 0.0;

        //find the best resolution
        for (DWORD j = 0; j < types && SUCCEEDED(hr); j++)
        {
            CComPtr<IMFMediaType> pMediaType(NULL);
            hr = pHandler->GetMediaTypeByIndex(j, &pMediaType);
            if (!SUCCEEDED(hr))
            {
                AMFTraceError(L"AMFMFSourceImpl", L"GetMediaTypeByIndex() failed");
                break;
            }

            MFVIDEOFORMAT*  pMFVF(NULL);
            UINT32          cbSize(0);
            hr = MFCreateMFVideoFormatFromMFMediaType(pMediaType, &pMFVF, &cbSize);

            if (!SUCCEEDED(hr))
            {
                pMediaType.Release();
                AMFTraceError(L"AMFMFSourceImpl", L"MFCreateMFVideoFormatFromMFMediaType() failed");
                break;
            }

            GUID guidMajor = GUID_NULL;
            hr = pMediaType->GetGUID(MF_MT_MAJOR_TYPE, &guidMajor);
            if (!SUCCEEDED(hr))
            {
                pMediaType.Release();
                AMFTraceError(L"AMFMFSourceImpl", L"GetGUID() failed");
                break;
            }

            GUID guidSubType = GUID_NULL;
            hr = pMediaType->GetGUID(MF_MT_SUBTYPE, &guidSubType);
            if (!SUCCEEDED(hr))
            {
                pMediaType.Release();
                AMFTraceError(L"AMFMFSourceImpl", L"GetGUID() failed");
                break;
            }

            UINT32 width = 0;
            UINT32 height = 0;
            MFGetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, &width, &height);

            UINT32 numerator = 0;
            UINT32 denominator = 0;
            MFGetAttributeRatio(pMediaType, MF_MT_FRAME_RATE, &numerator, &denominator);
            if (guidMajor != MFMediaType_Video)
            {
                ;
            }
            else if (guidSubType == MFVideoFormat_NV12)//skip raw for now
            {
                ;
            }
            else if (width*height > widthMax*heightMax)
            {
                widthMax = width;
                heightMax = height;
                frameRate = float(numerator) / float(denominator);
                selection = j;
                m_guidSubType = guidSubType;
                memcpy(&m_videoFormat, pMFVF, cbSize);
            }
            else if (width*height == widthMax*heightMax)
            {
                float frameRate2 = float(numerator) / float(denominator);
                if (frameRate < frameRate2)
                {
                    frameRate = frameRate2;
                    selection = j;
                    m_guidSubType = guidSubType;
                    memcpy(&m_videoFormat, pMFVF, cbSize);
                }
            }
            pMediaType.Release();
        }

        if (SUCCEEDED(hr))
        {
            CComPtr<IMFMediaType> pMediaType(NULL);
            pHandler->GetMediaTypeByIndex(selection, &pMediaType);
            pHandler->SetCurrentMediaType(pMediaType);
            MFGetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, &m_width, &m_height);

            UINT32 numerator = 0;
            UINT32 denominator = 0;
            MFGetAttributeRatio(pMediaType, MF_MT_FRAME_RATE, &numerator, &denominator);
            frameRate = float(numerator) / float(denominator);
            m_duration = (amf_pts)(AMF_SECOND / frameRate);

            pMediaType.Release();
        }

        pHandler.Release();
        pStreamDesc.Release();
    }

    pPresentationDesc.Release();
    return SUCCEEDED(hr) ? AMF_OK : AMF_FAIL;
}
//-------------------------------------------------------------------------------------------------
