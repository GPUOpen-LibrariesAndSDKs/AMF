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
///-------------------------------------------------------------------------
///  @file   Trace.h
///  @brief  AMFTrace interface
///-------------------------------------------------------------------------
#pragma once

#include "../../../include/core/Debug.h"
#include "../../../include/core/Trace.h"
#include "../../../include/core/Result.h"

#ifndef WIN32
#include <stdarg.h>
#endif
#include <assert.h>
//-----------------------------------
#include <wchar.h>


#ifndef _MSC_VER
#if defined(__cplusplus)
extern "C"
{
#endif 
#if defined(__cplusplus)
}
#endif

#elif _MSC_VER <= 1911
    #define snprintf _snprintf
    #define vscprintf _vscprintf
    #define vscwprintf _vscwprintf  //  Count chars without writing to string
    #define vswprintf _vsnwprintf
#endif


/**
*******************************************************************************
*   AMFTraceEnableAsync
*
*   @brief
*       Enable or disable async mode
*
*  There are 2 modes trace can work in:
*  Synchronous - every Trace call immediately goes to writers: console, windows, file, ...
*  Asynchronous - trace message go to thread local queues; separate thread passes them to writes
*  Asynchronous mode offers no synchronization between working threads which are writing traces
*  and high performance.
*  Asynchronous mode is not enabled always as that dedicated thread (started in Media SDK module) cannot be 
*  terminated safely. See msdn ExitProcess description: it terminates all threads without notifications.
*  ExitProcess is called after exit from main() -> before module static variables destroyed and before atexit 
*  notifiers are called -> no way to finish trace dedicated thread.
*  
*  Therefore here is direct enable of asynchronous mode.
*  AMFTraceEnableAsync(true) increases internal asynchronous counter by 1; AMFTraceEnableAsync(false) decreases by 1
*  when counter becomes > 0 mode - switches to async; when becomes 0 - switches to sync
*  
*  Tracer must be switched to sync mode before quit application, otherwise async writing thread will be force terminated by OS (at lease Windows)
*  See MSDN ExitProcess article for details.
*******************************************************************************
*/
#if defined(__cplusplus)
extern "C"
{
#endif

AMF_RESULT AMF_CDECL_CALL AMFTraceEnableAsync(amf_bool enable);

/**
*******************************************************************************
*   AMFDebugSetDebugger
*
*   @brief
*       it is used to set a local debugger, or set NULL to remove
*
*******************************************************************************
*/
AMF_RESULT AMF_CDECL_CALL AMFSetCustomDebugger(AMFDebug *pDebugger);

/**
*******************************************************************************
*   AMFTraceSetTracer
*
*   @brief
*       it is used to set a local tracer, or set NULL to remove
*
*******************************************************************************
*/
AMF_RESULT AMF_CDECL_CALL AMFSetCustomTracer(AMFTrace *pTrace);

/**
*******************************************************************************
*   AMFTraceFlush
*
*   @brief
*       Enforce trace writers flush
*
*******************************************************************************
*/
AMF_RESULT AMF_CDECL_CALL AMFTraceFlush();

/**
*******************************************************************************
*   EXPAND
*
*   @brief
*       Auxilary Macro used to evaluate __VA_ARGS__ from 1 macro argument into list of them
*
*   It is needed for COUNT_ARGS macro
*
*******************************************************************************
*/
#define EXPAND(x) x

/**
*******************************************************************************
*   GET_TENTH_ARG
*
*   @brief
*       Auxilary Macro for COUNT_ARGS macro
*
*******************************************************************************
*/
#define GET_TENTH_ARG(a, b, c, d, e, f, g, h, i, j, name, ...) name

/**
*******************************************************************************
*   COUNT_ARGS
*
*   @brief
*       Macro returns number of arguments actually passed into it
*
*   COUNT_ARGS macro works ok for 1..10 arguments
*   It is needed to distinguish macro call with optional parameters and without them
*******************************************************************************
*/
#define COUNT_ARGS(...) EXPAND(GET_TENTH_ARG(__VA_ARGS__, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1))

/**
*******************************************************************************
*   AMFTraceW
*
*   @brief
*       General trace function with all possible parameters
*******************************************************************************
*/
void AMF_CDECL_CALL AMFTraceW(const wchar_t* src_path, amf_int32 line, amf_int32 level, const wchar_t* scope,
        amf_int32 countArgs, const wchar_t* format, ...);

/**
*******************************************************************************
*   AMF_UNICODE
*
*   @brief
*       Macro to convert string constant into wide char string constant
*
*   Auxilary AMF_UNICODE_ macro is needed as otherwise it is not possible to use AMF_UNICODE(__FILE__)
*   Microsoft macro _T also uses 2 passes to accomplish that
*******************************************************************************
*/
#define AMF_UNICODE(s) AMF_UNICODE_(s)
#define AMF_UNICODE_(s) L ## s

/**
*******************************************************************************
*   AMFTrace
*
*   @brief
*       Most general macro for trace, incapsulates passing source file and line
*******************************************************************************
*/
#define AMFTrace(level, scope, /*format, */...) AMFTraceW(AMF_UNICODE(__FILE__), __LINE__, level, scope, COUNT_ARGS(__VA_ARGS__) - 1, __VA_ARGS__)

/**
*******************************************************************************
*   AMFTraceError
*
*   @brief
*       Shortened macro to trace exactly error.
*
*   Similar macroses are: AMFTraceWarning, AMFTraceInfo, AMFTraceDebug
*******************************************************************************
*/
#define AMFTraceError(scope, /*format, */...)   AMFTraceW(AMF_UNICODE(__FILE__), __LINE__, AMF_TRACE_ERROR, scope, COUNT_ARGS(__VA_ARGS__) - 1, __VA_ARGS__)
#define AMFTraceWarning(scope, /*format, */...) AMFTraceW(AMF_UNICODE(__FILE__), __LINE__, AMF_TRACE_WARNING, scope, COUNT_ARGS(__VA_ARGS__) - 1, __VA_ARGS__)
#define AMFTraceInfo(scope, /*format, */...)    AMFTraceW(AMF_UNICODE(__FILE__), __LINE__, AMF_TRACE_INFO, scope, COUNT_ARGS(__VA_ARGS__) - 1, __VA_ARGS__)
#define AMFTraceDebug(scope, /*format, */...)   AMFTraceW(AMF_UNICODE(__FILE__), __LINE__, AMF_TRACE_DEBUG, scope, COUNT_ARGS(__VA_ARGS__) - 1, __VA_ARGS__)

/**
*******************************************************************************
*   AMFDebugHitEvent
*
*   @brief
*       Designed to determine how many are specific events take place
*******************************************************************************
*/
void      AMF_CDECL_CALL AMFDebugHitEvent(const wchar_t* scope, const wchar_t* eventName);
/**
*******************************************************************************
*   AMFDebugGetEventsCount
*
*   @brief
*       Designed to acquire counter of events reported by call AMFDebugHitEvent
*******************************************************************************
*/
amf_int64 AMF_CDECL_CALL AMFDebugGetEventsCount(const wchar_t* scope, const wchar_t* eventName);

/**
*******************************************************************************
*   AMFAssertsEnabled
*
*   @brief
*       Returns amf_bool values indicating if asserts were enabled or not
*******************************************************************************
*/
amf_bool AMF_CDECL_CALL AMFAssertsEnabled();

/**
*******************************************************************************
*   AMFTraceEnterScope
*
*   @brief
*       Increase trace indentation value by 1
*
*   Indentation value is thread specific
*******************************************************************************
*/
void AMF_CDECL_CALL AMFTraceEnterScope(); 
/**
*******************************************************************************
*   AMFTraceExitScope
*
*   @brief
*       Decrease trace indentation value by 1
*
*   Indentation value is thread specific
*******************************************************************************
*/
void AMF_CDECL_CALL AMFTraceExitScope();

/**
*******************************************************************************
*   AMF_FACILITY
*
*   @brief
*       Default value for AMF_FACILITY, this NULL leads to generate facility from source file name
*
*   This AMF_FACILITY could be overloaded locally with #define AMF_FACILITY L"LocalScope"
*******************************************************************************
*/
static const wchar_t* AMF_FACILITY = NULL;
#if defined(__cplusplus)
} //extern "C"
#endif
/**
*******************************************************************************
*   AMFDebugBreak
*
*   @brief
*       Macro for switching to debug of application
*******************************************************************************
*/
#if defined(_DEBUG)
#if defined(_WIN32)
#define AMFDebugBreak  {if(AMFAssertsEnabled()) {__debugbreak();} \
}                                                                                //{  }
#elif defined(__linux)
//    #define AMFDebugBreak ((void)0)
#define AMFDebugBreak  {if(AMFAssertsEnabled()) {assert(0);} \
}                                                                           //{  }
#endif
#else
#define AMFDebugBreak 
#endif

/**
*******************************************************************************
*   AMF_FIRST_VALUE
*
*   @brief
*       Auxilary macro: extracts first argument from the list
*******************************************************************************
*/
#define AMF_FIRST_VALUE(x, ...) x

/**
*******************************************************************************
*   AMF_BASE_RETURN
*
*   @brief
*       Base generic macro: checks expression for success, if failed: trace error, debug break and return an error
*
*       return_result is a parameter to return to upper level, could be hard-coded or 
*           specified exp_res what means pass inner level error
*******************************************************************************
*/

AMF_INLINE wchar_t* __FormatMessage(int argsCount, const wchar_t* prefix, const wchar_t *expression, const wchar_t* message, ...)
{
    expression;
    // this version of __FormatMessage for case when descriptive message is provided with optional args
    int prefixSize = 0;
    int size = 0;
    wchar_t *buf = NULL;
    if(prefix != NULL)
    {
        prefixSize =  (int)wcslen(prefix);
    }
    if(argsCount <= 0)
    {
        size = (int)wcslen(message);
        buf = (wchar_t *)malloc(sizeof(wchar_t) * (size + prefixSize + 1));
        if(prefix != NULL)
        {
            wcscpy(buf, prefix);
        }
        wcscpy(buf + prefixSize, message);
    }
    else
    {
        va_list arglist;
        va_start(arglist, message);
        size = (int)vscwprintf(message, arglist);
        buf = (wchar_t *)malloc(sizeof(wchar_t) * (size + prefixSize + 1));
        if(prefix != NULL)
        {
            wcscpy(buf, prefix);
        }
        vswprintf(buf + prefixSize, size + 1, message, arglist);
        va_end(arglist);
    }
    return buf;
}


#ifdef _WIN32
//#define    amf_va_copy(x, y) x = y
#define    amf_va_copy(x, y) va_copy(x, y)

#else 
#define    amf_va_copy(x, y) va_copy(x, y)
#endif
//            amf_va_copy(argcopy, __VA_ARGS__); \

#define AMF_BASE_RETURN(exp, exp_type, check_func, format_prefix, level, scope, return_result/*(could be exp_res)*/, /* optional message args*/ ...) \
    { \
        exp_type exp_res = (exp_type)(exp); \
        if(!check_func(exp_res)) \
        { \
            wchar_t *prefix = format_prefix(exp_res); \
            wchar_t * message = __FormatMessage(COUNT_ARGS(__VA_ARGS__) - 2, prefix, __VA_ARGS__); \
            EXPAND(AMFTraceW(AMF_UNICODE(__FILE__), __LINE__, level, scope, 0, message) ); \
            free(message); \
            if(prefix != NULL) \
            {  \
                free(prefix); \
            } \
            AMFDebugBreak; \
            return return_result; \
        } \
    }

/**
*******************************************************************************
*   AMF_BASE_ASSERT
*
*   @brief
*       Base generic macro: checks expression for success, if failed: trace error, debug break
*******************************************************************************
*/
#define AMF_BASE_ASSERT(exp, exp_type, check_func, format_prefix, level, scope, return_result/*(could be exp_res)*/, /*optional message, optional message args*/ ...) \
    { \
        exp_type exp_res = (exp_type)(exp); \
        if(!check_func(exp_res)) \
        { \
            wchar_t *prefix = format_prefix(exp_res); \
            wchar_t * message = __FormatMessage(COUNT_ARGS(__VA_ARGS__) - 2, prefix, __VA_ARGS__); \
            EXPAND(AMFTraceW(AMF_UNICODE(__FILE__), __LINE__, level, scope, 0, message) ); \
            free(message); \
            if(prefix != NULL) \
            {  \
                free(prefix); \
            } \
            AMFDebugBreak; \
        } \
    } 

/**
*******************************************************************************
*   AMFCheckExpression
*
*   @brief
*       Checks if result succeeds
*******************************************************************************
*/
AMF_INLINE amf_bool AMFCheckExpression(int result) { return result != 0; }
/**
*******************************************************************************
*   AMFFormatAssert
*
*   @brief
*       Returns default assertion message
*******************************************************************************
*/
AMF_INLINE wchar_t *AMFFormatAssert(int result) 
{ 
    if(result)
        return NULL;
    static const wchar_t *pStr =  L"Assertion failed:";
    size_t size = wcslen(pStr);
    wchar_t *buf = (wchar_t *)malloc(sizeof(wchar_t) * (size + 1));
    wcscpy(buf, pStr);
    return buf; 
}

/**
*******************************************************************************
*   AMFOpenCLSucceeded
*
*   @brief
*       Checks cl_status for success
*******************************************************************************
*/
AMF_INLINE amf_bool AMFOpenCLSucceeded(int result) { return result == 0; }
/**
*******************************************************************************
*   AMFFormatOpenCLError
*
*   @brief
*       Formats open CL error
*******************************************************************************
*/
AMF_INLINE wchar_t *AMFFormatOpenCLError(int result)  
{ 
    static const wchar_t *pStr =  L"OpenCL failed, error = %d:";
    size_t size = wcslen(pStr);
    wchar_t *buf = (wchar_t *)malloc(sizeof(wchar_t) * (size + 20 + 1));
    _snwprintf(buf, size + 20, pStr, result);
    return buf; 
}

/**
*******************************************************************************
*   AMFResultIsOK
*
*   @brief
*       Checks if AMF_RESULT is OK
*******************************************************************************
*/
AMF_INLINE amf_bool AMFResultIsOK(AMF_RESULT result) { return result == AMF_OK; }
/**
*******************************************************************************
*   AMFSucceeded
*
*   @brief
*       Checks if AMF_RESULT is succeeded
*******************************************************************************
*/
AMF_INLINE amf_bool AMFSucceeded(AMF_RESULT result) { return result == AMF_OK || result == AMF_REPEAT; }
/**
*******************************************************************************
*   AMFFormatResult
*
*   @brief
*       Formats AMF_RESULT into descriptive string
*******************************************************************************
*/
wchar_t * AMF_CDECL_CALL  AMFFormatResult(AMF_RESULT result);

#if defined(_WIN32)
/**
*******************************************************************************
*   AMFHResultSucceded
*
*   @brief
*       Checks if HRESULT succeeded
*******************************************************************************
*/
AMF_INLINE amf_bool AMFHResultSucceded(HRESULT result) { return SUCCEEDED(result); }
/**
*******************************************************************************
*   AMFFormatHResult
*
*   @brief
*       Formats HRESULT into descriptive string
*******************************************************************************
*/
AMF_INLINE wchar_t *AMFFormatHResult(HRESULT result)  
{ 
    static const wchar_t *pStr =  L"COM failed, HR = %0X:";
    size_t size = wcslen(pStr);
    wchar_t *buf = (wchar_t *)malloc(sizeof(wchar_t) * (size + 20 + 1));
    _snwprintf(buf, size + 20 + 1, pStr, result);
    return buf; 
}
#endif //defined(_WIN32)
/**
*******************************************************************************
*   AMF_ASSERT_OK
*
*   @brief
*       Checks expression == AMF_OK, otherwise trace error and debug break
*
*       Could be used: A) with just expression B) with optinal descriptive message C) message + args for printf
*******************************************************************************
*/
#define AMF_ASSERT_OK(exp, ... /*optional format, args*/) AMF_BASE_ASSERT(exp, AMF_RESULT, AMFResultIsOK, AMFFormatResult, AMF_TRACE_ERROR, AMF_FACILITY, AMF_FAIL, L###exp, ##__VA_ARGS__)

/**
*******************************************************************************
*   AMF_ASSERT
*
*   @brief
*       Checks expression != 0, otherwise trace error and debug break
*
*       Could be used: A) with just expression B) with optinal descriptive message C) message + args for printf
*******************************************************************************
*/
#define AMF_ASSERT(exp, ...) AMF_BASE_ASSERT(exp, int, AMFCheckExpression, AMFFormatAssert, AMF_TRACE_ERROR, AMF_FACILITY, AMF_FAIL, L###exp, ##__VA_ARGS__)

/**
*******************************************************************************
*   AMF_RETURN_IF_FAILED
*
*   @brief
*       Checks expression != 0, otherwise trace error, debug break and return that error to upper level
*
*       Could be used: A) with just expression B) with optinal descriptive message C) message + args for printf
*******************************************************************************
*/
#define AMF_RETURN_IF_FAILED(exp, ...) AMF_BASE_RETURN(exp, AMF_RESULT, AMFResultIsOK, AMFFormatResult, AMF_TRACE_ERROR, AMF_FACILITY, exp_res, L###exp, ##__VA_ARGS__)

/**
*******************************************************************************
*   ASSERT_RETURN_IF_CL_FAILED
*
*   @brief
*       Checks cl error is ok, otherwise trace error, debug break and return that error to upper level
*
*       Could be used: A) with just expression B) with optinal descriptive message C) message + args for printf
*******************************************************************************
*/
#define ASSERT_RETURN_IF_CL_FAILED(exp, /*optional format, args,*/...) AMF_BASE_RETURN(exp, int, AMFOpenCLSucceeded, AMFFormatOpenCLError, AMF_TRACE_ERROR, AMF_FACILITY, AMF_OPENCL_FAILED, L###exp, ##__VA_ARGS__)
#define AMF_RETURN_IF_CL_FAILED(exp, /*optional format, args,*/...) AMF_BASE_RETURN(exp, int, AMFOpenCLSucceeded, AMFFormatOpenCLError, AMF_TRACE_ERROR, AMF_FACILITY, AMF_OPENCL_FAILED, L###exp, ##__VA_ARGS__)

/**
*******************************************************************************
*   ASSERT_RETURN_IF_HR_FAILED
*
*   @brief
*       Obsolete macro: Checks HRESULT if succeeded, otherwise trace error, debug break and return specified error to upper level
*
*       Other macroses below are also obsolete
*******************************************************************************
*/
#if defined(_WIN32)
#define ASSERT_RETURN_IF_HR_FAILED(exp, reterr, /*optional format, args,*/...) AMF_BASE_RETURN(exp, HRESULT, AMFHResultSucceded, AMFFormatHResult, AMF_TRACE_ERROR, AMF_FACILITY, reterr, L###exp, ##__VA_ARGS__)
#endif

/**
*******************************************************************************
*   AMF_RETURN_IF_FALSE
*
*   @brief
*       Checks expression != 0, otherwise trace error, debug break and return that error to upper level
*
*       Could be used: A) with just expression B) with optinal descriptive message C) message + args for printf
*******************************************************************************
*/
#define AMF_RETURN_IF_FALSE(exp, ret_value, /*optional message,*/ ...) AMF_BASE_RETURN(exp, int, AMFCheckExpression, AMFFormatAssert, AMF_TRACE_ERROR, AMF_FACILITY, ret_value, L###exp, ##__VA_ARGS__)

/**
*******************************************************************************
*   AMF_RETURN_IF_INVALID_POINTER
*
*   @brief
*       Checks ptr != NULL, otherwise trace error, debug break and return that error to upper level
*
*******************************************************************************
*/
#define AMF_RETURN_IF_INVALID_POINTER(ptr, /*optional message,*/ ...) AMF_BASE_RETURN(ptr != NULL, int, AMFCheckExpression, AMFFormatAssert, AMF_TRACE_ERROR, AMF_FACILITY, AMF_INVALID_POINTER, L"invalid pointer : " L###ptr, ##__VA_ARGS__)

/**
*******************************************************************************
*   AMFTestEventObserver
*
*   @brief
*       Interface to subscribe on test events
*******************************************************************************
*/

#if defined(__cplusplus)
extern "C"
{
#endif

    /**
    *******************************************************************************
    *   AMFTraceSetPath
    *
    *   @brief
    *       Set Trace path
    *
    *       Returns AMF_OK if succeeded
    *******************************************************************************
    */
    AMF_RESULT AMF_CDECL_CALL  AMFTraceSetPath(const wchar_t* path);

    /**
    *******************************************************************************
    *   AMFTraceGetPath
    *
    *   @brief
    *       Get Trace path
    *
    *       Returns AMF_OK if succeeded
    *******************************************************************************
    */
    AMF_RESULT AMF_CDECL_CALL  AMFTraceGetPath(
        wchar_t* path, ///< [out] buffer able to hold *pSize symbols; path is copied there, at least part fitting the buffer, always terminator is copied
        amf_size* pSize ///< [in, out] size of buffer, returned needed size of buffer including zero terminator
    );

    /**
    *******************************************************************************
    *   AMFTraceEnableWriter
    *
    *   @brief
    *       Disable trace to registered writer
    *
    *       Returns previous state
    *******************************************************************************
    */
    amf_bool AMF_CDECL_CALL  AMFTraceEnableWriter(const wchar_t* writerID, amf_bool enable);

    /**
    *******************************************************************************
    *   AMFTraceWriterEnabled
    *
    *   @brief
    *       Return flag if writer enabled
    *******************************************************************************
    */
    amf_bool AMF_CDECL_CALL  AMFTraceWriterEnabled(const wchar_t* writerID);

    /**
    *******************************************************************************
    *   AMFTraceSetGlobalLevel
    *
    *   @brief
    *       Sets trace level for writer and scope
    *
    *       Returns previous setting
    *******************************************************************************
    */
    amf_int32 AMF_CDECL_CALL  AMFTraceSetGlobalLevel(amf_int32 level); 

    /**
    *******************************************************************************
    *   AMFTraceGetGlobalLevel
    *
    *   @brief
    *       Returns global level
    *******************************************************************************
    */
    amf_int32 AMF_CDECL_CALL  AMFTraceGetGlobalLevel();

    /**
    *******************************************************************************
    *   AMFTraceSetWriterLevel
    *
    *   @brief
    *       Sets trace level for writer 
    *
    *       Returns previous setting
    *******************************************************************************
    */
    amf_int32 AMF_CDECL_CALL  AMFTraceSetWriterLevel(const wchar_t* writerID, amf_int32 level); 

    /**
    *******************************************************************************
    *   AMFTraceGetWriterLevel
    *
    *   @brief
    *       Gets trace level for writer 
    *******************************************************************************
    */
    amf_int32 AMF_CDECL_CALL  AMFTraceGetWriterLevel(const wchar_t* writerID); 

    /**
    *******************************************************************************
    *   AMFTraceSetWriterLevelForScope
    *
    *   @brief
    *       Sets trace level for writer and scope
    *
    *       Returns previous setting
    *******************************************************************************
    */
    amf_int32 AMF_CDECL_CALL  AMFTraceSetWriterLevelForScope(const wchar_t* writerID, const wchar_t* scope, amf_int32 level); 

    /**
    *******************************************************************************
    *   AMFTraceGetWriterLevelForScope
    *
    *   @brief
    *       Gets trace level for writer and scope
    *******************************************************************************
    */
    amf_int32 AMF_CDECL_CALL  AMFTraceGetWriterLevelForScope(const wchar_t* writerID, const wchar_t* scope); 

    /**
    *******************************************************************************
    *   AMFTraceRegisterWriter
    *
    *   @brief
    *       Register custom trace writer
    *
    *******************************************************************************
    */
    void AMF_CDECL_CALL  AMFTraceRegisterWriter(const wchar_t* writerID, AMFTraceWriter* pWriter);

    /**
    *******************************************************************************
    *   AMFTraceUnregisterWriter
    *
    *   @brief
    *       Register custom trace writer
    *
    *******************************************************************************
    */
    void AMF_CDECL_CALL  AMFTraceUnregisterWriter(const wchar_t* writerID);
    /*
    *******************************************************************************
    *   AMFAssertsEnable
    *
    *   @brief
    *       Enable asserts in checks
    *
    *******************************************************************************
    */
    void AMF_CDECL_CALL  AMFAssertsEnable(amf_bool enable);

    /**
    *******************************************************************************
    *   AMFAssertsEnabled
    *
    *   @brief
    *       Returns true if asserts in checks enabled
    *
    *******************************************************************************
    */
    amf_bool AMF_CDECL_CALL  AMFAssertsEnabled();

    const wchar_t* AMF_STD_CALL AMFGetResultText(AMF_RESULT res);
    const wchar_t* AMF_STD_CALL AMFSurfaceGetFormatName(const AMF_SURFACE_FORMAT eSurfaceFormat);
    AMF_SURFACE_FORMAT AMF_STD_CALL AMFSurfaceGetFormatByName(const wchar_t* pwName);
    const wchar_t* const AMF_STD_CALL AMFGetMemoryTypeName(const AMF_MEMORY_TYPE memoryType);
    AMF_MEMORY_TYPE AMF_STD_CALL AMFGetMemoryTypeByName(const wchar_t* name);

#if defined(__cplusplus)
} //extern "C"
#endif 

