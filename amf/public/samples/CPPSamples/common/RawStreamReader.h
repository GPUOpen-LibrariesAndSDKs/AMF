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
#ifndef AMF_RawStreamReader_h
#define AMF_RawStreamReader_h

#pragma once

#include "public/samples/CPPSamples/common/PipelineElement.h"
#include "public/samples/CPPSamples/common/ParametersStorage.h"
#include "public/common/ByteArray.h"


class RawStreamReader :public PipelineElement
{
public:
    RawStreamReader();
    virtual ~RawStreamReader();

    virtual AMF_RESULT Init(ParametersStorage* pParams, amf::AMFContext* pContext);

    virtual amf::AMF_SURFACE_FORMAT GetFormat()                 { return m_format; }
    virtual amf_int32               GetWidth()                  { return m_roi_width; }
    virtual amf_int32               GetHeight()                 { return m_roi_height; }
    virtual amf::AMF_MEMORY_TYPE    GetMemoryType()             { return m_memoryType; }

    virtual amf_int32               GetInputSlotCount() const   { return 0; }
    virtual amf_int32               GetOutputSlotCount() const  { return 1; }
    virtual amf_double              GetPosition()               { return static_cast<amf_double>(m_framesCountRead)/m_framesCount; }


    static void  ParseRawFileFormat(const std::wstring path, amf_int32 &width, amf_int32 &height, amf::AMF_SURFACE_FORMAT& format);
    void RestartReader();

private:
    virtual AMF_RESULT SubmitInput(amf::AMFData* pData);
    virtual AMF_RESULT QueryOutput(amf::AMFData** ppData);

    virtual AMF_RESULT Terminate();
    AMF_RESULT ReadNextFrame(int dstStride, int dstHeight, int valignment, unsigned char* pDstBits);
	AMF_RESULT ReadNextSearchCenterMap(int dstStride, int dstHeight, int valignment, unsigned char* pDstBits);

    amf::AMFContextPtr      m_pContext;
    amf::AMFDataStreamPtr   m_pDataStream;
    
    amf::AMF_SURFACE_FORMAT m_format;
    amf::AMF_MEMORY_TYPE    m_memoryType;

    amf_int32               m_width;
    amf_int32               m_height;
    amf_int32               m_stride;

    amf_int32               m_roi_x; 
    amf_int32               m_roi_y;
    amf_int32               m_roi_width;
    amf_int32               m_roi_height;

    amf_int64               m_framesCount;
    amf_int64               m_framesCountRead;

    AMFByteArray            m_frame;

	amf::AMFDataStreamPtr   m_pSearchCenterMapStream;   
	amf::AMF_SURFACE_FORMAT m_searchCenterMapformat;    
	amf_int32               m_searchCenterMapSize;     
	amf_int32               m_searchCenterMapWidth;
	amf_int32               m_searchCenterMapHeight;
	amf_int32               m_searchCenterMapStride;
	AMFByteArray            m_searchCenterMapFrame;
	amf_bool                m_pSearchCenterMapEnabled;
};

typedef std::shared_ptr<RawStreamReader> RawStreamReaderPtr;

amf::AMF_SURFACE_FORMAT AMF_STD_CALL  GetFormatFromString(const wchar_t* str);
bool                    AMF_STD_CALL  amf_path_is_relative(const wchar_t* const path);

#endif // AMF_RawStreamReader_h