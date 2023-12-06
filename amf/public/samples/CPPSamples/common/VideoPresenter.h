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
#define _USE_MATH_DEFINES
#include <math.h>

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
    virtual ~VideoPresenter();

    virtual amf_int32 GetInputSlotCount() const { return 1; }
    virtual amf_int32 GetOutputSlotCount() const { return 0; }

    virtual AMF_RESULT SubmitInput(amf::AMFData* pData);

    virtual AMF_RESULT              Present(amf::AMFSurface* pSurface) = 0;
    virtual amf::AMF_MEMORY_TYPE    GetMemoryType() const = 0;
    virtual amf::AMF_SURFACE_FORMAT GetInputFormat() const = 0;
    virtual AMF_RESULT              SetInputFormat(amf::AMF_SURFACE_FORMAT format) = 0;
    virtual AMF_RESULT              SetSubresourceIndex(amf_int32 index);
    virtual amf_int32               GetSubresourceIndex();

    virtual AMF_RESULT              Init(amf_int32 width, amf_int32 height, amf::AMFSurface* pSurface = nullptr);
    virtual AMF_RESULT              Terminate();
    virtual AMF_RESULT              Reset();
    virtual amf_pts                 GetCurrentTime() { return m_currentTime; }

    virtual double              GetFPS() const { return m_dLastFPS; }
    virtual amf_int64           GetFramesDropped() const {return m_iFramesDropped; }
    virtual bool                SupportAllocator() const { return false; }
    virtual void                DoActualWait(bool bDoWait) {m_bDoWait = bDoWait;}
    virtual void                SetFullScreen(bool bFullScreen) { m_bFullScreen = bFullScreen; }
    virtual bool                GetFullScreen() { return m_bFullScreen; }
    virtual AMFSize             GetSwapchainSize() { AMFSize size = {};  return size; }
	virtual void                SetWaitForVSync(bool bDoWait) { m_bWaitForVSync = bDoWait; }
	virtual	AMFRate				GetDisplayRefreshRate() { return AMFConstructRate(60, 0); }

    virtual AMFRect             GetSourceRect();
    virtual AMFPoint            MapClientToSource(const AMFPoint& point) { return TransformPoint(point, m_clientToSrcTransform); }
    virtual AMFPoint            MapSourceToClient(const AMFPoint& point) { return TransformPoint(point, m_srcToClientTransform); }

    virtual AMF_RESULT          Resume();
    AMF_RESULT Pause() { m_state = ModePaused; return AMF_OK; }
    AMF_RESULT Step() { m_state = ModeStep; return AMF_OK;}
    Mode       GetMode() const { return m_state;}

    virtual AMF_RESULT SetProcessor(amf::AMFComponent* pProcessor, amf::AMFComponent* pHQScaler = nullptr);

    // amf::AMFInterface interface
    virtual amf_long AMF_STD_CALL Acquire() { return 1; }
    virtual amf_long AMF_STD_CALL Release() { return 1; }
    virtual AMF_RESULT AMF_STD_CALL QueryInterface(const amf::AMFGuid& /*interfaceID*/, void** /*ppInterface*/) { return AMF_NOT_IMPLEMENTED; }

    // amf::AMFDataAllocatorCB interface
    virtual AMF_RESULT AMF_STD_CALL AllocBuffer(amf::AMF_MEMORY_TYPE /*type*/, amf_size /*size*/, amf::AMFBuffer** /*ppBuffer*/) { return AMF_NOT_IMPLEMENTED; }
    virtual AMF_RESULT AMF_STD_CALL AllocSurface(amf::AMF_MEMORY_TYPE /*type*/, amf::AMF_SURFACE_FORMAT /*format*/,
            amf_int32 /*width*/, amf_int32 /*height*/, amf_int32 /*hPitch*/, amf_int32 /*vPitch*/, amf::AMFSurface** /*ppSurface*/) { return AMF_NOT_IMPLEMENTED; }

    // amf::AMFSurfaceObserver interface
    virtual void AMF_STD_CALL OnSurfaceDataRelease(amf::AMFSurface* /*pSurface*/) {}

    virtual AMF_RESULT SetViewTransform(amf_int offsetX, amf_int offsetY, amf_float scale)
    {
        m_viewOffsetX = offsetX;
        m_viewOffsetY = offsetY;
        m_viewScale   = scale;
        return AMF_OK;
    }

    virtual AMF_RESULT GetViewTransform(amf_int& offsetX, amf_int& offsetY, amf_float& scale) const
    {
        offsetX = m_viewOffsetX;
        offsetY = m_viewOffsetY;
        scale   = m_viewScale;
        return AMF_OK;
    }

    virtual void SetOrientation(int orientation) { m_iOrientation = orientation; }
    virtual int GetOrientation() const { return m_iOrientation; }
    virtual amf_float GetOrientationDegrees() const { return 90.0f * m_iOrientation; }
    virtual amf_float GetOrientationRadians() const { return GetOrientationDegrees() * float(M_PI) / 180.0f; }

	// Get frame width and height
    amf_int32 GetFrameWidth() const     { return m_InputFrameSize.width; }
    amf_int32 GetFrameHeight() const    { return m_InputFrameSize.height; }

    void SetFrameSize(amf_int32 width, amf_int32 height)
    {
        m_InputFrameSize.width = width;
        m_InputFrameSize.height = height;
    }

    virtual void SetAVSyncObject(AVSyncObject *pAVSync) {m_pAVSync = pAVSync;}

    virtual void AMF_STD_CALL  ResizeIfNeeded() {} // call from UI thread (for VulkanPresenter on Linux)

    virtual void AMF_STD_CALL SetDropThreshold(amf_pts ptsDropThreshold) { m_ptsDropThreshold = ptsDropThreshold; }

    virtual void                SetEnablePIP(bool bEnablePIP) { m_enablePIP = bEnablePIP; }
    virtual bool                GetEnablePIP() const { return m_enablePIP; }
    virtual void                SetPIPZoomFactor(amf_int iPIPZoomFactor) { m_pipZoomFactor = iPIPZoomFactor; }
    virtual void                SetPIPFocusPositions(AMFFloatPoint2D fPIPFocusPos) { m_pipFocusPos = fPIPFocusPos; }

protected:
    VideoPresenter();
    virtual AMF_RESULT      Freeze();
    virtual AMF_RESULT      UnFreeze();

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
            return  this->dstRect == other.dstRect &&
                this->dstSize == other.dstSize &&
                this->srcRect == other.srcRect &&
                this->srcSize == other.srcSize &&
                this->rotation == other.rotation &&
                this->pipDstRect == other.pipDstRect &&
                this->pipSrcRect == other.pipSrcRect;
        }

        inline amf_bool operator!=(const RenderViewSizeInfo& other) const { return !operator==(other); }
    };

    // The viewport rect for vertex and texture coordinates used by the graphics API
    // Override in inherited presenters to set new value. 
    // Default is set to viewport clipping used in DirectX (All versions)
    virtual AMFRect             GetVertexViewRect()     const { return AMFConstructRect(-1, 1, 1, -1);  }
    virtual AMFRect             GetTextureViewRect()    const { return AMFConstructRect(0, 1, 1, 0);    }

    virtual AMF_RESULT          CalcOutputRect(const AMFRect* pSrcRect, const AMFRect* pScreenRect, AMFRect* pOutputRect) const;
    virtual AMF_RESULT          GetRenderViewSizeInfo(const AMFRect& srcSurfaceRect, const AMFSize& dstSurfaceSize, const AMFRect& dstSurfaceRect, RenderViewSizeInfo& renderView) const;
    virtual AMF_RESULT          OnRenderViewResize(const RenderViewSizeInfo& newRenderView);
    virtual AMF_RESULT          ResizeRenderView(const RenderViewSizeInfo& newRenderView);

    bool                        WaitForPTS(amf_pts pts, bool bRealWait = true); // returns false if frame is too late and should be dropped

    virtual void                UpdateProcessor();

    amf_pts                     m_startTime;
    amf_pts                     m_startPts;
    amf::AMFPreciseWaiter       m_waiter;
    AMFSize                     m_InputFrameSize;

    // Stats
    amf_int64                   m_iFrameCount;
    amf_int64                   m_iFramesDropped;
    amf_pts                     m_FpsStatStartTime;

    amf_double                  m_dLastFPS;
    amf_int                     m_instance;
    amf::AMFComponentPtr        m_pProcessor;
    amf::AMFComponentPtr        m_pHQScaler;
    Mode                        m_state;
    AMFRect                     m_rectClient;
    amf_pts                     m_currentTime;
    amf_bool                    m_bDoWait;
    amf_pts                     m_ptsDropThreshold;
    AVSyncObject*               m_pAVSync;
    amf_int32                   m_iSubresourceIndex;

    AMFRect                     m_sourceVertexRect;
    AMFRect                     m_destVertexRect;

    Transformation2D            m_srcToClientTransform;
    Transformation2D            m_srcToViewTransform;
    Transformation2D            m_normToViewTransform;
    Transformation2D            m_clientToSrcTransform;
    Transformation2D            m_texNormToViewTransform;
    Transformation2D            m_pipVertexTransform;
    Transformation2D            m_pipTexTransform;

    using ProjectionMatrix = amf_float[4][4];

    ProjectionMatrix            m_fNormalToViewMatrix;
    ProjectionMatrix            m_fTextureMatrix;
    ProjectionMatrix            m_pipNormalToViewMatrix;
    ProjectionMatrix            m_pipTextureMatrix;

    amf_bool                    m_bFullScreen;
    amf_bool                    m_bWaitForVSync;

    RenderViewSizeInfo          m_renderView;
    amf_float                   m_viewScale;
    amf_int                     m_viewOffsetX;
    amf_int                     m_viewOffsetY;

    static constexpr amf_float  PIP_SIZE_FACTOR = 0.3f;
    amf_bool                    m_enablePIP;
    amf_int32                   m_pipZoomFactor;
    AMFFloatPoint2D             m_pipFocusPos;

    amf_int                     m_iOrientation = 0;
};

inline AMFFloatPoint2D GetCenterFloatPoint(const AMFRect& rect)
{
    return AMFConstructFloatPoint2D((rect.left + rect.right) / 2.0f, (rect.top + rect.bottom) / 2.0f);
}

inline AMFFloatPoint2D GetCenterFloatPoint(const AMFSize& size)
{
    return AMFConstructFloatPoint2D(size.width / 2.0f, size.height / 2.0f);
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