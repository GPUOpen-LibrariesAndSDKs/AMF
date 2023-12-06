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
#include "BackBufferPresenter.h"

#ifdef _WIN32
#include "VideoPresenterDX12.h"
#include "VideoPresenterDX11.h"
#include "VideoPresenterDX9.h"
#endif
#include "VideoPresenterOpenGL.h"
#if !defined(DISABLE_VULKAN)
#include "VideoPresenterVulkan.h"
#endif
#include <public/common/TraceAdapter.h>

#define AMF_FACILITY L"BackBufferPresenter"

BackBufferPresenter::BackBufferPresenter(amf_handle hwnd, amf::AMFContext* pContext, amf_handle hDisplay)
    : m_hwnd(hwnd)
    , m_hDisplay(hDisplay)
    , m_pContext(pContext)
    , m_bRenderToBackBuffer(false)
    , m_currentFullScreenState(false)
    , m_windowModeRect{}
#ifdef _WIN32
    , m_windowModeStyle(0)
#endif
{
}

BackBufferPresenter::~BackBufferPresenter()
{
}

AMF_RESULT
BackBufferPresenter::Create(
    BackBufferPresenterPtr& pPresenter,
    amf::AMF_MEMORY_TYPE type,
    amf_handle hwnd,
    amf::AMFContext* pContext,
    amf_handle display
)
{
    switch(type)
    {
#ifdef _WIN32
    case amf::AMF_MEMORY_DX9:
        pPresenter = std::make_shared<VideoPresenterDX9>(hwnd, pContext);
        return AMF_OK;
    case amf::AMF_MEMORY_DX11:
        pPresenter = std::make_shared<VideoPresenterDX11>(hwnd, pContext);
        return AMF_OK;
	case amf::AMF_MEMORY_DX12:
		pPresenter = std::make_shared<VideoPresenterDX12>(hwnd, pContext);
		return AMF_OK;
#endif
    case amf::AMF_MEMORY_OPENGL:
        pPresenter = std::make_shared<VideoPresenterOpenGL>(hwnd, pContext);
        return AMF_OK;
    case amf::AMF_MEMORY_VULKAN:
#if defined(DISABLE_VULKAN)
#ifdef _WIN32
        pContext->InitDX11(NULL);
        pPresenter = std::make_shared<VideoPresenterDX11>(hwnd, pContext);
#endif        
#else
        pPresenter = std::make_shared<VideoPresenterVulkan>(hwnd, pContext, display);
#endif
        return AMF_OK;
    default:
        return AMF_NOT_SUPPORTED;
    }
}

AMF_RESULT
BackBufferPresenter::SetProcessor(amf::AMFComponent* pProcessor, amf::AMFComponent* pHQScaler)
{
    amf::AMFLock lock(&m_cs);
    AMF_RESULT res = VideoPresenter::SetProcessor(pProcessor, pHQScaler);

    if (res == AMF_OK)
    {
        m_bRenderToBackBuffer = (m_pProcessor != NULL);
    }

    return res;
}

AMFRect
BackBufferPresenter::GetClientRect()
{
    AMFRect clientRect = {0};
    if(m_hwnd!=NULL)
    {
#if defined(_WIN32)
        RECT client;
        ::GetClientRect((HWND)m_hwnd,&client);
        clientRect = AMFConstructRect(client.left, client.top, client.right, client.bottom);
#elif defined(__linux)
        Window root_return;
        int x_return, y_return;
        unsigned int width_return, height_return;
        unsigned int border_width_return;
        unsigned int depth_return;
        XGetGeometry((Display*)m_hDisplay, (Window)m_hwnd, &root_return, &x_return, &y_return, &width_return, 
                      &height_return, &border_width_return, &depth_return);

        clientRect = AMFConstructRect(0, 0, width_return - 2 * border_width_return, height_return - 2 * border_width_return);
#endif        
    }
    return clientRect;
}

AMF_RESULT BackBufferPresenter::SetFullScreenState(amf_bool fullScreen)
{
    if (m_currentFullScreenState == fullScreen)
    {
        return AMF_OK;
    }

    m_currentFullScreenState = fullScreen;

#if defined(_WIN32)
    const HWND hWnd = (HWND)m_hwnd;

    // Should only set fullscreen if holding top-most window
    if (hWnd != ::GetAncestor(hWnd, GA_ROOT))
    {
        return AMF_OK;
    }

    LONG_PTR style = 0;
    UINT topLeftX = 0;
    UINT topLeftY = 0;
    UINT width = 0;
    UINT height = 0;
    UINT posFlags = SWP_FRAMECHANGED | SWP_SHOWWINDOW;

    if (fullScreen)
    {
        WINDOWINFO info;
        ::GetWindowInfo(hWnd, &info);
        m_windowModeRect = AMFConstructRect(info.rcClient.left, info.rcClient.top, info.rcClient.right, info.rcClient.bottom);
        m_windowModeStyle = ::GetWindowLongPtr(hWnd, GWL_STYLE);

        style = WS_VISIBLE | WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;

        const HDC hdc = ::GetWindowDC(hWnd);
        width = ::GetDeviceCaps(hdc, DESKTOPHORZRES);
        height = ::GetDeviceCaps(hdc, DESKTOPVERTRES);
    }
    else
    {
        style = m_windowModeStyle;
        topLeftX = m_windowModeRect.left;
        topLeftY = m_windowModeRect.top;
        width = m_windowModeRect.Width();
        height = m_windowModeRect.Height();
    }

    AMF_RETURN_IF_FALSE(width > 0 && height > 0, AMF_INVALID_RESOLUTION, L"SetFullScreenState() - Invalid window size %ux%u", width, height);

    ::SetWindowLongPtr(hWnd, GWL_STYLE, style);
    ::SetWindowPos(hWnd, nullptr, topLeftX, topLeftY, width, height, posFlags);

#elif defined(__linux) // _WIN32

#endif // __linux
    return AMF_OK;
}


