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

//-------------------------------------------------------------------------------------------------
// VideoTransfer declaration
//-------------------------------------------------------------------------------------------------
#ifndef __VideoTransfer_h__
#define __VideoTransfer_h__
#pragma once

#include "public/include/core/Context.h"

#if defined(__cplusplus)
namespace amf
{
#endif

    class AMF_NO_VTABLE AMFVideoTransfer : public AMFInterface
    {
    public:
        AMF_DECLARE_IID (0x83ac29e1, 0xcfd7, 0x49f9, 0x98, 0xe4, 0xa3, 0xd2, 0x38, 0x40, 0xdf, 0xb6)

        virtual AMF_RESULT          AMF_STD_CALL AllocateSurface(size_t size, AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height, void **ppVirtualMemory, void **ppSurface) = 0;
        virtual AMF_RESULT          AMF_STD_CALL ReleaseSurface(void *pSurface) = 0;
        virtual AMF_RESULT          AMF_STD_CALL Transfer(void *pVirtualMemory, amf_size inLineSizeInBytes, void *pSurface) = 0;

        static  AMF_RESULT          AMF_STD_CALL CreateVideoTransfer(AMFContext *pContext, AMF_MEMORY_TYPE eType, AMFVideoTransfer **ppTransfer);
    };
    //----------------------------------------------------------------------------------------------
    // smart pointer
    //----------------------------------------------------------------------------------------------
    typedef AMFInterfacePtr_T<AMFVideoTransfer> AMFVideoTransferPtr;



#if defined(__cplusplus)
} // namespace
#endif

#endif // __VideoTransfer_h__