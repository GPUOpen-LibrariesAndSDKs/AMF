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

#include "SwapChainVulkan.h"
#include "CmdLogger.h"
#include <public/common/TraceAdapter.h>
//#define VK_NO_PROTOTYPES

using namespace amf;

#define AMF_FACILITY L"SwapChainVulkan"

SwapChainVulkan::SwapChainVulkan(amf::AMFContext* pContext) :
    SwapChain(pContext),
    m_pContext1(pContext),
    m_hSurfaceKHR(NULL),
    m_uQueuePresentFamilyIndex(UINT32_MAX),
    m_uQueueComputeFamilyIndex(UINT32_MAX),
    m_hQueuePresent(nullptr),
    m_surfaceFormat{},
    m_hSwapChain(NULL),
    m_hRenderPass(NULL),
    m_hCommandPool(NULL),
    m_lastPresentedImageIndex(0)
{
    SetFormat(AMF_SURFACE_UNKNOWN); // Set default format
}

SwapChainVulkan::~SwapChainVulkan()
{
    Terminate();
}

AMF_RESULT SwapChainVulkan::Init(amf_handle hwnd, amf_handle hDisplay, AMFSurface* /*pSurface*/, amf_int32 width, amf_int32 height,
    amf::AMF_SURFACE_FORMAT format, amf_bool fullscreen, amf_bool /*hdr*/, amf_bool /*stereo*/)
{
    AMF_RETURN_IF_FALSE(hwnd != nullptr, AMF_INVALID_ARG, L"CreateSwapChain() - hwnd is NULL");
    //AMF_RETURN_IF_FALSE(hDisplay != nullptr, AMF_INVALID_ARG, L"CreateSwapChain() - hDisplay is NULL");
    m_hwnd = hwnd;
    m_hDisplay = hDisplay;

    //  Get device
    AMFVulkanDevice* pDevice = (AMFVulkanDevice*)m_pContext1->GetVulkanDevice();
    AMF_RESULT res = VulkanContext::Init(pDevice, &m_ImportTable);
    AMF_RETURN_IF_FAILED(res, L"Init() - VulkanContext::Init() failed");

    res = m_ImportTable.LoadFunctionsTable();
    AMF_RETURN_IF_FAILED(res, L"Init() - LoadFunctionsTable() failed - check if the proper Vulkan SDK is installed");

    res = m_ImportTable.LoadInstanceFunctionsTableExt(m_pVulkanDevice->hInstance, false);
    AMF_RETURN_IF_FAILED(res, L"Init() - LoadInstanceFunctionsTableExt() failed - check if the proper Vulkan SDK is installed");

    res = m_ImportTable.LoadDeviceFunctionsTableExt(m_pVulkanDevice->hDevice);
    AMF_RETURN_IF_FAILED(res, L"Init() - LoadDeviceFunctionsTableExt() failed - check if the proper Vulkan SDK is installed");

    res = CreateSurface();
    AMF_RETURN_IF_FAILED(res, L"Init() - CreateSurface() failed");

    res = FindQueue();
    AMF_RETURN_IF_FAILED(res, L"Init() - GetQueueFamilies() failed");

    res = CreateSwapChain(width, height, fullscreen, format);
    AMF_RETURN_IF_FAILED(res, L"Init() - CreateSwapChain() failed");

    res = CreateImageViews();
    AMF_RETURN_IF_FAILED(res, L"Init() - CreateImageViews() failed");

    res = CreateRenderPass();
    AMF_RETURN_IF_FAILED(res, L"Init() - CreateRenderPass() failed");

    res = CreateFrameBuffers();
    AMF_RETURN_IF_FAILED(res, L"Init() - CreateFrameBuffers() failed");

    res = CreateCommandPool();
    AMF_RETURN_IF_FAILED(res, L"Init() - CreateCommandPool() failed");

    res = m_cmdBuffer.Init(m_pVulkanDevice, &m_ImportTable, m_hCommandPool);
    AMF_RETURN_IF_FAILED(res, L"Init() - CommandBuffer Init() failed");

    return AMF_OK;
}

AMF_RESULT SwapChainVulkan::Terminate()
{
    if(m_pVulkanDevice == NULL)
    {
        return AMF_OK;
    }

    AMFContext1::AMFVulkanLocker vkLock(m_pContext1);
    VkResult vkres = GetVulkan()->vkQueueWaitIdle(m_hQueuePresent);
    if (vkres != VK_SUCCESS)
    {
        AMFTraceError(AMF_FACILITY, L"Terminate() - vkQueueWaitIdle() failed");
    }

    AMF_RESULT res = TerminateSwapChain();
    if (res != AMF_OK)
    {
        AMFTraceError(AMF_FACILITY, L"Terminate() - TerminateSwapChain() failed");
    }

    if (m_hSwapChain != NULL)
    {
        GetVulkan()->vkDestroySwapchainKHR(m_pVulkanDevice->hDevice, m_hSwapChain, nullptr);
        m_hSwapChain = NULL;
    }

    m_cmdBuffer.Terminate();

    if (m_hCommandPool != NULL)
    {
        GetVulkan()->vkDestroyCommandPool(m_pVulkanDevice->hDevice, m_hCommandPool, nullptr);
        m_hCommandPool = NULL;
    }
    if (m_hRenderPass != NULL)
    {
        GetVulkan()->vkDestroyRenderPass(m_pVulkanDevice->hDevice, m_hRenderPass, nullptr);
        m_hRenderPass = NULL;
    }
    for (amf_list<VkSemaphore>::iterator it = m_Semaphores.begin(); it != m_Semaphores.end(); it++)
    {
        GetVulkan()->vkDestroySemaphore(m_pVulkanDevice->hDevice, *it, nullptr); // delete copy of semaphore since it could be replaced in surface
    }
    m_Semaphores.clear();

    if (m_hSurfaceKHR != NULL)
    {
        GetVulkan()->vkDestroySurfaceKHR(m_pVulkanDevice->hInstance, m_hSurfaceKHR, nullptr);
        m_hSurfaceKHR = NULL;
    }

    m_hQueuePresent = nullptr;
    m_uQueuePresentFamilyIndex = UINT32_MAX;
    m_uQueueComputeFamilyIndex = UINT32_MAX;

    VulkanContext::Terminate();
    return SwapChain::Terminate();
}

AMF_RESULT SwapChainVulkan::TerminateSwapChain()
{
    if (m_pVulkanDevice == NULL)
    {
        return AMF_OK;
    }

    AMFContext1::AMFVulkanLocker vkLock(m_pContext1);
    VkResult vkres = GetVulkan()->vkQueueWaitIdle(m_hQueuePresent);
    if (vkres != VK_SUCCESS)
    {
        AMFTraceError(AMF_FACILITY, L"TerminateSwapChain() - vkQueueWaitIdle() failed");
    }

    m_cmdBuffer.WaitForExecution();

    // Delete frame buffers and image views
    for (amf_vector<BackBufferBasePtr>::iterator it = m_pBackBuffers.begin(); it != m_pBackBuffers.end(); it++)
    {
        BackBuffer* pBuffer = (BackBuffer*)it->get();
        if (pBuffer->m_hFrameBuffer != NULL)
        {
            GetVulkan()->vkDestroyFramebuffer(m_pVulkanDevice->hDevice, pBuffer->m_hFrameBuffer, nullptr);
        }
        if (pBuffer->m_hImageView != NULL)
        {
            GetVulkan()->vkDestroyImageView(m_pVulkanDevice->hDevice, pBuffer->m_hImageView, nullptr);
        }

        if (pBuffer->m_surface.hImage != NULL)
        {
            // images belong to swapchain, no destroy
        }
    }
    m_pBackBuffers.clear();
    m_acquiredBuffers.clear();
    m_droppedBuffers.clear();
    m_lastPresentedImageIndex = 0;

    m_size = {};
    m_surfaceFormat = {};

    return AMF_OK;
}

AMF_RESULT SwapChainVulkan::CreateSwapChain(amf_int32 width, amf_int32 height, amf_bool fullscreen, AMF_SURFACE_FORMAT format)
{
    fullscreen;
    width;
    height;

    AMF_RETURN_IF_FALSE(m_pVulkanDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateSwapChain() - m_pVulkanDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pVulkanDevice->hDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateSwapChain() - m_pVulkanDevice->hDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_hSurfaceKHR != NULL, AMF_NOT_INITIALIZED, L"CreateSwapChain() - m_hSurfaceKHR is not initialized");

    // Get image count
    VkSurfaceCapabilitiesKHR surfaceCapabilities = {};
    VkResult vkres = GetVulkan()->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_pVulkanDevice->hPhysicalDevice, m_hSurfaceKHR, &surfaceCapabilities);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"CreateSurface() - vkGetPhysicalDeviceSurfaceCapabilitiesKHR() failed");

    amf_uint32 minImageCount = surfaceCapabilities.minImageCount + 1;
    if (surfaceCapabilities.maxImageCount > 0 && minImageCount > surfaceCapabilities.maxImageCount)
    {
        minImageCount = surfaceCapabilities.maxImageCount;
    }

    m_size.width = surfaceCapabilities.currentExtent.width;
    m_size.height = surfaceCapabilities.currentExtent.height;
    VkExtent2D windowExtent = { (amf_uint32)m_size.width, (amf_uint32)m_size.height};

    // Find format
    AMF_RESULT res = SetFormat(format);
    AMF_RETURN_IF_FAILED(res, L"CreateSwapChain() - SetFormat() failed");

    const VkSurfaceFormatKHR surfaceFormats[] = { {m_surfaceFormat.format, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} };
    res = FindSurfaceFormat(surfaceFormats, amf_countof(surfaceFormats), m_surfaceFormat); // VK_FORMAT_R8G8B8A8_UNORM  VK_FORMAT_B8G8R8A8_UNORM
    AMF_RETURN_IF_FAILED(res, L"CreateSwapChain() - FindSurfaceFormat() failed");

    // Find present mode
    // FindPresentMode should never fail since VK_PRESENT_MODE_FIFO_KHR is ALWAYS available
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    VkPresentModeKHR presentModes[] = { VK_PRESENT_MODE_MAILBOX_KHR,  VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR };
    res = FindPresentMode(presentModes, amf_countof(presentModes), presentMode);
    AMF_RETURN_IF_FALSE(res == AMF_OK || res == AMF_NOT_SUPPORTED, res, L"CreateSwapChain() - FindPresentMode() failed");

    // We can pass the old swapchain if we have one to the new one
    // and in some implementations of Vulkan it might be able to
    // "resuse" some parts of it making creation potentially faster
    VkSwapchainKHR hOldSwapChain = m_hSwapChain;

    // create swap chain
    VkSwapchainCreateInfoKHR swapChainCreateInfo = {};
    swapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapChainCreateInfo.surface = m_hSurfaceKHR;
    swapChainCreateInfo.minImageCount = minImageCount;
    swapChainCreateInfo.imageExtent = windowExtent;
    swapChainCreateInfo.imageArrayLayers = 1;
    swapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; //Potentially change for compute queue VK_SHARING_MODE_CONCURRENT
    swapChainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
#if defined(__ANDROID__)
    swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
#else
    swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
#endif
    swapChainCreateInfo.clipped = VK_TRUE;
    swapChainCreateInfo.oldSwapchain = hOldSwapChain;
    swapChainCreateInfo.queueFamilyIndexCount = 1;
    swapChainCreateInfo.pQueueFamilyIndices = &m_uQueuePresentFamilyIndex;
    swapChainCreateInfo.imageFormat = m_surfaceFormat.format;
    swapChainCreateInfo.imageColorSpace = m_surfaceFormat.colorSpace;
    swapChainCreateInfo.presentMode = presentMode;
    swapChainCreateInfo.pNext = nullptr;

    // Create swap chain
    vkres = GetVulkan()->vkCreateSwapchainKHR(m_pVulkanDevice->hDevice, &swapChainCreateInfo, nullptr, &m_hSwapChain);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"CreateSwapChain() - vkCreateSwapchainKHR() failed");

    // The old swapchain becomes retired now so we can delete it
    if (hOldSwapChain != NULL)
    {
        GetVulkan()->vkDestroySwapchainKHR(m_pVulkanDevice->hDevice, hOldSwapChain, nullptr);
        hOldSwapChain = NULL;
    }

    return AMF_OK;
}

AMF_RESULT SwapChainVulkan::CreateSurface()
{
    AMF_RETURN_IF_FALSE(m_pVulkanDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateSurface() - m_pVulkanDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pVulkanDevice->hInstance != nullptr, AMF_NOT_INITIALIZED, L"CreateSurface() - m_pVulkanDevice->hInstance is not initialized");
    AMF_RETURN_IF_FALSE(m_hwnd != nullptr, AMF_NOT_INITIALIZED, L"CreateSurface() - m_hwnd is not initialized");
    AMF_RETURN_IF_FALSE(m_hSurfaceKHR == NULL, AMF_ALREADY_INITIALIZED, L"CreateSurface() - m_hSurfaceKHR is already initialized");

#ifdef _WIN32

    HINSTANCE hModuleInstance = (HINSTANCE)GetModuleHandle(NULL);

    VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.hinstance = hModuleInstance;
    surfaceCreateInfo.hwnd = (HWND)m_hwnd;
    surfaceCreateInfo.pNext = nullptr;

    VkResult vkres = GetVulkan()->vkCreateWin32SurfaceKHR(m_pVulkanDevice->hInstance, &surfaceCreateInfo, nullptr, &m_hSurfaceKHR);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"CreateSurface() - vkCreateWin32SurfaceKHR() failed");

#elif defined(__ANDROID__)
    VkAndroidSurfaceCreateInfoKHR surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.pNext = nullptr;
    surfaceCreateInfo.flags = 0;
    surfaceCreateInfo.window = (struct ANativeWindow*)m_hwnd;

    VkResult vkres = GetVulkan()->vkCreateAndroidSurfaceKHR(m_pVulkanDevice->hInstance, &surfaceCreateInfo, nullptr, &m_hSurfaceKHR);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"CreateSurface() - vkCreateAndroidSurfaceKHR() failed");

#elif defined(__linux)
    AMF_RETURN_IF_FALSE(m_hDisplay != nullptr, AMF_NOT_INITIALIZED, L"CreateSurface() - m_hDisplay is not initialized");

    VkXlibSurfaceCreateInfoKHR surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.pNext = nullptr;
    surfaceCreateInfo.flags = 0;
    surfaceCreateInfo.dpy = (Display*)m_hDisplay;
    surfaceCreateInfo.window = (Window)m_hwnd;

    VkResult vkres = GetVulkan()->vkCreateXlibSurfaceKHR(m_pVulkanDevice->hInstance, &surfaceCreateInfo, nullptr, &m_hSurfaceKHR);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"CreateSurface() - vkCreateXlibSurfaceKHR() failed");
#endif

    return AMF_OK;
}

AMF_RESULT SwapChainVulkan::FindSurfaceFormat(const VkSurfaceFormatKHR* formats, amf_uint count, VkSurfaceFormatKHR& supportedFormat)
{
    AMF_RETURN_IF_FALSE(formats != nullptr, AMF_NOT_INITIALIZED, L"FindPresentMode() - formats is NULL");
    AMF_RETURN_IF_FALSE(count > 0, AMF_NOT_INITIALIZED, L"FindPresentMode() - need at least 1 format to find");
    AMF_RETURN_IF_FALSE(m_pVulkanDevice != nullptr, AMF_NOT_INITIALIZED, L"FindSurfaceFormat() - m_pVulkanDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pVulkanDevice->hPhysicalDevice != nullptr, AMF_NOT_INITIALIZED, L"FindSurfaceFormat() - m_pVulkanDevice->hPhysicalDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_hSurfaceKHR != NULL, AMF_NOT_INITIALIZED, L"FindSurfaceFormat() - m_hSurfaceKHR is not initialized");

    // Get formats
    amf_uint32 formatCount = 0;
    VkResult vkres = GetVulkan()->vkGetPhysicalDeviceSurfaceFormatsKHR(m_pVulkanDevice->hPhysicalDevice, m_hSurfaceKHR, &formatCount, nullptr);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"FindSurfaceFormat() - vkGetPhysicalDeviceSurfaceFormatsKHR() failed");
    AMF_RETURN_IF_FALSE(formatCount > 0, AMF_VULKAN_FAILED, L"FindSurfaceFormat() - vkGetPhysicalDeviceSurfaceFormatsKHR() returned 0 formats");

    amf_vector<VkSurfaceFormatKHR> supportedFormats(formatCount);
    vkres = GetVulkan()->vkGetPhysicalDeviceSurfaceFormatsKHR(m_pVulkanDevice->hPhysicalDevice, m_hSurfaceKHR, &formatCount, supportedFormats.data());
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"FindSurfaceFormat() - vkGetPhysicalDeviceSurfaceFormatsKHR() failed");

    // Find format
    for (amf_uint i = 0; i < count; ++i)
    {
        for (VkSurfaceFormatKHR format : supportedFormats)
        {
            if (format.format == formats[i].format && format.colorSpace == formats[i].colorSpace)
            {
                supportedFormat = formats[i];
                return AMF_OK;
            }
        }
    }

    supportedFormat = supportedFormats[0];
    return AMF_NOT_SUPPORTED;
}

AMF_RESULT SwapChainVulkan::FindPresentMode(const VkPresentModeKHR* modes, amf_uint count, VkPresentModeKHR& supportedMode)
{
    AMF_RETURN_IF_FALSE(modes != nullptr, AMF_NOT_INITIALIZED, L"FindPresentMode() - modes is NULL");
    AMF_RETURN_IF_FALSE(count > 0, AMF_NOT_INITIALIZED, L"FindPresentMode() - need at least 1 mode to find");
    AMF_RETURN_IF_FALSE(m_pVulkanDevice != nullptr, AMF_NOT_INITIALIZED, L"FindPresentMode() - m_pVulkanDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pVulkanDevice->hPhysicalDevice != nullptr, AMF_NOT_INITIALIZED, L"FindPresentMode() - m_pVulkanDevice->hPhysicalDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_hSurfaceKHR != NULL, AMF_NOT_INITIALIZED, L"FindPresentMode() - m_hSurfaceKHR is not initialized");

    // get present modes
    amf_uint32 presentModeCount = 0;

    VkResult vkres = GetVulkan()->vkGetPhysicalDeviceSurfacePresentModesKHR(m_pVulkanDevice->hPhysicalDevice, m_hSurfaceKHR, &presentModeCount, nullptr);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"FindPresentMode() - vkGetPhysicalDeviceSurfacePresentModesKHR() failed");
    AMF_RETURN_IF_FALSE(presentModeCount > 0, AMF_VULKAN_FAILED, L"FindPresentMode() - vkGetPhysicalDeviceSurfacePresentModesKHR() returned 0 modes");

    amf_vector<VkPresentModeKHR> supportedModes(presentModeCount);
    vkres = GetVulkan()->vkGetPhysicalDeviceSurfacePresentModesKHR(m_pVulkanDevice->hPhysicalDevice, m_hSurfaceKHR, &presentModeCount, supportedModes.data());
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"FindPresentMode() - vkGetPhysicalDeviceSurfacePresentModesKHR() failed");

    // find present mode
    for (amf_uint i = 0; i < count; ++i)
    {
        if (std::find(supportedModes.begin(), supportedModes.end(), modes[i]) != supportedModes.end())
        {
            supportedMode = modes[i];
            return AMF_OK;
        }
    }

    supportedMode = supportedModes[0];
    return AMF_NOT_SUPPORTED;
}

AMF_RESULT SwapChainVulkan::FindQueue()
{
    AMF_RETURN_IF_FALSE(m_pVulkanDevice != nullptr, AMF_NOT_INITIALIZED, L"GetQueueFamilies() - m_pVulkanDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pVulkanDevice->hPhysicalDevice != nullptr, AMF_NOT_INITIALIZED, L"GetQueueFamilies() - m_pVulkanDevice->hPhysicalDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pVulkanDevice->hDevice != nullptr, AMF_NOT_INITIALIZED, L"GetQueueFamilies() - (m_pVulkanDevice->hDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_hSurfaceKHR != NULL, AMF_NOT_INITIALIZED, L"GetQueueFamilies() - m_hSurfaceKHR is not initialized");
    AMF_RETURN_IF_FALSE(m_hQueuePresent == nullptr, AMF_ALREADY_INITIALIZED, L"GetQueueFamilies() - m_hQueuePresent is already initialized");

    m_uQueuePresentFamilyIndex = UINT32_MAX;
    m_uQueueComputeFamilyIndex = UINT32_MAX;

    amf_uint32 queueFamilyPropertyCount = 0;
    GetVulkan()->vkGetPhysicalDeviceQueueFamilyProperties(m_pVulkanDevice->hPhysicalDevice, &queueFamilyPropertyCount, nullptr);

    amf_vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyPropertyCount);
    GetVulkan()->vkGetPhysicalDeviceQueueFamilyProperties(m_pVulkanDevice->hPhysicalDevice, &queueFamilyPropertyCount, queueFamilyProperties.data());

    for (amf_uint32 i = 0, queues = 2; i < queueFamilyPropertyCount && queues > 0; i++)
    {
        if (m_uQueuePresentFamilyIndex == UINT32_MAX)
        {
            VkBool32 presentSupport = false;
            VkResult vkres = GetVulkan()->vkGetPhysicalDeviceSurfaceSupportKHR(m_pVulkanDevice->hPhysicalDevice, i, m_hSurfaceKHR, &presentSupport);
            ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"GetQueueFamilies() - vkGetPhysicalDeviceSurfaceSupportKHR() failed");

            constexpr amf_int PRESENT_QUEUE_FLAG_MASK = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT;
            if (presentSupport && (queueFamilyProperties[i].queueFlags & PRESENT_QUEUE_FLAG_MASK) == PRESENT_QUEUE_FLAG_MASK)
            {
                m_uQueuePresentFamilyIndex = i;
                queues--;
            }
        }

        if (m_uQueueComputeFamilyIndex == UINT32_MAX)
        {
            if (queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
            {
                m_uQueueComputeFamilyIndex = i;
                queues--;
            }
        }
    }

    AMF_RETURN_IF_FALSE(m_uQueuePresentFamilyIndex != UINT32_MAX, AMF_FAIL, L"Present queue not found");
    AMF_RETURN_IF_FALSE(m_uQueueComputeFamilyIndex != UINT32_MAX, AMF_FAIL, L"Compute queue not found");

    GetVulkan()->vkGetDeviceQueue(m_pVulkanDevice->hDevice, m_uQueuePresentFamilyIndex, 0, &m_hQueuePresent);
    AMF_RETURN_IF_FALSE(m_hQueuePresent != NULL, AMF_FAIL, L"Present queue not returned");

    return AMF_OK;
}

AMF_RESULT SwapChainVulkan::CreateCommandPool()
{
    AMF_RETURN_IF_FALSE(m_pVulkanDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateCommandPool() - m_pVulkanDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pVulkanDevice->hDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateCommandPool() - m_pVulkanDevice->hDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_uQueuePresentFamilyIndex != UINT32_MAX, AMF_NOT_INITIALIZED, L"CreateCommandPool() - m_uQueuePresentFamilyIndex is not initialized");
    AMF_RETURN_IF_FALSE(m_hCommandPool == NULL, AMF_ALREADY_INITIALIZED, L"CreateCommandPool() - m_hCommandPool is already initialized");

    // Create command pool
    VkCommandPoolCreateInfo poolCreateInfo = {};
        poolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolCreateInfo.queueFamilyIndex = m_uQueuePresentFamilyIndex;
        poolCreateInfo.flags =  VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    VkResult vkres = GetVulkan()->vkCreateCommandPool(m_pVulkanDevice->hDevice, &poolCreateInfo, nullptr, &m_hCommandPool);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"CreateCommandPool() - vkCreateCommandPool() failed");

    return AMF_OK;
}

AMF_RESULT SwapChainVulkan::CreateRenderPass()
{
    AMF_RETURN_IF_FALSE(m_pVulkanDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateRenderPass() - m_pVulkanDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pVulkanDevice->hDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateRenderPass() - m_pVulkanDevice->hDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_hRenderPass == NULL, AMF_ALREADY_INITIALIZED, L"CreateRenderPass() - m_hRenderPass is already initialized");

    VkAttachmentDescription colorAtt = {};
        colorAtt.format = (VkFormat)m_surfaceFormat.format;
        colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        colorAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
//        colorAtt.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
//        colorAtt.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

    VkAttachmentReference colorAttRef = {};
        colorAttRef.attachment = 0;
        colorAttRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subPassDesc = {};
        subPassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subPassDesc.colorAttachmentCount = 1;
        subPassDesc.pColorAttachments = &colorAttRef;

    VkSubpassDependency subDep = {};
        subDep.srcSubpass = VK_SUBPASS_EXTERNAL;
        subDep.dstSubpass = 0;
        subDep.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        subDep.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        subDep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAtt;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subPassDesc;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &subDep;
//        renderPassInfo.dependencyCount = 0;
//        renderPassInfo.pDependencies = NULL;

    VkResult vkres = GetVulkan()->vkCreateRenderPass(m_pVulkanDevice->hDevice, &renderPassInfo, nullptr, &m_hRenderPass);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"CreateRenderPass() - vkCreateRenderPass() failed");
    return AMF_OK;
}

AMF_RESULT SwapChainVulkan::CreateImageView(VkImage hImage, VkFormat format, VkImageView& hImageView)
{
    AMF_RETURN_IF_FALSE(m_pVulkanDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateImageView() - m_pVulkanDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pVulkanDevice->hDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateImageView() - m_pVulkanDevice->hDevice is not initialized");
    AMF_RETURN_IF_FALSE(hImage != NULL, AMF_INVALID_ARG, L"CreateImageView() - hImage is NULL");
    AMF_RETURN_IF_FALSE(hImageView == NULL, AMF_ALREADY_INITIALIZED, L"CreateImageView() - hImageView is already initialized");

    VkImageViewCreateInfo imageViewCreateInfo = {};
    imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCreateInfo.image = hImage;
    imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.format = format;

    imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
    imageViewCreateInfo.subresourceRange.levelCount = 1;
    imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    imageViewCreateInfo.subresourceRange.layerCount = 1;

    VkResult vkres = GetVulkan()->vkCreateImageView(m_pVulkanDevice->hDevice, &imageViewCreateInfo, nullptr, &hImageView);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"CreateImageView() - vkCreateImageView() failed");

    return AMF_OK;
}

AMF_RESULT SwapChainVulkan::CreateImageViews()
{
    AMF_RETURN_IF_FALSE(m_pVulkanDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateImageViews() - m_pVulkanDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pVulkanDevice->hDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateImageViews() - m_pVulkanDevice->hDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_hSwapChain != NULL, AMF_NOT_INITIALIZED, L"CreateImageViews() - m_hSwapChain is not initialized");

    // Store images in the chain
    amf_uint32 imageCount = 0;
    VkResult vkres = GetVulkan()->vkGetSwapchainImagesKHR(m_pVulkanDevice->hDevice, m_hSwapChain, &imageCount, nullptr);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"CreateImageViews() - vkGetSwapchainImagesKHR() failed");

    m_pBackBuffers.resize(imageCount);
    amf_vector<VkImage> images(imageCount);

    vkres = GetVulkan()->vkGetSwapchainImagesKHR(m_pVulkanDevice->hDevice, m_hSwapChain, &imageCount, images.data());
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"CreateImageViews() - vkGetSwapchainImagesKHR() failed");

    for (amf_uint32 i = 0; i < imageCount; i++)
    {
        // Some linux projects are compiled with c++11 so we can't use std::make_unique
        m_pBackBuffers[i] = std::unique_ptr<BackBufferVulkan>(new BackBufferVulkan());

        BackBuffer* pBuffer = (BackBuffer*)m_pBackBuffers[i].get();

        AMF_RESULT res = CreateImageView(images[i], m_surfaceFormat.format, pBuffer->m_hImageView);
        AMF_RETURN_IF_FAILED(res, L"CreateImageViews() - CreateImageView() failed");

        pBuffer->m_surface.cbSizeof = sizeof(amf::AMFVulkanSurface);    // sizeof(AMFVulkanSurface)
        // surface properties
        pBuffer->m_surface.hImage = images[i];
        pBuffer->m_surface.eUsage = static_cast<amf_uint32>(amf::AMF_SURFACE_USAGE_DEFAULT);
        pBuffer->m_surface.eAccess = static_cast<amf_uint32>(amf::AMF_MEMORY_CPU_DEFAULT);
        pBuffer->m_surface.hMemory = 0;
        pBuffer->m_surface.iSize = 0;      // memory size
        pBuffer->m_surface.eFormat = m_surfaceFormat.format;
        pBuffer->m_surface.iWidth = m_size.width;
        pBuffer->m_surface.iHeight = m_size.height;

        pBuffer->m_surface.Sync.cbSizeof = sizeof(pBuffer->m_surface.Sync);
        pBuffer->m_surface.Sync.hSemaphore = VK_NULL_HANDLE;
        pBuffer->m_surface.Sync.bSubmitted = false;

        pBuffer->m_surface.eCurrentLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // VkImageLayout
    }

    // Reuse existing semaphores on resize
    // Resize semaphore list to image count by
    // creating/destroying missing/extra semaphores
    if (imageCount < (amf_uint32)m_Semaphores.size())
    {
        const amf_uint32 extra = imageCount - (amf_uint32)m_Semaphores.size();
        for (amf_uint32 i = 0; i <= extra; ++i)
        {
            GetVulkan()->vkDestroySemaphore(m_pVulkanDevice->hDevice, m_Semaphores.back(), nullptr); // delete copy of semaphore since it could be replaced in surface
            m_Semaphores.pop_back();
        }
    }
    else
    {
        VkSemaphoreCreateInfo semaphoreInfo = {};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        for (amf_uint32 i = (amf_uint32)m_Semaphores.size(); i < imageCount; ++i)
        {
            VkSemaphore hSemaphore = VK_NULL_HANDLE;
            vkres = GetVulkan()->vkCreateSemaphore(m_pVulkanDevice->hDevice, &semaphoreInfo, nullptr, &hSemaphore);
            ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"CreateImageViews() - vkCreateSemaphore() failed");
            m_Semaphores.push_back(hSemaphore);
        }
    }

    return AMF_OK;
}

AMF_RESULT SwapChainVulkan::CreateFrameBuffers()
{
    AMF_RETURN_IF_FALSE(m_pVulkanDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateFrameBuffers() - m_pVulkanDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pVulkanDevice->hDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateFrameBuffers() - m_pVulkanDevice->hDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_hRenderPass != NULL, AMF_NOT_INITIALIZED, L"CreateFrameBuffers() - m_hRenderPass is not initialized");

    for (BackBufferBasePtr& pBackBuffer : m_pBackBuffers)
    {
        BackBuffer* pBuffer = (BackBuffer*)pBackBuffer.get();

        AMF_RETURN_IF_FALSE(pBuffer->m_hImageView != NULL, AMF_NOT_INITIALIZED, L"CreateFrameBuffers() - pBuffer->m_hImageView is not initialized");
        AMF_RETURN_IF_FALSE(pBuffer->m_hFrameBuffer == NULL, AMF_ALREADY_INITIALIZED, L"CreateFrameBuffers() - pBuffer->m_hFrameBuffer is already initialized");

        VkFramebufferCreateInfo frameBufferInfo = {};
            frameBufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            frameBufferInfo.renderPass = m_hRenderPass;
            frameBufferInfo.attachmentCount = 1;
            frameBufferInfo.pAttachments = &pBuffer->m_hImageView;
            frameBufferInfo.width = m_size.width;
            frameBufferInfo.height = m_size.height;
            frameBufferInfo.layers = 1;

        VkResult vkres = GetVulkan()->vkCreateFramebuffer(m_pVulkanDevice->hDevice, &frameBufferInfo, nullptr, &pBuffer->m_hFrameBuffer);
        ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"CreateFrameBuffers() - vkCreateFramebuffer() failed");
    }
    return AMF_OK;
}

AMF_RESULT SwapChainVulkan::Resize(amf_int32 width, amf_int32 height, amf_bool fullscreen, AMF_SURFACE_FORMAT format)
{
    AMF_RETURN_IF_FALSE(m_pVulkanDevice != nullptr, AMF_NOT_INITIALIZED, L"ResizeSwapChain() - m_pVulkanDevice is not initialized");

    if (m_size.width == width && m_size.height == height && m_format == format)
    {
        return AMF_OK;
    }

    AMF_RESULT res = TerminateSwapChain();
    AMF_RETURN_IF_FAILED(res, L"ResizeSwapchain() - TerminateSwapChain() failed");

    VkResult vkres = VK_SUCCESS;
    {
        AMFContext1::AMFVulkanLocker vkLock(m_pContext1);

        vkres = GetVulkan()->vkDeviceWaitIdle(m_pVulkanDevice->hDevice);
    }
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"ResizeSwapChain() - vkDeviceWaitIdle() failed");

    res = CreateSwapChain(width, height, fullscreen, format);
    AMF_RETURN_IF_FAILED(res, L"ResizeSwapchain() - CreateSwapChain() failed");

    res = CreateImageViews();
    AMF_RETURN_IF_FAILED(res, L"ResizeSwapchain() - CreateImageViews() failed");

    // Only recreate render pass if format changed
    if ((VkFormat)format != m_surfaceFormat.format)
    {
        // Todo: Recreating the render pass might require us to recreate the pipeline too
        if (m_hRenderPass != NULL)
        {
            GetVulkan()->vkDestroyRenderPass(m_pVulkanDevice->hDevice, m_hRenderPass, nullptr);
            m_hRenderPass = NULL;
        }
        res = CreateRenderPass();
        AMF_RETURN_IF_FAILED(res, L"ResizeSwapchain() - CreateRenderPass() failed");
    }

    res = CreateFrameBuffers();
    AMF_RETURN_IF_FAILED(res, L"ResizeSwapchain() - CreateFrameBuffers() failed");

    return AMF_OK;
}

amf_bool SwapChainVulkan::FormatSupported(AMF_SURFACE_FORMAT format)
{
    return GetSupportedVkFormat(format) != VK_FORMAT_UNDEFINED;
}

AMF_RESULT SwapChainVulkan::SetFormat(AMF_SURFACE_FORMAT format)
{
    VkFormat vkFormat = GetSupportedVkFormat(format);
    AMF_RETURN_IF_FALSE(vkFormat != VK_FORMAT_UNDEFINED, AMF_NOT_SUPPORTED, L"SetFormat() - Format (%s) not supported", AMFSurfaceGetFormatName(format));

    m_format = format == AMF_SURFACE_UNKNOWN ? AMF_SURFACE_BGRA : format;
    m_surfaceFormat.format = vkFormat;
    return AMF_OK;
}

VkFormat SwapChainVulkan::GetSupportedVkFormat(AMF_SURFACE_FORMAT format)
{
    switch (format)
    {
    case AMF_SURFACE_UNKNOWN:  return VK_FORMAT_B8G8R8A8_UNORM;
    case AMF_SURFACE_BGRA:     return VK_FORMAT_B8G8R8A8_UNORM;
    case AMF_SURFACE_RGBA:     return VK_FORMAT_R8G8B8A8_UNORM;
    }

    return VK_FORMAT_UNDEFINED;
}

AMF_RESULT SwapChainVulkan::AcquireNextBackBufferIndex(amf_uint& index)
{
    AMF_RETURN_IF_FALSE(m_pVulkanDevice != nullptr, AMF_NOT_INITIALIZED, L"AcquireBackBuffer() - m_pVulkanDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pVulkanDevice->hDevice != nullptr, AMF_NOT_INITIALIZED, L"AcquireBackBuffer() - m_pVulkanDevice->hDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_hSwapChain != NULL, AMF_NOT_INITIALIZED, L"AcquireBackBuffer() - m_hSwapChain is not initialized");
    AMF_RETURN_IF_FALSE(m_Semaphores.size() > 0, AMF_FAIL, L"AcquireBackBuffer() - no free semaphore");

    amf_uint32 imageIndex = 0;

    // Vulkan swapchains don't have any method to "return" acquired images
    // If we try to acquire more images than available before presenting
    // it will timeout and return error
    // For dropped frames, we can just store the index in m_droppedImages
    // and pass it back to the caller here for it to be reused.
    if (m_droppedBuffers.empty())
    {
        VkResult vkres = GetVulkan()->vkAcquireNextImageKHR(m_pVulkanDevice->hDevice, m_hSwapChain, UINT32_MAX, m_Semaphores.front(), VK_NULL_HANDLE, &imageIndex);
        AMF_RETURN_IF_FALSE(vkres == VK_SUCCESS || vkres == VK_SUBOPTIMAL_KHR || vkres == VK_ERROR_OUT_OF_DATE_KHR, AMF_VULKAN_FAILED, L"AcquireBackBuffer() - vkAcquireNextImageKHR() failed");
        if (vkres == VK_ERROR_OUT_OF_DATE_KHR)
        {
            return AMF_RESOLUTION_UPDATED;
        }
    }
    else
    {
        imageIndex = m_droppedBuffers.front();
        m_droppedBuffers.pop_front();
    }

    // This should never happen
    AMF_RETURN_IF_FALSE(imageIndex < m_pBackBuffers.size(), AMF_UNEXPECTED, L"AcquireBackBuffer() - Acquired image index (%u) out of bounds, must be in range [0, %zu]", imageIndex, m_pBackBuffers.size());
    BackBuffer* pBuffer = (BackBuffer*)m_pBackBuffers[imageIndex].get();
    pBuffer->m_surface.Sync.hSemaphore = m_Semaphores.front();
    pBuffer->m_surface.Sync.bSubmitted = true;

    m_acquiredBuffers.push_back(imageIndex);
    m_Semaphores.pop_front();
    index = imageIndex;

    return AMF_OK;
}

AMF_RESULT SwapChainVulkan::Present(amf_bool waitForVSync)
{
    AMF_RETURN_IF_FALSE(m_pVulkanDevice != nullptr, AMF_NOT_INITIALIZED, L"Present() - m_pVulkanDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pVulkanDevice->hDevice != nullptr, AMF_NOT_INITIALIZED, L"Present() - m_pVulkanDevice->hDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_hQueuePresent != nullptr, AMF_NOT_INITIALIZED, L"Present() - m_hQueuePresent is not initialized");
    AMF_RETURN_IF_FALSE(m_hSwapChain != NULL, AMF_NOT_INITIALIZED, L"Present() - m_hSwapChain is not initialized");

    if (m_acquiredBuffers.empty())
    {
        return AMF_NEED_MORE_INPUT;
    }

    const amf_uint32 index = m_acquiredBuffers.front();
    BackBuffer* pBuffer = (BackBuffer*)m_pBackBuffers[index].get();

    AMF_RETURN_IF_FALSE(pBuffer->m_surface.Sync.hSemaphore != NULL, AMF_NOT_INITIALIZED, L"Present() - backbuffer surface hSemaphore is not initialized");

    AMF_RESULT res = m_cmdBuffer.StartRecording();
    AMF_RETURN_IF_FAILED(res, L"Present() - Command Buffer StartRecording() failed");

    res = TransitionSurface(&m_cmdBuffer, &pBuffer->m_surface, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    AMF_RETURN_IF_FAILED(res, L"Present() - TransitionSurface(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) failed");

    res = m_cmdBuffer.Execute(m_hQueuePresent);
    AMF_RETURN_IF_FAILED(res, L"Present() - Command Buffer Execute() failed");

    VkSwapchainKHR swapChains[] = { m_hSwapChain };
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    if(pBuffer->m_surface.Sync.bSubmitted)
    {
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &pBuffer->m_surface.Sync.hSemaphore;
        pBuffer->m_surface.Sync.bSubmitted = false;
    }

    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &index;

    VkResult swapchainRes = VK_SUCCESS;
    presentInfo.pResults = &swapchainRes;

    VkResult vkres = GetVulkan()->vkQueuePresentKHR(m_hQueuePresent, &presentInfo);

    if (vkres == VK_ERROR_OUT_OF_HOST_MEMORY || vkres == VK_ERROR_OUT_OF_DEVICE_MEMORY || vkres == VK_ERROR_DEVICE_LOST)
    {
        // With these error codes, the buffer should not be consumed and we can just drop it for next time caller acquires
        res = DropBackBufferIndex(index);
        AMF_RETURN_IF_FAILED(res, L"Present() - DropBackBuffer() failed unexpectedly");
    }
    else
    {
        // Backbuffer was "used"
        m_Semaphores.push_back(pBuffer->m_surface.Sync.hSemaphore);
        pBuffer->m_surface.Sync.hSemaphore = VK_NULL_HANDLE;
        m_acquiredBuffers.pop_front();
    }

    AMF_RETURN_IF_FALSE((vkres == VK_SUCCESS|| vkres == VK_SUBOPTIMAL_KHR), AMF_VULKAN_FAILED, L"Present() - vkQueuePresentKHR() failed");
    AMF_RETURN_IF_FALSE((swapchainRes == VK_SUCCESS || swapchainRes == VK_SUBOPTIMAL_KHR), AMF_VULKAN_FAILED, L"Present() - vkQueuePresentKHR() swapchain present failed");

    m_lastPresentedImageIndex = index;

    //vkDeviceWaitIdle(m_pVulkanDevice->hDevice);
    if (waitForVSync)
    {
        AMFContext1::AMFVulkanLocker vkLock(m_pContext1);
        GetVulkan()->vkQueueWaitIdle(m_hQueuePresent);
    }
    return AMF_OK;

}

AMF_RESULT SwapChainVulkan::DropLastBackBuffer()
{
    if (m_acquiredBuffers.empty())
    {
        return AMF_OK;
    }

    return DropBackBufferIndex(m_acquiredBuffers.back());
}

AMF_RESULT SwapChainVulkan::DropBackBufferIndex(amf_uint index)
{
    if (std::find(m_droppedBuffers.begin(), m_droppedBuffers.end(), index) != m_droppedBuffers.end())
    {
        return AMF_OK;
    }

    amf_list<amf_uint>::iterator acquiredIt = std::find(m_acquiredBuffers.begin(), m_acquiredBuffers.end(), index);
    if (acquiredIt == m_acquiredBuffers.end())
    {
        return AMF_OK;
    }

    m_acquiredBuffers.erase(acquiredIt);
    m_droppedBuffers.push_back(index);

    BackBufferVulkan* pBuffer = (BackBufferVulkan*)m_pBackBuffers[index].get();
    if (pBuffer->m_surface.Sync.hSemaphore != NULL)
    {
        m_Semaphores.push_back(pBuffer->m_surface.Sync.hSemaphore);
        pBuffer->m_surface.Sync.hSemaphore = VK_NULL_HANDLE;
        pBuffer->m_surface.Sync.bSubmitted = false;
    }

    return AMF_OK;
}

AMF_RESULT SwapChainVulkan::BackBufferToSurface(const BackBufferBase* pBuffer, amf::AMFSurface** ppSurface) const
{
    AMF_RESULT res = m_pContext1->CreateSurfaceFromVulkanNative(pBuffer->GetNative(), ppSurface, nullptr);
    AMF_RETURN_IF_FAILED(res, L"BackBufferToSurface() - CreateSurfaceFromVulkanNative() failed");

    AMFComputePtr pCompute;
    res = m_pContext->GetCompute(AMF_MEMORY_VULKAN, &pCompute);
    AMF_RETURN_IF_FAILED(res, L"BackBufferToSurface() - GetCompute() failed");

    AMFPlane* pPlane = (*ppSurface)->GetPlane(AMF_PLANE_PACKED);
    constexpr amf_size origin[3] = { 0, 0, 0 };
    amf_size region[3] = { (amf_size)pPlane->GetWidth(), (amf_size)pPlane->GetHeight(), 0 };
    constexpr amf_uint8 color[4] = { 0,0,0,0 };
    res = pCompute->FillPlane(pPlane, origin, region, color);
    AMF_RETURN_IF_FAILED(res, L"BackBufferToSurface() - FillPlane() failed");

    return AMF_OK;
}

void* SwapChainVulkan::GetSurfaceNative(amf::AMFSurface* pSurface) const
{
    AMFVulkanView* pView = GetPackedSurfaceVulkan(pSurface);
    if (pView == nullptr)
    {
        return nullptr;
    }

    return (void*)pView->pSurface;
}

//-------------------------------------------------------------------------------------------------
//--------------------------------------- VulkanContext -------------------------------------------
//-------------------------------------------------------------------------------------------------
AMF_RESULT VulkanContext::Init(AMFVulkanDevice * pDevice, const VulkanImportTable * pImportTable)
{
    AMF_RETURN_IF_FALSE(m_pVulkanDevice == nullptr, AMF_ALREADY_INITIALIZED, L"Init() - already initialized");
    AMF_RETURN_IF_FALSE(pDevice != nullptr, AMF_INVALID_ARG, L"Init() - pDevice is NULL");
    AMF_RETURN_IF_FALSE(pImportTable != nullptr, AMF_INVALID_ARG, L"Init() - pImportTable is NULL");

    AMF_RETURN_IF_INVALID_POINTER(pDevice, L"Init() - m_pVulkanDevice is NULL");
    AMF_RETURN_IF_INVALID_POINTER(pDevice->hDevice, L"Init() - m_pVulkanDevice->hDevice is NULL");
    AMF_RETURN_IF_INVALID_POINTER(pDevice->hInstance, L"Init() - m_pVulkanDevice->hInstance is NULL");
    AMF_RETURN_IF_INVALID_POINTER(pDevice->hPhysicalDevice, L"Init() - m_pVulkanDevice->hPhysicalDevice is NULL");

    m_pVulkanDevice = pDevice;
    m_pImportTable = pImportTable;

    return AMF_OK;
}

AMF_RESULT VulkanContext::AllocateMemory(const VkMemoryRequirements& memoryRequirements, VkMemoryPropertyFlags memoryProperties, VkDeviceMemory& hMemory) const
{
    VkPhysicalDeviceMemoryProperties memProps = {};
    GetVulkan()->vkGetPhysicalDeviceMemoryProperties(m_pVulkanDevice->hPhysicalDevice, &memProps);

    amf_uint32 memType = 0;
    for (memType = 0; memType < memProps.memoryTypeCount; memType++)
    {
        if (memoryRequirements.memoryTypeBits & (1 << memType))
        {
            if ((memProps.memoryTypes[memType].propertyFlags & memoryProperties) == memoryProperties)
            {
                break;
            }
        }
    }

    AMF_RETURN_IF_FALSE(memType < memProps.memoryTypeCount, AMF_NOT_FOUND, L"AllocateMemory() - vkGetPhysicalDeviceMemoryProperties() failed to provide memory type");

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memoryRequirements.size;
    allocInfo.memoryTypeIndex = memType;

    VkResult vkres = GetVulkan()->vkAllocateMemory(m_pVulkanDevice->hDevice, &allocInfo, nullptr, &hMemory);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"AllocateMemory() - vkAllocateMemory() failed");

    return AMF_OK;
}

AMF_RESULT VulkanContext::MakeBuffer(const void* pData, amf_size size, amf_uint32 use, amf_uint32 props, amf::AMFVulkanBuffer& buffer) const
{
    AMF_RETURN_IF_FALSE(size != 0, AMF_INVALID_ARG, L"MakeBuffer() - Invalid size, cannot be 0");
    AMF_RETURN_IF_FALSE(buffer.hMemory == NULL, AMF_ALREADY_INITIALIZED, L"UpdateBuffer() - buffer memory handle is already initialized");
    AMF_RETURN_IF_FALSE(buffer.hBuffer == NULL, AMF_ALREADY_INITIALIZED, L"UpdateBuffer() - buffer handle is already initialized");

    VkBufferCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        createInfo.size = size;
        createInfo.usage = (VkBufferUsageFlags)use;
        createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult vkres = GetVulkan()->vkCreateBuffer(m_pVulkanDevice->hDevice, &createInfo, nullptr, &buffer.hBuffer);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"MakeBuffer() - vkCreateBuffer() failed");

    VkMemoryRequirements memReqs = {};
    GetVulkan()->vkGetBufferMemoryRequirements(m_pVulkanDevice->hDevice, buffer.hBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo = {};
    AMF_RESULT res = AllocateMemory(memReqs, props, buffer.hMemory);
    AMF_RETURN_IF_FAILED(res, L"MakeBuffer() - AllocateMemory() failed");

    vkres = GetVulkan()->vkBindBufferMemory(m_pVulkanDevice->hDevice, buffer.hBuffer, buffer.hMemory, 0);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"MakeBuffer() - vkBindBufferMemory() failed");

    buffer.iSize = size;
    buffer.iAllocatedSize = memReqs.size;

    if(pData != nullptr)
    {
        res = UpdateBuffer(buffer, pData, size, 0);
        AMF_RETURN_IF_FAILED(res, L"MakeBuffer() - UpdateBuffer() failed");
    }

    return AMF_OK;
}

AMF_RESULT VulkanContext::UpdateBuffer(amf::AMFVulkanBuffer& buffer, const void* pData, amf_size size, amf_size offset) const
{
    return UpdateBuffer(buffer, &pData, 1, &size, &offset);
}

AMF_RESULT VulkanContext::UpdateBuffer(amf::AMFVulkanBuffer& buffer, const void** ppBuffers, amf_uint count, const amf_size* pSizes, const amf_size* pOffsets) const
{
    AMF_RETURN_IF_FALSE(buffer.hMemory != NULL, AMF_INVALID_ARG, L"UpdateBuffer() - buffer memory handle is NULL");
    AMF_RETURN_IF_FALSE(buffer.iSize > 0, AMF_INVALID_ARG, L"UpdateBuffer() - Invalid buffer size=%" AMFPRId64, buffer.iSize);
    AMF_RETURN_IF_FALSE(ppBuffers != nullptr, AMF_INVALID_ARG, L"UpdateBuffer() - pBuffers is NULL");
    AMF_RETURN_IF_FALSE(count > 0, AMF_INVALID_ARG, L"UpdateBuffer() - count should not be 0");
    AMF_RETURN_IF_FALSE(pSizes != nullptr, AMF_INVALID_ARG, L"UpdateBuffer() - pBuffers is NULL");
    AMF_RETURN_IF_FALSE(pOffsets != nullptr, AMF_INVALID_ARG, L"UpdateBuffer() - pOffsets is NULL");

    // Validate
    amf_size minOffset = pOffsets[0];
    amf_size maxOffset = pSizes[0];
    for (amf_uint i = 0; i < count; ++i)
    {
        const amf_size size = pSizes[i];
        const amf_size offset = pOffsets[i];
        const amf_size endOffset = size + offset;

        AMF_RETURN_IF_FALSE(size != 0, AMF_INVALID_ARG, L"UpdateBuffer() - Invalid size at index %u, cannot be 0", i);
        AMF_RETURN_IF_FALSE(endOffset <= (amf_size)buffer.iSize, AMF_OUT_OF_RANGE, L"UpdateBuffer() - At index %d, size (%zu) + offset (%zu) cannot be bigger than buffer size=%" AMFPRId64, i, size, offset, buffer.iSize);

        if (offset < minOffset)
        {
            minOffset = offset;
        }

        if (endOffset > maxOffset)
        {
            maxOffset = endOffset;
        }
    }

    const amf_size mapSize = maxOffset - minOffset;
    void* bufferData = nullptr;
    VkResult vkres = GetVulkan()->vkMapMemory(m_pVulkanDevice->hDevice, buffer.hMemory, minOffset, mapSize, 0, &bufferData);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"UpdateBuffer() - vkMapMemory() failed");

    for (amf_uint i = 0; i < count; ++i)
    {
        const amf_size offset = pOffsets[i] - minOffset;
        const amf_uint8* pSrcData = (amf_uint8*)ppBuffers[i];
        amf_uint8* pDstData = (amf_uint8*)bufferData + offset;

        memcpy(pDstData, pSrcData, pSizes[i]);
    }

    GetVulkan()->vkUnmapMemory(m_pVulkanDevice->hDevice, buffer.hMemory);

    return AMF_OK;
}

AMF_RESULT VulkanContext::DestroyBuffer(amf::AMFVulkanBuffer& buffer) const
{
    if(buffer.hBuffer != NULL)
    {
        AMF_RETURN_IF_FALSE(m_pVulkanDevice != nullptr, AMF_NOT_INITIALIZED, L"DestroyBuffer() - m_pVulkanDevice is not initialized");
        AMF_RETURN_IF_FALSE(m_pVulkanDevice->hDevice != nullptr, AMF_NOT_INITIALIZED, L"DestroyBuffer() - m_pVulkanDevice->hDevice is not initialized");
        AMF_RETURN_IF_FALSE(buffer.hMemory != NULL, AMF_NOT_INITIALIZED, L"DestroyBuffer() - buffer memory handle is not initialized");

        if (buffer.Sync.hFence != NULL)
        {
            VkResult vkres = GetVulkan()->vkWaitForFences(m_pVulkanDevice->hDevice, 1, &buffer.Sync.hFence, VK_TRUE, 1000000000LL); // timeout is in nanoseconds
            ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"DestroyBuffer() - vkWaitForFences() failed");
            buffer.Sync.hFence = NULL;
        }

        GetVulkan()->vkDestroyBuffer(m_pVulkanDevice->hDevice, buffer.hBuffer, nullptr);
        GetVulkan()->vkFreeMemory(m_pVulkanDevice->hDevice, buffer.hMemory, nullptr);
        memset(&buffer, 0, sizeof(buffer));
    }

    return AMF_OK;
}

AMF_RESULT VulkanContext::CreateSurface(amf_uint32 queueIndex, amf_uint32 width, amf_uint32 height, VkFormat format, amf_uint32 usage, amf_uint32 memoryProperties, amf::AMFVulkanSurface& surface) const
{
    AMF_RETURN_IF_FALSE(width != 0 && height != 0, AMF_INVALID_ARG, L"CreateSurface() - Invalid width/height: %ux%u", width, height);
    AMF_RETURN_IF_FALSE(surface.hMemory == NULL, AMF_ALREADY_INITIALIZED, L"CreateSurface() - surface memory handle is already initialized");
    AMF_RETURN_IF_FALSE(surface.hImage == NULL, AMF_ALREADY_INITIALIZED, L"CreateSurface() - surface handle is already initialized");
    AMF_RETURN_IF_FALSE(m_pVulkanDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateSurface() - m_pVulkanDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pVulkanDevice->hDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateSurface() - m_pVulkanDevice->hDevice is not initialized");

    surface = {};
    surface.cbSizeof = sizeof(amf::AMFVulkanSurface);
    surface.eUsage = usage;
    surface.eFormat = format;
    surface.iWidth = width;
    surface.iHeight = height;
    surface.Sync.cbSizeof = sizeof(surface.Sync);
    surface.Sync.hSemaphore = VK_NULL_HANDLE;
    surface.Sync.bSubmitted = false;
    surface.eCurrentLayout = VK_IMAGE_LAYOUT_GENERAL; // VkImageLayout

    VkImageCreateInfo imageCreateInfo = {};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.pNext = nullptr;
    imageCreateInfo.flags = 0;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = (VkFormat)surface.eFormat;
    imageCreateInfo.extent.width = surface.iWidth;
    imageCreateInfo.extent.height = surface.iHeight;
    imageCreateInfo.extent.depth = 1;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
    imageCreateInfo.usage = surface.eUsage;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // VK_SHARING_MODE_CONCURRENT
    imageCreateInfo.queueFamilyIndexCount = 1;
    imageCreateInfo.pQueueFamilyIndices = &queueIndex;
    imageCreateInfo.initialLayout = (VkImageLayout)surface.eCurrentLayout;

    VkResult vkres = GetVulkan()->vkCreateImage(m_pVulkanDevice->hDevice, &imageCreateInfo, nullptr, &surface.hImage);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"CreateSurface() - vkCreateImage() failed to create image");

    VkMemoryRequirements memoryRequirements;
    GetVulkan()->vkGetImageMemoryRequirements(m_pVulkanDevice->hDevice, surface.hImage, &memoryRequirements);

    AMF_RESULT res = AllocateMemory(memoryRequirements, memoryProperties, surface.hMemory);
    AMF_RETURN_IF_FAILED(res, L"CreateSurface() - AllocateImageMemory() failed");

    vkres = GetVulkan()->vkBindImageMemory(m_pVulkanDevice->hDevice, surface.hImage, surface.hMemory, 0);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"CreateSurface() - vkBindImageMemory() failed");

    surface.iSize = memoryRequirements.size;

    return AMF_OK;
}

AMF_RESULT VulkanContext::DestroySurface(amf::AMFVulkanSurface& surface) const
{
    if (surface.hImage != NULL)
    {
        AMF_RETURN_IF_FALSE(m_pVulkanDevice != nullptr, AMF_NOT_INITIALIZED, L"DestroySurface() - m_pVulkanDevice is not initialized");
        AMF_RETURN_IF_FALSE(m_pVulkanDevice->hDevice != nullptr, AMF_NOT_INITIALIZED, L"DestroySurface() - m_pVulkanDevice->hDevice is not initialized");
        AMF_RETURN_IF_FALSE(surface.hMemory != NULL, AMF_NOT_INITIALIZED, L"DestroySurface() - surface memory handle is not initialized");

        if (surface.Sync.hFence != NULL)
        {
            VkResult vkres = GetVulkan()->vkWaitForFences(m_pVulkanDevice->hDevice, 1, &surface.Sync.hFence, VK_TRUE, 1000000000LL); // timeout is in nanoseconds
            ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"DestroySurface() - vkWaitForFences() failed");
            surface.Sync.hFence = NULL;
        }

        GetVulkan()->vkDestroyImage(m_pVulkanDevice->hDevice, surface.hImage, nullptr);
        GetVulkan()->vkFreeMemory(m_pVulkanDevice->hDevice, surface.hMemory, nullptr);
        surface = {};
    }

    return AMF_OK;
}

AMF_RESULT VulkanContext::TransitionSurface(CommandBufferVulkan* pBuffer, amf::AMFVulkanSurface* pSurface, VkImageLayout layout) const
{
    AMF_RETURN_IF_FALSE(pBuffer != nullptr, AMF_INVALID_ARG, L"TransitionSurface() - pBuffer is NULL");
    AMF_RETURN_IF_FALSE(pBuffer->GetBuffer() != nullptr, AMF_INVALID_ARG, L"TransitionSurface() - pBuffer->GetBuffer() is NULL");

    AMF_RETURN_IF_FALSE(pSurface != nullptr, AMF_INVALID_ARG, L"TransitionSurface() - pSurface is NULL");

    if (layout >= 0 && pSurface->eCurrentLayout == static_cast<amf_uint32>(layout))
    {
        return AMF_OK;
    }

    VkImageMemoryBarrier barrier = {};

    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = (VkImageLayout)pSurface->eCurrentLayout;
    pSurface->eCurrentLayout = layout;
    barrier.newLayout = (VkImageLayout)pSurface->eCurrentLayout;
    barrier.image = pSurface->hImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    barrier.srcAccessMask = 0;
    switch (barrier.oldLayout)
    {
    case VK_IMAGE_LAYOUT_UNDEFINED: barrier.srcAccessMask = 0; break;
    case VK_IMAGE_LAYOUT_PREINITIALIZED: barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT; break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; break;
    default:barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; break;
    }

    barrier.dstAccessMask = 0;
    switch (barrier.newLayout)
    {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        barrier.srcAccessMask = barrier.srcAccessMask | VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        barrier.dstAccessMask = barrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        if (barrier.srcAccessMask == 0)
        {
            barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        }
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        break;
    }
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    AMF_RESULT res = pBuffer->SyncResource(&pSurface->Sync, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    AMF_RETURN_IF_FAILED(res, L"TransitionResource() - SyncResource() failed");

    GetVulkan()->vkCmdPipelineBarrier(pBuffer->GetBuffer(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    return AMF_OK;
}


//-------------------------------------------------------------------------------------------------
//--------------------------------------- Command Buffer ------------------------------------------
//-------------------------------------------------------------------------------------------------
CommandBufferVulkan::CommandBufferVulkan() :
    VulkanContext(),
    m_hCommandPool(NULL),
    m_hCmdBuffer(nullptr),
    m_hFence(NULL),
    m_recording(false),
    m_fenceSignaled(false)
{
}

CommandBufferVulkan::~CommandBufferVulkan()
{
    Terminate();
}

AMF_RESULT CommandBufferVulkan::Init(amf::AMFVulkanDevice* pDevice, const VulkanImportTable* pImportTable, VkCommandPool hCommandPool)
{
    AMF_RESULT res = VulkanContext::Init(pDevice, pImportTable);
    AMF_RETURN_IF_FAILED(res, L"Init() - VulkanContext::Init() failed");

    AMF_RETURN_IF_FALSE(hCommandPool != NULL, AMF_INVALID_ARG, L"Init() - hCommandPool is NULL");
    m_hCommandPool = hCommandPool;

    res = CreateBuffer();
    if (res != AMF_OK)
    {
        Terminate();
    }
    AMF_RETURN_IF_FAILED(res, L"Init() - CreateBuffer() failed");

    return AMF_OK;
}

AMF_RESULT CommandBufferVulkan::Terminate()
{
    if (m_hCmdBuffer != nullptr)
    {
        GetVulkan()->vkFreeCommandBuffers(m_pVulkanDevice->hDevice, m_hCommandPool, 1, &m_hCmdBuffer);
        m_hCmdBuffer = nullptr;
        m_hCommandPool = NULL;
    }
    if (m_hFence != NULL)
    {
        VkResult vkres = GetVulkan()->vkWaitForFences(m_pVulkanDevice->hDevice, 1, &m_hFence, VK_FALSE, 1000000000LL); // timeout is in nanoseconds
        if (vkres != VK_SUCCESS)
        {
            AMFTraceError(AMF_FACILITY, L"Terminate() - vkWaitForFences() failed");
        }

        vkres = GetVulkan()->vkResetFences(m_pVulkanDevice->hDevice, 1, &m_hFence);
        if (vkres != VK_SUCCESS)
        {
            AMFTraceError(AMF_FACILITY, L"Terminate() - vkResetFences() failed");
        }

        GetVulkan()->vkDestroyFence(m_pVulkanDevice->hDevice, m_hFence, nullptr);
        m_hFence = NULL;
    }

    m_waitSemaphores.clear();
    m_signalSemaphores.clear();
    m_waitFlags.clear();
    m_pSyncFences.clear();
    m_recording = false;
    m_fenceSignaled = false;

    return VulkanContext::Terminate();
}

AMF_RESULT CommandBufferVulkan::StartRecording(amf_bool reset)
{
    if (m_recording == true && reset == false)
    {
        return AMF_OK;
    }

    AMF_RESULT res = Reset();
    AMF_RETURN_IF_FAILED(res, L"StartRecording() - Reset() failed");

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    VkResult vkres = GetVulkan()->vkBeginCommandBuffer(m_hCmdBuffer, &beginInfo);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"StartRecording() - vkBeginCommandBuffer() failed");
    m_recording = true;

    return AMF_OK;
}

AMF_RESULT CommandBufferVulkan::EndRecording()
{
    AMF_RETURN_IF_FALSE(m_hCmdBuffer != nullptr, AMF_NOT_INITIALIZED, L"EndRecording() - m_hCmdBuffer is not initialized");

    if (m_recording == false)
    {
        return AMF_OK;
    }

    VkResult vkres = GetVulkan()->vkEndCommandBuffer(m_hCmdBuffer);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"EndRecording() - vkEndCommandBuffer() failed");
    m_recording = false;

    return AMF_OK;
}

AMF_RESULT CommandBufferVulkan::Execute(VkQueue hQueue)
{
    AMF_RETURN_IF_FALSE(hQueue != nullptr, AMF_INVALID_ARG, L"Execute() - hQueue is NULL");
    AMF_RETURN_IF_FALSE(m_hCmdBuffer != nullptr, AMF_NOT_INITIALIZED, L"Execute() - m_hCmdBuffer is not initialized");
    AMF_RETURN_IF_FALSE(m_hFence != NULL, AMF_NOT_INITIALIZED, L"Execute() - m_hFence is not initialized");

    AMF_RESULT res = EndRecording();
    AMF_RETURN_IF_FAILED(res, L"Execute() - EndRecording() failed");

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    if (m_waitSemaphores.size() > 0)
    {
        submitInfo.waitSemaphoreCount = (amf_uint32)m_waitSemaphores.size();
        submitInfo.pWaitSemaphores = &m_waitSemaphores[0];
        submitInfo.pWaitDstStageMask = &m_waitFlags[0];
    }

    if (m_signalSemaphores.size() > 0)
    {
        submitInfo.signalSemaphoreCount = (amf_uint32)m_signalSemaphores.size();
        submitInfo.pSignalSemaphores = &m_signalSemaphores[0];
    }
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_hCmdBuffer;

    VkResult vkres = GetVulkan()->vkQueueSubmit(hQueue, 1, &submitInfo, m_hFence);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"Execute() - vkQueueSubmit() failed");

    m_fenceSignaled = true;

    for (VkFence* pFence : m_pSyncFences)
    {
        *pFence = m_hFence;
    }

    // Reset for next time
    m_waitSemaphores.clear();
    m_waitFlags.clear();
    m_signalSemaphores.clear();
    m_pSyncFences.clear();

    return AMF_OK;
}

AMF_RESULT CommandBufferVulkan::SyncResource(amf::AMFVulkanSync* pSync, VkPipelineStageFlags waitFlags)
{
    AMF_RETURN_IF_FALSE(pSync != nullptr, AMF_INVALID_ARG, L"SyncResource() - pSync is NULL");
    AMF_RETURN_IF_FALSE(m_hCmdBuffer != nullptr, AMF_NOT_INITIALIZED, L"SyncResource() - m_hCmdBuffer is not initialized");

    if (pSync->hSemaphore != VK_NULL_HANDLE)
    {
        amf_vector<VkSemaphore>::iterator it = std::find(m_signalSemaphores.begin(), m_signalSemaphores.end(), pSync->hSemaphore);
        if (it == m_signalSemaphores.end())
        {
            if (pSync->bSubmitted)
            {
                m_waitSemaphores.push_back(pSync->hSemaphore);
                m_waitFlags.push_back(waitFlags);
                pSync->bSubmitted = false;
            }

            m_signalSemaphores.push_back(pSync->hSemaphore);
            pSync->bSubmitted = true;
        }
        else
        {
            amf_vector<VkSemaphore>::iterator waitIt = std::find(m_waitSemaphores.begin(), m_waitSemaphores.end(), pSync->hSemaphore);
            if (waitIt != m_waitSemaphores.end())
            {
                const amf_uint index = amf_uint(waitIt - m_waitSemaphores.begin());
                m_waitFlags[index] |= waitFlags;
            }
        }
    }

    if (std::find(m_pSyncFences.begin(), m_pSyncFences.end(), &pSync->hFence) == m_pSyncFences.end())
    {
        m_pSyncFences.push_back(&pSync->hFence);
    }

    return AMF_OK;
}

AMF_RESULT CommandBufferVulkan::WaitForExecution()
{
    AMF_RETURN_IF_FALSE(m_pVulkanDevice != nullptr, AMF_NOT_INITIALIZED, L"WaitForExecution() - m_pVulkanDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pVulkanDevice->hDevice != nullptr, AMF_NOT_INITIALIZED, L"WaitForExecution() - m_pVulkanDevice->hDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_hFence != NULL, AMF_NOT_INITIALIZED, L"WaitForExecution() - m_hFence is not initialized");

    if (m_fenceSignaled == true)
    {
        // wait for the previous command buffer to complete - just in case
        VkResult vkres = GetVulkan()->vkWaitForFences(m_pVulkanDevice->hDevice, 1, &m_hFence, VK_FALSE, 1000000000LL); // timeout is in nanoseconds
        ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"WaitForExecution() - vkWaitForFences() failed");
        m_fenceSignaled = false;
    }

    return AMF_OK;
}

AMF_RESULT CommandBufferVulkan::CreateBuffer()
{
    AMF_RETURN_IF_FALSE(m_pVulkanDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateBuffer() - m_pVulkanDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pVulkanDevice->hDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateBuffer() - m_pVulkanDevice->hDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_hCmdBuffer == nullptr, AMF_ALREADY_INITIALIZED, L"CreateBuffer() - m_hCmdBuffer is already initialized");
    AMF_RETURN_IF_FALSE(m_hFence == NULL, AMF_ALREADY_INITIALIZED, L"CreateBuffer() - m_hFence is already initialized");

    VkCommandBufferAllocateInfo bufferAllocInfo = {};
    bufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    bufferAllocInfo.commandPool = m_hCommandPool;
    bufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    bufferAllocInfo.commandBufferCount = 1;

    VkResult vkres = GetVulkan()->vkAllocateCommandBuffers(m_pVulkanDevice->hDevice, &bufferAllocInfo, &m_hCmdBuffer);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"CreateBuffer() - vkAllocateCommandBuffers() failed");

    VkFenceCreateInfo fenceCreateInfo = {};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    vkres = GetVulkan()->vkCreateFence(m_pVulkanDevice->hDevice, &fenceCreateInfo, nullptr, &m_hFence);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"CreateBuffer() - vkCreateFence() failed");
    m_fenceSignaled = false;
    m_recording = false;

    return AMF_OK;
}

AMF_RESULT CommandBufferVulkan::Reset()
{
    AMF_RETURN_IF_FALSE(m_pVulkanDevice != nullptr, AMF_NOT_INITIALIZED, L"Reset() - m_pVulkanDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_pVulkanDevice->hDevice != nullptr, AMF_NOT_INITIALIZED, L"Reset() - m_pVulkanDevice->hDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_hCmdBuffer != nullptr, AMF_NOT_INITIALIZED, L"Reset() - m_hCmdBuffer is not initialized");
    AMF_RETURN_IF_FALSE(m_hFence != NULL, AMF_NOT_INITIALIZED, L"Reset() - m_hFence is not initialized");

    AMF_RESULT res = EndRecording();
    AMF_RETURN_IF_FAILED(res, L"Reset() - EndRecording() failed");

    res = WaitForExecution();
    AMF_RETURN_IF_FAILED(res, L"Reset() - WaitForExecution() failed");

    VkResult vkres = GetVulkan()->vkResetFences(m_pVulkanDevice->hDevice, 1, &m_hFence);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"Reset() - vkResetFences() failed");

    vkres = GetVulkan()->vkResetCommandBuffer(m_hCmdBuffer, 0);
    ASSERT_RETURN_IF_VK_FAILED(vkres, AMF_VULKAN_FAILED, L"Reset() - vkResetCommandBuffer() failed");

    m_waitSemaphores.clear();
    m_signalSemaphores.clear();
    m_waitFlags.clear();
    m_pSyncFences.clear();
    m_recording = false;
    m_fenceSignaled = false;

    return AMF_OK;
}
