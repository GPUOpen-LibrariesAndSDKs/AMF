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

#pragma once

#if defined(_WIN32)
    #include <tchar.h>
    #include <windows.h>
#else
    #include <X11/Xlib.h>
    #include <X11/Xutil.h>
    #include <X11/Xos.h>
#endif

#include "public/include/core/Platform.h"
#include <string>

#if defined(_WIN32)
class RenderWindow
{
public: 
    RenderWindow();
    ~RenderWindow();

    bool CreateD3Window(amf_int32 width, amf_int32 height, amf_int32 adapterID, bool visible, bool bFullScreen);
    bool CheckIntegratedMonitor();

    void ProcessWindowMessages();
    amf_handle GetHwnd() const;
    amf_handle GetDisplay() const {return NULL;}
    void Resize(amf_int32 width, amf_int32 height);

private:
//    void GetWindowPosition(int &posX, int &posY);
    HWND            m_hWnd;
    std::wstring    m_DeviceName;
    RECT            m_MonitorWorkArea;
public:
    BOOL    MyEnumProc(HMONITOR hMonitor);
};
#elif defined (__linux)

class RenderWindow
{
public: 
    RenderWindow();
    ~RenderWindow();

    bool CreateD3Window(amf_int32 width, amf_int32 height, amf_int32 adapterID, bool bFullScreen);

    void ProcessWindowMessages();
    amf_handle GetHwnd() const {return (amf_handle)m_hWnd;}
    amf_handle GetDisplay() const {return (amf_handle)m_pDisplay;}
    void Resize(amf_int32 width, amf_int32 height);

private:
//    void GetWindowPosition(int &posX, int &posY);
    Window          m_hWnd;
    Display*        m_pDisplay;
    Atom            WM_DELETE_WINDOW;
    std::wstring    m_DeviceName;
};
#endif