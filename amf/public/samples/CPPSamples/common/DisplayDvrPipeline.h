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
#include "public/include/components/VideoEncoderAV1.h"
#include "public/common/CurrentTimeImpl.h"

#if defined(_WIN32)
#if !defined(METRO_APP)
#include "DeviceDX9.h"
#endif//#if defined(METRO_APP)
#include "DeviceDX11.h"
#include "DeviceDX12.h"
#include "DeviceOpenCL.h"
#else
#include "DeviceVulkan.h"
#endif

#include "BitStreamParser.h"
#include "Pipeline.h"
#include "ParametersStorage.h"
#include "EncoderParamsAVC.h"
#include "EncoderParamsHEVC.h"
#include "EncoderParamsAV1.h"
#include "VideoPresenter.h"



// The DisplayDvrPipeline takes input from two sources and writes out
// out a movie file.  The video is generated from the desktop
// textures along with audio that is currently being played on
// the computer.
//
// The pipeline setup is the following:
//
//    DisplayCapture -> Converter -> Encoder
//                                            -> Muxer(writes out file)
//    AudioCapture -> Converter -> Encoder
//
// A video will still be generated if no audio is available
//
class DisplayDvrPipeline : public Pipeline, public ParametersStorage
{
    class PipelineElementEncoder;

public:
    DisplayDvrPipeline();
    virtual ~DisplayDvrPipeline();

public:
    // Codec
    static const wchar_t* PARAM_NAME_CODEC;
    // File to write media information to
    static const wchar_t* PARAM_NAME_OUTPUT;
    static const wchar_t* PARAM_NAME_URL;

    // GPU adapter ID
    static const wchar_t* PARAM_NAME_ADAPTERID;
    // Monitor index on GPU
    static const wchar_t* PARAM_NAME_MONITORID;
    static const wchar_t* PARAM_NAME_MULTI_MONITOR;

    // Video dimensions
    static const wchar_t* PARAM_NAME_VIDEO_HEIGHT;
    static const wchar_t* PARAM_NAME_VIDEO_WIDTH;

    // OpenCL Converter
    static const wchar_t* PARAM_NAME_OPENCL_CONVERTER;

    // Capture Component
    static const wchar_t* PARAM_NAME_CAPTURE_COMPONENT;

    // Pre Analysis
    static const wchar_t* PARAM_NAME_ENABLE_PRE_ANALYSIS;

#if !defined(METRO_APP)
    AMF_RESULT Init();
#else
    AMF_RESULT Init(const wchar_t* path, IRandomAccessStream^ inputStream, IRandomAccessStream^ outputStream,
        ISwapChainBackgroundPanelNative* previewTarget, AMFSize swapChainPanelSize, ParametersStorage* pParams);
#endif

    // AMFComponent interface
    virtual AMF_RESULT Stop();
    virtual void Terminate();

    // Method to provide a common pts for the pipeline
    amf_pts GetCurrentPts();

    // Info message for failures
    const wchar_t*        GetErrorMsg() const { return (m_errorMsg.empty()) ? NULL : m_errorMsg.c_str(); }

    // Pipeline format can change
    AMF_RESULT SwitchConverterFormat(amf_int32 index, amf::AMF_SURFACE_FORMAT format);
    AMF_RESULT GetMonitorIDs(std::vector<amf_uint32> &monitorIDs);
    AMF_RESULT SetMonitorIDs(const std::vector<amf_uint32>& monitorIDs);

    AMF_RESULT SetEngineMemoryTypes(amf::AMF_MEMORY_TYPE engineMemoryType);
    amf::AMF_MEMORY_TYPE  GetEngineMemoryTypes();

protected:
    virtual void OnParamChanged(const wchar_t* name);

private:
    AMF_RESULT            InitContext(
                            const std::wstring& engineStr,
                            amf::AMF_MEMORY_TYPE engineMemoryType,
                            amf_uint32 adapterID);

    AMF_RESULT            InitVideo(amf_uint32 monitorID,
                            amf::AMF_MEMORY_TYPE engineMemoryType,
                            amf_int32 videoWidth, amf_int32 videoHeight);

    AMF_RESULT            InitAudio();

    AMF_RESULT            InitMuxer(
                            amf_bool hasDDVideoStream, amf_bool hasSessionAudioStream,
                            amf_int32& outVideoStreamIndex, amf_int32& outAudioStreamIndex);

    AMF_RESULT            ConnectPipeline();

    void                  SetErrorMessage(const wchar_t* msg) { m_errorMsg = msg; }

    AMF_RESULT            UpdateMuxerFileName();

    amf::AMF_MEMORY_TYPE            m_engineMemoryType;

#if defined(_WIN32)
#if !defined(METRO_APP)
    DeviceDX9                       m_deviceDX9;
#endif//#if !defined(METRO_APP)
    DeviceDX11                      m_deviceDX11;
    DeviceDX12                      m_deviceDX12;
    DeviceOpenCL                    m_deviceOpenCL;
#else
    DeviceVulkan                m_deviceVulkan;
#endif

    amf::AMFContextPtr              m_pContext;

    // Video side
    std::vector<amf::AMFComponentPtr>            m_pDisplayCapture;
    std::vector<amf::AMFComponentPtr>            m_pConverter;
    std::vector<amf::AMFComponentPtr>            m_pEncoder;
    std::wstring                    m_szEncoderID;

    // Audio side
    amf::AMFComponentPtr            m_pAudioCapture;
    amf::AMFComponentPtr            m_pAudioDecoder;
    amf::AMFComponentPtr            m_pAudioConverter;
    amf::AMFComponentPtr            m_pAudioEncoder;

    // Muxer to bring audio and video together
    std::vector<amf::AMFComponentExPtr>          m_pMuxer;

    // Error string
    std::wstring                    m_errorMsg;

    // Current time for the pipeline
    amf::AMFCurrentTimePtr          m_pCurrentTime;

    // Muxer indices
    amf_int32                        m_outVideoStreamMuxerIndex;
    amf_int32                        m_outAudioStreamMuxerIndex;

    mutable amf::AMFCriticalSection m_sync;

    bool                            m_useOpenCLConverter;

};
