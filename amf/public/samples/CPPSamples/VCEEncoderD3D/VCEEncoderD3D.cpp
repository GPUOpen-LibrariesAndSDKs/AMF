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

#include <comdef.h>
#include "INITGUID.H"
#include "DXGIDebug.h"
#include "public/common/AMFFactory.h"

#include "../common/CmdLogger.h"
#include "../common/CmdLineParser.h"
#include "../common/ParametersStorage.h"
#include "RenderEncodePipeline.h"


static const wchar_t* PARAM_NAME_THREADCOUNT = L"THREADCOUNT";


static AMF_RESULT ParamConverterCodec(const std::wstring& value, amf::AMFVariant& valueOut)
{
    std::wstring paramValue;

    std::wstring uppValue = toUpper(value);
    if(uppValue == L"AVC" || uppValue == L"H264" || uppValue == L"H.264")
    {
        paramValue = AMFVideoEncoderVCE_AVC;
    } 
    else if(uppValue == L"HEVC" || uppValue == L"H265" || uppValue == L"H.265")
    {
        paramValue = AMFVideoEncoder_HEVC;
    } 
    else 
    {
        LOG_ERROR(L"Invalid codec name \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = paramValue.c_str();
    return AMF_OK;
}
static AMF_RESULT RegisterCodecParams(ParametersStorage* pParams)
{
    pParams->SetParamDescription(RenderEncodePipeline::PARAM_NAME_CODEC, ParamCommon, L"Codec name (AVC or H264, HEVC or H265)", ParamConverterCodec);
    return AMF_OK;
}

static AMF_RESULT RegisterParams(ParametersStorage* pParams)
{
    
    pParams->SetParamDescription(RenderEncodePipeline::PARAM_NAME_OUTPUT, ParamCommon, L"Output file name", NULL);
    pParams->SetParamDescription(RenderEncodePipeline::PARAM_NAME_RENDER, ParamCommon,  L"Specifies render type (DX9, DX9Ex, DX11, OpenGL, OpenCL, Host, OpenCLDX9, OpenCLDX11, OpenGLDX9, OpenGLDX11, OpenCLOpenGLDX9, OpenCLOpenGLDX11, HostDX9, HostDX11, DX11DX9)", NULL);

    pParams->SetParamDescription(RenderEncodePipeline::PARAM_NAME_WIDTH, ParamCommon, L"Frame width (integer, default = 0)", ParamConverterInt64);
    pParams->SetParamDescription(RenderEncodePipeline::PARAM_NAME_HEIGHT, ParamCommon, L"Frame height (integer, default = 0)", ParamConverterInt64);
    pParams->SetParamDescription(RenderEncodePipeline::PARAM_NAME_FRAMES, ParamCommon, L"Number of frames to render (in frames, default = 100 )", ParamConverterInt64);

    pParams->SetParamDescription(RenderEncodePipeline::PARAM_NAME_ADAPTERID, ParamCommon, L"Index of GPU adapter (number, default = 0)", NULL);
    pParams->SetParamDescription(RenderEncodePipeline::PARAM_NAME_WINDOW_MODE, ParamCommon, L"Render to window (true, false, default = false)", ParamConverterBoolean);
    pParams->SetParamDescription(RenderEncodePipeline::PARAM_NAME_FULLSCREEN, ParamCommon, L"Full screen (true, false, default = false)", ParamConverterBoolean);

	pParams->SetParamDescription(RenderEncodePipeline::PARAM_NAME_QUERY_INST_COUNT, ParamCommon, L"If the flag is set, the number of independent VCE instances will be quried and printed.", ParamConverterBoolean);
	pParams->SetParamDescription(RenderEncodePipeline::PARAM_NAME_SELECT_INSTANCE, ParamCommon, L"If there are more than one VCE Instances, you can force which instance to use. Valid range is [0.. (number of instances - 1)] (integer, default = depends on usage).", ParamConverterInt64);


    // to demo frame-specific properties - will be applied to each N-th frame (force IDR)
    // pParams->SetParam(AMF_VIDEO_ENCODER_FORCE_PICTURE_TYPE, amf_int64(AMF_VIDEO_ENCODER_PICTURE_TYPE_IDR) );

    pParams->SetParamDescription(PARAM_NAME_THREADCOUNT, ParamCommon, L"Number of session run ip parallel (number, default = 1)", ParamConverterInt64);
    return AMF_OK;
}

int _tmain(int argc, _TCHAR* argv[])
{
    AMF_RESULT              res = AMF_OK; // error checking can be added later
    res = g_AMFFactory.Init();
    if(res != AMF_OK)
    {
        wprintf(L"AMF Failed to initialize");
        return 1;
    }

    AMFCustomTraceWriter writer(AMF_TRACE_WARNING);
    g_AMFFactory.GetDebug()->AssertsEnable(false);
    amf_increase_timer_precision();

    ParametersStorage params;
    RegisterCodecParams(&params);

    // parse for codec name
    if (!parseCmdLineParameters(&params))
    {
        LOG_INFO(L"+++ AVC codec +++");
        ParametersStorage paramsAVC;
        RegisterCodecParams(&paramsAVC);
        RegisterEncoderParamsAVC(&paramsAVC);
        LOG_INFO(paramsAVC.GetParamUsage());

        LOG_INFO(L"+++ HEVC codec +++");
        ParametersStorage paramsHEVC;
        RegisterCodecParams(&paramsHEVC);
        RegisterEncoderParamsHEVC(&paramsHEVC);
        LOG_INFO(paramsHEVC.GetParamUsage());
        return -1;
    }

    std::wstring codec = AMFVideoEncoderVCE_AVC;
    params.GetParamWString(RenderEncodePipeline::PARAM_NAME_CODEC, codec);
    if(codec == AMFVideoEncoderVCE_AVC)
    {
        RegisterEncoderParamsAVC(&params);
    }
    else if(codec == AMFVideoEncoder_HEVC)
    {
        RegisterEncoderParamsHEVC(&params);
    }
    else
    {
        LOG_ERROR(L"Invalid codec ID");

    }
    RegisterParams(&params);

    // parse again with codec - dependent set of parameters
    if (!parseCmdLineParameters(&params))
    {
        return -1;
    }



    amf_int32 threadCount = 1;
    params.GetParam(PARAM_NAME_THREADCOUNT, threadCount);
    if(threadCount < 1)
    {
        threadCount = 1;
    }

    // run in multiple threads
    std::vector<RenderEncodePipeline*> threads;

    // start threads
    int counter = 0;
    for(amf_int32 i = 0 ; i < threadCount; i++)
    {
        RenderEncodePipeline *pipeline= new RenderEncodePipeline();
        AMF_RESULT res = pipeline->Init(&params, threadCount ==1 ? -1 : counter);
        if(res == AMF_OK)
        {
            threads.push_back(pipeline);
            counter++;
        }
        else
        {
            delete pipeline;
        }
    }

    if(threads.size() == 0)
    {
        LOG_ERROR(L"No threads were successfuly run");
        return -101;
    }

	LOG_SUCCESS(L"Start: " << threads.size() << L" threads");
    for(std::vector<RenderEncodePipeline*>::iterator it = threads.begin(); it != threads.end(); it++)
    {
        (*it)->Run();
    }

    // wait till end
    while(true)
    {
        amf_sleep(100);
        bool bRunning = false;
        for(std::vector<RenderEncodePipeline*>::iterator it = threads.begin(); it != threads.end(); it++)
        {
            if((*it)->GetState() != PipelineStateEof)
            {
                bRunning = true;
                (*it)->ProcessWindowMessages();
            }
        }
        if(!bRunning)
        {
            break;
        }
    }
    double encodeFPS = 0;

    for(std::vector<RenderEncodePipeline*>::iterator it = threads.begin(); it != threads.end(); it++)
    {
        (*it)->DisplayResult();
        encodeFPS += (*it)->GetFPS();

        (*it)->Terminate();
        delete *it;
    }
    std::wstringstream messageStream;
    messageStream.precision(1);
    messageStream.setf(std::ios::fixed, std::ios::floatfield);

    messageStream << L" Average FPS: " << encodeFPS / threads.size();
    messageStream << L" Combined FPS: " << encodeFPS;

    LOG_SUCCESS(messageStream.str());

    g_AMFFactory.Terminate();
    return 0;
}
