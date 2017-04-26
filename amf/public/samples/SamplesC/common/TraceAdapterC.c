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

#include "../../../include/core/Factory.h"
#include "ThreadC.h"
#include "TraceAdapterC.h"

#pragma warning(disable: 4251)
#pragma warning(disable: 4996)

#ifdef AMF_CORE_STATIC
extern "C"
{
    // forward definition of function from FactoryImpl.cpp
    extern AMF_CORE_LINK AMF_RESULT AMF_CDECL_CALL AMFInit(amf_uint64 version, AMFFactory **ppFactory);
}
#endif


//------------------------------------------------------------------------------------------------
static AMFTrace *s_pTrace = NULL;

static AMF_RESULT SetATracer(AMFTrace *localTracer)
{
    s_pTrace = localTracer;
    return AMF_OK;
}

static AMFTrace *GetTrace()
{
    if (s_pTrace == NULL)
    {
#ifndef AMF_CORE_STATIC
        amf_handle module = amf_load_library(AMF_DLL_NAME);
        if(module != NULL)
        {
            AMFInit_Fn initFun = (AMFInit_Fn)amf_get_proc_address(module, AMF_INIT_FUNCTION_NAME);
            AMFFactory *pFactory = NULL;
            initFun(AMF_FULL_VERSION, &pFactory);
            pFactory->pVtbl->GetTrace(pFactory, &s_pTrace);
            amf_free_library(module);
        }
#else
        AMFFactory *pFactory = NULL;
        AMFInit(AMF_FULL_VERSION, &pFactory);
        pFactory->pVtbl->GetTrace(pFactory, &s_pTrace);
#endif
    }
    return s_pTrace;
}

//------------------------------------------------------------------------------------------------
static AMFDebug *s_pDebug = NULL;

static AMF_RESULT SetADebugger(AMFDebug *localTracer)
{
    s_pDebug = localTracer;
    return AMF_OK;
}

static AMFDebug *GetDebug()
{
    if (s_pDebug == NULL)
    {
#ifndef AMF_CORE_STATIC
        amf_handle module = amf_load_library(AMF_DLL_NAME);
        if(module != NULL)
        {
            AMFInit_Fn initFun = (AMFInit_Fn)amf_get_proc_address(module, AMF_INIT_FUNCTION_NAME);
            AMFFactory *pFactory = NULL;
            initFun(AMF_FULL_VERSION, &pFactory);
            pFactory->pVtbl->GetDebug(pFactory, &s_pDebug);
            amf_free_library(module);
        }
#else
        AMFFactory *pFactory = NULL;
        AMFInit(AMF_FULL_VERSION, &pFactory);
        pFactory->pVtbl->GetDebug(pFactory, &s_pDebug);
#endif
    }
    return s_pDebug;
}
//------------------------------------------------------------------------------------------------
AMF_RESULT AMF_CDECL_CALL AMFSetCustomDebugger(AMFDebug *pDebugger)
{
    return SetADebugger(pDebugger);
}
//------------------------------------------------------------------------------------------------
AMF_RESULT AMF_CDECL_CALL AMFSetCustomTracer(AMFTrace *ATracer)
{
    return SetATracer(ATracer);
}
//------------------------------------------------------------------------------------------------
AMF_RESULT AMF_CDECL_CALL AMFTraceEnableAsync(amf_bool enable)
{
    return GetTrace()->pVtbl->TraceEnableAsync(GetTrace(), enable);
}
//------------------------------------------------------------------------------------------------
AMF_RESULT AMF_CDECL_CALL AMFTraceFlush()
{
    return GetTrace()->pVtbl->TraceFlush(GetTrace());
}
//------------------------------------------------------------------------------------------------
void AMF_CDECL_CALL AMFTraceW(const wchar_t* src_path, amf_int32 line, amf_int32 level, const wchar_t* scope,
            amf_int32 countArgs, const wchar_t* format, ...) // if countArgs <= 0 -> no args, formatting could be optimized then
{
    if(countArgs <= 0)
    {
        GetTrace()->pVtbl->Trace(GetTrace(), src_path, line, level, scope, format, NULL);
    }
    else
    {
        va_list vl;
        va_start(vl, format);

        GetTrace()->pVtbl->Trace(GetTrace(), src_path, line, level, scope, format, &vl);

        va_end(vl);
    }
}
//------------------------------------------------------------------------------------------------
AMF_RESULT AMF_CDECL_CALL AMFTraceSetPath(const wchar_t* path)
{
    return GetTrace()->pVtbl->SetPath(GetTrace(), path);
}
//------------------------------------------------------------------------------------------------
AMF_RESULT AMF_CDECL_CALL AMFTraceGetPath(wchar_t* path, amf_size* pSize)
{
    return GetTrace()->pVtbl->GetPath(GetTrace(), path, pSize);
}
//------------------------------------------------------------------------------------------------
amf_bool AMF_CDECL_CALL AMFTraceEnableWriter(const wchar_t* writerID, amf_bool enable)
{
    return GetTrace()->pVtbl->EnableWriter(GetTrace(), writerID, enable);
}
//------------------------------------------------------------------------------------------------
amf_bool AMF_CDECL_CALL AMFTraceWriterEnabled(const wchar_t* writerID)
{
    return GetTrace()->pVtbl->WriterEnabled(GetTrace(), writerID);
}
//------------------------------------------------------------------------------------------------
amf_int32 AMF_CDECL_CALL AMFTraceSetGlobalLevel(amf_int32 level)
{
    return GetTrace()->pVtbl->SetGlobalLevel(GetTrace(), level);
}
//------------------------------------------------------------------------------------------------
amf_int32 AMF_CDECL_CALL AMFTraceGetGlobalLevel()
{
    return GetTrace()->pVtbl->GetGlobalLevel(GetTrace());
}
//------------------------------------------------------------------------------------------------
amf_int32 AMF_CDECL_CALL AMFTraceSetWriterLevel(const wchar_t* writerID, amf_int32 level)
{
    return GetTrace()->pVtbl->SetWriterLevel(GetTrace(), writerID, level);
}
//------------------------------------------------------------------------------------------------
amf_int32 AMF_CDECL_CALL AMFTraceGetWriterLevel(const wchar_t* writerID)
{
    return GetTrace()->pVtbl->GetWriterLevel(GetTrace(), writerID);
}
//------------------------------------------------------------------------------------------------
amf_int32 AMF_CDECL_CALL AMFTraceSetWriterLevelForScope(const wchar_t* writerID, const wchar_t* scope, amf_int32 level)
{
    return GetTrace()->pVtbl->SetWriterLevelForScope(GetTrace(), writerID, scope, level);
}
//------------------------------------------------------------------------------------------------
amf_int32 AMF_CDECL_CALL AMFTraceGetWriterLevelForScope(const wchar_t* writerID, const wchar_t* scope)
{
    return GetTrace()->pVtbl->GetWriterLevelForScope(GetTrace(), writerID, scope);
}
//------------------------------------------------------------------------------------------------
void AMF_CDECL_CALL AMFTraceRegisterWriter(const wchar_t* writerID, AMFTraceWriter* pWriter)
{
    GetTrace()->pVtbl->RegisterWriter(GetTrace(), writerID, pWriter, true);
}

void AMF_CDECL_CALL AMFTraceUnregisterWriter(const wchar_t* writerID)
{
    GetTrace()->pVtbl->UnregisterWriter(GetTrace(), writerID);
}

void AMF_CDECL_CALL AMFTraceEnterScope()
{
    GetTrace()->pVtbl->Indent(GetTrace(), 1);
}

amf_uint32 AMF_CDECL_CALL AMFTraceGetScopeDepth()
{
    return GetTrace()->pVtbl->GetIndentation(GetTrace());
}

void AMF_CDECL_CALL AMFTraceExitScope()
{
    GetTrace()->pVtbl->Indent(GetTrace(), -1);
}

void AMF_CDECL_CALL  AMFAssertsEnable(amf_bool enable)
{
    GetDebug()->pVtbl->AssertsEnable(GetDebug(), enable);
}
amf_bool AMF_CDECL_CALL  AMFAssertsEnabled()
{
    return GetDebug()->pVtbl->AssertsEnabled(GetDebug());
}
wchar_t * AMF_CDECL_CALL  AMFFormatResult(AMF_RESULT result) 
{ 
    static const wchar_t *pStr =  L"AMF_ERROR %d : %s: ";
    size_t size = wcslen(pStr);
    const wchar_t *text = GetTrace()->pVtbl->GetResultText(GetTrace(), result);
    size_t sizeText = wcslen(text);
    wchar_t *buf = (wchar_t *)malloc(sizeof(wchar_t) * (size + sizeText + 20 + 1));
    _snwprintf(buf, size + sizeText + 20 + 1, pStr, result, text);
    return buf; 
}

const wchar_t* AMF_STD_CALL AMFGetResultText(AMF_RESULT res)
{
    return GetTrace()->pVtbl->GetResultText(GetTrace(), res);
}
const wchar_t* AMF_STD_CALL AMFSurfaceGetFormatName(const AMF_SURFACE_FORMAT eSurfaceFormat)
{
    return GetTrace()->pVtbl->SurfaceGetFormatName(GetTrace(),eSurfaceFormat);
}
AMF_SURFACE_FORMAT AMF_STD_CALL AMFSurfaceGetFormatByName(const wchar_t* pwName)
{
    return GetTrace()->pVtbl->SurfaceGetFormatByName(GetTrace(),pwName);
}
const wchar_t* const AMF_STD_CALL AMFGetMemoryTypeName(const AMF_MEMORY_TYPE memoryType)
{
    return GetTrace()->pVtbl->GetMemoryTypeName(GetTrace(),memoryType);
}

AMF_MEMORY_TYPE AMF_STD_CALL AMFGetMemoryTypeByName(const wchar_t* name)
{
    return GetTrace()->pVtbl->GetMemoryTypeByName(GetTrace(), name);
}
