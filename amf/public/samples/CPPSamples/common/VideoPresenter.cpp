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

using namespace amf;

#define AMF_FACILITY L"VideoPresenter"

#define WAIT_THRESHOLD 5 * AMF_SECOND / 1000LL // 5 ms

#define DROP_THRESHOLD 10 * AMF_SECOND / 1000LL // 10 ms

VideoPresenter::VideoPresenter() :
    m_startTime(-1LL),
    m_startPts(-1LL),
    m_state(ModePlaying),
    m_iFrameCount(0),
    m_FpsStatStartTime(0),
    m_iFramesDropped(0),
    m_dLastFPS(0),
    m_instance(0),
    m_pProcessor(NULL),
    m_pHQScaler(NULL),
    m_currentTime(0),
    m_bDoWait(true),
    m_pAVSync(NULL),
    m_iSubresourceIndex(0),
    m_sourceVertexRect(AMFConstructRect(0, 0, 0, 0)),
    m_destVertexRect(AMFConstructRect(0, 0, 0, 0)),
    m_InputFrameSize(AMFConstructSize(0, 0)),
    m_ptsDropThreshold(DROP_THRESHOLD),
    m_bFullScreen(false),
	m_bWaitForVSync(false),
    m_viewScale(1.0f),
    m_viewOffsetX(0),
    m_viewOffsetY(0),
    m_enablePIP(false),
    m_pipZoomFactor(4),
    m_pipFocusPos({ 0.45f, 0.45f }),

    m_srcToClientTransform{},
    m_srcToViewTransform{},
    m_normToViewTransform{},
    m_clientToSrcTransform{},
    m_texNormToViewTransform{},
    m_pipVertexTransform{},
    m_pipTexTransform{},
    m_renderView{}
{
    amf_increase_timer_precision();
}
VideoPresenter::~VideoPresenter()
{
}

AMF_RESULT VideoPresenter::Reset()
{
    m_iFrameCount = 0;
    m_FpsStatStartTime = 0;
    m_iFramesDropped = 0;
    m_startTime = -1LL;
    m_startPts = -1LL;

    return AMF_OK;
}
AMF_RESULT VideoPresenter::Init(amf_int32 width, amf_int32 height, amf::AMFSurface* /*pSurface*/)
{
    m_iFrameCount = 0;
    m_FpsStatStartTime = 0;
    m_iFramesDropped = 0;
    m_InputFrameSize.width = width;
    m_InputFrameSize.height = height;
    return AMF_OK;
}

AMF_RESULT VideoPresenter::Terminate()
{
    SetProcessor(NULL, NULL);
    return AMF_OK;
}

AMF_RESULT VideoPresenter::SubmitInput(amf::AMFData* pData)
{
    if(m_bFrozen)
    {
        return AMF_OK;
    }

    if(pData)
    {
        m_currentTime = pData->GetPts();
        if(m_pAVSync != NULL)
        {
             //AMFTraceWarning(AMF_FACILITY, L"First PTS=%5.2f", (double)pData->GetPts() / 10000);
            if (m_pAVSync->IsVideoStarted() == false)
            {
                m_pAVSync->VideoStarted();
            }
            m_pAVSync->SetVideoPts(pData->GetPts());
        }

        switch(m_state)
        {
        case ModeStep:
            m_state = ModePaused;
            return Present(amf::AMFSurfacePtr(pData));
        case ModePlaying:
            return Present(amf::AMFSurfacePtr(pData));
        case ModePaused:
            return AMF_INPUT_FULL;
        }
        return AMF_WRONG_STATE;
    }
    return AMF_OK;
}

AMF_RESULT VideoPresenter::CalcOutputRect(const AMFRect* pSrcRect, const AMFRect* pScreenRect, AMFRect* pOutputRect) const
{
    amf::AMFLock lock(&m_cs);

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
    AMF_RETURN_IF_FALSE(dstSurfaceRect.Width() > 0 && dstSurfaceRect.Height() > 0, AMF_INVALID_ARG, L"GetRenderViewRects() - Invalid dst size (%dx%d)", dstSurfaceRect.Width(), dstSurfaceRect.Height());

    renderView.srcRect = srcSurfaceRect;
    renderView.srcSize = AMFConstructSize(srcSurfaceRect.Width(), srcSurfaceRect.Height());
    renderView.dstSize = dstSurfaceSize;
    renderView.rotation = GetOrientationRadians();

    AMF_RESULT res = ScaleAndCenterRect(renderView.srcRect, dstSurfaceRect, renderView.rotation, renderView.dstRect);
    AMF_RETURN_IF_FAILED(res, L"GetRenderViewRects() - ScaleAndCenterRect() failed");
    AMF_RETURN_IF_FALSE(renderView.dstRect.Width() > 0 && renderView.dstRect.Height() > 0, AMF_UNEXPECTED,
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

    TransformationToMatrix(m_normToViewTransform, m_fNormalToViewMatrix);
    m_clientToSrcTransform = Inverse(m_srcToClientTransform);

    if (newRenderView.srcRect != m_renderView.srcRect || newRenderView.srcSize != m_renderView.srcSize)
    {
        res = ProjectRectToView(newRenderView.srcSize, newRenderView.srcRect, textureViewRect, m_texNormToViewTransform);
        AMF_RETURN_IF_FAILED(res, L"OnRenderViewResize() - ProjectRectToView() failed to get view texture transform");
        TransformationToMatrix(m_texNormToViewTransform, m_fTextureMatrix);
    }

    // Pip vertex transform
    if (newRenderView.pipDstRect != m_renderView.pipDstRect || newRenderView.dstSize != m_renderView.dstSize)
    {
        res = ProjectRectToView(newRenderView.dstSize, newRenderView.pipDstRect, vertexViewRect, m_pipVertexTransform);
        AMF_RETURN_IF_FAILED(res, L"OnRenderViewResize() - ProjectRectToView() failed to get PIP view vertex transform");
        TransformationToMatrix(m_pipVertexTransform, m_pipNormalToViewMatrix);
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
        TransformationToMatrix(m_pipTexTransform, m_pipTextureMatrix);
    }

    return AMF_OK;
}

AMF_RESULT VideoPresenter::ResizeRenderView(const RenderViewSizeInfo& newRenderView)
{
    if (m_renderView != newRenderView)
    {
        AMF_RESULT res = OnRenderViewResize(newRenderView);
        AMF_RETURN_IF_FAILED(res, L"ResizeRenderView() - OnRenderViewResize() failed");

        m_renderView = newRenderView;
    }

    return AMF_OK;
}

bool VideoPresenter::WaitForPTS(amf_pts pts, bool bRealWait)
{

    bool bRet = true;
    amf_pts currTime = amf_high_precision_clock();

    if(m_startTime != -1LL)
    {
        currTime -= m_startTime;
        pts -= m_startPts;

        amf_pts diff = pts - currTime;
        bool bWaited = false;
        if(diff >  WAIT_THRESHOLD && m_bDoWait && bRealWait) // ignore delays < 5 ms 
        {
            m_waiter.Wait(diff);
            bWaited = true;
        } 
//      AMFTraceWarning(AMF_FACILITY, L"+++ Present Frame #%d pts=%5.2f time=%5.2f diff=%5.2f %s", (int)m_iFrameCount, (float)pts / 10000., (float)currTime / 10000., float(diff) / 10000., bRealWait ? L"R" : L"");
        if(diff < -m_ptsDropThreshold) // ignore lags < 10 ms 
        {
            if(m_iFrameCount == 1)
            {
//                m_startTime += currTime;
            }
            else
            {
                m_iFramesDropped++;

//                AMFTraceWarning(AMF_FACILITY, L"+++ Drop Frame #%d pts=%5.2f time=%5.2f diff=%5.2f %s", (int)m_iFrameCount, (double)pts / 10000., (double)currTime / 10000., (double)diff / 10000., bRealWait ? L"R" : L"");
                bRet = false;
            }
        }

            // update FPS = alpha filter
        if( (m_iFrameCount % 100) == 0)
        {
            amf_pts presentTime = amf_high_precision_clock()- m_startTime;
            double currFPS = double(AMF_SECOND) / ((presentTime - m_FpsStatStartTime) / 100.);
            m_dLastFPS = currFPS;
            m_FpsStatStartTime = presentTime;
        }
    }
    else
    {
        m_startTime = currTime;
        m_startPts = pts;
        m_FpsStatStartTime = 0;
    }
    m_iFrameCount++;

    return bRet;
}

AMF_RESULT VideoPresenter::SetProcessor(amf::AMFComponent *processor, amf::AMFComponent* pHQScaler)
{
    amf::AMFLock lock(&m_cs);
    if(m_pProcessor != NULL)
    {
        m_pProcessor->SetOutputDataAllocatorCB(NULL);
    }

    if (m_pHQScaler != NULL)
    {
        m_pHQScaler->SetOutputDataAllocatorCB(NULL);
    }

    m_pProcessor = processor;
    m_pHQScaler = pHQScaler;

    if (SupportAllocator())
    {
        amf::AMFComponent* pAllocatorUser = (m_pHQScaler != NULL) ? m_pHQScaler : m_pProcessor;
        if (pAllocatorUser != nullptr)
        {
            pAllocatorUser->SetOutputDataAllocatorCB(this);
        }
    }

    UpdateProcessor();
    return AMF_OK;
}

void  VideoPresenter::UpdateProcessor()
{
    amf::AMFLock lock(&m_cs);

    if (m_pProcessor != NULL || m_pHQScaler != NULL)
    {
        AMFRect srcRect = { 0, 0, m_InputFrameSize.width, m_InputFrameSize.height };
        AMFRect outputRect;
        CalcOutputRect(&srcRect, &m_rectClient, &outputRect);

        if (m_pProcessor != NULL && (m_pHQScaler == NULL))
        {
            // what we want to do here is check for the properties if they exist
            // as the HQ scaler has different property names than CSC
            const amf::AMFPropertyInfo* pParamInfo = nullptr;
            if ((m_pProcessor->GetPropertyInfo(AMF_VIDEO_CONVERTER_OUTPUT_SIZE, &pParamInfo) == AMF_OK) && pParamInfo)
            {
                m_pProcessor->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_SIZE, ::AMFConstructSize(m_rectClient.Width(), m_rectClient.Height()));
                m_pProcessor->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_RECT, outputRect);
            }
        }

        // Need to comment this out for now because it sets the HQScaler resolution to the window size instead of using the scaling ratio
        //if (m_pHQScaler != NULL)
        //{
        //    const amf::AMFPropertyInfo* pParamInfo = nullptr;
        //    if ((m_pHQScaler->GetPropertyInfo(AMF_HQ_SCALER_OUTPUT_SIZE, &pParamInfo) == AMF_OK) && pParamInfo)
        //    {
        //        m_pHQScaler->SetProperty(AMF_HQ_SCALER_OUTPUT_SIZE, ::AMFConstructSize(m_rectClient.Width(), m_rectClient.Height()));
        //    }
        //}
    }
}

AMFRect VideoPresenter::GetSourceRect()
{
    AMFRect out = {};
    if (m_pProcessor == nullptr)
    {
        out = m_sourceVertexRect;
    }
    else
    {
        m_pProcessor->GetProperty(AMF_VIDEO_CONVERTER_OUTPUT_SIZE, &out);
    }
    return out;
}

AMF_RESULT VideoPresenter::Freeze()
{
    amf::AMFLock lock(&m_cs);
    return PipelineElement::Freeze();
}

AMF_RESULT VideoPresenter::UnFreeze()
{
    amf::AMFLock lock(&m_cs);
    m_startTime = -1LL;
    m_startPts = -1LL;
    return PipelineElement::UnFreeze();
}

AMF_RESULT VideoPresenter::Resume() 
{ 
    amf::AMFLock lock(&m_cs);
    m_state = ModePlaying; 
    m_startTime = -1LL;
    m_startPts = -1LL;
    return AMF_OK;
}

AMF_RESULT              VideoPresenter::SetSubresourceIndex(amf_int32 index)
{
    m_iSubresourceIndex = index;
    return AMF_OK;
}

amf_int32               VideoPresenter::GetSubresourceIndex()
{
    return m_iSubresourceIndex;
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

AMFRect GetPlaneRect(amf::AMFPlane* pPlane)
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
    AMF_RETURN_IF_FALSE(dstWidth > 0 && dstHeight > 0, AMF_INVALID_ARG, L"ScaleAndCenterRect() - dst size is invalid: %dx%d", dstWidth, dstHeight);

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
    result.scaleLinear.x = left.scaleLinear.x * right.scaleLinear.x + left.scaleOrtho.y * right.scaleOrtho.x;
    result.scaleOrtho.x = left.scaleOrtho.x * right.scaleLinear.x + left.scaleLinear.y * right.scaleOrtho.x;

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
    inverse.scaleOrtho.x = -transformation.scaleOrtho.x / determinant;
    inverse.scaleOrtho.y = -transformation.scaleOrtho.y / determinant;

    // Now we have to add the translations to the matrix
    // To add this, we can just transform the translation with the inverse matrix so far
    // and add it as a translation to the 4th column
    inverse.translation.x = -transformation.translation.x * inverse.scaleLinear.x - transformation.translation.y * inverse.scaleOrtho.y;
    inverse.translation.y = -transformation.translation.x * inverse.scaleOrtho.x - transformation.translation.y * inverse.scaleLinear.y;

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

    transformation.scaleOrtho.x = matrix[1][0];
    transformation.scaleOrtho.y = matrix[0][1];

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
        transformation.scaleOrtho.x *= srcWidth;
        transformation.scaleOrtho.y *= srcHeight;

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
    AMF_RETURN_IF_FALSE(srcRectWidth > 0 && srcRectHeight > 0, AMF_INVALID_ARG, L"ProjectRectToView() - srcRect is invalid: %dx%d", srcRectWidth, srcRectHeight);
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