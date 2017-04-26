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
#include "PlaybackPipeline.h"
#include "AudioPresenterWin.h"
#include "BackBufferPresenter.h"

PlaybackPipeline::PlaybackPipeline()
    : m_hwnd(NULL)
{
}

AMF_RESULT
PlaybackPipeline::Init(HWND hwnd)
{
    m_hwnd = hwnd;
    return PlaybackPipelineBase::Init();
}

AMF_RESULT
PlaybackPipeline::InitContext(amf::AMF_MEMORY_TYPE type)
{
    g_AMFFactory.GetFactory()->CreateContext(&m_pContext);

    switch (type)
    {
    case amf::AMF_MEMORY_DX9:
        {
            const AMF_RESULT res = m_pContext->InitDX9(NULL);
            CHECK_AMF_ERROR_RETURN(res, "Init DX9");
            m_bVideoPresenterDirectConnect = true;
            return AMF_OK;
        }

    case amf::AMF_MEMORY_DX11:
        {
            const AMF_RESULT res = m_pContext->InitDX11(NULL);
            CHECK_AMF_ERROR_RETURN(res, "Init DX11");
            m_bVideoPresenterDirectConnect = true;
            return AMF_OK;
        }

    case amf::AMF_MEMORY_OPENGL:
        {
            AMF_RESULT res = m_pContext->InitDX9(NULL);
            CHECK_AMF_ERROR_RETURN(res, "Init DX9");
            res = m_pContext->InitOpenGL(NULL, m_hwnd, NULL);
            CHECK_AMF_ERROR_RETURN(res, "Init OpenGL");
            m_bVideoPresenterDirectConnect = false;
            return AMF_OK;
        }

    default:
        return AMF_NOT_SUPPORTED;
    }
}

AMF_RESULT
PlaybackPipeline::CreateVideoPresenter(amf::AMF_MEMORY_TYPE type, amf_int64 /*bitRate*/, double /*fps*/)
{
    BackBufferPresenterPtr pBackBufferPresenter;
    AMF_RESULT res = BackBufferPresenter::Create(pBackBufferPresenter, type, m_hwnd, m_pContext);
    m_pVideoPresenter = pBackBufferPresenter;
    return res;
}

AMF_RESULT
PlaybackPipeline::CreateAudioPresenter()
{
#if defined(_WIN32)
    m_pAudioPresenter = std::make_shared<AudioPresenterWin>();
    return AMF_OK;
#else
    return AMF_NOT_IMPLEMENTED;
#endif
}