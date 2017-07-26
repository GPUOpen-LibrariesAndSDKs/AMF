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

// TranscodeHW.cpp : Defines the entry point for the console application.
//

#include <tchar.h>
#include <iterator>
#include <functional>
#include <algorithm>
#include <iostream>
#include <cctype>
#include <vector>

#include "public/common/AMFFactory.h"
#include "../common/ParametersStorage.h"
#include "../common/TranscodePipeline.h"
#include "../common/CmdLineParser.h"
#include "public/include/core/Debug.h"

class PreviewWindow
{
public: 
    PreviewWindow()
    :m_hWnd(0)
    {
    }

    ~PreviewWindow()
    {
        if(m_hWnd != NULL)
        {
            ::DestroyWindow(m_hWnd);
        }
    }

    bool Create(amf_int32 width, amf_int32 height)
    {
        if(m_hWnd != NULL)
        {
            return true;
        }
        HINSTANCE hInstance = (HINSTANCE)GetModuleHandle(NULL);

        WNDCLASSEX wcex     = {0};
        wcex.cbSize         = sizeof(WNDCLASSEX);
        wcex.style          = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wcex.lpfnWndProc    = DefWindowProc;
        wcex.cbClsExtra     = 0;
        wcex.cbWndExtra     = 0;
        wcex.hInstance      = hInstance;
        wcex.hIcon          = LoadIcon(NULL, IDI_APPLICATION);
        wcex.hCursor        = LoadCursor(NULL,IDC_ARROW);
        wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
        wcex.lpszClassName  = L"videorender";
        wcex.hIconSm        = LoadIcon(NULL, IDI_APPLICATION);

        RegisterClassEx(&wcex);

        int posX = 100;
        int posY = 100;

        m_hWnd = CreateWindow( L"videorender", L"VIDEORENDER", 
            WS_OVERLAPPEDWINDOW | WS_CAPTION | WS_BORDER | WS_POPUP,
          posX, posY, width, height, NULL, NULL, hInstance, NULL);

        ::ShowWindow(m_hWnd, SW_NORMAL);
    
        ::UpdateWindow(m_hWnd);
        return true;
    }

    void ProcessWindowMessages()
    {
        if(m_hWnd)
        {
            MSG msg={0};
            while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            } 
        }
    }
    HWND GetHwnd() const
    {
        return m_hWnd;
    }

private:
    HWND            m_hWnd;
};


static const wchar_t* PARAM_NAME_THREADCOUNT = L"THREADCOUNT";
static const wchar_t* PARAM_NAME_PREVIEW_MODE = L"PREVIEWMODE";


static AMF_RESULT RegisterCodecParams(ParametersStorage* pParams)
{
    pParams->SetParamDescription(TranscodePipeline::PARAM_NAME_CODEC, ParamCommon, L"Codec name (AVC or H264, HEVC or H265)", ParamConverterCodec);
    return AMF_OK;
}


AMF_RESULT RegisterParams(ParametersStorage* pParams)
{
    pParams->SetParamDescription(TranscodePipeline::PARAM_NAME_OUTPUT, ParamCommon, L"Output file name", NULL);
    pParams->SetParamDescription(TranscodePipeline::PARAM_NAME_INPUT, ParamCommon,  L"Input file name", NULL);

    pParams->SetParamDescription(TranscodePipeline::PARAM_NAME_SCALE_WIDTH, ParamCommon, L"Frame width (integer, default = 0)", ParamConverterInt64);
    pParams->SetParamDescription(TranscodePipeline::PARAM_NAME_SCALE_HEIGHT, ParamCommon, L"Frame height (integer, default = 0)", ParamConverterInt64);

    pParams->SetParamDescription(TranscodePipeline::PARAM_NAME_ADAPTERID, ParamCommon, L"Index of GPU adapter (number, default = 0)", NULL);

    pParams->SetParamDescription(TranscodePipeline::PARAM_NAME_ENGINE, ParamCommon,  L"Specifiy engine type (DX9, DX11)", NULL);
    pParams->SetParamDescription(TranscodePipeline::PARAM_NAME_FRAMES, ParamCommon, L"Number of frames to render (in frames, default = 0 - means all )", ParamConverterInt64);


    // to demo frame-specific properties - will be applied to each N-th frame (force IDR)
    pParams->SetParam(AMF_VIDEO_ENCODER_FORCE_PICTURE_TYPE, amf_int64(AMF_VIDEO_ENCODER_PICTURE_TYPE_IDR));

    pParams->SetParamDescription(PARAM_NAME_THREADCOUNT, ParamCommon, L"Number of session run ip parallel (number, default = 1)", ParamConverterInt64);
    pParams->SetParamDescription(PARAM_NAME_PREVIEW_MODE, ParamCommon, L"Preview Mode (bool, default = false)", ParamConverterInt64);
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
#ifdef _DEBUG
    g_AMFFactory.GetDebug()->AssertsEnable(true);
    g_AMFFactory.GetTrace()->SetGlobalLevel(AMF_TRACE_INFO);
    g_AMFFactory.GetTrace()->SetWriterLevel(AMF_TRACE_WRITER_DEBUG_OUTPUT, AMF_TRACE_INFO);
    g_AMFFactory.GetTrace()->SetWriterLevel(AMF_TRACE_WRITER_CONSOLE, AMF_TRACE_INFO);

#else
    g_AMFFactory.GetDebug()->AssertsEnable(false);
    g_AMFFactory.GetTrace()->SetGlobalLevel(AMF_TRACE_WARNING);
#endif

    amf_increase_timer_precision();
    ParametersStorage params;
    RegisterCodecParams(&params);

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
    params.GetParamWString(TranscodePipeline::PARAM_NAME_CODEC, codec);
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
    PreviewWindow previewWindow;
    bool previewMode = false;
    params.GetParam(PARAM_NAME_PREVIEW_MODE, previewMode);
    if(previewMode)
    {
        previewWindow.Create(1024, 768);
        ShowWindow(previewWindow.GetHwnd(), SW_SHOW);
    }

    amf_int32 threadCount = 1;
    params.GetParam(PARAM_NAME_THREADCOUNT, threadCount);
    if(threadCount < 1)
    {
        threadCount = 1;
    }

    // run in multiple threads
    std::vector<TranscodePipeline*> threads;

    // start threads
    int counter = 0;
    for(amf_int32 i = 0 ; i < threadCount; i++)
    {
        TranscodePipeline *pipeline= new TranscodePipeline();
        AMF_RESULT res = pipeline->Init(&params, i == 0 ? previewWindow.GetHwnd() : NULL, threadCount ==1 ? -1 : counter);
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

    LOG_SUCCESS(L"Start: " << threadCount << L" threads");

    for(std::vector<TranscodePipeline*>::iterator it = threads.begin(); it != threads.end(); it++)
    {
        (*it)->Run();
    }

    // wait till end
    while(true)
    {
        if(previewWindow.GetHwnd())
        {
            previewWindow.ProcessWindowMessages();
        }
        bool bRunning = false;
        for(std::vector<TranscodePipeline*>::iterator it = threads.begin(); it != threads.end(); it++)
        {
            if((*it)->GetState() != PipelineStateEof)
            {
                bRunning = true;
            }
        }
        if(!bRunning)
        {
            break;
        }
    }
    double encodeFPS = 0;

    for(std::vector<TranscodePipeline*>::iterator it = threads.begin(); it != threads.end(); it++)
    {
        (*it)->DisplayResult();
        encodeFPS += (*it)->GetFPS();
        (*it)->Terminate();
        delete *it;
    }
    std::wstringstream messageStream;
    messageStream.precision(1);
    messageStream.setf(std::ios::fixed, std::ios::floatfield);

    messageStream << L" Average FPS: " << encodeFPS / threadCount;
    messageStream << L" Combined FPS: " << encodeFPS;

    LOG_SUCCESS(messageStream.str());

    g_AMFFactory.Terminate();
    return 0;
}

