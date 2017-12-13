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
#include "public/include/components/VideoEncoderVCE.h"
#include "public/include/components/VideoEncoderHEVC.h"

#if !defined(METRO_APP)
#include "DeviceDX9.h"
#endif//#if defined(METRO_APP)
#include "DeviceDX11.h"

#include "BitStreamParser.h"
#include "Pipeline.h"
#include "ParametersStorage.h"
#include "EncoderParamsAVC.h"
#include "EncoderParamsHEVC.h"
#include "BackBufferPresenter.h"


class TranscodePipeline : public Pipeline
{
    class PipelineElementEncoder;
public:
    TranscodePipeline();
    virtual ~TranscodePipeline();
public:
    static const wchar_t* PARAM_NAME_CODEC;
    static const wchar_t* PARAM_NAME_OUTPUT;
    static const wchar_t* PARAM_NAME_INPUT;

    static const wchar_t* PARAM_NAME_SCALE_WIDTH;
    static const wchar_t* PARAM_NAME_SCALE_HEIGHT;
    static const wchar_t* PARAM_NAME_ADAPTERID;
    static const wchar_t* PARAM_NAME_ENGINE;
    static const wchar_t* PARAM_NAME_FRAMES;



#if !defined(METRO_APP)
    AMF_RESULT Init(ParametersStorage* pParams, HWND previewTarget, int threadID = -1);
#else
    AMF_RESULT Init(const wchar_t* path, IRandomAccessStream^ inputStream, IRandomAccessStream^ outputStream, 
        ISwapChainBackgroundPanelNative* previewTarget, AMFSize swapChainPanelSize, ParametersStorage* pParams);
#endif
    void Terminate();

    double GetProgressSize();
    double GetProgressPosition();

    AMF_RESULT Run();

protected:
    virtual AMF_RESULT  InitAudio(amf::AMFOutput* pOutput);
    virtual AMF_RESULT  InitVideo(BitStreamParserPtr pParser, amf::AMFOutput* pOutput, amf::AMF_MEMORY_TYPE presenterEngine, HWND hwnd, ParametersStorage* pParams);

    virtual AMF_RESULT  InitVideoDecoder(const wchar_t *pDecoderID, amf_int32 videoWidth, amf_int32 videoHeight, amf::AMFBuffer* pExtraData);
    virtual AMF_RESULT  InitVideoProcessor(amf::AMF_MEMORY_TYPE presenterEngine, amf_int32 inWidth, amf_int32 inHeight, amf_int32 outWidth, amf_int32 outHeight);

#if !defined(METRO_APP)
    DeviceDX9                   m_deviceDX9;
#endif//#if !defined(METRO_APP)
    DeviceDX11                  m_deviceDX11;

    amf::AMFContextPtr              m_pContext;

    amf::AMFDataStreamPtr           m_pStreamIn;
    amf::AMFDataStreamPtr           m_pStreamOut;
    
    amf::AMFComponentExPtr          m_pDemuxer;
    amf::AMFComponentPtr            m_pDecoder;
    amf::AMFComponentPtr            m_pConverter;
    amf::AMFComponentPtr            m_pEncoder;
    std::wstring                    m_EncoderID;
    StreamWriterPtr                 m_pStreamWriter;

    amf::AMFComponentPtr    m_pAudioDecoder;
    amf::AMFComponentPtr    m_pAudioConverter;
    amf::AMFComponentPtr    m_pAudioEncoder;

    amf::AMFComponentExPtr          m_pMuxer;

    SplitterPtr                     m_pSplitter;
    amf::AMFComponentPtr            m_pConverter2;
    BackBufferPresenterPtr          m_pPresenter;
    amf::AMF_SURFACE_FORMAT         m_eDecoderFormat;
};