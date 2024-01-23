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
// Stitch.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include <Commdlg.h>
#include <Windowsx.h>
#include <CommCtrl.h>
#include <fcntl.h>
#include <io.h>

#include "CaptureVideo.h"
#include "public/samples/CPPSamples/common/CmdLineParser.h"
#include "public/samples/CPPSamples/common/Options.h"
#include "public/include/components/ChromaKey.h"

CaptureVideo* gCaptureVideo(NULL);

//------------------------------------------------------------------------------------------------------------
int APIENTRY _tWinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    ::CoInitializeEx(NULL,COINIT_MULTITHREADED);

    // initialize AMF
     AMF_RESULT res = g_AMFFactory.Init();
    CHECK_AMF_ERROR_RETURN(res, L"AMF Factory Failed to initialize");

    g_AMFFactory.GetDebug()->AssertsEnable(true);
    g_AMFFactory.GetTrace()->SetGlobalLevel(AMF_TRACE_WARNING);
    g_AMFFactory.GetTrace()->EnableWriter(AMF_TRACE_WRITER_DEBUG_OUTPUT, true);
    g_AMFFactory.GetTrace()->SetWriterLevel(AMF_TRACE_WRITER_DEBUG_OUTPUT, AMF_TRACE_WARNING);

    g_AMFFactory.GetTrace()->EnableWriter(AMF_TRACE_WRITER_FILE, true);
//    g_AMFFactory.GetTrace()->SetWriterLevel(AMF_TRACE_WRITER_FILE, AMF_TRACE_INFO);
    g_AMFFactory.GetTrace()->SetWriterLevel(AMF_TRACE_WRITER_FILE, AMF_TRACE_WARNING);

//    EnableAMFProfiling(true);
    int ret = 0;
    gCaptureVideo = new CaptureVideo(hInstance);

    if (gCaptureVideo)
    {
        ret = gCaptureVideo->Exec(nCmdShow);
    }

    g_AMFFactory.Terminate();
    return ret;
}

//------------------------------------------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return gCaptureVideo->WndProc(hWnd, message, wParam, lParam);
}

//------------------------------------------------------------------------------------------------------------
LRESULT CALLBACK ClientWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return gCaptureVideo->ClientWndProc(hWnd, message, wParam, lParam);
}

//------------------------------------------------------------------------------------------------------------
INT_PTR CALLBACK ProgressDlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return gCaptureVideo->ProgressDlgProc(hWnd, message, wParam, lParam);
}

//------------------------------------------------------------------------------------------------------------
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    return gCaptureVideo->About(hDlg, message, wParam, lParam);
}

//------------------------------------------------------------------------------------------------------------
CaptureVideo::CaptureVideo(HINSTANCE hInst) :
m_hInst(hInst),
m_hWnd(NULL),
m_timerID(0),
m_mouseDown(false)
{
    ::memset(m_title, 0, sizeof(m_title));
    ::memset(m_windowClass, 0, sizeof(m_windowClass));
    m_mousePos.x = 0;
    m_mousePos.y = 0;
}

//------------------------------------------------------------------------------------------------------------
CaptureVideo::~CaptureVideo()
{
}

//------------------------------------------------------------------------------------------------------------
int CaptureVideo::Exec(int nCmdShow)
{
    MSG msg;
    HACCEL hAccelTable;

    // Initialize global strings
    LoadString(m_hInst, IDS_APP_TITLE, m_title, MaxLoadString);
    LoadString(m_hInst, IDC_CAPTUREVIDEO, m_windowClass, MaxLoadString);
    MyRegisterClass();

    // Perform application initialization:
    if (!InitInstance(nCmdShow))
    {
        CoUninitialize();
        return FALSE;
    }

    m_hClientWindow = CreateClientWindow(m_hWnd);
    ReorderClientWindows();

    PostMessage(m_hWnd, WMUserMsgClose, 0, 0);

    ParseCmdLineParameters();

    hAccelTable = LoadAccelerators(m_hInst, MAKEINTRESOURCE(IDC_CAPTUREVIDEO));

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
    return (int)msg.wParam;
}

//------------------------------------------------------------------------------------------------------------
ATOM CaptureVideo::MyRegisterClass()
{
    WNDCLASSEX wcex;

    wcex.cbSize        = sizeof(WNDCLASSEX);
    wcex.style         = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc   = ::WndProc;
    wcex.cbClsExtra    = 0;
    wcex.cbWndExtra    = 0;
    wcex.hInstance     = m_hInst;
    wcex.hIcon         = LoadIcon(m_hInst, MAKEINTRESOURCE(IDI_CAPTUREVIDEO));
    wcex.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName  = MAKEINTRESOURCE(IDC_CAPTUREVIDEO);
    wcex.lpszClassName = m_windowClass;
    wcex.hIconSm       = LoadIcon(m_hInst, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassEx(&wcex);
}

//------------------------------------------------------------------------------------------------------------
BOOL CaptureVideo::InitInstance(int /* nCmdShow */)
{
    m_hWnd = ::CreateWindow(m_windowClass, m_title, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, m_hInst, NULL);

    if (!m_hWnd)
    {
        return FALSE;
    }

    m_timerID = ::SetTimer(m_hWnd, 10000, 1000, NULL);
    m_Pipeline.InitContext(amf::AMF_MEMORY_DX11);

    LoadFromOptions();

    ShowWindow(m_hWnd, SW_SHOWMAXIMIZED);
    UpdateWindow(m_hWnd);
    UpdateMenuItems(::GetMenu(m_hWnd));
    return TRUE;
}

//------------------------------------------------------------------------------------------------------------
HWND CaptureVideo::CreateClientWindow(HWND hWndParent)
{
    WNDCLASSEX wcex;
    static const wchar_t *ChildClassName = L"Client";
    wcex.cbSize        = sizeof(WNDCLASSEX);
    wcex.style         = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc   = ::ClientWndProc;
    wcex.cbClsExtra    = 0;
    wcex.cbWndExtra    = 0;
    wcex.hInstance     = m_hInst;
    wcex.hIcon         = 0;
    wcex.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName  = 0;
    wcex.lpszClassName = ChildClassName;
    wcex.hIconSm       = NULL;

    ATOM reg = RegisterClassEx(&wcex);

    if (reg == 0)
    {
        return nullptr;
    }

    HWND hClientWnd = CreateWindowEx( 0, ChildClassName, NULL, WS_CHILD | WS_VISIBLE, 0, 0, 100, 100,
        hWndParent, (HMENU)(int)(1), m_hInst, NULL);
    return hClientWnd;
}

//------------------------------------------------------------------------------------------------------------
void CaptureVideo::UpdateMenuItems(HMENU hMenu)
{
    CheckMenuItem(hMenu, ID_OPTIONS_LOOP, MF_BYCOMMAND | (m_bLoop ? MF_CHECKED : MF_UNCHECKED));

    for (amf_uint32 idx = 0; idx < 10; idx++)
    {
        CheckMenuItem(hMenu, ID_VIDEO_CAPTURE0+idx, MF_BYCOMMAND | MF_UNCHECKED);
    }

    if ((m_iSelectedDevice >= 0) && (m_eModeVideoSource == VIDEO_SOURCE_MODE_CAPTURE))
    {
        CheckMenuItem(hMenu, ID_VIDEO_CAPTURE0 + m_iSelectedDevice, MF_BYCOMMAND | MF_CHECKED);
    }

    CheckMenuItem(hMenu, ID_OPTIONS_CHROMAKEY,          MF_BYCOMMAND | (m_bChromaKey             ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, ID_OPTIONS_CHROMAKEY_BK,       MF_BYCOMMAND | (m_bChromaKeyBK           ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, ID_OPTIONS_CHROMAKEY_SPILL,    MF_BYCOMMAND | (m_bChromaKeySpill        ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, ID_OPTIONS_CHROMAKEY_COLOR,    MF_BYCOMMAND | (m_iChromaKeyColorAdj ==1 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, ID_OPTIONS_CHROMAKEY_ADVANCED, MF_BYCOMMAND | (m_iChromaKeyColorAdj ==2 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, ID_OPTIONS_SCALING,            MF_BYCOMMAND | (m_bChromaKeyScaling      ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, ID_OPTIONS_CHROMAKEY_RGBAFP16, MF_BYCOMMAND | (m_bChromaKeyRGBAFP16     ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, ID_OPTIONS_CHROMAKEY_10BIT,    MF_BYCOMMAND | (m_bChromaKey10BitLive    ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, ID_OPTIONS_CHROMAKEY_ALPHA_SRC,MF_BYCOMMAND | (m_bChromaKeyAlphaFromSrc ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, ID_OPTIONS_DEBUG,              MF_BYCOMMAND | (m_bChromaKeyDebug        ? MF_CHECKED : MF_UNCHECKED));
}

//------------------------------------------------------------------------------------------------------------
LRESULT CaptureVideo::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int wmId, wmEvent;
    PAINTSTRUCT ps;
    HDC hdc;

    switch (message)
    {
    case WMUserMsgClose:
        if (DialogBox(m_hInst, MAKEINTRESOURCE(IDD_PROGRESS_DLG), hWnd, (DLGPROC) ::ProgressDlgProc) != IDOK)
        {
            PostQuitMessage(0);
        }
        break;
    case WM_INITMENU:
        InitMenu(hWnd);
        DefWindowProc(hWnd, message, wParam, lParam);
        break;
    case WM_COMMAND:
        wmId = LOWORD(wParam);
        wmEvent = HIWORD(wParam);
        // Parse the menu selections:
        switch (wmId)
        {
        case IDM_ABOUT:
            DialogBox(m_hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, ::About);
            break;
        case ID_FILE_EXIT:
            DestroyWindow(hWnd);
            break;
        case ID_FILE_OPEN:
            if (FileOpenMedia())
            {
                m_eModeVideoSource = VIDEO_SOURCE_MODE_FILE;
                UpdateMenuItems(::GetMenu(hWnd));
            }
            break;
        case ID_FILE_OPEN_BK:
            FileOpenMedia(true);
            break;
        case ID_PLAYBACK_PLAY:
            UpdateOptions();
            Play();
            UpdateMenuItems(::GetMenu(hWnd));
            break;
        case ID_PLAYBACK_PAUSE:
            Pause();
            break;
        case ID_PLAYBACK_STEP:
            Step();
            break;
        case ID_PLAYBACK_STOP:
            UpdateOptions();
            Stop();
            break;
        case ID_OPTIONS_LOOP:
            m_bLoop = !m_bLoop;
            UpdateMenuItems(::GetMenu(hWnd));
            break;
        case ID_OPTIONS_CHROMAKEY:
            m_bChromaKey = !m_bChromaKey;
            UpdateMenuItems(::GetMenu(hWnd));
            break;
        case ID_OPTIONS_CHROMAKEY_BK:
            m_bChromaKeyBK = !m_bChromaKeyBK;
            UpdateMenuItems(::GetMenu(hWnd));
            break;
        case ID_OPTIONS_SCALING:
            m_bChromaKeyScaling = !m_bChromaKeyScaling;
            UpdateMenuItems(::GetMenu(hWnd));
            break;
        case ID_OPTIONS_CHROMAKEY_RGBAFP16:
            m_bChromaKeyRGBAFP16 = !m_bChromaKeyRGBAFP16;
            UpdateMenuItems(::GetMenu(hWnd));
            break;
        case ID_OPTIONS_CHROMAKEY_10BIT:
            m_bChromaKey10BitLive = !m_bChromaKey10BitLive;
            UpdateMenuItems(::GetMenu(hWnd));
            break;
        case ID_OPTIONS_DEBUG:
            m_bChromaKeyDebug = !m_bChromaKeyDebug;
            UpdateMenuItems(::GetMenu(hWnd));
            break;
        case ID_OPTIONS_CHROMAKEY_SPILL:
            m_bChromaKeySpill = !m_bChromaKeySpill;
            UpdateMenuItems(::GetMenu(hWnd));
            break;
        case ID_OPTIONS_CHROMAKEY_COLOR:
            m_iChromaKeyColorAdj = m_iChromaKeyColorAdj ==1 ? 0 : 1;
            UpdateMenuItems(::GetMenu(hWnd));
            break;
        case ID_OPTIONS_CHROMAKEY_ADVANCED:
            m_iChromaKeyColorAdj = m_iChromaKeyColorAdj == 2 ? 0 : 2;
            UpdateMenuItems(::GetMenu(hWnd));
            break;
        case ID_FILE_RESETHISTORY:
             ResetOptions();
             break;
         case ID_VIDEO_CAPTURE0:
         case ID_VIDEO_CAPTURE1:
         case ID_VIDEO_CAPTURE2:
         case ID_VIDEO_CAPTURE3:
         case ID_VIDEO_CAPTURE4:
         case ID_VIDEO_CAPTURE5:
         case ID_VIDEO_CAPTURE6:
         case ID_VIDEO_CAPTURE7:
         case ID_VIDEO_CAPTURE8:
         case ID_VIDEO_CAPTURE9:
             m_iSelectedDevice = wmId - ID_VIDEO_CAPTURE0;
             m_eModeVideoSource = VIDEO_SOURCE_MODE_CAPTURE;
             UpdateMenuItems(::GetMenu(hWnd));
             //reset scaling and position
             m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_VIDEO_SOURCE_SCALE, 100);
             m_Pipeline.SetParam(AMF_CHROMAKEY_POSX, 0);
             m_Pipeline.SetParam(AMF_CHROMAKEY_POSY, 0);
             break;
         case ID_FILE_LOAD_OPTIONS:
             FileLoadOptions(true);
             break;
         case ID_FILE_SAVE_OPTIONS:
             FileLoadOptions(false);
             break;
         case ID_OPTIONS_CHROMAKEY_ALPHA_SRC:
             m_bChromaKeyAlphaFromSrc = !m_bChromaKeyAlphaFromSrc;
             UpdateMenuItems(::GetMenu(hWnd));
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
        break;
    case WM_DESTROY:
        UpdateOptions();
        Terminate();
        PostQuitMessage(0);
        break;
    case WM_SIZE:
        ReorderClientWindows();
        break;
    case WM_TIMER:
        UpdateCaption();
        CheckForRestart();
        break;
    case WM_KEYDOWN: // to get arrows
        HandleKeyboard(wParam);
        UpdateMenuItems(::GetMenu(hWnd));
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

//------------------------------------------------------------------------------------------------------------
void CaptureVideo::InitMenu(HWND hWnd)
{
    HMENU hFileMenu = GetSubMenu(::GetMenu(hWnd), 0);
    if (hFileMenu == NULL) return;

    HMENU hCaptureMenu = NULL;
    int iItems = GetMenuItemCount(hFileMenu);
    for (int i = 0; i < iItems; i++)
    {
        hCaptureMenu = GetSubMenu(hFileMenu, i);
        if (hCaptureMenu != NULL)
        {
            break;
        }
    }
    //Update video capture devices
    if (hCaptureMenu != NULL)
    {
        // clear it;
        iItems = GetMenuItemCount(hCaptureMenu);
        for (int i = 0; i < iItems; i++)
        {
            RemoveMenu(hCaptureMenu, 0, MF_BYPOSITION);
        }
        // fill it
        m_Pipeline.GetCaptureManager()->Update();
        iItems = m_Pipeline.GetCaptureManager()->GetDeviceCount();

        MENUITEMINFO menuitem = { sizeof(MENUITEMINFO) };
        menuitem.fMask = MIIM_TYPE | MIIM_DATA | MIIM_ID;

        int iMenuIndex = 0;
        for (int i = 0; i < iItems; i++)
        {
            amf::AMFCaptureDevicePtr device;
            m_Pipeline.GetCaptureManager()->GetDevice(i, &device);
            if (device != NULL)
            {
                menuitem.wID = iMenuIndex + ID_VIDEO_CAPTURE0;
                amf_wstring name;
                device->GetPropertyWString(AMF_CAPTURE_DEVICE_NAME, &name);
                menuitem.dwTypeData = (LPWSTR)name.c_str();
                InsertMenuItem(hCaptureMenu, iMenuIndex, TRUE, &menuitem);
                SetMenuItemInfo(::GetMenu(hWnd), menuitem.wID, MF_BYCOMMAND, &menuitem);
                iMenuIndex++;
            }

        }
    }
    UpdateMenuItems(::GetMenu(hWnd));
}

//------------------------------------------------------------------------------------------------------------
LRESULT CaptureVideo::ClientWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_LBUTTONDOWN:
        m_mousePos.x = GET_X_LPARAM(lParam);
        m_mousePos.y = GET_Y_LPARAM(lParam);
        m_mouseDown = true;
        break;
    case WM_MOUSEMOVE:
        if(wParam & MK_LBUTTON)
        {
            if(m_mouseDown)
            {
                POINT newPos = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                m_mousePos = newPos;
            }
        }
        else
        {
            m_mouseDown = false;
        }
        break;
    case WM_LBUTTONUP:
        m_mouseDown = false;
        {
            RECT clientRect;
            ::GetClientRect(m_hWnd, &clientRect);
            m_Pipeline.SelectColorFromPosition(AMFConstructPoint(m_mousePos.x, m_mousePos.y),
                    AMFConstructRect(clientRect.left, clientRect.top, clientRect.right, clientRect.bottom));
        }
        break;
    case WM_KEYDOWN: // to get arrows
        HandleKeyboard(wParam);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

//------------------------------------------------------------------------------------------------------------
bool CaptureVideo::FileOpenMedia(bool isBackground)
{
    OPENFILENAME ofn;         // common dialog box structure
    WCHAR szFile[_MAX_PATH];  // buffer for file name
    bool ret = false;

    // Initialize OPENFILENAME
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = m_hWnd;
    ofn.lpstrFile       = szFile;
    ofn.lpstrFile[0]    = '\0';
    ofn.nMaxFile        = _countof(szFile);
    ofn.lpstrFilter     = L"Video streams\0*.mp4;*.mkv;*.flv;*.mov;*.h264;*.264;*.h265;*.265;*.hevc;\0All\0*.*\0";
    ofn.nFilterIndex    = 1;
    ofn.lpstrFileTitle  = NULL;
    ofn.nMaxFileTitle   = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags           = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;

    if (GetOpenFileName(&ofn)==TRUE)
    {
        if(ofn.nFileExtension != 0 )
        {
            if (isBackground)
            {
                m_FileNameBackground = ofn.lpstrFile;
            }
            else
            {
                m_FileName = ofn.lpstrFile;
            }

            //reset scaling and position
            m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_VIDEO_SOURCE_SCALE, 100);
            m_Pipeline.SetParam(AMF_CHROMAKEY_POSX, 0);
            m_Pipeline.SetParam(AMF_CHROMAKEY_POSY, 0);
        }
        ret = true;
    }
    return ret;
}

//------------------------------------------------------------------------------------------------------------
void CaptureVideo::FileLoadOptions(bool bLoad)
{
    OPENFILENAME ofn;         // common dialog box structure
    WCHAR szFile[_MAX_PATH];  // buffer for file name

    // Initialize OPENFILENAME
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hWnd;
    ofn.lpstrFile = szFile;
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = _countof(szFile);
    ofn.lpstrFilter = L"Options\0*.ini\0All\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = bLoad ? OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER : OFN_OVERWRITEPROMPT | OFN_EXPLORER;

    if (GetOpenFileName(&ofn) == TRUE)
    {
        if (ofn.nFileExtension != 0)
        {
            if (bLoad)
            {
                LoadFromOptions(ofn.lpstrFile);
            }
            else
            {
                UpdateOptions(ofn.lpstrFile);
            }

        }
    }
}

//------------------------------------------------------------------------------------------------------------
INT_PTR CaptureVideo::About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
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

//------------------------------------------------------------------------------------------------------------
SingleWindowPlayback::SingleWindowPlayback() :
    m_hClientWindow(NULL),
    m_eMemoryType(amf::AMF_MEMORY_DX11),
    m_bLoop(true),
    m_bChromaKey(false),
    m_bChromaKeyBK(false),
    m_bChromaKeySpill(false),
    m_iChromaKeyColorAdj(0),
    m_bChromaKeyDebug(false),
    m_bChromaKeyScaling(false),
    m_bChromaKeyRGBAFP16(false),
    m_bChromaKey10BitLive(false),
    m_bChromaKeyAlphaFromSrc(false),
    m_bStop(false),
    m_eModeVideoSource(VIDEO_SOURCE_MODE_FILE),
    m_iSelectedDevice(-1)
{

}

//------------------------------------------------------------------------------------------------------------
bool SingleWindowPlayback::ParseCmdLineParameters()
{
    return parseCmdLineParameters(&m_Pipeline);
}

//------------------------------------------------------------------------------------------------------------
void SingleWindowPlayback::Play()
{
    m_bStop = false;
    Init(m_hClientWindow);
    m_Pipeline.Play();
}

//------------------------------------------------------------------------------------------------------------
void SingleWindowPlayback::Init(HWND hClientWindow)
{
    if ((m_FileName.size() == 0) && (m_eModeVideoSource == VIDEO_SOURCE_MODE_ENUM::VIDEO_SOURCE_MODE_FILE))
    {
        return;
    }

    if (m_Pipeline.GetState() == PipelineStateEof || m_Pipeline.GetState() == PipelineStateNotReady)
    {
        m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_LOOP,                m_bLoop);
        m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_VIDEO_SOURCE_MODE,   m_eModeVideoSource);
        m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY,           m_bChromaKey);
        m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_BK,        m_bChromaKeyBK);
        m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_DEBUG,     m_bChromaKeyDebug);
        m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_COLOR_ADJ, m_iChromaKeyColorAdj);
        m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_SCALING,   m_bChromaKeyScaling);
        m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_RGBAFP16,  m_bChromaKeyRGBAFP16);
        m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_10BITLIVE,  m_bChromaKey10BitLive);
        m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_ALPHA_SRC,  m_bChromaKeyAlphaFromSrc);

        if (m_iSelectedDevice >= 0)
        {
            m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_VIDEO_SOURCE_DEVICE_INDEX, m_iSelectedDevice);
        }
        m_Pipeline.Stop();
        m_Pipeline.Init(hClientWindow, m_FileName, m_FileNameBackground);
    }
}

//------------------------------------------------------------------------------------------------------------
void SingleWindowPlayback::Pause()
{
    m_Pipeline.Pause();
}

//------------------------------------------------------------------------------------------------------------
void SingleWindowPlayback::Step()
{
    m_Pipeline.Step();
}

//------------------------------------------------------------------------------------------------------------
void SingleWindowPlayback::Stop()
{
    m_bStop = true;
    m_Pipeline.Stop();
}

//------------------------------------------------------------------------------------------------------------
void SingleWindowPlayback::Terminate()
{
    m_Pipeline.Terminate();
}

//------------------------------------------------------------------------------------------------------------
bool SingleWindowPlayback::IsPlaying()
{
    bool bRet = true;
    if(m_Pipeline.GetState() == PipelineStateEof || m_Pipeline.GetState() == PipelineStateNotReady)
    {
        bRet = false;
    }
    return bRet;
}

//------------------------------------------------------------------------------------------------------------
void CaptureVideo::ReorderClientWindows()
{
    if (m_hWnd != NULL && m_hClientWindow != NULL)
    {
        RECT client;
        ::GetClientRect(m_hWnd, &client);
        ::MoveWindow(m_hClientWindow, client.left, client.top, client.right - client.left, client.bottom - client.top, TRUE);
    }
}

//------------------------------------------------------------------------------------------------------------
void CaptureVideo::UpdateCaption()
{
    wchar_t text[1000];
    double FPS = m_Pipeline.GetFPS();
    amf_int32 countFrame = (amf_int32)m_Pipeline.GetNumberOfProcessedFrames();
    amf_int32 state = m_Pipeline.GetState();
    amf_int64 countFramTotal = m_Pipeline.GetFrameCount();
    wchar_t states[][16] = {L"Waiting", L"Ready", L"Running", L"Frozen", L"End"};
    if (countFramTotal > 0)
    {
        swprintf(text, L"%s | FPS( %.1f) | Frames: %lld / %lld | %s", m_title, FPS, countFrame%countFramTotal, countFramTotal, states[state]);
    }
    else
    {
        swprintf(text, L"%s | FPS( %.1f) | Frames: %d | %s", m_title, FPS, countFrame, states[state]);
    }

    ::SetWindowText(m_hWnd, text);
}

//------------------------------------------------------------------------------------------------------------
void SingleWindowPlayback::CheckForRestart()
{
    if (!m_bStop)
    {
        if(m_Pipeline.GetState() == PipelineStateEof || m_Pipeline.GetState() == PipelineStateNotReady)
        {
            m_Pipeline.Restart();
        }
    }
}

//------------------------------------------------------------------------------------------------------------
void CaptureVideo::HandleKeyboard(WPARAM wParam)
{
    bool shiftDown = (GetKeyState(VK_SHIFT)  & (1 << 16)) != 0;
    bool ctrlDown  = (GetKeyState(VK_CONTROL)  & (1 << 16)) != 0;
    int step = shiftDown ? 10 : 1;
    switch(wParam)
    {
    case L'a':
    case L'A':
        m_Pipeline.UpdateChromakeyProperty(AMF_CHROMAKEY_RANGE_MIN, -1, true);  //rangeMin, decrease
        break;
    case L's':
    case L'S':
        m_Pipeline.UpdateChromakeyProperty(AMF_CHROMAKEY_RANGE_MIN, 1, true);  //rangeMin, increase
        break;
    case L'd':
    case L'D':
        m_Pipeline.UpdateChromakeyProperty(AMF_CHROMAKEY_RANGE_MAX, -1, true);  //rangeMax, decrease
        break;
    case L'f':
    case L'F':
        m_Pipeline.UpdateChromakeyProperty(AMF_CHROMAKEY_RANGE_MAX, 1, true);  //rangeMax, increase
        break;
    case L'z':
    case L'Z':
        m_Pipeline.UpdateChromakeyProperty(AMF_CHROMAKEY_RANGE_SPILL, -1, true);  //rangeSpill, decrease
        break;
    case L'x':
    case L'X':
        m_Pipeline.UpdateChromakeyProperty(AMF_CHROMAKEY_RANGE_SPILL, 1, true);  //rangeSpill, increase
        break;
    case L'c':
    case L'C':
        m_Pipeline.UpdateChromakeyProperty(AMF_CHROMAKEY_COLOR_ADJ_THRE, -1, true);  //color adjust threshold, decrease
        break;
    case L'v':
    case L'V':
        m_Pipeline.UpdateChromakeyProperty(AMF_CHROMAKEY_COLOR_ADJ_THRE, 1, true);  //color adjust threshold, increase
        break;
    case L'b':
    case L'B':
        m_Pipeline.UpdateChromakeyProperty(AMF_CHROMAKEY_COLOR_ADJ_THRE2, -1, true);  //color adjust threshold, decrease
        break;
    case L'n':
    case L'N':
        m_Pipeline.UpdateChromakeyProperty(AMF_CHROMAKEY_COLOR_ADJ_THRE2, 1, true);  //color adjust threshold, increase
        break;
    case L'g':
    case L'G':
        m_Pipeline.UpdateChromakeyProperty(AMF_CHROMAKEY_BOKEH_RADIUS, -1, true);  //BK bokeh radius, decrease
        break;
    case L'h':
    case L'H':
        m_Pipeline.UpdateChromakeyProperty(AMF_CHROMAKEY_BOKEH_RADIUS, 1, true);  //BK bokeh radius, increase
        break;
    case L'j':
    case L'J':
        m_Pipeline.UpdateChromakeyProperty(AMF_CHROMAKEY_LUMA_LOW, -1, true);  //Luma threashold
        break;
//	case L'k':
    case L'K':
        m_Pipeline.UpdateChromakeyProperty(AMF_CHROMAKEY_LUMA_LOW, 1, true);  //Luma threashold
        break;
    case L'q':
    case L'Q':
        m_bChromaKeySpill = !m_bChromaKeySpill;
        m_Pipeline.ToggleChromakeyProperty(AMF_CHROMAKEY_SPILL_MODE);
        break;
    case L'w':
    case L'W':
        m_iChromaKeyColorAdj = (m_iChromaKeyColorAdj + 1) % 2;
        m_Pipeline.UpdateChromakeyProperty(AMF_CHROMAKEY_COLOR_ADJ, m_iChromaKeyColorAdj); //off, on
        break;
    case L'e':
    case L'E':
        m_iChromaKeyColorAdj = m_iChromaKeyColorAdj == 2 ? 0 : 2;//off, ,advanced
        m_Pipeline.UpdateChromakeyProperty(AMF_CHROMAKEY_COLOR_ADJ, m_iChromaKeyColorAdj); //off, ,advanced
        break;
    case L'r':
    case L'R':
        m_Pipeline.LoopChromakeyProperty(AMF_CHROMAKEY_BOKEH, 0, 2); //off, background, forground
        break;
    case L't':
    case L'T':
        m_Pipeline.LoopChromakeyProperty(AMF_CHROMAKEY_BYPASS, 0, 2);//on, off, background
        break;
    case L'y':
    case L'Y':
        m_Pipeline.ToggleChromakeyProperty(AMF_CHROMAKEY_EDGE);
        break;
    case 0x20:  //space, pause
        PostMessage(m_hWnd, WM_COMMAND, ID_PLAYBACK_PAUSE, 0);
        break;
    case 0x0D:  //enter, play
        PostMessage(m_hWnd, WM_COMMAND, ID_PLAYBACK_PLAY, 0);
        break;
    case 0x27:
        if (ctrlDown)  //move right
        {
            m_Pipeline.UpdateChromakeyProperty(AMF_CHROMAKEY_POSX, step, true);  //move right 1 pixel
        }
        else  //step forward
        {
            PostMessage(m_hWnd, WM_COMMAND, ID_PLAYBACK_STEP, 0);
        }
        break;
    case 0x25:  //<-, left
        if (ctrlDown)  //move left
        {
            m_Pipeline.UpdateChromakeyProperty(AMF_CHROMAKEY_POSX, -step, true);  //move left 1 pixel
        }
        break;
    case 0x26: // up
        if (ctrlDown) //move up
        {
            m_Pipeline.UpdateChromakeyProperty(AMF_CHROMAKEY_POSY, -step, true);  //move up 1 pixel
        }
        break;
    case 0x28:  //down
        if (ctrlDown) //move down
        {
            m_Pipeline.UpdateChromakeyProperty(AMF_CHROMAKEY_POSY, step, true);  //move down 1 pixel
        }
        break;
    case 0x6d:  //- zoom
    case 0xbd:  //- zoom
        if (ctrlDown) //move down
        {
            m_Pipeline.UpdateScalingProperty(CaptureVideoPipeline::PARAM_NAME_VIDEO_SOURCE_SCALE, step, true);
        }
        break;
    case 0x6b:    //+ zoom
    case 0xbb:    //+ zoom
        if (ctrlDown) //move down
        {
            m_Pipeline.UpdateScalingProperty(CaptureVideoPipeline::PARAM_NAME_VIDEO_SOURCE_SCALE, -step, true);
        }
        break;
    default:
        break;
    }
}

//------------------------------------------------------------------------------------------------------------
OptimizationThread::OptimizationThread() :
m_started(true),
m_percent(0)
{
}

//------------------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL OptimizationThread::OnComponentOptimizationProgress(amf_uint percent)
{
    m_percent = percent;
    return AMF_OK;
}

//------------------------------------------------------------------------------------------------------------
void OptimizationThread::Run()
{
    RequestStop();
    amf::AMFContextPtr pContext;
    g_AMFFactory.GetFactory()->CreateContext(&pContext);
    m_started = false;
    if (pContext)
    {
        pContext->InitDX9(NULL);
        pContext->InitOpenCL(NULL);
        amf::AMFComponentPtr pComponent;
        g_AMFFactory.GetFactory()->CreateComponent(pContext, AMFVideoConverter, &pComponent);
        if (pComponent)
        {
            pComponent->Optimize(this);
        }
    }
}

//------------------------------------------------------------------------------------------------------------
INT_PTR CaptureVideo::ProgressDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM /* lParam */)
{
    static OptimizationThread s_optimizationThread;

    switch (Message)
    {
    case WM_COMMAND:
        switch (LOWORD(wParam))
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
        if (s_optimizationThread.started())
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

//------------------------------------------------------------------------------------------------------------
void CaptureVideo::LoadFromOptions(LPWSTR file)
{
    //defaults
    m_eModeVideoSource = VIDEO_SOURCE_MODE_FILE;
    m_bLoop = true;
    m_bChromaKey = false;
    m_bChromaKeyBK = false;
    m_bChromaKeySpill = false;
    m_iChromaKeyColorAdj = 0;
    m_bChromaKeyDebug = false;
    amf_uint64 modeVideoSource = static_cast<amf_uint64>(VIDEO_SOURCE_MODE_INVALID);

    Options options;
    if (options.LoadFromPath(file) == AMF_OK)
    {
        options.GetParameterStorage(L"Pipeline", &m_Pipeline);
    }
    m_Pipeline.GetParam(CaptureVideoPipeline::PARAM_NAME_VIDEO_SOURCE_MODE, modeVideoSource);
    m_eModeVideoSource = (VIDEO_SOURCE_MODE_ENUM)modeVideoSource;
    m_Pipeline.GetParam(CaptureVideoPipeline::PARAM_NAME_LOOP,                m_bLoop);
    m_Pipeline.GetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY,           m_bChromaKey);
    m_Pipeline.GetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_BK,        m_bChromaKeyBK);
    m_Pipeline.GetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_SPILL,     m_bChromaKeySpill);
    m_Pipeline.GetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_COLOR_ADJ, m_iChromaKeyColorAdj);
    m_Pipeline.GetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_DEBUG,     m_bChromaKeyDebug);
    m_Pipeline.GetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_SCALING,   m_bChromaKeyScaling);
    m_Pipeline.GetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_RGBAFP16,  m_bChromaKeyRGBAFP16);
    m_Pipeline.GetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_10BITLIVE, m_bChromaKey10BitLive);
    m_Pipeline.GetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_ALPHA_SRC, m_bChromaKeyAlphaFromSrc);

    amf_wstring filename = L"";
    if (options.GetParameterWString(L"Inputs", L"filename", filename) == AMF_OK)
    {
        m_FileName = filename.c_str();
    }

    filename = L"";
    if (options.GetParameterWString(L"Inputs", L"filenameBK", filename) == AMF_OK)
    {
        m_FileNameBackground = filename.c_str();
    }

    filename = L"";
    m_iSelectedDevice = -1;
    if (options.GetParameterWString(L"Inputs", L"videoDeviceUSB", filename) == AMF_OK)
    {
        amf_int32 count = m_Pipeline.GetCaptureManager()->GetDeviceCount();
        for (amf_int32 idx = 0; idx < count ; idx++)
        {
            amf::AMFCaptureDevicePtr device;
            m_Pipeline.GetCaptureManager()->GetDevice(idx, &device);
            if(device != NULL)
            {
                amf_wstring name;
                device->GetPropertyWString(AMF_CAPTURE_DEVICE_NAME, &name);
                if(name == filename)
                {
                    m_iSelectedDevice = idx;
                    break;
                }
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------
void CaptureVideo::UpdateOptions(LPWSTR file)
{
    m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_VIDEO_SOURCE_MODE,   m_eModeVideoSource);
    m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_LOOP,                m_bLoop);
    m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY,           m_bChromaKey);
    m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_BK,        m_bChromaKeyBK);
    m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_SPILL,     m_bChromaKeySpill);
    m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_COLOR_ADJ, m_iChromaKeyColorAdj);
    m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_DEBUG,     m_bChromaKeyDebug);
    m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_SCALING,   m_bChromaKeyScaling);
    m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_RGBAFP16,  m_bChromaKeyRGBAFP16);
    m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_10BITLIVE, m_bChromaKey10BitLive);
    m_Pipeline.SetParam(CaptureVideoPipeline::PARAM_NAME_CHROMAKEY_ALPHA_SRC, m_bChromaKeyAlphaFromSrc);
    m_Pipeline.UpdateChromakeyParams(); //save chromakey parameters
    Options options;
    options.Reset();
    options.SetParameterStorage(L"Pipeline", &m_Pipeline);

    options.SetParameterWString(L"Inputs", L"filename", m_FileName.c_str());
    options.SetParameterWString(L"Inputs", L"filenameBK", m_FileNameBackground.c_str());

    if (m_iSelectedDevice >= 0)
    {
        amf::AMFCaptureDevicePtr device;
        m_Pipeline.GetCaptureManager()->GetDevice(m_iSelectedDevice, &device);
        if(device != NULL)
        {
            amf_wstring name;
            device->GetPropertyWString(AMF_CAPTURE_DEVICE_NAME, &name);
            options.SetParameterWString(L"Inputs", L"videoDeviceUSB",name.c_str());
        }
    }

    options.StoreToPath(file);
}

//------------------------------------------------------------------------------------------------------------
void CaptureVideo::ResetOptions()
{
    m_Pipeline.ResetOptions();
    Options options;
    options.LoadFromPath(NULL);
    options.Reset();
    options.StoreToPath(NULL);
    LoadFromOptions();
}
//------------------------------------------------------------------------------------------------------------
