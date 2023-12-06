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
#include "VideoPresenter.h"

class BackBufferPresenter;
typedef std::shared_ptr<BackBufferPresenter> BackBufferPresenterPtr;

class BackBufferPresenter : public VideoPresenter
{
public:    
    ~BackBufferPresenter() override;

    static AMF_RESULT Create(BackBufferPresenterPtr& pResult, amf::AMF_MEMORY_TYPE type, amf_handle hwnd, amf::AMFContext* pContext, amf_handle display = 0);
    virtual void SetRenderToBackBuffer(bool bRenderToBackBuffer) { m_bRenderToBackBuffer = bRenderToBackBuffer; }
    
    // VideoPresenter interface
    AMF_RESULT SetProcessor(amf::AMFComponent* pProcessor, amf::AMFComponent* pHQScaler = NULL) override;

protected:
    BackBufferPresenter(amf_handle hwnd, amf::AMFContext* pContext, amf_handle hDisplay = 0);
    virtual AMFRect GetClientRect();

    virtual amf_bool GetFullScreenState() const { return m_currentFullScreenState; }
    virtual AMF_RESULT SetFullScreenState(amf_bool bFullScreen);

    amf_handle              m_hwnd;
    amf_handle              m_hDisplay;
    
    amf::AMFContext*        m_pContext;
    bool                    m_bRenderToBackBuffer;

    amf_bool                m_currentFullScreenState;
    AMFRect                 m_windowModeRect;
#if defined(_WIN32)
    LONG_PTR                m_windowModeStyle;
#endif
};