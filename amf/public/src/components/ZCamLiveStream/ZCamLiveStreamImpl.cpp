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
#include "ZCamLiveStreamImpl.h"

#include "public/include/core/Context.h"
#include "public/include/core/Trace.h"
#include "public/common/TraceAdapter.h"
#include "public/common/AMFFactory.h"
#include "public/common/DataStream.h"

#include "public/include/components/ZCamLiveStream.h"

#define AMF_FACILITY L"AMFZCamLiveStreamImpl"

using namespace amf;

extern "C"
{
    AMF_RESULT AMFCreateComponentZCamLiveStream(amf::AMFContext* pContext, amf::AMFComponentEx** ppComponent)
    {
        *ppComponent = new amf::AMFInterfaceMultiImpl< amf::AMFZCamLiveStreamImpl, amf::AMFComponentEx, amf::AMFContext*>(pContext);
        (*ppComponent)->Acquire();
        return AMF_OK;
    }
}

static const AMFEnumDescriptionEntry ZCamVideoModeCommandEnum[] =
{
    { CAMLIVE_MODE_ZCAM_1080P24, L"1080P24" },
    { CAMLIVE_MODE_ZCAM_1080P30, L"1080P30" },
    { CAMLIVE_MODE_ZCAM_1080P60, L"1080P60" },
    { CAMLIVE_MODE_ZCAM_2K7P24,  L"2.7KP24" },
    { CAMLIVE_MODE_ZCAM_2K7P30,  L"2.7KP30" },
    { CAMLIVE_MODE_ZCAM_2K7P60,  L"2.7KP60" },
    { CAMLIVE_MODE_ZCAM_2544P24, L"2544P24%20(4:3)" },
    { CAMLIVE_MODE_ZCAM_2544P30, L"2544P30%20(4:3)" },
    { CAMLIVE_MODE_ZCAM_2544P60, L"2544P60%20(4:3)" },
};


const AMFSize AMFZCamLiveStreamImpl::VideoFrameSizeList[] =
{
    AMFConstructSize(1920, 1080),
    AMFConstructSize(1920, 1080),
    AMFConstructSize(1920, 1080),
    AMFConstructSize(2704, 1520),
    AMFConstructSize(2704, 1520),
    AMFConstructSize(2704, 1520),
    AMFConstructSize(3392, 2544),
    AMFConstructSize(3392, 2544),
    AMFConstructSize(3392, 2544)
};

const amf_int32 AMFZCamLiveStreamImpl::VideoFrameRateList[] =
{ 24, 30, 60, 24, 30, 60, 24, 30, 60 };

static const AMFEnumDescriptionEntry ZCamAudioModeEnum[] =
{
    { CAM_AUDIO_MODE_NONE,   L"NoAudio" },
    { CAM_AUDIO_MODE_SILENT, L"SilentAudio" },
    { CAM_AUDIO_MODE_CAMERA, L"CameraAudio" }
};

//-------------------------------------------------------------------------------------------------
AMFZCamLiveStreamImpl::AMFOutputZCamLiveStreamImpl::AMFOutputZCamLiveStreamImpl(AMFZCamLiveStreamImpl* pHost, amf_int32 index) :
    m_pHost(pHost),
    m_iIndex(index),
    m_frameCount(0),
    m_iQueueSize(10),
    m_isAudio(false),
    m_audioTimeStamp(-1LL),
    m_ptsLast(0),
    m_lowLatency(1)
{
    m_pHost->GetProperty(ZCAMLIVE_LOWLATENCY, &m_lowLatency);
    m_VideoFrameQueue.SetQueueSize(m_iQueueSize);
}
//-------------------------------------------------------------------------------------------------
AMFZCamLiveStreamImpl::AMFOutputZCamLiveStreamImpl::~AMFOutputZCamLiveStreamImpl()
{
    AMFTraceInfo(AMF_FACILITY, L"Stream# %d, frames read %d", m_iIndex, (int)m_frameCount);
}
//-------------------------------------------------------------------------------------------------
// NOTE: this call will return one compressed frame for each QueryOutput call
AMF_RESULT AMF_STD_CALL  AMFZCamLiveStreamImpl::AMFOutputZCamLiveStreamImpl::QueryOutput(AMFData** ppData)
{
    AMF_RETURN_IF_FALSE(m_pHost != NULL, AMF_UNEXPECTED, L"QueryOutput() - Host not Initialized");
    AMF_RESULT  err = AMF_REPEAT;
    AMFDataPtr  pFrame;
    amf_ulong   ulID = 0;
    if (m_isAudio)
    {
        err = GetAudioData(ppData);
    }
    else if (m_VideoFrameQueue.Get(ulID, pFrame, 0))
    {
        amf_pts timestamp = (m_lowLatency>0) ? amf_high_precision_clock() : m_ptsLast;
        pFrame->SetPts(timestamp);
        amf_uint32 frameRate = 30;
        m_pHost->GetProperty(ZCAMLIVE_VIDEO_FRAMERATE, &frameRate);
        m_frameCount++;
        amf_pts duration = AMF_SECOND / frameRate;
        pFrame->SetDuration(duration);
        m_ptsLast += duration;

        pFrame->SetProperty(L"DemuxerEnd", (double)timestamp);

        *ppData = pFrame.Detach();
        err = AMF_OK;
    }
    return err;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMFZCamLiveStreamImpl::AMFOutputZCamLiveStreamImpl::SubmitFrame(AMFData* pData)
{
    AMF_RETURN_IF_FALSE(m_pHost != NULL, AMF_UNEXPECTED, L"QueryOutput() - Host not Initialized");
    AMF_RESULT  err = AMF_INPUT_FULL;

    if (m_VideoFrameQueue.Add(0, pData, 0, 0))
    {
        err = AMF_OK;
    }

    return err;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMFZCamLiveStreamImpl::AMFOutputZCamLiveStreamImpl::GetAudioData(AMFData** ppData)
{
    AMF_RETURN_IF_FALSE(m_pHost != NULL, AMF_UNEXPECTED, L"QueryOutput() - Host not Initialized");
    AMF_RESULT  err = AMF_REPEAT;

    AMF_AUDIO_FORMAT sampleFormat = AMFAF_FLTP;
    amf_int32 samples = 1024;
    amf_int32 sampleRate = 44100;
    amf_int32 channels = 2;
    amf_int64 audioLength = 10000000LL * samples / sampleRate;
    //audio
    amf_int64 timestamp = amf_high_precision_clock();
    if(m_audioTimeStamp == -1LL)
    { 
        m_audioTimeStamp = timestamp;
    }
    timestamp -= m_audioTimeStamp;

    if (timestamp  > m_ptsLast - audioLength / 3 )
    {
        AMFAudioBufferPtr pAudioBuffer;
        err = m_pHost->m_pContext->AllocAudioBuffer(AMF_MEMORY_HOST, sampleFormat, samples, sampleRate, channels, &pAudioBuffer);
        if (AMF_OK == err && pAudioBuffer)
        {
            pAudioBuffer->SetPts(m_ptsLast);
            pAudioBuffer->SetDuration(audioLength);
            m_ptsLast += audioLength;
            *ppData = pAudioBuffer.Detach();
            err = AMF_OK;
        }
    }
    else
    {
        err = AMF_REPEAT;
    }

    return err;
}
// AMFZCamLiveStreamImpl
//-------------------------------------------------------------------------------------------------
AMFZCamLiveStreamImpl::AMFZCamLiveStreamImpl(AMFContext* pContext) :
    m_pContext(pContext),
    m_frameCount(0),
    m_bForceEof(false),
    m_bTerminated(true),
    m_ZCamPollingThread(this),
    m_streamCount(4),
    m_streamActive(-1),
    m_videoMode(CAMLIVE_MODE_ZCAM_2K7P30),
    m_audioMode(CAM_AUDIO_MODE_SILENT)
{
    m_OutputStreams.clear();
    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoInt64(ZCAMLIVE_STREAMCOUNT, ZCAMLIVE_STREAMCOUNT, m_streamCount, 0, 32, false),
        AMFPropertyInfoSize(ZCAMLIVE_VIDEO_FRAMESIZE, ZCAMLIVE_VIDEO_FRAMESIZE, AMFConstructSize(2704, 1520), AMFConstructSize(720, 480), AMFConstructSize(7680, 7680), false),
        AMFPropertyInfoDouble(ZCAMLIVE_VIDEO_FRAMERATE, ZCAMLIVE_VIDEO_FRAMERATE, 30.0, 0., 100000., false),
        AMFPropertyInfoInt64(ZCAMLIVE_VIDEO_BIT_RATE, ZCAMLIVE_VIDEO_BIT_RATE, 30000000, 0, 300000000, false),
        AMFPropertyInfoInt64(ZCAMLIVE_STREAM_FRAMECOUNT, ZCAMLIVE_STREAM_FRAMECOUNT, 0, 0, LLONG_MAX, true),
        AMFPropertyInfoInt64(ZCAMLIVE_STREAM_ACTIVE_CAMERA, ZCAMLIVE_STREAM_ACTIVE_CAMERA, -1, -1, 256, true),
        AMFPropertyInfoInt64(ZCAMLIVE_STREAM_FRAMECOUNT, ZCAMLIVE_STREAM_FRAMECOUNT, 0, 0, LLONG_MAX, true),
        AMFPropertyInfoWString(ZCAMLIVE_CODEC_ID, ZCAMLIVE_CODEC_ID, L"AMFVideoDecoderUVD_H264_AVC", false),
        AMFPropertyInfoEnum(ZCAMLIVE_VIDEO_MODE, ZCAMLIVE_VIDEO_MODE, CAMLIVE_MODE_ZCAM_2K7P30, ZCamVideoModeCommandEnum, false),
        AMFPropertyInfoEnum(ZCAMLIVE_AUDIO_MODE, ZCAMLIVE_AUDIO_MODE, CAM_AUDIO_MODE_SILENT, ZCamAudioModeEnum, false),
        AMFPropertyInfoInt64(ZCAMLIVE_LOWLATENCY, ZCAMLIVE_LOWLATENCY, 1, 0, 1, false),
        AMFPropertyInfoWString(ZCAMLIVE_IP_0, ZCAMLIVE_IP_0, L"10.98.32.1", false),
        AMFPropertyInfoWString(ZCAMLIVE_IP_1, ZCAMLIVE_IP_1, L"10.98.32.2", false),
        AMFPropertyInfoWString(ZCAMLIVE_IP_2, ZCAMLIVE_IP_2, L"10.98.32.3", false),
        AMFPropertyInfoWString(ZCAMLIVE_IP_3, ZCAMLIVE_IP_3, L"10.98.32.4", false),
        AMFPrimitivePropertyInfoMapEnd
}
//-------------------------------------------------------------------------------------------------
AMFZCamLiveStreamImpl::~AMFZCamLiveStreamImpl()
{
    Terminate();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFZCamLiveStreamImpl::Init(AMF_SURFACE_FORMAT /*format*/, amf_int32 /*width*/, amf_int32 /*height*/)
{
    AMFLock lock(&m_sync);
    AMF_RESULT res = AMF_OK;
    if (!m_bTerminated)
    {
        Terminate();
    }

    amf_int64 streamActive = -1;
    GetProperty(ZCAMLIVE_STREAM_ACTIVE_CAMERA, &streamActive);
    m_streamActive = (amf_int32)streamActive;

    m_OutputStreams.clear();

    //create outputs 
    amf_int32 outputs = (m_audioMode == CAM_AUDIO_MODE_SILENT) ? m_streamCount + 1 : m_streamCount;
    for (amf_int32 idx = 0; idx < outputs; idx++)
    {
        AMFZCamLiveStreamImpl::AMFOutputZCamLiveStreamImplPtr pStream = new AMFZCamLiveStreamImpl::AMFOutputZCamLiveStreamImpl(this, idx);
        if (!pStream)
        {
            res = AMF_OUT_OF_MEMORY;
            break;
        }

        m_OutputStreams.push_back(pStream);
    }

    for (amf_int32 idx = m_streamCount; idx < outputs; idx++)
    {
        m_OutputStreams[idx]->m_isAudio = true;
    }

    amf::AMFVariant var;
    GetProperty(ZCAMLIVE_IP_0, &var);
    m_dataStreamZCam.SetIP(0, var.ToString().c_str());
    GetProperty(ZCAMLIVE_IP_1, &var);
    m_dataStreamZCam.SetIP(1, var.ToString().c_str());
    GetProperty(ZCAMLIVE_IP_2, &var);
    m_dataStreamZCam.SetIP(2, var.ToString().c_str());
    GetProperty(ZCAMLIVE_IP_3, &var);
    m_dataStreamZCam.SetIP(3, var.ToString().c_str());

    //init cameras
    if (AMF_OK == res)
    {
        amf_wstring modeCommandW(ZCamVideoModeCommandEnum[m_videoMode].name);
        amf_string modeCommand(amf::amf_from_unicode_to_utf8(modeCommandW));

        int err = m_dataStreamZCam.SetupCameras(modeCommand.c_str());
        if (!err)
        {
            err = m_dataStreamZCam.Open();
        }

        if (err)
        {
            res = AMF_UNEXPECTED;
        }
    }

    if (AMF_OK == res)
    {
        m_ZCamPollingThread.Start();
    }

    m_bTerminated = false;
    return res;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFZCamLiveStreamImpl::ReInit(amf_int32 width, amf_int32 height)
{
    Terminate();
    return Init(AMF_SURFACE_UNKNOWN, width, height);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFZCamLiveStreamImpl::Terminate()
{
    m_ZCamPollingThread.RequestStop();
    m_ZCamPollingThread.WaitForStop();

    AMFLock lock(&m_sync);

    m_OutputStreams.clear();
    m_frameCount = 0;
    m_bForceEof = false;
    m_dataStreamZCam.Close();

    m_bTerminated = true;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFZCamLiveStreamImpl::Drain()
{
    AMFLock lock(&m_sync);
    m_bForceEof = true;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFZCamLiveStreamImpl::Flush()
{
    AMFLock lock(&m_sync);
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFZCamLiveStreamImpl::GetOutput(amf_int32 index, AMFOutput** ppOutput)
{
    AMF_RETURN_IF_FALSE(index >= 0 && index < (amf_int32)m_OutputStreams.size(), AMF_INVALID_ARG, L"Invalid index");
    AMF_RETURN_IF_FALSE(ppOutput != NULL, AMF_INVALID_ARG, L"ppOutput = NULL");

    *ppOutput = m_OutputStreams[index];
    (*ppOutput)->Acquire();

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL  AMFZCamLiveStreamImpl::OnPropertyChanged(const wchar_t* pName)
{
    AMFLock lock(&m_sync);
    amf_wstring name(pName);

    if (name == ZCAMLIVE_VIDEO_MODE)
    {
        GetProperty(ZCAMLIVE_VIDEO_MODE, (amf_int64*)&m_videoMode);
        SetProperty(ZCAMLIVE_VIDEO_FRAMESIZE, VideoFrameSizeList[m_videoMode]);
        SetProperty(ZCAMLIVE_VIDEO_FRAMERATE, VideoFrameRateList[m_videoMode]);
    }
    else if (name == ZCAMLIVE_AUDIO_MODE)
    {
        GetProperty(ZCAMLIVE_AUDIO_MODE, (amf_int64*)&m_audioMode);
    }
}
//-------------------------------------------------------------------------------------------------
amf_int32   AMF_STD_CALL  AMFZCamLiveStreamImpl::GetOutputCount()
{  
    AMFLock lock(&m_sync);
    return (amf_int32)((m_audioMode == CAM_AUDIO_MODE_SILENT) ? m_streamCount + 1 : m_streamCount);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFZCamLiveStreamImpl::PollStream()
{  
    AMF_RESULT res = AMF_OK; // error checking can be added later
    std::vector<int> lenDataList;
    std::vector<char*> pDataList;
    amf_pts timestampCapStart = amf_high_precision_clock();
    m_dataStreamZCam.CaptureOneFrame(pDataList, lenDataList);
    amf_pts timestampCapEnd = amf_high_precision_clock();
    if (pDataList.size() != static_cast<amf_uint32>(m_streamCount))
    {
        AMFTraceWarning(L"Videostitch", L"CaptureOneFrame, pDataList.size() != m_streamCount! Received streams = %d", (amf_int32)pDataList.size());
        return AMF_OK;
    }

    for (amf_int32 idx = 0; idx < m_streamCount; idx++)
    {
        char* pData = pDataList[idx];
        int lenData = lenDataList[idx];

        if (!pData || lenData <= 0)
        {
            AMFTraceWarning(L"Videostitch", L"Data len- !pData || lenData <= 0!!");
        }
        else
        {
            AMFBufferPtr pBuf;
            AMF_RESULT err = m_pContext->AllocBuffer(AMF_MEMORY_HOST, lenData, &pBuf);
            if (AMF_OK == err && pBuf)
            {
                if (idx == 0)
                {
                    pBuf->SetProperty(L"CaptureStart", timestampCapStart);
                    pBuf->SetProperty(L"CaptureEnd", timestampCapEnd);

                    amf_pts timestamp = amf_high_precision_clock();
                    pBuf->SetProperty(L"DemuxerStart", timestamp);
                }

                void* pMem = pBuf->GetNative();
                memcpy(pMem, pData, lenData);
                while (!m_ZCamPollingThread.StopRequested())
                {
                    //this is for playback
                    if ((m_streamActive >= 0) && (idx != m_streamActive))
                    {
                        break;
                    }
                    err = m_OutputStreams[idx]->SubmitFrame(pBuf);
                    if (AMF_INPUT_FULL != err)
                    {
                        break;
                    }
                    amf_sleep(1); // milliseconds
                }
            }
        }
    }
    m_frameCount++;
    return res;
}
//-------------------------------------------------------------------------------------------------
void AMFZCamLiveStreamImpl::ZCamLiveStreamPollingThread::Run()
{
    AMF_RESULT res = AMF_OK; // error checking can be added later
    while (true)
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
AMFZCamLiveStreamImpl::ZCamLiveStreamPollingThread::ZCamLiveStreamPollingThread(AMFZCamLiveStreamImpl *pHost) :
m_pHost(pHost)
{
}
//-------------------------------------------------------------------------------------------------
AMFZCamLiveStreamImpl::ZCamLiveStreamPollingThread::~ZCamLiveStreamPollingThread()
{
}
//-------------------------------------------------------------------------------------------------
