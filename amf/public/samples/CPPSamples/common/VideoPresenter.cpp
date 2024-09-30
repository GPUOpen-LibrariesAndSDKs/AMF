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
#include "VideoPresenter.h"
#include "public/include/components/VideoConverter.h"
#include "public/include/components/HQScaler.h"
#include "public/common/TraceAdapter.h"

#ifdef _WIN32
#include "VideoPresenterDX9.h"
#include "VideoPresenterDX11.h"
#include "VideoPresenterDX12.h"
#endif

// VideoPresenterOpenGL has not been implemented for Android
#if !defined(__ANDROID__)
#include "VideoPresenterOpenGL.h"
#endif

#if !defined(DISABLE_VULKAN)
#include "VideoPresenterVulkan.h"
#endif

using namespace amf;

//#define USE_COLOR_TWITCH_IN_DISPLAY

#define AMF_FACILITY L"VideoPresenter"

#define WAIT_THRESHOLD          (5   * AMF_MILLISECOND) // 5 ms

#define DROP_THRESHOLD          (10  * AMF_MILLISECOND) // 10 ms

#define RESIZE_CHECK_TIME       (2   * AMF_MILLISECOND) // 2 ms

#define RESIZE_CHECK_THRESHOLD  (4   * AMF_MILLISECOND) // 4 ms

// if elapsed pts/local time difference more than this, then resync
#define RESYNC_THRESHOLD        (100  * AMF_MILLISECOND) // 100 ms

const VideoPresenter::Vertex VideoPresenter::QUAD_VERTICES_NORM[4]
{
    { {  0.0f,  1.0f, 0.0f },   { 0.0f, 0.0f } }, // Top left
    { {  1.0f,  1.0f, 0.0f },   { 1.0f, 0.0f } }, // top right
    { {  0.0f,  0.0f, 0.0f },   { 0.0f, 1.0f } }, // bottom left
    { {  1.0f,  0.0f, 0.0f },   { 1.0f, 1.0f } }, // bottom right
};

const VideoPresenter::Interpolation VideoPresenter::PIP_INTERPOLATION = VideoPresenter::InterpolationPoint;

VideoPresenter::VideoPresenter(amf_handle hwnd, AMFContext* pContext, amf_handle hDisplay) :
    m_pContext(pContext),
    m_hwnd(hwnd),
    m_hDisplay(hDisplay),

    m_inputFormat(AMF_SURFACE_UNKNOWN),
    m_inputFrameSize{},
    m_subresourceIndex(0),

    m_state(ModePlaying),
    m_pAVSync(nullptr),
    m_doWait(true),
    m_startTime(-1LL),
    m_startPts(-1LL),
    m_ptsDropThreshold(DROP_THRESHOLD),
    m_pLastFrame(nullptr),

    m_currentTime(0),
    m_frameCount(0),
    m_fpsStatStartTime(0),
    m_lastFPS(0),
    m_framesDropped(0),
    m_firstFrame(true),

    m_fullscreen(false),
    m_exclusiveFullscreen(false),
    m_fullscreenContext{},
    m_waitForVSync(false),
    m_resizeSwapChain(false),

    m_pProcessor(nullptr),
    m_pHQScaler(nullptr),
    m_renderToBackBuffer(false),

    m_viewOffsetX(0),
    m_viewOffsetY(0),
    m_viewScale(1.0f),
    m_orientation(0),

    m_interpolation(InterpolationLinear),

    m_enablePIP(false),
    m_pipZoomFactor(4),
    m_pipFocusPos{ 0.45f, 0.45f },

    m_viewProjection{},
    m_pipViewProjection{},
    m_srcToClientTransform{},
    m_srcToViewTransform{},
    m_normToViewTransform{},
    m_clientToSrcTransform{},
    m_texNormToViewTransform{},
    m_pipVertexTransform{},
    m_pipTexTransform{},
    m_renderView{},
    m_resizing{}
{
    amf_increase_timer_precision();
}

VideoPresenter::~VideoPresenter()
{
    Terminate();
}

AMF_RESULT VideoPresenter::Create(VideoPresenterPtr& pPresenter, AMF_MEMORY_TYPE type, amf_handle hwnd, AMFContext* pContext, amf_handle hDisplay)
{
    switch (type)
    {
#ifdef _WIN32
    case AMF_MEMORY_DX9:
        pPresenter = std::make_shared<VideoPresenterDX9>(hwnd, pContext);
        return AMF_OK;
    case AMF_MEMORY_DX11:
        pPresenter = std::make_shared<VideoPresenterDX11>(hwnd, pContext);
        return AMF_OK;
    case AMF_MEMORY_DX12:
        pPresenter = std::make_shared<VideoPresenterDX12>(hwnd, pContext);
        return AMF_OK;
#endif
    case AMF_MEMORY_OPENGL:
#if defined(__ANDROID__)
        AMFTraceError(AMF_FACILITY, L"VideoPresenterOpenGL has not been implemented for Android");
        return AMF_NOT_SUPPORTED;
#else
        pPresenter = std::make_shared<VideoPresenterOpenGL>(hwnd, pContext);
        return AMF_OK;
#endif
    case AMF_MEMORY_VULKAN:
#if defined(DISABLE_VULKAN)
#ifdef _WIN32
        pContext->InitDX11(NULL);
        pPresenter = std::make_shared<VideoPresenterDX11>(hwnd, pContext);
#endif
        UNREFERENCED_PARAMETER(hDisplay);
#else
        pPresenter = std::make_shared<VideoPresenterVulkan>(hwnd, pContext, hDisplay);
#endif
        return AMF_OK;
    default:
        return AMF_NOT_SUPPORTED;
    }
}

AMF_RESULT VideoPresenter::SetInputFormat(AMF_SURFACE_FORMAT format)
{
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"SetInputFormat() - SwapChain is not initialized");

    AMFLock lock(&m_cs);

    if (m_pSwapChain->FormatSupported(format) == false)
    {
        return AMF_NOT_SUPPORTED;
    }

    m_inputFormat = format;
    return AMF_OK;
}

AMF_RESULT VideoPresenter::Init(amf_int32 width, amf_int32 height, AMFSurface* /*pSurface*/)
{
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"Init() - m_pSwapChain not initialized");

    AMFLock lock(&m_cs);

    Reset();

    m_inputFrameSize.width = width;
    m_inputFrameSize.height = height;
    m_firstFrame = true;

    if (m_inputFormat == AMF_SURFACE_UNKNOWN)
    {
        SetInputFormat(m_pSwapChain->GetFormat());
    }
    //ResizeIfNeeded(); //DX9 will crash if trying to resize here

    return AMF_OK;
}

AMF_RESULT VideoPresenter::Terminate()
{
    AMFLock lock(&m_cs);

    Reset();
    SetProcessor(NULL, NULL, true);
    if (m_pSwapChain != nullptr)
    {
        m_pSwapChain->Terminate();
    }
    m_fullscreenContext = {};
    m_renderView = {};

    return AMF_OK;
}

AMF_RESULT VideoPresenter::Reset()
{
    AMFLock lock(&m_cs);

    // Waiting
    m_startTime = -1LL;
    m_startPts = -1LL;

    // Playback
    m_state = ModePlaying;
    m_currentTime = 0;

    // Stats
    m_frameCount = 0;
    m_framesDropped = 0;
    m_fpsStatStartTime = 0;
    m_lastFPS = 0;

    // Presenter variables
    m_firstFrame = false;
    m_pTrackSurfaces.clear();
    m_pLastFrame = nullptr;
    m_resizeSwapChain = false;

    return AMF_OK;
}

AMF_RESULT VideoPresenter::SubmitInput(AMFData* pData)
{
    AMFLock lock(&m_cs);
    if(m_bFrozen)
    {
        return AMF_OK;
    }

    if(pData != nullptr)
    {
        m_currentTime = pData->GetPts();
        if(m_pAVSync != NULL)
        {
             //AMFTraceWarning(AMF_FACILITY, L"First PTS=%5.2f", (amf_double)pData->GetPts() / 10000);
            if (m_pAVSync->IsVideoStarted() == false)
            {
                m_pAVSync->VideoStarted();
            }
            m_pAVSync->SetVideoPts(pData->GetPts());
        }

        m_frameCount++;

        AMFSurfacePtr pSurface(pData);
        lock.Unlock();
        const bool keep = WaitForPTS(pSurface->GetPts());
        lock.Lock();
        if (keep == false)
        {
            m_framesDropped++;
            return AMF_OK;
        }

        AMF_RESULT res = AMF_OK;
        switch(m_state)
        {
        case ModeStep:
            m_state = ModePaused;
            res = Present(pSurface);
            break;
        case ModePlaying:
            res = Present(pSurface);
            break;
        case ModePaused:
            return AMF_INPUT_FULL;
        default:
            return AMF_WRONG_STATE;
        }

        m_firstFrame = false;
        //m_pLastFrame = pSurface; // Todo: What if the surface is used elsewhere?
        //amf_sleep(1);
        return res;
    }
    return AMF_OK;
}

amf_bool VideoPresenter::WaitForPTS(amf_pts pts, amf_bool realWait)
{
    amf_bool ret = true;
    if (m_state == ModeStep || m_state == ModePaused)
    {
        return ret;
    }

    // diff is difference between elapsed pts and elapsed amf_high_precision_clock
    // if elapsed > local (positive diff), then pts is in the 'future'
    // if local > elapsed (negative diff), then pts is in the 'past'
    amf_pts diff = -RESYNC_THRESHOLD;
    if (m_startTime != -1LL && pts > 0 && pts >= m_startPts)
    {
        diff = CalculatePtsWaitDiff(pts);
        if (diff >= RESYNC_THRESHOLD || diff < 0)
        {
            AMFTraceDebug(AMF_FACILITY, L"Resync #%d pts=%5.2f ms diff=%5.2f ms", (int)m_frameCount, (amf_double)pts / AMF_MILLISECOND, (amf_double)diff / AMF_MILLISECOND);
            if (diff < -m_ptsDropThreshold) // ignore lags < 10 ms
            {
                //                AMFTraceWarning(AMF_FACILITY, L"+++ Drop Frame #%d pts=%5.2f time=%5.2f diff=%5.2f %s", (int)m_frameCount, (amf_double)pts / 10000., (amf_double)currTime / 10000., (amf_double)diff / 10000., bRealWait ? L"R" : L"");
                AMFTraceDebug(AMF_FACILITY, L"+++ Drop Frame #%d pts=%5.2f diff=%5.2f", (int)m_frameCount, (amf_double)pts / AMF_MILLISECOND, (amf_double)diff / AMF_MILLISECOND);
                ret = false;
            }
            diff = -RESYNC_THRESHOLD;
        }
    }

    if (diff >= 0)
    {
        if (m_doWait && realWait && diff > WAIT_THRESHOLD) // ignore delays < 5 ms
        {
            m_waiter.Wait(diff);
        }

        // update FPS = alpha filter
        if ((m_frameCount % 100) == 0)
        {
            amf_pts presentTime = amf_high_precision_clock() - m_startTime;
            amf_double currFPS = amf_double(AMF_SECOND) / ((presentTime - m_fpsStatStartTime) / 100.);
            m_lastFPS = currFPS;
            m_fpsStatStartTime = presentTime;
        }
    }
    else
    {
        m_startTime = amf_high_precision_clock();
        m_startPts = pts;
        m_fpsStatStartTime = 0;
    }

    return ret;
}

AMF_RESULT VideoPresenter::Present(AMFSurface* pSurface)
{
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"Present() - SwapChain is not set");
    AMF_RETURN_IF_FALSE(pSurface != nullptr, AMF_INVALID_ARG, L"Present() - pSurface is NULL");
    AMF_RETURN_IF_FALSE(pSurface->GetFormat() == GetInputFormat(), AMF_INVALID_FORMAT, L"Present() - Surface format (%s)"
        "does not match input format (%s)", AMFSurfaceGetFormatName(pSurface->GetFormat()),
        AMFSurfaceGetFormatName(GetInputFormat()));

    AMF_RESULT res = pSurface->Convert(GetMemoryType());
    AMF_RETURN_IF_FAILED(res, L"Present() - Surface Convert() failed");

    AMFLock lock(&m_cs);

#if defined(_WIN32) // Called seperately on Linux
    res = CheckForResize(false);
    AMF_RETURN_IF_FAILED(res, L"Present() - CheckForResize() failed");
#endif
    if (m_resizeSwapChain == true && CanResize() == true)
    {
#if defined(_WIN32)
        lock.Unlock();
        res = ResizeSwapChain();
        lock.Lock();
        AMF_RETURN_IF_FAILED(res, L"Present() - ResizeSwapChain() failed");
#endif
    }

    if (m_renderToBackBuffer == true)
    {
        amf_uint index = 0;
        res = m_pSwapChain->GetBackBufferIndex(pSurface, index);
        AMF_RETURN_IF_FALSE(res == AMF_OK || res == AMF_NOT_FOUND, res, L"Present() - GetBackBufferIndex() failed");
    }

    if (m_renderToBackBuffer == false || res == AMF_NOT_FOUND)
    {
        res = RenderToSwapChain(pSurface);
        AMF_RETURN_IF_FAILED(res, L"Present() - RenderToSwapChain() failed");
    }

    res = m_pSwapChain->Present(m_waitForVSync);
    AMF_RETURN_IF_FAILED(res, L"Present() - SwapChain Present() failed");
#if defined(__ANDROID__)
#elif defined(__linux)
    XFlush((Display*)m_hDisplay);
#endif

    UpdateProcessor();

    if (m_resizeSwapChain)
    {
        return AMF_RESOLUTION_UPDATED;
    }

    return AMF_OK;
}

AMF_RESULT VideoPresenter::RenderToSwapChain(amf::AMFSurface* pSurface)
{
    AMF_RETURN_IF_FALSE(pSurface != nullptr, AMF_INVALID_ARG, L"RenderToSwapChain() - pSurface is NULL");
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"RenderToSwapChain() - m_pSwapChain not initialized");

    const RenderTargetBase* pRenderTarget = nullptr;
    AMF_RESULT res = m_pSwapChain->AcquireNextBackBuffer(&pRenderTarget);
    AMF_RETURN_IF_FAILED(res, L"RenderToSwapChain() - AcquireNextBackBuffer() failed");

    res = RenderSurface(pSurface, pRenderTarget);
    if (res != AMF_OK)
    {
        AMF_RESULT dropRes = m_pSwapChain->DropBackBuffer(pRenderTarget);
        AMF_RETURN_IF_FALSE(dropRes == AMF_OK || dropRes == AMF_NOT_FOUND, dropRes, L"RenderToSwapChain() - DropBackBuffer() failed");
    }
    AMF_RETURN_IF_FAILED(res, L"RenderToSwapChain() - RenderSurface() failed");

    return AMF_OK;
}

AMF_RESULT VideoPresenter::RenderSurface(amf::AMFSurface* pSurface, const RenderTargetBase* pRenderTarget)
{
    AMF_RETURN_IF_FALSE(pSurface != nullptr, AMF_INVALID_ARG, L"RenderSurface() - pSurface is NULL");
    AMF_RETURN_IF_FALSE(pRenderTarget != nullptr, AMF_INVALID_ARG, L"RenderSurface() - pRenderTarget is NULL");

    const AMFRect srcSurfaceRect = GetPlaneRect(pSurface->GetPlane(amf::AMF_PLANE_PACKED));
    const AMFRect dstSurfaceRect = GetClientRect(m_hwnd, m_hDisplay);
    const AMFSize dstSurfaceSize = pRenderTarget->GetSize();

    RenderViewSizeInfo renderView = {};
    AMF_RESULT res = GetRenderViewSizeInfo(srcSurfaceRect, dstSurfaceSize, dstSurfaceRect, renderView);
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - GetRenderViewSizeInfo() failed");

    res = RenderSurface(pSurface, pRenderTarget, renderView);
    AMF_RETURN_IF_FAILED(res, L"RenderSurface() - RenderSurface() failed");

    return AMF_OK;
}

AMFRate VideoPresenter::GetDisplayRefreshRate()
{
    constexpr AMFRate DEFAULT_RATE = { 60, 1 };

#if defined(_WIN32)
    if (m_hwnd == nullptr)
    {
        return DEFAULT_RATE;
    }

    DisplayInfo displayInfo;
    AMF_RESULT res = GetDisplayInfo(m_hwnd, displayInfo);
    if (res != AMF_OK)
    {
        AMFTraceError(AMF_FACILITY, L"GetDisplayRefreshRate() - GetDisplayInfo() failed");
        return DEFAULT_RATE;
    }

    return displayInfo.frequency;
#else
    return DEFAULT_RATE;
#endif
}

AMF_RESULT VideoPresenter::CheckForResize(amf_bool force)
{
    AMFLock lock(&m_cs);
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"CheckForResize() - SwapChain is not initialized");

    if (m_resizeSwapChain == true)
    {
        return AMF_OK;
    }

    if (force == true || m_fullscreen != m_fullscreenContext.fullscreenState)
    {
        m_resizeSwapChain = true;
        return AMF_OK;
    }

    AMFRect  rectClient = GetClientRect(m_hwnd, m_hDisplay);
    const amf_int clientWidth = rectClient.Width();
    const amf_int clientHeight = rectClient.Height();

    if (clientWidth == 0 || clientHeight == 0)
    {
        return AMF_OK;
    }

    const AMFSize swapChainSize = GetSwapchainSize();

    if (clientWidth == swapChainSize.width && clientHeight == swapChainSize.height)
    {
        return AMF_OK;
    }

    m_resizeSwapChain = true;
    return AMF_OK;
}

AMF_RESULT VideoPresenter::ResizeSwapChain()
{
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"ResizeSwapChain() - SwapChain is not initialized");

    {
        AMFLock lock(&m_cs);

        if (m_resizeSwapChain == false || m_resizing == true)
        {
            return AMF_OK;
        }
        m_resizeSwapChain = false;

        m_resizing = true;

        if (m_pTrackSurfaces.find(m_pLastFrame.GetPtr()) != m_pTrackSurfaces.end())
        {
            m_pLastFrame = nullptr;
        }

        while (CanResize() == false)
        {
            lock.Unlock();
            lock.Lock(1);
        }

        SetFullscreenState(m_fullscreen);

        const AMFRect rect = GetClientRect(m_hwnd, m_hDisplay);

        RenderViewSizeInfo newRenderView = {};
        AMFSize dstSize = { rect.Width(), rect.Height() };
        AMFRect srcRect = { 0, 0, m_inputFrameSize.width, m_inputFrameSize.height };
        if (m_inputFrameSize.width == 0 && m_inputFrameSize.height == 0)
        {
            srcRect = { 0, 0, rect.Width(), rect.Height() };
        }
        AMF_RESULT res = GetRenderViewSizeInfo(srcRect, dstSize, rect, newRenderView);
        if (res != AMF_OK)
        {
            AMFTraceWarning(AMF_FACILITY, L"ResizeSwapChain() - GetRenderViewSizeInfo() failed %s", AMFGetResultText(res));
        }

        res = ResizeRenderView(newRenderView);
        if (res != AMF_OK)
        {
            AMFTraceWarning(AMF_FACILITY, L"ResizeSwapChain() - ResizeRenderView() failed %s", AMFGetResultText(res));
        }

        res = m_pSwapChain->Resize(rect.Width(), rect.Height(), m_fullscreen);
        m_resizing = false;
        AMF_RETURN_IF_FAILED(res, L"ResizeSwapChain() - SwapChain Resize() failed");
    }//UpdateProcessor in not under lock
    UpdateProcessor();

    return AMF_OK;
}

void AMF_STD_CALL VideoPresenter::ResizeIfNeeded()
{
    if (m_pSwapChain == nullptr || m_pSwapChain->IsInitialized() == false)
    {
        return;
    }

    AMF_RESULT res = CheckForResize(false);
    if (res != AMF_OK)
    {
        AMFTraceError(AMF_FACILITY, L"ResizeIfNeeded() - CheckForResize() failed");
    }

    res = ResizeSwapChain();
    if (res != AMF_OK)
    {
        AMFTraceError(AMF_FACILITY, L"ResizeIfNeeded() - ResizeSwapChain() failed");
    }
}

AMF_RESULT VideoPresenter::ApplyCSC(AMFSurface* pSurface)
{
    AMFLock lock(&m_cs);

    pSurface; // Suppress C4100 Unreferenced parameter warning
#ifdef USE_COLOR_TWITCH_IN_DISPLAY
    AMF_RETURN_IF_FALSE(pSurface != nullptr, AMF_INVALID_ARG, L"ApplyCSC() - pSurface is NULL");

    if (m_firstFrame == false || m_pSwapChain == nullptr)
    {
        return AMF_OK;
    }

    AMFVariant varBuf;
    AMF_RESULT res = pSurface->GetProperty(AMF_VIDEO_DECODER_HDR_METADATA, &varBuf);
    if (res != AMF_OK || varBuf.type != AMF_VARIANT_INTERFACE)
    {
        return AMF_OK;
    }

    AMFBufferPtr pBuffer(varBuf.pInterface);
    if (pBuffer == nullptr)
    {
        return AMF_OK;
    }

    AMFHDRMetadata* pHDRData = (AMFHDRMetadata*)pBuffer->GetNative();

    res = m_pSwapChain->SetHDRMetaData(pHDRData);
    if (res == AMF_NOT_SUPPORTED)
    {
        return AMF_OK;
    }
    AMF_RETURN_IF_FAILED(res, L"ApplyCSC() - SetHDRMetaData() failed");

#endif
    return AMF_OK;
}

AMF_RESULT VideoPresenter::CalcOutputRect(const AMFRect* pSrcRect, const AMFRect* pScreenRect, AMFRect* pOutputRect) const
{
    amf_double dDstRatio = pScreenRect->Height() / (amf_double)pScreenRect->Width();
    amf_double dSrcRatio = pSrcRect->Height() / (amf_double)pSrcRect->Width();

    // TODO : add aspect ratio from frame
    //dSrcRatio /= frameApectRatioFromDecoder;

    if(dDstRatio > dSrcRatio)
    {   // empty areas on top and bottom
        pOutputRect->left = 0;
        pOutputRect->right = pScreenRect->Width();
        amf_int lViewHeight = amf_int(pScreenRect->Width() * dSrcRatio);
        pOutputRect->top = (pScreenRect->Height() - lViewHeight) / 2;
        pOutputRect->bottom = pOutputRect->top + lViewHeight;
    }
    else
    {   // empty areas on left and right
        pOutputRect->bottom = pScreenRect->Height();
        pOutputRect->top = 0;
        amf_int lViewWidth = amf_int(pScreenRect->Height() / dSrcRatio);
        pOutputRect->left = (pScreenRect->Width() - lViewWidth) / 2;
        pOutputRect->right = pOutputRect->left + lViewWidth;
    }
    return AMF_OK;
}

AMF_RESULT VideoPresenter::GetRenderViewSizeInfo(const AMFRect& srcSurfaceRect, const AMFSize& dstSurfaceSize, const AMFRect& dstSurfaceRect, RenderViewSizeInfo& renderView) const
{
    AMF_RETURN_IF_FALSE(srcSurfaceRect.Width() > 0 && srcSurfaceRect.Height() > 0, AMF_INVALID_ARG, L"GetRenderViewRects() - Invalid src size (%dx%d)", srcSurfaceRect.Width(), srcSurfaceRect.Height());
    // dst may be 0,0 when minimized
    AMF_RETURN_IF_FALSE(dstSurfaceRect.Width() >= 0 && dstSurfaceRect.Height() >= 0, AMF_INVALID_ARG, L"GetRenderViewRects() - Invalid dst size (%dx%d)", dstSurfaceRect.Width(), dstSurfaceRect.Height());

    renderView.srcRect = srcSurfaceRect;
    renderView.srcSize = AMFConstructSize(srcSurfaceRect.Width(), srcSurfaceRect.Height());
    renderView.dstSize = dstSurfaceSize;
    renderView.rotation = GetOrientationRadians();

    AMF_RESULT res = ScaleAndCenterRect(renderView.srcRect, dstSurfaceRect, renderView.rotation, renderView.dstRect);
    AMF_RETURN_IF_FAILED(res, L"GetRenderViewRects() - ScaleAndCenterRect() failed");
    AMF_RETURN_IF_FALSE(renderView.dstRect.Width() >= 0 && renderView.dstRect.Height() >= 0, AMF_UNEXPECTED,
        L"GetRenderViewRects() - Invalid output rect size (%dx%d)", renderView.dstRect.Width(), renderView.dstRect.Height());

    res = TransformRect(renderView.dstRect, m_viewOffsetX, m_viewOffsetY, m_viewScale);
    AMF_RETURN_IF_FAILED(res, L"GetRenderViewRects() - TransformRect() failed");

    renderView.pipDstRect = AMFConstructRect(0,
                                            (amf_int32)(renderView.dstSize.height - renderView.srcSize.height * PIP_SIZE_FACTOR),
                                            (amf_int32)(renderView.srcSize.width * PIP_SIZE_FACTOR),
                                            (amf_int32)renderView.dstSize.height);

    renderView.pipSrcRect.left = (amf_int32)m_pipFocusPos.x + renderView.dstRect.left;
    renderView.pipSrcRect.top = (amf_int32)m_pipFocusPos.y + renderView.dstRect.top;
    renderView.pipSrcRect.right = renderView.pipSrcRect.left + renderView.pipDstRect.Width() / m_pipZoomFactor;
    renderView.pipSrcRect.bottom = renderView.pipSrcRect.top + renderView.pipDstRect.Height() / m_pipZoomFactor;

    return AMF_OK;
}

AMF_RESULT VideoPresenter::OnRenderViewResize(const RenderViewSizeInfo& newRenderView)
{
    AMFLock lock(&m_cs);

    const AMFRect& vertexViewRect = GetVertexViewRect();
    const AMFRect& textureViewRect = GetTextureViewRect();

    AMF_RESULT res = CalcVertexTransformation(newRenderView.srcSize, newRenderView.dstRect, newRenderView.dstSize, vertexViewRect,
                                               newRenderView.rotation, &m_srcToClientTransform, &m_srcToViewTransform, &m_normToViewTransform);
    AMF_RETURN_IF_FAILED(res, L"OnRenderViewResize() - CalcVertexTransformMatrix() failed to get view vertex transform");

    TransformationToMatrix(m_normToViewTransform, m_viewProjection.vertexTransform);
    m_clientToSrcTransform = Inverse(m_srcToClientTransform);

    if (newRenderView.srcRect != m_renderView.srcRect || newRenderView.srcSize != m_renderView.srcSize)
    {
        res = ProjectRectToView(newRenderView.srcSize, newRenderView.srcRect, textureViewRect, m_texNormToViewTransform);
        AMF_RETURN_IF_FAILED(res, L"OnRenderViewResize() - ProjectRectToView() failed to get view texture transform");
        TransformationToMatrix(m_texNormToViewTransform, m_viewProjection.texTransform);
    }

    // Pip vertex transform
    if (newRenderView.pipDstRect != m_renderView.pipDstRect || newRenderView.dstSize != m_renderView.dstSize)
    {
        res = ProjectRectToView(newRenderView.dstSize, newRenderView.pipDstRect, vertexViewRect, m_pipVertexTransform);
        AMF_RETURN_IF_FAILED(res, L"OnRenderViewResize() - ProjectRectToView() failed to get PIP view vertex transform");
        TransformationToMatrix(m_pipVertexTransform, m_pipViewProjection.vertexTransform);
    }

    // Pip texture transform
    if (newRenderView.pipSrcRect != m_renderView.pipSrcRect || newRenderView.srcRect != m_renderView.srcRect || newRenderView.srcSize != m_renderView.srcSize)
    {
        const AMFFloatPoint2D srcCenter = GetCenterFloatPoint(newRenderView.srcSize);
        const AMFFloatPoint2D pipSrcRectCenter = GetCenterFloatPoint(newRenderView.pipSrcRect);
        const AMFFloatPoint2D texViewRectCenter = GetCenterFloatPoint(textureViewRect);

        m_pipTexTransform = ConstructIdentity();
        Translate(m_pipTexTransform, -0.5f, -0.5f);
        Scale(m_pipTexTransform, (amf_float)newRenderView.pipSrcRect.Width(), (amf_float)newRenderView.pipSrcRect.Height());
        Translate(m_pipTexTransform, pipSrcRectCenter.x, pipSrcRectCenter.y);

        m_pipTexTransform = Combine(m_clientToSrcTransform, m_pipTexTransform);

        Translate(m_pipTexTransform, newRenderView.srcRect.left - srcCenter.x, newRenderView.srcRect.top - srcCenter.y);
        Scale(m_pipTexTransform, textureViewRect.Width() / (amf_float)newRenderView.srcRect.Width(),
            -textureViewRect.Height() / (amf_float)newRenderView.srcRect.Height());

        Translate(m_pipTexTransform, texViewRectCenter.x, texViewRectCenter.y);
        TransformationToMatrix(m_pipTexTransform, m_pipViewProjection.texTransform);
    }

    return AMF_OK;
}

AMF_RESULT VideoPresenter::ResizeRenderView(const RenderViewSizeInfo& newRenderView)
{
    AMFLock lock(&m_cs);

    if (m_renderView != newRenderView)
    {
        AMF_RESULT res = OnRenderViewResize(newRenderView);
        AMF_RETURN_IF_FAILED(res, L"ResizeRenderView() - OnRenderViewResize() failed");

        m_renderView = newRenderView;
    }

    return AMF_OK;
}

AMF_RESULT VideoPresenter::SetProcessor(AMFComponent *processor, AMFComponent* pHQScaler, bool bAllocator)
{
    AMFLock lock(&m_cs);
    if(m_pProcessor != nullptr)
    {
        m_pProcessor->SetOutputDataAllocatorCB(nullptr);
    }

    if (m_pHQScaler != nullptr)
    {
        m_pHQScaler->SetOutputDataAllocatorCB(nullptr);
    }

    m_pProcessor = processor;
    m_pHQScaler = pHQScaler;

    if (SupportAllocator() && bAllocator)
    {
        AMFComponent* pAllocatorUser = (m_pHQScaler != nullptr) ? m_pHQScaler : m_pProcessor;
        if (pAllocatorUser != nullptr)
        {
            pAllocatorUser->SetOutputDataAllocatorCB(this);
        }
    }

    SetRenderToBackBuffer((m_pProcessor != NULL) && SupportAllocator() && bAllocator);

    UpdateProcessor();
    return AMF_OK;
}

void VideoPresenter::UpdateProcessor()
{
    SwapChain::ColorSpace colorSpace = {};
    AMFHDRMetadata hdrMetaData = {};
    amf::AMFComponentPtr     pProcessor;
    amf::AMFComponentPtr     pHQScaler;
    bool                    bSwapChain = m_pSwapChain != nullptr;
    AMFBufferPtr             pHDRMetaDataBuffer;
    AMFSize outputSize = {};
    AMFRect outputRect = {};
    AMFRect rectClient = {};

    {
        AMFLock lock(&m_cs);

        if (m_pSwapChain != nullptr && (m_pProcessor != nullptr || m_pHQScaler != nullptr))
        {
            // check and set color space and HDR support
            AMF_RESULT res = m_pSwapChain->GetColorSpace(colorSpace);
            if (res != AMF_OK)
            {
                AMFTraceError(AMF_FACILITY, L"UpdateProcessor() - GetColorSpace() failed");
                return;
            }
            res = m_pSwapChain->GetOutputHDRMetaData(hdrMetaData);
            if (res != AMF_OK)
            {
                AMFTraceError(AMF_FACILITY, L"UpdateProcessor() - GetOutputHDRMetaData() failed");
                return;
            }
            if (hdrMetaData.maxMasteringLuminance != 0)
            {
                res = m_pContext->AllocBuffer(AMF_MEMORY_HOST, sizeof(AMFHDRMetadata), &pHDRMetaDataBuffer);
                if (res != AMF_OK)
                {
                    AMFTraceError(AMF_FACILITY, L"UpdateProcessor() - AllocBuffer() failed to allocate HDR metadata buffer");
                    return;
                }

                AMFHDRMetadata* pData = (AMFHDRMetadata*)pHDRMetaDataBuffer->GetNative();
                memcpy(pData, &hdrMetaData, sizeof(AMFHDRMetadata));
            }
        }
        pProcessor = m_pProcessor;
        pHQScaler = m_pHQScaler;

        rectClient = GetClientRect(m_hwnd, m_hDisplay);
        if (m_renderToBackBuffer == true)
        {
            ScaleAndCenterRect(SizeToRect(m_inputFrameSize), rectClient, 0, outputRect);
            outputSize = RectToSize(rectClient);
        }
        else
        {
            outputSize = m_inputFrameSize;
            outputRect = SizeToRect(outputSize);
        }

    }
    if (pProcessor != nullptr && pHQScaler == nullptr)
    {

        // what we want to do here is check for the properties if they exist
        // as the HQ scaler has different property names than CSC
        const AMFPropertyInfo* pParamInfo = nullptr;
        if ((pProcessor->GetPropertyInfo(AMF_VIDEO_CONVERTER_OUTPUT_SIZE, &pParamInfo) == AMF_OK) && pParamInfo)
        {
            pProcessor->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_SIZE, outputSize);
            pProcessor->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_RECT, outputRect);
        }
    }

    if (pHQScaler != NULL)
    {
        const AMFPropertyInfo* pParamInfo = nullptr;
        if ((pHQScaler->GetPropertyInfo(AMF_HQ_SCALER_OUTPUT_SIZE, &pParamInfo) == AMF_OK) && pParamInfo)
        {

            pHQScaler->SetProperty(AMF_HQ_SCALER_OUTPUT_SIZE, ::AMFConstructSize(rectClient.Width(), rectClient.Height()));
        }
    }

    if (pProcessor == nullptr || bSwapChain == false)
    {
        return;
    }

    m_pProcessor->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_TRANSFER_CHARACTERISTIC, colorSpace.transfer);
    m_pProcessor->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_COLOR_PRIMARIES, colorSpace.primaries);
    m_pProcessor->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_COLOR_RANGE, colorSpace.range);
    m_pProcessor->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_HDR_METADATA, pHDRMetaDataBuffer);
}

AMF_RESULT AMF_STD_CALL VideoPresenter::AllocSurface(AMF_MEMORY_TYPE /*type*/, AMF_SURFACE_FORMAT /*format*/, amf_int32 /*width*/,
    amf_int32 /*height*/, amf_int32 /*hPitch*/, amf_int32 /*vPitch*/, AMFSurface** ppSurface)
{
    if (SupportAllocator() == false || m_renderToBackBuffer == false)
    {
        return AMF_NOT_SUPPORTED;
    }

    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"RenderToSwapChain() - SwapChain is not set");
    //AMF_RETURN_IF_FALSE(format == m_pSwapChain->GetFormat(), AMF_INVALID_ARG, L"AllocSurface() - Format (%s)"
    //    "does not match swapchain format (%s)", AMFSurfaceGetFormatName(format),
    //    AMFSurfaceGetFormatName(GetInputFormat()));

    //const AMFSize swapChainSize = GetSwapchainSize();
    //AMF_RETURN_IF_FALSE(width == 0 || width == swapChainSize.width, AMF_INVALID_ARG, L"AllocSurface() - width(%d) must be either 0 or equal swapchain width(%d)", width, swapChainSize.width);
    //AMF_RETURN_IF_FALSE(height == 0 || height == swapChainSize.height, AMF_INVALID_ARG, L"AllocSurface() - height(%d) must be either 0 or equal swapchain height(%d)", height, swapChainSize.height);

    AMF_RETURN_IF_FALSE(ppSurface != nullptr, AMF_INVALID_ARG, L"AllocSurface() - ppSurface is NULL");


    {
        AMFLock lock(&m_cs);
        CheckForResize(false);

        if (m_resizeSwapChain == true && (CanResize() == false))
        {
            AMFTraceWarning(AMF_FACILITY, L"AllocSurface() - swapchain resize needed");
            return AMF_INPUT_FULL;
        }
    }
    ResizeIfNeeded();
    //while (m_pSwapChain->GetBackBuffersAvailable() == 0)
    //{
    //    if (m_bFrozen)
    //    {
    //        return AMF_INPUT_FULL;
    //    }
    //    amf_sleep(1);
    //}

    //if (m_resizeSwapChain == true)
    //{
    //    //AMF_RESULT res = ResizeSwapChain();
    //    //AMF_RETURN_IF_FAILED(res, L"AllocSurface() - ResizeSwapChain() failed");
    //}
    AMFLock lock(&m_cs);

    while (m_resizing)
    {
        lock.Unlock();
        lock.Lock(1);
    }

    AMF_RESULT res = m_pSwapChain->AcquireNextBackBuffer(ppSurface);
    AMF_RETURN_IF_FAILED(res, L"AllocSurface() - AcquireNextBackBuffer() failed");

    // check and set color space, should be used by caller (processor) to determine if target is HDR, etc
    SwapChain::ColorSpace colorSpace = {};
    res = m_pSwapChain->GetColorSpace(colorSpace);
    if (res == AMF_OK)
    {
        (*ppSurface)->SetProperty(AMF_VIDEO_COLOR_TRANSFER_CHARACTERISTIC, colorSpace.transfer);
        (*ppSurface)->SetProperty(AMF_VIDEO_COLOR_PRIMARIES, colorSpace.primaries);
        (*ppSurface)->SetProperty(AMF_VIDEO_COLOR_RANGE, colorSpace.range);
    }

    (*ppSurface)->AddObserver(this);
    m_pTrackSurfaces.insert(*ppSurface);

    //res = (*ppSurface)->Convert(type);
    //AMF_RETURN_IF_FAILED(res, L"AllocSurface() - Convert() failed");

    return AMF_OK;
}

void AMF_STD_CALL VideoPresenter::OnSurfaceDataRelease(AMFSurface* pSurface)
{
    if (pSurface == nullptr)
    {
        return;
    }

    AMFLock lock(&m_cs);

    auto it = m_pTrackSurfaces.find(pSurface);

    if (it == m_pTrackSurfaces.end())
    {
        return;
    }

    pSurface->RemoveObserver(this);
    m_pTrackSurfaces.erase(it);

    if (m_pSwapChain != nullptr)
    {
        // Drop backbuffer if acquired
        AMF_RESULT res = m_pSwapChain->DropBackBuffer(pSurface);
        if (res != AMF_OK && res != AMF_NOT_FOUND)
        {
            AMFTraceError(AMF_FACILITY, L"OnSurfaceDataRelease() - DropBackBuffer() failed unexpectedly");
        }
    }
}

AMF_RESULT VideoPresenter::Freeze()
{
    AMFLock lock(&m_cs);
    return PipelineElement::Freeze();
}

AMF_RESULT VideoPresenter::UnFreeze()
{
    AMFLock lock(&m_cs);
    m_startTime = -1LL;
    m_startPts = -1LL;
    return PipelineElement::UnFreeze();
}

AMFRect VideoPresenter::GetSourceRect() const
{
    AMFLock lock(&m_cs);

    AMFRect out = {};
    if (m_pProcessor == nullptr)
    {
        out = m_renderView.srcRect;
    }
    else
    {
        m_pProcessor->GetProperty(AMF_VIDEO_CONVERTER_OUTPUT_SIZE, &out);
    }
    return out;
}

AMF_RESULT VideoPresenter::Resume()
{
    AMFLock lock(&m_cs);
    m_state = ModePlaying;
    m_startTime = -1LL;
    m_startPts = -1LL;
    return AMF_OK;
}

AMF_RESULT VideoPresenter::DropFrame()
{
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"RenderToSwapChain() - SwapChain is not set");
    AMFLock lock(&m_cs);
    return m_pSwapChain->DropLastBackBuffer();
}

AMF_RESULT VideoPresenter::SetFullscreenState(amf_bool fullscreen)
{
    // Fullscreen on DX9 only works if swapchain and device were both
    // created with fullscreen settings so instead, fullscreen is set
    // manually by controlling window position and size.
    AMF_RESULT res = AMF_NOT_IMPLEMENTED;
    if (true == m_exclusiveFullscreen)
    {
        res = m_pSwapChain->SetExclusiveFullscreenState(fullscreen);
        //AMF_RETURN_IF_FALSE(res == AMF_OK || res == AMF_NOT_IMPLEMENTED, res, L"SetFullscreenState() - SetWindowFullscreenState() failed");
        if (res == AMF_OK)
        {
            m_fullscreenContext.fullscreenState = fullscreen;
            return res;
        }
        AMFTraceWarning(AMF_FACILITY, L"SetFullscreenState() SetExclusiveFullscreenState failed %s - fall back to borderless window", AMFGetResultText(res));
    }

    // fall back to windowed fullscreen if not implemented or failed
    res = SetWindowFullscreenState(m_hwnd, m_hDisplay, fullscreen);
    AMF_RETURN_IF_FAILED(res, L"SetFullscreenState() - SetWindowFullscreenState() failed");

    m_fullscreenContext.fullscreenState = fullscreen;
    return res;
}

AMF_RESULT VideoPresenter::SetWindowFullscreenState(amf_handle hwnd, amf_handle hDisplay, amf_bool fullscreen)
{
    hDisplay; // Suppress unreferenced parameter warning (C4100)

    AMF_RETURN_IF_FALSE(hwnd != nullptr, AMF_INVALID_ARG, L"SetWindowFullscreenState() - hwnd is NULL");

    if (m_fullscreenContext.fullscreenState == fullscreen)
    {
        return AMF_OK;
    }

#if defined(_WIN32)

    // Should only set fullscreen if holding top-most window
    if ((HWND)hwnd != ::GetAncestor((HWND)hwnd, GA_ROOT))
    {
        return AMF_OK;
    }

    LONG_PTR style = 0;
    LONG_PTR exStyle = 0;
    AMFPoint origin = {};
    AMFSize size = {};
    const HWND hWnd = (HWND)hwnd;
    UINT posFlags = SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE;

    if (fullscreen)
    {
        WINDOWINFO info;
        ::GetWindowInfo(hWnd, &info);
        m_fullscreenContext.windowModeRect = AMFConstructRect(info.rcWindow.left, info.rcWindow.top, info.rcWindow.right, info.rcWindow.bottom);
        m_fullscreenContext.windowModeStyle = ::GetWindowLongPtr(hWnd, GWL_STYLE);
        m_fullscreenContext.windowModeExStyle = ::GetWindowLongPtr(hWnd, GWL_EXSTYLE);

        style = WS_VISIBLE | WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        exStyle = WS_EX_APPWINDOW | WS_EX_TOPMOST;

        // Get display information
        DisplayInfo displayInfo = {};
        AMF_RESULT res = GetDisplayInfo(hwnd, displayInfo);
        AMF_RETURN_IF_FAILED(res, L"SetWindowFullscreenState() - GetDisplayInfo() failed");

        origin.x = displayInfo.monitorRect.left;
        origin.y = displayInfo.monitorRect.top;
        size.width = displayInfo.monitorRect.Width();
        size.height = displayInfo.monitorRect.Height();
    }
    else
    {
        style = m_fullscreenContext.windowModeStyle;
        AMF_RETURN_IF_FALSE(style != 0, AMF_NOT_INITIALIZED, L"SetWindowFullscreenState() - Window mode not initialized");

        exStyle = m_fullscreenContext.windowModeExStyle;
        origin.x = m_fullscreenContext.windowModeRect.left;
        origin.y = m_fullscreenContext.windowModeRect.top;
        size.width = m_fullscreenContext.windowModeRect.Width();
        size.height = m_fullscreenContext.windowModeRect.Height();
    }

    AMF_RETURN_IF_FALSE(size.width > 0 && size.height > 0, AMF_INVALID_RESOLUTION, L"SetWindowFullscreenState() - Invalid window size %ux%u", size.width, size.height);

    ::SetWindowLongPtr(hWnd, GWL_STYLE, style);
    ::SetWindowLongPtr(hWnd, GWL_EXSTYLE, exStyle);

    //ShowCursor(fullscreen ? FALSE : TRUE);

    LONG ret = ::SetWindowPos(hWnd, fullscreen ? HWND_TOPMOST : HWND_NOTOPMOST, origin.x, origin.y, size.width, size.height, posFlags);
    AMF_RETURN_IF_FALSE(ret != 0, AMF_FAIL, L"SetWindowFullscreenState() - SetWindowPos() failed, code=%d", GetLastError());

#elif defined(__ANDROID__)
    // TODO
#elif defined(__linux)
    // TODO
#else
    return AMF_NOT_SUPPORTED;
#endif

    m_fullscreenContext.fullscreenState = fullscreen;
    return AMF_OK;
}

// Utility functions
AMF_RESULT TransformRect(AMFRect& rect, amf_int offsetX, amf_int offsetY, amf_float scale)
{
    // Offset the output rect
    rect.left += offsetX;
    rect.right += offsetX;
    rect.top -= offsetY;
    rect.bottom -= offsetY;

    // Scale the output rect from center
    const AMFPoint outputRectCenter = AMFConstructPoint((rect.left + rect.right) / 2, (rect.bottom + rect.top) / 2);
    rect.left = (amf_int32)((rect.left - outputRectCenter.x) * scale + outputRectCenter.x);
    rect.right = (amf_int32)((rect.right - outputRectCenter.x) * scale + outputRectCenter.x);
    rect.top = (amf_int32)((rect.top - outputRectCenter.y) * scale + outputRectCenter.y);
    rect.bottom = (amf_int32)((rect.bottom - outputRectCenter.y) * scale + outputRectCenter.y);

    return AMF_OK;
}

AMFRect GetPlaneRect(AMFPlane* pPlane)
{
    return pPlane == nullptr ?
        AMFConstructRect(0, 0, 0, 0) :
        AMFConstructRect(pPlane->GetOffsetX(),                          // Left
                        pPlane->GetOffsetY(),                           // Top
                        pPlane->GetOffsetX() + pPlane->GetWidth(),      // Right
                        pPlane->GetOffsetY() + pPlane->GetHeight());    // Bottom
}

AMF_RESULT ScaleAndCenterRect(const AMFRect& srcRect, const AMFRect& dstRect, amf_float rotation, AMFRect& outputRect)
{
    // Centers srcRect in dstRect after applying the rotation
    // Scales to fit while maintaining aspect ratio

    const amf_int32 srcWidth = srcRect.Width();
    const amf_int32 srcHeight = srcRect.Height();

    const amf_int32 dstWidth = dstRect.Width();
    const amf_int32 dstHeight = dstRect.Height();

    AMF_RETURN_IF_FALSE(srcWidth > 0 && srcHeight > 0, AMF_INVALID_ARG, L"ScaleAndCenterRect() - src size is invalid: %dx%d", srcWidth, srcHeight);
    AMF_RETURN_IF_FALSE(dstWidth >= 0 && dstHeight >= 0, AMF_INVALID_ARG, L"ScaleAndCenterRect() - dst size is invalid: %dx%d", dstWidth, dstHeight);

    const amf_float rotSin = sinf(rotation);
    const amf_float rotCos = cosf(rotation);

    const AMFPoint dstCenter = AMFConstructPoint((dstRect.left + dstRect.right) / 2, (dstRect.bottom + dstRect.top) / 2);

    // Scaling applied to the src to get it mapped onto the dst rect after applying rotation
    // With a rotation that is not a multiple of 90 degrees, the scaling ensures the
    // src surfaces edges fit inside the rect with the rotation
    const AMFFloatSize rotatedBoundary = AMFConstructFloatSize(
        srcWidth * abs(rotCos) + srcHeight * abs(rotSin),
        srcHeight * abs(rotCos) + srcWidth * abs(rotSin));

    const amf_float scale = AMF_MIN(dstWidth / rotatedBoundary.width, dstHeight / rotatedBoundary.height);

    const AMFSize rectSize = AMFConstructSize(amf_int32(scale * rotatedBoundary.width), amf_int32(scale * rotatedBoundary.height));

    outputRect.left = dstCenter.x - rectSize.width / 2;
    outputRect.right = outputRect.left + rectSize.width;
    outputRect.top = dstCenter.y - rectSize.height / 2;
    outputRect.bottom = outputRect.top + rectSize.height;

    return AMF_OK;
}

Transformation2D ConstructIdentity()
{
    Transformation2D transformation = {};
    transformation.scaleLinear = AMFConstructFloatPoint2D(1.0f, 1.0f);
    return transformation;
}

void Scale(Transformation2D& transformation, amf_float xScale, amf_float yScale)
{
    transformation.scaleLinear.x *= xScale;
    transformation.scaleLinear.y *= yScale;

    transformation.scaleOrtho.x *= yScale;
    transformation.scaleOrtho.y *= xScale;

    transformation.translation.x *= xScale;
    transformation.translation.y *= yScale;
}

void Translate(Transformation2D& transformation, amf_float xOffset, amf_float yOffset)
{
    transformation.translation.x += xOffset;
    transformation.translation.y += yOffset;
}

void Rotate(Transformation2D& transformation, amf_float radians)
{
    const amf_float cosVal = cosf(radians);
    const amf_float sinVal = sinf(radians);

    Transformation2D result;
    result.scaleLinear.x = transformation.scaleLinear.x * cosVal + transformation.scaleOrtho.x * sinVal;
    result.scaleOrtho.x = transformation.scaleOrtho.x * cosVal - transformation.scaleLinear.x * sinVal;

    result.scaleLinear.y = transformation.scaleLinear.y * cosVal - transformation.scaleOrtho.y * sinVal;
    result.scaleOrtho.y = transformation.scaleOrtho.y * cosVal + transformation.scaleLinear.y * sinVal;

    result.translation.x = transformation.translation.x * cosVal + transformation.translation.y * sinVal;
    result.translation.y = transformation.translation.y * cosVal - transformation.translation.x * sinVal;
    transformation = result;
}

Transformation2D Combine(const Transformation2D& left, const Transformation2D& right)
{
    Transformation2D result = {};
    // Applies left to right
    result.scaleLinear.x = left.scaleLinear.x * right.scaleLinear.x + left.scaleOrtho.y  * right.scaleOrtho.x;
    result.scaleOrtho.x =  left.scaleOrtho.x  * right.scaleLinear.x + left.scaleLinear.y * right.scaleOrtho.x;

    result.scaleLinear.y = left.scaleOrtho.x * right.scaleOrtho.y + left.scaleLinear.y * right.scaleLinear.y;
    result.scaleOrtho.y = left.scaleLinear.x * right.scaleOrtho.y + left.scaleOrtho.y * right.scaleLinear.y;

    result.translation.x = left.scaleLinear.x * right.translation.x + left.scaleOrtho.y * right.translation.y + left.translation.x;
    result.translation.y = left.scaleOrtho.x * right.translation.x + left.scaleLinear.y * right.translation.y + left.translation.y;

    return result;
}

Transformation2D Inverse(const Transformation2D& transformation)
{
    Transformation2D inverse = {};
    // Consider transforming from srcPoint to dstPoint using the transformation
    // The inverse should go from dstPoint to srcPoint
    // We basically have to solve this equation to find srcPoint.x and srcPoint.y
    // srcPoint.x * matrix[0][0] + srcPoint.y * matrix[0][1] = dstPoint.x - matrix[0][3];
    // srcPoint.x * matrix[1][0] + srcPoint.y * matrix[1][1] = dstPoint.y - matrix[1][3];
    //
    // Using the inverse determinant formula for 2x2 matrix on the left

    const amf_float determinant = transformation.scaleLinear.x * transformation.scaleLinear.y -
                              transformation.scaleOrtho.x * transformation.scaleOrtho.y;

    // Adjugate divided by determinant
    inverse.scaleLinear.x = transformation.scaleLinear.y / determinant;
    inverse.scaleLinear.y = transformation.scaleLinear.x / determinant;
    inverse.scaleOrtho.x = -transformation.scaleOrtho.x  / determinant;
    inverse.scaleOrtho.y = -transformation.scaleOrtho.y  / determinant;

    // Now we have to add the translations to the matrix
    // To add this, we can just transform the translation with the inverse matrix so far
    // and add it as a translation to the 4th column
    inverse.translation.x = -transformation.translation.x * inverse.scaleLinear.x - transformation.translation.y * inverse.scaleOrtho.y;
    inverse.translation.y = -transformation.translation.x * inverse.scaleOrtho.x  - transformation.translation.y * inverse.scaleLinear.y;

    return inverse;
}

void TransformationToMatrix(const Transformation2D& transformation, amf_float matrix[4][4])
{
    const amf_float m[4][4] =
    {
        {transformation.scaleLinear.x,      transformation.scaleOrtho.y,    0.0f,   transformation.translation.x},
        {transformation.scaleOrtho.x,       transformation.scaleLinear.y,   0.0f,   transformation.translation.y},
        {0.0f,                              0.0f,                           1.0f,   0.0f},
        {0.0f,                              0.0f,                           0.0f,   1.0f},
    };
    memcpy(matrix, m, sizeof(m));
}

void MatrixToTransformation(const amf_float matrix[4][4], Transformation2D& transformation)
{
    transformation.scaleLinear.x = matrix[0][0];
    transformation.scaleLinear.y = matrix[1][1];

    transformation.scaleOrtho.x  = matrix[1][0];
    transformation.scaleOrtho.y  = matrix[0][1];

    transformation.translation.x = matrix[0][3];
    transformation.translation.y = matrix[1][3];
}

AMFPoint TransformPoint(const AMFPoint& point, const Transformation2D& transformation)
{
    return AMFConstructPoint
    (
        amf_int32(point.x * transformation.scaleLinear.x + point.y * transformation.scaleOrtho.y + transformation.translation.x),
        amf_int32(point.x * transformation.scaleOrtho.x + point.y * transformation.scaleLinear.y + transformation.translation.y)
    );
}

AMFFloatPoint2D TransformPoint(const AMFFloatPoint2D& point, const Transformation2D& transformation)
{
    return AMFConstructFloatPoint2D
    (
        point.x * transformation.scaleLinear.x + point.y * transformation.scaleOrtho.y + transformation.translation.x,
        point.x * transformation.scaleOrtho.x + point.y * transformation.scaleLinear.y + transformation.translation.y
    );
}

AMFPoint TransformPoint(const AMFPoint& point, const amf_float matrix[4][4])
{
    return AMFConstructPoint
    (
        amf_int32(point.x * matrix[0][0] + point.y * matrix[0][1] + matrix[0][3]),
        amf_int32(point.x * matrix[1][0] + point.y * matrix[1][1] + matrix[1][3])
    );
}

AMFFloatPoint2D TransformPoint(const AMFFloatPoint2D& point, const amf_float matrix[4][4])
{
    return AMFConstructFloatPoint2D
    (
        point.x * matrix[0][0] + point.y * matrix[0][1] + matrix[0][3],
        point.x * matrix[1][0] + point.y * matrix[1][1] + matrix[1][3]
    );
}

amf_bool IsIdentity(const amf_float matrix[4][4])
{
    constexpr amf_float IDENTITY[4][4] =
    {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f}
    };

    return memcmp(matrix, IDENTITY, sizeof(IDENTITY)) == 0;
}

AMF_RESULT CalcVertexTransformation(const AMFSize& srcSize, const AMFRect& dstRect, const AMFSize& dstSize, const AMFRect& viewRect,
                                    amf_float rotation, Transformation2D* pSrcToDst, Transformation2D* pSrcToView, Transformation2D* pNormToView)
{
    // Computes a projection matrix for vertex shader to map a src surface onto the view.
    //
    // srcSize: Size of the surface to map. This only includes the part of the surface that actually gets mapped (no padding)
    // dstRect: The rect/subwindow of the destination surface to scale the surface to. No part of the surface will fall outside
    //           the area indicated by this argument. The surface will be scaled to use as much room as possible. With rotation
    //           the surface will be scaled so that the edges of the rotated rectangle touch the rect edges.
    //
    // dstSize: Total size of the destination surface. The destination surface in many cases is just the client window / swapchain size
    // viewRect: Rect describing the final view of the transformed vertices. Once the src surface is mapped onto the dest surface after
    //            rotation, the dst surface is mapped to this. This is used for normalizing the vertices to standards used by graphic API
    //            For example, DirectX has a clip space of -1 to 1 in both directions so the view rect should be set to this area.
    //
    // rotation: The rotation to apply to the src surface in radians. Direction is CW.
    // pSrcToDst: Transformation that transform from source coordinate to destination surface coordinate
    // pSrcToView: Transformation that transforms from source coordinate to view coordinates
    // pNormToView: Transformation that transforms from normalized coordinates (0-1 both directions) to view coordinates
    //
    // For transformations not required, they can be left as nullptr however at least 1 of the 3 must be specified.


    AMF_RETURN_IF_FALSE(pSrcToDst != nullptr || pSrcToView != nullptr || pNormToView != nullptr, AMF_INVALID_ARG,
        L"CalcVertexTransformMatrix() - At least one output matrix must be specified");

    const amf_int32 srcWidth = srcSize.width;
    const amf_int32 srcHeight = srcSize.height;

    const amf_int32 dstRectWidth = dstRect.Width();
    const amf_int32 dstRectHeight = dstRect.Height();
    const amf_int32 dstWidth = dstSize.width;
    const amf_int32 dstHeight = dstSize.height;

    // The other rects use pixel coordinate system with y = 0 at the top
    // View rect uses the standard cartesian coordinate system so flip height
    const amf_int32 viewRectWidth = viewRect.Width();
    const amf_int32 viewRectHeight = -viewRect.Height();

    AMF_RETURN_IF_FALSE(srcWidth > 0 && srcHeight > 0, AMF_INVALID_ARG, L"CalcVertexTransformMatrix() - srcSize is invalid: %dx%d", srcWidth, srcHeight);
    AMF_RETURN_IF_FALSE(dstRectWidth > 0 && dstRectHeight > 0, AMF_INVALID_ARG, L"CalcVertexTransformMatrix() - dstRect is invalid: %dx%d", dstRectWidth, dstRectHeight);
    AMF_RETURN_IF_FALSE(dstWidth > 0 && dstHeight > 0, AMF_INVALID_ARG, L"CalcVertexTransformMatrix() - dstSize is invalid: %dx%d", dstWidth, dstHeight);
    AMF_RETURN_IF_FALSE(viewRectWidth != 0 && viewRectHeight != 0, AMF_INVALID_ARG, L"CalcVertexTransformMatrix() - viewRect is invalid: %dx%d", viewRectWidth, viewRectHeight);

    const amf_float rotSin = sinf(rotation);
    const amf_float rotCos = cosf(rotation);

    const AMFFloatPoint2D srcCenter = GetCenterFloatPoint(srcSize);
    const AMFFloatPoint2D dstRectCenter = GetCenterFloatPoint(dstRect);
    const AMFFloatPoint2D dstCenter = GetCenterFloatPoint(dstSize);
    const AMFFloatPoint2D viewRectCenter = GetCenterFloatPoint(viewRect);

    Transformation2D transformation = ConstructIdentity();

    // Move src to center and rotate it around origin
    Translate(transformation, -srcCenter.x, -srcCenter.y);
    Rotate(transformation, rotation);

    // Scale the rotated image to fit within the dstRect
    // Scale so that the out edge of the rotated boundary
    // is scaled to touch the the dstRect bounds
    Scale(transformation, dstRectWidth / (srcWidth * abs(rotCos) + srcHeight * abs(rotSin)),
                          dstRectHeight / (srcHeight * abs(rotCos) + srcWidth * abs(rotSin)));

    // Move from origin to dstRect center
    Translate(transformation, dstRectCenter.x, dstRectCenter.y);

    if (pSrcToDst != nullptr)
    {
        *pSrcToDst = transformation;
    }

    if (pSrcToView == nullptr && pNormToView == nullptr)
    {
        return AMF_OK;
    }

    // Move dst center to origin
    Translate(transformation, -dstCenter.x, -dstCenter.y);

    // Scale the rect so that the dst size fits the view rect
    Scale(transformation, viewRectWidth / (amf_float)dstWidth, viewRectHeight / (amf_float)dstHeight);
    Translate(transformation, viewRectCenter.x, viewRectCenter.y);

    if (pSrcToView != nullptr)
    {
        *pSrcToView = transformation;
    }

    if (pNormToView != nullptr)
    {
        transformation.scaleLinear.x *= srcWidth;
        transformation.scaleLinear.y *= srcHeight;
        transformation.scaleOrtho.x  *= srcWidth;
        transformation.scaleOrtho.y  *= srcHeight;

        *pNormToView = transformation;
    }

    return AMF_OK;
}

AMF_RESULT ProjectRectToView(const AMFSize& srcSize, const AMFRect& srcRect, const AMFRect& viewRect, Transformation2D& normToView)
{
    const amf_int32 srcWidth = srcSize.width;
    const amf_int32 srcHeight = srcSize.height;

    const amf_int32 srcRectWidth = srcRect.Width();
    const amf_int32 srcRectHeight = srcRect.Height();

    const amf_int32 viewRectWidth = viewRect.Width();
    const amf_int32 viewRectHeight = -viewRect.Height();

    AMF_RETURN_IF_FALSE(srcWidth > 0 && srcHeight > 0, AMF_INVALID_ARG, L"ProjectRectToView() - srcSize is invalid: %dx%d", srcWidth, srcHeight);
    AMF_RETURN_IF_FALSE(srcRectWidth >= 0 && srcRectHeight >= 0, AMF_INVALID_ARG, L"ProjectRectToView() - srcRect is invalid: %dx%d", srcRectWidth, srcRectHeight);
    AMF_RETURN_IF_FALSE(viewRectWidth != 0 && viewRectHeight != 0, AMF_INVALID_ARG, L"ProjectRectToView() - viewRect is invalid: %dx%d", viewRectWidth, viewRectHeight);

    const AMFFloatPoint2D srcCenter = GetCenterFloatPoint(srcSize);
    const AMFFloatPoint2D srcRectCenter = GetCenterFloatPoint(srcRect);
    const AMFFloatPoint2D viewRectCenter = GetCenterFloatPoint(viewRect);

    normToView = ConstructIdentity();
    Translate(normToView, -0.5f, -0.5f);
    Scale(normToView, (amf_float)srcRectWidth, (amf_float)srcRectHeight);
    Translate(normToView, srcRectCenter.x - srcCenter.x, srcRectCenter.y - srcCenter.y);
    Scale(normToView, viewRectWidth / (amf_float)srcWidth, viewRectHeight / (amf_float)srcHeight);
    Translate(normToView, viewRectCenter.x, viewRectCenter.y);

    return AMF_OK;
}
