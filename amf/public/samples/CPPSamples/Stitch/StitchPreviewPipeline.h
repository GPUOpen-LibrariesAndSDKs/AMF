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

#include "public/samples/CPPSamples/common/StitchPipeline.h"

class StitchPreviewPipeline : public StitchPipeline
{
public:
    StitchPreviewPipeline();

    AMF_RESULT Init(
        HWND hwnd,
        const std::vector<std::wstring>& filenames,
        const std::wstring& ptguifilename,
        AMF_VIDEO_STITCH_OUTPUT_MODE_ENUM outputMode
    );

    AMF_RESULT Init(
        const std::vector<std::wstring>& filenames,
        const std::wstring& ptguifilename,
        AMF_VIDEO_STITCH_OUTPUT_MODE_ENUM outputMode
    ) = delete;

    void MouseShift(int x1, int y1, int x2, int y2);

protected:
    AMF_RESULT InitContext(amf::AMF_MEMORY_TYPE type) override;
    AMF_RESULT CreateVideoPresenter(amf::AMF_MEMORY_TYPE type, amf_int32 compositedWidth, amf_int32 compositedHeight) override;
    AMF_RESULT CreateAudioPresenter();

    HWND m_hwnd;
};

typedef std::shared_ptr<StitchPreviewPipeline> StitchPreviewPipelinePtr;
