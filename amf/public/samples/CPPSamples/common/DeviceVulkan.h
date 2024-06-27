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

#pragma once
#ifdef _WIN32
#include <SDKDDKVer.h>
#endif
#include <string>
#include <vector>
#include "public/include/core/Context.h"
#include "public/common/VulkanImportTable.h"

struct VulkanPhysicalDeviceInfo
{
    VkPhysicalDeviceProperties2             props;
#if defined(__linux__)
    VkPhysicalDevicePCIBusInfoPropertiesEXT pciBusInfo;
#endif
};

class DeviceVulkan
{
public:
    DeviceVulkan();
    virtual ~DeviceVulkan();

    AMF_RESULT Init(amf_uint32 adapterID, amf::AMFContext *pContext);
    AMF_RESULT Terminate();

	VulkanImportTable* GetVulkan();
    amf::AMFVulkanDevice*      GetDevice();
    std::wstring GetDisplayDeviceName() { return m_displayDeviceName; }

     amf_uint32 GetQueueGraphicFamilyIndex(){return m_uQueueGraphicsFamilyIndex;}
     VkQueue    GetQueueGraphicQueue() {return m_hQueueGraphics;}

     amf_uint32 GetQueueComputeFamilyIndex() {return m_uQueueComputeFamilyIndex;}
     VkQueue    GetQueueComputeQueue() {return m_hQueueCompute;}

    // return list of adapters and their information
    static AMF_RESULT GetAdapterList(amf::AMFContext *pContext, std::vector<VulkanPhysicalDeviceInfo>& adapters);

private:
    AMF_RESULT CreateInstance();
    AMF_RESULT GetPhysicalDevices(std::vector<VkPhysicalDevice>& devices);
    AMF_RESULT CreateDeviceAndFindQueues(amf_uint32 adapterID, std::vector<const char*> &deviceExtensions);

    std::vector<const char*> GetDebugInstanceExtensionNames();
    std::vector<const char*> GetDebugInstanceLayerNames();
    std::vector<const char*> GetDebugDeviceLayerNames(VkPhysicalDevice device);

	amf::AMFVulkanDevice            m_VulkanDev;    
    std::wstring                    m_displayDeviceName;
	VulkanImportTable               m_ImportTable;

    amf_uint32                      m_uQueueGraphicsFamilyIndex;
    amf_uint32                      m_uQueueComputeFamilyIndex;

    VkQueue                         m_hQueueGraphics;
    VkQueue                         m_hQueueCompute;
};
