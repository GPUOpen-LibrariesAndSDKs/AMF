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
// Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.
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
#include "public/include/components/VideoEncoderVCE.h"
#include "public/include/components/VideoEncoderHEVC.h"
#include "public/samples/CPPSamples/common/BitStreamParser.h"
#include "public/samples/CPPSamples/common/VideoPresenter.h"
#include "public/samples/CPPSamples/common/ParametersStorage.h"
#include "public/samples/CPPSamples/common/Pipeline.h"
#include "public/common/Thread.h"
#include "public/include/components/VideoStitch.h"
#include "public/samples/CPPSamples/common/AudioPresenter.h"
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

class MediaInfo
{
public:
    amf_int64 duration;
    AMFRate framerate;
    AMFSize   sizeInput;
    std::wstring codecID;
    std::wstring fileName;
};

class StitchStream
{
public:
    amf::AMFDataStreamPtr                       m_pStream;
    BitStreamParserPtr                          m_pParser;
    amf::AMFComponentExPtr                      m_pSource;
    amf::AMFComponentPtr                        m_pDecoder;
    PipelineElementPtr                          m_pDecoderElement;
    amf_uint32                                  m_nFfmpegRefCount = 0;

    AMF_RESULT InitDemuxer(amf::AMFContext *pContext, const wchar_t *filename,
        std::wstring& pVideoDecoderID, AMFSize& frameSize, 
        amf_int64  startTime,          // Starting encoder at timestamp startTime (secs)
        amf_int64  endTime,            // Stopping encoder at timestamp endTime (secs)
        amf_int32& iVideoStreamIndex, amf_int32& iAudioStreamIndex,
        ParamAudioCodec* pParamsAudio, bool infoOnly, amf::AMFBufferPtr& pExtraData);
    AMF_RESULT InitDecoder(amf::AMFContext *pContext, std::wstring pVideoDecoderID,
        AMFSize frameSize, amf::AMF_SURFACE_FORMAT eComposerInput, bool bLowLatency, amf::AMFBufferPtr pExtraData);
    AMF_RESULT GetMediaInfo(amf::AMFContext *pContext, const wchar_t *filename, MediaInfo& mediaInfo, ParamAudioCodec &paramsAudio);
    AMF_RESULT Terminate();
    amf_int64 m_duration;
    AMFRate m_framerate;
    AMFSize   m_sizeInput;
    std::wstring m_codecID;
    std::wstring m_fileName;
};

class StitchTemplate
{
public:
    double pitch;   // in radians
    double yaw;     // in radians
    double roll;    // in radians

    double offsetX; // in pixels
    double offsetY; // in pixels
    double offsetZ; // in pixels
    double hfov;    // M_PI / 2.0
    double scale;   // 1.0

    double lensK1;
    double lensK2;
    double lensK3;
    double lensOffX;
    double lensOffY;

    wchar_t overlayFile[_MAX_PATH];
    AMFSize overlaySize;
    AMFRect crop;
    AMF_VIDEO_STITCH_LENS_ENUM lensMode;

public:
    AMF_RESULT ApplyToCamera(amf::AMFPropertyStorage *camera);
    void Clear();
};

class StitchElement : public PipelineElement
{
protected:
    struct Data
    {
        amf::AMFDataPtr     data;
    };
public:
    StitchElement(amf::AMFComponent *pComponent, amf_int32 inputCount, AMFSize outputSize);
    virtual ~StitchElement(){};
    virtual amf_int32 GetInputSlotCount() const { return m_iInputCount; }
    virtual amf_int32 GetOutputSlotCount() const { return 1; }
    virtual AMF_RESULT SubmitInput(amf::AMFData* pData, amf_int32 slot);
    amf_int32 GetFrameCount(){ return m_countFrame; };

    virtual AMF_RESULT OnEof();
    virtual AMF_RESULT SubmitInputInt(amf::AMFData* pData, amf_int32 slot);
    AMF_RESULT QueryOutput(amf::AMFData** ppData);
    virtual AMF_RESULT Drain(amf_int32 inputSlot);
    AMF_RESULT Resubmit();
    AMF_RESULT Pause();
    AMF_RESULT Resume();
protected:
    amf::AMFComponentExPtr      m_pComponent;
    amf_int32                   m_iInputCount;
    amf::AMFCriticalSection     m_cs;
    AMFSize                     m_OutputSize;
    std::vector<Data>           m_lastInputs;
    bool                        m_bPause;
    amf_int32                   m_countFrame;
};

class StitchPipelineBase : public Pipeline, public ParametersStorage
{
    class  DecoderSubmissionSync;
    class PipelineElementAMFDecoder;
public:
    static const wchar_t* PARAM_NAME_INPUT;
    static const wchar_t* PARAM_NAME_PRESENTER;
    static const wchar_t* PARAM_NAME_FRAMERATE;
    static const wchar_t* PARAM_NAME_STREAM_COUNT;
    static const wchar_t* PARAM_NAME_COMPOSITED_WIDTH;
    static const wchar_t* PARAM_NAME_COMPOSITED_HEIGHT;
    static const wchar_t* PARAM_NAME_LOWLATENCY;
    static const wchar_t* PARAM_NAME_STITCH_ENGINE;
    static const wchar_t* PARAM_NAME_ZCAMLIVE_MODE;
    static const wchar_t* PARAM_NAME_PTS_FILE;
    static const wchar_t* PARAM_NAME_OUTPUT_FILE;
    static const wchar_t* PARAM_NAME_LOOP;
    static const wchar_t* PARAM_NAME_ENCODER_CODEC;
    static const wchar_t* PARAM_NAME_ENCODE;
    static const wchar_t* PARAM_NAME_ENCODE_PREVIEW;
    static const wchar_t* PARAM_NAME_STREAMING;
    static const wchar_t* PARAM_NAME_STREAMING_URL;
    static const wchar_t* PARAM_NAME_STREAMING_KEY;
    static const wchar_t* PARAM_NAME_AUDIO_CH;
    static const wchar_t* PARAM_NAME_AUDIO_MODE;
    static const wchar_t* PARAM_NAME_AUDIO_FILE;
    static const wchar_t* PARAM_NAME_ZCAMLIVE_IP;


    StitchPipelineBase();
    virtual ~StitchPipelineBase();

    // from Pipeline
    virtual AMF_RESULT  Stop();
    virtual AMF_RESULT  Restart();
    virtual double      GetFPS();

    // StitchPipelineBase
    virtual AMF_RESULT  InitMedia(const std::vector<std::wstring> &filenames, bool forceInitilize = false);
    virtual AMF_RESULT  InitCamera(const std::wstring &filenamesPTGui, bool forceInitilize=false);
    virtual AMF_RESULT  Init();

    virtual AMF_RESULT  Play();
    virtual AMF_RESULT  Pause();
    virtual AMF_RESULT  Step();

    virtual AMF_RESULT	Terminate();

    double              GetProgressSize();
    double              GetProgressPosition();
    amf_int64           GetFramesDropped();

    void                SetChannel(int channel);
    void                ChangeK1(bool bPlus, bool bAccel);
    void                ChangeK2(bool bPlus, bool bAccel);
    void                ChangeK3(bool bPlus, bool bAccel);
    void                ChangeZoom(bool bPlus, bool bAccel);
    void                ChangeOffsetX(bool bPlus, bool bAccel);
    void                ChangeOffsetY(bool bPlus, bool bAccel);


    void                ChangeCameraPitch(bool bPlus, bool bAccel);
    void                ChangeCameraYaw(bool bPlus, bool bAccel);
    void                ChangeCameraRoll(bool bPlus, bool bAccel);
    AMF_RESULT          GetProperty(int channel, wchar_t* paramName, double& value);
    AMF_RESULT          SetProperty(int channel, wchar_t* paramName, double value);

    amf::AMFContext*    GetContext() {return m_pContext;}

    AMF_RESULT          Dump();
    std::vector<StitchTemplate>*    GetCameras(){ return &m_Cameras; };
    std::vector<MediaInfo>*         GetMediaInfo();
    AMFSize                         GetInputSize(){ return m_sizeInput; };
    ParametersStorage&              GetEncoderParamsAVC() {return m_EncoderParamsAVC;}
    ParametersStorage&              GetEncoderParamsHEVC() {return m_EncoderParamsHEVC;}
    ParametersStorage&              GetEncoderParamsFFMPEG() { return m_EncoderParamsFFMPEG; }

    std::vector<MediaInfo>*         UpdateMediaInfo(const wchar_t *pName, amf_int32 ch);
    
protected:
    virtual AMF_RESULT  InitInternal();

    virtual AMF_RESULT  InitContext(amf::AMF_MEMORY_TYPE type) = 0;
    virtual AMF_RESULT  InitStitcher(amf::AMF_SURFACE_FORMAT &stitchInputFmt) = 0;
    virtual AMF_RESULT  CreateVideoPresenter(amf::AMF_MEMORY_TYPE type, amf_int32 compositedWidth, amf_int32 compositedHeight) = 0;
    virtual AMF_RESULT  InitVideoEncoder();

    AMF_RESULT StartConnect();
    AMF_RESULT OnConnect();
    void       ParsePTGuiProject(const std::wstring &ptguifilename);

    AMF_RESULT InitEncoderSW(const unsigned int codec_id, AMFSize frameSize);

    void       EofDecoders();
    void       ChangeParameter(const wchar_t *pName, bool bPlus, double step);
    void       ChangeParameterChannel(int channel, const wchar_t *pName, bool bPlus, double step);
    AMF_RESULT SetVideoMuxerParams(amf::AMFInput* const input);
    AMF_RESULT InitZCam();

    amf::AMFContextPtr                          m_pContext;
    VideoPresenterPtr                           m_pVideoPresenter;
    amf::AMFComponentExPtr                      m_pStitch;
    StitchElement*                              m_pStitchElement;
    std::vector<MediaInfo>                      m_Media;
    std::vector<StitchTemplate>                 m_Cameras;
    bool                                        m_bVideoPresenterDirectConnect;
	amf_uint32                                  m_nFfmpegRefCount;
	
    int                                         m_iCurrentChannel;   //current video channel for adjustment
    std::vector<StitchStream>                   m_Streams;
    amf::AMFComponentPtr                        m_pConverterEncode;
    std::shared_ptr<DecoderSubmissionSync>      m_pDecoderSubmissionSync;
    std::vector<amf::AMFComponentPtr>           m_Converters;
    amf::AMFComponentExPtr                      m_pMuxer;
    amf::AMFComponentExPtr                      m_pSourceZCam;

    amf::AMFComponentPtr                        m_pEncoder;
    ParametersStorage                           m_EncoderParamsAVC;
    ParametersStorage                           m_EncoderParamsHEVC;
    ParametersStorage                           m_EncoderParamsFFMPEG;
    SplitterPtr                                 m_pSplitter;
    amf_int32                                   m_stateCameras;   //0:not ready, 1: ready, 2:updated
    
    ParamAudioCodec                             m_paramsAudio;
    AMFSize                                     m_sizeInput;
    bool                                        m_mediaInitilized;
    bool                                        m_cameraInitilized;

    //audio
    amf::AMFComponentExPtr  m_pAudioCap;
    amf::AMFComponentExPtr  m_pSourceAudio;
    amf::AMFComponentPtr    m_pAudioDecoder;
    amf::AMFComponentPtr    m_pAudioConverter;
    amf::AMFComponentPtr    m_pAudioConverterEncode;
    amf::AMFComponentPtr    m_pAudioEncoder;
    AudioPresenterPtr       m_pAudioPresenter;
    amf::AMFComponentPtr    m_pAmbisonicRender;
    SplitterPtr             m_pSplitterAudio;
    amf_int32               m_streamAudioPinIndex;
    ParamAudioCodec         m_paramsAudioOutput;
    virtual AMF_RESULT  InitAudio(bool /* enableEncode */) { return AMF_NOT_IMPLEMENTED; };
    virtual AMF_RESULT  InitAudioSource() { return AMF_NOT_IMPLEMENTED; };
    virtual AMF_RESULT  InitAudioDecoder() { return AMF_NOT_IMPLEMENTED; };
    virtual AMF_RESULT  InitAudioAmbisonicRender() { return AMF_NOT_IMPLEMENTED; };
    virtual AMF_RESULT  InitAudioEncoder() { return AMF_NOT_IMPLEMENTED; };
    virtual AMF_RESULT  InitAudioMuxer(amf::AMFInputPtr /* pInput */) { return AMF_NOT_IMPLEMENTED; };
    virtual AMF_RESULT  InitAudioPipeline(PipelineElementPtr /* pMuxer */ = NULL) { return AMF_NOT_IMPLEMENTED; };
    virtual AMF_RESULT  InitAudioPresenter() { return AMF_NOT_IMPLEMENTED; };
    virtual AMF_RESULT  CreateAudioPresenter() { return AMF_NOT_IMPLEMENTED; };


    class ConnectThread : public amf::AMFThread
    {
        StitchPipelineBase* m_pHost;
    public:
        ConnectThread(StitchPipelineBase* pHost) : m_pHost(pHost){}
        virtual void Run()
        {
            while(!StopRequested())
            {
                AMF_RESULT res = m_pHost->OnConnect();
                if(res == AMF_OK)
                {
                    break;
                }
                amf_sleep(10);
            }
        }
    };
    ConnectThread                           m_ConnectThread;
    bool                                    m_bIsConnected;
    static amf::AMF_SURFACE_FORMAT          m_eComposerOutputDefault;
};