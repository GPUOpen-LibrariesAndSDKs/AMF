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
#ifndef AMF_PipelineDefines_h
#define AMF_PipelineDefines_h

#pragma once


#define PARAM_NAME_CODEC                   L"CODEC"
#define PARAM_NAME_INPUT                   L"INPUT"
#define PARAM_NAME_INPUT_DIR               L"INPUT_DIR"
#define PARAM_NAME_INPUT_WIDTH             L"WIDTH"
#define PARAM_NAME_INPUT_HEIGHT            L"HEIGHT"
#define PARAM_NAME_INPUT_FORMAT            L"FORMAT"
#define PARAM_NAME_INPUT_FRAMES            L"FRAMES"
               
#define PARAM_NAME_INPUT_ROI_X             L"ROI_X"
#define PARAM_NAME_INPUT_ROI_Y             L"ROI_Y"
#define PARAM_NAME_INPUT_ROI_WIDTH         L"ROI_WIDTH"
#define PARAM_NAME_INPUT_ROI_HEIGHT        L"ROI_HEIGHT"
               
#define PARAM_NAME_INPUT_MEMORY_TYPE       L"INPUT_MEMORY_TYPE"
#define PARAM_NAME_INPUT_OFFSET_X          L"INPUT_OFFSET_X"
#define PARAM_NAME_INPUT_OFFSET_Y          L"INPUT_OFFSET_Y"
               
#define PARAM_NAME_OUTPUT_MEMORY_TYPE      L"OUTPUT_MEMORY_TYPE"
#define PARAM_NAME_OUTPUT_OFFSET_X         L"OUTPUT_OFFSET_X"
#define PARAM_NAME_OUTPUT_OFFSET_Y         L"OUTPUT_OFFSET_Y"
               
#define PARAM_NAME_CROP_WIDTH              L"CROP_WIDTH"
#define PARAM_NAME_CROP_HEIGHT             L"CROP_HEIGHT"
               
#define PARAM_NAME_OUTPUT                  L"OUTPUT"
#define PARAM_NAME_OUTPUT_DIR              L"OUTPUT_DIR"
#define PARAM_NAME_OUTPUT_WIDTH            L"OUTPUT_WIDTH"
#define PARAM_NAME_OUTPUT_HEIGHT           L"OUTPUT_HEIGHT"
#define PARAM_NAME_OUTPUT_FORMAT           L"OUTPUT_FORMAT"
               
#define PARAM_NAME_TEST_MODE               L"TEST_MODE"
#define PARAM_NAME_VALIDATE                L"VALIDATE"
#define PARAM_NAME_THREADCOUNT             L"THREADCOUNT"
#define PARAM_NAME_ADAPTERID               L"ADAPTERID"
#define PARAM_NAME_ENGINE                  L"ENGINE"

#define PARAM_NAME_SEARCH_CENTER_MAP_INPUT L"SEARCH_CENTER_MAP_INPUT"
#define PARAM_NAME_COMPUTE_QUEUE           L"COMPUTEQUEUE"
#define PARAM_NAME_SWENCODE                L"ENABLE_SWENCODE"
#define PARAM_NAME_SWDECODE                L"ENABLE_SWDECODE"

#define PARAM_NAME_TRACE_LEVEL             L"TRACE_LEVEL"
#define PARAM_NAME_TRACE_TO_FILE           L"TRACE_TO_FILE"
#define PARAM_NAME_DECODER_INSTANCE        L"DECODER_INSTANCE"

#endif // AMF_PipelineDefines_h