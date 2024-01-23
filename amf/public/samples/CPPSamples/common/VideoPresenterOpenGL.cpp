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
#include "VideoPresenterOpenGL.h"
#include "public/common/TraceAdapter.h"

#include "QuadOpenGL.vert.h"
#include "QuadOpenGL.frag.h"

using namespace amf;

#define AMF_FACILITY L"VideoPresenterOpenGL"

const GLVertexAttribute VideoPresenterOpenGL::QUAD_VERTEX_ATTRIBUTES[] = 
{
    { 3, GL_FLOAT, false, sizeof(Vertex), offsetof(Vertex, pos) },
    { 2, GL_FLOAT, false, sizeof(Vertex), offsetof(Vertex, tex) }
};

VideoPresenterOpenGL::VideoPresenterOpenGL(amf_handle hwnd, amf::AMFContext* pContext, amf_handle hDisplay) :
    VideoPresenter(hwnd, pContext, hDisplay),
    m_quadVertShader(0),
    m_quadFragShader(0),
    m_quadProgram(0),
    m_quadViewProjectionIndex(0),
    m_quadSamplerIndex(0),
    m_vertexBuffer{},
    m_viewProjectionBuffer(0),
    m_pipViewProjectionBuffer(0)
{
    m_pSwapChain = std::unique_ptr<SwapChainOpenGL>(new SwapChainOpenGL(pContext));
    OpenGLDLLContext::Init((OpenGLDLLContext&)*((SwapChainOpenGL*)m_pSwapChain.get()));
}

VideoPresenterOpenGL::~VideoPresenterOpenGL()
{
    Terminate();
}

AMF_RESULT VideoPresenterOpenGL::Init(amf_int32 width, amf_int32 height, amf::AMFSurface* pSurface)
{
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_UNEXPECTED, L"Init() - m_pSwapChain is NULL");

    AMF_RESULT res = VideoPresenter::Init(width, height, pSurface);
    AMF_RETURN_IF_FAILED(res, L"Init() - VideoPresenter::Init() failed");

    res = m_pSwapChain->Init(m_hwnd, m_hDisplay, pSurface, width, height, GetInputFormat(), false, false, false);
    AMF_RETURN_IF_FAILED(res, L"Init() - SwapChain Init() failed");

    res = PrepareStates();
    AMF_RETURN_IF_FAILED(res, L"Init() - PrepareStates() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterOpenGL::Terminate()
{
    GetOpenGL()->glDeleteProgram(m_quadProgram); // Will detach shaders for deletion
    m_quadProgram = 0;

    GetOpenGL()->glDeleteShader(m_quadVertShader);
    m_quadVertShader = 0;

    GetOpenGL()->glDeleteShader(m_quadFragShader);
    m_quadFragShader = 0;

    m_quadViewProjectionIndex = 0;
    m_quadSamplerIndex = 0;
    
    const amf_uint buffers[] = { m_vertexBuffer.vbo, m_viewProjectionBuffer, m_pipViewProjectionBuffer };
    GetOpenGL()->glDeleteBuffers(amf_countof(buffers), buffers); // Silently ignores '0' and unnamed buffers
    
    const amf_uint vertexArrays[] = { m_vertexBuffer.vao };
    GetOpenGL()->glDeleteVertexArrays(amf_countof(vertexArrays), vertexArrays);

    m_vertexBuffer = {};
    m_viewProjectionBuffer = 0;
    m_pipViewProjectionBuffer = 0;

    amf_uint samplers[] = { m_samplerMap[InterpolationLinear], m_samplerMap[InterpolationPoint] };
    GetOpenGL()->glDeleteSamplers(amf_countof(samplers), samplers); // Silently ignores '0' and unnamed samplers
    m_samplerMap.clear();

    return VideoPresenter::Terminate();
}

AMF_RESULT VideoPresenterOpenGL::RenderSurface(amf::AMFSurface* pSurface, const RenderTargetBase* pRenderTarget, RenderViewSizeInfo& renderView)
{
    AMF_RETURN_IF_FALSE(pSurface != nullptr, AMF_INVALID_ARG, L"RenderSurface() - pSrcSurface is NULL");

    RenderTarget* pTarget = (RenderTarget*)pRenderTarget;
    AMF_RETURN_IF_FALSE(pTarget != nullptr, AMF_INVALID_ARG, L"RenderSurface() - pRenderTarget is NULL");

    AMFContext::AMFOpenGLLocker oglLock(m_pContext);

    amf_uint glTexture = CastPointer<amf_uint>(GetPackedSurfaceOpenGL(pSurface));
    AMF_RETURN_IF_FALSE(glTexture != 0, AMF_INVALID_ARG, L"RenderSurface() - glTexture ID was 0");

    AMF_RESULT res = UpdateStates(pSurface, pTarget, renderView);
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - UpdateStates() failed");

    res = DrawBackground(pTarget); // Clears the background
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - DrawBackground() failed");

    res = SetStates(glTexture);
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - SetStates() failed");

    res = DrawFrame(pTarget);
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - DrawFrame() failed");

    res = DrawOverlay(pSurface, pTarget);
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - DrawOverlay() failed");

    if (m_enablePIP)
    {
        res = SetPipStates(glTexture);
        AMF_RETURN_IF_FAILED(res, L"RenderSurface() - SetPipStates() failed");

        res = DrawFrame(pTarget);
        AMF_RETURN_IF_FAILED(res, L"RenderSurface() - DrawFrame() PIP failed");
    }

    return AMF_OK;
}

AMF_RESULT VideoPresenterOpenGL::UpdateStates(amf::AMFSurface* /*pSurface*/, const RenderTarget* /*pRenderTarget*/, RenderViewSizeInfo& renderView)
{
    AMF_RESULT res = ResizeRenderView(renderView);
    AMF_RETURN_IF_FAILED(res, L"UpdateStates() - ResizeRenderView() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterOpenGL::DrawBackground(const RenderTarget* pRenderTarget)
{
    AMF_RETURN_IF_FALSE(pRenderTarget != nullptr, AMF_INVALID_ARG, L"DrawBackground() - pRenderTarget is NULL");
    
    GL_CALL_DLL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, pRenderTarget->frameBuffer));

    GL_CALL_DLL(glClearColor(ClearColor[0], ClearColor[1], ClearColor[2], ClearColor[3]));
    GL_CALL_DLL(glClearDepth(1.0));
    GL_CALL_DLL(glClearStencil(0));
    GL_CALL_DLL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT));

    return AMF_OK;
}

AMF_RESULT VideoPresenterOpenGL::SetStates(amf_uint texture)
{
    GL_CALL_DLL(glUseProgram(m_quadProgram));

    GL_CALL_DLL(glUniform1i(m_quadSamplerIndex, QUAD_SAMPLER_BINDING))
    GL_CALL_DLL(glActiveTexture(GL_TEXTURE_UNIT(QUAD_SAMPLER_BINDING)));
    GL_CALL_DLL(glBindTexture(GL_TEXTURE_2D, texture));
    GL_CALL_DLL(glBindSampler(QUAD_SAMPLER_BINDING, m_samplerMap[m_interpolation]));

    GL_CALL_DLL(glBindBufferBase(GL_UNIFORM_BUFFER, QUAD_VIEW_PROJECTION_BINDING, m_viewProjectionBuffer));
    GL_CALL_DLL(glBindVertexArray(m_vertexBuffer.vao));

    return AMF_OK;
}

AMF_RESULT VideoPresenterOpenGL::SetPipStates(amf_uint texture)
{
    SetStates(texture);
    GL_CALL_DLL(glBindSampler(QUAD_SAMPLER_BINDING, m_samplerMap[PIP_INTERPOLATION]));
    GL_CALL_DLL(glBindBufferBase(GL_UNIFORM_BUFFER, QUAD_VIEW_PROJECTION_BINDING, m_pipViewProjectionBuffer));

    return AMF_OK;
}

AMF_RESULT VideoPresenterOpenGL::DrawFrame(const RenderTarget* pRenderTarget)
{
    GL_CALL_DLL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, pRenderTarget->frameBuffer));
    GL_CALL_DLL(glDrawArrays(GL_TRIANGLE_STRIP, 0, amf_countof(QUAD_VERTICES_NORM)));
    GL_CALL_DLL(glBindVertexArray(0));
    return AMF_OK;
}

AMF_RESULT VideoPresenterOpenGL::OnRenderViewResize(const RenderViewSizeInfo& newRenderView)
{
    AMFContext::AMFOpenGLLocker oglLock(m_pContext);

    AMF_RESULT res = VideoPresenter::OnRenderViewResize(newRenderView);
    AMF_RETURN_IF_FAILED(res, L"OnRenderViewResize() - VideoPresenter::OnRenderViewResize() failed");

    res = UpdateBuffer(m_viewProjectionBuffer, &m_viewProjection, sizeof(ViewProjection), 0);
    AMF_RETURN_IF_FAILED(res, L"OnRenderViewResize() - UpdateBuffe() failed to update view projection buffer");

    res = UpdateBuffer(m_pipViewProjectionBuffer, &m_pipViewProjection, sizeof(ViewProjection), 0);
    AMF_RETURN_IF_FAILED(res, L"OnRenderViewResize() - UpdateBuffe() failed to update pip view projection buffer");

    return AMF_OK;
}

AMF_RESULT VideoPresenterOpenGL::CreateQuadPipeline()
{
    const char* shaderSources[]         = { QuadOpenGL_vert,            QuadOpenGL_frag };
    constexpr amf_size sourceSizes[]    = { sizeof(QuadOpenGL_vert),    sizeof(QuadOpenGL_frag) };
    constexpr amf_uint shaderTypes[]    = { GL_VERTEX_SHADER,            GL_FRAGMENT_SHADER};
    amf_uint shaders[2] = {};

    AMF_RESULT res = CreatePipeline(shaderSources, amf_countof(shaderSources), sourceSizes, shaderTypes, shaders, m_quadProgram);
    AMF_RETURN_IF_FAILED(res, L"CreateQuadPipeline() - CreatePipeline() failed");

    m_quadVertShader = shaders[0];
    m_quadFragShader = shaders[1];

    return AMF_OK;
}

AMF_RESULT VideoPresenterOpenGL::SetupQuadPipelineUniforms()
{
    m_quadViewProjectionIndex = GL_CALL_DLL(glGetUniformBlockIndex(m_quadProgram, "ViewProjection"));
    GL_CALL_DLL(glUniformBlockBinding(m_quadProgram, m_quadViewProjectionIndex, QUAD_VIEW_PROJECTION_BINDING));
    m_quadSamplerIndex = GL_CALL_DLL(glGetUniformLocation(m_quadProgram, "samplerTex"));
    return AMF_OK;
}

AMF_RESULT VideoPresenterOpenGL::PrepareStates()
{
    AMFContext::AMFOpenGLLocker oglLock(m_pContext);
    AMFLock lock(&m_cs);

    // Quad pipeline shaders
    AMF_RESULT res = CreateQuadPipeline();
    AMF_RETURN_IF_FAILED(res, L"PrepareStates() - CreateQuadPipeline() failed");

    res = SetupQuadPipelineUniforms();
    AMF_RETURN_IF_FAILED(res, L"PrepareStates() - SetupQuadPipelineUniforms() failed");

    // Vertex buffer
    res = CreateVertexBuffer(QUAD_VERTEX_ATTRIBUTES, amf_countof(QUAD_VERTEX_ATTRIBUTES), QUAD_VERTICES_NORM, sizeof(QUAD_VERTICES_NORM), GL_STATIC_DRAW, m_vertexBuffer);
    AMF_RETURN_IF_FAILED(res, L"PrepareStates() - CreateBuffer() failed to create vertex buffer");

    // Projection buffers
    res = CreateBuffer(GL_ARRAY_BUFFER, &m_viewProjection, sizeof(ViewProjection), GL_DYNAMIC_DRAW, m_viewProjectionBuffer);
    AMF_RETURN_IF_FAILED(res, L"PrepareStates() - CreateBuffer() failed to create view projection buffer");

    res = CreateBuffer(GL_ARRAY_BUFFER, &m_pipViewProjection, sizeof(ViewProjection), GL_DYNAMIC_DRAW, m_pipViewProjectionBuffer);
    AMF_RETURN_IF_FAILED(res, L"PrepareStates() - CreateBuffer() failed to create PIP view projection buffer");

    // Samplers
    GLSamplerParams params = {};
 
    // DO NOT SET GL_XX_MIPMAP_XX options since textures without
    // mipmaps do not get rendered. TODO: add check for detecting mipmap

    params.minFilter = GL_LINEAR; // GL_LINEAR_MIPMAP_LINEAR
    params.magFilter = GL_LINEAR;
    res = CreateSampler(params, m_samplerMap[InterpolationLinear]);
    AMF_RETURN_IF_FAILED(res, L"PrepareStates() - CreateSampler() failed to create linear sampler");

    params.minFilter = GL_NEAREST; // GL_NEAREST_MIPMAP_NEAREST
    params.magFilter = GL_NEAREST;
    res = CreateSampler(params, m_samplerMap[InterpolationPoint]);
    AMF_RETURN_IF_FAILED(res, L"PrepareStates() - CreateSampler() failed to create point sampler");

    return AMF_OK;
}

AMF_RESULT VideoPresenterOpenGL::CreateShader(const char* const pShaderData, amf_int size, amf_uint type, amf_uint& shader) const
{
    AMF_RETURN_IF_FALSE(pShaderData != nullptr, AMF_INVALID_ARG, L"CreateShader() - pShaderData is NULL");
    AMF_RETURN_IF_FALSE(size > 0, AMF_INVALID_ARG, L"CreateShader() - size has to be > 0");
    AMF_RETURN_IF_FALSE(shader == 0, AMF_ALREADY_INITIALIZED, L"CreateShader() - shader is already initialized");

    AMFContext::AMFOpenGLLocker oglLock(m_pContext);

    shader = GL_CALL_DLL(glCreateShader(type));
    GL_CALL_DLL(glShaderSource(shader, 1, &pShaderData, &size));
    GL_CALL_DLL(glCompileShader(shader));

    amf_int success = 0;
    GL_CALL_DLL(glGetShaderiv(shader, GL_COMPILE_STATUS, &success));
    if (success == GL_FALSE)
    {
        amf_int length = 0;
        GL_CALL_DLL(glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length));

        std::unique_ptr<char[]> errorMsg = std::unique_ptr<char[]>(new char[length]);
        GL_CALL_DLL(glGetShaderInfoLog(shader, length, nullptr, errorMsg.get()));

        GetOpenGL()->glDeleteShader(shader);
        AMF_RETURN_IF_FAILED(AMF_GLX_FAILED, L"CreateShader() - failed to compile shader:\n%S", errorMsg.get());
    }

    return AMF_OK;
}

AMF_RESULT VideoPresenterOpenGL::CreateProgram(const amf_uint* pShaders, amf_size count, amf_uint& program) const
{
    AMF_RETURN_IF_FALSE(pShaders != nullptr, AMF_INVALID_ARG, L"CreateProgram() - pShaders is NULL");
    AMF_RETURN_IF_FALSE(count > 0, AMF_INVALID_ARG, L"CreateProgram() - count has to be > 0");
    AMF_RETURN_IF_FALSE(program == 0, AMF_ALREADY_INITIALIZED, L"CreateProgram() - program is already initialized");

    AMFContext::AMFOpenGLLocker oglLock(m_pContext);

    program = GL_CALL_DLL(glCreateProgram());

    for (amf_uint i = 0; i < count; ++i)
    {
        GL_CALL_DLL(glAttachShader(program, pShaders[i]));
    }

    GL_CALL_DLL(glLinkProgram(program));

    amf_int success = 0;
    GL_CALL_DLL(glGetProgramiv(program, GL_LINK_STATUS, &success));
    if (success == GL_FALSE)
    {
        amf_int length = 0;
        GL_CALL_DLL(glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length));

        std::unique_ptr<char[]> errorMsg = std::unique_ptr<char[]>(new char[length]);
        GL_CALL_DLL(glGetProgramInfoLog(program, length, nullptr, errorMsg.get()));

        GetOpenGL()->glDeleteProgram(program);
        AMF_RETURN_IF_FAILED(AMF_GLX_FAILED, L"CreateProgram() - failed to link shaders:\n%S", errorMsg.get());
    }

    AMF_RESULT res = ValidateProgram(program);
    if (res != AMF_OK)
    {
        GetOpenGL()->glDeleteProgram(program);
    }
    AMF_RETURN_IF_FAILED(res, L"CreateProgram() - ValidateProgram() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenterOpenGL::ValidateProgram(amf_uint program) const
{
    AMFContext::AMFOpenGLLocker oglLock(m_pContext);

    amf_int success = 0;
    GL_CALL_DLL(glValidateProgram(program));
    GL_CALL_DLL(glGetProgramiv(program, GL_VALIDATE_STATUS, &success));
    if (success == GL_FALSE)
    {
        amf_int length = 0;
        GL_CALL_DLL(glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length));

        std::unique_ptr<char[]> errorMsg = std::unique_ptr<char[]>(new char[length]);
        GL_CALL_DLL(glGetProgramInfoLog(program, length, nullptr, errorMsg.get()));

        GetOpenGL()->glDeleteProgram(program);
        AMF_RETURN_IF_FAILED(AMF_GLX_FAILED, L"ValidateProgram() - program validation failed:\n%S", errorMsg.get());
    }

    return AMF_OK;
}

AMF_RESULT VideoPresenterOpenGL::CreatePipeline(const char* const* ppShaderSources, amf_uint count, const amf_size* pShaderSizes, const amf_uint* pShaderTypes, amf_uint* pShaders, amf_uint& program) const
{
    AMF_RETURN_IF_FALSE(ppShaderSources != nullptr, AMF_INVALID_ARG, L"CreatePipeline() - ppShaderSources is NULL");
    AMF_RETURN_IF_FALSE(count > 0, AMF_INVALID_ARG, L"CreatePipeline() - count must be > 0");
    AMF_RETURN_IF_FALSE(pShaderSizes != nullptr, AMF_INVALID_ARG, L"CreatePipeline() - pShaderSizes is NULL");
    AMF_RETURN_IF_FALSE(pShaderTypes != nullptr, AMF_INVALID_ARG, L"CreatePipeline() - pShaderTypes is NULL");
    AMF_RETURN_IF_FALSE(pShaders != nullptr, AMF_INVALID_ARG, L"CreatePipeline() - pShaders is NULL");
    AMF_RETURN_IF_FALSE(program == 0, AMF_ALREADY_INITIALIZED, L"CreatePipeline() - program is already initialized");

    // Quickly validate before we create anything
    for (amf_uint i = 0; i < count; ++i)
    {
        AMF_RETURN_IF_FALSE(pShaders[i] == 0, AMF_ALREADY_INITIALIZED, L"CreatePipeline() - shader %d already initialized", i);
        AMF_RETURN_IF_FALSE(pShaderSizes[i] > 0, AMF_INVALID_ARG, L"CreatePipeline() - shader size %d was 0", i);
    }
    
    // Resource managers for ogl could be a nice addition instead of code like this...
#define DELETE_SHADERS_IF_FAILED(r, s, e)\
if (r != AMF_OK)\
{\
    for (amf_uint j = s; j <= e; ++j)\
    {\
        GL_CALL_DLL(glDeleteShader(pShaders[j])); \
    }\
}

    AMFContext::AMFOpenGLLocker oglLock(m_pContext);

    for (amf_uint i = 0; i < count; ++i)
    {
        AMF_RESULT res = CreateShader(ppShaderSources[i], (amf_int)pShaderSizes[i], pShaderTypes[i], pShaders[i]);
        DELETE_SHADERS_IF_FAILED(res, 0, i);
        AMF_RETURN_IF_FAILED(res, L"CreatePipeline() - CreateShader() failed to create shader %d", i);
    }

    AMF_RESULT res = CreateProgram(pShaders, count, program);
    DELETE_SHADERS_IF_FAILED(res, 0, count-1);
    AMF_RETURN_IF_FAILED(res, L"CreatePipeline() - CreateProgram() failed to create program");

#undef DELETE_SHADERS_IF_FAILED

    return AMF_OK;
}

AMF_RESULT VideoPresenterOpenGL::CreateBuffer(amf_uint target, const void* pData, amf_size size, amf_uint usage, amf_uint& buffer) const
{
    AMF_RETURN_IF_FALSE(pData != nullptr, AMF_INVALID_ARG, L"CreateBuffer() - pShaderData is NULL");
    AMF_RETURN_IF_FALSE(size > 0, AMF_INVALID_ARG, L"CreateBuffer() - size has to be > 0");
    AMF_RETURN_IF_FALSE(buffer == 0, AMF_ALREADY_INITIALIZED, L"CreateBuffer() - buffer is already initialized");

    AMFContext::AMFOpenGLLocker oglLock(m_pContext);

    GL_CALL_DLL(glGenBuffers(1, &buffer));
    GL_CALL_DLL(glBindBuffer(target, buffer));
    GL_CALL_DLL(glBufferData(target, size, pData, usage));
    GL_CALL_DLL(glBindBuffer(target, 0));

    return AMF_OK;
}

AMF_RESULT VideoPresenterOpenGL::CreateVertexBuffer(const GLVertexAttribute* pAttribs, amf_uint attribCount, const void* pData, amf_size size, amf_uint usage, GLVertexBuffer& buffer) const
{
    AMF_RETURN_IF_FALSE(pAttribs != nullptr, AMF_INVALID_ARG, L"CreateVertexBuffer() - pAttribs is NULL");
    AMF_RETURN_IF_FALSE(attribCount > 0, AMF_INVALID_ARG, L"CreateVertexBuffer() - attribCount has to be > 0");
    
    AMFContext::AMFOpenGLLocker oglLock(m_pContext);

    AMF_RESULT res = CreateBuffer(GL_ARRAY_BUFFER, pData, size, usage, buffer.vbo);
    AMF_RETURN_IF_FAILED(res, L"CreateVertexBuffer() - CreateBuffer() failed");

    GL_CALL_DLL(glGenVertexArrays(1, &buffer.vao));
    GL_CALL_DLL(glBindVertexArray(buffer.vao));

    GL_CALL_DLL(glBindBuffer(GL_ARRAY_BUFFER, buffer.vbo));
    for (amf_uint i = 0; i < attribCount; ++i)
    {
        const GLVertexAttribute& attrib = pAttribs[i];
        AMF_RETURN_IF_FALSE(attrib.size > 0, AMF_INVALID_ARG, L"CreateVertexBuffer() - Invalid attribute size at index %u, cannot be 0", i);
        GL_CALL_DLL(glVertexAttribPointer(i, attrib.size, attrib.type, attrib.normalized ? GL_TRUE : GL_FALSE, (amf_uint)attrib.stride, (void*)attrib.offset));
        GL_CALL_DLL(glEnableVertexAttribArray(i));
    }

    GL_CALL_DLL(glBindVertexArray(0));
    GL_CALL_DLL(glBindBuffer(GL_ARRAY_BUFFER, 0));

    return AMF_OK;
}

AMF_RESULT VideoPresenterOpenGL::UpdateBuffer(amf_uint buffer, const void* pData, amf_size size, amf_size offset) const
{
    AMF_RETURN_IF_FALSE(pData != nullptr, AMF_INVALID_ARG, L"UpdateBuffer() - pShaderData is NULL");
    AMF_RETURN_IF_FALSE(size > 0, AMF_INVALID_ARG, L"UpdateBuffer() - size has to be > 0");
    AMF_RETURN_IF_FALSE(buffer != 0, AMF_NOT_INITIALIZED, L"CreateBuffer() - buffer is not initialized");

    AMFContext::AMFOpenGLLocker oglLock(m_pContext);

    GL_CALL_DLL(glBindBuffer(GL_ARRAY_BUFFER, buffer));
    GL_CALL_DLL(glBufferSubData(GL_ARRAY_BUFFER, offset, size, pData));
    GL_CALL_DLL(glBindBuffer(GL_ARRAY_BUFFER, 0));

    return AMF_OK;
}


AMF_RESULT VideoPresenterOpenGL::CreateSampler(const GLSamplerParams& params, amf_uint& sampler) const
{
    AMF_RETURN_IF_FALSE(sampler == 0, AMF_ALREADY_INITIALIZED, L"CreateSampler() - sampler is already initialized");

    GL_CALL_DLL(glGenSamplers(1, &sampler));

    GL_CALL_DLL(glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, params.minFilter));
    GL_CALL_DLL(glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, params.magFilter));

    GL_CALL_DLL(glSamplerParameterf(sampler, GL_TEXTURE_MIN_LOD, params.minLOD));
    GL_CALL_DLL(glSamplerParameterf(sampler, GL_TEXTURE_MAX_LOD, params.maxLOD));

    GL_CALL_DLL(glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, params.wrapS));
    GL_CALL_DLL(glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, params.wrapT));
    GL_CALL_DLL(glSamplerParameteri(sampler, GL_TEXTURE_WRAP_R, params.wrapR));


    GL_CALL_DLL(glSamplerParameterfv(sampler, GL_TEXTURE_BORDER_COLOR, params.borderColor));

    GL_CALL_DLL(glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_MODE, params.compareMode));
    GL_CALL_DLL(glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_FUNC, params.compareFunc));

    return AMF_OK;
}

AMF_RESULT VideoPresenterOpenGL::CreateRenderTarget(RenderTarget* pTarget, amf_int32 width, amf_int32 height, amf::AMF_SURFACE_FORMAT format) const
{
    AMF_RETURN_IF_FALSE(pTarget != nullptr, AMF_INVALID_ARG, L"CreateRenderTarget() - pTarget is NULL");
    AMF_RETURN_IF_FALSE(pTarget->frameBuffer == 0, AMF_ALREADY_INITIALIZED, L"CreateRenderTarget() - Frame buffer already initialized");

    GL_CALL_DLL(glGenFramebuffers(1, &pTarget->frameBuffer));

    AMFSize swapchainSize = GetSwapchainSize();
    if (width == 0 || height == 0)
    {
        width = swapchainSize.width;
        height = swapchainSize.height;
    }

    if (format == AMF_SURFACE_UNKNOWN)
    {
        format = GetInputFormat();
    }

    pTarget->size = AMFConstructSize(width, height);
    
    // We can have AMF create texture for us
    AMF_RESULT res = m_pContext->AllocSurface(AMF_MEMORY_OPENGL, format, width,height, &pTarget->pSurface);
    AMF_RETURN_IF_FAILED(res, L"CreateRenderTarget() - AllocSurface() failed to create texture");

    pTarget->texture = (void*)GetPackedSurfaceOpenGL(pTarget->pSurface);

    GL_CALL_DLL(glBindFramebuffer(GL_FRAMEBUFFER, pTarget->frameBuffer));
    GL_CALL_DLL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, CastPointer<amf_uint>(pTarget->texture), 0));

    amf_uint status = GL_CALL_DLL(glCheckFramebufferStatus(GL_FRAMEBUFFER));
    AMF_RETURN_IF_FALSE(status == GL_FRAMEBUFFER_COMPLETE, AMF_GLX_FAILED, L"CreateRenderTarget() - failed to create framebuffer, status = %s", 
                        GetOpenGLFrameBufferStatusName(status));

    return AMF_OK;

}

AMF_RESULT VideoPresenterOpenGL::DestroyRenderTarget(RenderTarget* pTarget) const
{
    AMF_RETURN_IF_FALSE(pTarget != nullptr, AMF_INVALID_ARG, L"DestroyRenderTarget() - pTarget is NULL");
    GL_CALL_DLL(glFinish());    
    GL_CALL_DLL(glDeleteFramebuffers(1, &pTarget->frameBuffer));
    *pTarget = {};

    return AMF_OK;
}
