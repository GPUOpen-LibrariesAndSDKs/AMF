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

#include "SwapChain.h"
#include "public/common/AMFSTL.h"
#include "public/common/OpenGLImportTable.h"

class OpenGLDLLContext
{
public:
    OpenGLDLLContext() : m_pImportTable(nullptr) {}
    virtual ~OpenGLDLLContext() { Terminate(); }

    virtual AMF_RESULT Init(const OpenGLImportTable* pTable) { m_pImportTable = pTable; return AMF_OK; }
    virtual AMF_RESULT Init(const OpenGLDLLContext& other) { return Init(other.GetOpenGL()); }
    virtual AMF_RESULT Terminate() { m_pImportTable = nullptr; return AMF_OK; }

protected:
    const OpenGLImportTable* GetOpenGL() const { return m_pImportTable; }
private:
    const OpenGLImportTable* m_pImportTable;
};

struct BackBufferOpenGL : public BackBufferBase
{

    amf_uint                        frameBuffer;
    void*                           texture;  // Unused with default framebuffer
    amf::AMFSurfacePtr              pSurface; // Unused with default framebuffer. Holds access to texture
    AMFSize                         size;

    virtual void*                   GetNative()     const override { return texture; }
    virtual amf::AMF_MEMORY_TYPE    GetMemoryType() const override { return amf::AMF_MEMORY_OPENGL; }
    AMFSize                         GetSize()       const override { return size; }

};

#define GL_FORMAT_UNKNOWN 0

#ifdef _WIN32
    typedef HGLRC       OGLContext;
    typedef HDC         OGLDrawable;

#define NULL_OGL_DRAWABLE nullptr
#elif __ANDROID__
    typedef EGLContext  OGLContext;
    typedef EGLSurface  OGLDrawable;

#define NULL_OGL_DRAWABLE nullptr
#elif __linux
    typedef GLXContext  OGLContext;
    typedef GLXDrawable OGLDrawable;

#define NULL_OGL_DRAWABLE 0
#endif

class SwapChainOpenGL : public SwapChain, protected OpenGLDLLContext
{
public:
    typedef BackBufferOpenGL BackBuffer;

    SwapChainOpenGL(amf::AMFContext* pContext);
    virtual                             ~SwapChainOpenGL();

    virtual AMF_RESULT                  Init(amf_handle hwnd, amf_handle hDisplay, amf::AMFSurface* pSurface, amf_int32 width, amf_int32 height,
                                             amf::AMF_SURFACE_FORMAT format, amf_bool fullscreen = false, amf_bool hdr = false, amf_bool stereo = false) override;

    virtual AMF_RESULT                  Terminate() override;

    virtual AMF_RESULT                  Present(amf_bool waitForVSync) override;

    virtual amf_uint                    GetBackBufferCount() const override                                 { return BACK_BUFFER_COUNT; }
    virtual amf_uint                    GetBackBuffersAcquireable() const override                          { return BACK_BUFFER_COUNT; }
    virtual amf_uint                    GetBackBuffersAvailable() const override                            { return m_acquired ? 0 : 1; }

    virtual AMF_RESULT                  AcquireNextBackBufferIndex(amf_uint& index) override;
    virtual AMF_RESULT                  DropLastBackBuffer() override;
    virtual AMF_RESULT                  DropBackBufferIndex(amf_uint index) override;

    // Leave format as AMF_SURFACE_UNKNOWN to keep format
    // width
    virtual AMF_RESULT                  Resize(amf_int32 width, amf_int32 height, amf_bool fullscreen, 
                                               amf::AMF_SURFACE_FORMAT format=amf::AMF_SURFACE_UNKNOWN) override;
    virtual AMFSize                     GetSize() override;

    // Formats
    virtual amf_bool                    FormatSupported(amf::AMF_SURFACE_FORMAT format) override;
    virtual amf_uint                    GetOGLFormat() const                                                { return m_oglFormat; };

    // HDR
    virtual amf_bool                    HDRSupported() override                                             { return false; }
    virtual AMF_RESULT                  SetHDRMetaData(const AMFHDRMetadata* /*pHDRMetaData*/) override     { return AMF_NOT_IMPLEMENTED; }

    // Stereo 3D
    virtual amf_bool                    StereoSupported()  override                                         { return false; }

    static constexpr amf_uint BACK_BUFFER_COUNT = 1;
protected:
    virtual AMF_RESULT                  SetFormat(amf::AMF_SURFACE_FORMAT format) override;
    virtual amf_uint                    GetSupportedOGLFormat(amf::AMF_SURFACE_FORMAT format) const;

    virtual AMF_RESULT                  UpdateCurrentOutput() override;

    virtual const BackBufferBasePtr*    GetBackBuffers() const override                                     { return &m_pBackBuffer; }
    virtual AMF_RESULT                  BackBufferToSurface(const BackBufferBase* pBuffer, amf::AMFSurface** ppSurface) const override;

    amf_uint                            m_oglFormat;

    BackBufferBasePtr                   m_pBackBuffer;
    amf_bool                            m_acquired;
    OpenGLImportTable                   m_importTable;
};

#if defined(__linux) && !defined(__ANDROID__)
class XDisplayLock
{
    Display* m_hDisplay;
public:
    XDisplayLock(Display* hDisplay) : m_hDisplay(hDisplay)
    {
        if (hDisplay != nullptr)
        {
            XLockDisplay(hDisplay);
        }
    }

    ~XDisplayLock()
    {
        if (m_hDisplay != nullptr)
        {
            XUnlockDisplay(m_hDisplay);
        }
    }
};
#endif

inline const wchar_t* GetOpenGLFrameBufferStatusName(amf_uint status)
{
#define NAME_ENTRY(x) case x: return L###x
    switch (status)
    {
        NAME_ENTRY(GL_FRAMEBUFFER_UNDEFINED);
        NAME_ENTRY(GL_FRAMEBUFFER_COMPLETE);
        NAME_ENTRY(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT);
        NAME_ENTRY(GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT);
        NAME_ENTRY(GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER);
        NAME_ENTRY(GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER);
        NAME_ENTRY(GL_FRAMEBUFFER_UNSUPPORTED);
        NAME_ENTRY(GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE);
        NAME_ENTRY(GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS);
    default: return L"UNKNOWN_STATUS";
    }
#undef NAME_ENTRY
}

inline const wchar_t* GetOpenGLErrorMessage(amf_uint res)
{
#define MESSAGE_ENTRY(x) case x: return L###x
    switch (res)
    {
        MESSAGE_ENTRY(GL_NO_ERROR);
        MESSAGE_ENTRY(GL_INVALID_ENUM);
        MESSAGE_ENTRY(GL_INVALID_VALUE);
        MESSAGE_ENTRY(GL_INVALID_OPERATION);
        MESSAGE_ENTRY(GL_STACK_OVERFLOW);
        MESSAGE_ENTRY(GL_STACK_UNDERFLOW);
        MESSAGE_ENTRY(GL_OUT_OF_MEMORY);
    default:
        return L"GL_UNKNOWN_ERROR";
    }
#undef MESSAGE_ENTRY
}

inline amf::amf_list<amf_uint> QueryOpengGLErrors(glGetError_fn glGetErrorFunc)
{
    amf::amf_list<amf_uint> errorList;

    if (glGetErrorFunc == nullptr)
    {
        return errorList;
    }

    amf_uint err = 0;
    constexpr amf_uint WATCHDOG_LIMIT = 20; // Just in case
    while ((err = glGetErrorFunc()) != GL_NO_ERROR && errorList.size() < WATCHDOG_LIMIT)
    {
        // Repeated error means we are about to fall into infinite loop
        if (errorList.empty() == false && err == errorList.back())
        {
            return errorList;
        }
        errorList.push_back(err);
    }
    return errorList;
}

inline void ClearOpenGLErrors(glGetError_fn glGetErrorFunc) { QueryOpengGLErrors(glGetErrorFunc); }

inline amf_wstring QueryOpenGLErrorMessages(glGetError_fn glGetErrorFunc)
{
    amf_wstring message;
    amf::amf_list<amf_uint>  errors = QueryOpengGLErrors(glGetErrorFunc);
    for(amf_uint err : errors)
    {
        if (message.empty() == false)
        {
            message.append(L";");
        }
        message.append(GetOpenGLErrorMessage(err));
    }

    return message;
}

inline bool        AMFOpenGLSucceeded(const amf_wstring& errorMessage)   { return errorMessage.empty(); }
inline amf_wstring AMFFormatOpenGLError(const amf_wstring& errorMessage) { return amf::amf_string_format(L"OpenGL failed, %s:", errorMessage.c_str()); }
#define AMF_RETURN_IF_OGL_FAILED(...) AMF_BASE_RETURN(QueryOpenGLErrorMessages(GetOpenGL()->glGetError), amf_wstring, AMFOpenGLSucceeded, AMFFormatOpenGLError,\
                                                      AMF_TRACE_ERROR, AMF_FACILITY, AMF_GLX_FAILED, ##__VA_ARGS__)

#define GL_CALL(exp) \
    exp;\
    AMF_RETURN_IF_OGL_FAILED(L###exp L" failed");

#define GL_CALL_DLL(exp) GetOpenGL()->GL_CALL(exp);

template<typename T>
T CastPointer(void* p)
{
    return static_cast<T>(reinterpret_cast<uintptr_t>(p));
}