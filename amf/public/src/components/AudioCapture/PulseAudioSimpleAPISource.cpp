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
// Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.
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

#include "PulseAudioSimpleAPISource.h"
#include "../../../include/core/AudioBuffer.h"
#include "../../../common/TraceAdapter.h"

#define AMF_FACILITY L"AMFPulseAudioSimpleAPISourceImpl"

using namespace amf;

// Callback functions will need a main loop pointer to terminate the mainloop.
// This struct also enable callback function to return AMF_RESULT and string messages.
class PACallbackData {
    bool                                m_GetSourceListDone = false;
    bool                                m_GetSinkListDone = false;
    bool                                m_GetDisplaySourceDone = false;
        // Any read/write to above fields need to be protected by m_sync.
    mutable AMFCriticalSection          m_sync;

public:

    AMFPulseAudioSimpleAPISourceImpl*   m_APIPtr = nullptr;
    pa_mainloop*                        m_pPaMainLoop = nullptr;
    AMF_RESULT                          m_Result = AMF_OK;
    PulseAudioImportTable*              m_pa = nullptr;

    void QuitIfAllDone()
    {
        AMFLock lock(&m_sync);
        if (true == m_GetSourceListDone && true == m_GetSinkListDone && true == m_GetDisplaySourceDone
            && nullptr != m_pPaMainLoop)
        {
            m_pa->m_pPA_Mainloop_Quit(m_pPaMainLoop, 0);
        }
    }

    void QuitWithError(AMF_RESULT result)
    {
        AMFLock lock(&m_sync);
        m_Result = result;
        if (nullptr != m_pPaMainLoop)
        {
            m_pa->m_pPA_Mainloop_Quit(m_pPaMainLoop, -1);
        }
    }

    void SetSourceListDone()
    {
        AMFLock lock(&m_sync);
        m_GetSourceListDone = true;
    }

    void SetSinkListDone()
    {
        AMFLock lock(&m_sync);
        m_GetSinkListDone = true;
    }

    void SetDisplaySourceDone()
    {
        AMFLock lock(&m_sync);
        m_GetDisplaySourceDone = true;
    }

};


typedef std::unique_ptr<pa_mainloop, decltype(&pa_mainloop_free)>    PAMainLoopPtr;
typedef std::unique_ptr<pa_context, decltype(&pa_context_unref)>     PAContextPtr;
typedef std::unique_ptr<pa_operation, decltype(&pa_operation_unref)> PAOperationPtr;

//-------------------------------------------------------------------------------------------------
const char* PaContextStateToStr(pa_context_state_t state)
{
    switch(state)
    {
        case PA_CONTEXT_UNCONNECTED:  return "PA_CONTEXT_UNCONNECTED";
        case PA_CONTEXT_CONNECTING:   return "PA_CONTEXT_CONNECTING";
        case PA_CONTEXT_AUTHORIZING:  return "PA_CONTEXT_AUTHORIZING";
        case PA_CONTEXT_SETTING_NAME: return "PA_CONTEXT_SETTING_NAME";
        case PA_CONTEXT_READY:        return "PA_CONTEXT_READY";
        case PA_CONTEXT_FAILED:       return "PA_CONTEXT_FAILED";
        case PA_CONTEXT_TERMINATED:   return "PA_CONTEXT_TERMINATED";
        default:                      return "Not a pa_context_state_t!";
    }
}

//-------------------------------------------------------------------------------------------------
void GetSinkListCallback(pa_context* c,const pa_sink_info*i, int eol, void* userdata)
{
    PACallbackData* cbData = (PACallbackData*)userdata;

    if(eol)
    {
        cbData->SetSinkListDone();
        cbData->QuitIfAllDone();
        return;
    }

    // Record the sink name.
    amf_string sinkName = i->name;
    cbData->m_APIPtr->AddToSinkList(sinkName);

    // Record the monitor source.
    amf_string sourceName = i->monitor_source_name;
    cbData->m_APIPtr->AddToSinkMonitorList(sourceName);
}

//-------------------------------------------------------------------------------------------------
void GetSourceListCallback(pa_context* c, const pa_source_info* i, int eol, void* userdata)
{
    PACallbackData* cbData = (PACallbackData*)userdata;

    // If reached end of line, set the doen flag and quit if all done.
    if(eol)
    {
        cbData->SetSourceListDone();
        cbData->QuitIfAllDone();
        return;
    }

    // Record the source name if it's not a monitor of a sink.
    if (NULL == i->monitor_of_sink_name)
    {
        amf_string sourceName = i->name;
        cbData->m_APIPtr->AddToSourceList(sourceName);
    }
}

//-------------------------------------------------------------------------------------------------
void GetDisplaySourceCallback(pa_context* c, const pa_sink_info* i, int eol, void* userdata)
{
    PACallbackData* cbData = (PACallbackData*)userdata;

    // If reached end of line, set the done flag and quit if all done.
    if(eol)
    {
        cbData->SetDisplaySourceDone();
        cbData->QuitIfAllDone();
        return;
    }

    // Record the default sink's monitor's name.
    amf_string sourceName = i->monitor_source_name;
    cbData->m_APIPtr->SetDefaultSinkMonitor(sourceName);
}

//-------------------------------------------------------------------------------------------------
void GetPAServerInfoCallback(pa_context*c,const pa_server_info *i, void* userdata)
{
    PACallbackData* cbData = (PACallbackData*)userdata;

    // Check if it doesn't have a default sink.
    if(i->default_sink_name == nullptr && i->default_source_name == nullptr)
    {
        cbData->QuitWithError(AMF_FAIL);
        AMFTraceError(AMF_FACILITY, L"AMFPulseAudioSimpleAPISourceImpl::GetPAServerInfoCallback(): Cannot find a default sink nor a default source!");
        return;
    }

    // Record default source and sink.
    amf_string defaultSrc = i->default_source_name;
    cbData->m_APIPtr->SetDefaultSource(defaultSrc);

    // Get default sink info
    PAOperationPtr op = PAOperationPtr(cbData->m_pa->m_pPA_Context_Get_Sink_Info_By_Name(c, i->default_sink_name, GetDisplaySourceCallback,userdata), cbData->m_pa->m_pPA_Operation_Unref);
    if (nullptr == op)
    {
        // Terminate the main loop, give it a negative return code.
        cbData->QuitWithError(AMF_FAIL);
        AMFTraceError(AMF_FACILITY, L"AMFPulseAudioSimpleAPISourceImpl::GetPAServerInfoCallback(): pa_context_get_sink_info_by_name() failed!");
        return;
    }

    // Get all sink info.
    op = PAOperationPtr(cbData->m_pa->m_pPA_Context_Get_Sink_Info_List(c, GetSinkListCallback, userdata), cbData->m_pa->m_pPA_Operation_Unref);
    // Only prints warning and do not terminate the main loop.
    if (nullptr == op)
    {
        AMFTraceWarning(AMF_FACILITY, L"AMFPulseAudioSimpleAPISourceImpl::GetPAServerInfoCallback(): pa_contgext_get_sink_info_list() failed!");
    }

    // Get all source info.
    op = PAOperationPtr(cbData->m_pa->m_pPA_Context_Get_Source_Info_List(c, GetSourceListCallback, userdata), cbData->m_pa->m_pPA_Operation_Unref);
    if (nullptr == op)
    {
        AMFTraceWarning(AMF_FACILITY, L"AMFPulseAudioSimpleAPISourceImpl::GetPAServerInfoCallback(): pa_contgext_get_source_info_list() failed!");
    }
}

//-------------------------------------------------------------------------------------------------
void PAContextStateCallback(pa_context *c, void *userdata)
{

    PACallbackData* cbData = (PACallbackData*)userdata;
    pa_context_state_t state = cbData->m_pa->m_pPA_Context_Get_State(c);
    switch(state)
    {
        case PA_CONTEXT_READY:
        {
            PAOperationPtr op = PAOperationPtr(cbData->m_pa->m_pPA_Context_Get_Server_Info(c,GetPAServerInfoCallback,userdata), cbData->m_pa->m_pPA_Operation_Unref);
            // Try to get server info. The callback function will get the data.
            if (nullptr == op)
            {
                // If operation is NULL, terminate the main loop, give it a negative return code.
                AMFTraceError(AMF_FACILITY, L"AMFPulseAudioSimpleAPISourceImpl::PAContextStateCallback(): pa_context_get_server_info() failed!");
                cbData->QuitWithError(AMF_FAIL);
            }
            break;
        }
        case PA_CONTEXT_FAILED:
        {
            AMFTraceError(AMF_FACILITY, L"AMFPulseAudioSimpleAPISourceImpl::PAContextStateCallback(): Failed to connect!\n");
            cbData->QuitWithError(AMF_FAIL);
            break;
        }

        // For other states, instead of cases for each state, simply print the state with AMFTraceDebug.
        default:
        {
            AMFTraceDebug(AMF_FACILITY, L"PAContextStateCallback(): %S.",PaContextStateToStr(state));
            break;
        }
     }
}

// AMFPulseAudioSimpleAPISourceImpl
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFPulseAudioSimpleAPISourceImpl::InitDeviceNames()
{
    AMF_RESULT res = AMF_FAIL;
    // Init the pulse audio main loop.
    PAMainLoopPtr       paMainLoop = PAMainLoopPtr(m_pa.m_pPA_Mainloop_New(),m_pa.m_pPA_Mainloop_Free);
    AMF_RETURN_IF_INVALID_POINTER(paMainLoop, L"paMainLoop is null!");

    pa_mainloop_api*    paMainLoopAPI = m_pa.m_pPA_Mainloop_Get_API(paMainLoop.get());
    AMF_RETURN_IF_INVALID_POINTER(paMainLoopAPI, L"paMainLoopAPI is null!");

    PAContextPtr        paContext = PAContextPtr(m_pa.m_pPA_Context_New(paMainLoopAPI, "AMFPulseAudioSimpleAPISourceImpl:InitDeviceNames()"),m_pa.m_pPA_Context_Unref);
    AMF_RETURN_IF_INVALID_POINTER(paContext, L"paContext is null!");

    int mlErrCode = 0, mlReturnCode = 0;

    // Init a PACallbackData to carry necessary info to and from layers of callbacks.
    PACallbackData callbackData;
    // Set the API pointer field to this.
    callbackData.m_APIPtr = this;
    callbackData.m_pPaMainLoop = paMainLoop.get();
    callbackData.m_pa = &m_pa;


    // Set state callback, when context is ready, the callback sets the
    // server info callback and ask for server info, server info callback
    // retreives default sink(we expect it to be monitor or headphone)
    // then uses the sink to get corresponding loopback output (the source, what we want to capture).
    // When connection failed, callbackData.m_Result will be AMF_FAIL.
    m_pa.m_pPA_Context_Set_State_Callback(paContext.get(), PAContextStateCallback,(void*)(&callbackData));

    // Check if context state has failures.
    res = callbackData.m_Result;
    if (AMF_FAIL == res)
    {
        return res;
    }

    // Connect to pulse audio server. NOAUTOSPAWN prevents puslse audio to auto spawn an unwatned server.
    mlErrCode = m_pa.m_pPA_Context_Connect(paContext.get(),NULL,PA_CONTEXT_NOAUTOSPAWN, NULL);
    if (0 != mlErrCode)
    {
        AMF_RETURN_IF_FAILED(res,L"AMFPulseAudioSimpleAPISourceImpl::pa_context_connect() failed with: %S",m_pa.m_pPA_Strerror(mlErrCode));
    }

    // Run the main loop.
    if ((mlReturnCode = m_pa.m_pPA_Mainloop_Run(paMainLoop.get(), &mlErrCode)) < 0)
    {
        res = AMF_FAIL;
    } else
    {
        res = callbackData.m_Result;
    }

    // Disconnect the context after everything's done.
    m_pa.m_pPA_Context_Disconnect(paContext.get());

    // Need to add a trace error here.
    AMF_RETURN_IF_FAILED(res,L"AMFPulseAudioSimpleAPISourceImpl::InitDeviceNames() failed, main loop returned with %d", mlReturnCode);
    return res;
}

//-------------------------------------------------------------------------------------------------
AMFPulseAudioSimpleAPISourceImpl::AMFPulseAudioSimpleAPISourceImpl()
{}

//-------------------------------------------------------------------------------------------------
AMFPulseAudioSimpleAPISourceImpl::~AMFPulseAudioSimpleAPISourceImpl()
{
    // Just in case, check if pa_simple is freed.
    if (m_pPaSimple)
    {
        m_pa.m_pPA_Simple_Free(m_pPaSimple);
        m_pPaSimple = nullptr;
    }
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFPulseAudioSimpleAPISourceImpl::Init(bool captureMic)
{
    AMF_RETURN_IF_FAILED(m_pa.LoadFunctionsTable());

    AMF_RESULT res = AMF_OK;

    // First we need to get a list of sources, including mic and displays.
    // Use the default monitor for now. Need a proper way to get the correct source.
    // i.e. mic vs desktop monitor.
    res = InitDeviceNames();
    AMF_RETURN_IF_FAILED(res,L"AMFPulseAudioSimpleAPISourceImpl::Init() failed. Cannot init with default device names.");

    // Setup the PaSample Spec
    pa_sample_spec paSampleSS;
    paSampleSS.format = PA_SAMPLE_S16NE;
    paSampleSS.channels = m_ChannelCount;
    paSampleSS.rate = m_SampleRate;

    // Setup the buffer attribute.
    pa_buffer_attr bufferAttr;
    bufferAttr.fragsize = m_SampleCount * sizeof(short) * m_ChannelCount;
    bufferAttr.maxlength = bufferAttr.fragsize * 2;

    // Create the new pa_simple
    amf_string srcDevice = (true == captureMic)? m_DefaultSource:m_DefaultSinkMonitor;
    m_pPaSimple = m_pa.m_pPA_Simple_New(NULL, "AudioCaptureImplLinux", PA_STREAM_RECORD, srcDevice.c_str(),
        "AudioCaptureImplLinux",& paSampleSS, NULL, &bufferAttr, NULL);

    if (m_pPaSimple == NULL)
    {
        res = AMF_FAIL;
    }
    return res;
}


//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFPulseAudioSimpleAPISourceImpl::Terminate()
{
    AMF_RESULT res = AMF_OK;
    if (m_pPaSimple)
    {
        m_pa.m_pPA_Simple_Free(m_pPaSimple);
        m_pPaSimple = nullptr;
    }
    return res;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFPulseAudioSimpleAPISourceImpl::CaptureAudio(AMFAudioBufferPtr& pAudioBuffer, AMFContextPtr& pContext, amf_uint32& capturedSampleCount)
{
    AMF_RETURN_IF_FALSE(pContext != nullptr, AMF_FAIL, L"AMFPulseAudioSimpleAPISourceImpl::CaptureAudio(): AMF context is NULL");
    AMF_RESULT res = AMF_FAIL;
    short* pDst = nullptr;

    // Allocate memory for pAudioBuffer, a fixed size of 44100/90 = 490 samples.
    res = pContext->AllocAudioBuffer(AMF_MEMORY_HOST, AMFAF_S16, m_SampleCount, m_SampleRate, m_ChannelCount, &pAudioBuffer);
    if (AMF_OK == res && pAudioBuffer != nullptr)
    {
        // FI succesfully got audio buffer, alloc memory and pass captured data to it.
        pDst = (short*)pAudioBuffer->GetNative();
        res = CaptureAudioRaw(pDst, m_SampleCount, capturedSampleCount);
    }
    return res;
}

AMF_RESULT AMFPulseAudioSimpleAPISourceImpl::CaptureAudioRaw(short* dest, amf_uint32 sampleCount, amf_uint32& capturedSampleCount)
{
    // Capture audio and save directly into Audio Buffer.
    amf_int32 paReadErr = 0;
    amf_int32 paReadReturn = 0;

    // Capture 1/90 second.
    paReadReturn = m_pa.m_pPA_Simple_Read(m_pPaSimple, dest, sizeof(short)*sampleCount*m_ChannelCount, &paReadErr);
    AMF_RETURN_IF_FALSE(paReadReturn == 0, AMF_FAIL, L"pa_simple_read returned error: (%S)", m_pa.m_pPA_Strerror(paReadErr));
    capturedSampleCount = sampleCount;

    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
void AMFPulseAudioSimpleAPISourceImpl::AddToSourceList(amf_string& srcName)
{
    // So far only one callback function call this in a serial manner. No need to protect with lock.
    m_SrcList.emplace_back(srcName);
}

//-------------------------------------------------------------------------------------------------
void AMFPulseAudioSimpleAPISourceImpl::AddToSinkMonitorList(amf_string& srcName)
{
    // So far only one callback function call this in a serial manner. No need to protect with lock.
    m_SinkMonitorList.emplace_back(srcName);
}

//-------------------------------------------------------------------------------------------------
void AMFPulseAudioSimpleAPISourceImpl::AddToSinkList(amf_string& sinkName)
{
    // So far only one callback function call this in a serial manner. No need to protect with lock.
    m_SinkList.emplace_back(sinkName);
}

//-------------------------------------------------------------------------------------------------
void AMFPulseAudioSimpleAPISourceImpl::SetDefaultSource(amf_string& deviceName)
{
    m_DefaultSource = deviceName;
}

//-------------------------------------------------------------------------------------------------
void AMFPulseAudioSimpleAPISourceImpl::SetDefaultSinkMonitor(amf_string& deviceName)
{
    m_DefaultSinkMonitor = deviceName;
}
