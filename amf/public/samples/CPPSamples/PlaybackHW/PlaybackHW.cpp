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
// PlaybackHW.cpp : Defines the entry point for the application.
//

#include "targetver.h"

//#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#include <windows.h>

// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include "PlaybackHW.h"
#include <objbase.h>
#include <Commdlg.h>
#include <CommCtrl.h>

#include "public/common/AMFFactory.h"

#include "../common/PlaybackPipeline.h"
#include "../common/CmdLineParser.h"
#include "../common/CmdLogger.h"

#define MAX_LOADSTRING 100

// Global Variables:
static HWND    hToolbar; 
HINSTANCE hInst;                                // current instance
TCHAR szTitle[MAX_LOADSTRING];                    // The title bar text
TCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
static  UINT_PTR uiTimerID = 0;
static  UINT_PTR uiTimerToolbarID = 0;
static  bool bTrackingStarted = false;

static PlaybackPipeline *s_pPipeline = NULL;

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, HWND* , int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    OpenStreamProc(HWND, UINT, WPARAM, LPARAM);
void                FileOpen(HWND hwnd);
void                StreamOpen(HWND hwnd);
void                UpdateMenuItems(HMENU hMenu);
HWND                CreateClientWindow(HWND hWndParent);
void                ResizeClient(HWND hWndParent);
void                UpdateCaption(HWND hWnd);
void                ToggleToolbar(HWND hwnd);
void                CloseToolbar();

HWND                hClientWindow;


static INT_PTR CALLBACK ProgressDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
    class OptimizationThread : public amf::AMFThread, public amf::AMFComponentOptimizationCallback
    {
        bool m_started;
        amf_uint m_percent;
    public:
        OptimizationThread()
            :m_started(true), m_percent(0)
        {
        }
        ~OptimizationThread()
        {
        }
        bool started() {return m_started;}
        amf_uint percent() {return m_percent;}

    protected:
        virtual AMF_RESULT AMF_STD_CALL OnComponentOptimizationProgress(amf_uint percent)
        {
            m_percent = percent;
            return AMF_OK;
        }

        virtual void Run()
        {
            RequestStop();
            amf::AMFContextPtr pContext;
            g_AMFFactory.GetFactory()->CreateContext(&pContext);
            pContext->InitDX11(NULL);
            amf::AMFComponentPtr pComponent;
            g_AMFFactory.GetFactory()->CreateComponent(pContext, AMFVideoConverter, &pComponent);
            pComponent->Optimize(this);

            m_started = false;
        }
    };
    static OptimizationThread s_optimizationThread;

    switch(Message)
    {
    case WM_COMMAND:
        switch(LOWORD(wParam))
        {
        case IDCANCEL:
            EndDialog(hwnd, IDCANCEL);
            break;
        }
        break;
    case WM_INITDIALOG:
        {
            HWND hProgress = GetDlgItem(hwnd, IDC_PROGRESS_BAR);
            SendMessage(hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            s_optimizationThread.Start();
            SetTimer(hwnd, 100, 1, NULL);
        }
        return TRUE;
    case WM_TIMER:
        if(s_optimizationThread.started())
        {
            HWND hProgress = GetDlgItem(hwnd, IDC_PROGRESS_BAR);
            SendMessage(hProgress, PBM_SETPOS, DWORD(s_optimizationThread.percent()), 0);
        }
        else
        {
            s_optimizationThread.WaitForStop();
            KillTimer(hwnd, 100);
            EndDialog(hwnd, IDOK);
        }

    default:
        return FALSE;
    }
    return TRUE;
}

int APIENTRY _tWinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    MSG msg;

    g_AMFFactory.Init();
    {
        PlaybackPipeline        pipeline;
        s_pPipeline = &pipeline;
        ::CoInitializeEx(NULL,COINIT_MULTITHREADED);

        AMF_RESULT              res = AMF_OK; // error checking can be added later
        res = g_AMFFactory.Init();
        if(res != AMF_OK)
        {
            wprintf(L"AMF Failed to initialize");
            g_AMFFactory.Terminate();
            return 1;
        }


        g_AMFFactory.GetDebug()->AssertsEnable(true);

        HACCEL hAccelTable;

        // Initialize global strings
        LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
        LoadString(hInstance, IDC_PLAYBACKHW, szWindowClass, MAX_LOADSTRING);
        MyRegisterClass(hInstance);
        HWND hWnd = NULL;

        // Perform application initialization:
        if (!InitInstance (hInstance, &hWnd, nCmdShow))
        {
            CoUninitialize();
            g_AMFFactory.Terminate();
            return FALSE;
        }

        PostMessage(hWnd, WM_USER+1000, 0, 0);
        //------------------------------------------------------------------------------------------------------------
        if (parseCmdLineParameters(s_pPipeline))
        {
            if(s_pPipeline->Init(hClientWindow) == AMF_OK)
            {
                s_pPipeline->Play();
                UpdateMenuItems(::GetMenu(hWnd));
            }
        }
        else
        {
            LOG_INFO(s_pPipeline->GetParamUsage());
        }
        //------------------------------------------------------------------------------------------------------------


        hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_PLAYBACKHW));

        // Main message loop:
        while (GetMessage(&msg, NULL, 0, 0))
        {
                if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
                {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }
        CoUninitialize();
    }
    g_AMFFactory.Terminate();

    return (int) msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
//  COMMENTS:
//
//    This function and its usage are only necessary if you want this code
//    to be compatible with Win32 systems prior to the 'RegisterClassEx'
//    function that was added to Windows 95. It is important to call this function
//    so that the application will get 'well formed' small icons associated
//    with it.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style            = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra        = 0;
    wcex.cbWndExtra        = 0;
    wcex.hInstance        = hInstance;
    wcex.hIcon            = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_PLAYBACKHW));
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground    = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName    = MAKEINTRESOURCE(IDC_PLAYBACKHW);
    wcex.lpszClassName    = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassEx(&wcex);
}


//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, HWND* phWnd, int nCmdShow)
{

    hInst = hInstance; // Store instance handle in our global variable
    
    HWND hWnd = NULL;

    hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);

    if (!hWnd)
    {
        return FALSE;
    }

    uiTimerID = ::SetTimer(hWnd, 10000, 1000, NULL);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    UpdateMenuItems(::GetMenu(hWnd));
    *phWnd = hWnd;

    return TRUE;
}

void UpdateMenuItems(HMENU hMenu)
{
    amf::AMF_MEMORY_TYPE    presenterType = amf::AMF_MEMORY_DX11;
    {
        amf_int64 engineInt = amf::AMF_MEMORY_UNKNOWN;
        if(s_pPipeline->GetParam(PlaybackPipeline::PARAM_NAME_PRESENTER, engineInt) == AMF_OK)
        {
            if(amf::AMF_MEMORY_UNKNOWN != engineInt)
            {
                presenterType = (amf::AMF_MEMORY_TYPE)engineInt;
            }
        }
        else
        {
            s_pPipeline->SetParam(PlaybackPipeline::PARAM_NAME_PRESENTER, presenterType);
        }
    }

    CheckMenuItem(hMenu,ID_OPTIONS_PRESENTER_DX11,      MF_BYCOMMAND| ( presenterType == amf::AMF_MEMORY_DX11 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu,ID_OPTIONS_PRESENTER_DX9,       MF_BYCOMMAND| ( presenterType == amf::AMF_MEMORY_DX9 ? MF_CHECKED: MF_UNCHECKED));
    CheckMenuItem(hMenu,ID_OPTIONS_PRESENTER_OPENGL,    MF_BYCOMMAND| ( presenterType == amf::AMF_MEMORY_OPENGL ? MF_CHECKED: MF_UNCHECKED));

}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND    - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY    - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int wmId, wmEvent;
    PAINTSTRUCT ps;
    HDC hdc;

    switch (message)
    {
    case WM_USER+1000:
        if(DialogBox( hInst, MAKEINTRESOURCE(IDD_PROGRESS_DLG),  hWnd,  (DLGPROC) ProgressDlgProc ) != IDOK)
        {
            PostQuitMessage(0);
        }
        break;
    case WM_COMMAND:
        wmId    = LOWORD(wParam);
        wmEvent = HIWORD(wParam);
        // Parse the menu selections:
        switch (wmId)
        {
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        case ID_FILE_OPEN:
            FileOpen(hWnd);
            break;
        case ID_FILE_OPENNETWORKSTREAM:
            StreamOpen(hWnd);
            break;
        case ID_PLAYBACK_PLAY:
            if(s_pPipeline->GetState() == PipelineStateEof || s_pPipeline->GetState() == PipelineStateNotReady)
            {
                if(s_pPipeline->Init(hClientWindow) == AMF_OK)
                {
                    s_pPipeline->Play();
                    UpdateMenuItems(::GetMenu(hWnd));
                }
            }else if(s_pPipeline->GetState() == PipelineStateRunning)
            {
                s_pPipeline->Play();
            }

            break;
        case ID_PLAYBACK_PAUSE:
            s_pPipeline->Pause();
            break;
        case ID_PLAYBACK_STEP:
            s_pPipeline->Step();
            break;
        case ID_PLAYBACK_STOP:
            s_pPipeline->Stop();
            break;
        case ID_OPTIONS_PRESENTER_DX11:
            s_pPipeline->SetParam(PlaybackPipeline::PARAM_NAME_PRESENTER, amf::AMF_MEMORY_DX11);
            UpdateMenuItems(::GetMenu(hWnd));
            break;
        case ID_OPTIONS_PRESENTER_DX9:
            s_pPipeline->SetParam(PlaybackPipeline::PARAM_NAME_PRESENTER, amf::AMF_MEMORY_DX9);
            UpdateMenuItems(::GetMenu(hWnd));
            break;
        case ID_OPTIONS_PRESENTER_OPENGL:
            s_pPipeline->SetParam(PlaybackPipeline::PARAM_NAME_PRESENTER, amf::AMF_MEMORY_OPENGL);
            UpdateMenuItems(::GetMenu(hWnd));
            break;
        case ID_TOOLBAR:
            ToggleToolbar(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;
    case WM_PAINT:
        hdc = BeginPaint(hWnd, &ps);
        // TODO: Add any drawing code here...
        EndPaint(hWnd, &ps);
        break;
    case WM_CREATE:
        hClientWindow = CreateClientWindow(hWnd);
        ResizeClient(hWnd);
        break;
    case WM_SIZE:
        ResizeClient(hWnd);
        break;
    case WM_TIMER:
        UpdateCaption(hWnd);
        break;
    case WM_DESTROY:
        s_pPipeline->Terminate();
        CloseToolbar();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}


void FileOpen(HWND hwnd) 
{
    OPENFILENAME ofn;       // common dialog box structure
    WCHAR szFile[260];       // buffer for file name
     
    // Initialize OPENFILENAME
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = _countof(szFile);
    ofn.lpstrFilter = L"Videos\0*.WMV;*.WMA;*.AVI;*.ASF;*.FLV;*.BFI;*.CAF;*.GXF;*.IFF;*.RL2;*.MP4;*.3GP;*.QTFF;*.MKV;*.MK3D;*.MKA;*.MKS;*.MPG;*.MPEG;*.PS;*.TS;*.MXF;*.OGV;*.OGA;*.OGX;*.OGG;*.SPX;*.FLV;*.F4V;*.F4P;*.F4A;*.F4B;*.JSV;*.h264;*.264;*.vc1;*.mov;*.mvc;*.m1v;*.m2v;*.m2ts;*.vpk;*.yuv;*.rgb;*.nv12;*.h265;*.265\0All\0*.*\0";
//    ofn.lpstrFilter = L"Video streams\0*.h264;*.264;*.h265;*.265;*.hevc;\0All\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileName(&ofn)==TRUE)
    {
        s_pPipeline->SetParam(PlaybackPipeline::PARAM_NAME_INPUT, ofn.lpstrFile);
        s_pPipeline->Stop();
        if(s_pPipeline->Init(hClientWindow) == AMF_OK)
        {
            s_pPipeline->Play();
            UpdateMenuItems(::GetMenu(hwnd));
        }
    }
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

HWND CreateClientWindow(HWND hWndParent)
{
    WNDCLASSEX wcex;

    static wchar_t *ChildClassName = L"Client";
    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style            = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = DefWindowProc;
    wcex.cbClsExtra        = 0;
    wcex.cbWndExtra        = 0;
    wcex.hInstance        = hInst;
    wcex.hIcon            = 0;
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground    = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName    = 0;
    wcex.lpszClassName    = ChildClassName;
    wcex.hIconSm        = NULL;

    ATOM reg = RegisterClassEx(&wcex);


    HWND hClientWnd = CreateWindowEx( 0, ChildClassName, NULL, WS_CHILD | WS_VISIBLE,
        0, 0, 100, 100, 
        hWndParent, (HMENU) (int) (1) , hInst, NULL);
    return hClientWnd;
}
void ResizeClient(HWND hWndParent)
{
    if(hWndParent != NULL && hClientWindow!=NULL)
    {
        RECT client;
        ::GetClientRect(hWndParent, &client);
        ::MoveWindow(hClientWindow, client.left, client.top, client.right - client.left, client.bottom - client.top, TRUE);
    }
}
void                UpdateCaption(HWND hWnd)
{
    wchar_t text[1000];
    double FPS = s_pPipeline->GetFPS();
    swprintf(text,L"%s | FPS( %.1f)",szTitle, FPS);
    ::SetWindowText(hWnd, text);
}
//-------------------------------------------------------------------------------------------------
// toolbar
//-------------------------------------------------------------------------------------------------
#define MAX_SLIDER_VALUE 10000
static INT_PTR CALLBACK ToolbarDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
    HWND hSlider = GetDlgItem(hwnd, IDC_SEEK);
    switch(Message)
    {
    case WM_COMMAND:
        switch(LOWORD(wParam))
        {
        case IDCANCEL:
        case IDOK:
             CloseToolbar();
            break;
        }
        break;
    case WM_INITDIALOG:
        {
            SendMessage(hSlider, TBM_SETRANGE , 0, MAKELPARAM(0, MAX_SLIDER_VALUE));
            // configure slider

        }
        break;
    case WM_HSCROLL:
    {
        if(LOWORD(wParam) == TB_THUMBTRACK)
        { 
            bTrackingStarted = true;
        }
        if(LOWORD(wParam) == TB_ENDTRACK)
        { 
            amf_int32 pos = (amf_int32)SendMessage(hSlider, TBM_GETPOS , 0, 0);
            s_pPipeline->Seek(static_cast<amf_pts>(s_pPipeline->GetProgressSize() * double(pos) / double(MAX_SLIDER_VALUE)));
            bTrackingStarted = false;
        }
        break;
    }
    case WM_TIMER:
        if(wParam == uiTimerToolbarID && !bTrackingStarted)
        { // update scroll
            double duration =  s_pPipeline->GetProgressSize();
            if(duration != 0)
            { 
                amf_int32 pos = amf_int32(s_pPipeline->GetProgressPosition() * double(MAX_SLIDER_VALUE) / duration);
                SendMessage(hSlider, TBM_SETPOS , TRUE, pos);
            }
        }
        break;
    default:
        return FALSE;
    }
    return TRUE;
}
//-------------------------------------------------------------------------------------------------
void ToggleToolbar(HWND hwnd)
{
    if(hToolbar != NULL)
    {
        CloseToolbar();
    }
    else
    { 
        // modeless dialog
        hToolbar = CreateDialog(hInst, MAKEINTRESOURCE(IDD_TOOLBAR_DLG),  hwnd,  (DLGPROC) ToolbarDlgProc );
        uiTimerToolbarID = ::SetTimer(hToolbar, 10001, 200, NULL); // timer for slider update 
        ShowWindow(hToolbar, SW_SHOW); 
    }
         
}
//-------------------------------------------------------------------------------------------------
void CloseToolbar()
{
    if(hToolbar != NULL)
    {
        ::KillTimer(hToolbar, uiTimerToolbarID);
        uiTimerToolbarID = 0;
        DestroyWindow(hToolbar);
        hToolbar = NULL;
    }
}
//-------------------------------------------------------------------------------------------------
void                StreamOpen(HWND hWnd)
{
    if(DialogBox(hInst, MAKEINTRESOURCE(IDD_OPEN_STREAM), hWnd, OpenStreamProc) == IDOK)
    {
        s_pPipeline->Stop();
        if(s_pPipeline->Init(hClientWindow) == AMF_OK)
        {
//            s_pPipeline->SetParam(PLAYBACK360_URL_PARAM, L"");
            s_pPipeline->Play();
            UpdateMenuItems(::GetMenu(hWnd));
        }
        else
        {
            s_pPipeline->SetParam(PlaybackPipelineBase::PARAM_NAME_URL_VIDEO, L"");
            s_pPipeline->SetParam(PlaybackPipelineBase::PARAM_NAME_URL_AUDIO, L"");
        }

    }

}
INT_PTR CALLBACK    OpenStreamProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        // update values
        {
            bool bListen = false;
            s_pPipeline->GetParam(PlaybackPipelineBase::PARAM_NAME_LISTEN_FOR_CONNECTION, bListen);
            CheckDlgButton(hDlg, IDC_CHECK_LISTEN, bListen ? BST_CHECKED : BST_UNCHECKED);

        }
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            wchar_t buf[10000];
            GetWindowText(GetDlgItem(hDlg, IDC_EDIT_URL_VIDEO), buf, amf_countof(buf));
            s_pPipeline->SetParamAsString(PlaybackPipelineBase::PARAM_NAME_URL_VIDEO, buf);
            GetWindowText(GetDlgItem(hDlg, IDC_EDIT_URL_AUDIO), buf, amf_countof(buf));
            s_pPipeline->SetParamAsString(PlaybackPipelineBase::PARAM_NAME_URL_AUDIO, buf);
            
            bool bListen = IsDlgButtonChecked(hDlg, IDC_CHECK_LISTEN) == BST_CHECKED;
            s_pPipeline->SetParam(PlaybackPipelineBase::PARAM_NAME_LISTEN_FOR_CONNECTION, bListen);

            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
