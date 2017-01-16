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

#pragma once

#include "public/include/core/Context.h"
#include "public/include/components/VideoEncoderVCE.h"
#include "public/include/components/VideoEncoderHEVC.h"
#include "public/include/components/VideoConverter.h"


#include "../common/Pipeline.h"
#include "../common/ParametersStorage.h"
#include "../common/EncoderParamsAVC.h"
#include "../common/EncoderParamsHEVC.h"

#include "../common/DeviceDX9.h"
#include "../common/DeviceDX11.h"
#include "../common/DeviceOpenGL.h"
#include "../common/DeviceOpenCL.h"

#include "VideoRender.h"
#include "RenderWindow.h"

class RenderEncodePipeline : public Pipeline
{
    class PipelineElementEncoder;
public:
    RenderEncodePipeline();
    ~RenderEncodePipeline();
public:
    static const wchar_t* PARAM_NAME_CODEC;
    static const wchar_t* PARAM_NAME_OUTPUT;
    static const wchar_t* PARAM_NAME_RENDER;

    static const wchar_t* PARAM_NAME_WIDTH;
    static const wchar_t* PARAM_NAME_HEIGHT;
    static const wchar_t* PARAM_NAME_FRAMES;

    static const wchar_t* PARAM_NAME_ADAPTERID;
    static const wchar_t* PARAM_NAME_WINDOW_MODE;
    static const wchar_t* PARAM_NAME_FULLSCREEN;

	static const wchar_t* PARAM_NAME_QUERY_INST_COUNT;
	static const wchar_t* PARAM_NAME_SELECT_INSTANCE;

    AMF_RESULT Init(ParametersStorage* pParams, int threadID = -1);
    void Terminate();

    AMF_RESULT Run();
    void ProcessWindowMessages();

private:
    amf::AMFContextPtr          m_pContext;

    amf::AMFDataStreamPtr            m_pStreamOut;
    
    VideoRenderPtr              m_pVideoRender;
    amf::AMFComponentPtr        m_pEncoder;
    PipelineElementPtr          m_pStreamWriter;
    DummyWriterPtr              m_pDummyWriter;
    amf::AMFComponentPtr        m_pConverter;
    SplitterPtr                 m_pSplitter;
    amf::AMFDataStreamPtr            m_pStreamOut2;
    PipelineElementPtr          m_pStreamWriter2;

    DeviceDX9                   m_deviceDX9;
    DeviceDX11                  m_deviceDX11;
    DeviceOpenGL                m_deviceOpenGL;
    DeviceOpenCL                m_deviceOpenCL;

    RenderWindow                m_window;

};