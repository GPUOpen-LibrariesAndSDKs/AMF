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

#include "AudioPresenter.h"
#include "AudioPresenterWin.h"

//#if !defined(METRO_APP)
//#include "AudioPresenterDX9.h"
//#include "AudioPresenterOpenGL.h"
//#endif
//#include "public/include/components/VideoConverter.h"

using namespace amf;


AudioPresenter::AudioPresenter(amf::AMFContext* pContext) 
    : m_pContext(pContext)
{
}

AudioPresenter::~AudioPresenter()
{
}


#if defined(METRO_APP)
AudioPresenterPtr AudioPresenter::Create(ISwapChainBackgroundPanelNative* pSwapChainPanel, AMFSize swapChainPanelSize, amf::AMFContext* pContext)
{
    AudioPresenterPtr pPresenter;
    pPresenter = AudioPresenterPtr(new AudioPresenterDX11(pSwapChainPanel, swapChainPanelSize, pContext));
    return pPresenter;
}
#else
AudioPresenterPtr AudioPresenter::Create(amf::AMFContext* pContext)
{
#if defined(_WIN32)
    AudioPresenterPtr pPresenter = AudioPresenterPtr(new AudioPresenterWin(pContext));
#else
    AudioPresenterPtr pPresenter = AudioPresenterPtr(new AudioPresenter(pContext));
#endif
    return pPresenter;
}
#endif


AMF_RESULT AudioPresenter::SubmitInput(amf::AMFData* pData)
{
    // check if we get new input - if we don't and we don't 
    // have anything cached, there's nothing to process
    if ((pData == NULL) && (m_pLastData == NULL))
    {
        return AMF_EOF;
    }

    // if we submit the same information, do nothing
    if (m_pLastData == pData)
    {
        return AMF_ALREADY_INITIALIZED;
    }

    // there's not much to do in the base class
    m_pLastData = AMFAudioBufferPtr(pData);
    return AMF_OK;
}
