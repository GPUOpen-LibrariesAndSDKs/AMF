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
#include <stdio.h>
#include <fstream>
#include <iosfwd>
#include "VideoCaptureImpl.h"

#include "public/include/core/Context.h"
#include "public/include/core/Trace.h"
#include "public/common/TraceAdapter.h"
#include "public/common/AMFFactory.h"
#include "public/common/DataStream.h"

#include "public/include/components/VideoCapture.h"

#define AMF_FACILITY L"AMFVideoCaptureImpl"

using namespace amf;

extern "C"
{
    AMF_RESULT AMFCreateComponentVideoCapture(amf::AMFContext* pContext, amf::AMFComponentEx** ppComponent)
    {
        *ppComponent = new amf::AMFInterfaceMultiImpl< amf::AMFVideoCaptureImpl, amf::AMFComponentEx, amf::AMFContext*>(pContext);
        (*ppComponent)->Acquire();
        return AMF_OK;
    }
}

//-------------------------------------------------------------------------------------------------
AMFVideoCaptureImpl::AMFOutputVideoCaptureImpl::AMFOutputVideoCaptureImpl(AMFVideoCaptureImpl* pHost) :
    m_pHost(pHost),
    m_frameCount(0),
    m_iQueueSize(30)
{
    m_VideoDataQueue.SetQueueSize(m_iQueueSize);
}
//-------------------------------------------------------------------------------------------------
AMFVideoCaptureImpl::AMFOutputVideoCaptureImpl::~AMFOutputVideoCaptureImpl()
{
}
//-------------------------------------------------------------------------------------------------
// NOTE: this call will return one compressed frame for each QueryOutput call
AMF_RESULT AMF_STD_CALL  AMFVideoCaptureImpl::AMFOutputVideoCaptureImpl::QueryOutput(AMFData** ppData)
{
    AMF_RETURN_IF_FALSE(m_pHost != NULL, AMF_UNEXPECTED, L"QueryOutput() - Host not Initialized");
    AMF_RESULT  err = AMF_REPEAT;
    AMFDataPtr  pFrame;
    amf_ulong   ulID = 0;

    if (m_VideoDataQueue.Get(ulID, pFrame, 0))
    {
        *ppData = pFrame.Detach();
        err = AMF_OK;
    }
    return err;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMFVideoCaptureImpl::AMFOutputVideoCaptureImpl::SubmitFrame(AMFData* pData)
{
    AMF_RETURN_IF_FALSE(m_pHost != NULL, AMF_UNEXPECTED, L"QueryOutput() - Host not Initialized");
    AMF_RESULT  err = AMF_INPUT_FULL;

    if (m_VideoDataQueue.Add(0, pData, 0, 0))
    {
        err = AMF_OK;
    }

    return err;
}

// AMFVideoCaptureImpl
//-------------------------------------------------------------------------------------------------
AMFVideoCaptureImpl::AMFVideoCaptureImpl(AMFContext* pContext) :
    m_pContext(pContext),
    m_frameCount(0),
    m_bTerminated(true),
    m_videoPollingThread(this),
    m_lenData(0)
{
    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoInt64(VIDEOCAP_DEVICE_COUNT, VIDEOCAP_DEVICE_COUNT, 2, 0, 8, false),
        AMFPropertyInfoWString(VIDEOCAP_DEVICE_NAME, VIDEOCAP_DEVICE_NAME, L"", false),
        AMFPropertyInfoWString(VIDEOCAP_DEVICE_ACTIVE, VIDEOCAP_DEVICE_ACTIVE, L"", false),

        AMFPropertyInfoWString(VIDEOCAP_CODEC, VIDEOCAP_CODEC, L"", false),
        AMFPropertyInfoSize(VIDEOCAP_FRAMESIZE, VIDEOCAP_FRAMESIZE, AMFConstructSize(1920, 1080), AMFConstructSize(720, 480), AMFConstructSize(7680, 7680), false),
        AMFPrimitivePropertyInfoMapEnd
}
//-------------------------------------------------------------------------------------------------
AMFVideoCaptureImpl::~AMFVideoCaptureImpl()
{
    Terminate();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoCaptureImpl::Init(AMF_SURFACE_FORMAT /*format*/, amf_int32 /*width*/, amf_int32 /*height*/)
{
    AMFLock lock(&m_sync);
    AMF_RESULT res = AMF_OK;
    if (!m_bTerminated)
    {
        Terminate();
    }
    std::wstring deviceNameActive;
    GetPropertyWString(VIDEOCAP_DEVICE_ACTIVE, &deviceNameActive);

    //create output 
    m_OutputStream = new AMFOutputVideoCaptureImpl(this);
    if (!m_OutputStream)
    {
        res = AMF_OUT_OF_MEMORY;
    }

    //init device
    if (AMF_OK == res)
    {
        res = m_videoSource.Init(deviceNameActive);

        if (res != AMF_OK)
        {
            AMFTraceError(L"Videostitch", L"AMFVideoCaptureImpl, AMFMFSourceImpl::Init() failed!");
        }

        SetProperty(VIDEOCAP_FRAMESIZE, AMFConstructSize(m_videoSource.GetFrameWidth(), m_videoSource.GetFrameHeight()));
        GUID guidSubType = m_videoSource.GetCodec();

        if (MFVideoFormat_H264 == guidSubType)
        {
            SetProperty(VIDEOCAP_CODEC, L"AMFVideoDecoderUVD_H264_AVC");
        }
        else if (MFVideoFormat_HEVC == guidSubType)
        {
            SetProperty(VIDEOCAP_CODEC, L"AMFVideoEncoder_HEVC");
        }
        else if (MFVideoFormat_MJPG == guidSubType)//mjpeg
        {
            SetProperty(VIDEOCAP_CODEC, L"AMFVideoDecoderUVD_MJPEG");
        }
        else if (MFVideoFormat_NV12 == guidSubType)//nv12 raw
        {
            SetProperty(VIDEOCAP_CODEC, L"AMFVideoDecoderUVD_H264_AVC");
        }
    }

    if (AMF_OK == res)
    {
        if (deviceNameActive.length() <= 0) //query device list
        {
            UINT deviceCount = m_videoSource.GetDeviceCount();
            std::string nameList("");
            for (UINT idx = 0; idx < deviceCount; idx++)
            {
                std::string deviceName = m_videoSource.GetDeviceName(idx);
                nameList += (idx == 0) ? deviceName : "\t" + deviceName;
            }
            SetProperty(VIDEOCAP_DEVICE_NAME, nameList.c_str());
            SetProperty(VIDEOCAP_DEVICE_COUNT, deviceCount);
        }
        else
        {
            m_videoPollingThread.Start();
        }
    }

    m_bTerminated = false;
    return res;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoCaptureImpl::ReInit(amf_int32 width, amf_int32 height)
{
    Terminate();
    return Init(AMF_SURFACE_UNKNOWN, width, height);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoCaptureImpl::Terminate()
{
    m_videoPollingThread.RequestStop();
    m_videoPollingThread.WaitForStop();

    AMFLock lock(&m_sync);

    m_OutputStream = NULL;
    m_frameCount = 0;
    m_videoSource.Close();
    m_bTerminated = true;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoCaptureImpl::Drain()
{
    AMFLock lock(&m_sync);
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoCaptureImpl::Flush()
{
    AMFLock lock(&m_sync);
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFVideoCaptureImpl::GetOutput(amf_int32 /* index */, AMFOutput** ppOutput)
{
    AMF_RETURN_IF_FALSE(ppOutput != NULL, AMF_INVALID_ARG, L"ppOutput = NULL");

    *ppOutput = m_OutputStream;
    (*ppOutput)->Acquire();

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL  AMFVideoCaptureImpl::OnPropertyChanged(const wchar_t* pName)
{
    AMFLock lock(&m_sync);
    amf_wstring name(pName);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFVideoCaptureImpl::PollStream()
{  
    AMF_RESULT res = AMF_OK; // error checking can be added later
    char* pData(NULL);
    UINT lenData(0);

    int err = m_videoSource.CaptureOnePacket(&pData, lenData);

    if ((err != AMF_OK) || (lenData <= 0))
    {
        AMFTraceWarning(L"Videostitch", L"Video Data len- lenData <= 0!!");
        res = AMF_FAIL;
    }
    else
    {
        AMFBufferPtr pVideoBuffer;
        err = m_pContext->AllocBuffer(AMF_MEMORY_HOST, lenData, &pVideoBuffer);

        if (AMF_OK == err && pVideoBuffer)
        {
            amf_pts duration = m_videoSource.GetFrameDuration();
            amf_pts timestamp = amf_high_precision_clock();
            pVideoBuffer->SetPts(timestamp);
            pVideoBuffer->SetDuration(duration);

            if (pData)
            {
                void* pDst = pVideoBuffer->GetNative();
                memcpy(pDst, pData, lenData);
            }

            while (!m_videoPollingThread.StopRequested())
            {
                AMF_RESULT result = m_OutputStream->SubmitFrame(pVideoBuffer);
                if (AMF_INPUT_FULL != result)
                {
                    break;
                }
                amf_sleep(1); // milliseconds
            }
        }
    }
    m_videoSource.CaptureOnePacketDone();
    m_frameCount++;
    return res;
}
//-------------------------------------------------------------------------------------------------
void AMFVideoCaptureImpl::VideoCapturePollingThread::Run()
{
    AMF_RESULT res = AMF_OK; // error checking can be added later
    while (AMF_OK == res)
    {
        res = m_pHost->PollStream();
        if (res == AMF_EOF)
        {
            break; // Drain complete
        }
        if (StopRequested())
        {
            break;
        }
    }
}
//-------------------------------------------------------------------------------------------------
AMFVideoCaptureImpl::VideoCapturePollingThread::VideoCapturePollingThread(AMFVideoCaptureImpl *pHost) :
m_pHost(pHost)
{
}
//-------------------------------------------------------------------------------------------------
AMFVideoCaptureImpl::VideoCapturePollingThread::~VideoCapturePollingThread()
{
}
