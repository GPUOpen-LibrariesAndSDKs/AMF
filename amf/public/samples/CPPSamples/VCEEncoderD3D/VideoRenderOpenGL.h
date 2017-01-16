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

#pragma once

#include "VideoRender.h"
#include <gl/gl.h>
#include <gl/glext.h>

class VideoRenderOpenGL : public VideoRender
{
public:
    VideoRenderOpenGL(amf_int width, amf_int height, bool bInterlaced, amf_int frames, amf::AMFContext* pContext);
    virtual ~VideoRenderOpenGL();

    virtual AMF_RESULT              Init(HWND hWnd, bool bFullScreen);
    virtual AMF_RESULT              Terminate();
    virtual AMF_RESULT              Render(amf::AMFData** ppData);
    virtual amf::AMF_SURFACE_FORMAT GetFormat() { return amf::AMF_SURFACE_BGRA; }

protected:
    AMF_RESULT InitFunctionPtrs();

    AMF_RESULT RenderScene(bool bRotate);
    //D3Window                                m_Window;

    HDC                                 m_hDC;
    HWND                                m_hWnd;
    HGLRC                               m_hContextOGL;

    GLuint                              m_frameBufferName;
    GLuint                              m_depthStencilName;

    PFNGLBINDFRAMEBUFFEREXTPROC         m_glBindFramebufferEXT;
    PFNGLFRAMEBUFFERTEXTURE2DEXTPROC    m_glFramebufferTexture2DEXT;
    PFNGLGENFRAMEBUFFERSEXTPROC         m_glGenFramebuffersEXT;
    PFNGLDELETEFRAMEBUFFERSPROC         m_glDeleteFramebuffersEXT;

    PFNGLGENRENDERBUFFERSEXTPROC        m_glGenRenderbuffersEXT;
    PFNGLBINDRENDERBUFFEREXTPROC        m_glBindRenderbufferEXT;
    PFNGLRENDERBUFFERSTORAGEEXTPROC     m_glRenderbufferStorageEXT;
    PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC m_glFramebufferRenderbufferEXT;
    PFNGLDELETERENDERBUFFERSEXTPROC     m_glDeleteRenderbuffersEXT;
};

