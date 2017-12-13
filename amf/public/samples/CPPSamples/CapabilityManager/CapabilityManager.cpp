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
// CapabilityManager.cpp : Defines the entry point for the console application.

#include "stdafx.h"

#include "public/common/AMFFactory.h"
#include "public/include/components/VideoDecoderUVD.h"
#include "public/include/components/VideoEncoderVCE.h"
#include "public/include/components/VideoEncoderHEVC.h"
#include "public/include/components/VideoConverter.h"
#include "public/include/core/Context.h"
#include "public/include/core/Debug.h"

#ifdef _WIN32

#include "../common/DeviceDX9.h"
#include "../common/DeviceDX11.h"

#include <windows.h>

#endif

#include <fstream>
#include <iostream>
#include <string>
#include <iomanip>

std::wstring AccelTypeToString(amf::AMF_ACCELERATION_TYPE accelType)
{
    std::wstring strValue;
    switch (accelType)
    {
    case amf::AMF_ACCEL_NOT_SUPPORTED:
        strValue = L"Not supported";
        break;
    case amf::AMF_ACCEL_HARDWARE:
        strValue = L"Hardware-accelerated";
        break;
    case amf::AMF_ACCEL_GPU:
        strValue = L"GPU-accelerated";
        break;
    case amf::AMF_ACCEL_SOFTWARE:
        strValue = L"Not accelerated (software)";
        break;
    }
    return strValue;
}

bool QueryIOCaps(amf::AMFIOCapsPtr& ioCaps)
{
    bool result = true;
    if (ioCaps != NULL)
    {
        amf_int32 minWidth, maxWidth;
        ioCaps->GetWidthRange(&minWidth, &maxWidth);
        std::wcout << L"\t\t\tWidth: [" << minWidth << L"-" << maxWidth << L"]\n";
    
        amf_int32 minHeight, maxHeight;
        ioCaps->GetHeightRange(&minHeight, &maxHeight);
        std::wcout << L"\t\t\tHeight: [" << minHeight << L"-" << maxHeight << L"]\n";

        amf_int32 vertAlign = ioCaps->GetVertAlign();
        std::wcout << L"\t\t\tVertical alignment: " << vertAlign << L" lines.\n";

        amf_bool interlacedSupport = ioCaps->IsInterlacedSupported();
        std::wcout << L"\t\t\tInterlaced support: " << (interlacedSupport ? L"YES" : L"NO") << L"\n";

        amf_int32 numOfFormats = ioCaps->GetNumOfFormats();
        std::wcout << L"\t\t\tTotal of " << numOfFormats << L" pixel format(s) supported:\n";
        for (amf_int32 i = 0; i < numOfFormats; i++)
        {
            amf::AMF_SURFACE_FORMAT format;
            amf_bool native = false;
            if (ioCaps->GetFormatAt(i, &format, &native) == AMF_OK)
            {
                std::wcout << L"\t\t\t\t" << i << L": " << g_AMFFactory.GetTrace()->SurfaceGetFormatName(format) << L" " << (native ? L"(native)" : L"") << L"\n";
            }
            else
            {
                result = false;
                break;
            }
        }

        if (result == true)
        {
            amf_int32 numOfMemTypes = ioCaps->GetNumOfMemoryTypes();
            std::wcout << L"\t\t\tTotal of " << numOfMemTypes << L" memory type(s) supported:\n";
            for (amf_int32 i = 0; i < numOfMemTypes; i++)
            {
                amf::AMF_MEMORY_TYPE memType;
                amf_bool native = false;
                if (ioCaps->GetMemoryTypeAt(i, &memType, &native) == AMF_OK)
                {
                    std::wcout << L"\t\t\t\t" << i << L": " << g_AMFFactory.GetTrace()->GetMemoryTypeName(memType) << L" " << (native ? L"(native)" : L"") << L"\n";
                }
            }
        }
    }
    else
    {
        std::wcerr << L"ERROR: ioCaps == NULL\n";
        result = false;
    }
    return result;
}

bool QueryDecoderForCodec(const wchar_t *componentID, amf::AMFContext* pContext)
{
    std::wcout << L"\tCodec " << componentID << L" is ";
    amf::AMFCapsPtr decoderCaps;
    bool result = false;
    amf::AMFComponentPtr pDecoder;
    g_AMFFactory.GetFactory()->CreateComponent(pContext, componentID, &pDecoder);
    if(pDecoder == NULL)
    {
        std::wcout << AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED) << L"\n";
        return false;
    }
    if (pDecoder->GetCaps(&decoderCaps) == AMF_OK)
    {
        amf::AMF_ACCELERATION_TYPE accelType = decoderCaps->GetAccelerationType();
        std::wcout << AccelTypeToString(accelType) << L"\n";

        std::wcout << L"\t\tDecoder input:\n";
        amf::AMFIOCapsPtr inputCaps;
        if (decoderCaps->GetInputCaps(&inputCaps) == AMF_OK)
        {
            result = QueryIOCaps(inputCaps);
        }

        std::wcout << L"\t\tDecoder output:\n";
        amf::AMFIOCapsPtr outputCaps;
        if (decoderCaps->GetOutputCaps(&outputCaps) == AMF_OK)
        {
            result = QueryIOCaps(outputCaps);
        }
        return true;
    }
    else
    {
        std::wcout << AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED) << L"\n";
        return false;
    }
}

bool QueryEncoderForCodecAVC(const wchar_t *componentID, amf::AMFContext* pContext)
{
    std::wcout << L"\tCodec " << componentID << L"\n";
    amf::AMFCapsPtr encoderCaps;
    bool result = false;

    amf::AMFComponentPtr pEncoder;
    g_AMFFactory.GetFactory()->CreateComponent(pContext, componentID, &pEncoder);
    if(pEncoder == NULL)
    {
        std::wcout << AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED) << L"\n";
        return false;
    }
    if (pEncoder->GetCaps(&encoderCaps) == AMF_OK)
    {
        amf::AMF_ACCELERATION_TYPE accelType = encoderCaps->GetAccelerationType();
        std::wcout << L"\t\tAcceleration Type:" << AccelTypeToString(accelType) << L"\n";

        amf_uint32 maxProfile = 0;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_MAX_PROFILE, &maxProfile);
        std::wcout << L"\t\tmaximum profile:" <<maxProfile << L"\n";

        amf_uint32 maxLevel = 0;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_MAX_LEVEL, &maxLevel);
        std::wcout << L"\t\tmaximum level:" <<maxLevel << L"\n";

        amf_uint32 maxTemporalLayers = 0;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_MAX_TEMPORAL_LAYERS, &maxTemporalLayers);
        std::wcout << L"\t\tNumber of temporal Layers:" << maxTemporalLayers << L"\n";

        bool bBPictureSupported = false;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_BFRAMES, &bBPictureSupported);
        std::wcout << L"\t\tIsBPictureSupported:" << bBPictureSupported << L"\n\n";

        amf_uint32 maxNumOfStreams = 0;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_NUM_OF_STREAMS, &maxNumOfStreams);
        std::wcout << L"\t\tMax Number of streams supported:" << maxNumOfStreams << L"\n";

        amf_uint32 NumOfHWInstances = 0;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_NUM_OF_HW_INSTANCES, &NumOfHWInstances);
        std::wcout << L"\t\tNumber HW instances:" << NumOfHWInstances << L"\n";

        std::wcout << L"\t\tEncoder input:\n";
        amf::AMFIOCapsPtr inputCaps;
        if (encoderCaps->GetInputCaps(&inputCaps) == AMF_OK)
        {
            result = QueryIOCaps(inputCaps);
        }

        std::wcout << L"\t\tEncoder output:\n";
        amf::AMFIOCapsPtr outputCaps;
        if (encoderCaps->GetOutputCaps(&outputCaps) == AMF_OK)
        {
            result = QueryIOCaps(outputCaps);
        }
        return true;
    }
    else
    {
        std::wcout << AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED) << L"\n";
        return false;
    }
}

bool QueryEncoderForCodecHEVC(const wchar_t *componentID, amf::AMFContext* pContext)
{
    std::wcout << L"\tCodec " << componentID << L"\n";
    amf::AMFCapsPtr encoderCaps;
    bool result = false;

    amf::AMFComponentPtr pEncoder;
    g_AMFFactory.GetFactory()->CreateComponent(pContext, componentID, &pEncoder);
    if(pEncoder == NULL)
    {
        std::wcout << AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED) << L"\n";
        return false;
    }
    if (pEncoder->GetCaps(&encoderCaps) == AMF_OK)
    {
        amf::AMF_ACCELERATION_TYPE accelType = encoderCaps->GetAccelerationType();
        std::wcout << L"\t\tAcceleration Type:" << AccelTypeToString(accelType) << L"\n";

        amf_uint32 maxProfile = 0;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_HEVC_CAP_MAX_PROFILE, &maxProfile);
        std::wcout << L"\t\tmaximum profile:" <<maxProfile << L"\n";

        amf_uint32 maxTier = 0;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_HEVC_CAP_MAX_TIER, &maxTier);
        std::wcout << L"\t\tmaximum tier:" <<maxTier << L"\n";

        amf_uint32 maxLevel = 0;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_HEVC_CAP_MAX_LEVEL, &maxLevel);
        std::wcout << L"\t\tmaximum level:" <<maxLevel << L"\n";

        amf_uint32 maxNumOfStreams = 0;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_HEVC_CAP_NUM_OF_STREAMS, &maxNumOfStreams);
        std::wcout << L"\t\tMax Number of streams supported:" << maxNumOfStreams << L"\n";

        std::wcout << L"\t\tEncoder input:\n";
        amf::AMFIOCapsPtr inputCaps;
        if (encoderCaps->GetInputCaps(&inputCaps) == AMF_OK)
        {
            result = QueryIOCaps(inputCaps);
        }

        std::wcout << L"\t\tEncoder output:\n";
        amf::AMFIOCapsPtr outputCaps;
        if (encoderCaps->GetOutputCaps(&outputCaps) == AMF_OK)
        {
            result = QueryIOCaps(outputCaps);
        }
        return true;
    }
    else
    {
        std::wcout << AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED) << L"\n";
        return false;
    }
}

void QueryDecoderCaps(amf::AMFContext* pContext)
{
    std::wcout << L"Querying video decoder capabilities...\n";
    QueryDecoderForCodec(AMFVideoDecoderUVD_MJPEG, pContext);
    QueryDecoderForCodec(AMFVideoDecoderUVD_MPEG4, pContext);
    QueryDecoderForCodec(AMFVideoDecoderUVD_H264_AVC, pContext);
    QueryDecoderForCodec(AMFVideoDecoderUVD_MPEG2, pContext);
    QueryDecoderForCodec(AMFVideoDecoderHW_H265_HEVC, pContext);
    
}

bool QueryEncoderCaps(amf::AMFContext* pContext)
{
    std::wcout << L"Querying video encoder capabilities...\n";
    
    QueryEncoderForCodecAVC(AMFVideoEncoderVCE_AVC, pContext);
    QueryEncoderForCodecAVC(AMFVideoEncoderVCE_SVC, pContext);
    QueryEncoderForCodecHEVC(AMFVideoEncoder_HEVC, pContext);

    return  true;
}

bool QueryConverterCaps(amf::AMFContext* pContext)
{
    std::wcout << L"Querying video converter capabilities...\n";
    bool result = false;
    amf::AMFCapsPtr converterCaps;

    amf::AMFComponentPtr pConverter;
    g_AMFFactory.GetFactory()->CreateComponent(pContext, AMFVideoConverter, &pConverter);
    if(pConverter == NULL)
    {
        std::wcout << AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED) << L"\n";
        return false;
    }
    if (pConverter->GetCaps(&converterCaps) == AMF_OK)
    {
        std::wcout << L"\t\tConverter input:\n";
        amf::AMFIOCapsPtr inputCaps;
        if (converterCaps->GetInputCaps(&inputCaps) == AMF_OK)
        {
            result = QueryIOCaps(inputCaps);
        }

        std::wcout << L"\t\tConverter output:\n";
        amf::AMFIOCapsPtr outputCaps;
        if (converterCaps->GetOutputCaps(&outputCaps) == AMF_OK)
        {
            result = QueryIOCaps(outputCaps);
        }
    }
    return result;
}

int _tmain(int argc, _TCHAR* argv[])
{
    bool result = false;
    AMF_RESULT              res = AMF_OK; // error checking can be added later
    res = g_AMFFactory.Init();
    if(res != AMF_OK)
    {
        wprintf(L"AMF Failed to initialize");
        return 1;
    }
    wprintf(L"AMF version (header):  %I64X\n", AMF_FULL_VERSION);
    wprintf(L"AMF version (runtime): %I64X\n", g_AMFFactory.AMFQueryVersion() );

    g_AMFFactory.GetDebug()->AssertsEnable(false);

    for(int deviceIdx = 0; ;deviceIdx++)
    { 

        amf::AMFContextPtr pContext;
        if (g_AMFFactory.GetFactory()->CreateContext(&pContext) == AMF_OK)
        {
            bool deviceInit = false;
        
    #ifdef _WIN32

            OSVERSIONINFO osvi;
            memset(&osvi, 0, sizeof(osvi));
            osvi.dwOSVersionInfoSize = sizeof(osvi);
            GetVersionEx(&osvi);
  
            if (osvi.dwMajorVersion >= 6)
            {
                if (osvi.dwMinorVersion >= 2)   //  Win 8 or Win Server 2012 or newer
                {
                    DeviceDX11  deviceDX11;
                    if (deviceDX11.Init(deviceIdx) == AMF_OK)
                    {
                        deviceInit = (pContext->InitDX11(deviceDX11.GetDevice()) == AMF_OK);
                    }
                    else
                    {
                        break;
                    }
                }
                else
                {
                    DeviceDX9   deviceDX9;
                    if (deviceDX9.Init(true, deviceIdx, false, 1, 1) == AMF_OK || deviceDX9.Init(false, deviceIdx, false, 1, 1) == AMF_OK)    //  For DX9 try DX9Ex first and fall back to DX9 if Ex is not available
                    {
                        deviceInit = (pContext->InitDX9(deviceDX9.GetDevice()) == AMF_OK);
                    }
                    else
                    {
                        break;
                    }
                }
            }
            else    //  Older than Vista - not supported
            {
                std::wcerr << L"This version of Windows is too old\n";
            }
    #endif
            if (deviceInit == true)
            {
                QueryDecoderCaps(pContext);
                QueryEncoderCaps(pContext);
                QueryConverterCaps(pContext);
                result = true;
            }
        }
        else
        {
            std::wcerr << L"Failed to instatiate Capability Manager.\n";
            return -1;
        }
        pContext.Release();
    }
    g_AMFFactory.Terminate();
    return result ? 0 : 1;
}

