// Stitch.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "Stitch.h"
#include <objbase.h>
#include <Commdlg.h>
#include <Windowsx.h>
#include <CommCtrl.h>

#include "public/include/components/VideoStitch.h"

#include "public/include/core/Debug.h"

#include "StitchPreviewPipeline.h"
#include "public/samples/CPPSamples/common/CmdLineParser.h"
#include "public/samples/CPPSamples/common/CmdLogger.h"
#include "public/samples/CPPSamples/common/PlaybackPipeline.h"
#include "public/include/components/ZCamLiveStream.h"

#define MAX_LOADSTRING 100

static AMF_VIDEO_STITCH_OUTPUT_MODE_ENUM outputMode = AMF_VIDEO_STITCH_OUTPUT_MODE_PREVIEW;

void EnableAMFProfiling(bool bEnable);

// Global Variables:
HINSTANCE hInst;                                // current instance
TCHAR szTitle[MAX_LOADSTRING];                    // The title bar text
TCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

class Playback
{
public:
    virtual ~Playback(){}
    virtual void                Play() = 0;
    virtual void                Pause() = 0;
    virtual void                Step() = 0;
    virtual void                Stop() = 0;
    virtual void                Terminate() = 0;
    virtual bool                IsPlaying() = 0;
    virtual void                ReorderClientWindows() = 0;
    virtual void                UpdateCaption() = 0;
    virtual void                CheckForRestart() = 0;
    virtual bool                ParseCmdLineParameters() = 0;
    static amf_size m_iWindowCount;
    static amf_int32 m_iWindowsInRow;

};
amf_size Playback::m_iWindowCount = 4;
amf_int32 Playback::m_iWindowsInRow = 2;


class SingleWindowPlayback : public Playback
{
public:
    SingleWindowPlayback() : m_hClientWindow(NULL){}
    virtual ~SingleWindowPlayback(){}
    virtual void                Play();
    virtual void                Pause();
    virtual void                Step();
    virtual void                Stop();
    virtual void                Terminate();
    virtual bool                IsPlaying();
    virtual void                ReorderClientWindows();
    virtual void                UpdateCaption();
    virtual void                CheckForRestart();
    virtual bool                ParseCmdLineParameters();

    StitchPreviewPipeline        m_Pipeline;
protected:
    HWND                        m_hClientWindow;
};

static SingleWindowPlayback *g_pPipeline;

std::vector<std::wstring>           InputFileList;
std::wstring                        PTGuiProject;
static HWND hWnd = NULL;
static  UINT_PTR uiTimerID = 0;

static  amf::AMF_MEMORY_TYPE memoryType = amf::AMF_MEMORY_DX11;
static  bool bLoop = true;
static  bool bStop = false;
static  bool bForce60FPS = true;
static bool bLowLatency = false;
static bool bFullSpeed = false;
static amf_int64 iZCamMode = CAMLIVE_MODE_INVALID;

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass();
BOOL                InitInstance(int);
HWND                CreateClientWindow(HWND hWndParent);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK    ClientWndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
void                FileOpen();
void                FileOpenPTGuiProject();
void                UpdateMenuItems(HMENU hMenu);
void                HandleKeyboard(WPARAM wParam);

static INT_PTR CALLBACK ProgressDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM /* lParam */)
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
//            pContext->InitOpenCL(NULL);
            amf::AMFComponentPtr pComponent;
            g_AMFFactory.GetFactory()->CreateComponent(pContext, AMFVideoConverter, &pComponent);
            pComponent->Optimize(this);
            g_AMFFactory.LoadExternalComponent(pContext, STITCH_DLL_NAME, "AMFCreateComponentInt", NULL, &pComponent);
            pComponent->Optimize(this);
            pComponent.Release();
            g_AMFFactory.UnLoadExternalComponent(STITCH_DLL_NAME);
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

    hInst = hInstance;
    ::CoInitializeEx(NULL,COINIT_MULTITHREADED);

    AMF_RESULT res = AMF_OK;

    // initialize AMF
    res = g_AMFFactory.Init();
    CHECK_AMF_ERROR_RETURN(res, L"AMF Factory Failed to initialize");

    g_AMFFactory.GetDebug()->AssertsEnable(true);

//    EnableAMFProfiling(true);

    MSG msg;
    HACCEL hAccelTable;

    // Initialize global strings
    LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadString(hInstance, IDC_STITCH, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass();

    g_pPipeline = new SingleWindowPlayback();

    // Perform application initialization:
    if (!InitInstance (nCmdShow))
    {
        CoUninitialize();
        g_AMFFactory.Terminate();
        return FALSE;
    }

    PostMessage(hWnd, WM_USER+1000, 0, 0);
    //------------------------------------------------------------------------------------------------------------

    g_pPipeline->ParseCmdLineParameters();
/*
        if(g_pPipeline.Init(hWnd, hClientWnd) == AMF_OK)
        {
            g_pPipeline.Play();
            UpdateMenuItems(::GetMenu(hWnd));
        }
*/
    //------------------------------------------------------------------------------------------------------------


    hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_STITCH));

    // Main message loop:
    while (GetMessage(&msg, NULL, 0, 0))
    {
//        if(g_PropertyEditor.IsDialogMessage(&msg))
//        {
//            continue;
//        }

        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    CoUninitialize();

    delete g_pPipeline;

    g_AMFFactory.Terminate();
    return (int)msg.wParam;
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
ATOM MyRegisterClass()
{
    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style            = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra        = 0;
    wcex.cbWndExtra        = 0;
    wcex.hInstance        = hInst;
    wcex.hIcon            = LoadIcon(hInst, MAKEINTRESOURCE(IDI_STITCH));
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground    = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName    = MAKEINTRESOURCE(IDC_STITCH);
    wcex.lpszClassName    = szWindowClass;
    wcex.hIconSm        = LoadIcon(hInst, MAKEINTRESOURCE(IDI_SMALL));

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
BOOL InitInstance(int /* nCmdShow */)
{
    hWnd = NULL;

    hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW  ,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInst, NULL);

    if (!hWnd)
    {
        return FALSE;
    }

    uiTimerID = ::SetTimer(hWnd, 10000, 1000, NULL);

    ShowWindow(hWnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hWnd);
    UpdateMenuItems(::GetMenu(hWnd));

    return TRUE;
}
HWND CreateClientWindow(HWND hWndParent)
{
    WNDCLASSEX wcex;

    static const wchar_t *ChildClassName = L"Client";
    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style            = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = ClientWndProc;
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

    if (reg == 0)
    {
        return nullptr;
    }

    HWND hClientWnd = CreateWindowEx( 0, ChildClassName, NULL, WS_CHILD | WS_VISIBLE,
        0, 0, 100, 100,
        hWndParent, (HMENU) (int) (1) , hInst, NULL);
    return hClientWnd;
}
void UpdateMenuItems(HMENU hMenu)
{
/*
    amf::AMF_MEMORY_TYPE    presenterType = amf::AMF_MEMORY_DX9;
    {
        amf_int64 engineInt = amf::AMF_MEMORY_UNKNOWN;
        if(s_Pipeline.GetParam(StitchPipeline::PARAM_NAME_PRESENTER, engineInt) == AMF_OK)
        {
            if(amf::AMF_MEMORY_UNKNOWN != engineInt)
            {
                presenterType = (amf::AMF_MEMORY_TYPE)engineInt;
            }
        }
        else
        {
            s_Pipeline.SetParam(StitchPipeline::PARAM_NAME_PRESENTER, presenterType);
        }
    }
*/
    CheckMenuItem(hMenu,ID_OPTIONS_RENDERER_DX11,      MF_BYCOMMAND| ( memoryType == amf::AMF_MEMORY_DX11 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu,ID_OPTIONS_COMPUTE_FOR_DX11,      MF_BYCOMMAND| ( memoryType == amf::AMF_MEMORY_COMPUTE_FOR_DX11 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu,ID_OPTIONS_RENDERER_OPENCL,      MF_BYCOMMAND| ( memoryType == amf::AMF_MEMORY_OPENCL ? MF_CHECKED : MF_UNCHECKED));

    CheckMenuItem(hMenu,ID_OPTIONS_LOOP,    MF_BYCOMMAND| ( bLoop ? MF_CHECKED: MF_UNCHECKED));
    CheckMenuItem(hMenu,ID_OPTIONS_FORCE60FPS,    MF_BYCOMMAND| ( bForce60FPS ? MF_CHECKED: MF_UNCHECKED));

    CheckMenuItem(hMenu,ID_OPTIONS_FULLSPEED,    MF_BYCOMMAND| ( bFullSpeed ? MF_CHECKED: MF_UNCHECKED));

    CheckMenuItem(hMenu,ID_OPTIONS_LOW_LATENCY,    MF_BYCOMMAND| ( bLowLatency ? MF_CHECKED: MF_UNCHECKED));
    CheckMenuItem(hMenu, ID_FILE_ZCAMLIVE, MF_BYCOMMAND | (iZCamMode == CAMLIVE_MODE_ZCAM_1080P30 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, ID_FILE_ZCAMLIVE_2K7, MF_BYCOMMAND | (iZCamMode == CAMLIVE_MODE_ZCAM_2K7P30 ? MF_CHECKED : MF_UNCHECKED));

    CheckMenuItem(hMenu, ID_FILE_RICOH_THETA_S, MF_BYCOMMAND | (iZCamMode == CAMLIVE_MODE_THETAS ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, ID_FILE_RICOH_THETA_V, MF_BYCOMMAND | (iZCamMode == CAMLIVE_MODE_THETAV ? MF_CHECKED : MF_UNCHECKED));

    amf_int32 index = g_pPipeline->m_Pipeline.GetSubresourceIndex();
    CheckMenuItem(hMenu,ID_SUBRESOURCE_INDEX0,    MF_BYCOMMAND| ( index == 0 ? MF_CHECKED: MF_UNCHECKED));
    CheckMenuItem(hMenu,ID_SUBRESOURCE_INDEX1,    MF_BYCOMMAND| ( index == 1 ? MF_CHECKED: MF_UNCHECKED));
    CheckMenuItem(hMenu,ID_SUBRESOURCE_INDEX2,    MF_BYCOMMAND| ( index == 2 ? MF_CHECKED: MF_UNCHECKED));
    CheckMenuItem(hMenu,ID_SUBRESOURCE_INDEX3,    MF_BYCOMMAND| ( index == 3 ? MF_CHECKED: MF_UNCHECKED));
    CheckMenuItem(hMenu,ID_SUBRESOURCE_INDEX4,    MF_BYCOMMAND| ( index == 4 ? MF_CHECKED: MF_UNCHECKED));
    CheckMenuItem(hMenu,ID_SUBRESOURCE_INDEX5,    MF_BYCOMMAND| ( index == 5 ? MF_CHECKED: MF_UNCHECKED));

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

static POINT s_MousePos;
static bool     s_MouseDown = false;

LRESULT CALLBACK WndProc(HWND hWindow, UINT message, WPARAM wParam, LPARAM lParam)
{
    int wmId, wmEvent;
    PAINTSTRUCT ps;
    HDC hdc;

    switch (message)
    {
    case WM_USER+1000:
        if(DialogBox( hInst, MAKEINTRESOURCE(IDD_PROGRESS_DLG), hWindow,  (DLGPROC) ProgressDlgProc ) != IDOK)
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
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWindow, About);
            break;
        case IDM_EXIT:
            DestroyWindow(hWindow);
            break;
        case ID_FILE_OPEN:
            FileOpen();
            break;
        case ID_FILE_CLEARFILELIST:
            InputFileList.clear();
            break;
        case ID_FILE_OPENPTGUIPROJECT:
            FileOpenPTGuiProject();
            break;
        case ID_PLAYBACK_PLAY:
            g_pPipeline->Play();
            UpdateMenuItems(::GetMenu(hWindow));
            break;
        case ID_PLAYBACK_PAUSE:
            g_pPipeline->Pause();
            break;
        case ID_PLAYBACK_STEP:
            g_pPipeline->Step();
            break;
        case ID_PLAYBACK_STOP:
            g_pPipeline->Stop();
            break;

        case ID_OPTIONS_RENDERER_DX11:
            memoryType = amf::AMF_MEMORY_DX11;
            UpdateMenuItems(::GetMenu(hWindow));
            break;
        case ID_OPTIONS_COMPUTE_FOR_DX11:
            memoryType = amf::AMF_MEMORY_COMPUTE_FOR_DX11;
            UpdateMenuItems(::GetMenu(hWindow));
            break;
        case ID_OPTIONS_RENDERER_OPENCL:
            memoryType = amf::AMF_MEMORY_OPENCL;
            UpdateMenuItems(::GetMenu(hWindow));
            break;
        case ID_OPTIONS_LOOP:
            bLoop = !bLoop;
            UpdateMenuItems(::GetMenu(hWindow));
            break;
        case ID_OPTIONS_FORCE60FPS:
            bForce60FPS = !bForce60FPS;
            UpdateMenuItems(::GetMenu(hWindow));
            break;
        case ID_OPTIONS_FULLSPEED:
            bFullSpeed = !bFullSpeed;
            g_pPipeline->m_Pipeline.SetFullSpeed(bFullSpeed);
            UpdateMenuItems(::GetMenu(hWindow));
            break;
        case ID_OPTIONS_LOW_LATENCY:
            bLowLatency = !bLowLatency;
            UpdateMenuItems(::GetMenu(hWindow));
            break;
        case ID_FILE_ZCAMLIVE:
            iZCamMode = (iZCamMode == CAMLIVE_MODE_ZCAM_1080P30) ? CAMLIVE_MODE_INVALID : CAMLIVE_MODE_ZCAM_1080P30;
            UpdateMenuItems(::GetMenu(hWindow));
            break;
        case ID_FILE_ZCAMLIVE_2K7:
            iZCamMode = (iZCamMode == CAMLIVE_MODE_ZCAM_2K7P30) ? CAMLIVE_MODE_INVALID : CAMLIVE_MODE_ZCAM_2K7P30;
            UpdateMenuItems(::GetMenu(hWindow));
            break;
        case ID_FILE_RICOH_THETA_S:
            iZCamMode = (iZCamMode == CAMLIVE_MODE_THETAS) ? CAMLIVE_MODE_INVALID : CAMLIVE_MODE_THETAS;
            UpdateMenuItems(::GetMenu(hWindow));
            break;
        case ID_FILE_RICOH_THETA_V:
            iZCamMode = (iZCamMode == CAMLIVE_MODE_THETAV) ? CAMLIVE_MODE_INVALID : CAMLIVE_MODE_THETAV;
            UpdateMenuItems(::GetMenu(hWindow));
            break;
        case ID_OPTIONS_DUMP_TEMPLATE:
            g_pPipeline->m_Pipeline.Dump();
            break;
        case ID_SUBRESOURCE_INDEX0:
            g_pPipeline->m_Pipeline.SetSubresourceIndex(0);
            UpdateMenuItems(::GetMenu(hWindow));
            break;
        case ID_SUBRESOURCE_INDEX1:
            g_pPipeline->m_Pipeline.SetSubresourceIndex(1);
            UpdateMenuItems(::GetMenu(hWindow));
            break;
        case ID_SUBRESOURCE_INDEX2:
            g_pPipeline->m_Pipeline.SetSubresourceIndex(2);
            UpdateMenuItems(::GetMenu(hWindow));
            break;
        case ID_SUBRESOURCE_INDEX3:
            g_pPipeline->m_Pipeline.SetSubresourceIndex(3);
            UpdateMenuItems(::GetMenu(hWindow));
            break;
        case ID_SUBRESOURCE_INDEX4:
            g_pPipeline->m_Pipeline.SetSubresourceIndex(4);
            UpdateMenuItems(::GetMenu(hWindow));
            break;
        case ID_SUBRESOURCE_INDEX5:
            g_pPipeline->m_Pipeline.SetSubresourceIndex(5);
            UpdateMenuItems(::GetMenu(hWindow));
            break;
        default:
            return DefWindowProc(hWindow, message, wParam, lParam);
        }
        break;
    case WM_PAINT:
        hdc = BeginPaint(hWindow, &ps);
        // TODO: Add any drawing code here...
        EndPaint(hWindow, &ps);
        break;
    case WM_CREATE:
        break;
    case WM_DESTROY:
        g_pPipeline->Terminate();
        PostQuitMessage(0);
        break;
    case WM_SIZE:
        g_pPipeline->ReorderClientWindows();
        break;
    case WM_TIMER:
        g_pPipeline->UpdateCaption();
        g_pPipeline->CheckForRestart();
        break;
    case WM_KEYDOWN: // to get arrows
//    case WM_CHAR: // to get []{}
        HandleKeyboard(wParam);
        break;

    default:
        return DefWindowProc(hWindow, message, wParam, lParam);
    }
    return 0;
}

LRESULT CALLBACK ClientWndProc(HWND hWindow, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_LBUTTONDOWN:
        s_MousePos.x = GET_X_LPARAM(lParam);
        s_MousePos.y = GET_Y_LPARAM(lParam);
        s_MouseDown = true;
        break;
    case WM_MOUSEMOVE:
        if(wParam & MK_LBUTTON)
        {
            if(s_MouseDown)
            {
                POINT newPos = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                g_pPipeline->m_Pipeline.MouseShift(s_MousePos.x, s_MousePos.y, newPos.x, newPos.y);
                s_MousePos = newPos;
            }
        }
        else
        {
            s_MouseDown = false;
        }
        break;
    case WM_LBUTTONUP:
        s_MouseDown = false;
        break;
    case WM_KEYDOWN: // to get arrows
//    case WM_CHAR: // to get []{}
        HandleKeyboard(wParam);
        break;
    default:
        return DefWindowProc(hWindow, message, wParam, lParam);
    }
    return 0;
}

void FileOpen()
{
    OPENFILENAME ofn;       // common dialog box structure
    WCHAR szFile[260];       // buffer for file name

    // Initialize OPENFILENAME
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = szFile;
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = _countof(szFile);
//    ofn.lpstrFilter = L"Videos\0*.WMV;*.WMA;*.AVI;*.ASF;*.FLV;*.BFI;*.CAF;*.GXF;*.IFF;*.RL2;*.MP4;*.3GP;*.QTFF;*.MKV;*.MK3D;*.MKA;*.MKS;*.MPG;*.MPEG;*.PS;*.TS;*.MXF;*.OGV;*.OGA;*.OGX;*.OGG;*.SPX;*.FLV;*.F4V;*.F4P;*.F4A;*.F4B;*.JSV;*.h264;*.264;*.vc1;*.mov;*.mvc;*.m1v;*.m2v;*.m2ts;*.vpk;*.yuv;*.rgb;*.nv12;*.h265;*.265\0All\0*.*\0";
    ofn.lpstrFilter = L"Video streams\0*.mp4;*.mkv;*.flv;*.mov;*.h264;*.264;*.h265;*.265;*.hevc;\0All\0*.*\0";
//    ofn.lpstrFilter = L"Video streams\0*.h264;*.264;*.h265;*.265;*.hevc;\0All\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

    if (GetOpenFileName(&ofn)==TRUE)
    {
        if(ofn.nFileExtension != 0 )
        {
            InputFileList.push_back(ofn.lpstrFile);
        }
        else
        {
            std::wstring path(ofn.lpstrFile);
            path += L"\\";

            LPWSTR p = ofn.lpstrFile;
            size_t size = wcslen(p);
            p += size+1;

            while(true)
            {
                size = wcslen(p);
                if(size == 0 )
                {
                    break;
                }
                std::wstring full_name = path + p;
                p += size + 1;
                InputFileList.push_back(full_name);
            }
        }

//        InputFileList.push_back(ofn.lpstrFile);
/*
        s_Pipeline.SetParam(StitchPipeline::PARAM_NAME_INPUT, ofn.lpstrFile);
        s_Pipeline.Stop();
        if(s_Pipeline.Init(hWnd, hClientWnd) == AMF_OK)
        {
            s_Pipeline.Play();
            UpdateMenuItems(::GetMenu(hWnd));
        }
*/
        iZCamMode = CAMLIVE_MODE_INVALID;
        UpdateMenuItems(::GetMenu(hWnd));
    }
}

void                FileOpenPTGuiProject()
{
    OPENFILENAME ofn;       // common dialog box structure
    WCHAR szFile[260];       // buffer for file name

    // Initialize OPENFILENAME
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = szFile;
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = _countof(szFile);
//    ofn.lpstrFilter = L"PanoKolor\0*.pano;\0PTGui\0*.pts;\0All\0*.*\0";
    ofn.lpstrFilter = L"PTGui\0*.pts;\0PanoKolor\0*.pano;\0All\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;

    if (GetOpenFileName(&ofn)==TRUE)
    {
        if(ofn.nFileExtension != 0 )
        {
            PTGuiProject = ofn.lpstrFile;
        }
    }

    if (PTGuiProject.length() > 0)
    {
        std::wstring::size_type pos = PTGuiProject.find_last_of(L".");
        std::wstring ext = PTGuiProject.substr(pos + 1);
        std::wstring result;
        std::transform(ext.begin(), ext.end(), std::back_inserter(result), toUpperWchar);
        if (result == L"PTS")
        {
            g_pPipeline->m_Pipeline.InitCamera(PTGuiProject);
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


bool                SingleWindowPlayback::ParseCmdLineParameters()
{
    return parseCmdLineParameters(&m_Pipeline);
}

void                SingleWindowPlayback::Play()
{
    if (InputFileList.size() == 0 && (iZCamMode == CAMLIVE_MODE_INVALID))
    {
        return;
    }
    bStop = false;
    if(m_hClientWindow == NULL)
    {
        m_hClientWindow = CreateClientWindow(hWnd);
    }
    ReorderClientWindows();
    std::wstring filename = L"";
    if (iZCamMode == CAMLIVE_MODE_INVALID)
    {
        filename = InputFileList.front();
    }

    if(m_Pipeline.GetState() == PipelineStateEof || m_Pipeline.GetState() == PipelineStateNotReady)
    {
        m_Pipeline.SetParam(StitchPipeline::PARAM_NAME_FRAMERATE, bForce60FPS ? 60. : 0.);
        m_Pipeline.SetParam(StitchPipeline::PARAM_NAME_PRESENTER, memoryType);
        m_Pipeline.SetParam(StitchPipeline::PARAM_NAME_INPUT, filename.c_str());
        m_Pipeline.SetParam(StitchPipeline::PARAM_NAME_LOWLATENCY, bLowLatency);
        m_Pipeline.SetParam(StitchPipeline::PARAM_NAME_ZCAMLIVE_MODE, iZCamMode);


        m_Pipeline.Stop();
        m_Pipeline.Init(m_hClientWindow, InputFileList, PTGuiProject, outputMode);
    }
    m_Pipeline.Play();
    m_Pipeline.SetFullSpeed(bFullSpeed);
}
void                SingleWindowPlayback::Pause()
{
    m_Pipeline.Pause();
}
void                SingleWindowPlayback::Step()
{
    m_Pipeline.Step();
}
void                SingleWindowPlayback::Stop()
{
    bStop = true;
    m_Pipeline.Stop();
}
void                SingleWindowPlayback::Terminate()
{
    m_Pipeline.Terminate();
}
bool                SingleWindowPlayback::IsPlaying()
{
    bool bRet = true;
    if(m_Pipeline.GetState() == PipelineStateEof || m_Pipeline.GetState() == PipelineStateNotReady)
    {
        bRet = false;
    }
    return bRet;
}
void                SingleWindowPlayback::ReorderClientWindows()
{
    if(hWnd != NULL && m_hClientWindow!=NULL)
    {
        RECT client;
        ::GetClientRect(hWnd, &client);
        ::MoveWindow(m_hClientWindow, client.left, client.top, client.right - client.left, client.bottom - client.top, TRUE);
    }
}
void                SingleWindowPlayback::UpdateCaption()
{
    wchar_t text[1000];
    double FPS = m_Pipeline.GetFPS();
    swprintf(text,L"%s | FPS( %.1f)",szTitle, FPS);
    ::SetWindowText(hWnd, text);
}
void                SingleWindowPlayback::CheckForRestart()
{
    if(bLoop && !bStop)
    {
        if(m_Pipeline.GetState() == PipelineStateEof || m_Pipeline.GetState() == PipelineStateNotReady)
        {
            m_Pipeline.Restart();
            /*
            m_Pipeline.SetParam(StitchPipeline::PARAM_NAME_FRAMERATE, bForce60FPS ? 60. : 0.);
            m_Pipeline.SetParam(StitchPipeline::PARAM_NAME_PRESENTER, memoryType);
            m_Pipeline.SetParam(StitchPipeline::PARAM_NAME_LOWLATENCY, bLowLatency);
            m_Pipeline.Init(m_hClientWindow, InputFileList);
            m_Pipeline.Play();
            */
        }
    }
}

void              HandleKeyboard(WPARAM wParam)
{
    bool shiftDown = (GetKeyState ( VK_SHIFT )  & (1<<16)) != 0;
    switch(wParam)
    {
    case VK_UP:
        g_pPipeline->m_Pipeline.ChangeOffsetY(true, shiftDown);
        break;
    case VK_DOWN:
        g_pPipeline->m_Pipeline.ChangeOffsetY(false, shiftDown);
        break;
    case VK_LEFT:
        g_pPipeline->m_Pipeline.ChangeOffsetX(false, shiftDown);
        break;
    case VK_RIGHT:
        g_pPipeline->m_Pipeline.ChangeOffsetX(true, shiftDown);
        break;
    case VK_END:
        g_pPipeline->m_Pipeline.ChangeK2(false, shiftDown);
        break;
    case VK_HOME:
        g_pPipeline->m_Pipeline.ChangeK2(true, shiftDown);
        break;
    case VK_PRIOR:
        g_pPipeline->m_Pipeline.ChangeK1(true, shiftDown);
        break;
    case VK_NEXT:
        g_pPipeline->m_Pipeline.ChangeK1(false, shiftDown);
        break;
    case VK_INSERT:
        g_pPipeline->m_Pipeline.ChangeK3(true, shiftDown);
        break;
    case VK_DELETE:
        g_pPipeline->m_Pipeline.ChangeK3(false, shiftDown);
        break;
    case L'A':
    case L'a':
        g_pPipeline->m_Pipeline.ChangeZoom(true, shiftDown);
        break;
    case L'Z':
    case L'z':
        g_pPipeline->m_Pipeline.ChangeZoom(false, shiftDown);
        break;
    case L'0':
    case L'1':
    case L'2':
    case L'3':
    case L'4':
    case L'5':
    case L'6':
        g_pPipeline->m_Pipeline.SetChannel((int)(wParam - L'0'));
        break;
    case L'w':
    case L'W':
        g_pPipeline->m_Pipeline.ToggleWire();
        break;
    case L'b':
    case L'B':
        g_pPipeline->m_Pipeline.ToggleColorBalance();
        break;
    case L'i':
    case L'I':
        g_pPipeline->m_Pipeline.ChangeCameraPitch(true, shiftDown);
        break;
    case L'm':
    case L'M':
        g_pPipeline->m_Pipeline.ChangeCameraPitch(false, shiftDown);
        break;
    case L'l':
    case L'L':
        g_pPipeline->m_Pipeline.ChangeCameraYaw(true, shiftDown);
        break;
    case L'j':
    case L'J':
        g_pPipeline->m_Pipeline.ChangeCameraYaw(false, shiftDown);
        break;
    case L'o':
    case L'O':
        g_pPipeline->m_Pipeline.ChangeCameraRoll(false, shiftDown);
        break;
    case L'p':
    case L'P':
        g_pPipeline->m_Pipeline.ChangeCameraRoll(true, shiftDown);
        break;
    case L'q':
    case L'Q':
        g_pPipeline->m_Pipeline.ToggleOutputMode();
        break;
    case L'e':
        break;
    }
}



