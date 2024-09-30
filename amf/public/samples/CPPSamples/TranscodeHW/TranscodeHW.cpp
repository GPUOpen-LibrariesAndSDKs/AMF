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
// Copyright (c) 2018 Advanced Micro Devices, Inc. All rights reserved.
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
#if defined(_WIN32)
    #include <tchar.h>
    #include <windows.h>
#else
    #include <X11/Xlib.h>
    #include <X11/Xutil.h>
    #include <X11/Xos.h>
#endif

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
#include "../common/PipelineDefines.h"
#include "public/include/core/Debug.h"

static AMF_RESULT ParamConverterScaleType(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_CONVERTER_SCALE_ENUM paramValue;

    std::wstring uppValue = toUpper(value);
    if (uppValue == L"BILINEAR" || uppValue == L"0")
    {
        paramValue = AMF_VIDEO_CONVERTER_SCALE_BILINEAR;
    }
    else if (uppValue == L"BICUBIC" || uppValue == L"1") {
        paramValue = AMF_VIDEO_CONVERTER_SCALE_BICUBIC;
    }
    else {
        LOG_ERROR(L"AMF_VIDEO_CONVERTER_SCALE_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

#if defined(_WIN32)
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
        ::ShowWindow(m_hWnd, SW_SHOW);
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
    amf_handle GetHwnd() const
    {
        return (amf_handle)m_hWnd;
    }
    amf_handle GetDisplay() const
    {
        return (amf_handle)nullptr;
    }

private:
    HWND            m_hWnd;
};
#elif defined(__linux)
class PreviewWindow
{
public:
    PreviewWindow()
    :m_hWnd(0), m_pDisplay(nullptr), WM_DELETE_WINDOW(0)
    {
    }

    ~PreviewWindow()
    {
        if(m_hWnd)
        {
            XDestroyWindow(m_pDisplay, m_hWnd);
            XCloseDisplay(m_pDisplay);
        }
    }

    bool Create(amf_int32 width, amf_int32 height)
    {
        if(m_hWnd)
        {
            return true;
        }
        m_pDisplay = XOpenDisplay(NULL);
        int screen_num = DefaultScreen(m_pDisplay);

        Window parentWnd = DefaultRootWindow(m_pDisplay);

        m_hWnd = XCreateSimpleWindow(m_pDisplay,
        parentWnd,
        10, 10, width, height,
        1, BlackPixel(m_pDisplay, screen_num), WhitePixel(m_pDisplay, screen_num));
        XSelectInput(m_pDisplay, m_hWnd, ExposureMask | KeyPressMask | StructureNotifyMask);
        XMapWindow(m_pDisplay, m_hWnd);
        XStoreName(m_pDisplay, m_hWnd, "PlaybackHW");

        WM_DELETE_WINDOW = XInternAtom(m_pDisplay, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(m_pDisplay, m_hWnd, &WM_DELETE_WINDOW, 1);

        return true;
    }

    void ProcessWindowMessages()
    {
        if(m_hWnd)
        {
            XEvent e;
            bool bRun = true;

            while (XCheckMaskEvent(m_pDisplay,0xFFFFFF , &e))
            {
                switch(e.type)
                {
                case Expose:
                    break;
                case KeyPress:
                    break;
                case ConfigureNotify:
                    {
                        XConfigureEvent xce = e.xconfigure;
//                        s_pPipeline->CheckForResize();
                    }
                    break;
                case ClientMessage:
                    if((static_cast<unsigned int>(e.xclient.data.l[0]) == WM_DELETE_WINDOW))
                    {
                        bRun = false;
                    }
                    break;
                }
            }
        }
    }
    amf_handle GetHwnd() const
    {
        return (amf_handle)m_hWnd;
    }
    amf_handle GetDisplay() const
    {
        return (amf_handle)m_pDisplay;
    }

private:
    Window            m_hWnd;
    Display*          m_pDisplay;
    Atom              WM_DELETE_WINDOW;

};
#endif


static const wchar_t* PARAM_NAME_PREVIEW_MODE = L"PREVIEWMODE";
static const wchar_t* PARAM_NAME_REPEAT       = L"REPEAT";


static AMF_RESULT RegisterCodecParams(ParametersStorage* pParams)
{
    pParams->SetParamDescription(PARAM_NAME_CODEC, ParamCommon, L"Codec name (AVC or H264, HEVC or H265, AV1)", ParamConverterCodec);
    return AMF_OK;
}


AMF_RESULT RegisterParams(ParametersStorage* pParams)
{
    pParams->SetParamDescription(PARAM_NAME_OUTPUT, ParamCommon, L"Output file name", NULL);
    pParams->SetParamDescription(PARAM_NAME_INPUT,  ParamCommon, L"Input file name", NULL);

    pParams->SetParamDescription(TranscodePipeline::PARAM_NAME_SCALE_WIDTH,  ParamCommon, L"Frame width (integer, default = 0)", ParamConverterInt64);
    pParams->SetParamDescription(TranscodePipeline::PARAM_NAME_SCALE_HEIGHT, ParamCommon, L"Frame height (integer, default = 0)", ParamConverterInt64);
    pParams->SetParamDescription(TranscodePipeline::PARAM_NAME_FRAMES,       ParamCommon, L"Number of frames to render (in frames, default = 0 - means all )", ParamConverterInt64);
    pParams->SetParamDescription(TranscodePipeline::PARAM_NAME_SCALE_TYPE,   ParamCommon, L"Frame height (integer, default = 0)", ParamConverterScaleType);

    pParams->SetParamDescription(PARAM_NAME_ADAPTERID, ParamCommon, L"Index of GPU adapter (number, default = 0)", NULL);
    pParams->SetParamDescription(PARAM_NAME_ENGINE,    ParamCommon, L"Specifiy engine type (DX9, DX11, Vulkan)", NULL);

    pParams->SetParamDescription(PARAM_NAME_THREADCOUNT,   ParamCommon, L"Number of session run ip parallel (number, default = 1)", ParamConverterInt64);
    pParams->SetParamDescription(PARAM_NAME_PREVIEW_MODE,  ParamCommon, L"Preview Mode (bool, default = false)", ParamConverterBoolean);
    pParams->SetParamDescription(PARAM_NAME_COMPUTE_QUEUE, ParamCommon, L"Vulkan Compute Queue Index (integer, default = 0, range [0,queueCount-1])", ParamConverterInt64);
    pParams->SetParamDescription(PARAM_NAME_TRACE_LEVEL, ParamCommon, L"Set the trace level (integer, default = 1 - means AMF_TRACE_WARNING)", ParamConverterInt64);

    // to demo frame-specific properties - will be applied to each N-th frame (force IDR)
    pParams->SetParam(AMF_VIDEO_ENCODER_FORCE_PICTURE_TYPE, amf_int64(AMF_VIDEO_ENCODER_PICTURE_TYPE_IDR));

    // add AAA properties
    pParams->SetParamDescription(AMF_VIDEO_DECODER_ENABLE_SMART_ACCESS_VIDEO, ParamCommon, L"Enable decoder smart access video feature (bool, default = false)", ParamConverterBoolean);

    pParams->SetParamDescription(AMF_VIDEO_DECODER_LOW_LATENCY, ParamCommon, L"Enable low latency decode, false = regular decode (bool, default = false)", ParamConverterBoolean);
    // SW encoder enable - require full build version of ffmpeg dlls with shared libs
    pParams->SetParamDescription(PARAM_NAME_SWENCODE, ParamCommon, L"Enable SW encoder (true, false default =  false)", ParamConverterBoolean);

    // Enable tracing to log files.
    pParams->SetParamDescription(PARAM_NAME_TRACE_TO_FILE, ParamCommon, L"Enable Tracing to File (true, false default = false)", ParamConverterBoolean);

    // allow Transcode to run multiple times with the same paramters
    pParams->SetParamDescription(PARAM_NAME_REPEAT, ParamCommon, L"How many times the command should be executed with the same parameters (integer, default = 1)", ParamConverterInt64);
    
    // allow Transcode to run decode with ffmpeg deocder.
    pParams->SetParamDescription(PARAM_NAME_SWDECODE, ParamCommon, L"Enable FFMPEG decoder.(true, false default = false)", ParamConverterBoolean);

    return AMF_OK;
}

#if defined(_WIN32)
int _tmain(int /* argc */, _TCHAR* /* argv */[])
#else
int main(int argc, char* argv[])
#endif
{
    AMF_RESULT res = AMF_OK; // error checking can be added later
    res = g_AMFFactory.Init();
    if(res != AMF_OK)
    {
        wprintf(L"AMF Failed to initialize");
        return 1;
    }

    amf_increase_timer_precision();

    ParametersStorage params;
    RegisterParams(&params);
    RegisterCodecParams(&params);
    RegisterPreProcessingParams(&params);
    RegisterEncoderParamsAVC(&params);
    RegisterEncoderParamsHEVC(&params);
    RegisterEncoderParamsAV1(&params);
#if defined(_WIN32)
    if (!parseCmdLineParameters(&params))
#else
    if (!parseCmdLineParameters(&params, argc, argv))
#endif
    {
        LOG_INFO(L"+++ Pre processor +++");
        ParametersStorage paramsPreProc;
        RegisterPreProcessingParams(&paramsPreProc);
        LOG_INFO(paramsPreProc.GetParamUsage());

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

        LOG_INFO(L"+++ AV1 codec +++");
        ParametersStorage paramsAV1;
        RegisterCodecParams(&paramsAV1);
        RegisterEncoderParamsAV1(&paramsAV1);
        LOG_INFO(paramsAV1.GetParamUsage());

        return -1;
    }


    // check if tracing level change was requested
    amf_int32 traceLevel = AMF_TRACE_WARNING;
    params.GetParam(PARAM_NAME_TRACE_LEVEL, traceLevel);

    AMFCustomTraceWriter writer(traceLevel);

#ifdef _DEBUG
    g_AMFFactory.GetDebug()->AssertsEnable(true);
    g_AMFFactory.GetTrace()->SetGlobalLevel(traceLevel);
    g_AMFFactory.GetTrace()->EnableWriter  (AMF_TRACE_WRITER_CONSOLE, true);
    g_AMFFactory.GetTrace()->SetWriterLevel(AMF_TRACE_WRITER_CONSOLE, traceLevel);
    g_AMFFactory.GetTrace()->EnableWriter  (AMF_TRACE_WRITER_DEBUG_OUTPUT, true);
    g_AMFFactory.GetTrace()->SetWriterLevel(AMF_TRACE_WRITER_DEBUG_OUTPUT, traceLevel);

    amf_bool traceToFile = false;
    params.GetParam(PARAM_NAME_TRACE_TO_FILE, traceToFile);
    if (true == traceToFile)
    {
        g_AMFFactory.GetTrace()->EnableWriter(AMF_TRACE_WRITER_FILE, true);
        g_AMFFactory.GetTrace()->SetWriterLevel(AMF_TRACE_WRITER_FILE, traceLevel);
    }

#else
    g_AMFFactory.GetDebug()->AssertsEnable(false);
    g_AMFFactory.GetTrace()->SetGlobalLevel(traceLevel);
#endif


    // figure out the codec
    std::wstring codec = AMFVideoEncoderVCE_AVC;
    params.GetParamWString(PARAM_NAME_CODEC, codec);

    // clear existing parameters
    params.Clear();

    // update the proper parameters for the correct codec
    RegisterParams(&params);
    RegisterCodecParams(&params);
    RegisterPreProcessingParams(&params);
    if (codec == AMFVideoEncoderVCE_AVC)
    {
        RegisterEncoderParamsAVC(&params);
    }
    else if(codec == AMFVideoEncoder_HEVC)
    {
        RegisterEncoderParamsHEVC(&params);
    }
    else if (codec == AMFVideoEncoder_AV1)
    {
        RegisterEncoderParamsAV1(&params);
    }
    else
    {
        LOG_ERROR(L"Invalid codec ID");
        return -1;
    }

    // parse again with codec - dependent set of parameters
#if defined(_WIN32)
    if (!parseCmdLineParameters(&params))
#else
    if (!parseCmdLineParameters(&params, argc, argv))
#endif
    {
        return -1;
    }

    PreviewWindow previewWindow;
    bool previewMode = false;
    params.GetParam(PARAM_NAME_PREVIEW_MODE, previewMode);
    if(previewMode)
    {
        previewWindow.Create(1024, 768);
    }

    amf_int32 threadCount = 1;
    params.GetParam(PARAM_NAME_THREADCOUNT, threadCount);
    if(threadCount < 1)
    {
        threadCount = 1;
    }

    amf_int32 loopCount = 1;
    params.GetParam(PARAM_NAME_REPEAT, loopCount);
    if (loopCount <= 0)
    {
        loopCount = 1;
    }
    if (loopCount > 1)
    {
        LOG_SUCCESS(L"Run: " << loopCount << L" times");
    }

    for (amf_int32 j = 0; j < loopCount; j++)
    {
        // run in multiple threads
        std::vector<TranscodePipeline*> threads;

        // start threads
        int counter = 0;
        for(amf_int32 i = 0 ; i < threadCount; i++)
        {
            TranscodePipeline *pipeline= new TranscodePipeline();
            res = pipeline->Init(&params,
                i == 0 ? previewWindow.GetHwnd() : NULL,
                i == 0 ? previewWindow.GetDisplay() : NULL,
                threadCount ==1 ? -1 : counter);
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

        // calculate FPS and clean-up threads
        double encodeFPS = 0;
        for(std::vector<TranscodePipeline*>::iterator it = threads.begin(); it != threads.end(); it++)
        {
            (*it)->DisplayResult();
            encodeFPS += (*it)->GetFPS();
            (*it)->Terminate();
            delete *it;
        }

        // print out FPS
        std::wstringstream messageStream;
        messageStream.precision(1);
        messageStream.setf(std::ios::fixed, std::ios::floatfield);

        messageStream << L" Average FPS: " << encodeFPS / threadCount;
        messageStream << L" Combined FPS: " << encodeFPS << std::endl;

        LOG_SUCCESS(messageStream.str());
    }

    g_AMFFactory.Terminate();
    return 0;
}

