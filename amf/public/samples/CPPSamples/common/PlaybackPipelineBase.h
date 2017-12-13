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

class PlaybackPipelineBase : public Pipeline, public ParametersStorage
{
    class PipelineElementDemuxer;

protected:
    PlaybackPipelineBase();
    AMF_RESULT Init();

public:
    virtual ~PlaybackPipelineBase();

    static const wchar_t* PARAM_NAME_INPUT;
    static const wchar_t* PARAM_NAME_URL_VIDEO;
    static const wchar_t* PARAM_NAME_URL_AUDIO;
    static const wchar_t* PARAM_NAME_LISTEN_FOR_CONNECTION;

    static const wchar_t* PARAM_NAME_PRESENTER;
    static const wchar_t* PARAM_NAME_FRAMERATE;
    static const wchar_t* PARAM_NAME_LOOP;

    virtual AMF_RESULT Play();
    virtual AMF_RESULT Pause();
    virtual AMF_RESULT Step();
    virtual AMF_RESULT Stop();
    virtual bool       IsPaused() const;


    virtual void Terminate();

    virtual AMF_RESULT GetDuration(amf_pts& pts) const;
	virtual AMF_RESULT GetCurrentPts(amf_pts& pts) const;
    
    virtual double     GetProgressSize() const;
    virtual double     GetProgressPosition() const;

	virtual AMF_RESULT Seek(amf_pts pts);

    virtual double     GetFPS();
    amf_int64  GetFramesDropped() const;

    virtual void        UpdateVideoProcessorProperties(const wchar_t* name);

protected:
    virtual void OnEof();

    virtual AMF_RESULT InitContext(amf::AMF_MEMORY_TYPE type) = 0;
	virtual AMF_RESULT CreateVideoPresenter(amf::AMF_MEMORY_TYPE type, amf_int64 bitRate, double fps) = 0;
    virtual AMF_RESULT CreateAudioPresenter() = 0;

    virtual void        OnParamChanged(const wchar_t* name);
    virtual AMF_RESULT  InitVideoProcessor();
    virtual AMF_RESULT  InitVideoDecoder(const wchar_t *pDecoderID, amf::AMFBuffer* pExtraData);
    virtual AMF_RESULT  InitAudio(amf::AMFOutput* pOutput);
    virtual AMF_RESULT  InitVideo(amf::AMFOutput* pOutput, amf::AMF_MEMORY_TYPE presenterEngine);
	virtual AMF_RESULT  InitVideoPipeline(amf_uint32 iVideoStreamIndex, PipelineElementPtr pVideoSourceStream);
	virtual AMF_RESULT  InitAudioPipeline(amf_uint32 iAudioStreamIndex, PipelineElementPtr pAudioSourceStream);
    
    amf::AMFContextPtr      m_pContext;

    amf::AMFComponentExPtr  m_pDemuxerVideo;
    amf::AMFComponentExPtr  m_pDemuxerAudio; //optional demuxer if audio is a diffrent URL (ie YouTube)

    amf::AMFDataStreamPtr   m_pVideoStream;
    BitStreamParserPtr      m_pVideoStreamParser;

    amf::AMFComponentPtr    m_pVideoDecoder;
    amf::AMFComponentPtr    m_pVideoProcessor;
    VideoPresenterPtr       m_pVideoPresenter;

    amf::AMFComponentPtr    m_pAudioDecoder;
    amf::AMFComponentPtr    m_pAudioConverter;
    AudioPresenterPtr       m_pAudioPresenter;

    amf_int32               m_iVideoWidth;
    amf_int32               m_iVideoHeight;

    bool                    m_bVideoPresenterDirectConnect;
    amf_uint32              m_nFfmpegRefCount;

    AVSyncObject            m_AVSync;
    bool                    m_bURL;
    amf::AMF_SURFACE_FORMAT m_eDecoderFormat;

};
