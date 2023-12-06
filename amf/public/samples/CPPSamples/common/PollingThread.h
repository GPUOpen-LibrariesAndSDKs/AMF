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

#include "public/common/DataStream.h"
#include "public/common/Thread.h"
#include "public/common/TraceAdapter.h"
#include "public/include/components/Component.h"

#define AMF_FACILITY L"PollingThread"

#define START_TIME_PROPERTY L"StartTimeProperty" // custom property ID to store submission time in a frame - all custom properties are copied from input to output

class PollingThread : public amf::AMFThread
{
public:
    PollingThread(amf::AMFContext* pContext, amf::AMFComponent* pComponent, const wchar_t* pFileName, bool bWriteToFile)
        : m_pContext(pContext),
        m_pComponent(pComponent),
        m_LatencyTime(0),
        m_WriteDuration(0),
        m_ComponentDuration(0),
        m_LastPollTime(0)
    {
        if (bWriteToFile == true)
        {
            AMF_RESULT res = AMF_OK;
            res = amf::AMFDataStream::OpenDataStream(pFileName, amf::AMFSO_WRITE, amf::AMFFS_SHARE_READ, &m_pFile);
            AMF_ASSERT_OK(res, L"Failed to open file %s", pFileName);
        }
    }

    virtual ~PollingThread()
    {
        if (m_pFile != NULL)
        {
            m_pFile->Close();
            m_pFile = NULL;
        }
    }

    void PrintTimes(const char* component, amf_int32 frameCount)
    {
        printf("latency           = %.4fms\n%s per frame = %.4fms\nwrite per frame   = %.4fms\n",
            double(m_LatencyTime) / AMF_MILLISECOND,
            component,
            double(m_ComponentDuration) / AMF_MILLISECOND / frameCount,
            double(m_WriteDuration) / AMF_MILLISECOND / frameCount);
    }

protected:

    virtual void Run()
    {
        AMF_RESULT res = AMF_OK; // error checking can be added later
        while (true)
        {
            amf::AMFDataPtr pData;
            res = m_pComponent->QueryOutput(&pData);
            if (res == AMF_EOF)
            {
                break; // Drain complete
            }
            if ((res != AMF_OK) && (res != AMF_REPEAT))
            {
                // trace possible error message
                break; // Drain complete
            }
            if (pData != NULL)
            {
                ProcessData(pData);
            }
            else
            {
                amf_sleep(1);
            }
        }
        PrintResults();
    }

    virtual bool Init() override
    {
        m_LatencyTime = 0;
        m_WriteDuration = 0;
        m_ComponentDuration = 0;
        m_LastPollTime = 0;
        return true;
    }

    virtual void ProcessData(amf::AMFData* pData) = 0;
    virtual void PrintResults() {}

    virtual bool Terminate() override
    {
        m_pComponent = NULL;
        m_pContext = NULL;
        return true;
    }

    void AdjustTimes(amf::AMFData* pData)
    {
        amf_pts poll_time = amf_high_precision_clock();
        amf_pts start_time = 0;
        pData->GetProperty(START_TIME_PROPERTY, &start_time);
        if (start_time < m_LastPollTime) // remove wait time if submission was faster then encode
        {
            start_time = m_LastPollTime;
        }
        m_LastPollTime = poll_time;
        m_ComponentDuration += poll_time - start_time;

        if (m_LatencyTime == 0)
        {
            m_LatencyTime = poll_time - start_time;
        }
    }

    amf::AMFContextPtr     m_pContext;
    amf::AMFComponentPtr   m_pComponent;
    amf::AMFDataStreamPtr  m_pFile;

    amf_pts m_LatencyTime;
    amf_pts m_WriteDuration;
    amf_pts m_ComponentDuration;
    amf_pts m_LastPollTime;
};

#undef AMF_FACILITY
