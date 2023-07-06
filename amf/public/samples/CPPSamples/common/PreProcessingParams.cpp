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

#include "public/include/components/PreProcessing.h"
#include "CmdLogger.h"
#include "ParametersStorage.h"
#include "PreProcessingParams.h"

#include <algorithm>
#include <iterator>
#include <cctype>



AMF_RESULT RegisterPreProcessingParams(ParametersStorage* pParams)
{
    // PP parameters
    pParams->SetParamDescription(AMF_VIDEO_PRE_ENCODE_FILTER_ENABLE, ParamCommon, L"Enable Edge Filter (true, false default =  false)", ParamConverterBoolean);

    pParams->SetParamDescription(AMF_PP_ENGINE_TYPE, ParamCommon, L"Engine Type (DX11, Vulkan, OPENCL, HOST, Auto default = OPENCL)", ParamConverterMemoryType);

    pParams->SetParamDescription(AMF_PP_ADAPTIVE_FILTER_STRENGTH, ParamCommon, L"Edge Detect filter strength (0 - 10: the lower the bitrate, the bigger the 'strength')", ParamConverterInt64);
    pParams->SetParamDescription(AMF_PP_ADAPTIVE_FILTER_SENSITIVITY, ParamCommon, L"Edge Detect filter sensitivity (0 - 10: the lower the bitrate, the bigger the 'sensitivity')", ParamConverterInt64);

    // Adaptive pre-filter parameters
    pParams->SetParamDescription(AMF_PP_TARGET_BITRATE, ParamCommon, L"Target bitrate utilized in the adaptive filter algorithm, (default: 2000000)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_PP_FRAME_RATE, ParamCommon, L"Frame rate utilized in the adaptive filter algorithm, (default: 30, 1)", ParamConverterRate);
    pParams->SetParamDescription(AMF_PP_ADAPTIVE_FILTER_ENABLE, ParamCommon, L"Enable the adaptive filter mechanism (default: false)", ParamConverterBoolean);
    return AMF_OK;
}
