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
typedef struct {
    pa_mainloop*                m_pPaMainLoop = nullptr;
    AMF_RESULT                  m_Result = AMF_OK;
    std::string                 m_Message;
} PACallbackData;


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
void GetPASinkInfoCallback(pa_context* c,const pa_sink_info*i, int eol, void* userdata)
{
    PACallbackData* cbData = (PACallbackData*)userdata;
    if(eol)
    {
        pa_mainloop_quit(cbData->m_pPaMainLoop,0);
        return;
    }
    cbData->m_Result = AMF_OK;
    cbData->m_Message = i->monitor_source_name;
}

//-------------------------------------------------------------------------------------------------
void GetPAServerInfoCallback(pa_context*c,const pa_server_info *i, void* userdata)
{
    PACallbackData* cbData = (PACallbackData*)userdata;

    // Check if it doesn't have a default sink.
    if(i->default_sink_name == nullptr){
        pa_mainloop_quit(cbData->m_pPaMainLoop,-1);
        cbData->m_Result = AMF_FAIL;
        AMFTraceError(AMF_FACILITY, L"AMFPulseAudioSimpleAPISourceImpl::GetPAServerInfoCallback(): Cannot find a default sink!");
        return;
    }

    PAOperationPtr op = PAOperationPtr(pa_context_get_sink_info_by_name(c, i->default_sink_name, GetPASinkInfoCallback,userdata), &pa_operation_unref);

    // Get default sink info
    if (nullptr == op)
    {
        // Terminate the main loop, give it a negative return code.
        pa_mainloop_quit(cbData->m_pPaMainLoop,-1);
        cbData->m_Result = AMF_FAIL;
        AMFTraceError(AMF_FACILITY, L"AMFPulseAudioSimpleAPISourceImpl::GetPAServerInfoCallback(): pa_context_get_sink_info_by_name() failed!");
    }
}

//-------------------------------------------------------------------------------------------------
void PAContextStateCallback(pa_context *c, void *userdata)
{

    PACallbackData* cbData = (PACallbackData*)userdata;
    pa_context_state_t state = pa_context_get_state(c);
    switch(state)
    {
        case PA_CONTEXT_READY:
        {
            PAOperationPtr op = PAOperationPtr(pa_context_get_server_info(c,GetPAServerInfoCallback,userdata), &pa_operation_unref);
            // Try to get server info. The callback function will get the data.
            if (nullptr == op)
            {
                // If operation is NULL, terminate the main loop, give it a negative return code.
                pa_mainloop_quit(cbData->m_pPaMainLoop,-1);
                cbData->m_Result = AMF_FAIL;
                AMFTraceError(AMF_FACILITY, L"AMFPulseAudioSimpleAPISourceImpl::PAContextStateCallback(): pa_context_get_server_info() failed!");
            }
            break;
        }
        case PA_CONTEXT_FAILED:
        {
            cbData->m_Result = AMF_FAIL;
            AMFTraceError(AMF_FACILITY, L"AMFPulseAudioSimpleAPISourceImpl::PAContextStateCallback(): Failed to connect!\n");
            pa_mainloop_quit(cbData->m_pPaMainLoop,-1);
            break;
        }
        case PA_CONTEXT_TERMINATED:
        {
            AMFTraceDebug(AMF_FACILITY,L"PAContextStateCallback(): PA_CONTEXT_TERMINATED.");
            // Theoratically this quit is not needed. But just incase the loop doesn't quit.
            pa_mainloop_quit(cbData->m_pPaMainLoop,-1);
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
    PAMainLoopPtr       paMainLoop = PAMainLoopPtr(pa_mainloop_new(),&pa_mainloop_free);
    pa_mainloop_api*    paMainLoopAPI = pa_mainloop_get_api(paMainLoop.get());
    PAContextPtr        paContext = PAContextPtr(pa_context_new(paMainLoopAPI, "AMFPulseAudioSimpleAPISourceImpl:InitDeviceNames()"),&pa_context_unref);

    int mlErrCode = 0, mlReturnCode = 0;

    // Init a PACallbackData to carry necessary info to and from layers of callbacks.
    PACallbackData callbackData;
    callbackData.m_pPaMainLoop = paMainLoop.get();

    // Set state callback, when context is ready, the callback sets the
    // server info callback and ask for server info, server info callback
    // retreives default sink(we expect it to be monintor or headphone)
    // then uses the sink to get corresponding loopback output (the source, what we want to capture).
    // When connection failed, callbackData.m_Result will be AMF_FAIL.
    pa_context_set_state_callback(paContext.get(), PAContextStateCallback,(void*)(&callbackData));

    // Check if context state has failures.
    res = callbackData.m_Result;
    if (AMF_FAIL == res)
    {
        return res;
    }

    // Connect to pulse audio server. NOAUTOSPAWN prevents puslse audio to auto spawn an unwatned server.
    mlErrCode = pa_context_connect(paContext.get(),NULL,PA_CONTEXT_NOAUTOSPAWN, NULL);
    if (0 != mlErrCode)
    {
        AMF_RETURN_IF_FAILED(res,L"AMFPulseAudioSimpleAPISourceImpl::pa_context_connect() failed with: %S",pa_strerror(mlErrCode));
    }

    // Run the main loop.
    if ((mlReturnCode = pa_mainloop_run(paMainLoop.get(), &mlErrCode)) < 0)
    {
        res = AMF_FAIL;
    } else
    {
        res = callbackData.m_Result;
    }

    // Disconnect the context after everything's done.
    pa_context_disconnect(paContext.get());

    // Need to add a trace error here.
    AMF_RETURN_IF_FAILED(res,L"AMFPulseAudioSimpleAPISourceImpl::InitDeviceNames() failed, main loop returned with %d", mlReturnCode);
    m_DefaultSrc = callbackData.m_Message;
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
        pa_simple_free(m_pPaSimple);
        m_pPaSimple = nullptr;
    }
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFPulseAudioSimpleAPISourceImpl::Init()
{

    AMF_RESULT res = AMF_OK;

    // First we need to get a list of sources, including mic and displays.
    // Use the default monintor for now. Need a proper way to get the correct source.
    // i.e. mic vs desktop monitor.
    res = InitDeviceNames();
    AMF_RETURN_IF_FAILED(res,L"AMFPulseAudioSimpleAPISourceImpl::Init() failed. Cannot init m_DefaultSrc with device names.");

    // Setup the PaSample Spec
    pa_sample_spec paSampleSS;
    paSampleSS.format = PA_SAMPLE_S16NE;
    paSampleSS.channels = m_ChannelCount;
    paSampleSS.rate = m_SampleRate;

    // Create the new pa_simple
    m_pPaSimple = pa_simple_new(NULL, "AudioCaptureImplLinux", PA_STREAM_RECORD, m_DefaultSrc.c_str(),
        "AudioCaptureImplLinux",& paSampleSS, NULL, NULL,NULL);

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
        pa_simple_free(m_pPaSimple);
        m_pPaSimple = nullptr;
    }
    return res;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFPulseAudioSimpleAPISourceImpl::CaptureAudio(AMFAudioBufferPtr& pAudioBuffer, AMFContextPtr& pContext, amf_uint32& capturedSampleCount, amf_pts& latencyPts)
{
    AMF_RETURN_IF_FALSE(pContext != nullptr, AMF_FAIL, L"AMFPulseAudioSimpleAPISourceImpl::CaptureAudio(): AMF context is NULL");
    AMF_RESULT res = AMF_FAIL;
    short* pDst = nullptr;

    // Allocate memory for pAudioBuffer, a fixed size of 44100/90 = 490 samples.
    res = pContext->AllocAudioBuffer(AMF_MEMORY_HOST, AMFAF_S16, m_SampleCount, m_SampleRate, m_ChannelCount, &pAudioBuffer);
    if (AMF_OK == res && pAudioBuffer != nullptr)
    {
        // An amf_string to hold error message.
        amf_string errorMsg;
        // FI succesfully got audio buffer, alloc memory and pass captured data to it.
        pDst = (short*)pAudioBuffer->GetNative();

        // Capture audio and save directly into Audio Buffer.
        amf_int32 paReadErr = 0;
        amf_int32 paReadReturn = 0;

        // Capture 1/90 second.
        paReadReturn = pa_simple_read(m_pPaSimple, pDst, sizeof(short)*m_SampleCount*m_ChannelCount, &paReadErr);
        errorMsg = pa_strerror(paReadErr);
        AMF_RETURN_IF_FALSE(paReadReturn == 0, AMF_FAIL, L"pa_simple_read returned error: (%S)", amf_from_utf8_to_unicode(errorMsg).c_str());
        capturedSampleCount = m_SampleCount;

        // Get Latency.
        pa_usec_t latency = pa_simple_get_latency(m_pPaSimple,&paReadErr);
        errorMsg = pa_strerror(paReadErr);
        AMF_RETURN_IF_FALSE(latency != pa_usec_t(-1), AMF_FAIL, L"pa_simple_get_latency() failed: (%S)", amf_from_utf8_to_unicode(errorMsg).c_str());

        // Convert mciro second to pts. Because AMF_MICROSECOND is not defined, we use the following formula.
        // pts / AMF_MILISECOND * 1000 = microseconds -> pts = microseconds * AMF_MILLISECOND / 1000
        latencyPts = latency * AMF_MILLISECOND/1000;
    }
    return res;
}
