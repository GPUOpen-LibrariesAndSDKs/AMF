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

#include "CursorCaptureWin.h"

using namespace amf;

AMFCursorCaptureWin::AMFCursorCaptureWin(AMFContext* pContext) :
    m_pContext(pContext),
    m_cursor(0),
    m_lastCaptureTime(0)
{
    HMODULE libUser32 = LoadLibraryW(L"user32.dll");
    getCursorFrameInfo = (GET_CURSOR_FRAME_INFO)GetProcAddress(libUser32, "GetCursorFrameInfo");

    memset(&m_animated, 0, sizeof(m_animated));
    memset(&m_color, 0, sizeof(m_color));
    memset(&m_mask, 0, sizeof(m_mask));
}

AMFCursorCaptureWin::~AMFCursorCaptureWin()
{
}

AMF_RESULT AMF_STD_CALL AMFCursorCaptureWin::AcquireCursor(AMFSurface** pSurface)
{
    AMFLock lock(&m_Sect);

    CURSORINFO cursorInfo;
    memset(&cursorInfo, 0, sizeof(CURSORINFO));
    cursorInfo.cbSize = sizeof(CURSORINFO);
    GetCursorInfo(&cursorInfo);

    if ((cursorInfo.flags & CURSOR_SHOWING) == 0)
    {
        // cursor is not visible
        *pSurface = NULL;
        m_cursor = 0;
        return AMF_OK;
    }

    HCURSOR cursor = 0;

    if (m_cursor != cursorInfo.hCursor)
    {
        m_cursor = cursorInfo.hCursor;
        //AMFTraceInfo(L"CursorCapture", L"cursor changed (%x)", m_cursor);

        m_animated.hAnimatedCursor = getCursorFrameInfo(m_cursor, L"", m_animated.dwFrameIndex, &m_animated.dwDisplayRate, &m_animated.dwTotalFrames);

        m_lastCaptureTime = amf_high_precision_clock();

        cursor = m_cursor;
    }

    if (m_animated.dwTotalFrames > 1)
    {
        amf_pts now = amf_high_precision_clock();

        if ((now - m_lastCaptureTime) > ((amf_pts)m_animated.dwDisplayRate * 16 * AMF_MILLISECOND))
        {
            m_animated.dwFrameIndex = (m_animated.dwFrameIndex + 1) % m_animated.dwTotalFrames;
            m_animated.hAnimatedCursor = getCursorFrameInfo(m_cursor, L"", m_animated.dwFrameIndex, &m_animated.dwDisplayRate, &m_animated.dwTotalFrames);
            //AMFTraceInfo(L"CursorCapture", L"animated cursor %x (frame index %d, display rate %d, total frames %d)", m_animated.hAnimatedCursor, m_animated.dwFrameIndex, m_animated.dwDisplayRate, m_animated.dwTotalFrames);

            cursor = m_animated.hAnimatedCursor;
            m_lastCaptureTime = now;
        }
    }

    // cursor changed or animated cursor advanced a frame
    if (0 != cursor)
    {
        ICONINFO iconInfo;
        if (GetIconInfo(cursor, &iconInfo))
        {
            CopyBitmapToBuffer(iconInfo.hbmMask, m_mask);
            CopyBitmapToBuffer(iconInfo.hbmColor, m_color);

            bool useXor = false;
            if (NULL == iconInfo.hbmColor)
            {
                GetMonochromeCursor(m_mask, m_color, useXor);
            }
            else
            {
                WriteMaskToAlpha(m_mask, m_color);
            }

            AMF_RESULT res = m_pContext->AllocSurface(AMF_MEMORY_HOST, AMF_SURFACE_BGRA, m_color.bm.bmWidth, m_color.bm.bmHeight, pSurface);
            AMF_RETURN_IF_FAILED(res, L"AllocSurface failed");

            amf_uint8* src = (amf_uint8*)m_color.buffer.GetData();
            amf_uint8* dst = (amf_uint8*)(*pSurface)->GetPlaneAt(0)->GetNative();
            amf_int32 width = m_color.bm.bmWidth;
            amf_int32 height = m_color.bm.bmHeight;
            amf_int32 srcPitch = m_color.bm.bmWidthBytes;
            amf_int32 dstPitch = (*pSurface)->GetPlaneAt(0)->GetHPitch();
            for (int y = 0; y < height; y++)
            {
                memcpy(dst + y * dstPitch, src + y * srcPitch, width * 4);
            }

            AMFPoint hotspot;
            hotspot.x = iconInfo.xHotspot;
            hotspot.y = iconInfo.yHotspot;

            (*pSurface)->SetProperty(L"Hotspot", hotspot);

            // mark the cursor as monochrome
            (*pSurface)->SetProperty(L"Monochrome", useXor);

            AMFSize screenSize = AMFConstructSize(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
            (*pSurface)->SetProperty(L"Resolution", screenSize);

            DeleteObject(iconInfo.hbmColor);
            DeleteObject(iconInfo.hbmMask);

            return AMF_OK;
        }
    }

    // no change to cursor
    return AMF_REPEAT;
}
AMF_RESULT AMF_STD_CALL AMFCursorCaptureWin::Reset()
{
    AMFLock lock(&m_Sect);
    m_cursor = 0;
    m_lastCaptureTime = 0;
    return AMF_OK;
}

bool AMFCursorCaptureWin::CopyBitmapToBuffer(HBITMAP const hBitmap, BitmapBuffer& buffer)
{
    if (NULL != hBitmap && GetObject(hBitmap, sizeof(BITMAP), &buffer.bm) > 0)
    {
        unsigned int uiNewSize = buffer.bm.bmWidthBytes * buffer.bm.bmHeight;

        buffer.buffer.SetSize(uiNewSize);

        unsigned int uiRealSize = GetBitmapBits(hBitmap, uiNewSize, buffer.buffer.GetData());

        if (uiRealSize == buffer.buffer.GetSize())
        {
            return true;
        }
    }

    return false;
}

bool AMFCursorCaptureWin::WriteMaskToAlpha(const BitmapBuffer& mask, BitmapBuffer& color)
{
    if (color.bm.bmWidth == mask.bm.bmWidth && color.bm.bmHeight == mask.bm.bmHeight && color.bm.bmBitsPixel == 32)
    {
        unsigned int uiNumPixels = color.bm.bmWidth * color.bm.bmHeight;

        // do a pass through all the color mask first
        // if none of the color mask bits have alpha, then we need to manually insert alpha values
        bool useColorAlpha = false;

        unsigned char* pColor = static_cast<unsigned char*>(color.buffer.GetData());
        unsigned char* pMask = static_cast<unsigned char*>(mask.buffer.GetData());
        int maskBit = 7;
        for (unsigned int x = 0; x < uiNumPixels; ++x)
        {
            if (*(pColor + 3) > 0)
            {
                useColorAlpha = true;
                break;
            }
            pColor += 4;
        }

        pColor = static_cast<unsigned char*>(color.buffer.GetData());
        pMask = static_cast<unsigned char*>(mask.buffer.GetData());
        maskBit = 7;
        for (unsigned int x = 0; x < uiNumPixels; ++x)
        {
            // manipulate the alpha of the color mask only if:
            // - the entire color mask has no alpha value
            // - the bit mask is 0, or color rgb is non 0
            if (!useColorAlpha)
            {
                if ((*pMask >> maskBit) & 1)
                {
                    unsigned int c = *(unsigned int*)pColor;

                    if (c > 0)
                    {
                        *(pColor + 3) = 255;
                        *(pColor) = 255 - *(pColor);
                        *(pColor + 1) = 255 - *(pColor + 1);
                        *(pColor + 2) = 255 - *(pColor + 2);
                    }
                }
                else
                {
                    *(pColor + 3) = 255;
                }
            }

            maskBit--;
            if (maskBit == -1)
            {
                pMask++;
                maskBit = 7;
            }
            pColor += 4;
        }

        return true;
    }

    return false;
}

bool AMFCursorCaptureWin::GetMonochromeCursor(const BitmapBuffer& mask, BitmapBuffer& color, bool &xorValue)
{
    if (mask.bm.bmHeight == mask.bm.bmWidth * 2)
    {
        unsigned int uiNewSize = mask.bm.bmWidth * mask.bm.bmHeight * 4 / 2;

        color.bm.bmBits = NULL;
        color.bm.bmBitsPixel = 32;
        color.bm.bmHeight = mask.bm.bmHeight / 2;
        color.bm.bmWidth = mask.bm.bmWidth;
        color.bm.bmPlanes = 1;
        color.bm.bmType = 0;
        color.bm.bmWidthBytes = color.bm.bmWidth * 4;

        color.buffer.SetSize(uiNewSize);

        unsigned char* pColor = static_cast<unsigned char*>(color.buffer.GetData());
        unsigned char* pMask = static_cast<unsigned char*>(mask.buffer.GetData());
        int maskBit = 7;
        unsigned int uiNumPixels = color.bm.bmWidth * color.bm.bmHeight;

        xorValue = true;

        pColor = static_cast<unsigned char*>(color.buffer.GetData());
        pMask = static_cast<unsigned char*>(mask.buffer.GetData());
        maskBit = 7;

        for (unsigned int x = 0; x < uiNumPixels; ++x)
        {
            *(pColor) = 0;
            *(pColor + 1) = 0;
            *(pColor + 2) = 0;

            int m1 = (*pMask >> maskBit) & 1;
            int m2 = (*(pMask + uiNumPixels / 8) >> maskBit) & 1;

            if (m1 == 1 && m2 == 0)
            {
                *(pColor + 3) = 0;
            }
            else if (m1 == 1 && m2 == 1)
            {
                *(pColor + 3) = 255;
            }
            else if (m1 == 0 && m2 == 0)
            {
                *(pColor + 3) = 255;
            }
            else if (m1 == 0 && m2 == 1)
            {
                *(pColor) = 255;
                *(pColor + 1) = 255;
                *(pColor + 2) = 255;

                *(pColor + 3) = 255;

                xorValue = false;
            }

            maskBit--;
            if (maskBit == -1)
            {
                pMask++;
                maskBit = 7;
            }
            pColor += 4;
        }

        return true;
    }
    return false;
}
