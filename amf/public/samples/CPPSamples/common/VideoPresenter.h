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

#include "public/include/core/Context.h"
#include "PipelineElement.h"
#include "SwapChain.h"
#define _USE_MATH_DEFINES
#include <math.h>

// The Presenter's Vulkan Shader QuadVulkan_fs.frag and QuadVulkan_vs.vert, and Presenter's
// DX Shader QuadDX9/11/12_vs.hlsl and QuadDX9/11/12_ps.hlsl are located under directory
// drivers/amf/stable/public/samples/CPPSamples/common/.
// The build command for shaders are imported into the project using AMF_Presenter_Vulkan_Shader.props
// and AMF_Presenter_DX_Shader.props under drivers/amf/stable/public/props for Visual Studio
// project, and vulkan_shader_sources for Makefiles (project Makefile and common_rule.mak).
class VideoPresenter;
typedef std::shared_ptr<VideoPresenter> VideoPresenterPtr;

/*
#if defined(__linux)
struct XWindowContext
{
    Display             *m_pDisplay;
    AMFCriticalSection      m_Sect;
};
#endif
*/

struct Transformation2D
{
    AMFFloatPoint2D translation;
    AMFFloatPoint2D scaleLinear;
    AMFFloatPoint2D scaleOrtho;
};

AMFPoint            TransformPoint(const AMFPoint& point, const Transformation2D& transformation);
AMFFloatPoint2D     TransformPoint(const AMFFloatPoint2D& point, const Transformation2D& transformation);

AMFPoint            TransformPoint(const AMFPoint& point, const amf_float matrix[4][4]);
AMFFloatPoint2D     TransformPoint(const AMFFloatPoint2D& point, const amf_float matrix[4][4]);

class VideoPresenter: public PipelineElement, public amf::AMFDataAllocatorCB, public amf::AMFSurfaceObserver
{
public:
    enum Mode
    {
        ModePlaying,
        ModeStep,
        ModePaused
    };

    enum Interpolation
    {
        InterpolationLinear = 0,
        InterpolationPoint,
        INTERPOLATION_COUNT,
    };

    virtual ~VideoPresenter();

    static AMF_RESULT                   Create(VideoPresenterPtr& pPresenter, amf::AMF_MEMORY_TYPE type, amf_handle hwnd, amf::AMFContext* pContext, amf_handle hDisplay=nullptr);

    // amf::AMFInterface interface
    virtual amf_long AMF_STD_CALL       Acquire() { return 1; }
    virtual amf_long AMF_STD_CALL       Release() { return 1; }
    virtual AMF_RESULT AMF_STD_CALL     QueryInterface(const amf::AMFGuid& /*interfaceID*/, void** /*ppInterface*/) { return AMF_NOT_IMPLEMENTED; }

    // PipelineElement
    virtual amf_int32                   GetInputSlotCount() const                           { return 1; }
    virtual amf_int32                   GetOutputSlotCount() const                          { return 0; }

    virtual AMF_RESULT                  Init(amf_int32 width, amf_int32 height, amf::AMFSurface* pSurface = nullptr);
    virtual AMF_RESULT                  Terminate();
    virtual AMF_RESULT                  Reset();

    virtual AMF_RESULT                  SubmitInput(amf::AMFData* pData);
    virtual AMF_RESULT                  Present(amf::AMFSurface* pSurface);

    // Input frame settings
    virtual AMF_RESULT                  SetInputFormat(amf::AMF_SURFACE_FORMAT format);
    virtual amf::AMF_SURFACE_FORMAT     GetInputFormat() const                              { amf::AMFLock lock(&m_cs); return m_inputFormat; }

    void                                SetFrameSize(amf_int32 width, amf_int32 height)     { amf::AMFLock lock(&m_cs); m_inputFrameSize = AMFConstructSize(width, height); }
    amf_int32                           GetFrameWidth()     const                           { amf::AMFLock lock(&m_cs); return m_inputFrameSize.width; }
    amf_int32                           GetFrameHeight()    const                           { amf::AMFLock lock(&m_cs); return m_inputFrameSize.height; }
    virtual AMFRect                     GetSourceRect()     const;

    virtual AMF_RESULT                  SetSubresourceIndex(amf_int32 index)                { amf::AMFLock lock(&m_cs); m_subresourceIndex = index; return AMF_OK; }
    virtual amf_int32                   GetSubresourceIndex() const                         { amf::AMFLock lock(&m_cs); return m_subresourceIndex; }

    // Playback
    virtual AMF_RESULT                  Resume();
    AMF_RESULT                          Pause()                                             { amf::AMFLock lock(&m_cs); m_state = ModePaused; return AMF_OK; }
    AMF_RESULT                          Step()                                              { amf::AMFLock lock(&m_cs); m_state = ModeStep; return AMF_OK;}
    Mode                                GetMode() const                                     {  return m_state;}
    virtual void                        SetAVSyncObject(AVSyncObject *pAVSync)              { amf::AMFLock lock(&m_cs); m_pAVSync = pAVSync;}
    virtual void                        DoActualWait(amf_bool doWait)                       { amf::AMFLock lock(&m_cs); m_doWait = doWait; }
    virtual void AMF_STD_CALL           SetDropThreshold(amf_pts ptsDropThreshold)          { amf::AMFLock lock(&m_cs); m_ptsDropThreshold = ptsDropThreshold; }

    // Stats
    virtual amf_pts                     GetCurrentTime()                                    { return m_currentTime; }
    virtual amf_double                  GetFPS() const                                      { return m_lastFPS; }
    virtual amf_int64                   GetFramesDropped() const                            { return m_framesDropped; }

    // Swapchain
    virtual amf::AMF_MEMORY_TYPE        GetMemoryType() const = 0;
    virtual AMFSize                     GetSwapchainSize() const                            { amf::AMFLock lock(&m_cs); return m_pSwapChain != nullptr ? m_pSwapChain->GetSize() : AMFConstructSize(0, 0); }

    void                                SetFullScreen(amf_bool fullscreen)                  { m_fullscreen = fullscreen; }
    amf_bool                            GetFullScreen()                                     { return m_fullscreen; }
    void                                SetExclusiveFullscreen(const amf_bool excluse)      { m_exclusiveFullscreen = excluse; }

    virtual void                        SetWaitForVSync(amf_bool doWait)                    { amf::AMFLock lock(&m_cs); m_waitForVSync = doWait; }
    virtual AMFRate                     GetDisplayRefreshRate();
    virtual void AMF_STD_CALL           ResizeIfNeeded();

    // Backbuffers/Processor
    virtual amf_bool                    SupportAllocator() const                            { return false; }
    virtual AMF_RESULT                  SetProcessor(amf::AMFComponent* pProcessor, amf::AMFComponent* pHQScaler, bool bAllocator);
    virtual void                        SetRenderToBackBuffer(amf_bool renderToBackBuffer)  { amf::AMFLock lock(&m_cs); m_renderToBackBuffer = renderToBackBuffer; }

    // amf::AMFDataAllocatorCB interface
    virtual AMF_RESULT AMF_STD_CALL     AllocBuffer(amf::AMF_MEMORY_TYPE /*type*/, amf_size /*size*/, amf::AMFBuffer** /*ppBuffer*/) { return AMF_NOT_IMPLEMENTED; }
    virtual AMF_RESULT AMF_STD_CALL     AllocSurface(amf::AMF_MEMORY_TYPE type, amf::AMF_SURFACE_FORMAT format, amf_int32 width,
        amf_int32 height, amf_int32 hPitch, amf_int32 vPitch, amf::AMFSurface** ppSurface);
    // amf::AMFSurfaceObserver interface
    virtual void AMF_STD_CALL           OnSurfaceDataRelease(amf::AMFSurface* pSurface);

    // View transform
    virtual AMF_RESULT                  SetViewTransform(amf_int offsetX, amf_int offsetY, amf_float scale)
    {
        amf::AMFLock lock(&m_cs);
        m_viewOffsetX = offsetX;
        m_viewOffsetY = offsetY;
        m_viewScale   = scale;
        return AMF_OK;
    }

    virtual AMF_RESULT                  GetViewTransform(amf_int& offsetX, amf_int& offsetY, amf_float& scale) const
    {
        amf::AMFLock lock(&m_cs);
        offsetX = m_viewOffsetX;
        offsetY = m_viewOffsetY;
        scale   = m_viewScale;
        return AMF_OK;
    }

    // Orientation
    virtual void                        SetOrientation(amf_int orientation)                 { m_orientation = orientation; }
    virtual amf_int                     GetOrientation()        const                       { return m_orientation; }
    virtual amf_float                   GetOrientationDegrees() const                       { return 90.0f * m_orientation; }
    virtual amf_float                   GetOrientationRadians() const                       { return GetOrientationDegrees() * amf_float(M_PI) / 180.0f; }

    // View mapping
    virtual AMFPoint                    MapClientToSource(const AMFPoint& point) const      { amf::AMFLock lock(&m_cs); return TransformPoint(point, m_clientToSrcTransform); }
    virtual AMFPoint                    MapSourceToClient(const AMFPoint& point) const      { amf::AMFLock lock(&m_cs); return TransformPoint(point, m_srcToClientTransform); }

    // Interpolation
    virtual AMF_RESULT                  SetInterpolation(Interpolation interpolation)       { m_interpolation = interpolation; return AMF_OK; }
    virtual Interpolation               GetInterpolation()                                  { return m_interpolation; }

    // Picture in picture
    virtual void                        SetEnablePIP(amf_bool enablePIP)                    { m_enablePIP = enablePIP; }
    virtual amf_bool                    GetEnablePIP() const                                { return m_enablePIP; }
    virtual void                        SetPIPZoomFactor(amf_int pipZoomFactor)             { m_pipZoomFactor = pipZoomFactor; }
    virtual void                        SetPIPFocusPositions(AMFFloatPoint2D pipFocusPos)   { m_pipFocusPos = pipFocusPos; }

protected:
    VideoPresenter(amf_handle hwnd, amf::AMFContext* pContext, amf_handle hDisplay=nullptr);
    virtual AMF_RESULT                  Freeze();
    virtual AMF_RESULT                  UnFreeze();


    inline amf_pts CalculatePtsWaitDiff(amf_pts pts)
    {
        return (pts - m_startPts) - (amf_high_precision_clock() - m_startTime);
    }
    amf_bool                            WaitForPTS(amf_pts pts, amf_bool realWait = true); // returns false if frame is too late and should be dropped


    virtual AMF_RESULT                  DropFrame();

    virtual AMF_RESULT                  CheckForResize(amf_bool force);
    virtual AMF_RESULT                  ResizeSwapChain();
    virtual amf_bool                    CanResize()
    {
        amf::AMFLock lock(&m_cs);
        return m_pSwapChain == nullptr ? false : m_pTrackSurfaces.empty() && m_pSwapChain->GetBackBuffersAcquired() == 0;
    }

    virtual AMF_RESULT                  ApplyCSC(amf::AMFSurface* pSurface);


    typedef BackBufferBase RenderTargetBase;

    virtual AMF_RESULT RenderToSwapChain(amf::AMFSurface* pSurface);
    virtual AMF_RESULT RenderSurface(amf::AMFSurface* pSurface, const RenderTargetBase* pRenderTarget);

    struct RenderViewSizeInfo
    {
        AMFSize     srcSize;
        AMFRect     srcRect;
        AMFSize     dstSize;
        AMFRect     dstRect;
        amf_float   rotation; // Radians

        AMFRect     pipSrcRect;
        AMFRect     pipDstRect;

        inline amf_bool operator==(const RenderViewSizeInfo& other) const
        {
            return  this->dstRect    == other.dstRect    &&
                    this->dstSize    == other.dstSize    &&
                    this->srcRect    == other.srcRect    &&
                    this->srcSize    == other.srcSize    &&
                    this->rotation   == other.rotation   &&
                    this->pipDstRect == other.pipDstRect &&
                    this->pipSrcRect == other.pipSrcRect;
        }

        inline amf_bool operator!=(const RenderViewSizeInfo& other) const { return !operator==(other); }
    };

    virtual AMF_RESULT RenderSurface(amf::AMFSurface* /*pSurface*/, const RenderTargetBase* /*pRenderTarget*/, RenderViewSizeInfo& /*renderView*/) { return AMF_OK; };

    // The viewport rect for vertex and texture coordinates used by the graphics API
    // Override in inherited presenters to set new value.
    // Default is set to viewport clipping used in DirectX (All versions)
    virtual AMFRect                     GetVertexViewRect()     const                       { return AMFConstructRect(-1, 1, 1, -1);  }
    virtual AMFRect                     GetTextureViewRect()    const                       { return AMFConstructRect(0, 1, 1, 0);    }

    virtual AMF_RESULT                  CalcOutputRect(const AMFRect* pSrcRect, const AMFRect* pScreenRect, AMFRect* pOutputRect) const;
    virtual AMF_RESULT                  GetRenderViewSizeInfo(const AMFRect& srcSurfaceRect, const AMFSize& dstSurfaceSize, const AMFRect& dstSurfaceRect, RenderViewSizeInfo& renderView) const;
    virtual AMF_RESULT                  OnRenderViewResize(const RenderViewSizeInfo& newRenderView);
    virtual AMF_RESULT                  ResizeRenderView(const RenderViewSizeInfo& newRenderView);

    virtual void                        UpdateProcessor();

    AMF_RESULT                          SetFullscreenState(amf_bool fullscreen);
    AMF_RESULT                          SetWindowFullscreenState(amf_handle hwnd, amf_handle hDisplay, amf_bool fullscreen);

    // Core
    amf::AMFContext*                    m_pContext;
    amf_handle                          m_hwnd;
    amf_handle                          m_hDisplay;

    // Input frame
    amf::AMF_SURFACE_FORMAT             m_inputFormat;
    AMFSize                             m_inputFrameSize;
    amf_int32                           m_subresourceIndex;

    // Playback
    Mode                                m_state;
    AVSyncObject*                       m_pAVSync;
    amf_bool                            m_doWait;
    amf_pts                             m_startTime;
    amf_pts                             m_startPts;
    amf_pts                             m_ptsDropThreshold;
    amf::AMFPreciseWaiter               m_waiter;
    amf::AMFSurfacePtr                  m_pLastFrame;

    // Stats
    amf_pts                             m_currentTime;
    amf_int64                           m_frameCount;
    amf_pts                             m_fpsStatStartTime;
    amf_double                          m_lastFPS;
    amf_int64                           m_framesDropped;
    amf_bool                            m_firstFrame;

    // Swapchain
    typedef std::unique_ptr<SwapChain>  SwapChainPtr;
    SwapChainPtr                        m_pSwapChain;

    // Fullscreen state for saving original window size when using
    // borderless window fullscreen mode.
    struct WindowFullscreenContext
    {
        amf_bool    fullscreenState;
        AMFRect     windowModeRect;
#ifdef _WIN32
        DEVMODEW    windowModeDevMode;
        LONG_PTR    windowModeStyle;
        LONG_PTR    windowModeExStyle;
#endif
    };

    amf_bool                            m_fullscreen; // desired fullscreen state
    amf_bool                            m_exclusiveFullscreen; // desired fullscreen type

    WindowFullscreenContext             m_fullscreenContext;
    amf_bool                            m_waitForVSync;
    amf_bool                            m_resizeSwapChain;

    // Processor
    amf::AMFComponentPtr                m_pProcessor;
    amf::AMFComponentPtr                m_pHQScaler;
    amf_bool                            m_renderToBackBuffer;
    amf::amf_set<amf::AMFSurface*>      m_pTrackSurfaces;
    amf_bool                            m_resizing;

    // View
    amf_int                             m_viewOffsetX;
    amf_int                             m_viewOffsetY;
    amf_float                           m_viewScale;
    amf_int                             m_orientation;

    // Rendering
    static constexpr amf_float          ClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    // Quad Vertex
    struct Vertex
    {
        amf_float pos[3];
        amf_float tex[2];
    };

    static const Vertex             QUAD_VERTICES_NORM[4];

    // Interpolation
    Interpolation                       m_interpolation;
    template<typename T> using SamplerMap = amf::amf_map<Interpolation, T>;

    // PIP
    static const Interpolation          PIP_INTERPOLATION;
    static constexpr amf_float          PIP_SIZE_FACTOR = 0.3f;
    amf_bool                            m_enablePIP;
    amf_int32                           m_pipZoomFactor;
    AMFFloatPoint2D                     m_pipFocusPos;

    // View transform
    using ProjectionMatrix = amf_float[4][4];
    struct ViewProjection
    {
        ProjectionMatrix vertexTransform;
        ProjectionMatrix texTransform;
    };

    ViewProjection                      m_viewProjection;
    ViewProjection                      m_pipViewProjection;

    Transformation2D                    m_srcToClientTransform;
    Transformation2D                    m_srcToViewTransform;
    Transformation2D                    m_normToViewTransform;
    Transformation2D                    m_clientToSrcTransform;
    Transformation2D                    m_texNormToViewTransform;
    Transformation2D                    m_pipVertexTransform;
    Transformation2D                    m_pipTexTransform;

    RenderViewSizeInfo                  m_renderView;
};

inline amf_bool LeftFrameExists(amf::AMF_FRAME_TYPE frameType)
{
    return (frameType & amf::AMF_FRAME_LEFT_FLAG) == amf::AMF_FRAME_LEFT_FLAG || (frameType & amf::AMF_FRAME_STEREO_FLAG) == 0 || frameType == amf::AMF_FRAME_UNKNOWN;
}

inline amf_bool RightFrameExists(amf::AMF_FRAME_TYPE frameType)
{
    return (frameType & amf::AMF_FRAME_RIGHT_FLAG) == amf::AMF_FRAME_RIGHT_FLAG && frameType != amf::AMF_FRAME_UNKNOWN;
}

inline AMFFloatPoint2D GetCenterFloatPoint(const AMFRect& rect)
{
    return AMFConstructFloatPoint2D((rect.left + rect.right) / 2.0f, (rect.top + rect.bottom) / 2.0f);
}

inline AMFFloatPoint2D GetCenterFloatPoint(const AMFSize& size)
{
    return AMFConstructFloatPoint2D(size.width / 2.0f, size.height / 2.0f);
}

inline AMFSize RectToSize(const AMFRect& rect)
{
    return AMFConstructSize(rect.Width(), rect.Height());
}

inline AMFRect SizeToRect(const AMFSize& size, amf_int32 left = 0, amf_int32 top = 0)
{
    return AMFConstructRect(left, top, size.width, size.height);
}

AMF_RESULT          TransformRect(AMFRect& rect, amf_int offsetX, amf_int offsetY, amf_float scale);
AMFRect             GetPlaneRect(amf::AMFPlane* pPlane);
AMF_RESULT          ScaleAndCenterRect(const AMFRect& srcRect, const AMFRect& dstRect, amf_float rotation, AMFRect& outputRect);

Transformation2D    ConstructIdentity();
void                Scale(Transformation2D& transformation, amf_float xScale, amf_float yScale);
void                Translate(Transformation2D& transformation, amf_float xOffset, amf_float yOffset);
void                Rotate(Transformation2D& transformation, amf_float radians);
Transformation2D    Combine(const Transformation2D& left, const Transformation2D& right);
Transformation2D    Inverse(const Transformation2D& transformation);
void                TransformationToMatrix(const Transformation2D& transformation, amf_float matrix[4][4]);
void                MatrixToTransformation(const amf_float matrix[4][4], Transformation2D& transformation);

amf_bool            IsIdentity(const amf_float matrix[4][4]);
AMF_RESULT          CalcVertexTransformation(const AMFSize& srcSize, const AMFRect& dstRect, const AMFSize& dstSize, const AMFRect& viewRect,
                                             amf_float rotation, Transformation2D* pSrcToDst, Transformation2D* pSrcToView, Transformation2D* pNormToView);
AMF_RESULT          ProjectRectToView(const AMFSize& srcSize, const AMFRect& srcRect, const AMFRect& viewRect, Transformation2D& normToView);
