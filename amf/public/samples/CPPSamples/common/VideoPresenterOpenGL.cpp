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
#include "VideoPresenterOpenGL.h"

#include <gl\GL.h>
#include <gl\GLU.h>
#pragma comment(lib, "opengl32.lib")

VideoPresenterOpenGL::VideoPresenterOpenGL(HWND hwnd, amf::AMFContext* pContext) :
    BackBufferPresenter(hwnd, pContext),
    m_initialized(false)
{
}

VideoPresenterOpenGL::~VideoPresenterOpenGL()
{
    Terminate();
}

AMF_RESULT VideoPresenterOpenGL::Present(amf::AMFSurface* pSurface)
{
    AMF_RESULT err = AMF_OK;

    const amf::AMF_SURFACE_FORMAT surfaceType = pSurface->GetFormat();

    err = pSurface->Convert(amf::AMF_MEMORY_OPENGL);
    if(err != AMF_OK)
    {
        return err;
    }

    amf::AMFContext::AMFOpenGLLocker glLocker(m_pContext);

    RECT tmpRectClient = {0, 0, 500, 500};
    BOOL getWindowRectResult = GetClientRect(m_hwnd, &tmpRectClient);
    AMFRect rectClient = AMFConstructRect(tmpRectClient.left, tmpRectClient.top, tmpRectClient.right, tmpRectClient.bottom);

    glViewport(0, 0, rectClient.right, rectClient.bottom);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    amf::AMFPlane* pPlane = pSurface->GetPlane(amf::AMF_PLANE_PACKED);

    AMFRect srcRect = {pPlane->GetOffsetX(), pPlane->GetOffsetY(), pPlane->GetOffsetX() + pPlane->GetWidth(), pPlane->GetOffsetY() + pPlane->GetHeight()};
    AMFRect outputRect;

    if(err == AMF_OK)
    {
        err = CalcOutputRect(&srcRect, &rectClient, &outputRect);
    }
    if(err == AMF_OK)
    {
        WaitForPTS(pSurface->GetPts());

        const float left =      2.f / rectClient.Width() * ((float)rectClient.Width() / 2 - (float)outputRect.left);
        const float right =     2.f / rectClient.Width() * ((float)rectClient.Width() / 2 - (float)outputRect.right);
        const float top =       2.f / rectClient.Height() * ((float)rectClient.Height() / 2 - (float)outputRect.top);
        const float bottom =    2.f / rectClient.Height() * ((float)rectClient.Height() / 2 - (float)outputRect.bottom);

        glEnable(GL_TEXTURE_2D);

        glBindTexture(GL_TEXTURE_2D, (GLuint)(amf_size)(pPlane->GetNative()));
        glBegin(GL_QUADS);

        glTexCoord2f(1.0, 1.0);
        glVertex2f(left, bottom);
        glTexCoord2f(0.0, 1.0);
        glVertex2f(right, bottom);
        glTexCoord2f(0.0, 0.0);
        glVertex2f(right, top);
        glTexCoord2f(1.0, 0.0);
        glVertex2f(left, top);
        glEnd();

    }

    BOOL swapResult = ::SwapBuffers((HDC)m_pContext->GetOpenGLDrawable());
    return AMF_OK;
}

AMF_RESULT VideoPresenterOpenGL::Init(amf_int32 width, amf_int32 height)
{
    AMF_RESULT res = AMF_OK;

    VideoPresenter::Init(width, height);
    m_initialized = true;
    return res;
}

AMF_RESULT VideoPresenterOpenGL::Terminate()
{
    return VideoPresenter::Terminate();
}
