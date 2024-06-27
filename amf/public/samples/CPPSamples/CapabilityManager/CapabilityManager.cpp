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
// Copyright (c) 2018 Advanced Micro Devices, Inc. All rights reserved.
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


#include "public/common/AMFFactory.h"
#if !defined(__aarch64__) && !defined(__arm__)
#include "public/common/CPUCaps.h" // Doesn't work on aarch64 arm.
#endif
#include "public/common/AMFSTL.h"
#include "public/include/components/VideoDecoderUVD.h"
#include "public/include/components/VideoEncoderVCE.h"
#include "public/include/components/VideoEncoderHEVC.h"
#include "public/include/components/VideoEncoderAV1.h"
#include "public/include/components/VideoConverter.h"
#include "public/include/core/Context.h"
#include "public/include/core/Surface.h"
#include "public/include/core/Debug.h"
#include "../common/ParametersStorage.h"
#include "../common/CmdLineParser.h"
#include <stdio.h>

#define USE_DX12   1

#ifdef _WIN32
    #include <tchar.h>
    #include <windows.h>
    #include "../common/DeviceDX9.h"
    #include "../common/DeviceDX11.h"
  #if USE_DX12 == 1
    #include "../common/DeviceDX12.h"
  #endif
#endif

#include "../common/DeviceVulkan.h"

#include <fstream>
#include <iostream>
#include <string>
#include <iomanip>


static const wchar_t*  PARAM_NAME_CAPABILITY = L"CAPABILITY";

enum CAPABILITY_ENUM
{
    CAPABILITY_DX9,
    CAPABILITY_DX11,
#if USE_DX12 == 1
    CAPABILITY_DX12,
#endif
    CAPABILITY_VULKAN,
    CAPABILITY_ALL
};


static const amf::AMFEnumDescriptionEntry  AMF_ENGINE_ENUM_DESCRIPTION[] =
{
#if USE_DX12 == 1
    { CAPABILITY_DX12,      L"DirectX 12" },
#endif
    { CAPABILITY_DX11,      L"DirectX 11" },
    { CAPABILITY_DX9,       L"DirectX 9" },
    { CAPABILITY_VULKAN,    L"Vulkan" },
};





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
        std::wcout << L"\t\t\tInterlaced support: " << (interlacedSupport ? L"YES" : L"NO") << std::endl;

        amf_int32 numOfFormats = ioCaps->GetNumOfFormats();
        std::wcout << L"\t\t\tTotal of " << numOfFormats << L" pixel format(s) supported:\n";
        for (amf_int32 i = 0; i < numOfFormats; i++)
        {
            amf::AMF_SURFACE_FORMAT format;
            amf_bool native = false;
            if (ioCaps->GetFormatAt(i, &format, &native) == AMF_OK)
            {
                std::wcout << L"\t\t\t\t" << i << L": " << g_AMFFactory.GetTrace()->SurfaceGetFormatName(format) << L" " << (native ? L"(native)" : L"") << std::endl;
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
                    std::wcout << L"\t\t\t\t" << i << L": " << g_AMFFactory.GetTrace()->GetMemoryTypeName(memType) << L" " << (native ? L"(native)" : L"") << std::endl;
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
        std::wcout << AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED) << std::endl;
        return false;
    }
    if (pDecoder->GetCaps(&decoderCaps) == AMF_OK)
    {
        amf::AMF_ACCELERATION_TYPE accelType = decoderCaps->GetAccelerationType();
        std::wcout << AccelTypeToString(accelType) << std::endl;

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

        amf_bool saVideo = false;
        decoderCaps->GetProperty(AMF_VIDEO_DECODER_CAP_SUPPORT_SMART_ACCESS_VIDEO, &saVideo);
        std::wcout << L"\t\tSmartAccess Video: " << (saVideo ? L"true" : L"false") << L"\n";
        return true;
    }
    else
    {
        std::wcout << AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED) << std::endl;
        return false;
    }
}

bool QueryEncoderForCodecAVC(const wchar_t *componentID, amf::AMFContext* pContext)
{
    std::wcout << L"\tCodec " << componentID << std::endl;
    amf::AMFCapsPtr encoderCaps;
    bool result = false;

    amf::AMFComponentPtr pEncoder;
    g_AMFFactory.GetFactory()->CreateComponent(pContext, componentID, &pEncoder);
    if(pEncoder == NULL)
    {
        std::wcout << AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED) << std::endl;
        return false;
    }
    if (pEncoder->GetCaps(&encoderCaps) == AMF_OK)
    {
        amf_uint32 NumOfHWInstances = 1;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_NUM_OF_HW_INSTANCES, &NumOfHWInstances);
        std::wcout << L"\t\tNumber HW instances:" << NumOfHWInstances << std::endl;

        for (amf_uint32 i = 0; i < NumOfHWInstances; i++)
        {
            if (NumOfHWInstances > 1)
            {
                pEncoder->SetProperty(AMF_VIDEO_ENCODER_INSTANCE_INDEX, i);
                std::wcout << L"\t\t--------------------" << std::endl;
                std::wcout << L"\t\tInstance:" << i << std::endl;
            }

            amf::AMF_ACCELERATION_TYPE accelType = encoderCaps->GetAccelerationType();
            std::wcout << L"\t\tAcceleration Type:" << AccelTypeToString(accelType) << std::endl;

            amf_uint32 maxProfile = 0;
            encoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_MAX_PROFILE, &maxProfile);
            std::wcout << L"\t\tmaximum profile:" <<maxProfile << std::endl;

            amf_uint32 maxLevel = 0;
            encoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_MAX_LEVEL, &maxLevel);
            std::wcout << L"\t\tmaximum level:" <<maxLevel << std::endl;

            amf_uint32 maxTemporalLayers = 0;
            encoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_MAX_TEMPORAL_LAYERS, &maxTemporalLayers);
            std::wcout << L"\t\tNumber of temporal Layers:" << maxTemporalLayers << std::endl;

            bool bBPictureSupported = false;
            encoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_BFRAMES, &bBPictureSupported);
            std::wcout << L"\t\tIsBPictureSupported:" << bBPictureSupported << L"\n\n";

            amf_uint32 maxNumOfStreams = 0;
            encoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_NUM_OF_STREAMS, &maxNumOfStreams);
            std::wcout << L"\t\tMax Number of streams supported:" << maxNumOfStreams << std::endl;

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
        }
        amf_bool saVideo = false;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_SUPPORT_SMART_ACCESS_VIDEO, &saVideo);
        std::wcout << L"\t\tSmartAccess Video: " << (saVideo ? L"true" : L"false") << L"\n";
        return true;
    }
    else
    {
        std::wcout << AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED) << std::endl;
        return false;
    }
}

bool QueryEncoderForCodecHEVC(const wchar_t *componentID, amf::AMFContext* pContext)
{
    std::wcout << L"\tCodec " << componentID << std::endl;
    amf::AMFCapsPtr encoderCaps;
    bool result = false;

    amf::AMFComponentPtr pEncoder;
    g_AMFFactory.GetFactory()->CreateComponent(pContext, componentID, &pEncoder);
    if(pEncoder == NULL)
    {
        std::wcout << AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED) << std::endl;
        return false;
    }
    if (pEncoder->GetCaps(&encoderCaps) == AMF_OK)
    {
        amf_uint32 NumOfHWInstances = 1;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_HEVC_CAP_NUM_OF_HW_INSTANCES, &NumOfHWInstances);
        std::wcout << L"\t\tNumber HW instances:" << NumOfHWInstances << std::endl;

        for (amf_uint32 i = 0; i < NumOfHWInstances; i++)
        {
            if (NumOfHWInstances > 1)
            {
                pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_INSTANCE_INDEX, i);
                std::wcout << L"\t\t--------------------" << std::endl;
                std::wcout << L"\t\tInstance:" << i << std::endl;
            }

            amf::AMF_ACCELERATION_TYPE accelType = encoderCaps->GetAccelerationType();
            std::wcout << L"\t\tAcceleration Type:" << AccelTypeToString(accelType) << std::endl;

            amf_uint32 maxProfile = 0;
            encoderCaps->GetProperty(AMF_VIDEO_ENCODER_HEVC_CAP_MAX_PROFILE, &maxProfile);
            std::wcout << L"\t\tmaximum profile:" <<maxProfile << std::endl;

            amf_uint32 maxTier = 0;
            encoderCaps->GetProperty(AMF_VIDEO_ENCODER_HEVC_CAP_MAX_TIER, &maxTier);
            std::wcout << L"\t\tmaximum tier:" <<maxTier << std::endl;

            amf_uint32 maxLevel = 0;
            encoderCaps->GetProperty(AMF_VIDEO_ENCODER_HEVC_CAP_MAX_LEVEL, &maxLevel);
            std::wcout << L"\t\tmaximum level:" <<maxLevel << std::endl;

            amf_uint32 maxNumOfStreams = 0;
            encoderCaps->GetProperty(AMF_VIDEO_ENCODER_HEVC_CAP_NUM_OF_STREAMS, &maxNumOfStreams);
            std::wcout << L"\t\tMax Number of streams supported:" << maxNumOfStreams << std::endl;

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
        }
        amf_bool saVideo = false;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_HEVC_CAP_SUPPORT_SMART_ACCESS_VIDEO, &saVideo);
        std::wcout << L"\t\tSmartAccess Video: " << (saVideo ? L"true" : L"false") << L"\n";
        return true;
    }
    else
    {
        std::wcout << AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED) << std::endl;
        return false;
    }
}

bool QueryEncoderForCodecAV1(const wchar_t* componentID, amf::AMFContext* pContext)
{
    std::wcout << L"\tCodec " << componentID << std::endl;
    amf::AMFCapsPtr encoderCaps;
    bool result = false;

    amf::AMFComponentPtr pEncoder;
    g_AMFFactory.GetFactory()->CreateComponent(pContext, componentID, &pEncoder);
    if (pEncoder == NULL)
    {
        std::wcout << AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED) << std::endl;
        return false;
    }
    if (pEncoder->GetCaps(&encoderCaps) == AMF_OK)
    {
        amf_uint32 NumOfHWInstances = 1;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_AV1_CAP_NUM_OF_HW_INSTANCES, &NumOfHWInstances);
        std::wcout << L"\t\tNumber HW instances:" << NumOfHWInstances << std::endl;

        for (amf_uint32 i = 0; i < NumOfHWInstances; i++)
        {
            if (NumOfHWInstances > 1)
            {
                pEncoder->SetProperty(AMF_VIDEO_ENCODER_AV1_ENCODER_INSTANCE_INDEX, i);
                std::wcout << L"\t\t--------------------" << std::endl;
                std::wcout << L"\t\tInstance:" << i << std::endl;
            }

            amf_int32 minWidth = 0, maxWidth = 0;
            amf_int32 minHeight = 0, maxHeight = 0;
            amf::AMFIOCapsPtr inputCaps = nullptr;
            encoderCaps->GetInputCaps(&inputCaps);
            if (inputCaps)
            {
                inputCaps->GetWidthRange(&minWidth, &maxWidth);
                inputCaps->GetHeightRange(&minHeight, &maxHeight);
            }
            if (pEncoder->Init(amf::AMF_SURFACE_NV12, minWidth, minHeight) == AMF_OK) {
                amf::AMF_ACCELERATION_TYPE accelType = encoderCaps->GetAccelerationType();
                std::wcout << L"\t\tAcceleration Type:" << AccelTypeToString(accelType) << std::endl;
            }
            else
            {
                std::wcout << L"\t\t" << AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED) << std::endl;
                pEncoder->Terminate();
                continue;
            }
            pEncoder->Terminate();

            amf_uint32 maxProfile = 0;
            encoderCaps->GetProperty(AMF_VIDEO_ENCODER_AV1_CAP_MAX_PROFILE, &maxProfile);
            std::wcout << L"\t\tmaximum profile:" << maxProfile << std::endl;

            amf_uint32 maxLevel = 0;
            encoderCaps->GetProperty(AMF_VIDEO_ENCODER_AV1_CAP_MAX_LEVEL, &maxLevel);
            std::wcout << L"\t\tmaximum level:" << maxLevel << std::endl;

            std::wcout << L"\t\tEncoder input:\n";
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
        }
        amf_bool saVideo = false;
        encoderCaps->GetProperty(AMF_VIDEO_ENCODER_AV1_CAP_SUPPORT_SMART_ACCESS_VIDEO, &saVideo);
        std::wcout << L"\t\tSmartAccess Video: " << (saVideo ? L"true" : L"false") << L"\n";
        return true;
    }
    else
    {
        std::wcout << AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED) << std::endl;
        return false;
    }
}

void QueryDecoderCaps(amf::AMFContext* pContext)
{
    std::wcout << L"Querying video decoder capabilities...\n";

#ifdef _WIN32
    QueryDecoderForCodec(AMFVideoDecoderUVD_MJPEG, pContext);
    QueryDecoderForCodec(AMFVideoDecoderUVD_MPEG2, pContext);
    QueryDecoderForCodec(AMFVideoDecoderUVD_MPEG4, pContext);
#endif
    QueryDecoderForCodec(AMFVideoDecoderUVD_H264_AVC, pContext);
    QueryDecoderForCodec(AMFVideoDecoderHW_H265_HEVC, pContext);
    QueryDecoderForCodec(AMFVideoDecoderHW_H265_MAIN10, pContext);
}

void QueryEncoderCaps(amf::AMFContext* pContext)
{
    std::wcout << L"Querying video encoder capabilities...\n";
    
    QueryEncoderForCodecAVC(AMFVideoEncoderVCE_AVC, pContext);
    QueryEncoderForCodecAVC(AMFVideoEncoderVCE_SVC, pContext);
    QueryEncoderForCodecHEVC(AMFVideoEncoder_HEVC, pContext);
    QueryEncoderForCodecAV1(AMFVideoEncoder_AV1, pContext);
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
        std::wcout << AccelTypeToString(amf::AMF_ACCEL_NOT_SUPPORTED) << std::endl;
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

#if !defined(__aarch64__) && !defined(__arm__)
const InstructionSet::InstructionSet_Internal InstructionSet::CPU_Rep;
#endif
void QueryCPUForSSE()
{
    #if !defined(__aarch64__) && !defined(__arm__)
    std::wcout << L"\t\tSSE capabilities:" << std::endl;

    if (InstructionSet::SSE() == true)
    {
        std::wcout << L"\t\t\tSSE" << std::endl;
    }
    if (InstructionSet::SSE2() == true)
    {
        std::wcout << L"\t\t\tSSE2" << std::endl;
    }
    if (InstructionSet::SSE3() == true)
    {
        std::wcout << L"\t\t\tSSE3" << std::endl;
    }
    if (InstructionSet::SSSE3() == true)
    {
        std::wcout << L"\t\t\tSSSE3" << std::endl;
    }
    if (InstructionSet::SSE41() == true)
    {
        std::wcout << L"\t\t\tSSE4.1" << std::endl;
    }
    if (InstructionSet::SSE42() == true)
    {
        std::wcout << L"\t\t\tSSE4.2" << std::endl;
    }
    if (InstructionSet::SSE4a() == true)
    {
        std::wcout << L"\t\t\tSSE4a" << std::endl;
    }
    #endif

}

void QueryCPUForAVX()
{
    #if !defined(__aarch64__) && !defined(__arm__)
    std::wcout << L"\t\tAVX capabilities:" << std::endl;

    if (InstructionSet::AVX() == true)
    {
        std::wcout << L"\t\t\tAVX" << std::endl;
    }
    if (InstructionSet::AVX2() == true)
    {
        std::wcout << L"\t\t\tAVX2" << std::endl;
    }
    if (InstructionSet::AVX512CD() == true)
    {
        std::wcout << L"\t\t\tAVX-512CD" << std::endl;
    }
    if (InstructionSet::AVX512ER() == true)
    {
        std::wcout << L"\t\t\tAVX-512ER" << std::endl;
    }
    if (InstructionSet::AVX512F() == true)
    {
        std::wcout << L"\t\t\tAVX-512F" << std::endl;
    }
    if (InstructionSet::AVX512PF() == true)
    {
        std::wcout << L"\t\t\tAVX-512PF" << std::endl;
    }
    #endif
}

void QueryCPUCaps()
{
    #if !defined(__aarch64__) && !defined(__arm__)
    std::wcout << std::endl << L"Querying CPU capabilities..." << std::endl;

    std::wstring vendor = amf::amf_from_utf8_to_unicode(amf_string(InstructionSet::Vendor().c_str())).c_str();
    std::wstring brand = amf::amf_from_utf8_to_unicode(amf_string(InstructionSet::Brand().c_str())).c_str();
    std::wcout << L"\tVendor: " << vendor << std::endl;
    std::wcout << L"\tBrand: " << brand << std::endl;

    std::wcout << L"\tSupported Instruction Sets:" << std::endl;
    QueryCPUForSSE();
    QueryCPUForAVX();
    #endif
}

#if defined(_WIN32)
int _tmain(int /* argc */, _TCHAR* /* argv */[])
#else
int main(int argc, char* argv[])
#endif
{
    ParametersStorage params;
    params.SetParamDescription(PARAM_NAME_CAPABILITY, ParamCommon, L"Capability: DX9, DX11, DX12, Vulkan, ALL", NULL);

#if defined(_WIN32)
    if (!parseCmdLineParameters(&params))
#else
    if (!parseCmdLineParameters(&params, argc, argv))
#endif        
    {
        return -1;
    }

#if defined(_WIN32)
    std::wstring     capability = L"DX11";
#else
    std::wstring     capability = L"Vulkan";
#endif        
    CAPABILITY_ENUM  mode       = CAPABILITY_DX11;
    params.GetParamWString(PARAM_NAME_CAPABILITY, capability);
    std::transform(capability.begin(), capability.end(), capability.begin(), toUpperWchar);
    if (capability == L"DX9")
    {
        mode = CAPABILITY_DX9;
    }
    else if (capability == L"DX11")
    {
        mode = CAPABILITY_DX11;
    }
#if USE_DX12 == 1
    else if (capability == L"DX12")
    {
        mode = CAPABILITY_DX12;
    }
#endif
    else if (capability == L"VULKAN")
    {
        mode = CAPABILITY_VULKAN;
    }
    else if (capability == L"ALL")
    {
        mode = CAPABILITY_ALL;
    }
    else
    {
        wprintf(L"Invalid capability requested!");
    }


    bool result = false;
    AMF_RESULT  res = g_AMFFactory.Init();
    if (res != AMF_OK)
    {
        wprintf(L"AMF Failed to initialize");
        return 1;
    }
    wprintf(L"AMF version (header):  %I64X\n", AMF_FULL_VERSION);
    wprintf(L"AMF version (runtime): %I64X\n", g_AMFFactory.AMFQueryVersion());

    g_AMFFactory.GetDebug()->AssertsEnable(false);

    for (int deviceIdx = 0; ;deviceIdx++)
    { 
        bool deviceInitOverall = false;
        for (int type = 0; type < sizeof(AMF_ENGINE_ENUM_DESCRIPTION) / sizeof(AMF_ENGINE_ENUM_DESCRIPTION[0]); type++)
        {
            amf::AMFContextPtr pContext;
            if (g_AMFFactory.GetFactory()->CreateContext(&pContext) != AMF_OK)
            {
                std::wcerr << L"Failed to instatiate Capability Manager.\n";
                return -1;
            }

            bool deviceInit = false;

            // unlike DX devices, the Vulkan one needs to exist
            // when the Query calls happen, otherwise the object
            // disappears out of scope and calls throw exceptions
            // as vulkan DLL has been unloaded
            DeviceVulkan   deviceVulkan;

#if defined(_WIN32)

            OSVERSIONINFO osvi;
            memset(&osvi, 0, sizeof(osvi));
            osvi.dwOSVersionInfoSize = sizeof(osvi);
            GetVersionEx(&osvi);
  
            if (osvi.dwMajorVersion >= 6)
            {
                if (osvi.dwMinorVersion >= 2)   //  Win 8 or Win Server 2012 or newer
                {
    #if USE_DX12==1
                    if ((AMF_ENGINE_ENUM_DESCRIPTION[type].value == CAPABILITY_DX12) && 
                        ((mode == CAPABILITY_DX12) || (mode == CAPABILITY_ALL)) )
                    {
                        amf::AMFContext2Ptr pContext2(pContext);
                        if (pContext2 != nullptr)
                        {
                            DeviceDX12  deviceDX12;
                            if (deviceDX12.Init(deviceIdx) == AMF_OK)
                            {
                                deviceInit = (pContext2->InitDX12(deviceDX12.GetDevice()) == AMF_OK);
                            }
                        }
                    }
    #endif
                    if ((AMF_ENGINE_ENUM_DESCRIPTION[type].value == CAPABILITY_DX11) &&
                        ((mode == CAPABILITY_DX11) || (mode == CAPABILITY_ALL)))
                    {
                        DeviceDX11  deviceDX11;
                        if (deviceDX11.Init(deviceIdx) == AMF_OK)
                        {
                            deviceInit = (pContext->InitDX11(deviceDX11.GetDevice()) == AMF_OK);
                        }
                    }
                }

                if ((AMF_ENGINE_ENUM_DESCRIPTION[type].value == CAPABILITY_DX9) &&
                    ((mode == CAPABILITY_DX9) || (mode == CAPABILITY_ALL)))
                {
                    DeviceDX9   deviceDX9;
                    if (deviceDX9.Init(true, deviceIdx, false, 1, 1) == AMF_OK || 
                        deviceDX9.Init(false, deviceIdx, false, 1, 1) == AMF_OK)    //  For DX9 try DX9Ex first and fall back to DX9 if Ex is not available
                    {
                        deviceInit = (pContext->InitDX9(deviceDX9.GetDevice()) == AMF_OK);
                    }
                }
            }
            else    //  Older than Vista - not supported
            {
                std::wcerr << L"This version of Windows is too old\n";
            }
#endif

            if ((AMF_ENGINE_ENUM_DESCRIPTION[type].value == CAPABILITY_VULKAN) &&
                ((mode == CAPABILITY_VULKAN) || (mode == CAPABILITY_ALL)))
            {
                amf::AMFContext1Ptr pContext1(pContext);
                if (pContext1 != nullptr)
                {
                    if (deviceVulkan.Init(deviceIdx, pContext) == AMF_OK)
                    {
                        deviceInit = (pContext1->InitVulkan(deviceVulkan.GetDevice()) == AMF_OK);
                    }            
                }
            }

            if (deviceInit == true)
            {
                std::wcout << L"\n" << AMF_ENGINE_ENUM_DESCRIPTION[type].name << L"\n";
                std::wcout << L"-------------------------\n";

                QueryDecoderCaps(pContext);
                QueryEncoderCaps(pContext);
                QueryConverterCaps(pContext);
                deviceInitOverall = true;
                result = true;
            }

            pContext->Terminate();
        }

        if (deviceInitOverall == false)
        {
            break;
        }
    }

    QueryCPUCaps();

    g_AMFFactory.Terminate();
    return result ? 0 : 1;
}

