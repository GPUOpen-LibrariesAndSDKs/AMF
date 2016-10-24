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
#include "public/include/core/Data.h"
#include "public/include/core/AudioBuffer.h"
#include "PipelineElement.h"

#if defined(METRO_APP)
#include <collection.h>
#include <windows.ui.xaml.media.dxinterop.h>
using namespace Platform;
using namespace Windows::UI;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
#endif



class AudioPresenter;
typedef std::shared_ptr<AudioPresenter> AudioPresenterPtr;



class AudioPresenter: public PipelineElement
{
public:
    virtual ~AudioPresenter();

    virtual AMF_RESULT            AMF_STD_CALL Init() = 0;

    virtual amf_int32   GetInputSlotCount()   {  return 1;  }
    virtual amf_int32   GetOutputSlotCount()  {  return 0;  }

    virtual AMF_RESULT GetDescription(
        amf_int64 &streamBitRate,
        amf_int64 &streamSampleRate,
        amf_int64 &streamChannels,
        amf_int64 &streamFormat,
        amf_int64 &streamLayout,
        amf_int64 &streamBlockAlign
    ) = 0;

    virtual AMF_RESULT  SubmitInput(amf::AMFData* pData);


#if defined(METRO_APP)
    static AudioPresenterPtr Create(ISwapChainBackgroundPanelNative* pSwapChainPanel, AMFSize swapChainPanelSize, amf::AMFContext* pContext);
#else
    static AudioPresenterPtr Create(amf::AMFContext* pContext);
#endif


protected:
    AudioPresenter(amf::AMFContext* pContext);

    amf::AMFContext*        m_pContext;
    amf::AMFAudioBufferPtr  m_pLastData;
};