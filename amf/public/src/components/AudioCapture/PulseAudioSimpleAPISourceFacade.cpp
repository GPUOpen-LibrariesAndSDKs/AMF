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

#include "PulseAudioSimpleAPISourceFacade.h"
#include "../../../include/core/AudioBuffer.h"
#include "../../../common/TraceAdapter.h"
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>

#define AMF_FACILITY L"AMFPulseAudioSimpleAPISourceFacade"

using namespace amf;

typedef enum PulseFacadeCommand
{
    PULSE_FACADE_TERMINATE,
    PULSE_FACADE_CAPTUREAUDIO
} PulseFacadeCommand;

#define CHILD_SOCKET 0
#define PARENT_SOCKET 1

//-------------------------------------------------------------------------------------------------
AMFPulseAudioSimpleAPISourceFacade::AMFPulseAudioSimpleAPISourceFacade()
{}
//-------------------------------------------------------------------------------------------------
AMFPulseAudioSimpleAPISourceFacade::~AMFPulseAudioSimpleAPISourceFacade()
{
    Terminate();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFPulseAudioSimpleAPISourceFacade::Init(bool captureMic)
{
    AMFLock lock(&m_sync);
    m_isRoot = (getuid() == 0);
    if (m_isRoot == false)
    {
        //do not do facade if not root
        return AMFPulseAudioSimpleAPISourceImpl::Init(captureMic);
    }

    AMF_RESULT res = AMF_FAIL;


    const char* runtimeDir = getenv("XDG_RUNTIME_DIR");
    AMF_RETURN_IF_INVALID_POINTER(runtimeDir, L"XDG_RUNTIME_DIR unset, cannot determine pulseaudio user");

    struct stat st;
    int error = stat(runtimeDir, &st);
    AMF_RETURN_IF_FALSE(error == 0, AMF_FAIL, L"stat failed: %S", strerror(errno));

    //todo, use Comm sockets
    error = socketpair(AF_UNIX, SOCK_STREAM, 0, m_iSockets);
    AMF_RETURN_IF_FALSE(error == 0, AMF_FAIL, L"socketpair failed: %S", strerror(errno));

    struct timeval timeout = {};
    struct timeval childTimeout = {};
    timeout.tv_sec = 1;
    childTimeout.tv_sec = 0;

    //don't let sockets hang for longer than 1 second
    setsockopt(m_iSockets[PARENT_SOCKET], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(m_iSockets[CHILD_SOCKET], SOL_SOCKET, SO_RCVTIMEO, &childTimeout, sizeof(childTimeout));


    m_iChildPid = fork();

    if (m_iChildPid == 0) //child
    {
        close(m_iSockets[PARENT_SOCKET]);

        //downgrade permissions to those of XDG_RUNTIME_DIR
        error = setgid(st.st_gid);
        if (error != 0)
        {
            AMFTraceWarning(AMF_FACILITY, L"Could not downgrade gid!");
        }
        error = setuid(st.st_uid);
        if (error != 0)
        {
            AMFTraceWarning(AMF_FACILITY, L"Could not downgrade uid!");
        }

        res = Run(captureMic); //main loop
        AMFTraceInfo(AMF_FACILITY, L"Successfully exited.");

        exit(res);
    }
    else //parent
    {
        close(m_iSockets[CHILD_SOCKET]);

        AMF_RESULT recvRes = Receive(m_iSockets[PARENT_SOCKET], &res, sizeof(AMF_RESULT));
        AMF_RETURN_IF_FAILED(recvRes, L"pulseaudio child process failed to report Init");

        if (res == AMF_OK)
        {
            res = ReceiveStringList(m_iSockets[PARENT_SOCKET], m_SrcList);
            AMF_RETURN_IF_FAILED(res, L"pulseaudio child process failed to send SrcList");
            res = ReceiveStringList(m_iSockets[PARENT_SOCKET], m_SinkList);
            AMF_RETURN_IF_FAILED(res, L"pulseaudio child process failed to send SinkList");
            res = ReceiveStringList(m_iSockets[PARENT_SOCKET], m_SinkMonitorList);
            AMF_RETURN_IF_FAILED(res, L"pulseaudio child process failed to send SinkMonitorList");
        }
    }

    return res;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFPulseAudioSimpleAPISourceFacade::Terminate()
{
    AMFLock lock(&m_sync);
    if (m_isRoot == false)
    {
        return AMFPulseAudioSimpleAPISourceImpl::Terminate();
    }
    m_isRoot = false;

    if (m_iChildPid != 0)
    {
        PulseFacadeCommand command = PULSE_FACADE_TERMINATE;
        AMF_RESULT res = Send(m_iSockets[PARENT_SOCKET], &command, sizeof(PulseFacadeCommand));
        if (res != AMF_OK)
        {
            // the process is misbehaving
            kill(m_iChildPid, SIGKILL);
        }
        waitpid(m_iChildPid, nullptr, 0);
    }
    m_iChildPid = 0;
    amf_size totalData = sizeof(short)*m_SampleCount*m_ChannelCount;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFPulseAudioSimpleAPISourceFacade::CaptureAudio(AMFAudioBufferPtr& pAudioBuffer, AMFContextPtr& pContext, amf_uint32& capturedSampleCount)
{
    AMFLock lock(&m_sync);
    if (m_isRoot == false)
    {
        return AMFPulseAudioSimpleAPISourceImpl::CaptureAudio(pAudioBuffer, pContext, capturedSampleCount);
    }

    AMF_RETURN_IF_FALSE(m_iChildPid != 0, AMF_FAIL, L"Failed CaptureAudio(), no pulseaudio process.");

    PulseFacadeCommand command = PULSE_FACADE_CAPTUREAUDIO;
    AMF_RESULT res = Send(m_iSockets[PARENT_SOCKET], &command, sizeof(PulseFacadeCommand));
    if (res != AMF_OK) abort();
    AMF_RETURN_IF_FAILED(res, L"Failed CaptureAudio(), couldn't send command");

    res = pContext->AllocAudioBuffer(AMF_MEMORY_HOST, AMFAF_S16, m_SampleCount, m_SampleRate, m_ChannelCount, &pAudioBuffer);
    AMF_RETURN_IF_FAILED(res, L"Couldn't allocate audio buffer.");

    amf_size totalData = sizeof(short)*m_SampleCount*m_ChannelCount;

    short* pDst = (short*)pAudioBuffer->GetNative();

    res = Receive(m_iSockets[PARENT_SOCKET], pDst, totalData);
    AMF_RETURN_IF_FAILED(res, L"Couldn't receive audio data from pulseaudio process");
    res = Receive(m_iSockets[PARENT_SOCKET], &capturedSampleCount, sizeof(capturedSampleCount));
    AMF_RETURN_IF_FAILED(res, L"Couldn't receive capturedSampleCount to from pulseaudio process");

    return res;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFPulseAudioSimpleAPISourceFacade::Run(bool captureMic)
{
    //locking not needed, this is a different process
    AMF_RESULT res = AMFPulseAudioSimpleAPISourceImpl::Init(captureMic);

    res = Send(m_iSockets[CHILD_SOCKET], &res, sizeof(AMF_RESULT));
    AMF_RETURN_IF_FAILED(res, L"send to Init result to parent process failed");

    res = SendStringList(m_iSockets[CHILD_SOCKET], m_SrcList);
    AMF_RETURN_IF_FAILED(res, L"send SrcList to parent process failed");
    res = SendStringList(m_iSockets[CHILD_SOCKET], m_SinkList);
    AMF_RETURN_IF_FAILED(res, L"send SinkList to parent process failed");
    res = SendStringList(m_iSockets[CHILD_SOCKET], m_SinkMonitorList);
    AMF_RETURN_IF_FAILED(res, L"send SinkMonitorList to parent process failed");

    //buffer for audio, can't use AudioBuffer because we don't have context
    amf_size totalData = sizeof(short)*m_SampleCount*m_ChannelCount;
    std::unique_ptr<short[]> data(new short[totalData]);

    bool isRunning = true;
    while (isRunning == true)
    {
        PulseFacadeCommand command;
        res = Receive(m_iSockets[CHILD_SOCKET], &command, sizeof(PulseFacadeCommand));
        AMF_RETURN_IF_FAILED(res, L"failed to get command from parent");

        switch (command)
        {
            case PULSE_FACADE_CAPTUREAUDIO: {
                amf_uint32 capturedSampleCount = 0;
                res = AMFPulseAudioSimpleAPISourceImpl::CaptureAudioRaw(data.get(), m_SampleCount, capturedSampleCount);

                res = Send(m_iSockets[CHILD_SOCKET], data.get(), totalData);
                AMF_RETURN_IF_FAILED(res, L"send audio data to parent process failed");
                res = Send(m_iSockets[CHILD_SOCKET], &capturedSampleCount, sizeof(capturedSampleCount));
                AMF_RETURN_IF_FAILED(res, L"send capturedSampleCount to parent process failed");
                break;
            }
            case PULSE_FACADE_TERMINATE:
                isRunning = false;
                break;
            default:
                break;
        }
    }
    return AMF_OK;
}
// //-------------------------------------------------------------------------------------------------
AMF_RESULT AMFPulseAudioSimpleAPISourceFacade::Receive(int socket, void* data, size_t size)
{
    int received = 0;
    int count = 0;
    AMF_RESULT res = AMF_FAIL;
    do {
        received = recv(socket, data, size, MSG_NOSIGNAL | MSG_WAITALL);
        res = (received == size) ? AMF_OK : AMF_FAIL;
        count ++;
    } while(res == AMF_FAIL && errno == EAGAIN && count < 3); // Take 3 tries.

    AMF_RETURN_IF_FAILED(res,L"Receive() failed with \"%S\", tried %d times.", strerror(errno), count);
    return res;


}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFPulseAudioSimpleAPISourceFacade::Send(int socket, const void* data, size_t size)
{
    int sent = send(socket, data, size, MSG_NOSIGNAL | MSG_WAITALL);
    AMF_RESULT res = (sent == size)? AMF_OK : AMF_FAIL;
    AMF_RETURN_IF_FAILED(res,L"Send() failed with \"%S\"", strerror(errno));
    return res;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFPulseAudioSimpleAPISourceFacade::ReceiveStringList(int socket, PASourceList& list)
{
    AMF_RESULT res = AMF_OK;
    list.clear();

    amf_size count;
    res = Receive(socket, &count, sizeof(count));
    AMF_RETURN_IF_FAILED(res);
    list.reserve(count);

    for (int i = 0; i < count; i++)
    {
        amf_size size;
        res = Receive(socket, &size, sizeof(size));
        AMF_RETURN_IF_FAILED(res);
        amf_string string(size, '\0');

        if (size != 0)
        {
            res = Receive(socket, const_cast<char*>(string.data()), size * sizeof(char));
            AMF_RETURN_IF_FAILED(res);
        }

        list.push_back(string);
    }
    return res;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFPulseAudioSimpleAPISourceFacade::SendStringList(int socket, const PASourceList& list)
{
    //simple p-string serialization, with length prefix for the whole list
    AMF_RESULT res = AMF_OK;

    amf_size count = list.size();
    res = Send(socket, &count, sizeof(count));
    AMF_RETURN_IF_FAILED(res);

    for (const amf_string& string : list)
    {
        amf_size size = string.size();
        res = Send(socket, &size, sizeof(size));
        AMF_RETURN_IF_FAILED(res);

        if (size != 0)
        {
            res = Send(socket, string.c_str(), size * sizeof(char));
            AMF_RETURN_IF_FAILED(res);
        }

    }
    return res;
}