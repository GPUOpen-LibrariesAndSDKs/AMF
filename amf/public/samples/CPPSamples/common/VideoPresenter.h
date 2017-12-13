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

    virtual amf_int32 GetInputSlotCount() const { return 1; }
    virtual amf_int32 GetOutputSlotCount() const { return 0; }

    virtual AMF_RESULT SubmitInput(amf::AMFData* pData);

    virtual AMF_RESULT              Present(amf::AMFSurface* pSurface) = 0;
    virtual amf::AMF_MEMORY_TYPE    GetMemoryType() const = 0;
    virtual amf::AMF_SURFACE_FORMAT GetInputFormat() const = 0;
    virtual AMF_RESULT              SetInputFormat(amf::AMF_SURFACE_FORMAT format) = 0;
    virtual AMF_RESULT              SetSubresourceIndex(amf_int32 index);
    virtual amf_int32               GetSubresourceIndex();

    virtual AMF_RESULT              Init(amf_int32 width, amf_int32 height);
    virtual AMF_RESULT              Terminate();
    virtual AMF_RESULT              Reset();
    virtual amf_pts                 GetCurrentTime() { return m_currentTime; }

    virtual double              GetFPS() const { return m_dLastFPS; }
    virtual amf_int64           GetFramesDropped() const {return m_iFramesDropped; }
    virtual bool                SupportAllocator() const { return true; }
    virtual void                DoActualWait(bool bDoWait) {m_bDoWait = bDoWait;}

    virtual AMF_RESULT          Resume();
    AMF_RESULT Pause() { m_state = ModePaused; return AMF_OK; }
    AMF_RESULT Step() { m_state = ModeStep; return AMF_OK;}
    Mode       GetMode() const { return m_state;}

    virtual AMF_RESULT SetProcessor(amf::AMFComponent* pProcessor);

    // amf::AMFInterface interface
    virtual amf_long AMF_STD_CALL Acquire() { return 1; }
    virtual amf_long AMF_STD_CALL Release() { return 1; }
    virtual AMF_RESULT AMF_STD_CALL QueryInterface(const amf::AMFGuid& interfaceID, void** ppInterface) { return AMF_NOT_IMPLEMENTED; }

    // amf::AMFDataAllocatorCB interface
    virtual AMF_RESULT AMF_STD_CALL AllocBuffer(amf::AMF_MEMORY_TYPE type, amf_size size, amf::AMFBuffer** ppBuffer) { return AMF_NOT_IMPLEMENTED; }
    virtual AMF_RESULT AMF_STD_CALL AllocSurface(amf::AMF_MEMORY_TYPE type, amf::AMF_SURFACE_FORMAT format,
            amf_int32 width, amf_int32 height, amf_int32 hPitch, amf_int32 vPitch, amf::AMFSurface** ppSurface) {return AMF_NOT_IMPLEMENTED; }

    // amf::AMFSurfaceObserver interface
    virtual void AMF_STD_CALL OnSurfaceDataRelease(amf::AMFSurface* pSurface) {}

	// Get frame width and height
    amf_int32 GetFrameWidth() const     { return m_InputFrameSize.width; }
    amf_int32 GetFrameHeight() const    { return m_InputFrameSize.height; }


    virtual void SetAVSyncObject(AVSyncObject *pAVSync) {m_pAVSync = pAVSync;}

protected:
    VideoPresenter();
    virtual AMF_RESULT Freeze();
    virtual AMF_RESULT UnFreeze();

    virtual AMF_RESULT CalcOutputRect(const AMFRect* pSrcRect, const AMFRect* pDstRect, AMFRect* pTargetRect);
    bool WaitForPTS(amf_pts pts, bool bRealWait = true); // returns false if frame is too late and should be dropped

    void        UpdateProcessor();

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
    amf::AMFComponentPtr                m_pProcessor;
    Mode                                m_state;
    AMFRect                             m_rectClient;
    
    amf_pts                             m_currentTime;
    bool                                m_bDoWait;
    AVSyncObject*                       m_pAVSync;
    amf_int32                           m_iSubresourceIndex;
};