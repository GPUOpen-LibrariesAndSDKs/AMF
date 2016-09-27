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
#include "VideoPresenter.h"
#include "VideoPresenterDX11.h"
#if !defined(METRO_APP)
#include "VideoPresenterDX9.h"
#include "VideoPresenterOpenGL.h"
#endif
#include "public/include/components/VideoConverter.h"

VideoPresenter::VideoPresenter(HWND hwnd, amf::AMFContext* pContext) : 
    m_hwnd(hwnd),
    m_pContext(pContext),
    m_startTime(-1LL),
    m_startPts(-1LL),
    m_state(ModePlaying),
    m_iFrameCount(0),
    m_FpsStatStartTime(0),
    m_iFramesDropped(0),
    m_dLastFPS(0)
{
    amf_increase_timer_precision();
    memset(&m_InputFrameSize, 0, sizeof(m_InputFrameSize));
}
VideoPresenter::~VideoPresenter()
{
}

#if defined(METRO_APP)
VideoPresenterPtr VideoPresenter::Create(ISwapChainBackgroundPanelNative* pSwapChainPanel, AMFSize swapChainPanelSize, amf::AMFContext* pContext)
{
    VideoPresenterPtr pPresenter;
    pPresenter = VideoPresenterPtr(new VideoPresenterDX11(pSwapChainPanel, swapChainPanelSize, pContext));
    return pPresenter;
}
#else
VideoPresenterPtr VideoPresenter::Create(amf::AMF_MEMORY_TYPE type, HWND hwnd, amf::AMFContext* pContext)
{
    AMF_RESULT res = AMF_OK;
    VideoPresenterPtr pPresenter;
    switch(type)
    {
    case amf::AMF_MEMORY_DX9:
        pPresenter = VideoPresenterPtr(new VideoPresenterDX9(hwnd, pContext));
        break;
    case amf::AMF_MEMORY_OPENGL:
        pPresenter = VideoPresenterPtr(new VideoPresenterOpenGL(hwnd, pContext));
        break;
    case amf::AMF_MEMORY_DX11:
        pPresenter = VideoPresenterPtr(new VideoPresenterDX11(hwnd, pContext));
        break;    
    default:
        break;
    }
    return pPresenter;
}
#endif

AMF_RESULT VideoPresenter::Init(amf_int32 width, amf_int32 height)
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
    SetConverter(NULL);
    return AMF_OK;
}

AMF_RESULT VideoPresenter::SubmitInput(amf::AMFData* pData)
{
    if(pData)
    {
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

AMF_RESULT VideoPresenter::CalcOutputRect(const AMFRect* pSrcRect, const AMFRect* pDstRect, AMFRect* pTargetRect)
{
    amf_double dDstRatio = pDstRect->Height() / (amf_double)pDstRect->Width();
    amf_double dSrcRatio = pSrcRect->Height() / (amf_double)pSrcRect->Width();

    // TODO : add aspect ratio from frame
    //dSrcRatio /= frameApectRatioFromDecoder;

    if(dDstRatio > dSrcRatio)
    {   // empty areas on top and bottom
        pTargetRect->left = 0;
        pTargetRect->right = pDstRect->Width();
        LONG lViewHeight = amf_int(pDstRect->Width() * dSrcRatio);
        pTargetRect->top = (pDstRect->Height() - lViewHeight) / 2;
        pTargetRect->bottom = pTargetRect->top + lViewHeight;
    }
    else
    {   // empty areas on left and right
        pTargetRect->bottom = pDstRect->Height();
        pTargetRect->top = 0;
        LONG lViewWidth = amf_int(pDstRect->Height() / dSrcRatio);
        pTargetRect->left = (pDstRect->Width() - lViewWidth) / 2;
        pTargetRect->right = pTargetRect->left + lViewWidth;
    }
    return AMF_OK;
}

bool VideoPresenter::WaitForPTS(amf_pts pts)
{
    bool bRet = true;
    amf_pts currTime = amf_high_precision_clock();

    if(m_startTime != -1LL)
    {
        currTime -= m_startTime;
        pts -= m_startPts;

        amf_pts diff = pts - currTime;

        if(diff > AMF_SECOND / 1000LL) // ignore delays < 1 ms 
        {
            m_waiter.Wait(diff);
        } 
        if(diff < - 10 * AMF_SECOND / 1000LL) // ignore lags < 2 ms 
        {
            if(m_iFrameCount == 1)
            {
//                m_startTime += currTime;
            }
            else
            {
                m_iFramesDropped++;
                bRet = false;
            }
        }
//        wchar_t buf[1000];
//        swprintf(buf,L"+++ Present Frame #%d pts=%d time=%d diff=%d\n",(int)m_iFrameCount, (int)pts, (int)currTime, int(diff));
//        ::OutputDebugStringW(buf);

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
AMF_RESULT VideoPresenter::SetConverter(amf::AMFComponent *converter)
{
    if(m_pConverter != NULL)
    {
        m_pConverter->SetOutputDataAllocatorCB(NULL);
    }
    m_pConverter = converter;
    if(m_pConverter != NULL)
    {
        m_pConverter->SetOutputDataAllocatorCB(this);
    }
    UpdateConverter();
    return AMF_OK;
}
void        VideoPresenter::UpdateConverter()
{
    if(m_pConverter != NULL)
    {
        AMFRect srcRect = {0, 0, m_InputFrameSize.width, m_InputFrameSize.height};
        AMFRect outputRect;
        CalcOutputRect(&srcRect, &m_rectClient, &outputRect);
        m_pConverter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_SIZE, ::AMFConstructSize(m_rectClient.Width(),m_rectClient.Height()));
        m_pConverter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_RECT, outputRect);
    }
}

