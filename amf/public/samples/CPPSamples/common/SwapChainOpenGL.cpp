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


#include "SwapChainOpenGL.h"
#include "public/common/TraceAdapter.h"
#include "public/include/core/Context.h"

using namespace amf;

#define AMF_FACILITY L"SwapChainOpenGL"

SwapChainOpenGL::SwapChainOpenGL(AMFContext* pContext) :
    SwapChain(pContext),
    m_oglFormat(GL_FORMAT_UNKNOWN),
    m_acquired(false)
{
    m_pBackBuffer = std::unique_ptr<BackBufferOpenGL>(new BackBufferOpenGL());
    SetFormat(AMF_SURFACE_UNKNOWN); // Set default format
    m_importTable.LoadFunctionsTable();
    OpenGLDLLContext::Init(&m_importTable);
}

SwapChainOpenGL::~SwapChainOpenGL()
{
    Terminate();
}

AMF_RESULT SwapChainOpenGL::Init(amf_handle hwnd, amf_handle hDisplay, AMFSurface* /*pSurface*/, amf_int32 width, amf_int32 height, AMF_SURFACE_FORMAT format, amf_bool fullscreen, amf_bool /*hdr*/, amf_bool /*stereo*/)
{
    AMF_RETURN_IF_FALSE(hwnd != nullptr, AMF_INVALID_ARG, L"Init() - hwnd is NULL");
#if defined(__linux)
    AMF_RETURN_IF_FALSE(hDisplay != nullptr, AMF_INVALID_ARG, L"Init() - hDisplay is NULL");
#endif

    m_hwnd = hwnd;
    m_hDisplay = hDisplay;

    AMF_RESULT res = Resize(width, height, fullscreen, format);
    AMF_RETURN_IF_FAILED(res, L"Init() - Resize() failed");

    return AMF_OK;
}

AMF_RESULT SwapChainOpenGL::Terminate()
{
    m_acquired = false;
    *((BackBuffer*)m_pBackBuffer.get()) = {};

    return SwapChain::Terminate();
}

AMF_RESULT SwapChainOpenGL::Resize(amf_int32 width, amf_int32 height, amf_bool /*fullscreen*/, AMF_SURFACE_FORMAT format)
{
    {
        AMFContext::AMFOpenGLLocker oglLock(m_pContext);
        GL_CALL_DLL(glFinish());
    }


    if (format != AMF_SURFACE_UNKNOWN)
    {
        AMF_RESULT res = SetFormat(format);
        AMF_RETURN_IF_FAILED(res, L"Resize() - SetFormat() failed");
    }

    //if (fullscreen == true)
    //{
    //    const AMFRect rect = GetOutputRect();
    //    width = rect.Width();
    //    height = rect.Height();
    //}

    if (width == 0 || height == 0)
    {
        const AMFRect clientRect = GetClientRect(m_hwnd, m_hDisplay);
        const amf_int32 clientWidth = clientRect.Width();
        const amf_int32 clientHeight = clientRect.Height();

        if (width == 0 && clientWidth != 0)
        {
            m_size.width = clientWidth;
        }

        if (height == 0 && clientHeight != 0)
        {
            m_size.height = clientHeight;
        }
    }
    else
    {
        m_size = AMFConstructSize(width, height);
    }

    m_acquired = false;
    BackBuffer* pBuffer = (BackBuffer*)m_pBackBuffer.get();
    *pBuffer = {};
    pBuffer->size = m_size;
    
    return AMF_OK;
}

AMFSize SwapChainOpenGL::GetSize()
{
    return m_size;
}

AMF_RESULT SwapChainOpenGL::Present(amf_bool waitForVSync)
{
#if defined(__ANDROID__) || defined(__linux)
    AMF_RETURN_IF_FALSE(m_hDisplay != nullptr, AMF_NOT_INITIALIZED, L"Present() - m_hDisplay is not initialized");
#endif

    if (m_acquired == false)
    {
        return AMF_OK;
    }

    AMFContext::AMFOpenGLLocker oglLock(m_pContext);

    OGLContext hContext = (OGLContext)m_pContext->GetOpenGLContext();
    AMF_RETURN_IF_FALSE(hContext != nullptr, AMF_NOT_INITIALIZED, L"Present() - OpenGL context is NULL");

    OGLDrawable hDrawable = (OGLDrawable)m_pContext->GetOpenGLDrawable();
    AMF_RETURN_IF_FALSE(hDrawable != NULL_OGL_DRAWABLE, AMF_NOT_INITIALIZED, L"Present() - OpenGL drawable is NULL");

    m_acquired = false;
    // Need to update viewport otherwise we get weird scaling
    GL_CALL_DLL(glViewport(0, 0, m_size.width, m_size.height));

    const amf_int swapInterval = waitForVSync ? 1 : 0;

#ifdef _WIN32
    if (GetOpenGL()->wglSwapIntervalEXT != nullptr)
    {
        GL_CALL_DLL(wglSwapIntervalEXT(swapInterval));
    }

    ::SwapBuffers(hDrawable);
#elif __ANDROID__
    EGLDisplay display = static_cast<EGLDisplay>(m_hDisplay);

    if (GetOpenGL()->eglSwapInterval != nullptr)
    {
        GL_CALL_DLL(eglSwapInterval(display, swapInterval));
    }
    
    eglSwapBuffers(display, hDrawable);
#elif __linux
    Display* pXDisplay = static_cast<Display*>(m_hDisplay);
    XDisplayLock xDisplayLock(pXDisplay);
    
    if (GetOpenGL()->glXSwapIntervalEXT != nullptr)
    {
        GL_CALL_DLL(glXSwapIntervalEXT(pXDisplay, hDrawable, swapInterval));
    }

    GL_CALL_DLL(glXSwapBuffers(pXDisplay, hDrawable));
#endif

    //GL_CALL_DLL(glFinish());

    return AMF_OK;
}

AMF_RESULT SwapChainOpenGL::AcquireNextBackBufferIndex(amf_uint& index)
{
    if (m_acquired)
    {
        return AMF_NEED_MORE_INPUT;
    }

    index = 0;
    m_acquired = true;
    return AMF_OK;
}

AMF_RESULT SwapChainOpenGL::DropLastBackBuffer()
{
    return DropBackBufferIndex(0);
}

AMF_RESULT SwapChainOpenGL::DropBackBufferIndex(amf_uint index)
{
    if (index >= BACK_BUFFER_COUNT)
    {
        return AMF_OK;
    }

    m_acquired = false;
    return AMF_OK;
}

amf_bool SwapChainOpenGL::FormatSupported(AMF_SURFACE_FORMAT format)
{
    return GetSupportedOGLFormat(format) != GL_FORMAT_UNKNOWN;
}

AMF_RESULT SwapChainOpenGL::SetFormat(AMF_SURFACE_FORMAT format)
{
    amf_uint oglFormat = GetSupportedOGLFormat(format);
    AMF_RETURN_IF_FALSE(oglFormat != GL_FORMAT_UNKNOWN, AMF_NOT_SUPPORTED, L"SetFormat() - Format (%s) not supported", AMFSurfaceGetFormatName(format));

    m_format = format == AMF_SURFACE_UNKNOWN ? AMF_SURFACE_BGRA : format;
    m_oglFormat = oglFormat;
    return AMF_OK;
}

amf_uint SwapChainOpenGL::GetSupportedOGLFormat(AMF_SURFACE_FORMAT format) const
{
    switch (format)
    {
    case AMF_SURFACE_UNKNOWN:   return GL_BGRA_EXT;
    case AMF_SURFACE_BGRA:      return GL_BGRA_EXT;
    case AMF_SURFACE_RGBA:      return GL_RGBA;
    }
    return GL_FORMAT_UNKNOWN;
}

AMF_RESULT SwapChainOpenGL::UpdateCurrentOutput()
{
    return AMF_OK;
}

AMF_RESULT SwapChainOpenGL::BackBufferToSurface(const BackBufferBase* /*pBuffer*/, amf::AMFSurface** /*ppSurface*/) const
{
    return AMF_NOT_SUPPORTED;
}
