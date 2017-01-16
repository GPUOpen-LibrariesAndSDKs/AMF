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

#include "RenderWindow.h"
#include "../common/CmdLogger.h"

RenderWindow::RenderWindow() :
    m_hWnd(0)
{
    memset(&m_MonitorWorkArea,0, sizeof(m_MonitorWorkArea));
}
RenderWindow::~RenderWindow()
{
    if(m_hWnd != NULL)
    {
        ::DestroyWindow(m_hWnd);
    }
}

LRESULT CALLBACK MyDefWindowProcW(
    __in HWND hWnd,
    __in UINT Msg,
    __in WPARAM wParam,
    __in LPARAM lParam);
BOOL CALLBACK MyStaticEnumProc(
  _In_  HMONITOR hMonitor,
  _In_  HDC hdcMonitor,
  _In_  LPRECT lprcMonitor,
  _In_  LPARAM dwData
)
{
    RenderWindow *pThis= (RenderWindow *)dwData;
    return pThis->MyEnumProc(hMonitor);

}

BOOL    RenderWindow::MyEnumProc(HMONITOR hMonitor)
{
    MONITORINFOEX mi;
    mi.cbSize = sizeof(mi);
    GetMonitorInfo(hMonitor, &mi);
    if(m_DeviceName == mi.szDevice)
    {
        m_MonitorWorkArea = mi.rcWork;
        return FALSE;
    }
    return TRUE;
}

bool RenderWindow::CreateD3Window(amf_int32  width, amf_int32  height, amf_int32  adapterID, bool bFullScreen)
{
    if(m_hWnd != NULL)
    {
        return true;
    }
    HINSTANCE hInstance = (HINSTANCE)GetModuleHandle(NULL);

    WNDCLASSEX wcex     = {0};
    wcex.cbSize         = sizeof(WNDCLASSEX);
    wcex.style          = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wcex.lpfnWndProc    = MyDefWindowProcW;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hCursor        = LoadCursor(NULL,IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszClassName  = L"videorender";
    wcex.hIconSm        = LoadIcon(NULL, IDI_APPLICATION);

    RegisterClassEx(&wcex);

    int posX = 0;
    int posY = 0;

    UINT count=0;
    amf_int32 adapterIDLocal = 0;

    while(true)
    {

        DISPLAY_DEVICE displayDevice;
        displayDevice.cb = sizeof(displayDevice);
        if(EnumDisplayDevices(NULL, count, &displayDevice, 0) == FALSE)
        {
            break;
        }
        if(displayDevice.StateFlags & DISPLAY_DEVICE_ACTIVE)
        {
            if(adapterIDLocal == adapterID  || (adapterID == -1 && (displayDevice.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)) )
            {
                m_DeviceName = displayDevice.DeviceName;
                break;
            }
            adapterIDLocal++;
        }
        count++;
    }
    EnumDisplayMonitors(NULL, NULL, MyStaticEnumProc, (LPARAM)this);
    // find adapter and provide coordinates
    posX = (m_MonitorWorkArea.left + m_MonitorWorkArea.right) / 2 - width  / 2;
    posY = (m_MonitorWorkArea.top + m_MonitorWorkArea.bottom) / 2 - height / 2;

//    GetWindowPosition(posX, posY);
    m_hWnd = CreateWindow( L"videorender", L"VIDEORENDER", 
//        bFullScreen ?  (WS_EX_TOPMOST | WS_POPUP) : WS_OVERLAPPEDWINDOW,
        WS_POPUP,
      posX, posY, width, height, NULL, NULL, hInstance, NULL);

    ::ShowWindow(m_hWnd, SW_NORMAL);
    
    ::UpdateWindow(m_hWnd);
    return true;
}


void RenderWindow::ProcessWindowMessages()
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
HWND RenderWindow::GetHwnd() const 
{
    return m_hWnd;
}
void RenderWindow::Resize(amf_int32  width, amf_int32 height)
{
    if(m_hWnd)
    {
        SetWindowPos(m_hWnd, 0, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}


LRESULT CALLBACK MyDefWindowProcW(
    __in HWND hWnd,
    __in UINT Msg,
    __in WPARAM wParam,
    __in LPARAM lParam)
{
    
    switch(Msg)
    {
//        case WM_ACTIVATEAPP:
        case WM_ACTIVATE:                            // Watch For Window Activate Message
        {
            {
                bool active = LOWORD(wParam) != WA_INACTIVE ;                    // Check Minimization State
                if(active)
                {
                    LOG_INFO(L"VIDEORENDER Window Activated");
                }
                else
                {
                    LOG_INFO(L"VIDEORENDER Window Deactivated");
                }
            }
            return 0;                                // Return To The Message Loop
        }
        break;
    }
    return DefWindowProc( hWnd,Msg,wParam,lParam);
}