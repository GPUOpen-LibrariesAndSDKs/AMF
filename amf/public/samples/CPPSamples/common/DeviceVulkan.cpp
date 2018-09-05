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

#include "DeviceVulkan.h"
#include "CmdLogger.h"
#include <set>

//#define VK_NO_PROTOTYPES
#if defined(_WIN32)
    #define VK_USE_PLATFORM_WIN32_KHR
#endif
#include "vulkan/vulkan.h"

DeviceVulkan::DeviceVulkan() :
m_hQueueGraphics(NULL),
m_hQueueCompute(NULL),
m_uQueueGraphicsFamilyIndex(UINT32_MAX),
m_uQueueComputeFamilyIndex(UINT32_MAX)
{
    memset(&m_VulkanDev, 0, sizeof(m_VulkanDev));
}
DeviceVulkan::~DeviceVulkan()
{
    Terminate();
}

amf::AMFVulkanDevice*      DeviceVulkan::GetDevice()
{
    return &m_VulkanDev;
}

AMF_RESULT DeviceVulkan::Init(amf_uint32 adapterID, amf::AMFContext *pContext)
{
    AMF_RESULT res = AMF_OK;

    amf::AMFContext1Ptr pContext1(pContext);
    amf_size nCount = 0;
    pContext1->GetVulkanDeviceExtensions(&nCount, NULL);
    std::vector<const char*> deviceExtensions;
    if(nCount > 0)
    { 
        deviceExtensions.resize(nCount);
        pContext1->GetVulkanDeviceExtensions(&nCount, deviceExtensions.data());
    }


   res = m_ImportTable.LoadFunctionsTable();
   CHECK_RETURN(res == AMF_OK, res, L"LoadFunctionsTable() failed - check if the proper Vulkan SDK is installed");

   res = CreateInstance();
   CHECK_RETURN(res == AMF_OK, res, L"CreateInstance() failed");

#if defined(_DEBUG) && defined(ENABLE_VALIDATION)
   bool bDebug = true;
#else
   bool bDebug = false;
#endif
   res = m_ImportTable.LoadInstanceFunctionsTableExt(m_VulkanDev.hInstance, bDebug);
   CHECK_RETURN(res == AMF_OK, res, L"LoadInstanceFunctionsTableExt() failed - check if the proper Vulkan SDK is installed");

// load instance based functions
   res = CreateDeviceAndFindQueues(adapterID, deviceExtensions);
   CHECK_RETURN(res == AMF_OK, res, L"CreateDeviceAndFindQueues() failed");

   res = m_ImportTable.LoadDeviceFunctionsTableExt(m_VulkanDev.hDevice);
   CHECK_RETURN(res == AMF_OK, res, L"LoadDeviceFunctionsTableExt() failed - check if the proper Vulkan SDK is installed");

   return AMF_OK;
}

AMF_RESULT DeviceVulkan::Terminate()
{
    m_hQueueGraphics = NULL;
    m_hQueueCompute = NULL;

    if (m_VulkanDev.hDevice != VK_NULL_HANDLE)
    {
        GetVulkan()->vkDestroyDevice(m_VulkanDev.hDevice, nullptr);
    }
    if (m_VulkanDev.hInstance != VK_NULL_HANDLE)
    {
        GetVulkan()->vkDestroyInstance(m_VulkanDev.hInstance, nullptr);
    }
    memset(&m_VulkanDev, 0, sizeof(m_VulkanDev));

    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
std::vector<const char*> DeviceVulkan::GetDebugInstanceExtensionNames()

{
    uint32_t extensionCount = 0;
    GetVulkan()->vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> instanceExtensions{ extensionCount };
    GetVulkan()->vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, instanceExtensions.data());

    std::vector<const char*> result;
    for (const auto& e : instanceExtensions)
    {
        if (strcmp(e.extensionName, "VK_EXT_debug_report") == 0)
        {
            result.push_back("VK_EXT_debug_report");
        }
    }
    return result;
}
//-------------------------------------------------------------------------------------------------
std::vector<const char*> DeviceVulkan::GetDebugInstanceLayerNames()
{
    uint32_t layerCount = 0;
    GetVulkan()->vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> instanceLayers{ layerCount };
    GetVulkan()->vkEnumerateInstanceLayerProperties(&layerCount, instanceLayers.data());
    std::vector<const char*> result;
    for (const auto& p : instanceLayers)
    {
        if (strcmp(p.layerName, "VK_LAYER_LUNARG_standard_validation") == 0)
        {
            result.push_back("VK_LAYER_LUNARG_standard_validation");
        }
    }
    return result;
}
//-------------------------------------------------------------------------------------------------
std::vector<const char*> DeviceVulkan::GetDebugDeviceLayerNames(VkPhysicalDevice device)
{
    uint32_t layerCount = 0;
    GetVulkan()->vkEnumerateDeviceLayerProperties(device, &layerCount, nullptr);
    std::vector<VkLayerProperties> deviceLayers{ layerCount };
    GetVulkan()->vkEnumerateDeviceLayerProperties(device, &layerCount, deviceLayers.data());
    std::vector<const char*> result;
    for (const auto& p : deviceLayers)
    {
        if (strcmp(p.layerName, "VK_LAYER_LUNARG_standard_validation") == 0)
        {
            result.push_back("VK_LAYER_LUNARG_standard_validation");
        }
    }
    return result;
}
AMF_RESULT DeviceVulkan::CreateInstance()
{
    VkResult vkres = VK_SUCCESS;

    if (m_VulkanDev.hInstance != VK_NULL_HANDLE)
    {
        return AMF_OK;
    }
    // VkInstanceCreateInfo
    ///////////////////////
    VkInstanceCreateInfo instanceCreateInfo = {};
    instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

    std::vector<const char*> instanceExtensions =
    {
#if defined(_WIN32)
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#elif defined(__linux)
        "VK_KHR_surface",
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#endif
    };

#if defined(_DEBUG) && defined(ENABLE_VALIDATION)
    auto debugInstanceExtensionNames = GetDebugInstanceExtensionNames();

    instanceExtensions.insert(instanceExtensions.end(),
        debugInstanceExtensionNames.begin(),
        debugInstanceExtensionNames.end());
#endif

    instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();
    instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t> (instanceExtensions.size());

    std::vector<const char*> instanceLayers;

#if defined(_DEBUG) && defined(ENABLE_VALIDATION)
    auto debugInstanceLayerNames = GetDebugInstanceLayerNames();

    instanceLayers.insert(instanceLayers.end(),
        debugInstanceLayerNames.begin(),
        debugInstanceLayerNames.end());
#endif

    instanceCreateInfo.ppEnabledLayerNames = instanceLayers.data();
    instanceCreateInfo.enabledLayerCount = static_cast<uint32_t> (instanceLayers.size());

    // VkApplicationInfo
    ///////////////////////
    VkApplicationInfo applicationInfo = {};
    applicationInfo.sType               = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.apiVersion          = VK_API_VERSION_1_0;
    applicationInfo.applicationVersion  = VK_MAKE_VERSION(1, 0, 0);
    applicationInfo.engineVersion       = VK_MAKE_VERSION(1, 0, 0);
    applicationInfo.pApplicationName    = "AMF Vulkan application";
    applicationInfo.pEngineName         = "AMD Vulkan Sample Engine";

    instanceCreateInfo.pApplicationInfo = &applicationInfo;
    
    vkres = GetVulkan()->vkCreateInstance(&instanceCreateInfo, nullptr, &m_VulkanDev.hInstance);
    CHECK_RETURN(vkres == VK_SUCCESS, AMF_FAIL, L"CreateInstance() failed to vkCreateInstance, Error=" << (int)vkres);

    return AMF_OK;
}

AMF_RESULT DeviceVulkan::CreateDeviceAndFindQueues(amf_uint32 adapterID, std::vector<const char*> &deviceExtensions)
{
    VkResult vkres = VK_SUCCESS;
    uint32_t physicalDeviceCount = 0;

    vkres = GetVulkan()->vkEnumeratePhysicalDevices(m_VulkanDev.hInstance, &physicalDeviceCount, nullptr);
    CHECK_RETURN(vkres == VK_SUCCESS, AMF_FAIL, L"CreateDeviceAndQueues() failed to vkEnumeratePhysicalDevices, Error=%d" << (int)vkres);

    std::vector<VkPhysicalDevice> physicalDevices{ physicalDeviceCount };

    vkres = GetVulkan()->vkEnumeratePhysicalDevices(m_VulkanDev.hInstance, &physicalDeviceCount,physicalDevices.data());
    CHECK_RETURN(vkres == VK_SUCCESS, AMF_FAIL, L"CreateDeviceAndQueues() failed to vkEnumeratePhysicalDevices, Error=%d" << (int)vkres);

    if(adapterID < 0)
    {
        adapterID = 0;
    }
    CHECK_RETURN(adapterID < physicalDeviceCount, AMF_FAIL, L"Invalid Adapter ID");

    m_VulkanDev.hPhysicalDevice = physicalDevices[adapterID];
    std::vector<VkDeviceQueueCreateInfo> deviceQueueCreateInfoItems;
    std::vector<float>                   queuePriorities;

    uint32_t queueFamilyPropertyCount = 0;

    GetVulkan()->vkGetPhysicalDeviceQueueFamilyProperties(m_VulkanDev.hPhysicalDevice, &queueFamilyPropertyCount, nullptr);

    CHECK_RETURN(vkres == VK_SUCCESS, AMF_FAIL, L"CreateDeviceAndQueues() queueFamilyPropertyCount = 0");

    std::vector<VkQueueFamilyProperties> queueFamilyProperties{ queueFamilyPropertyCount };

    GetVulkan()->vkGetPhysicalDeviceQueueFamilyProperties(m_VulkanDev.hPhysicalDevice, &queueFamilyPropertyCount, queueFamilyProperties.data());

    m_uQueueGraphicsFamilyIndex  = UINT32_MAX;
    m_uQueueComputeFamilyIndex = UINT32_MAX;


    amf_uint32 uQueueGraphicsIndex = UINT32_MAX;
    amf_uint32 uQueueComputeIndex = UINT32_MAX;
    for(uint32_t i = 0; i < queueFamilyPropertyCount; i++)
    {
        VkQueueFamilyProperties &queueFamilyProperty = queueFamilyProperties[i];
        if (queuePriorities.size() < queueFamilyProperty.queueCount)
        {
            queuePriorities.resize(queueFamilyProperty.queueCount, 1.0f);
        }
    }
    for (uint32_t i = 0; i < queueFamilyPropertyCount; i++)
    {
        VkQueueFamilyProperties &queueFamilyProperty = queueFamilyProperties[i];
        VkDeviceQueueCreateInfo queueCreateInfo = {};


        queueCreateInfo.pQueuePriorities = &queuePriorities[0];
        queueCreateInfo.queueFamilyIndex = i;
        queueCreateInfo.queueCount = queueFamilyProperty.queueCount;
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;

        if ((queueFamilyProperty.queueFlags & VK_QUEUE_COMPUTE_BIT) && (queueFamilyProperty.queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 && m_uQueueComputeFamilyIndex == UINT32_MAX)
        {
            m_uQueueComputeFamilyIndex = i;
            uQueueComputeIndex = 0;
        }
        if ((queueFamilyProperty.queueFlags & VK_QUEUE_GRAPHICS_BIT) && m_uQueueGraphicsFamilyIndex == UINT32_MAX)
        {
            m_uQueueGraphicsFamilyIndex = i;
            uQueueGraphicsIndex = 0;
        }
        if (queueFamilyProperty.queueCount > 1)
        {
            queueCreateInfo.queueCount = 1;
        }

        deviceQueueCreateInfoItems.push_back(queueCreateInfo);
    }
    VkDeviceCreateInfo deviceCreateInfo = {};

    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(deviceQueueCreateInfoItems.size());
    deviceCreateInfo.pQueueCreateInfos = &deviceQueueCreateInfoItems[0];

    VkPhysicalDeviceFeatures features = {};
	GetVulkan()->vkGetPhysicalDeviceFeatures(m_VulkanDev.hPhysicalDevice, &features);
    deviceCreateInfo.pEnabledFeatures = &features;


    std::vector<const char*> deviceLayers;

#if defined(_DEBUG) && defined(ENABLE_VALIDATION)
    auto debugDeviceLayerNames = GetDebugDeviceLayerNames(physicalDevices[0]);

    deviceLayers.insert(deviceLayers.end(),
        debugDeviceLayerNames.begin(),
        debugDeviceLayerNames.end());
#endif

    deviceCreateInfo.ppEnabledLayerNames = deviceLayers.data();
    deviceCreateInfo.enabledLayerCount = static_cast<uint32_t> (deviceLayers.size());

    deviceExtensions.insert(deviceExtensions.begin(), "VK_KHR_swapchain");

    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
    deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t> (deviceExtensions.size());

    vkres = GetVulkan()->vkCreateDevice(m_VulkanDev.hPhysicalDevice, &deviceCreateInfo, nullptr, &m_VulkanDev.hDevice);
    CHECK_RETURN(vkres == VK_SUCCESS, AMF_FAIL, L"CreateDeviceAndQueues() vkCreateDevice() failed, Error=%d" << (int)vkres);
    CHECK_RETURN(m_VulkanDev.hDevice != nullptr, AMF_FAIL, L"CreateDeviceAndQueues() vkCreateDevice() returned nullptr");
    
	GetVulkan()->vkGetDeviceQueue(m_VulkanDev.hDevice, m_uQueueGraphicsFamilyIndex, uQueueGraphicsIndex, &m_hQueueGraphics);
	GetVulkan()->vkGetDeviceQueue(m_VulkanDev.hDevice, m_uQueueComputeFamilyIndex, uQueueComputeIndex, &m_hQueueCompute);

    return AMF_OK;
}

VulkanImportTable * DeviceVulkan::GetVulkan()
{
	return &m_ImportTable;
}
