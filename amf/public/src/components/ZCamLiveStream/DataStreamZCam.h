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

#pragma once

#include "public/common/DataStream.h"
#include "public/common/InterfaceImpl.h"
#include <string>

namespace amf
{
    class AMFDataStreamZCamVideo
    {
    public:
        AMFDataStreamZCamVideo(SOCKET& mySocket, const char* pAddressIP);
        ~AMFDataStreamZCamVideo();
        // interface
        AMF_RESULT Close();
        AMF_RESULT Read(void* pData, amf_size iSize, amf_size* pRead);

        AMF_RESULT Init();
        AMF_RESULT VideoCaptureGetOneFrame();
        AMF_RESULT CaptureOneFrame(char** ppData, int& lenData);
        AMF_RESULT CaptureOneFrameReq();
        static int GetDataBufLength(){ return DataBufLen; };

    protected:
        static const int DataBufLen = 3392 * 2544 * 3 / 2;// 1080p

        SOCKET          m_socket;
        std::ofstream   m_fileVideo;

        std::vector<std::string> m_addressIP;

        char*    m_pDataBuf;
        amf_size m_dataLen;
        amf_size m_dataStartPos;
        amf_size m_curPos;

        char m_message[256];
        int m_framCount;

        SOCKET CreateSocket(const char* pAddressIP, const char* port, struct addrinfo& addressIP);
        int ConnectToCamera(SOCKET mySocket, struct addrinfo& address);
        int SendCommand(SOCKET mySocket, const char* ipCamera, char* sendBuf, char* recvbuf, int lenBuf);
        int ReceiveCommand(SOCKET mySocket, char* recvbuf, int lenBuf);
        AMF_RESULT ReadInternal(void* pData, amf_size iSize, amf_size* pRead);
    };

    class AMFDataStreamZCamImpl
    {
    public:
        AMFDataStreamZCamImpl();
        virtual ~AMFDataStreamZCamImpl();

        AMF_RESULT Read(void* pData, amf_size iSize, amf_size* pRead);
        AMF_RESULT Open(const char* pFilePath, AMF_STREAM_OPEN eOpenType, AMF_FILE_SHARE eShareType);
        AMF_RESULT Close();
        int        SetupCameras(const char* mode);
        AMF_RESULT Open();
        int RequestFrame();
        int CaptureOneFrame(int index, char** ppData, int& lenData);
        int CaptureOneFrame(std::vector<char*>& ppDataList, std::vector<int>& lenDataList);
        void       SetIP(int index, const char* addressIP){ m_addressIP[index] = addressIP;};

        static const int CountCamera = 4;
        static const int CommandBufLen = 256;
        static const char* PortTCP;
        static const char* PortHTTP;

    protected:
        std::vector<SOCKET>        m_socket;
        std::vector<AMFDataStreamZCamVideo*> m_videoZCam;
        std::vector<std::string> m_addressIP;

        std::ofstream m_fileVideo;
        std::ofstream m_fileLog;
        int     m_framCount;
        int     m_framCountLog;
        int     m_activeCamera;
        int     m_startCamera;
        int     m_endCamera;
        char    m_message[256];
        bool    m_bFirstCapture;

        int SetupCamera(SOCKET& mySocket, const char* ipAddress, bool isMaterCamer, const char* mode);
        int VideoCaptureGetOneFrame();
        SOCKET CreateSocket(const char* pAddressIP, const char* port, struct addrinfo& addressIP);
        int ConnectToCamera(SOCKET mySocket, struct addrinfo& address);
        int SendCommand(SOCKET mySocket, const char* ipCamera, char* sendBuf, char* recvbuf, int lenBuf);
        int ReceiveCommand(SOCKET mySocket, char* recvbuf, int lenBuf);

        AMF_RESULT Open(const char* pAddressIP, SOCKET& mySocket);
        AMF_RESULT ReadInternal(void* pData, amf_size iSize, amf_size* pRead);
    };
} //namespace amf
