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

#include "AMFFactoryC.h"
#include "../../../include/core/Version.h"
#include "ThreadC.h"

struct AMFFactoryHelper
{
    HMODULE        m_hDLLHandle;
    AMFFactory*    m_pFactory;
    AMFDebug*      m_pDebug;
    AMFTrace*      m_pTrace;
    amf_uint64     m_AMFRuntimeVersion;
    amf_long       m_iRefCount;
};

static struct AMFFactoryHelper    g_AMFFactory = {0,0,0,0,0,0};



//-------------------------------------------------------------------------------------------------
AMF_RESULT      AMFFactoryHelper_Init()
{
    if (g_AMFFactory.m_hDLLHandle != NULL)
    {
        amf_atomic_inc(&g_AMFFactory.m_iRefCount);
        return AMF_OK;
    }
    g_AMFFactory.m_hDLLHandle = amf_load_library(AMF_DLL_NAME);
    if(g_AMFFactory.m_hDLLHandle == NULL)
    {
        return AMF_FAIL;
    }

    AMFInit_Fn initFun = (AMFInit_Fn)amf_get_proc_address(g_AMFFactory.m_hDLLHandle, AMF_INIT_FUNCTION_NAME);
    if(initFun == NULL)
    {
        return AMF_FAIL;
    }
    
    AMF_RESULT res = initFun(AMF_FULL_VERSION, &g_AMFFactory.m_pFactory);
    if(res != AMF_OK)
    {
        return res;
    }
    AMFQueryVersion_Fn versionFun = (AMFQueryVersion_Fn)amf_get_proc_address(g_AMFFactory.m_hDLLHandle, AMF_QUERY_VERSION_FUNCTION_NAME);
    if(versionFun == NULL)
    {
        return AMF_FAIL;
    }
    res = versionFun(&g_AMFFactory.m_AMFRuntimeVersion);
    if(res != AMF_OK)
    {
        return res;
    }

    g_AMFFactory.m_pFactory->pVtbl->GetTrace(g_AMFFactory.m_pFactory, &g_AMFFactory.m_pTrace);
    g_AMFFactory.m_pFactory->pVtbl->GetDebug(g_AMFFactory.m_pFactory, &g_AMFFactory.m_pDebug);

    amf_atomic_inc(&g_AMFFactory.m_iRefCount);
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT      AMFFactoryHelper_Terminate()
{
    if(g_AMFFactory.m_hDLLHandle != NULL)
    { 
        amf_atomic_dec(&g_AMFFactory.m_iRefCount);
        if(g_AMFFactory.m_iRefCount == 0)
        { 
            amf_free_library(g_AMFFactory.m_hDLLHandle);
            g_AMFFactory.m_hDLLHandle = NULL;
            g_AMFFactory.m_pFactory= NULL;
            g_AMFFactory.m_pDebug = NULL;
            g_AMFFactory.m_pTrace = NULL;
        }
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMFFactory*     AMFFactoryHelper_GetFactory()
{
    return g_AMFFactory.m_pFactory;
}
//-------------------------------------------------------------------------------------------------
AMFDebug*       AMFFactoryHelper_GetDebug()
{
    return g_AMFFactory.m_pDebug;
}
//-------------------------------------------------------------------------------------------------
AMFTrace*       AMFFactoryHelper_GetTrace()
{
    return g_AMFFactory.m_pTrace;
}
//-------------------------------------------------------------------------------------------------
amf_uint64      AMFFactoryHelper_QueryVersion()
{
    return g_AMFFactory.m_AMFRuntimeVersion;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT      AMFFactoryHelper_LoadExternalComponent(AMFContext* pContext, const wchar_t* dll, const char* function, void* reserved, AMFComponent** ppComponent)
{
    // check passed in parameters
    if (!pContext || !dll || !function)
    { 
        return AMF_INVALID_ARG;
    }
#if 0 //MM TODO
    // check if DLL has already been loaded
    HMODULE  hDll = NULL;
    for (std::vector<ComponentHolder>::iterator it = m_extComponents.begin(); it != m_extComponents.end(); ++it)
    { 
        if (wcsicmp(it->m_DLL.c_str(), dll) == 0)
        {
            if (it->m_hDLLHandle != NULL)
            {
                hDll = it->m_hDLLHandle;
                amf_atomic_inc(&it->m_iRefCount);
                break;
            }

            return AMF_UNEXPECTED;
        }
    }
    // DLL wasn't loaded before so load it now and 
    // add it to the internal list
    if (hDll == NULL)
    {
        ComponentHolder component;
        component.m_iRefCount = 0;
        component.m_hDLLHandle = NULL;
        component.m_DLL = dll;

        hDll = LoadLibraryW(dll);
        if (hDll == NULL)
            return AMF_FAIL;

        // since LoadLibrary succeeded add the information
        // into the internal list so we can properly free
        // the DLL later on, even if we fail to get the 
        // required information from it...
        component.m_hDLLHandle = hDll;
        amf_atomic_inc(&component.m_iRefCount);
        m_extComponents.push_back(component);
    }

    // look for function we want in the dll we just loaded
    typedef AMF_RESULT(AMF_CDECL_CALL *AMFCreateComponentFunc)(amf::AMFContext*, void* reserved, amf::AMFComponent**);
    AMFCreateComponentFunc  initFn = (AMFCreateComponentFunc)::GetProcAddress(hDll, function);
    if (initFn == NULL)
        return AMF_FAIL;

    return initFn(pContext, reserved, ppComponent);
#endif
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT      AMFFactoryHelper_UnLoadExternalComponent(const wchar_t* dll)
{
    if (!dll)
    { 
        return AMF_INVALID_ARG;
    }
#if 0 //MM TODO
    for (std::vector<ComponentHolder>::iterator it = m_extComponents.begin(); it != m_extComponents.end(); ++it)
    { 
        if (wcsicmp(it->m_DLL.c_str(), dll) == 0)
        {
            if (it->m_hDLLHandle == NULL)
            {
                return AMF_UNEXPECTED;
            }
            amf_atomic_dec(&it->m_iRefCount);
            if (it->m_iRefCount == 0)
            { 
                FreeLibrary(it->m_hDLLHandle);
                m_extComponents.erase(it);
            }
            break;
        }
    }
#endif
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

