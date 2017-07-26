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

#include "VideoRenderOpenGL.h"
#include "../common/CmdLogger.h"

bool ReportGLErrors(const char* file, int line)
{
    unsigned int err = glGetError();
    if(err != GL_NO_ERROR)
    {
        std::wstringstream ss;
        ss << "GL failed. Error numbers: ";
        while(err != GL_NO_ERROR)
        {
            ss  << std::hex << err << ", ";
            err = glGetError();
        }
        ss << std::dec << " file: " << file << ", line: " << line;
        LOG_ERROR(ss.str());
        return false;
    }
    return true;
}

#define CALL_GL(exp) \
{ \
    exp; \
    if( !ReportGLErrors(__FILE__, __LINE__) ) return AMF_FAIL; \
}


VideoRenderOpenGL::VideoRenderOpenGL(amf_int width, amf_int height, bool bInterlaced, amf_int frames, amf::AMFContext* pContext)
    :VideoRender(width, height, bInterlaced, frames, pContext),
    m_hDC(NULL),
    m_hWnd(NULL),
    m_hContextOGL(0),
    m_frameBufferName(0),
    m_depthStencilName(0)

{
}

VideoRenderOpenGL::~VideoRenderOpenGL()
{
    Terminate();
}

AMF_RESULT VideoRenderOpenGL::Init(HWND hWnd, bool bFullScreen)
{
    AMF_RESULT res = AMF_OK;

    m_hDC = (HDC)m_pContext->GetOpenGLDrawable();
    m_hContextOGL = (HGLRC)m_pContext->GetOpenGLContext();
    if(m_hWnd != ::GetDesktopWindow())
    {
        m_hWnd = hWnd; // windowed mode
    }
    BOOL glRET = wglMakeCurrent(m_hDC, m_hContextOGL);
    if(!glRET) 
    {
        LOG_ERROR("wglMakeCurrent()  failed. Error: " << GetLastError());
        return AMF_FAIL;
    }
    InitFunctionPtrs();


    CALL_GL(glEnable(GL_DEPTH_TEST));

    CALL_GL(m_glGenFramebuffersEXT(1, &m_frameBufferName));
    CALL_GL(m_glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, m_frameBufferName));

    CALL_GL(m_glGenRenderbuffersEXT(1, &m_depthStencilName));
    CALL_GL(m_glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, m_depthStencilName));
    CALL_GL(m_glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH24_STENCIL8_EXT, m_width, m_height));
    CALL_GL(m_glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, m_depthStencilName));
    CALL_GL(glViewport(0, 0, m_width, m_height));
    glRET = wglMakeCurrent(NULL, NULL);
    return AMF_OK;
}

AMF_RESULT VideoRenderOpenGL::Terminate()
{
    if(m_glDeleteFramebuffersEXT && m_frameBufferName) 
    {
        m_glDeleteFramebuffersEXT(1, &m_frameBufferName);
        m_frameBufferName = 0;
    }
    if(m_glBindFramebufferEXT)
    {
        m_glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
    }
    if(m_glDeleteRenderbuffersEXT && m_depthStencilName)
    {
        m_glDeleteRenderbuffersEXT(1, &m_depthStencilName);
        m_depthStencilName = 0;
    }
    return AMF_OK;
}

AMF_RESULT VideoRenderOpenGL::InitFunctionPtrs()
{
    m_glBindFramebufferEXT = (PFNGLBINDFRAMEBUFFEREXTPROC) wglGetProcAddress("glBindFramebufferEXT");
    if(!m_glBindFramebufferEXT)
    {
        LOG_ERROR("Could not get glBindFramebufferEXT proc address.");
        return AMF_FAIL;
    }
    m_glFramebufferTexture2DEXT = (PFNGLFRAMEBUFFERTEXTURE2DEXTPROC) wglGetProcAddress("glFramebufferTexture2DEXT");
    if(!m_glFramebufferTexture2DEXT)
    {
        LOG_ERROR("Could not get glFramebufferTexture2DEXT proc address.");
        return AMF_FAIL;
    }
    m_glGenFramebuffersEXT = (PFNGLGENFRAMEBUFFERSEXTPROC) wglGetProcAddress("glGenFramebuffersEXT");
    if(!m_glGenFramebuffersEXT) 
    {
        LOG_ERROR("Could not get glGenFramebuffersEXT proc address.");
        return AMF_FAIL;
    }
    m_glDeleteFramebuffersEXT = (PFNGLDELETEFRAMEBUFFERSEXTPROC) wglGetProcAddress("glDeleteFramebuffersEXT");
	if (!m_glDeleteFramebuffersEXT)
    {
        LOG_ERROR("Could not get glDeleteFramebuffersEXT proc address.");
        return AMF_FAIL;
    }
    m_glGenRenderbuffersEXT = (PFNGLGENRENDERBUFFERSEXTPROC) wglGetProcAddress("glGenRenderbuffersEXT");
    if( !m_glGenRenderbuffersEXT) 
    {
        LOG_ERROR("Could not get m_glGenRenderbuffersEXT proc address.");
        return AMF_FAIL;
    }
    m_glBindRenderbufferEXT = (PFNGLBINDRENDERBUFFEREXTPROC) wglGetProcAddress("glBindRenderbufferEXT");
    if( !m_glBindRenderbufferEXT) 
    {
        LOG_ERROR("Could not get m_glBindRenderbufferEXT proc address.");
        return AMF_FAIL;
    }
    m_glRenderbufferStorageEXT = (PFNGLRENDERBUFFERSTORAGEEXTPROC) wglGetProcAddress("glRenderbufferStorageEXT");
    if( !m_glRenderbufferStorageEXT) 
    {
        LOG_ERROR("Could not get m_glRenderbufferStorageEXT proc address.");
        return AMF_FAIL;
    }
    m_glFramebufferRenderbufferEXT = (PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC) wglGetProcAddress("glFramebufferRenderbufferEXT");
    if( !m_glFramebufferRenderbufferEXT) 
    {
        LOG_ERROR("Could not get m_glFramebufferRenderbufferEXT proc address.");
        return AMF_FAIL;
    }
    m_glDeleteRenderbuffersEXT = (PFNGLDELETERENDERBUFFERSEXTPROC) wglGetProcAddress("glDeleteRenderbuffersEXT");
    if( !m_glDeleteRenderbuffersEXT) 
    {
        LOG_ERROR("Could not get m_glDeleteRenderbuffersEXT proc address.");
        return AMF_FAIL;
    }

    return AMF_OK;
}
AMF_RESULT VideoRenderOpenGL::Render(amf::AMFData** ppData)
{
#if !defined(_WIN64 )
// this is done to get identical results on 32 nad 64 bit builds
    _controlfp(_PC_24, MCW_PC);
#endif

    AMF_RESULT res = AMF_OK;
    HRESULT hr = S_OK;
    amf::AMFSurfacePtr pSurface;
    res = m_pContext->AllocSurface(amf::AMF_MEMORY_OPENGL, amf::AMF_SURFACE_BGRA, m_width, m_height, &pSurface);
    CHECK_AMF_ERROR_RETURN(res, L"AMFSurfrace::AllocSurface() failed");

    amf::AMFContext::AMFOpenGLLocker locker(m_pContext);

    GLuint texture = (GLuint)(amf_size)(pSurface->GetPlaneAt(0)->GetNative());


    // render to texture 
    CALL_GL(m_glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, m_frameBufferName));
    CALL_GL(m_glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, texture, 0));

    res = RenderScene(true);
    CHECK_AMF_ERROR_RETURN(res, L"RenderScene() failed");

    CALL_GL(m_glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0));
    // render to window
    if(m_hWnd != NULL)
    {
        res = RenderScene(false); // second call  - do not animate
        CHECK_AMF_ERROR_RETURN(res, L"RenderScene() failed");
        ::SwapBuffers(m_hDC);
    }
    *ppData = pSurface.Detach();
    return AMF_OK;
}


AMF_RESULT VideoRenderOpenGL::RenderScene(bool bRotate)
{
    CALL_GL(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
    CALL_GL(glClearDepth( 1.0f)); 
    CALL_GL(glClearStencil(0));
    CALL_GL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT));
    if(bRotate)
    {
        CALL_GL(glRotatef(1.2f, 0.4f, 1.0f, 0.7f));
    }
    static GLfloat n[6][3] = 
    {  
        {-1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0},
        {0.0, -1.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, 0.0, -1.0} 
    };
    static GLint faces[6][4] = 
    {
        {0, 1, 2, 3}, {3, 2, 6, 7}, {7, 6, 5, 4},
        {4, 5, 1, 0}, {5, 6, 2, 1}, {7, 4, 0, 3} 
    };
    static GLfloat v[8][3] = 
    {
        {-0.3f, -0.3f, 0.3f},
        {-0.3f, -0.3f, -0.3f},
        {-0.3f, 0.3f, -0.3f},
        {-0.3f, 0.3f, 0.3f},
        {0.3f, -0.3f, 0.3f},
        {0.3f, -0.3f, -0.3f},
        {0.3f, 0.3f, -0.3f},
        {0.3f, 0.3f, 0.3f}
    };

    static GLfloat color[6][3] = 
    { 
        {1.0, 0.0, 0.0},
        {1.0, 1.0, 0.0},
        {1.0, 0.0, 1.0},
        {0.0, 1.0, 0.0},
        {0.0, 1.0, 1.0},
        {0.0, 0.0, 1.0}
    };
    for (int i = 0; i < 6; i++) 
    {
        CALL_GL(glColor3f( color[i][0] , color[i][1], color[i][2] ));

        glBegin(GL_QUADS);
            glNormal3fv(&n[i][0]);
            glVertex3fv(&v[faces[i][0]][0]);
            glVertex3fv(&v[faces[i][1]][0]);
            glVertex3fv(&v[faces[i][2]][0]);
            glVertex3fv(&v[faces[i][3]][0]);
        CALL_GL(glEnd());
    }
    CALL_GL(glFinish());
    return AMF_OK;
}

