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
#include "CaptureVideoPipeline.h"
#include "public/samples/CPPSamples/common/VideoPresenter.h"

CaptureVideoPipeline::CaptureVideoPipeline()
    : m_hwnd(NULL)
{
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT CaptureVideoPipeline::Init(
HWND hwnd,
const std::wstring& filename,
const std::wstring& filenameBK
)
{
    m_filename = filename;
    m_filenameBK = filenameBK;
    m_hwnd = hwnd;
    AMF_RESULT res = AMF_OK;
    m_bUseBackBufferPresenter = true;
    CHECK_AMF_ERROR_RETURN(res, L"InitMedia() failed!\n");
    res = CaptureVideoPipelineBase::Init();
    CHECK_AMF_ERROR_RETURN(res, L"StitchPipelineBase::Init() failed!\n");
    return res;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT CaptureVideoPipeline::InitContext(amf::AMF_MEMORY_TYPE type)
{
    if(m_pContext != NULL)
    {
        return AMF_OK;
    }
    g_AMFFactory.GetFactory()->CreateContext(&m_pContext);

    AMF_RESULT res = AMF_OK;

    switch (type)
    {
    case amf::AMF_MEMORY_DX11:
        res = m_pContext->InitDX11(NULL);
        CHECK_AMF_ERROR_RETURN(res, "Init DX11");
        break;
    case amf::AMF_MEMORY_OPENGL:
        m_pContext->InitDX9(NULL);
        CHECK_AMF_ERROR_RETURN(res, "Init DX9");
        res = m_pContext->InitOpenGL(NULL, m_hwnd, NULL);
        CHECK_AMF_ERROR_RETURN(res, "Init OpenGL");
        break;
    default:
        break;;
    }
    res = AMFCreateCaptureManager(m_pContext, &m_pCaptureManager);
    CHECK_AMF_ERROR_RETURN(res, "AMFCreateCaptureManager() failed");
    res = m_pCaptureManager->Update();
    CHECK_AMF_ERROR_RETURN(res, "m_pCaptureManager->Update() failed");
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT CaptureVideoPipeline::CreateVideoPresenter(
    amf::AMF_MEMORY_TYPE type, 
    amf_int32 /* compositedWidth */,
    amf_int32 /* compositedHeight */ )
{
    return VideoPresenter::Create(m_pVideoPresenter, type, m_hwnd, m_pContext);
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
