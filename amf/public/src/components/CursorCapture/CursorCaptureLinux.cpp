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
// Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.
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

#include "CursorCaptureLinux.h"
#include "public/common/TraceAdapter.h"
#include <X11/extensions/Xfixes.h>
#include <memory>

using namespace amf;

#define AMF_FACILITY L"AMFCursorCaptureLinux"

typedef std::unique_ptr<XFixesCursorImage, decltype(&XFree)> XFixesCursorImagePtr;

AMFCursorCaptureLinux::AMFCursorCaptureLinux(AMFContext* pContext) : m_pContext(pContext)
{
    XInitThreads();
    m_pDisplay = XDisplay::Ptr(new XDisplay);
    if (m_pDisplay->IsValid() == false)
    {
        AMFTraceWarning(AMF_FACILITY, L"Couldn't connect to XDisplay");
    }
    else
    {
        XDisplayPtr display(m_pDisplay);
        int error;
        if (XFixesQueryExtension(display, &m_iXfixesEventBase, &error) == false)
        {
            AMFTraceWarning(AMF_FACILITY, L"XFixes not available on display.");
            m_pDisplay = nullptr;
        }
        Window root = DefaultRootWindow((Display*)display);
        XFixesSelectCursorInput(display, root, XFixesDisplayCursorNotifyMask);
    }
}

AMFCursorCaptureLinux::~AMFCursorCaptureLinux()
{
}

AMF_RESULT AMF_STD_CALL AMFCursorCaptureLinux::AcquireCursor(AMFSurface** pSurface)
{
    AMFLock lock(&m_Sect);
    *pSurface = NULL;

    // hide cursor if display or xfixes is not available
    if (m_pDisplay == nullptr || m_pDisplay->IsValid() == false)
    {
        if (m_bFirstCursor == true)
        {
            return AMF_REPEAT;
        }

        AMF_RESULT res = m_pContext->AllocSurface(AMF_MEMORY_HOST, AMF_SURFACE_ARGB, 1, 1, pSurface);
        AMF_RETURN_IF_FAILED(res, L"AllocSurface failed");

        m_bFirstCursor = true;
        return AMF_OK;
    }

    XDisplayPtr display(m_pDisplay);

    XEvent event;
    if (m_bFirstCursor == true && XCheckTypedEvent(display, m_iXfixesEventBase + XFixesCursorNotify, &event) == false)
    {
        return AMF_REPEAT;
    }

    //this is a unique_ptr with custom XFree deleter so we don't have to worry about calling XFree ourselves
    XFixesCursorImagePtr cursor = XFixesCursorImagePtr(XFixesGetCursorImage(display), &XFree);

    if (cursor == nullptr)
    {
        AMFTraceInfo(AMF_FACILITY, L"XFixesGetCursorImage - returned nullptr");
        return AMF_OK;
    }

    AMFTraceInfo(AMF_FACILITY, L"w: %d, h: %d, atom: %d", cursor->width, cursor->height, cursor->atom);

    AMF_RESULT res = m_pContext->AllocSurface(AMF_MEMORY_HOST, AMF_SURFACE_ARGB, cursor->width, cursor->height, pSurface);
    AMF_RETURN_IF_FAILED(res, L"AllocSurface failed");

    unsigned long* src = cursor->pixels;
    amf_uint32* dst = reinterpret_cast<amf_uint32*>((*pSurface)->GetPlaneAt(0)->GetNative());
    amf_int32 width = cursor->width;
    amf_int32 height = cursor->height;
    amf_int32 dstPitch = (*pSurface)->GetPlaneAt(0)->GetHPitch();
    // cursor->pixels is 32-bit values stored in a 64-bit unsigned longs, so we can't just use memcpy
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            dst[y * dstPitch/4 + x] = static_cast<amf_uint32>(src[y * width + x]);
        }
    }

    AMFPoint hotspot;
    hotspot.x = cursor->xhot;
    hotspot.y = cursor->yhot;
    (*pSurface)->SetProperty(L"Hotspot", hotspot);

    Window root = DefaultRootWindow((Display*)display);

    Window rootOut = 0;
    int x, y = 0;
    unsigned int swidth, sheight = 0;
    unsigned int borderWidth, depth = 0;
    Status ret = XGetGeometry(display, root, &rootOut, &x, &y, &swidth, &sheight, &borderWidth, &depth);

    AMFSize screenSize = {0, 0};
    // zero status codes indicate an error
    if (ret != 0)
    {
        screenSize = AMFConstructSize(swidth, sheight);
    }
    (*pSurface)->SetProperty(L"Resolution", screenSize);

    m_bFirstCursor = true;

    return AMF_OK;
}

AMF_RESULT AMF_STD_CALL AMFCursorCaptureLinux::Reset()
{
    AMFLock lock(&m_Sect);

    m_bFirstCursor = false;

    return AMF_OK;
}
