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

#include "resource.h"
#include "CaptureVideoPipeline.h"

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow);

class SingleWindowPlayback
{
public:
    SingleWindowPlayback();
    virtual ~SingleWindowPlayback(){}
    virtual void  Play();
    virtual void  Pause();
    virtual void  Step();
    virtual void  Stop();
    virtual void  Terminate();
    virtual bool  IsPlaying();
    virtual void  CheckForRestart();
    virtual bool  ParseCmdLineParameters();

    CaptureVideoPipeline m_Pipeline;
    void Init(HWND hClientWindow);
protected:
    HWND m_hClientWindow;
    amf::AMF_MEMORY_TYPE    m_eMemoryType;
    bool                    m_bStop;
    bool                    m_bLoop;
    bool                    m_bChromaKey;
    bool                    m_bChromaKeyBK;
    bool                    m_bChromaKeySpill;
    int                     m_iChromaKeyColorAdj; //0:off, 1:on, 2:advanced
    bool                    m_bChromaKeyDebug;
    bool                    m_bChromaKeyScaling;
    bool                    m_bChromaKeyRGBAFP16;
    bool                    m_bChromaKey10BitLive;
    bool                    m_bChromaKeyAlphaFromSrc;
    VIDEO_SOURCE_MODE_ENUM  m_eModeVideoSource;
    amf_int32               m_iSelectedDevice;
    std::wstring            m_FileName;
    std::wstring            m_FileNameBackground;
};

static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK ClientWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK ProgressDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

class CaptureVideo : public SingleWindowPlayback
{
public:
    CaptureVideo(HINSTANCE hInst);
    virtual ~CaptureVideo();
    int Exec(int nCmdShow);
    void EnableAMFProfiling(bool bEnable);
    ATOM MyRegisterClass();
    BOOL InitInstance(int nCmdShow);
    HWND CreateClientWindow(HWND hWndParent);
    void UpdateMenuItems(HMENU hMenu);
    bool FileOpenMedia(bool isBackground=false);
    void HandleKeyboard(WPARAM wParam);
    LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT ClientWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    INT_PTR ProgressDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam);
    INT_PTR About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
protected:
    static const INT MaxLoadString = 100;
    static const UINT WMUserMsgClose = WM_USER + 1000;
    void  ReorderClientWindows();
    void  UpdateCaption();
    void  InitMenu(HWND hWnd);

    HINSTANCE m_hInst;                        // current instance
    HWND      m_hWnd;
    UINT_PTR  m_timerID;
    TCHAR     m_title[MaxLoadString];         // The title bar text
    TCHAR     m_windowClass[MaxLoadString];   // the main window class name

    POINT m_mousePos;
    bool  m_mouseDown;
    void LoadFromOptions(LPWSTR file=NULL);
    void UpdateOptions(LPWSTR file=NULL);
    void ResetOptions();
    void FileLoadOptions(bool bLoad=true);
};

class OptimizationThread : public amf::AMFThread, public amf::AMFComponentOptimizationCallback
{
    bool     m_started;
    amf_uint m_percent;
public:
    OptimizationThread();
    ~OptimizationThread(){};
    bool started()     { return m_started; }
    amf_uint percent() { return m_percent; }

protected:
    virtual AMF_RESULT AMF_STD_CALL OnComponentOptimizationProgress(amf_uint percent);
    virtual void Run();
};
