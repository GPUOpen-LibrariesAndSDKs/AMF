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
#pragma once

#include "public/include/core/Context.h"
#include "PipelineElement.h"

#if defined(METRO_APP)
#include <collection.h>
#include <windows.ui.xaml.media.dxinterop.h>
using namespace Platform;
using namespace Windows::UI;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
#endif

class VideoPresenter;
typedef std::shared_ptr<VideoPresenter> VideoPresenterPtr;

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

    virtual amf_int32 GetInputSlotCount() {return 1;}
    virtual amf_int32 GetOutputSlotCount() {return 0;}

    virtual AMF_RESULT SubmitInput(amf::AMFData* pData);

    virtual AMF_RESULT              Present(amf::AMFSurface* pSurface) = 0;
    virtual amf::AMF_MEMORY_TYPE    GetMemoryType() = 0;
    virtual amf::AMF_SURFACE_FORMAT GetInputFormat() = 0;
    virtual AMF_RESULT              SetInputFormat(amf::AMF_SURFACE_FORMAT format) = 0;

    virtual AMF_RESULT              Init(amf_int32 width, amf_int32 height);
    virtual AMF_RESULT              Terminate();

    virtual double              GetFPS(){return m_dLastFPS;}
    virtual amf_int64           GetFramesDropped(){return m_iFramesDropped;}

    AMF_RESULT Resume() { m_state = ModePlaying; return AMF_OK;}
    AMF_RESULT Pause() { m_state = ModePaused; return AMF_OK; }
    AMF_RESULT Step() { m_state = ModeStep; return AMF_OK;}
    Mode       GetMode() { return m_state;}


    virtual AMF_RESULT SetConverter(amf::AMFComponent *converter);
    // amf::AMFInterface interface
    virtual amf_long AMF_STD_CALL Acquire(){return 1;}
    virtual amf_long AMF_STD_CALL Release(){return 1;}
    virtual AMF_RESULT AMF_STD_CALL QueryInterface(const amf::AMFGuid& interfaceID,void** ppInterface){return AMF_NOT_IMPLEMENTED;}

    // amf::AMFDataAllocatorCB interface
    virtual AMF_RESULT AMF_STD_CALL AllocBuffer(amf::AMF_MEMORY_TYPE type, amf_size size, amf::AMFBuffer** ppBuffer){return AMF_NOT_IMPLEMENTED;}
    virtual AMF_RESULT AMF_STD_CALL AllocSurface(amf::AMF_MEMORY_TYPE type, amf::AMF_SURFACE_FORMAT format,
            amf_int32 width, amf_int32 height, amf_int32 hPitch, amf_int32 vPitch, amf::AMFSurface** ppSurface){return AMF_NOT_IMPLEMENTED;}

    // amf::AMFSurfaceObserver interface
    virtual void AMF_STD_CALL OnSurfaceDataRelease(amf::AMFSurface* pSurface){}

public:
#if defined(METRO_APP)
    static VideoPresenterPtr Create(ISwapChainBackgroundPanelNative* pSwapChainPanel, AMFSize swapChainPanelSize, amf::AMFContext* pContext);
#else
    static VideoPresenterPtr Create(amf::AMF_MEMORY_TYPE type, HWND hwnd, amf::AMFContext* pContext);
#endif

protected:
    VideoPresenter(HWND hwnd, amf::AMFContext* pContext);

    AMF_RESULT CalcOutputRect(const AMFRect* pSrcRect, const AMFRect* pDstRect, AMFRect* pTargetRect);
    bool WaitForPTS(amf_pts pts); // returns false if frame is too late and should be dropped

    void        UpdateConverter();

    HWND                                m_hwnd;
    amf::AMFContext*                    m_pContext;
    amf_pts                             m_startTime;
    amf_pts                             m_startPts;
    amf::AMFPreciseWaiter               m_waiter;
    AMFSize                             m_InputFrameSize;

    // Stats
    amf_int64                           m_iFrameCount;
    amf_int64                           m_iFramesDropped;
    amf_pts                             m_FpsStatStartTime;

    double                              m_dLastFPS;
    int                                 m_instance;
    amf::AMFComponentPtr                m_pConverter;
    Mode                                m_state;
    AMFRect                             m_rectClient;
};