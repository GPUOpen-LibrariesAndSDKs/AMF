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
#include "MFCaptureImpl.h"
#include "public/common/TraceAdapter.h"
#include <windows.h>
#include <propsys.h>
#include <atlcomcli.h>
#include <D3d11_4.h>

//#include <windows.h>
//#include <windowsx.h>
#include <mferror.h>
//#include <shlwapi.h>
//#include <strsafe.h>
//#include <commctrl.h>
//#include <vector>
//#include <mmdeviceapi.h>
//#include <mfcaptureengine.h>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "strmiids.lib")


#define AV_CODEC_ID_MP3 0x15000+1   // works with current FFmpeg only
#define AV_CODEC_ID_AAC 0x15000+2   // works with current FFmpeg only

using namespace amf;

#define AMF_FACILITY L"MFCaptureImpl"

static GUID  AMFTextureArrayIndexGUID = { 0x28115527, 0xe7c3, 0x4b66, 0x99, 0xd3, 0x4f, 0x2a, 0xe6, 0xb4, 0x7f, 0xaf}; // sizeof(amf_int)

extern "C"
{
    AMF_RESULT AMF_CDECL_CALL AMFCreateCaptureManagerMediaFoundation(amf::AMFContext* pContext, amf::AMFCaptureManager** ppManager)
    {
        *ppManager = new amf::AMFInterfaceMultiImpl< amf::MFCaptureManagerImpl, amf::AMFCaptureManager, amf::AMFContext*>(pContext);
        (*ppManager)->Acquire();
        return AMF_OK;
    }
}

//-------------------------------------------------------------------------------------------------
MFCaptureManagerImpl::MFCaptureManagerImpl(amf::AMFContext* pContext) :
m_pContext(pContext)
{
}
//-------------------------------------------------------------------------------------------------
MFCaptureManagerImpl::~MFCaptureManagerImpl()
{
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT          AMF_STD_CALL MFCaptureManagerImpl::Update()
{
    m_Devices.clear();

    CComPtr<IMFAttributes> pAttributes;
    HRESULT hr = MFCreateAttributes(&pAttributes, 1);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"MFCreateAttributes() failed");

    // enumerate video devices
    hr = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"SetGUID() failed");

    IMFActivate** ppActivateSources = NULL;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(pAttributes, &ppActivateSources, &count);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"MFEnumDeviceSources() failed");

    for (UINT32 i = 0; i < count; i++)
    {
        Device device;
        device.video = ppActivateSources[i];

        LPWSTR pName = 0;
        hr = ppActivateSources[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &pName, 0);
        if(pName != NULL)
        {
            device.name = pName;
        }
        CoTaskMemFree(pName);

        LPWSTR pID = 0;
        hr = ppActivateSources[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &pID, 0);
        if(pID != NULL)
        {
            device.id = pID;

        }
        CoTaskMemFree(pID);
        AMFTraceInfo(AMF_FACILITY, L"Video Device: %s ,link = %s", device.name.c_str(), device.id.c_str());

        GUID typeGUID;
        ppActivateSources[i]->GetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &typeGUID);

        m_Devices.push_back(device);
        ppActivateSources[i]->Release();
    }
    CoTaskMemFree(ppActivateSources);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"ActivateObject failed");

    // enumerate audio devices
    hr = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"SetGUID() failed");

    ppActivateSources = NULL;
    count = 0;
    hr = MFEnumDeviceSources(pAttributes, &ppActivateSources, &count);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"MFEnumDeviceSources() failed");

    for (UINT32 i = 0; i < count; i++)
    {
        LPWSTR pName = 0;
        hr = ppActivateSources[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &pName, 0);
        amf_wstring name;
        amf_wstring id;
        if(pName != NULL)
        {
            name = pName;
        }
        CoTaskMemFree(pName);

        LPWSTR pID = 0;
//        hr = ppActivateSources[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_ENDPOINT_ID, &pID, 0);
        hr = ppActivateSources[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_SYMBOLIC_LINK, &pID, 0);

        if(pID != NULL)
        {
            id = pID;

        }
        CoTaskMemFree(pID);
        AMFTraceInfo(AMF_FACILITY, L"Audio Device: %s, link = %s", name.c_str(), id.c_str());


        GUID typeGUID;
        ppActivateSources[i]->GetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &typeGUID);

        // match with video device
        bool bFound = false;
        for(amf_vector< Device >::iterator it = m_Devices.begin(); it != m_Devices.end(); it++)
        {
            if(it->name == name)
            {
                it->audio = ppActivateSources[i];
                bFound = true;
                break;
            }
        }
        if(!bFound)
        {
            Device device;
            device.name = name;
            device.id = id;
            device.audio = ppActivateSources[i];
            m_Devices.push_back(device);
        }
        ppActivateSources[i]->Release();
    }
    CoTaskMemFree(ppActivateSources);
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
amf_int32           AMF_STD_CALL MFCaptureManagerImpl::GetDeviceCount()
{
    return (amf_int32)m_Devices.size();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT          AMF_STD_CALL MFCaptureManagerImpl::GetDevice(amf_int32 index,AMFCaptureDevice **pDevice)
{
    AMF_RETURN_IF_FALSE(index < amf_int32(m_Devices.size()), AMF_INVALID_ARG, L"Invlid device index %d", (int)index);
    AMFMFCaptureImplPtr device = new amf::AMFInterfaceMultiImpl< amf::AMFMFCaptureImpl, amf::AMFCaptureDevice, amf::AMFContext*>(m_pContext);
    AMF_RESULT res = device->UpdateDevice(m_Devices[index].video, m_Devices[index].audio, m_Devices[index].name.c_str());
    AMF_RETURN_IF_FAILED(res, L"InitDeckLink() failed");
    *pDevice = device.Detach();
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
static const AMFEnumDescriptionEntry AMF_CAPTURE_VIDEO_FORMAT_DESC[] =
{

    {AMF_SURFACE_UNKNOWN, L"UNKNOWN"  },
    {AMF_SURFACE_NV12,    L"NV12"  },
    {AMF_SURFACE_UYVY,    L"UYVY"  },
    {AMF_SURFACE_RGBA,    L"RGBA" },
    {AMF_SURFACE_BGRA,    L"BGRA" },
    {0, NULL}
};

//-------------------------------------------------------------------------------------------------
static const AMFEnumDescriptionEntry AMF_CAPTURE_VIDEO_MEMORY_TYPE_DESC[] =
{
    {AMF_MEMORY_DX11, L"DX11"},
    {AMF_MEMORY_HOST, L"HOST"},
    {0, NULL}
};

//-------------------------------------------------------------------------------------------------
AMFMFCaptureImpl::AMFOutputBase::AMFOutputBase(AMFMFCaptureImpl* pHost, IMFActivate* pActivate) :
    m_pHost(pHost),
    m_pActivate(pActivate),
    m_iSelectedIndex(-1),
    m_PollingThread(this),
    m_bEof(false)
{
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFMFCaptureImpl::AMFOutputBase::Init()
{
    m_bEof = false;
    if(m_pReader != NULL)
    {
        return AMF_OK;
    }
    HRESULT hr = S_OK;
    CComPtr<IMFActivate> pActivateSource;
    hr = m_pActivate->ActivateObject(IID_IMFMediaSource, (void**)&pActivateSource);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"ActivateObject(IID_IMFMediaSource) failed");

    CComPtr<IMFCollection> pCollection;
    hr = MFCreateCollection(&pCollection);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"MFCreateCollection() failed");

    hr = pCollection->AddElement((IUnknown*)pActivateSource);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"AddElement() failed");

    hr = MFCreateAggregateSource(pCollection, &m_pMediaSource);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"MFCreateAggregateSource() failed");

    hr = SelectSource(m_pMediaSource);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"SelectSource() failed");

    // TODO set MF_SOURCE_READER_D3D_MANAGER and MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS  as attribute storage - second parameter

    CComPtr<IMFAttributes> pIMFAttributes;
    GetInitAttributes(&pIMFAttributes);

    hr = MFCreateSourceReaderFromMediaSource(m_pMediaSource, pIMFAttributes, &m_pReader);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"SelectSource() failed");

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFMFCaptureImpl::AMFOutputBase::QueryOutput(AMFData** ppData)
{
    if (m_bEof)
    {
        return AMF_EOF;
    }

    AMF_RESULT  err = AMF_REPEAT;
    AMFDataPtr  pFrame;
    amf_ulong   ulID = 0;

    if (m_DataQueue.Get(ulID, pFrame, 0))
    {
        *ppData = pFrame.Detach();
        err = AMF_OK;
    }
    return err;
}
AMF_RESULT  AMF_STD_CALL    AMFMFCaptureImpl::AMFOutputBase::Start()
{
    m_bEof = false;
    m_PollingThread.Start();
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL    AMFMFCaptureImpl::AMFOutputBase::Stop()
{
    m_PollingThread.RequestStop();
    m_PollingThread.WaitForStop();
    m_DataQueue.Clear();
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL    AMFMFCaptureImpl::AMFOutputBase::Drain()
{
    m_bEof = true;
    return Stop();
}
//-------------------------------------------------------------------------------------------------
void AMFMFCaptureImpl::AMFOutputBase::PollingThread::Run()
{
    while (!StopRequested())
    {
        HRESULT hr = S_OK;
        AMF_RESULT res = AMF_OK;
        CComPtr<IMFSample>       pSample;

        DWORD streamIndex, flags;
        LONGLONG llTimeStamp;
        hr = m_pHost->m_pReader->ReadSample((DWORD)m_pHost->m_iSelectedIndex, 0, &streamIndex, &flags, &llTimeStamp, &pSample);
        if(FAILED(hr))
        {
            AMFTraceError(AMF_FACILITY, L"ReadSample() failed %s", amf::AMFFormatHResult(hr).c_str());
        }
        if (pSample == NULL)
        {
            amf_sleep(1);
            continue;
        }

        CComPtr<IMFMediaBuffer>  pMediaBuf;

        hr = pSample->GetBufferByIndex(0, &pMediaBuf);
        if(FAILED(hr) || pMediaBuf == NULL)
        {
            AMFTraceError(AMF_FACILITY, L"GetBufferByIndex() failed", hr);
            continue;
        }

        AMFDataPtr pData;
        res = m_pHost->ConvertData(pSample, pMediaBuf, &pData);
        if(res != AMF_OK)
        {
            AMFTraceError(AMF_FACILITY, L"GetBufferByIndex() failed", hr);
            continue;
        }
        LONGLONG sampleTime = 0;
        pSample->GetSampleTime(&sampleTime); // in 100-nanosecond units
        if(SUCCEEDED(hr))
        {
            pData->SetPts(amf_pts(sampleTime));
        }

        LONGLONG sampleDuration = 0;
        hr = pSample->GetSampleDuration(&sampleDuration); // in 100-nanosecond units
        if(SUCCEEDED(hr))
        {
            pData->SetDuration(amf_pts(sampleDuration));
        }
        while (!StopRequested())
        {
            if(m_pHost->m_DataQueue.Add(0, pData, 0, 0))
            {
                break;
            }
            amf_sleep(1); // milliseconds
        }
    }
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL    AMFMFCaptureImpl::AMFOutputBase::ConvertData(IMFSample* /* pSample */, IMFMediaBuffer* pMediaBufffer, AMFData** ppData)
{
    HRESULT hr = S_OK;
    AMF_RESULT res = AMF_OK;
    // all compressed buffers
    DWORD lenData = 0;
    pMediaBufffer->GetCurrentLength((DWORD*)&lenData);
    AMF_RETURN_IF_FALSE(lenData != 0, AMF_FAIL, L"GetCurrentLength() == 0");

    AMFBufferPtr pBuffer;
    res = m_pHost->m_pContext->AllocBuffer(AMF_MEMORY_HOST, lenData, &pBuffer);
    AMF_RETURN_IF_FAILED(res, L"AllocBuffer() failed");

    void *pData = NULL;
    hr = pMediaBufffer->Lock((BYTE**)&pData, 0, 0);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"pMediaBufffer->Lock() failed");

    memcpy(pBuffer->GetNative(), pData, lenData);

    pMediaBufffer->Unlock();
    *ppData = pBuffer.Detach();
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMFMFCaptureImpl::AMFVideoOutput::AMFVideoOutput(AMFMFCaptureImpl* pHost, IMFActivate* pActivate) :
    AMFOutputBase(pHost, pActivate)
{
    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoEnum (AMF_STREAM_VIDEO_MEMORY_TYPE,  AMF_STREAM_VIDEO_MEMORY_TYPE, AMF_MEMORY_HOST, AMF_CAPTURE_VIDEO_MEMORY_TYPE_DESC, false),
        AMFPropertyInfoEnum (AMF_STREAM_VIDEO_FORMAT,       AMF_STREAM_VIDEO_FORMAT, AMF_SURFACE_UNKNOWN, AMF_CAPTURE_VIDEO_FORMAT_DESC, false),
        AMFPropertyInfoRate (AMF_STREAM_VIDEO_FRAME_RATE,   AMF_STREAM_VIDEO_FRAME_RATE, 0, 1,  false),
        AMFPropertyInfoSize (AMF_STREAM_VIDEO_FRAME_SIZE,   AMF_STREAM_VIDEO_FRAME_SIZE, AMFConstructSize(0, 0), AMFConstructSize(0, 0) , AMFConstructSize(4096, 4096),  false),
        AMFPropertyInfoInt64(AMF_STREAM_VIDEO_SURFACE_POOL, AMF_STREAM_VIDEO_SURFACE_POOL, 5, 0, INT_MAX, false),
        AMFPropertyInfoInt64(AMF_STREAM_TYPE,               AMF_STREAM_TYPE, AMF_STREAM_VIDEO, AMF_STREAM_VIDEO, AMF_STREAM_VIDEO, false),
        AMFPropertyInfoBool (AMF_STREAM_ENABLED,            AMF_STREAM_ENABLED, false, true),
        AMFPropertyInfoInt64(AMF_STREAM_CODEC_ID,           AMF_STREAM_CODEC_ID, 0, 0, INT_MAX, false),

    AMFPrimitivePropertyInfoMapEnd

    m_DataQueue.SetQueueSize(30);
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL    AMFMFCaptureImpl::AMFVideoOutput::GetInitAttributes(IMFAttributes** ppAttributes)
{
    if(m_Format.m_iCodecID != 0)
    {
        return AMF_OK;
    }
    void *pDevice = m_pHost->m_pContext->GetDX11Device();
    if(pDevice == NULL)
    {
        return AMF_OK;
    }

    // Function MFCreateDXGIDeviceManager exists only on Windows 8+. So we have to
    // obtain its address dynamically to be able work on Windows 7.

    HMODULE mfplatDll = LoadLibrary(L"mfplat.dll");
    AMF_RETURN_IF_FALSE(mfplatDll != NULL, AMF_FAIL, L"Failed to load mfplat.dll");

    typedef HRESULT (STDAPICALLTYPE* LPMFCreateDXGIDeviceManager)(UINT* resetToken, IMFDXGIDeviceManager** ppDeviceManager);

    LPMFCreateDXGIDeviceManager mfCreateDXGIDeviceManager = (LPMFCreateDXGIDeviceManager)GetProcAddress(mfplatDll, "MFCreateDXGIDeviceManager");
    AMF_RETURN_IF_FALSE(mfCreateDXGIDeviceManager != NULL, AMF_FAIL, L"Failed to get address of MFCreateDXGIDeviceManager()");

    UINT resetToken;

    HRESULT hr = mfCreateDXGIDeviceManager(&resetToken, &m_pDxgiDeviceManager);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"mfCreateDXGIDeviceManager() failed");

    hr = m_pDxgiDeviceManager->ResetDevice((ID3D11Device*)pDevice, resetToken);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"ResetDevice() failed");

    MFCreateAttributes(ppAttributes, 2);

//    (*ppAttributes)->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, m_pDxgiDeviceManager);
    (*ppAttributes)->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, 1);

    return AMF_OK;
}
static AMFSize StandardResolutions[] ={
        { 3840, 1920 },  //todo, this is for temporary use only, to support the Ricoh ThetaV
        { 1280, 720 },
        {1920, 1080},
        {3840, 2160},
};

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL   AMFMFCaptureImpl::AMFVideoOutput::SelectSource(IMFMediaSource* pMediaSource)
{

    CComQIPtr<IMFMediaSourceEx> pMediaSourceEx(pMediaSource);

    CComPtr<IMFPresentationDescriptor> pPresentationDesc;
    HRESULT hr = pMediaSource->CreatePresentationDescriptor(&pPresentationDesc);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"CreatePresentationDescriptor() failed");

    AMFSize framesizeRequested = {};
    GetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, &framesizeRequested);
    AMFRate framerateRequested = {};
    GetProperty(AMF_STREAM_VIDEO_FRAME_RATE, &framerateRequested);
    amf_int64 formatRequested = AMF_SURFACE_UNKNOWN;
    GetProperty(AMF_STREAM_VIDEO_FORMAT, &formatRequested);
    amf_int64 codecRequested = 0;
    GetProperty(AMF_STREAM_CODEC_ID, &codecRequested);

    GUID guidRequestedFormat = GUID_NULL;


    switch(formatRequested)
    {
    case AMF_SURFACE_NV12: guidRequestedFormat = MFVideoFormat_NV12; break;
    case AMF_SURFACE_BGRA: guidRequestedFormat = MFVideoFormat_RGB32; break;
    case AMF_SURFACE_RGBA: guidRequestedFormat = MFVideoFormat_RGB32; break;
    case AMF_SURFACE_UNKNOWN: guidRequestedFormat = GUID_NULL; break;
    }

    switch(codecRequested)
    {
    case AMF_STREAM_CODEC_ID_H264_AVC: guidRequestedFormat = MFVideoFormat_H264; break;
    case AMF_STREAM_CODEC_ID_H265_HEVC: guidRequestedFormat = MFVideoFormat_HEVC; break;
    case AMF_STREAM_CODEC_ID_MJPEG: guidRequestedFormat = MFVideoFormat_MJPG; break;
    }

    Format StandardResolutionsFound[amf_countof(StandardResolutions)];
    Format biggestResolutionFound;

    CComPtr<IMFMediaTypeHandler> pSelectedHandler;

    amf_int64 maxPixelCount = 0;


    DWORD streamCount = 0;
    pPresentationDesc->GetStreamDescriptorCount(&streamCount);

    for (UINT32 i = 0; i < streamCount; i++)
    {
        CComPtr<IMFStreamDescriptor> pStreamDesc;
        BOOL selected(FALSE);
        hr = pPresentationDesc->GetStreamDescriptorByIndex(i, &selected, &pStreamDesc);

        if (FAILED(hr))
        {
            AMFTraceError(AMF_FACILITY, L"GetStreamDescriptorByIndex() failed!");
            continue;
        }
//        if (!selected)
//        {
//            continue;
//        }

        CComPtr<IMFMediaTypeHandler> pHandler;
        hr = pStreamDesc->GetMediaTypeHandler(&pHandler);

        if (FAILED(hr))
        {
            AMFTraceError(AMF_FACILITY, L"GetMediaTypeHandler() failed!");
            continue;
        }

        DWORD typeCount = 0;
        hr = pHandler->GetMediaTypeCount(&typeCount);
        if (FAILED(hr))
        {
            AMFTraceError(L"AMFMFSourceImpl", L"GetMediaTypeCount() failed!");
            continue;
        }


        //find the best resolution
        for (DWORD j = 0; j < typeCount ; j++)
        {
            // get all properties
            CComPtr<IMFMediaType> pMediaType;
            hr = pHandler->GetMediaTypeByIndex(j, &pMediaType);
            if (FAILED(hr))
            {
                AMFTraceError(L"AMFMFSourceImpl", L"GetMediaTypeByIndex() failed");
                continue;
            }

            const wchar_t *pType = L"";
            GUID guidMajor = GUID_NULL;
            hr = pMediaType->GetGUID(MF_MT_MAJOR_TYPE, &guidMajor);

            if(guidMajor == MFMediaType_Video)
            {
                pType = L"Video";
            }
            else if(guidMajor == MFMediaType_Audio)
            {
                pType = L"Audio";
            }
            else if(guidMajor == MFMediaType_Image)
            {
                pType = L"Image";
            }



            GUID guidSubType = GUID_NULL;
            hr = pMediaType->GetGUID(MF_MT_SUBTYPE, &guidSubType);

            amf_int64 codec = 0;
            amf_int64 format = 0;

            const wchar_t * codecName = L"";

            if(guidSubType == MFVideoFormat_NV12) { format = AMF_SURFACE_NV12;}
            else if(guidSubType == MFVideoFormat_RGB32) {format = AMF_SURFACE_RGBA;}
            else if(guidSubType == MFVideoFormat_H264) {codec = AMF_STREAM_CODEC_ID_H264_AVC; codecName= L"H264";}
            else if(guidSubType == MFVideoFormat_HEVC || guidSubType == MFVideoFormat_HEVC || guidSubType == MFVideoFormat_HEVC_ES) {codec = AMF_STREAM_CODEC_ID_H265_HEVC; codecName= L"H265";}
            else if(guidSubType == MFVideoFormat_MJPG) {codec = AMF_STREAM_CODEC_ID_MJPEG; codecName= L"MJPG";}


            UINT32 numerator = 0;
            UINT32 denominator = 0;
            MFGetAttributeRatio(pMediaType, MF_MT_FRAME_RATE, &numerator, &denominator);

            UINT32 width = 0;
            UINT32 height = 0;
            MFGetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, &width, &height);

            // trace properties

            AMFTraceInfo(AMF_FACILITY, L"Stream:%d Media:%d %s %s %s %dx%d@%5.2f" , i, j, pType, AMFSurfaceGetFormatName(AMF_SURFACE_FORMAT(format)), codecName, width, height, double(numerator) / denominator);

            // verify properties

            if(guidMajor != MFMediaType_Video)
            {
                continue;
            }


            if(guidRequestedFormat != GUID_NULL && guidSubType != guidRequestedFormat)
            {
                continue;
            }

            if(codec ==  0 && format == 0)
//            if(codec ==  0) // find compressed
//            if(format == 0) // uncompressed
            {
                continue;
            }


            if(framerateRequested.num != 0 && (framerateRequested.num != numerator || framerateRequested.den != denominator))
            {
                continue;
            }
            if(numerator == 1 && denominator == 1)
            {
                continue; // photo stream
            }


            if(framesizeRequested.width != 0 && framesizeRequested.height != 0)

            {
                if(static_cast<amf_uint32>(framesizeRequested.width) != width ||
                   static_cast<amf_uint32>(framesizeRequested.height) != height)
                {
                    continue;
                }
            }
            else if(maxPixelCount >= amf_int64(width) * height)
            {
                continue;
            }

            for(int k = 0; k < amf_countof(StandardResolutions); k++)
            {
                if(static_cast<amf_uint32>(StandardResolutions[k].width) == width &&
                   static_cast<amf_uint32>(StandardResolutions[k].height) == height)
                {
                    if(double(numerator) / denominator > double(StandardResolutionsFound[k].m_FrameRate.num) / StandardResolutionsFound[k].m_FrameRate.den) // maximum frame rate
                    {
                        StandardResolutionsFound[k].m_FrameRate.num = numerator;
                        StandardResolutionsFound[k].m_FrameRate.den = denominator;
                        StandardResolutionsFound[k].m_FrameSize.width = width;
                        StandardResolutionsFound[k].m_FrameSize.height = height;
                        StandardResolutionsFound[k].m_eFormat = AMF_SURFACE_FORMAT(format);
                        StandardResolutionsFound[k].m_iCodecID = codec;
                        StandardResolutionsFound[k].m_iMediaTypeIndex = j;
                        StandardResolutionsFound[k].m_iStreamIndex = i;
                    }
                    break;
                }
            }


            maxPixelCount = amf_int64(width) * height;

            biggestResolutionFound.m_FrameRate.num = numerator;
            biggestResolutionFound.m_FrameRate.den = denominator;
            biggestResolutionFound.m_FrameSize.width = width;
            biggestResolutionFound.m_FrameSize.height = height;
            biggestResolutionFound.m_eFormat = AMF_SURFACE_FORMAT(format);
            biggestResolutionFound.m_iCodecID = codec;
            biggestResolutionFound.m_iMediaTypeIndex = j;
            biggestResolutionFound.m_iStreamIndex = i;


            pSelectedHandler = pHandler;
        }
    }

    AMF_RETURN_IF_FALSE(biggestResolutionFound.m_iMediaTypeIndex >= 0 && biggestResolutionFound.m_iStreamIndex >= 0, AMF_NOT_FOUND, L"Video Stream not found");

    m_Format = biggestResolutionFound;
    // find standard resolution
    for(int k = amf_countof(StandardResolutions) - 1; k >=0 ; k--)
    {
        if(StandardResolutionsFound[k].m_iMediaTypeIndex >= 0 && StandardResolutionsFound[k].m_iStreamIndex >= 0)
        {
            m_Format = StandardResolutionsFound[k];
            break;
        }
    }



    pPresentationDesc->SelectStream(m_Format.m_iStreamIndex);
    CComPtr<IMFMediaType> pMediaType;
    pSelectedHandler->GetMediaTypeByIndex(m_Format.m_iMediaTypeIndex, &pMediaType);
    pSelectedHandler->SetCurrentMediaType(pMediaType);

    SetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, m_Format.m_FrameSize);
    SetProperty(AMF_STREAM_VIDEO_FRAME_RATE, m_Format.m_FrameRate);
    SetProperty(AMF_STREAM_VIDEO_FORMAT, m_Format.m_eFormat);
    SetProperty(AMF_STREAM_CODEC_ID, m_Format.m_iCodecID);

    m_iSelectedIndex = m_Format.m_iStreamIndex;


    if(m_Format.m_iCodecID != 0)
    {
        UINT32 cbBlob = 0;
        pMediaType->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &cbBlob);
        if(cbBlob > 0 )
        {
            AMFBufferPtr pBuffer;
            m_pHost->m_pContext->AllocBuffer(AMF_MEMORY_HOST, cbBlob, &pBuffer);
            pMediaType->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, (BYTE*)pBuffer->GetNative(), cbBlob, &cbBlob);
            SetProperty(AMF_STREAM_EXTRA_DATA, AMFInterfacePtr(pBuffer));
        }
    }
     AMFTraceInfo(AMF_FACILITY, L"Selected: Stream:%d Media:%d %s %d %dx%d@%5.2f" , m_Format.m_iStreamIndex, m_Format.m_iMediaTypeIndex, AMFSurfaceGetFormatName(m_Format.m_eFormat),
         m_Format.m_iCodecID, m_Format.m_FrameSize.width, m_Format.m_FrameSize.height, double(m_Format.m_FrameRate.num) / m_Format.m_FrameRate.den);

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL    AMFMFCaptureImpl::AMFVideoOutput::ConvertData(IMFSample* pSample, IMFMediaBuffer* pMediaBufffer, AMFData **ppData)
{
    if(m_Format.m_iCodecID != 0) // compressed stream
    {
        return AMFOutputBase::ConvertData(pSample, pMediaBufffer, ppData);
    }
    // uncompressed stream
    HRESULT hr = S_OK;
    AMF_RESULT res = AMF_OK;
    // check for Video surface
    if(m_pDxgiDeviceManager != NULL)
    {
        CComQIPtr<IMFDXGIBuffer> spD11Buffer(pMediaBufffer);
        if (spD11Buffer != NULL)
        {
            UINT subResourceIndex = 0;
            spD11Buffer->GetSubresourceIndex(&subResourceIndex);

            CComPtr<ID3D11Texture2D> pSurface;
            hr = spD11Buffer->GetResource(__uuidof(ID3D11Texture2D), (void**)&pSurface);
            if (SUCCEEDED(hr) && pSurface != NULL)
            {
                // check device
                CComPtr<ID3D11Device> pSurfaceDevice;
                pSurface->GetDevice(&pSurfaceDevice);
                if(pSurfaceDevice == (ID3D11Device*)m_pHost->m_pContext->GetDX11Device())
                {
                    AMFSurfacePtr pSurfaceOut;

                    pSurface->SetPrivateData(AMFTextureArrayIndexGUID, sizeof(subResourceIndex), &subResourceIndex);
                    res = m_pHost->m_pContext->CreateSurfaceFromDX11Native(pSurface, &pSurfaceOut, NULL);
                    if (res == AMF_OK)
                    {
                        *ppData = pSurfaceOut;
                        (*ppData)->Acquire();
                        pSurfaceOut->AddObserver(new InputSampleTracker(pSample));

                        return AMF_OK;
                    }
                }
            }
        }
    }

    // all compressed buffers
    DWORD lenData = 0;
    pMediaBufffer->GetCurrentLength((DWORD*)&lenData);
    AMF_RETURN_IF_FALSE(lenData != 0, AMF_FAIL, L"GetCurrentLength() == 0");
    void *pData = NULL;
    hr = pMediaBufffer->Lock((BYTE**)&pData, 0, 0);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"pMediaBufffer->Lock() failed");

    // TODO add support for DX
    AMFSurfacePtr pSurfaceWrapped;
    res = m_pHost->m_pContext->CreateSurfaceFromHostNative(m_Format.m_eFormat, m_Format.m_FrameSize.width, m_Format.m_FrameSize.height,0,0,pData, &pSurfaceWrapped, NULL);

    res = pSurfaceWrapped->Duplicate(AMF_MEMORY_HOST, ppData);
    pMediaBufffer->Unlock();

    AMF_RETURN_IF_FAILED(res, L"Duplicate() failed");
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
static const AMFEnumDescriptionEntry AMF_CAPTURE_AUDIO_FORMAT_DESC[] =
{
    {AMFAF_UNKNOWN, L"UNKNOWN" },
    {AMFAF_S16 , L"S16" },
    {AMFAF_S32 , L"S32" },
    {AMFAF_FLT , L"FLT" },
    //TODO add planar if needed
    {0, NULL}
};

//-------------------------------------------------------------------------------------------------
AMFMFCaptureImpl::AMFAudioOutput::AMFAudioOutput(AMFMFCaptureImpl* pHost, IMFActivate* pActivate) :
    AMFOutputBase(pHost, pActivate),
    m_eFormat(AMFAF_UNKNOWN),
    m_iSampleRate(0),
    m_iChannels(0),
    m_iCodecID(0)

{
    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoEnum( AMF_STREAM_AUDIO_FORMAT,       AMF_STREAM_AUDIO_FORMAT, AMFAF_S16, AMF_CAPTURE_AUDIO_FORMAT_DESC, false),
        AMFPropertyInfoInt64(AMF_STREAM_AUDIO_SAMPLE_RATE,  AMF_STREAM_AUDIO_SAMPLE_RATE,  0, 0, 256000, false),
        AMFPropertyInfoInt64(AMF_STREAM_AUDIO_CHANNELS,     AMF_STREAM_AUDIO_CHANNELS,  0, 0, 128, false),
        AMFPropertyInfoInt64(AMF_STREAM_TYPE,               AMF_STREAM_TYPE, AMF_STREAM_AUDIO, AMF_STREAM_AUDIO, AMF_STREAM_AUDIO, false),
        AMFPropertyInfoBool( AMF_STREAM_ENABLED,            AMF_STREAM_ENABLED, false, true),
        AMFPropertyInfoInt64(AMF_STREAM_CODEC_ID,           AMF_STREAM_CODEC_ID, 0, 0, INT_MAX, false),
    AMFPrimitivePropertyInfoMapEnd
    m_DataQueue.SetQueueSize(100);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL   AMFMFCaptureImpl::AMFAudioOutput::SelectSource(IMFMediaSource* pMediaSource)
{
    CComPtr<IMFPresentationDescriptor> pPresentationDesc;
    HRESULT hr = pMediaSource->CreatePresentationDescriptor(&pPresentationDesc);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"CreatePresentationDescriptor() failed");

    amf_int64 formatRequested = AMFAF_UNKNOWN;
    GetProperty(AMF_STREAM_AUDIO_FORMAT, &formatRequested);
    amf_int64 samplerateRequested = 0;
    GetProperty(AMF_STREAM_AUDIO_SAMPLE_RATE, &samplerateRequested);
    amf_int64 channelsRequested = 0;
    GetProperty(AMF_STREAM_AUDIO_CHANNELS, &channelsRequested);
    amf_int64 codecRequested = 0;
    GetProperty(AMF_STREAM_CODEC_ID, &codecRequested);

    GUID guidRequestedFormat = GUID_NULL;


    switch(formatRequested)
    {
    case AMFAF_S16: guidRequestedFormat = MFAudioFormat_PCM; break;
    case AMFAF_S32: guidRequestedFormat = MFAudioFormat_PCM; break;
    case AMFAF_FLT: guidRequestedFormat = MFAudioFormat_PCM; break;
    case AMFAF_UNKNOWN: guidRequestedFormat = GUID_NULL; break;
    }

    switch(codecRequested)
    {
    case AV_CODEC_ID_MP3: guidRequestedFormat = MFAudioFormat_AAC; break;
    case AV_CODEC_ID_AAC: guidRequestedFormat = MFAudioFormat_MP3; break;
    }


    amf_int64 formatFound = AMFAF_UNKNOWN;
    amf_int64 samplerateFound = 0;
    amf_int64 codecFound = 0;
    amf_int64 channelsFound = 0;
    GUID guidFoundFormat = GUID_NULL;


    DWORD streamCount = 0;
    pPresentationDesc->GetStreamDescriptorCount(&streamCount);

    for (UINT32 i = 0; i < streamCount; i++)
    {
        CComPtr<IMFStreamDescriptor> pStreamDesc;
        BOOL selected(FALSE);
        hr = pPresentationDesc->GetStreamDescriptorByIndex(i, &selected, &pStreamDesc);

        if (FAILED(hr))
        {
            AMFTraceError(AMF_FACILITY, L"GetStreamDescriptorByIndex() failed!");
            continue;
        }
        if (!selected)
        {
            continue;
        }

        CComPtr<IMFMediaTypeHandler> pHandler;
        hr = pStreamDesc->GetMediaTypeHandler(&pHandler);

        if (FAILED(hr))
        {
            AMFTraceError(AMF_FACILITY, L"GetMediaTypeHandler() failed!");
            continue;
        }

        DWORD typeCount = 0;
        hr = pHandler->GetMediaTypeCount(&typeCount);
        if (FAILED(hr))
        {
            AMFTraceError(L"AMFMFSourceImpl", L"GetMediaTypeCount() failed!");
            continue;
        }

        amf_int64 maxSampleRate = 0;
        amf_int32 selection = -1;

        //find the best resolution
        for (DWORD j = 0; j < typeCount ; j++)
        {
            CComPtr<IMFMediaType> pMediaType;
            hr = pHandler->GetMediaTypeByIndex(j, &pMediaType);
            if (FAILED(hr))
            {
                AMFTraceError(L"AMFMFSourceImpl", L"GetMediaTypeByIndex() failed");
                continue;
            }

            GUID guidMajor = GUID_NULL;
            hr = pMediaType->GetGUID(MF_MT_MAJOR_TYPE, &guidMajor);
            if (FAILED(hr))
            {
                AMFTraceError(L"AMFMFSourceImpl", L"GetGUID() failed");
                continue;
            }
            if(guidMajor != MFMediaType_Audio)
            {
                continue;
            }

            GUID guidSubType = GUID_NULL;
            hr = pMediaType->GetGUID(MF_MT_SUBTYPE, &guidSubType);
            if (FAILED(hr))
            {
                AMFTraceError(L"AMFMFSourceImpl", L"GetGUID() failed");
                continue;
            }

            if(guidRequestedFormat != GUID_NULL && guidSubType != guidRequestedFormat)
            {
                continue;
            }

            UINT32 sampleRate = 0;
            hr = pMediaType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND , &sampleRate);

            if(samplerateRequested != 0)
            {
                if(samplerateRequested != sampleRate)
                {
                    continue;
                }
            }else if(sampleRate < maxSampleRate)
            {
                continue;
            }
            maxSampleRate = sampleRate;

            UINT32 channels = 0;
            hr = pMediaType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS  , &channels);
            if(channelsRequested != 0 && channelsRequested != channels)
            {
                continue;
            }

            samplerateFound = sampleRate;
            guidFoundFormat = guidSubType;
            channelsFound = channels;

            if(guidFoundFormat == MFAudioFormat_PCM)
            {
                codecFound = 0;
                UINT32 bits = 0;
                hr = pMediaType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE  , &bits);

                switch(bits)
                {
                case 16: formatFound = AMFAF_S16; break;
                case 32: formatFound = AMFAF_S32; break;
                }
            }
            else if(guidFoundFormat == MFAudioFormat_AAC) {codecFound = AV_CODEC_ID_AAC;}
            else if(guidFoundFormat == MFAudioFormat_MP3) {codecFound = AV_CODEC_ID_MP3;}

            selection = j;
        }
        if (selection >= 0)
        {
            SetProperty(AMF_STREAM_AUDIO_FORMAT,formatFound);
            SetProperty(AMF_STREAM_AUDIO_SAMPLE_RATE, samplerateFound);
            SetProperty(AMF_STREAM_AUDIO_CHANNELS, channelsFound);
            SetProperty(AMF_STREAM_CODEC_ID, codecFound);

            CComPtr<IMFMediaType> pMediaType(NULL);
            pHandler->GetMediaTypeByIndex(selection, &pMediaType);
            pHandler->SetCurrentMediaType(pMediaType);

            m_eFormat = (AMF_AUDIO_FORMAT)formatFound;
            m_iSampleRate = (amf_int32)samplerateFound;
            m_iChannels = (amf_int32) channelsFound;
            m_iCodecID = codecFound;

            if(codecFound != 0)
            {
                UINT32 cbBlob = 0;
                pMediaType->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &cbBlob);
                if(cbBlob > 0 )
                {
                    AMFBufferPtr pBuffer;
                    m_pHost->m_pContext->AllocBuffer(AMF_MEMORY_HOST, cbBlob, &pBuffer);
                    pMediaType->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, (BYTE*)pBuffer->GetNative(), cbBlob, &cbBlob);
                    SetProperty(AMF_STREAM_EXTRA_DATA, AMFInterfacePtr(pBuffer));
                }
            }
            m_iSelectedIndex = i;

            break;
        }
    }
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL    AMFMFCaptureImpl::AMFAudioOutput::ConvertData(IMFSample* pSample, IMFMediaBuffer* pMediaBufffer, AMFData **ppData)
{
    if(m_iCodecID != 0) // compressed stream
    {
        return AMFOutputBase::ConvertData(pSample, pMediaBufffer, ppData);
    }
    // uncompressed stream
    HRESULT hr = S_OK;
    AMF_RESULT res = AMF_OK;
    // all compressed buffers
    DWORD lenData = 0;
    pMediaBufffer->GetCurrentLength((DWORD*)&lenData);
    AMF_RETURN_IF_FALSE(lenData != 0, AMF_FAIL, L"GetCurrentLength() == 0");

    amf_int32 bytesInSample = 0;
    switch(m_eFormat)
    {
    case AMFAF_U8  : bytesInSample = 1; break;
    case AMFAF_S16 : bytesInSample = 2; break;
    case AMFAF_S32 : bytesInSample = 4; break;
    case AMFAF_FLT : bytesInSample = 4; break;
    case AMFAF_DBL : bytesInSample = 8; break;
    case AMFAF_U8P : bytesInSample = 1; break;
    case AMFAF_S16P: bytesInSample = 2; break;
    case AMFAF_S32P: bytesInSample = 4; break;
    case AMFAF_FLTP: bytesInSample = 4; break;
    case AMFAF_DBLP: bytesInSample = 8; break;
    }

    amf_int32 samples = lenData / (bytesInSample * m_iChannels);

    AMFAudioBufferPtr pBuffer;
    res = m_pHost->m_pContext->AllocAudioBuffer(AMF_MEMORY_HOST, m_eFormat, samples, m_iSampleRate, m_iChannels, &pBuffer);
    AMF_RETURN_IF_FAILED(res, L"AllocAudioBuffer() failed");

    void *pData = NULL;
    hr = pMediaBufffer->Lock((BYTE**)&pData, 0, 0);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"pMediaBufffer->Lock() failed");

    memcpy(pBuffer->GetNative(), pData, lenData);

    pMediaBufffer->Unlock();

    *ppData = pBuffer.Detach();

    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AMFMFCaptureImpl::AMFMFCaptureImpl(AMFContext* pContext) :
    m_pContext(pContext)
{
    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoWString(AMF_CAPTURE_DEVICE_NAME, AMF_CAPTURE_DEVICE_NAME, L"", false),
        AMFPropertyInfoInt64(AMF_CAPTURE_DEVICE_TYPE, AMF_CAPTURE_DEVICE_TYPE,  AMF_CAPTURE_DEVICE_MEDIAFOUNDATION, AMF_CAPTURE_DEVICE_MEDIAFOUNDATION, AMF_CAPTURE_DEVICE_MEDIAFOUNDATION, false),
    AMFPrimitivePropertyInfoMapEnd
}
//-------------------------------------------------------------------------------------------------
AMFMFCaptureImpl::~AMFMFCaptureImpl()
{
    Terminate();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFMFCaptureImpl::Init(AMF_SURFACE_FORMAT /*format*/, amf_int32 /*width*/, amf_int32 /*height*/)
{
    Terminate();
    AMFLock lock(&m_sync);

    for(amf_vector<AMFOutputBasePtr>::iterator it = m_OutputStreams.begin(); it != m_OutputStreams.end(); it++)
    {
        (*it)->Init();
    }
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL            AMFMFCaptureImpl::Start()
{
    for(amf_vector<AMFOutputBasePtr>::iterator it = m_OutputStreams.begin(); it != m_OutputStreams.end(); it++)
    {
        (*it)->Start();
    }
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL            AMFMFCaptureImpl::Stop()
{
    for(amf_vector<AMFOutputBasePtr>::iterator it = m_OutputStreams.begin(); it != m_OutputStreams.end(); it++)
    {
        (*it)->Stop();
    }
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFMFCaptureImpl::UpdateDevice(IMFActivate *pActivateVideo, IMFActivate *pActivateAudio, const wchar_t* pName)
{
    m_OutputStreams.clear();
    if(pActivateVideo != NULL)
    {
        m_OutputStreams.push_back(new AMFVideoOutput(this, pActivateVideo));
    }
    if(pActivateAudio != NULL)
    {
        m_OutputStreams.push_back(new AMFAudioOutput(this, pActivateAudio));
    }
    SetProperty(AMF_CAPTURE_DEVICE_NAME, pName);
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFMFCaptureImpl::ReInit(amf_int32 /* width */, amf_int32 /* height */)
{
    AMFLock lock(&m_sync);
    for (amf_vector<AMFOutputBasePtr>::iterator it = m_OutputStreams.begin(); it != m_OutputStreams.end(); it++)
    {
        (*it)->Start();
    }
    return AMF_OK;

//    Terminate();
//    return Init(AMF_SURFACE_UNKNOWN, width, height);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFMFCaptureImpl::Terminate()
{
    Stop();
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFMFCaptureImpl::Drain()
{
    AMFLock lock(&m_sync);
    for (amf_vector<AMFOutputBasePtr>::iterator it = m_OutputStreams.begin(); it != m_OutputStreams.end(); it++)
    {
        (*it)->Drain();
    }
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFMFCaptureImpl::Flush()
{
    AMFLock lock(&m_sync);
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
amf_int32   AMF_STD_CALL  AMFMFCaptureImpl::GetOutputCount()
{
    AMFLock lock(&m_sync);
    return (amf_int32)m_OutputStreams.size();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL  AMFMFCaptureImpl::GetOutput(amf_int32 index, AMFOutput** ppOutput)
{
    AMFLock lock(&m_sync);
    AMF_RETURN_IF_FALSE(index < (amf_int32)m_OutputStreams.size(), AMF_INVALID_ARG, L"index %d out of range", index);
    *ppOutput = m_OutputStreams[index];
    (*ppOutput)->Acquire();
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
