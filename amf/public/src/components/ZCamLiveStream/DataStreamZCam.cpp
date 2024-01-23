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
//
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <fstream>
#include <Mstcpip.h>

// Need to link with Ws2_32.lib, Mswsock.lib, and Advapi32.lib
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

#include "public/common/TraceAdapter.h"
#include "public/common/AMFSTL.h"
#include "DataStreamZCam.h"

#pragma warning(disable: 4996)
#if defined(_WIN32)
#include <io.h>
#endif

#include <fcntl.h>
#include <sys/stat.h>

const char* amf::AMFDataStreamZCamImpl::PortTCP = "9876";
const char* amf::AMFDataStreamZCamImpl::PortHTTP = "80";

using namespace amf;

#if _DEBUG
    #define ENABLE_LOG      0
#endif
//-------------------------------------------------------------------------------------------------
AMFDataStreamZCamVideo::AMFDataStreamZCamVideo(SOCKET& mySocket, const char* /* pAddressIP */) :
    m_socket(mySocket),
    m_pDataBuf(NULL),
    m_dataLen(0),
    m_dataStartPos(0),
    m_curPos(0),
    m_framCount(0)
{
    ::memset(m_message, 0, sizeof(m_message));
}
//-------------------------------------------------------------------------------------------------
AMFDataStreamZCamVideo::~AMFDataStreamZCamVideo()
{
    Close();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFDataStreamZCamVideo::Close()
{
    AMF_RESULT err = AMF_OK;
    if (m_pDataBuf)
    {
        delete[] m_pDataBuf;
        m_pDataBuf = NULL;
    }

    if (m_socket != INVALID_SOCKET)
    {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }

    return err;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFDataStreamZCamVideo::Read(void* pData, amf_size iSize, amf_size* pRead)
{
    AMF_RETURN_IF_FALSE(m_socket != INVALID_SOCKET, AMF_WRONG_STATE, L"Read() - Camera not ready!");
    AMF_RESULT err = AMF_OK;

    if (m_curPos < m_dataLen)
    {
        amf_size dataAvailable = m_dataLen - m_curPos;
        amf_size readSize = iSize;
        if (iSize > dataAvailable)
        {
            readSize = dataAvailable;
        }

        memcpy(pData, &m_pDataBuf[(int)m_curPos + (int)m_dataStartPos], readSize);
        m_curPos += readSize;
        if (pRead)
        {
            *pRead = readSize;
        }
    }
    else
    {
        if (pRead)
        {
            *pRead = 0;
        }
    }
    return err;
}
//-------------------------------------------------------------------------------------------------
int AMFDataStreamZCamImpl::VideoCaptureGetOneFrame()
{
    int result = 0;
    for (int idx = m_startCamera; !result && (idx < m_endCamera); idx++)
    {
        if (m_videoZCam[idx])
        {
            result = m_videoZCam[idx]->VideoCaptureGetOneFrame();
        }
    }
    m_framCount++;
    return result;
}
//-------------------------------------------------------------------------------------------------
int AMFDataStreamZCamImpl::CaptureOneFrame(int index, char** ppData, int& lenData)
{
    if ((index >= m_endCamera) || !m_videoZCam[index])
        return NULL;
    m_videoZCam[index]->CaptureOneFrameReq();
    return m_videoZCam[index]->CaptureOneFrame(ppData, lenData);
}
//-------------------------------------------------------------------------------------------------
int AMFDataStreamZCamImpl::RequestFrame()
{
    m_framCount++;
    int result = 0;
    for (int idx = m_startCamera; !result && (idx < m_endCamera); idx++)
    {
        result = m_videoZCam[idx]->CaptureOneFrameReq();
    }
    return result;
}
//-------------------------------------------------------------------------------------------------
int AMFDataStreamZCamImpl::CaptureOneFrame(std::vector<char*>& pDataList, std::vector<int>& lenDataList)
{
    int result = 0;
    pDataList.clear();
    lenDataList.clear();

    if(m_bFirstCapture)
    {
        m_bFirstCapture = false;
        RequestFrame();
    }

    for (int idx = m_startCamera; !result && (idx < m_endCamera); idx++)
    {
        int lenData = 0;
        char* pData = NULL;
        result = m_videoZCam[idx]->CaptureOneFrame(&pData, lenData);
        if (result)
        {
            break;
        }
        lenDataList.push_back(lenData);
        pDataList.push_back(pData);
    }

    // request frames for the next call
    RequestFrame();
    m_framCount++;
    return result;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFDataStreamZCamVideo::CaptureOneFrameReq()
{

    char sendCommand = 0x01;
    int sendSize = send(m_socket, &sendCommand, 1, 0);
    if (sendSize != 1)
    {
        AMFTraceError(L"AMFDataStreamZCamVideo", L"CaptureOneFrameReq() failed!");
        return AMF_FAIL;
    }
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFDataStreamZCamVideo::CaptureOneFrame(char** ppData, int& lenData)
{
    m_dataStartPos = 0;
    int result = VideoCaptureGetOneFrame();
    if (!result)
    {
        lenData = (int)m_dataLen;
        if (ppData)
        {
            *ppData  = &m_pDataBuf[m_dataStartPos];
        }
    }
    return SUCCEEDED(result) ?  AMF_OK : AMF_FAIL;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFDataStreamZCamVideo::VideoCaptureGetOneFrame()
{
    int result = 0;
    m_dataLen = 0;
    int lenReceived = recv(m_socket, m_pDataBuf, DataBufLen, 0);

    if (lenReceived <= 0)
    {
        result = WSAGetLastError();
        ASSERT_RETURN_IF_HR_FAILED(result, AMF_FAIL, L"VideoCaptureGetOneFrame().recv() failed");
    }
    else if (lenReceived >= 4)
    {
        int packetLen = ntohl(*(int*)m_pDataBuf);
        m_dataLen = lenReceived - 4;

        while ((int)m_dataLen < packetLen)
        {
            lenReceived = recv(m_socket, &m_pDataBuf[4 + m_dataLen], (int)(DataBufLen - 4 - m_dataLen), 0);
            if (lenReceived == 0)
            {
                Sleep(1);
            }
            else if (lenReceived == -1)
            {
                result = WSAGetLastError();
                if (result == WSATRY_AGAIN || result == WSAEINTR || result == WSAEWOULDBLOCK)
                {
                    continue;
                }
                AMFTraceError(L"AMFDataStreamZCamVideo", L"VideoCaptureGetOneFrame().recv() failed!");
                break;
            }
            m_dataLen += lenReceived;
        };
        m_dataStartPos = 4;
    }

    m_curPos = 0;
    m_framCount++;
    return SUCCEEDED(result) ? AMF_OK : AMF_FAIL;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFDataStreamZCamVideo::Init()
{
    AMF_RESULT err = AMF_OK;
    if (!m_pDataBuf)
    {
        m_pDataBuf = new char[DataBufLen];
        if (!m_pDataBuf)
        {
            AMFTraceError(L"AMFDataStreamZCamVideo", L"Init() failed!");
            err = AMF_FAIL;
        }
    }
    m_framCount = 0;
    return err;
}
//-------------------------------------------------------------------------------------------------
AMFDataStreamZCamImpl::AMFDataStreamZCamImpl() :
    m_framCount(0),
    m_framCountLog(0),
    m_activeCamera(0),
    m_startCamera(0),
    m_endCamera(CountCamera),
    m_bFirstCapture(true)
{
    ::memset(m_message, 0, sizeof(m_message));

    // Initialize Winsock
    WSADATA wsaData;
    WORD winsockVer = MAKEWORD(2, 2);
    WSAStartup(winsockVer, &wsaData);

    m_socket.resize(CountCamera);
    m_videoZCam.resize(CountCamera);

    for (int idx = 0; idx < CountCamera; idx++)
    {
        m_socket[idx] = INVALID_SOCKET;
        m_videoZCam[idx] = NULL;
    }

    m_addressIP.resize(CountCamera);
}
//-------------------------------------------------------------------------------------------------
AMFDataStreamZCamImpl::~AMFDataStreamZCamImpl()
{
    Close();
    WSACleanup();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFDataStreamZCamImpl::Close()
{
    AMF_RESULT err = AMF_OK;

    for (int idx = 0; idx < (int)m_socket.size(); idx++)
    {
        if (m_socket[idx] != INVALID_SOCKET)
        {
            closesocket(m_socket[idx]);
            m_socket[idx] = INVALID_SOCKET;
        }

        if (m_videoZCam[idx])
        {
            m_videoZCam[idx]->Close();
            delete m_videoZCam[idx];
            m_videoZCam[idx] = NULL;
        }
    }
    m_socket.clear();

    return err;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFDataStreamZCamImpl::Read(void* pData, amf_size iSize, amf_size* pRead)
{
    AMF_RESULT err = AMF_OK;
    if (m_videoZCam[m_activeCamera])
    {
        err = m_videoZCam[m_activeCamera]->Read(pData, iSize, pRead);

        if ((err == AMF_OK) && (*pRead==0))
        {
            if (VideoCaptureGetOneFrame())  err = AMF_FAIL;
            if (err == AMF_OK)
            {
                err = m_videoZCam[m_activeCamera]->Read(pData, iSize, pRead);
            }
        }
    }

    return err;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFDataStreamZCamImpl::Open()
{
    return Open(m_addressIP[0].c_str(), AMF_STREAM_OPEN::AMFSO_READ, AMF_FILE_SHARE::AMFFS_SHARE_READ);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFDataStreamZCamImpl::Open(const char* pAddressIP, AMF_STREAM_OPEN /* eOpenType */, AMF_FILE_SHARE /* eShareType */)
{
    m_bFirstCapture = true;
    AMF_RESULT err = AMF_OK;
    std::string addressIP = pAddressIP;

    for (int idx = m_startCamera; (err == AMF_OK) && (idx < m_endCamera); idx++)
    {
        if (!memcmp(addressIP.c_str(), m_addressIP[idx].c_str(), addressIP.length()))
        {
            m_activeCamera = idx;
        }

        err = Open(m_addressIP[idx].c_str(), m_socket[idx]);

        if (err == AMF_OK)
        {
            m_videoZCam[idx] = new AMFDataStreamZCamVideo(m_socket[idx], m_addressIP[idx].c_str());

            if (m_videoZCam[idx])
            {
                err = m_videoZCam[idx]->Init();
            }
            else
            {
                AMFTraceError(L"AMFDataStreamZCamImpl", L"Open() failed!");
                err = AMF_OUT_OF_MEMORY;
            }
        }
    }
    return err;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMFDataStreamZCamImpl::Open(const char* pAddressIP, SOCKET& mySocket)
{
    AMF_RESULT err = AMF_FAIL;

    if (mySocket != INVALID_SOCKET)
    {
        closesocket(mySocket);
    }

    struct addrinfo address = { 0 };
    mySocket = CreateSocket(pAddressIP, PortTCP, address);

    if (mySocket != INVALID_SOCKET)
    {
        int result = ConnectToCamera(mySocket, address);
        err = result ? AMF_FAIL : AMF_OK;
    }

    return err;
}
//-------------------------------------------------------------------------------------------------
SOCKET AMFDataStreamZCamImpl::CreateSocket(const char* pAddressIP, const char* port, struct addrinfo& addressIP)
{
    SOCKET mySocket = INVALID_SOCKET;

    struct addrinfo addressHints;
    ZeroMemory(&addressHints, sizeof(addressHints));
    addressHints.ai_family = AF_INET;
    addressHints.ai_socktype = SOCK_STREAM;
    addressHints.ai_protocol = IPPROTO_TCP;

    // Resolve the server address and port
    struct addrinfo* pAddress = NULL;
    int result = getaddrinfo(pAddressIP, port, &addressHints, &pAddress);

    if (!result)
    {
        memcpy(&addressIP, pAddress, sizeof(addrinfo));

        // Create a SOCKET for connecting to server
        mySocket = socket(addressIP.ai_family, addressIP.ai_socktype, addressIP.ai_protocol);
        if (mySocket == INVALID_SOCKET)
        {
            AMFTraceError(L"AMFDataStreamZCamImpl", L"CreateSocket() failed!");
            result = -1;
        }
    }

    if (!result)
    {
        DWORD timeout = 2000;
        result = setsockopt(mySocket, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));

        if (result == 0)
        {
            result = setsockopt(mySocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
            ASSERT_RETURN_IF_HR_FAILED(result, AMF_FAIL, L"CreateSocket() failed");
        }
    }

    if (!result)
    {
        u_long mode = 1;
        result = setsockopt(mySocket, SOL_SOCKET, SO_KEEPALIVE, (char*)&mode, sizeof(u_long));
        ASSERT_RETURN_IF_HR_FAILED(result, AMF_FAIL, L"CreateSocket() failed");
    }

    if (result == 0)
    {
        int bufsize = AMFDataStreamZCamVideo::GetDataBufLength();
         result = setsockopt(mySocket, SOL_SOCKET, SO_RCVBUF, (char *)&bufsize, (socklen_t)sizeof(bufsize));
         ASSERT_RETURN_IF_HR_FAILED(result, AMF_FAIL, L"CreateSocket() failed");
    }

    if (result != 0)
    {
        mySocket = INVALID_SOCKET;
    }

    return mySocket;
}
//-------------------------------------------------------------------------------------------------
int AMFDataStreamZCamImpl::ConnectToCamera(SOCKET mySocket, struct addrinfo& address)
{
    int result = 0;

    unsigned long iMode = 1; //non-blocking mode to use timeout
    int iResult = ioctlsocket(mySocket, FIONBIO, &iMode);

    result = connect(mySocket, address.ai_addr, (int)address.ai_addrlen);

    if (result == SOCKET_ERROR)
    {
        result = WSAGetLastError();
        if (result == WSAEINPROGRESS || result == WSAEWOULDBLOCK)
        {
            struct timeval tv;
            fd_set myset;
            do
            {
                tv.tv_sec = 15;
                tv.tv_usec = 0;
                FD_ZERO(&myset);
                FD_SET(mySocket, &myset);
                result = select((int)mySocket + 1, NULL, &myset, NULL, &tv);

                if (result < 0 && (result = WSAGetLastError()) == WSAEINTR)
                {
                    AMFTraceError(L"AMFDataStreamZCamImpl", L"ConnectToCamera() failed!");
                    break;
                }
                if (result > 0)
                {
                    // Socket selected for write
                    int valopt;
                    int lon = sizeof(valopt);
                    if (getsockopt(mySocket, SOL_SOCKET, SO_ERROR, (char*)(&valopt), &lon) < 0)
                    {
                        AMFTraceError(L"AMFDataStreamZCamImpl", L"ConnectToCamera() failed!");
                        result = WSAGetLastError();
                        break;
                    }
                    // Check the value returned...
                    if (valopt)
                    {
                        result = WSAEINTR;
                        break;
                    }
                    result = 0; // connected
                    break;
                }
                else
                {
                    break;
                }
            } while (true);
        }
    }

    iMode = 0; //back to blocking mode
    iResult = ioctlsocket(mySocket, FIONBIO, &iMode);
    return result;
}
//-------------------------------------------------------------------------------------------------
int AMFDataStreamZCamImpl::SetupCameras(const char* mode)
{
    int result = 0;

    SOCKET mySocket[CountCamera] = { INVALID_SOCKET };
    struct addrinfo address[CountCamera];

    for (int idx = 0; !result && (idx < CountCamera); idx++)
    {
        mySocket[idx] = CreateSocket(m_addressIP[idx].c_str(), PortHTTP, address[idx]);

        if (mySocket[idx] != INVALID_SOCKET)
        {
            result = ConnectToCamera(mySocket[idx], address[idx]);
        }
        else
        {
            result = -1;
            AMFTraceError(L"AMFDataStreamZCamImpl", L"SetupCameras() failed!");
        }
    }

    for (int idx = 0; !result && (idx < CountCamera); idx++)
    {
        bool isMaterCamer = !idx ? true : false;
        result = SetupCamera(mySocket[idx], m_addressIP[idx].c_str(), isMaterCamer, mode);
    }

    for (int idx = 0; idx < CountCamera; idx++)
    {
        // shutdown the connection since no more data will be sent
        result = shutdown(mySocket[idx], SD_SEND);
        char  recvbuf[CommandBufLen];
        ReceiveCommand(mySocket[idx], recvbuf, CommandBufLen);
    }

    // cleanup
    for (int idx = 0; idx < CountCamera; idx++)
    {
        closesocket(mySocket[idx]);
    }
    return result;
}
//-------------------------------------------------------------------------------------------------
int AMFDataStreamZCamImpl::SetupCamera(SOCKET& mySocket, const char* ipAddress, bool isMaterCamer, const char* mode)
{
    int recvbuflen = 1024;
    char  recvbuf[1024];
    char  sendBuf[256];

    const char* httpData01 = "Connection: Keep-Alive\r\nAccept-Encoding: gzip, deflate\r\nUser-Agent: Mozilla/5.0\r\nHost: ";
    const char* httpData02 = "Connection: Keep-Alive\r\nUser-Agent: cpprestsdk/2.9.0\r\nHost: ";
    strcpy_s(sendBuf, "GET /ctrl/session HTTP/1.1\r\n");
    strcat_s(sendBuf, httpData01);
    SendCommand(mySocket, ipAddress, sendBuf, recvbuf, recvbuflen);

    if (isMaterCamer)
    {
        strcpy_s(sendBuf, "GET /ctrl/set?movfmt=");
        strcat_s(sendBuf, mode);
        strcat_s(sendBuf, " HTTP/1.1\r\n");
        strcat_s(sendBuf, httpData02);
        SendCommand(mySocket, ipAddress, sendBuf, recvbuf, recvbuflen);
    }

    strcpy_s(sendBuf, "GET /ctrl/set?send_stream=Stream0 HTTP/1.1\r\n");
    strcat_s(sendBuf, httpData02);
    SendCommand(mySocket, ipAddress, sendBuf, recvbuf, recvbuflen);

    strcpy_s(sendBuf, "GET /ctrl/stream_setting?index=stream0&bitrate=10000000 HTTP/1.1\r\n");
    strcat_s(sendBuf, httpData02);
    SendCommand(mySocket, ipAddress, sendBuf, recvbuf, recvbuflen);

    strcpy_s(sendBuf, "GET /ctrl/session?action=quit HTTP/1.1\r\n");
    SendCommand(mySocket, ipAddress, sendBuf, recvbuf, recvbuflen);

    return 0;
}
//-------------------------------------------------------------------------------------------------
int AMFDataStreamZCamImpl::SendCommand(SOCKET mySocket, const char* ipCamera, char* sendBuf, char* recvbuf, int lenBuf)
{
    int result = 0;
    strcat(sendBuf, ipCamera);
    strcat(sendBuf, "\r\n\r\n\0");
    int sendSize = send(mySocket, sendBuf, (int)strlen(sendBuf), 0);

    if (sendSize != static_cast<int>(strlen(sendBuf)))
    {
        AMFTraceError(L"AMFDataStreamZCamImpl", L"SendCommand() failed!");
        return -1;
    }

    if (!result)
    {
        result = ReceiveCommand(mySocket, recvbuf, lenBuf);
    }

    return result;
}
//-------------------------------------------------------------------------------------------------
int AMFDataStreamZCamImpl::ReceiveCommand(SOCKET mySocket, char* recvbuf, int lenBuf)
{
    int len = 0;

    for (int idx = 0; idx < 2; idx++)   //header + body
    {
        len = recv(mySocket, recvbuf, lenBuf, 0);

        if (len <= 0)
        {
            break;
        }
    }

    return len;
}
//-------------------------------------------------------------------------------------------------
