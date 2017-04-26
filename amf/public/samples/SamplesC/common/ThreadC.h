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

#ifndef __AMFThreadC_h__
#define __AMFThreadC_h__
#pragma once


#include "../../../include/core/Platform.h"

#ifndef _WIN32
#include <pthread.h>
#endif

    // threads
    #define AMF_INFINITE        (0xFFFFFFFF) // Infinite ulTimeout

    // threads: atomic
    amf_long    AMF_CDECL_CALL amf_atomic_inc(amf_long* X);
    amf_long    AMF_CDECL_CALL amf_atomic_dec(amf_long* X);

    // threads: critical section
    amf_handle  AMF_CDECL_CALL amf_create_critical_section();
    amf_bool        AMF_CDECL_CALL amf_delete_critical_section(amf_handle cs);
    amf_bool        AMF_CDECL_CALL amf_enter_critical_section(amf_handle cs);
    amf_bool        AMF_CDECL_CALL amf_leave_critical_section(amf_handle cs);
    // threads: event
    amf_handle  AMF_CDECL_CALL amf_create_event(amf_bool bInitiallyOwned, amf_bool bManualReset, const wchar_t* pName);
    amf_bool        AMF_CDECL_CALL amf_delete_event(amf_handle hevent);
    amf_bool        AMF_CDECL_CALL amf_set_event(amf_handle hevent);
    amf_bool        AMF_CDECL_CALL amf_reset_event(amf_handle hevent);
    amf_bool        AMF_CDECL_CALL amf_wait_for_event(amf_handle hevent, amf_ulong ulTimeout);
    amf_bool        AMF_CDECL_CALL amf_wait_for_event_timeout(amf_handle hevent, amf_ulong ulTimeout);

    // threads: mutex
    amf_handle  AMF_CDECL_CALL amf_create_mutex(amf_bool bInitiallyOwned, const wchar_t* pName);
#if defined(_WIN32)
    amf_handle  AMF_CDECL_CALL amf_open_mutex(const wchar_t* pName);
#endif
    amf_bool        AMF_CDECL_CALL amf_delete_mutex(amf_handle hmutex);
    amf_bool        AMF_CDECL_CALL amf_wait_for_mutex(amf_handle hmutex, amf_ulong ulTimeout);
    amf_bool        AMF_CDECL_CALL amf_release_mutex(amf_handle hmutex);

    // threads: semaphore
    amf_handle  AMF_CDECL_CALL amf_create_semaphore(amf_long iInitCount, amf_long iMaxCount, const wchar_t* pName);
    amf_bool        AMF_CDECL_CALL amf_delete_semaphore(amf_handle hsemaphore);
    amf_bool        AMF_CDECL_CALL amf_wait_for_semaphore(amf_handle hsemaphore, amf_ulong ulTimeout);
    amf_bool        AMF_CDECL_CALL amf_release_semaphore(amf_handle hsemaphore, amf_long iCount, amf_long* iOldCount);

    // threads: delay
    void        AMF_CDECL_CALL amf_sleep(amf_ulong delay);
    amf_pts     AMF_CDECL_CALL amf_high_precision_clock();    // in 100 of nanosec

    void        AMF_CDECL_CALL amf_increase_timer_precision();
    void        AMF_CDECL_CALL amf_restore_timer_precision();

    amf_handle  AMF_CDECL_CALL amf_load_library(const wchar_t* filename);
    void*       AMF_CDECL_CALL amf_get_proc_address(amf_handle module, const char* procName);
    int         AMF_CDECL_CALL amf_free_library(amf_handle module);

#if !defined(METRO_APP)
    // virtual memory
    void*       AMF_CDECL_CALL amf_virtual_alloc(amf_size size);
    void        AMF_CDECL_CALL amf_virtual_free(void* ptr);
#else
    #define amf_virtual_alloc amf_alloc
    #define amf_virtual_free amf_free
#endif


#endif // __AMFThreadC_h__
