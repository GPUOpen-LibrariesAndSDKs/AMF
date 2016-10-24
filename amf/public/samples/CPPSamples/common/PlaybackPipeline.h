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
#include "public/include/components/Component.h"
#include "public/include/components/VideoDecoderUVD.h"
#include "public/include/components/VideoConverter.h"
#include "public/include/components/FFMPEGComponents.h"
#include "public/include/components/FFMPEGAudioDecoder.h"
#include "public/include/components/FFMPEGAudioConverter.h"
#include "public/include/components/FFMPEGFileDemuxer.h"
#include "BitStreamParser.h"
#include "VideoPresenter.h"
#include "AudioPresenter.h"
#include "ParametersStorage.h"
#include "Pipeline.h"

class PlaybackPipeline : public Pipeline, public ParametersStorage
{
    class PipelineElementDemuxer;
public:
    PlaybackPipeline();
    virtual ~PlaybackPipeline();
public:
    static const wchar_t* PARAM_NAME_INPUT;
    static const wchar_t* PARAM_NAME_PRESENTER;
    static const wchar_t* PARAM_NAME_FRAMERATE;
#if !defined(METRO_APP)
    AMF_RESULT Init(HWND hwnd);
#else
    AMF_RESULT Init(const wchar_t* path, IRandomAccessStream^ inputStream, ISwapChainBackgroundPanelNative* pSwapChainPanel, AMFSize swapChainPanelSize);
#endif

    virtual AMF_RESULT Play();
    virtual AMF_RESULT Pause();
    virtual AMF_RESULT Step();
    virtual AMF_RESULT Stop();

    void Terminate();

    double     GetProgressSize();
    double     GetProgressPosition();
    double     GetFPS();
    amf_int64  GetFramesDropped();

protected:

    virtual void  OnParamChanged(const wchar_t* name);
    virtual void  UpdateVideoProcessorProperties(const wchar_t* name);
    virtual AMF_RESULT  InitVideoProcessor(bool bUseDirectOutput, amf_int32 videoWidth, amf_int32 videoHeight);
    virtual AMF_RESULT  InitVideoDecoder(const wchar_t *pDecoderID, amf_int32 videoWidth, amf_int32 videoHeight, amf::AMFBuffer* pExtraData);
    virtual AMF_RESULT  InitAudio(amf::AMFOutput* pOutput);
    virtual AMF_RESULT  InitVideo(BitStreamParserPtr pParser, amf::AMFOutput* pOutput, amf::AMF_MEMORY_TYPE presenterEngine, HWND hwnd, bool bUseDirectOutput);


    amf::AMFDataStreamPtr   m_pStream;
    amf::AMFContextPtr      m_pContext;
    
    amf::AMFComponentPtr    m_pDecoder;
    amf::AMFComponentPtr    m_pConverter;
    VideoPresenterPtr       m_pPresenter;
    amf::AMFComponentExPtr  m_pDemuxer;

    amf::AMFComponentPtr    m_pAudioDecoder;
    amf::AMFComponentPtr    m_pAudioConverter;
    AudioPresenterPtr       m_pAudioPresenter;
};

typedef std::shared_ptr<PlaybackPipeline> PlaybackPipelinePtr;