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

#include "EncoderPipeline.h"
#include "CmdLogger.h"

#pragma warning(disable:4355)

EncoderPipeline::EncoderPipeline(ParametersManagerPtr pParams) :
    m_params(pParams),
    m_framesRequested(0),
    m_framesSubmitted(0),
    m_framesEncoded(0),
    m_frameParameterFreq(0),
    m_dynamicParameterFreq(0),
    m_stream(NULL),
    m_eStatus(PS_UnInitialized),
    m_RenderThread(this),
    m_PollingThread(this)
{
}
EncoderPipeline::~EncoderPipeline()
{
    Terminate();
}
AMF_RESULT  EncoderPipeline::Init(int threadID)
{
    AMF_RESULT res = AMF_OK;
    // Creating objects
    res = AMFCreateContext(&m_context);
    CHECK_AMF_ERROR_RETURN(res, L"AMFCreateContext() failed.");

    m_pFrameProvider = CreateFrameProvider(m_context, m_params);

    res = m_pFrameProvider->Init();
    CHECK_AMF_ERROR_RETURN(res, L"FrameProvider->Init() failed.");

    res = AMFCreateComponent(m_context, AMFVideoEncoderHW_AVC, &m_encoder);
    CHECK_AMF_ERROR_RETURN(res, L"AMFCreateComponent() failed.");

    amf::AMF_ENGINE_TYPE eEngineType = m_pFrameProvider->GetEngineType();
    m_encoder->SetProperty(amf::VIDEO_ENCODER_ENGINE_TYPE, amf_int64(eEngineType));

    // init encoder
    amf_int32 width = 0;
    amf_int32 height = 0;

    amf_int64 formatInt = amf::AMF_SURFACE_BGRA;
    if( m_params->GetParam(FORMAT_PARAM_NAME, formatInt) != AMF_OK )
    {
        m_params->SetParam(FORMAT_PARAM_NAME, formatInt);
    }
    amf::AMF_SURFACE_FORMAT format = amf::AMF_SURFACE_FORMAT(formatInt);

    res = m_params->GetParam(amf::VIDEO_ENCODER_WIDTH, width);
    CHECK_PARAMETER_RETURN(res, amf::VIDEO_ENCODER_WIDTH);

    res = m_params->GetParam(amf::VIDEO_ENCODER_HEIGHT, height);
    CHECK_PARAMETER_RETURN(res, amf::VIDEO_ENCODER_HEIGHT);


    PushProperties(ParametersManager::ParamEncoderStatic, m_encoder);

    res = m_encoder->Init(format, width, height);
    CHECK_AMF_ERROR_RETURN(res, L"m_encoder->Init() failed.");

    PushProperties(ParametersManager::ParamEncoderDynamic, m_encoder);

    m_params->GetParam(FRAMES_PARAM_NAME, m_framesRequested);

    m_params->GetParam(SETFRAMEPARAMFREQ_PARAM_NAME, m_frameParameterFreq);
    if(m_frameParameterFreq != 0) 
    { // to demo frame-specific properties - will be applied to each N-th frame (force IDR)
        m_params->SetParam(amf::VIDEO_ENCODER_FORCE_PICTURE_TYPE, amf_int64(amf::VIDEO_ENCODER_PICTURE_TYPE_IDR) );
    }
    
    m_params->GetParam(SETDYNAMICPARAMFREQ_PARAM_NAME, m_dynamicParameterFreq);
    

    // open file
    std::wstring outputPath;
    res = m_params->GetParamWString(PARAM_NAME_OUTPUT, outputPath);
    CHECK_PARAMETER_RETURN(res, PARAM_NAME_OUTPUT);

    // modify path to separate output files for each thread
    if(threadID != -1)
    {
        std::wstring::size_type pos_dot = outputPath.rfind(L'.');
        if(pos_dot == std::wstring::npos)
        {
            LOG_ERROR(L"Bad file name (no extension): " << outputPath);
            return AMF_FAIL;
        }
        outputPath = outputPath.substr(0, pos_dot) + L"_" + (wchar_t)(threadID+L'0') + outputPath.substr(pos_dot);
    }

    m_stream = AMFDataStream::Create(outputPath.c_str(), AMF_FileWrite);
    if(m_stream == NULL )
    {
        LOG_ERROR(L"Failed to open file: " << outputPath);
        return AMF_FAIL;
    }
    SetStatus(PS_Initialized);
    return AMF_OK;
}

void EncoderPipeline::PushProperties(ParametersManager::ParamType ptype, amf::AMFPropertyStorage *storage)
{
    amf_size count = m_params->GetParamCount();
    for(amf_size i = 0; i < count; i++)
    {
        std::wstring name;
        amf::AMFVariant value;
        if(m_params->GetParamAt(i, name, &value) == AMF_OK)
        {
            ParametersManager::ParamDescription description;
            m_params->GetParamDescription(name.c_str(), description);
            if(description.m_Type == ptype)
            {
                AMF_RESULT err = storage->SetProperty(description.m_Name.c_str(), value); // use original name
                LOG_AMF_ERROR(err, L"m_encoder->SetProperty(" << description.m_Name << L") failed " );
            }
        }
    }
}

AMF_RESULT  EncoderPipeline::Run()
{
    m_stat.Start();

    SetStatus(PS_Running);

    m_RenderThread.Start();
    m_PollingThread.Start();

    return AMF_OK;
}
AMF_RESULT  EncoderPipeline::Terminate()
{
    m_RenderThread.RequestStop();
    m_RenderThread.WaitForStop();

    m_PollingThread.RequestStop();
    m_PollingThread.WaitForStop();

    if(m_stream != NULL)
    {
        m_stream = NULL;
    }
    return AMF_OK;
}

void EncoderPipeline::RenderThread::Run()
{
    AMF_RESULT res = AMF_OK;
    for( amf_int64 i = 0 ; i<m_pHost->m_framesRequested; i++)
    {
        if(StopRequested())
        {
            break;
        }
        m_pHost->m_stat.StartRender();
        amf::AMFSurfacePtr surface;
        res = m_pHost->m_pFrameProvider->GetFrame(surface);
        if(res == AMF_EOF)
        {
            break;
        }
        if(m_pHost->m_frameParameterFreq != 0 && m_pHost->m_framesSubmitted !=0 && (m_pHost->m_framesSubmitted % m_pHost->m_frameParameterFreq) == 0)
        { // apply frame-specific properties to the current frame
            m_pHost->PushProperties(ParametersManager::ParamEncoderFrame, surface);
        }
        if(m_pHost->m_dynamicParameterFreq != 0 && m_pHost->m_framesSubmitted !=0 && (m_pHost->m_framesSubmitted % m_pHost->m_dynamicParameterFreq) == 0)
        { // apply dynamic properties to the encoder
            m_pHost->PushProperties(ParametersManager::ParamEncoderDynamic, m_pHost->m_encoder);
        }
        
        m_pHost->m_stat.StopRender();
        LOG_AMF_ERROR(res , L"m_pFrameProvider->GetFrame() failed");
        do
        {
            res = m_pHost->m_encoder->SubmitInput(surface);
            if(res != AMF_INPUT_FULL)
            {
                break;
            }
            if(StopRequested())
            {
                break;
            }
            amf_sleep(5);
        }
        while(true);

        if(res == AMF_OK)
        {
            m_pHost->m_framesSubmitted++;
        }
        LOG_AMF_ERROR(res , L"m_encoder->SubmitInput() failed");
    }
    res = m_pHost->m_encoder->Drain();
    LOG_AMF_ERROR(res , L"m_encoder->Drain() failed");
}

void EncoderPipeline::PollingThread::Run()
{
    AMF_RESULT res = AMF_OK;
    while(true)
    {
        if(StopRequested())
        {
            break;
        }
        amf::AMFDataPtr data;
        res = m_pHost->m_encoder->QueryOutput(&data);
        if(res == AMF_OK && data != NULL)
        {
            m_pHost->m_framesEncoded++; 
            amf::AMFBufferPtr buffer(data);

            amf_size towrite = buffer->GetSize();
            amf_size written = m_pHost->m_stream->Write(buffer->GetNative(), towrite);
            if(written != towrite)
            {
                LOG_ERROR(L"Failed to write "<< towrite << L"bytes");
            }

            if( m_pHost->m_framesEncoded == m_pHost->m_framesRequested)
            {
            }
        }
        else if(res == AMF_REPEAT)
        {
            amf_sleep(3);
        }
        else if(res == AMF_EOF)
        {
            break;
        }
    }
    m_pHost->m_stat.Stop();
    m_pHost->m_stat.SetFrameCount(m_pHost->m_framesEncoded);
    m_pHost->SetStatus(PS_Eof);
}
double          EncoderPipeline::GetFPS()
{
    return m_stat.GetFPS();
}
void            EncoderPipeline::LogStat()
{
    m_stat.DisplayResult();
}
