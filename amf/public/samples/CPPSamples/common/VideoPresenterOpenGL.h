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
#pragma once

#include "VideoPresenter.h"
#include "SwapChainOpenGL.h"

struct GLSamplerParams
{
    amf_uint    minFilter;
    amf_uint    magFilter;

    amf_float   minLOD;
    amf_float   maxLOD;

    amf_uint    wrapS;
    amf_uint    wrapT;
    amf_uint    wrapR;

    amf_float   borderColor[4];

    amf_uint    compareMode;
    amf_uint    compareFunc;

    GLSamplerParams() :
        minFilter(GL_NEAREST_MIPMAP_LINEAR),
        magFilter(GL_LINEAR),
        minLOD(-1000.0f),
        maxLOD(1000.0f),
        wrapS(GL_REPEAT),
        wrapT(GL_REPEAT),
        wrapR(GL_REPEAT),
        borderColor{ 0.0f, 0.0f, 0.0f, 0.0f },
        compareMode(GL_NONE),
        compareFunc(GL_NEVER)
    {
    }
};

struct GLVertexBuffer
{
    amf_uint vbo;
    amf_uint vao;
};

struct GLVertexAttribute
{
    amf_uint    size;
    amf_uint    type;
    amf_bool    normalized;
    amf_size    stride;
    amf_size    offset;
};

class VideoPresenterOpenGL : public VideoPresenter, public OpenGLDLLContext
{
public:
    VideoPresenterOpenGL(amf_handle hwnd, amf::AMFContext* pContext, amf_handle hDisplay=nullptr);
    virtual                             ~VideoPresenterOpenGL();

    virtual bool                        SupportAllocator() const { return false; }
    virtual amf::AMF_MEMORY_TYPE        GetMemoryType() const { return amf::AMF_MEMORY_OPENGL; }

    virtual AMF_RESULT                  Init(amf_int32 width, amf_int32 height, amf::AMFSurface* pSurface);
    virtual AMF_RESULT                  Terminate();

protected:
    typedef SwapChainOpenGL::BackBuffer RenderTarget;

    virtual AMF_RESULT                  CreateQuadPipeline();
    virtual AMF_RESULT                  SetupQuadPipelineUniforms();

    virtual AMF_RESULT                  RenderSurface(amf::AMFSurface* pSurface, const RenderTargetBase* pRenderTarget, RenderViewSizeInfo& renderView) override;
    virtual AMF_RESULT                  UpdateStates(amf::AMFSurface* pSurface, const RenderTarget* pRenderTarget, RenderViewSizeInfo& renderView);
    virtual AMF_RESULT                  DrawBackground(const RenderTarget* pRenderTarget);
    virtual AMF_RESULT                  SetStates(amf_uint texture);
    virtual AMF_RESULT                  SetPipStates(amf_uint texture);
    virtual AMF_RESULT                  DrawFrame(const RenderTarget* pRenderTarget);
    virtual AMF_RESULT                  DrawOverlay(amf::AMFSurface* /* pSurface */, const RenderTarget* /*pRenderTarget*/) { return AMF_OK; }

    virtual AMF_RESULT                  OnRenderViewResize(const RenderViewSizeInfo& newRenderView) override;

    AMF_RESULT                          CreateShader(const char* const pShaderData, amf_int size, amf_uint type, amf_uint& shader) const;
    AMF_RESULT                          CreateProgram(const amf_uint* pShaders, amf_size count, amf_uint& program) const;
    AMF_RESULT                          ValidateProgram(amf_uint shader) const;
    AMF_RESULT                          CreatePipeline(const char* const* ppShaderSources, amf_uint count, const amf_size* pShaderSizes, const amf_uint* pShaderTypes, amf_uint* pShaders, amf_uint& program) const;
    AMF_RESULT                          CreateBuffer(amf_uint target, const void* pData, amf_size size, amf_uint usage, amf_uint& buffer) const;
    AMF_RESULT                          CreateVertexBuffer(const GLVertexAttribute* pAttribs, amf_uint attribCount, const void* pData, amf_size size, amf_uint usage, GLVertexBuffer& buffer) const;
    AMF_RESULT                          UpdateBuffer(amf_uint buffer, const void* pData, amf_size size, amf_size offset) const;
    AMF_RESULT                          CreateSampler(const GLSamplerParams& createInfo, amf_uint& sampler) const;
    AMF_RESULT                          CreateRenderTarget(RenderTarget* pTarget, amf_int32 width, amf_int32 height, amf::AMF_SURFACE_FORMAT format) const;
    AMF_RESULT                          DestroyRenderTarget(RenderTarget* pTarget) const;

    amf_uint                            m_quadVertShader;
    amf_uint                            m_quadFragShader;
    amf_uint                            m_quadProgram;
    amf_uint                            m_quadViewProjectionIndex;
    amf_uint                            m_quadSamplerIndex;

    static constexpr amf_uint           QUAD_VIEW_PROJECTION_BINDING = 0;
    static constexpr amf_uint           QUAD_SAMPLER_BINDING = 0;
    static const GLVertexAttribute      QUAD_VERTEX_ATTRIBUTES[2];

    amf_uint                            m_viewProjectionBuffer;

private:
    AMF_RESULT                          PrepareStates();

    GLVertexBuffer                      m_vertexBuffer;
    amf_uint                            m_pipViewProjectionBuffer;
    SamplerMap<amf_uint>                m_samplerMap;
};
