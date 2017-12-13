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
#include "public/include/components/VideoConverter.h"

VideoPresenter::VideoPresenter() :
    m_startTime(-1LL),
    m_startPts(-1LL),
    m_state(ModePlaying),
    m_iFrameCount(0),
    m_FpsStatStartTime(0),
    m_iFramesDropped(0),
    m_dLastFPS(0),
    m_currentTime(0),
    m_bDoWait(true),
    m_pAVSync(NULL),
    m_iSubresourceIndex(0)
{
    amf_increase_timer_precision();
    memset(&m_InputFrameSize, 0, sizeof(m_InputFrameSize));
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
    SetProcessor(NULL);
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
        if(m_pAVSync != NULL && !m_pAVSync->IsVideoStarted())
        {
             //AMFTraceWarning(AMF_FACILITY, L"First PTS=%5.2f", (double)pData->GetPts() / 10000);
            m_pAVSync->VideoStarted();
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

AMF_RESULT VideoPresenter::CalcOutputRect(const AMFRect* pSrcRect, const AMFRect* pDstRect, AMFRect* pTargetRect)
{
    amf::AMFLock lock(&m_cs);

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

bool VideoPresenter::WaitForPTS(amf_pts pts, bool bRealWait)
{
#define WAIT_THRESHOLD 5 * AMF_SECOND / 1000LL // 5 ms

    bool bRet = true;
    amf_pts currTime = amf_high_precision_clock();

    if(m_startTime != -1LL)
    {
        currTime -= m_startTime;
        pts -= m_startPts;

        amf_pts diff = pts - currTime;
        bool bWaited = false;
        if(diff >  WAIT_THRESHOLD && m_bDoWait && bRealWait) // ignore delays < 1 ms 
        {
            m_waiter.Wait(diff);
            bWaited = true;
        } 
        if(diff < - 10 * AMF_SECOND / 1000LL) // ignore lags < 10 ms 
        {
            if(m_iFrameCount == 1)
            {
//                m_startTime += currTime;
            }
            else
            {
                m_iFramesDropped++;
                wchar_t buf[1000];
                swprintf(buf,L"+++ Drop Frame #%d pts=%5.2f time=%5.2f diff=%5.2f\n",(int)m_iFramesDropped, (float)pts / 10000., (float)currTime / 10000., float(diff) / 10000.);
                ::OutputDebugStringW(buf);

                bRet = false;
            }
        }
//        wchar_t buf[1000];
//        swprintf(buf,L"+++ Present Frame #%d pts=%5.2f time=%5.2f diff=%5.2f\n",(int)m_iFrameCount, (float)pts / 10000., (float)currTime / 10000., float(diff) / 10000.);
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

AMF_RESULT VideoPresenter::SetProcessor(amf::AMFComponent *processor)
{
    amf::AMFLock lock(&m_cs);
    if(m_pProcessor != NULL)
    {
        m_pProcessor->SetOutputDataAllocatorCB(NULL);
    }

    m_pProcessor = processor;
    if(m_pProcessor != NULL)
    {
        m_pProcessor->SetOutputDataAllocatorCB(this);
    }
    UpdateProcessor();
    return AMF_OK;

}

void        VideoPresenter::UpdateProcessor()
{
    amf::AMFLock lock(&m_cs);
    if(m_pProcessor != NULL)
    {
        AMFRect srcRect = {0, 0, m_InputFrameSize.width, m_InputFrameSize.height};
        AMFRect outputRect;
        CalcOutputRect(&srcRect, &m_rectClient, &outputRect);
        m_pProcessor->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_SIZE, ::AMFConstructSize(m_rectClient.Width(),m_rectClient.Height()));
        m_pProcessor->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_RECT, outputRect);
    }
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
