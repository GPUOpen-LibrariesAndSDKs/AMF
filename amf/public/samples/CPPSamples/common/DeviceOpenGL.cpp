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
#include "DeviceOpenGL.h"
#include "CmdLogger.h"
#include <set>

#pragma comment(lib, "opengl32.lib")


static bool ReportGLErrors(const char* file, int line)
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

DeviceOpenGL::DeviceOpenGL() : 
    m_hDC(NULL),
    m_hContextOGL(NULL),
    m_hWnd(NULL),
    m_adaptersCount(0)
{
    memset(m_adaptersIndexes, 0, sizeof(m_adaptersIndexes));
}

DeviceOpenGL::~DeviceOpenGL()
{
    Terminate();
}

AMF_RESULT DeviceOpenGL::Init(HWND hWnd, const wchar_t* displayDeviceName)
{
    DWORD error = 0;

    if(hWnd == ::GetDesktopWindow() && displayDeviceName)
    {
        LOG_INFO("OPENGL : Chosen Device " << displayDeviceName);
        m_hDC = CreateDC(displayDeviceName, NULL, NULL, NULL);
    }
    else
    {
        m_hWnd = hWnd;
        m_hDC = GetDC(hWnd);
    }
    if(!m_hDC) 
    {
        LOG_ERROR("Could not GetDC. Error: " << GetLastError());
        return AMF_FAIL;
    }

    static    PIXELFORMATDESCRIPTOR pfd=                // pfd Tells Windows How We Want Things To Be
    {
        sizeof(PIXELFORMATDESCRIPTOR),                // Size Of This Pixel Format Descriptor
        1,                                            // Version Number
        PFD_DRAW_TO_WINDOW |                        // Format Must Support Window
        PFD_SUPPORT_OPENGL |                        // Format Must Support OpenGL
        PFD_DOUBLEBUFFER,                            // Must Support Double Buffering
        PFD_TYPE_RGBA,                                // Request An RGBA Format
        24,                                            // Select Our Color Depth
        0, 0, 0, 0, 0, 0,                            // Color Bits Ignored
        0,                                            // No Alpha Buffer
        0,                                            // Shift Bit Ignored
        0,                                            // No Accumulation Buffer
        0, 0, 0, 0,                                    // Accumulation Bits Ignored
        16,                                            // 16Bit Z-Buffer (Depth Buffer)  
        0,                                            // No Stencil Buffer
        0,                                            // No Auxiliary Buffer
        PFD_MAIN_PLANE,                                // Main Drawing Layer
        0,                                            // Reserved
        0, 0, 0                                        // Layer Masks Ignored
    };

    GLuint pixelFormat = (GLuint)ChoosePixelFormat(m_hDC, &pfd);

    BOOL res = SetPixelFormat(m_hDC, (int)pixelFormat, &pfd);
    if(!res) 
    {
        LOG_ERROR("SetPixelFormat() failed. Error: " << GetLastError());
        return AMF_FAIL;
    }

    m_hContextOGL= wglCreateContext(m_hDC);
    if(!m_hContextOGL) 
    {
        LOG_ERROR("wglCreateContext()  failed. Error: " << GetLastError());
        return AMF_FAIL;
    }
    return AMF_OK;
}

AMF_RESULT DeviceOpenGL::Terminate()
{
    if(m_hContextOGL)
    {
        wglMakeCurrent( NULL , NULL );
        wglDeleteContext(m_hContextOGL);
        m_hContextOGL = NULL;
    }
    if(m_hDC)
    {
        if(m_hWnd == NULL)
        {
            ::DeleteDC(m_hDC);
        }
        else
        {
            ::ReleaseDC(m_hWnd, m_hDC);
        }
        m_hDC = NULL;
        m_hWnd = NULL;
    }
    return AMF_OK;
}
