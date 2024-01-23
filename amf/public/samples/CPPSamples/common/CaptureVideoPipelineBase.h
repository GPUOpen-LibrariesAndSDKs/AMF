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
#include "public/include/components/Component.h"
#include "public/include/components/VideoDecoderUVD.h"
#include "public/include/components/VideoConverter.h"
#include "public/include/components/FFMPEGVideoDecoder.h"
#include "public/samples/CPPSamples/common/BitStreamParser.h"
#include "public/samples/CPPSamples/common/VideoPresenter.h"
#include "public/samples/CPPSamples/common/ParametersStorage.h"
#include "public/samples/CPPSamples/common/Pipeline.h"
#include "public/common/Thread.h"
#include "public/include/components/ChromaKey.h"
#include "public/include/components/Capture.h"

class Options;
//Video source mode
enum VIDEO_SOURCE_MODE_ENUM
{
    VIDEO_SOURCE_MODE_FILE,      //media file
    VIDEO_SOURCE_MODE_CAPTURE,   //capture from camera
    VIDEO_SOURCE_MODE_INVALID = -1,
};

class ParamAudioCodec
{
public:
    amf_int64 codecID = 0;
    amf_int64 streamBitRate = 0;
    amf_int64 streamSampleRate = 0;
    amf_int64 streamChannels = 0;
    amf_int64 streamFormat = 0;
    amf_int64 streamLayout = 0;
    amf_int64 streamBlockAlign = 0;
    amf_int64 streamFrameSize = 0;
    amf::AMFInterfacePtr pExtradata;
};

class PlaybackStream
{
public:
    AMF_RESULT InitStream(amf::AMFContext *pContext, amf::AMFComponentEx *pDemuxer,
                    amf_int32& iVideoIndex, amf_int32& iAudioIndex, ParamAudioCodec* pParamsAudio,
                    bool bLowLatency);
    AMF_RESULT InitVideoDecoder(amf::AMFContext *pContext, amf::AMFOutput* pOutput, bool bLowLatency);
    AMF_RESULT Terminate();
    AMF_RESULT Update();

    amf::AMFComponentExPtr  m_pDemuxer;
    amf::AMFComponentPtr    m_pDecoder;
    PipelineElementPtr      m_pDecoderElement;
    amf_uint32              m_nFfmpegRefCount = 0;
    amf::AMF_SURFACE_FORMAT m_eVideoDecoderFormat;
    AMFRate                 m_frameRate;
    amf_int64               m_frameCount;
    AMFSize                 m_frameSize;
    std::wstring            m_codecID;
    std::wstring            m_fileName;
    bool                    m_bSWDecoder;
};

class CaptureVideoPipelineBase : public Pipeline, public ParametersStorage
{
    class  DecoderSubmissionSync;
    class PipelineElementAMFDecoder;
public:
    static const wchar_t* PARAM_NAME_INPUT;
    static const wchar_t* PARAM_NAME_INPUT_BK;
    static const wchar_t* PARAM_NAME_VIDEO_SOURCE_MODE;
    static const wchar_t* PARAM_NAME_VIDEO_SOURCE_DEVICE_INDEX;
    static const wchar_t* PARAM_NAME_VIDEO_SOURCE_SCALE;
    static const wchar_t* PARAM_NAME_LOOP;
    static const wchar_t* PARAM_NAME_CHROMAKEY;
    static const wchar_t* PARAM_NAME_CHROMAKEY_BK;
    static const wchar_t* PARAM_NAME_CHROMAKEY_DEBUG;
    static const wchar_t* PARAM_NAME_CHROMAKEY_RGB;
    static const wchar_t* PARAM_NAME_CHROMAKEY_SPILL;
    static const wchar_t* PARAM_NAME_CHROMAKEY_COLOR_ADJ;
    static const wchar_t* PARAM_NAME_CHROMAKEY_SCALING;
    static const wchar_t* PARAM_NAME_CHROMAKEY_COLOR;
    static const wchar_t* PARAM_NAME_CHROMAKEY_COLOR_EX;
    static const wchar_t* PARAM_NAME_CHROMAKEY_RGBAFP16;
    static const wchar_t* PARAM_NAME_CHROMAKEY_10BITLIVE;
    static const wchar_t* PARAM_NAME_CHROMAKEY_ALPHA_SRC;

    CaptureVideoPipelineBase();
    virtual ~CaptureVideoPipelineBase();

    // from Pipeline
    virtual AMF_RESULT  Stop();
    virtual AMF_RESULT  Restart();
    virtual double      GetFPS();

    // CaptureVideoPipelineBase
    virtual AMF_RESULT  Init();

    virtual AMF_RESULT  Play();
    virtual AMF_RESULT  Pause();
    virtual AMF_RESULT  Step();

    virtual AMF_RESULT  Terminate();

    double              GetProgressSize();
    double              GetProgressPosition();
    amf_int64           GetFramesDropped();

    amf::AMFContext*        GetContext() {return m_pContext;}
    amf::AMFCaptureManager* GetCaptureManager(){ return  m_pCaptureManager;}

    AMF_RESULT          Dump();

    virtual AMF_RESULT  InitContext(amf::AMF_MEMORY_TYPE type) = 0;

    void SelectColorFromPosition(AMFPoint pos, AMFRect clientRect);
    void ToggleChromakeyProperty(const wchar_t* name);
    void LoopChromakeyProperty(const wchar_t* name, amf_int32 rangeMin, amf_int32 rangeMax);
    void UpdateChromakeyProperty(const wchar_t* name, amf_int32 value, bool delta = false);
    void UpdateScalingProperty(const wchar_t* name, amf_int32 value, bool delta = false);
    void UpdateChromakeyParams(); //save the parameters
    amf_int64  GetFrameCount();
    void ResetOptions();

protected:
    static amf::AMF_SURFACE_FORMAT  m_eComposerOutputDefault;
    virtual AMF_RESULT CreateVideoPresenter(amf::AMF_MEMORY_TYPE type, amf_int32 compositedWidth, amf_int32 compositedHeight) = 0;

    AMF_RESULT InitInternal();
    AMF_RESULT InitDemuxer(std::wstring& sFilename, amf::AMFComponent** ppDemuxer);
    AMF_RESULT InitCapture(amf::AMF_SURFACE_FORMAT eFormat);
    AMF_RESULT InitVideoConverter(amf::AMF_SURFACE_FORMAT eFormatIn, amf::AMF_SURFACE_FORMAT eFormatOut,
                AMFSize sizeIn, AMFSize sizeOut, amf::AMFComponent**  ppConverter);
    AMF_RESULT InitChromakeyer(bool enableBK);
    void       EofDecoders();

    amf::AMFContextPtr                          m_pContext;
    std::shared_ptr<DecoderSubmissionSync>      m_pDecoderSubmissionSync;
    amf::AMFCaptureManagerPtr                   m_pCaptureManager;
    VideoPresenterPtr                           m_pVideoPresenter;
    amf::AMFCaptureDevicePtr                    m_pCaptureDevice;
    amf::AMFComponentPtr                        m_pConverter;
    amf::AMFComponentExPtr                      m_pChromaKeyer;

    std::wstring                                m_filename;
    std::wstring                                m_filenameBK;
    PlaybackStream                              m_stream;
    PlaybackStream                              m_streamBK;

    bool                                        m_bIsConnected;
    bool                                        m_bUseBackBufferPresenter;
    bool                                        m_bEnableScaling;
};