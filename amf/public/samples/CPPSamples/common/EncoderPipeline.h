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
#pragma once

#include "../../../include/components/VideoEncoderHW_AVC.h"
#include "EncoderStatistic.h"
#include "FrameProvider.h"

class EncoderPipeline
{
public:

    enum PipelineStatus
    {
        PS_UnInitialized = 0,
        PS_Initialized,
        PS_Running,
        PS_Eof,
    };


    EncoderPipeline(ParametersManagerPtr pParams);
    virtual ~EncoderPipeline();

    AMF_RESULT      Init(int threadID = -1);
    AMF_RESULT      Run();
    AMF_RESULT      Terminate();
    PipelineStatus  GetStatus(){ return m_eStatus;}
    double          GetFPS();
    void            LogStat();

protected:
    void    PushProperties(ParametersManager::ParamType ptype, amf::AMFPropertyStorage *storage);
    void    SetStatus(PipelineStatus status) {m_eStatus = status;}


    class RenderThread : public AMFThread
    {
    protected:
        EncoderPipeline            *m_pHost;
    public:
        RenderThread(EncoderPipeline *host) : m_pHost(host) {}
        virtual void Run();
    };
    class PollingThread : public AMFThread
    {
    protected:
        EncoderPipeline            *m_pHost;
    public:
        PollingThread(EncoderPipeline *host) : m_pHost(host) {}
        virtual void Run();
    };

    FrameProviderPtr        m_pFrameProvider;
    ParametersManagerPtr    m_params; 
    amf::AMFContextPtr      m_context;
    amf::AMFComponentPtr    m_encoder;
    AMFDataStreamPtr        m_stream;

    RenderThread            m_RenderThread;
    PollingThread           m_PollingThread;
    amf_int64               m_framesRequested;
    amf_int64               m_framesSubmitted;
    amf_int64               m_framesEncoded;
    amf_int64               m_frameParameterFreq;
    amf_int64               m_dynamicParameterFreq;
    PipelineStatus          m_eStatus;

    EncoderStat             m_stat;
};