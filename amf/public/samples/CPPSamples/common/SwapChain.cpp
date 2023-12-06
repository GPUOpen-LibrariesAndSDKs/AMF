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


#include "SwapChain.h"
#include "public/common/TraceAdapter.h"
#include <climits>

using namespace amf;

#define AMF_FACILITY L"SwapChain"

SwapChain::SwapChain(amf::AMFContext* pContext) :
    m_pContext(pContext),
    m_hwnd(nullptr),
    m_hDisplay(nullptr),
    m_size{},
    m_format(amf::AMF_SURFACE_UNKNOWN),
    m_fullscreenEnabled(false),
    m_hdrEnabled(false),
    m_outputHDRMetaData{},
    m_colorSpace {},
    m_stereoEnabled(false)
{
}

SwapChain::~SwapChain()
{
    Terminate();
}

AMF_RESULT SwapChain::Terminate()
{
    m_hwnd = nullptr;
    m_hDisplay = nullptr;
    m_size = {};
    SetFormat(AMF_SURFACE_UNKNOWN); // Set default format
    m_fullscreenEnabled = false;
    m_hdrEnabled = false;
    m_outputHDRMetaData = {};
    m_colorSpace = {};
    m_stereoEnabled = false;
    return AMF_OK;
}

AMF_RESULT SwapChain::EnableHDR(amf_bool enable)
{
    AMFLock lock(&m_sync);

    if (enable == m_hdrEnabled)
    {
        return AMF_OK;
    }

    if (HDRSupported() == false)
    {
        return AMF_NOT_SUPPORTED;
    }

    m_hdrEnabled = enable;
    return UpdateCurrentOutput();
}

AMF_RESULT SwapChain::GetColorSpace(ColorSpace& colorSpace)
{ 
    AMF_RESULT res = UpdateCurrentOutput();
    AMF_RETURN_IF_FAILED(res, L"GetColorSpace() - UpdateCurrentOutput() failed");

    colorSpace = m_colorSpace;
    
    return AMF_OK;
}

AMF_RESULT SwapChain::GetOutputHDRMetaData(AMFHDRMetadata& hdrMetaData)
{
    AMF_RESULT res = UpdateCurrentOutput();
    AMF_RETURN_IF_FAILED(res, L"GetColorSpace() - UpdateCurrentOutput() failed");

    hdrMetaData = m_outputHDRMetaData;
    return AMF_OK;
}

AMF_RESULT SwapChain::GetBackBufferIndex(const BackBufferBase* pBuffer, amf_uint& index) const
{
    AMF_RETURN_IF_FALSE(pBuffer != nullptr, AMF_INVALID_ARG, L"GetBackBufferIndex() - pBuffer is NULL");

    const BackBufferBasePtr* ppBackBuffers = GetBackBuffers();
    AMF_RETURN_IF_FALSE(ppBackBuffers != nullptr, AMF_NOT_INITIALIZED, L"GetBackBufferIndex() - Buffers are not initialized");

    for (amf_uint i = 0; i < GetBackBufferCount(); ++i)
    {
        if (*ppBackBuffers[i].get() == *pBuffer)
        {
            index = i;
            return AMF_OK;
        }
    }

    index = UINT_MAX;
    return AMF_NOT_FOUND;
}

AMF_RESULT SwapChain::GetBackBufferIndex(amf::AMFSurface* pSurface, amf_uint& index) const
{
    AMF_RETURN_IF_FALSE(pSurface != nullptr, AMF_INVALID_ARG, L"GetNextBackBuffer() - pSurface is NULL");

    const BackBufferBasePtr* ppBackBuffers = GetBackBuffers();
    AMF_RETURN_IF_FALSE(ppBackBuffers != nullptr, AMF_NOT_INITIALIZED, L"GetBackBufferIndex() - Buffers are not initialized");

    const void* pNativeSurface = GetSurfaceNative(pSurface);
    AMF_RETURN_IF_FALSE(pNativeSurface != nullptr, AMF_INVALID_ARG, L"GetBackBufferIndex() - Surface packed plane native is NULL");


    for (amf_uint i = 0; i < GetBackBufferCount(); ++i)
    {
        if (ppBackBuffers[i]->GetNative() == pNativeSurface)
        {
            index = i;
            return AMF_OK;
        }
    }

    index = UINT_MAX;
    return AMF_NOT_FOUND;
}

AMF_RESULT SwapChain::GetBackBuffer(amf_uint index, const BackBufferBase** ppBuffer) const
{
    AMF_RETURN_IF_FALSE(index < GetBackBufferCount(), AMF_INVALID_ARG, L"GetBackBuffer() - index (%u) out of range, must be < %u", index, GetBackBufferCount());
    AMF_RETURN_IF_FALSE(ppBuffer != nullptr, AMF_INVALID_ARG, L"GetNextBackBuffer() - ppBuffer is NULL");

    const BackBufferBasePtr* ppBackBuffers = GetBackBuffers();
    AMF_RETURN_IF_FALSE(ppBackBuffers != nullptr, AMF_NOT_INITIALIZED, L"GetBackBuffer() - Buffers are not initialized");

    *ppBuffer = ppBackBuffers[index].get();

    return AMF_OK;
}

AMF_RESULT SwapChain::GetBackBuffer(amf_uint index, amf::AMFSurface** ppSurface) const
{
    const BackBufferBase* pBuffer = nullptr;
    AMF_RESULT res = GetBackBuffer(index, &pBuffer);
    AMF_RETURN_IF_FAILED(res, L"GetBackBuffer() - GetBackBuffer(pBuffer) failed");

    res = BackBufferToSurface(pBuffer, ppSurface);
    AMF_RETURN_IF_FAILED(res, L"GetBackBuffer() - BackBufferToSurface() failed");

    return AMF_OK;
}

AMF_RESULT SwapChain::AcquireNextBackBuffer(const BackBufferBase** ppBackBuffer)
{
    AMF_RETURN_IF_FALSE(ppBackBuffer != nullptr, AMF_INVALID_ARG, L"AcquireNextBackBuffer() - ppBackBuffer is NULL");

    amf_uint index = 0;
    AMF_RESULT res = AcquireNextBackBufferIndex(index);
    if (res == AMF_NEED_MORE_INPUT)
    {
        return AMF_NEED_MORE_INPUT;
    }
    AMF_RETURN_IF_FAILED(res, L"AcquireNextBackBuffer() - GetNextBackBufferIndex() failed");

    res = GetBackBuffer(index, ppBackBuffer);
    if (res != AMF_OK)
    {
        DropBackBufferIndex(index);
    }
    AMF_RETURN_IF_FAILED(res, L"AcquireNextBackBuffer() - GetBackBuffer() failed");

    return AMF_OK;
}

AMF_RESULT SwapChain::AcquireNextBackBuffer(AMFSurface** ppSurface)
{
    AMF_RETURN_IF_FALSE(ppSurface != nullptr, AMF_INVALID_ARG, L"AcquireNextBackBuffer() - ppSurface is NULL");

    amf_uint index = 0;
    AMF_RESULT res = AcquireNextBackBufferIndex(index);
    if (res == AMF_NEED_MORE_INPUT || res == AMF_NOT_SUPPORTED)
    {
        return res;
    }
    AMF_RETURN_IF_FAILED(res, L"AcquireNextBackBuffer() - GetNextBackBufferIndex() failed");

    res = GetBackBuffer(index, ppSurface);
    if (res != AMF_OK)
    {
        DropBackBufferIndex(index);
        if (res == AMF_NOT_SUPPORTED)
        {
            return res;
        }
    }
    AMF_RETURN_IF_FAILED(res, L"AcquireNextBackBuffer() - GetBackBuffer() failed");

    return AMF_OK;
}

AMF_RESULT SwapChain::DropBackBuffer(const BackBufferBase* pBuffer)
{
    amf_uint index = 0;
    AMF_RESULT res = GetBackBufferIndex(pBuffer, index);
    if (res == AMF_NOT_FOUND)
    {
        return AMF_OK;
    }

    AMF_RETURN_IF_FAILED(res, L"DropBackBuffer() - GetBackBufferIndex() failed");

    return DropBackBufferIndex(index);
}

AMF_RESULT SwapChain::DropBackBuffer(amf::AMFSurface* pSurface)
{
    amf_uint index = 0;
    AMF_RESULT res = GetBackBufferIndex(pSurface, index);
    if (res == AMF_NOT_FOUND)
    {
        return AMF_OK;
    }

    AMF_RETURN_IF_FAILED(res, L"DropBackBuffer() - GetBackBufferIndex() failed");
    return DropBackBufferIndex(index);
}

inline void* SwapChain::GetSurfaceNative(amf::AMFSurface* pSurface) const
{
    return GetNativePackedSurface<void>(pSurface, GetBackBuffers()[0]->GetMemoryType());
}


AMF_RESULT SetWindowFullscreenState(amf_handle hwnd, amf_handle hDisplay, amf_bool fullscreen, WindowFullscreenContext& context)
{
    hDisplay; // Suppress unreferenced parameter warning (C4100)

    AMF_RETURN_IF_FALSE(hwnd != nullptr, AMF_INVALID_ARG, L"SetWindowFullscreenState() - hwnd is NULL");

    if (context.fullscreenState == fullscreen)
    {
        return AMF_OK;
    }

#if defined(_WIN32)

    // Should only set fullscreen if holding top-most window
    if ((HWND)hwnd != ::GetAncestor((HWND)hwnd, GA_ROOT))
    {
        return AMF_OK;
    }

    LONG_PTR style = 0;
    LONG_PTR exStyle = 0;
    AMFPoint origin = {};
    AMFSize size = {};
    const HWND hWnd = (HWND)hwnd;
    UINT posFlags = SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE;

    if (fullscreen)
    {
        WINDOWINFO info;
        ::GetWindowInfo(hWnd, &info);
        context.windowModeRect = AMFConstructRect(info.rcClient.left, info.rcClient.top, info.rcClient.right, info.rcClient.bottom);
        context.windowModeStyle = ::GetWindowLongPtr(hWnd, GWL_STYLE);
        context.windowModeExStyle = ::GetWindowLongPtr(hWnd, GWL_EXSTYLE);

        style = WS_VISIBLE | WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        exStyle = WS_EX_APPWINDOW | WS_EX_TOPMOST;

        // Get display information
        DisplayInfo displayInfo = {};
        AMF_RESULT res = GetDisplayInfo(hwnd, displayInfo);
        AMF_RETURN_IF_FAILED(res, L"SetWindowFullscreenState() - GetDisplayInfo() failed");

        origin.x = displayInfo.monitorRect.left;
        origin.y = displayInfo.monitorRect.top;
        size.width = displayInfo.monitorRect.Width();
        size.height = displayInfo.monitorRect.Height();
    }
    else
    {
        style = context.windowModeStyle;
        AMF_RETURN_IF_FALSE(style != 0, AMF_NOT_INITIALIZED, L"SetWindowFullscreenState() - Window mode not initialized");

        exStyle = context.windowModeExStyle;
        origin.x = context.windowModeRect.left;
        origin.y = context.windowModeRect.top;
        size.width = context.windowModeRect.Width();
        size.height = context.windowModeRect.Height();
    }

    AMF_RETURN_IF_FALSE(size.width > 0 && size.height > 0, AMF_INVALID_RESOLUTION, L"SetWindowFullscreenState() - Invalid window size %ux%u", size.width, size.height);

    ::SetWindowLongPtr(hWnd, GWL_STYLE, style);
    ::SetWindowLongPtr(hWnd, GWL_EXSTYLE, exStyle);

    ShowCursor(fullscreen ? FALSE : TRUE);

    LONG ret = ::SetWindowPos(hWnd, fullscreen ? HWND_TOPMOST : HWND_NOTOPMOST, origin.x, origin.y, size.width, size.height, posFlags);
    AMF_RETURN_IF_FALSE(ret != 0, AMF_FAIL, L"SetWindowFullscreenState() - SetWindowPos() failed, code=%d", GetLastError());

    context.currentRect = AMFConstructRect(origin.x, origin.y, origin.x + size.width, origin.y + size.height);

#elif defined(__ANDROID__)
    // TODO
#elif defined(__linux)
    // TODO
#else
    return AMF_NOT_SUPPORTED;
#endif

    context.fullscreenState = fullscreen;
    return AMF_OK;
}