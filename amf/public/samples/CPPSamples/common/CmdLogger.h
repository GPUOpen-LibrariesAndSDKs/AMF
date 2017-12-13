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
#include <string>
#include <memory>
#include <sstream>
#include <ios>
#include "public/include/core/Result.h"
#include "public/include/core/Debug.h"
#include "public/common/Thread.h"
#include "public/common/AMFFactory.h"

enum AMFLogLevel
{ 
    AMFLogLevelInfo,
    AMFLogLevelSuccess,
    AMFLogLevelError
};

void WriteLog(const wchar_t* message, AMFLogLevel level);

#define LOG_WRITE(a, level)\
    { \
        std::wstringstream messageStream12345;\
        messageStream12345 << a;\
        WriteLog(messageStream12345.str().c_str(), level);\
    }

#define LOG_INFO(a) LOG_WRITE(a << std::endl, AMFLogLevelInfo)
#define LOG_SUCCESS(a) LOG_WRITE(a << std::endl, AMFLogLevelSuccess)
#define LOG_ERROR(a) LOG_WRITE(a << std::endl, AMFLogLevelError)

#ifdef _DEBUG
    #define LOG_DEBUG(a)     LOG_INFO(a)
#else
    #define LOG_DEBUG(a)
#endif

#define LOG_AMF_ERROR(err, text) \
    { \
        if( (err) != AMF_OK) \
        { \
            LOG_WRITE(text << L" Error:" << g_AMFFactory.GetTrace()->GetResultText((err)) << std::endl, AMFLogLevelError);\
        } \
    }

#define CHECK_RETURN(exp, err, text) \
    { \
        if((exp) == false) \
        {  \
            LOG_AMF_ERROR(err, text);\
            return (err); \
        } \
    }

#define CHECK_AMF_ERROR_RETURN(err, text) \
    { \
        if((err) != AMF_OK) \
        {  \
            LOG_AMF_ERROR(err, text);\
            return (err); \
        } \
    }

#define CHECK_HRESULT_ERROR_RETURN(err, text) \
    { \
        if(FAILED(err)) \
        {  \
            LOG_WRITE(text << L" HRESULT Error: " << std::hex << err << std::endl, AMFLogLevelError); \
            return AMF_FAIL; \
        } \
    }

#define CHECK_OPENCL_ERROR_RETURN(err, text) \
    { \
        if(err) \
        {  \
            LOG_WRITE(text << L" OpenCL Error: " << err<< std::endl, AMFLogLevelError); \
            return AMF_FAIL; \
        } \
    }


class AMFCustomTraceWriter : public amf::AMFTraceWriter
{
public:
    AMFCustomTraceWriter(amf_int32 level)
    {
        g_AMFFactory.Init();
        g_AMFFactory.GetTrace()->RegisterWriter(L"AMFCustomTraceWriter", this, true);
        g_AMFFactory.GetTrace()->SetWriterLevel(L"AMFCustomTraceWriter", level);
        //disabling amf console writer, we receiving output here
        g_AMFFactory.GetTrace()->EnableWriter(AMF_TRACE_WRITER_CONSOLE, false);
    }
    virtual ~AMFCustomTraceWriter()
    {
        g_AMFFactory.GetTrace()->UnregisterWriter(L"AMFCustomTraceWriter");
        g_AMFFactory.Terminate();
    }
    virtual void AMF_CDECL_CALL Write(const wchar_t* scope, const wchar_t* message) override
    {
        WriteLog(message, AMFLogLevelInfo);
    }
    virtual void AMF_CDECL_CALL Flush() override
    {
    }
};
