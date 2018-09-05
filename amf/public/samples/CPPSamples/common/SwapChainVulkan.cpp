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
//#define VK_NO_PROTOTYPES


SwapChainVulkan::SwapChainVulkan(amf::AMFContext* pContext) :
    m_pContext(pContext),
    m_hVulkanDev(NULL),
    m_hSurfaceKHR(NULL),
    m_uQueuePresentFamilyIndex(UINT32_MAX),
    m_uQueueComputeFamilyIndex(UINT32_MAX),
    m_hQueuePresent(NULL),
    m_eSwapChainImageFormat(VK_FORMAT_UNDEFINED),
    m_hSwapChain(NULL),
    m_SwapChainExtent(AMFConstructSize(0, 0)),
    m_hRenderPass(NULL),
    m_hCommandPool(NULL),
    m_hTransitionCommandBuffer(NULL),
    m_hTransitionWaitFence(NULL)
{

}
SwapChainVulkan::~SwapChainVulkan()
{
    Terminate();
}

AMF_RESULT SwapChainVulkan::Init(amf_handle hWnd, amf_handle hDisplay, bool bFullScreen, amf_int32 width, amf_int32 height, amf_uint32 format)
{
    AMF_RESULT res = AMF_OK;
    CHECK_RETURN(width != 0 && height != 0, AMF_FAIL, L"Bad width/height: width=" << width << L"height=" << height);
    m_SwapChainExtent = AMFConstructSize(width, height);

    m_hVulkanDev = m_pContext->GetVulkanDevice();

    CHECK_RETURN(m_hVulkanDev != NULL, AMF_FAIL, L"GetVulkanDevice() returned NULL");
    
    res = m_ImportTable.LoadFunctionsTable();
    CHECK_AMF_ERROR_RETURN(res, L"LoadFunctionsTable() failed - check if the proper Vulkan SDK is installed");
    
    res = m_ImportTable.LoadInstanceFunctionsTableExt(((amf::AMFVulkanDevice*) m_hVulkanDev)->hInstance, false);
    CHECK_AMF_ERROR_RETURN(res, L"LoadInstanceFunctionsTableExt() failed - check if the proper Vulkan SDK is installed");
    
    res = m_ImportTable.LoadDeviceFunctionsTableExt( ((amf::AMFVulkanDevice*) m_hVulkanDev)->hDevice);
    CHECK_AMF_ERROR_RETURN(res, L"LoadDeviceFunctionsTableExt() failed - check if the proper Vulkan SDK is installed");
    
    res = CreateSwapChain(hWnd, hDisplay, format);
    CHECK_AMF_ERROR_RETURN(res, L"CreateSwapChain() failed");

    res = CreateRenderPass();
    CHECK_AMF_ERROR_RETURN(res, L"CreateRenderPass() failed");

    res = CreateFrameBuffers();
    CHECK_AMF_ERROR_RETURN(res, L"CreateFrameBuffers() failed");

    res = CreateCommandPool();
    CHECK_AMF_ERROR_RETURN(res, L"CreateCommandPool() failed");

    return AMF_OK;
}

AMF_RESULT SwapChainVulkan::Terminate()
{
    if(m_hVulkanDev == NULL)
    {
        return AMF_OK;
    }

    amf::AMFVulkanDevice* pVulkanDev = (amf::AMFVulkanDevice*)m_hVulkanDev;  

    GetVulkan()->vkDeviceWaitIdle(pVulkanDev->hDevice);

    if(m_hTransitionCommandBuffer != NULL)
    {
        GetVulkan()->vkFreeCommandBuffers(pVulkanDev->hDevice, m_hCommandPool, 1, &m_hTransitionCommandBuffer);
        m_hTransitionCommandBuffer = NULL;
    }
    if(m_hTransitionWaitFence != NULL)
    {
        GetVulkan()->vkWaitForFences(pVulkanDev->hDevice, 1,  &m_hTransitionWaitFence, VK_FALSE, 1000000000LL ); // timeout is in nanoseconds
        GetVulkan()->vkResetFences(pVulkanDev->hDevice, 1, &m_hTransitionWaitFence);
        GetVulkan()->vkDestroyFence(pVulkanDev->hDevice, m_hTransitionWaitFence, nullptr);
        m_hTransitionWaitFence = NULL;
    }


    if (m_hCommandPool != NULL)
    {
        GetVulkan()->vkDestroyCommandPool(pVulkanDev->hDevice, m_hCommandPool, nullptr);
        m_hCommandPool = NULL;
    }

    if (m_hRenderPass != NULL)
    {
        GetVulkan()->vkDestroyRenderPass(pVulkanDev->hDevice, m_hRenderPass, nullptr);
        m_hRenderPass = NULL;
    }

    for(std::vector<BackBuffer>::iterator it = m_BackBuffers.begin(); it != m_BackBuffers.end(); it++)
    {
        if(it->m_hFrameBuffer != NULL)
        {
            GetVulkan()->vkDestroyFramebuffer(pVulkanDev->hDevice, it->m_hFrameBuffer, nullptr);
        }
        if(it->m_hImageView != NULL)
        {
            GetVulkan()->vkDestroyImageView(pVulkanDev->hDevice, it->m_hImageView, nullptr);
        }

        if(it->m_Surface.hImage != NULL)
        {
            // images belong to swapchain, no destroy
        }
    }
    m_BackBuffers.clear();

    for(std::list<VkSemaphore>::iterator it = m_Semaphores.begin(); it != m_Semaphores.end(); it++)
    {
        GetVulkan()->vkDestroySemaphore(pVulkanDev->hDevice, *it, nullptr); // delete copy of semaphore since it could be replaced in surface
    }
    m_Semaphores.clear();

    if (m_hSwapChain != NULL)
    {
        GetVulkan()->vkDestroySwapchainKHR(pVulkanDev->hDevice, m_hSwapChain, nullptr);
        m_hSwapChain = NULL;
    }
    m_eSwapChainImageFormat = 0;
    m_hQueuePresent = NULL;
    m_uQueuePresentFamilyIndex = UINT32_MAX;
    m_uQueueComputeFamilyIndex = UINT32_MAX;

    if (m_hSurfaceKHR != NULL)
    {
        GetVulkan()->vkDestroySurfaceKHR(pVulkanDev->hInstance, m_hSurfaceKHR, nullptr);
        m_hSurfaceKHR = NULL;
    }
    m_hVulkanDev = NULL;

    return AMF_OK;
}
AMF_RESULT SwapChainVulkan::CreateSwapChain(amf_handle hWnd, amf_handle hDisplay, amf_uint32 format)
{
    amf::AMFVulkanDevice* pVulkanDev = (amf::AMFVulkanDevice*)m_hVulkanDev;    
    VkResult res = VK_INCOMPLETE;

#ifdef _WIN32
    HINSTANCE hModuleInstance = (HINSTANCE)GetModuleHandle(NULL);

    VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.hinstance = hModuleInstance;
    surfaceCreateInfo.hwnd = (HWND)hWnd;
    surfaceCreateInfo.pNext = nullptr;

    res = GetVulkan()->vkCreateWin32SurfaceKHR(pVulkanDev->hInstance, &surfaceCreateInfo, nullptr, &m_hSurfaceKHR);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkCreateWin32SurfaceKHR() failed with error=" << res);
    
#elif defined(__linux)

    VkXlibSurfaceCreateInfoKHR surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.pNext = nullptr;;
    surfaceCreateInfo.flags = 0;
    surfaceCreateInfo.dpy = (Display*)hDisplay;
    surfaceCreateInfo.window = (Window)hWnd;

    res = GetVulkan()->vkCreateXlibSurfaceKHR(pVulkanDev->hInstance, &surfaceCreateInfo, nullptr, &m_hSurfaceKHR);
#endif    

    VkSurfaceCapabilitiesKHR surfaceCapabilities = {};
    res = GetVulkan()->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pVulkanDev->hPhysicalDevice, m_hSurfaceKHR, &surfaceCapabilities);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkGetPhysicalDeviceSurfaceCapabilitiesKHR() failed with error=" << res);

    // get formats
    uint32_t formatCount = 0;

    res = GetVulkan()->vkGetPhysicalDeviceSurfaceFormatsKHR(pVulkanDev->hPhysicalDevice, m_hSurfaceKHR, &formatCount, nullptr);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkGetPhysicalDeviceSurfaceFormatsKHR() failed with error=" << res);
    CHECK_RETURN(formatCount > 0, AMF_FAIL, L"vkGetPhysicalDeviceSurfaceFormatsKHR() returned 0 formats");

    std::vector<VkSurfaceFormatKHR> formats;
    formats.resize(formatCount);

    res = GetVulkan()->vkGetPhysicalDeviceSurfaceFormatsKHR(pVulkanDev->hPhysicalDevice, m_hSurfaceKHR, &formatCount, formats.data());
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkGetPhysicalDeviceSurfaceFormatsKHR() failed with error=" << res);

    // get present modes
    uint32_t presentModeCount = 0;

    res = GetVulkan()->vkGetPhysicalDeviceSurfacePresentModesKHR(pVulkanDev->hPhysicalDevice, m_hSurfaceKHR, &presentModeCount, nullptr);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkGetPhysicalDeviceSurfacePresentModesKHR() failed with error=" << res);
    CHECK_RETURN(presentModeCount > 0, AMF_FAIL, L"vkGetPhysicalDeviceSurfacePresentModesKHR() returned 0 modes");

    std::vector<VkPresentModeKHR> presentMode;
    presentMode.resize(presentModeCount);

    res = GetVulkan()->vkGetPhysicalDeviceSurfacePresentModesKHR(pVulkanDev->hPhysicalDevice, m_hSurfaceKHR, &presentModeCount, presentMode.data());
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkGetPhysicalDeviceSurfacePresentModesKHR() failed with error=" << res);

    uint32_t imageCount = surfaceCapabilities.minImageCount + 1;
    if (surfaceCapabilities.maxImageCount > 0 && imageCount > surfaceCapabilities.maxImageCount)
    {
        imageCount = surfaceCapabilities.maxImageCount;
    }

    // find present queue
    m_uQueuePresentFamilyIndex = UINT32_MAX;
    m_uQueueComputeFamilyIndex = UINT32_MAX;

    uint32_t queueFamilyPropertyCount = 0;
    GetVulkan()->vkGetPhysicalDeviceQueueFamilyProperties(pVulkanDev->hPhysicalDevice, &queueFamilyPropertyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilyProperties{ queueFamilyPropertyCount };
    GetVulkan()->vkGetPhysicalDeviceQueueFamilyProperties(pVulkanDev->hPhysicalDevice, &queueFamilyPropertyCount, queueFamilyProperties.data());
    for (uint32_t i = 0; i < queueFamilyPropertyCount; i++)
    {
        VkQueueFamilyProperties &queueFamilyProperty = queueFamilyProperties[i];
        VkBool32 presentSupport = false;
        res = GetVulkan()->vkGetPhysicalDeviceSurfaceSupportKHR(pVulkanDev->hPhysicalDevice, i, m_hSurfaceKHR, &presentSupport);

        if (presentSupport && queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT)
        {
            if(m_uQueuePresentFamilyIndex == UINT32_MAX)
            {
                m_uQueuePresentFamilyIndex = i;
            }
        }
        if (queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
        {
            if(m_uQueueComputeFamilyIndex == UINT32_MAX)
            { 
                m_uQueueComputeFamilyIndex = i;
            }
        }
    }
    CHECK_RETURN(m_uQueuePresentFamilyIndex != UINT32_MAX, AMF_FAIL, L"Present queue not found");


    GetVulkan()->vkGetDeviceQueue(pVulkanDev->hDevice, m_uQueuePresentFamilyIndex , 0, &m_hQueuePresent);
    CHECK_RETURN(m_hQueuePresent!= NULL, AMF_FAIL, L"Present queue not returned");


    // create swap chain
    VkSwapchainCreateInfoKHR swapChainCreateInfo = {};
    swapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapChainCreateInfo.surface = m_hSurfaceKHR;
    swapChainCreateInfo.minImageCount = imageCount;
    swapChainCreateInfo.imageExtent = surfaceCapabilities.currentExtent;
    swapChainCreateInfo.imageArrayLayers = 1;
    swapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; //Potentially change for compute queue VK_SHARING_MODE_CONCURRENT
    swapChainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
    swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapChainCreateInfo.clipped = VK_TRUE;
    swapChainCreateInfo.oldSwapchain = VK_NULL_HANDLE;
    swapChainCreateInfo.queueFamilyIndexCount = 1;
    swapChainCreateInfo.pQueueFamilyIndices = &m_uQueuePresentFamilyIndex;

    // find format
    for (uint32_t i = 0; i < formatCount; i++)
    {
        VkSurfaceFormatKHR &formatKHR = formats[i];
//        if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
//        if (format.format == VK_FORMAT_R8G8B8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        if (formatKHR.format == (VkFormat)format && formatKHR.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            swapChainCreateInfo.imageFormat = formatKHR.format;
            swapChainCreateInfo.imageColorSpace = formatKHR.colorSpace;
            m_eSwapChainImageFormat = formatKHR.format;
            break;
        }
    }

    // find present mode
    for (uint32_t i = 0; i < presentModeCount; i++)
    {
        VkPresentModeKHR &present = presentMode[i];
        if (present == VK_PRESENT_MODE_MAILBOX_KHR ) //  VK_PRESENT_MODE_FIFO_KHR   VK_PRESENT_MODE_IMMEDIATE_KHR 
        {
            swapChainCreateInfo.presentMode = present;
            break;
        }
    }

    //Create swap chain
    res = GetVulkan()->vkCreateSwapchainKHR(pVulkanDev->hDevice, &swapChainCreateInfo, nullptr, &m_hSwapChain);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkCreateSwapchainKHR() failed with error=" << res);

    //Store images in the chain
    imageCount = 0;
    res = GetVulkan()->vkGetSwapchainImagesKHR(pVulkanDev->hDevice, m_hSwapChain, &imageCount, nullptr);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkGetSwapchainImagesKHR() failed with error=" << res);

    BackBuffer zeroBackBuffer = {};
    
    m_BackBuffers.resize(imageCount, zeroBackBuffer );
    std::vector<VkImage> images;
    images.resize(imageCount);

    res = GetVulkan()->vkGetSwapchainImagesKHR(pVulkanDev->hDevice, m_hSwapChain, &imageCount, images.data());
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkGetSwapchainImagesKHR() failed with error=" << res);

    m_SwapChainExtent.width = surfaceCapabilities.currentExtent.width;
    m_SwapChainExtent.height = surfaceCapabilities.currentExtent.height;

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (uint32_t i = 0; i < imageCount; i++)
    {
        BackBuffer &backbuffer = m_BackBuffers[i];

        VkImageViewCreateInfo imageViewCreateInfo = {};
        imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewCreateInfo.image = images[i];
        imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCreateInfo.format = (VkFormat)m_eSwapChainImageFormat;
    
        imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    
        imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
        imageViewCreateInfo.subresourceRange.levelCount = 1;
        imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        imageViewCreateInfo.subresourceRange.layerCount = 1;

        res = GetVulkan()->vkCreateImageView(pVulkanDev->hDevice, &imageViewCreateInfo, nullptr, &backbuffer.m_hImageView);
        CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkCreateImageView() failed with error=" << res);


        backbuffer.m_Surface.cbSizeof = sizeof(amf::AMFVulkanSurface);    // sizeof(AMFVulkanSurface)
        // surface properties
        backbuffer.m_Surface.hImage = images[i];
        backbuffer.m_Surface.eUsage = amf::AMF_SURFACE_USAGE_DEFAULT;
        backbuffer.m_Surface.hMemory = 0;
        backbuffer.m_Surface.iSize = 0;      // memory size
        backbuffer.m_Surface.eFormat = m_eSwapChainImageFormat;    // VkFormat
        backbuffer.m_Surface.iWidth = surfaceCapabilities.currentExtent.width;
        backbuffer.m_Surface.iHeight = surfaceCapabilities.currentExtent.height;

        backbuffer.m_Surface.Sync.cbSizeof = sizeof(backbuffer.m_Surface.Sync);
        backbuffer.m_Surface.Sync.hSemaphore = VK_NULL_HANDLE;
        backbuffer.m_Surface.Sync.bSubmitted = false;
        
        backbuffer.m_Surface.eCurrentLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // VkImageLayout
        VkSemaphore hSemaphore = VK_NULL_HANDLE;
        res = GetVulkan()->vkCreateSemaphore(pVulkanDev->hDevice, &semaphoreInfo, nullptr, &hSemaphore);
        CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkCreateSemaphore() failed with error=" << res);
        m_Semaphores.push_back(hSemaphore);
    }
    return AMF_OK;
}
AMF_RESULT SwapChainVulkan::CreateCommandPool()
{
    amf::AMFVulkanDevice* pVulkanDev = (amf::AMFVulkanDevice*)m_hVulkanDev;    

    VkResult res = VK_INCOMPLETE;

    VkCommandPoolCreateInfo poolCreateInfo = {};
        poolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolCreateInfo.queueFamilyIndex = m_uQueuePresentFamilyIndex;
        poolCreateInfo.flags =  VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;


    res = GetVulkan()->vkCreateCommandPool(pVulkanDev->hDevice, &poolCreateInfo, nullptr, &m_hCommandPool);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkCreateCommandPool() failed with error=" << res);

    VkCommandBufferAllocateInfo bufferAllocInfo = {};
        bufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        bufferAllocInfo.commandPool = m_hCommandPool;
        bufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        bufferAllocInfo.commandBufferCount = 1;

    res = GetVulkan()->vkAllocateCommandBuffers(pVulkanDev->hDevice, &bufferAllocInfo, &m_hTransitionCommandBuffer);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkAllocateCommandBuffers() failed with error=" << res);


    return AMF_OK;
}

AMF_RESULT SwapChainVulkan::CreateRenderPass()
{
    amf::AMFVulkanDevice* pVulkanDev = (amf::AMFVulkanDevice*)m_hVulkanDev;    

    VkResult res = VK_INCOMPLETE;

    VkAttachmentDescription colorAtt = {};
        colorAtt.format = (VkFormat)m_eSwapChainImageFormat;
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

    res = GetVulkan()->vkCreateRenderPass(pVulkanDev->hDevice, &renderPassInfo, nullptr, &m_hRenderPass);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkCreateRenderPass() failed with error=" << res);
    return AMF_OK;
}
AMF_RESULT SwapChainVulkan::CreateFrameBuffers()
{
    amf::AMFVulkanDevice* pVulkanDev = (amf::AMFVulkanDevice*)m_hVulkanDev;    
    VkResult res = VK_INCOMPLETE;
    for (uint32_t i = 0; i < m_BackBuffers.size(); i++)
    {
        BackBuffer &backbuffer = m_BackBuffers[i];

        VkFramebufferCreateInfo frameBufferInfo = {};
            frameBufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            frameBufferInfo.renderPass = m_hRenderPass;
            frameBufferInfo.attachmentCount = 1;
            frameBufferInfo.pAttachments = &backbuffer.m_hImageView;
            frameBufferInfo.width = m_SwapChainExtent.width;
            frameBufferInfo.height = m_SwapChainExtent.height;
            frameBufferInfo.layers = 1;

        res = GetVulkan()->vkCreateFramebuffer(pVulkanDev->hDevice, &frameBufferInfo, nullptr, &backbuffer.m_hFrameBuffer);
        CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkCreateFramebuffer() failed with error=" << res);
    }
    return AMF_OK;
}

AMF_RESULT SwapChainVulkan::AcquireBackBuffer(amf_uint32 *pIndex)
{
    CHECK_RETURN(m_Semaphores.size() > 0, AMF_FAIL, L"AcquireBackBuffer() no free semaphore");
    

    amf::AMFVulkanDevice* pVulkanDev = (amf::AMFVulkanDevice*)m_hVulkanDev;    

    VkResult res = VK_INCOMPLETE;

    uint32_t imageIndex = 0;
    res = GetVulkan()->vkAcquireNextImageKHR(pVulkanDev->hDevice, m_hSwapChain, UINT32_MAX, m_Semaphores.front(), VK_NULL_HANDLE, &imageIndex);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkAcquireNextImageKHR() failed with error=" << res);
    BackBuffer &backBuffer = m_BackBuffers[imageIndex];
    backBuffer.m_Surface.Sync.hSemaphore = m_Semaphores.front();
    backBuffer.m_Surface.Sync.bSubmitted = true;

    m_Semaphores.pop_front();

    *pIndex = imageIndex;

    return AMF_OK;

}
AMF_RESULT              SwapChainVulkan::Present(amf_uint32 imageIndex)
{
    VkResult res = VK_INCOMPLETE;

    BackBuffer &backBuffer = m_BackBuffers[imageIndex];

    TransitionSurface(&backBuffer.m_Surface, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VkSwapchainKHR swapChains[] = { m_hSwapChain };
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    
    if(backBuffer.m_Surface.Sync.bSubmitted)
    {
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &backBuffer.m_Surface.Sync.hSemaphore;
        backBuffer.m_Surface.Sync.bSubmitted = false;
    }
    
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    res = GetVulkan()->vkQueuePresentKHR(m_hQueuePresent, &presentInfo);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkQueuePresentKHR() failed with error=" << res);

    m_Semaphores.push_back(backBuffer.m_Surface.Sync.hSemaphore);
    backBuffer.m_Surface.Sync.hSemaphore = VK_NULL_HANDLE;

    //vkDeviceWaitIdle(pVulkanDev->hDevice);
    GetVulkan()->vkQueueWaitIdle(m_hQueuePresent);

    return AMF_OK;

}

AMF_RESULT SwapChainVulkan::TransitionSurface(amf::AMFVulkanSurface    *surface, amf_int32 layout)
{
    if(surface->eCurrentLayout == layout)
    {
        return AMF_OK;
    }

    amf::AMFVulkanDevice* pVulkanDev = (amf::AMFVulkanDevice*)m_hVulkanDev;    

    VkResult res = VK_INCOMPLETE;

    // wait if needed
    if(m_hTransitionWaitFence ==NULL)
    { 
        VkFenceCreateInfo fenceCreateInfo = {};
        fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        res = GetVulkan()->vkCreateFence(pVulkanDev->hDevice, &fenceCreateInfo, nullptr, &m_hTransitionWaitFence);
        CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkCreateFence() failed with error=" << res);
    }
    else
    {
        GetVulkan()->vkWaitForFences(pVulkanDev->hDevice, 1,  &m_hTransitionWaitFence, VK_FALSE, 1000000000LL ); // timeout is in nanoseconds
    }
    GetVulkan()->vkResetFences(pVulkanDev->hDevice, 1, &m_hTransitionWaitFence);

    // reset command buffer
    res = GetVulkan()->vkResetCommandBuffer(m_hTransitionCommandBuffer, 0 );
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkResetCommandBuffer() failed with error=" << res);


    VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

    GetVulkan()->vkBeginCommandBuffer(m_hTransitionCommandBuffer, &beginInfo);


    VkImageMemoryBarrier barrier = {};

    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = (VkImageLayout)surface->eCurrentLayout;
    surface->eCurrentLayout = layout;
    barrier.newLayout = (VkImageLayout)surface->eCurrentLayout;
    barrier.image = surface->hImage;
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

    GetVulkan()->vkCmdPipelineBarrier(m_hTransitionCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
         0,
         0, nullptr,
         0, nullptr,
         1, &barrier
     );
    res = GetVulkan()->vkEndCommandBuffer(m_hTransitionCommandBuffer);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkEndCommandBuffer() failed with error=" << res);


    VkSubmitInfo submitInfo = {};
    submitInfo.commandBufferCount       = 1;
    submitInfo.pCommandBuffers          = &m_hTransitionCommandBuffer;
    submitInfo.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    res = GetVulkan()->vkQueueSubmit(m_hQueuePresent, 1, &submitInfo, m_hTransitionWaitFence);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkQueueSubmit() failed with error=" << res);
    surface->Sync.hFence = m_hTransitionWaitFence;

    return AMF_OK;

}

AMF_RESULT SwapChainVulkan::MakeBuffer(void * data, amf_size size, amf_uint32 use, amf_uint32 props, amf::AMFVulkanBuffer& memory)
{
    amf::AMFVulkanDevice* pVulkanDev = (amf::AMFVulkanDevice*)m_hVulkanDev;    

    VkResult res = VK_INCOMPLETE;
    
    VkBufferCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        createInfo.size = size;
        createInfo.usage = (VkBufferUsageFlags)use;
        createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    res = GetVulkan()->vkCreateBuffer(pVulkanDev->hDevice, &createInfo, nullptr, &memory.hBuffer);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkCreateBuffer() failed with error=" << res);

    VkMemoryRequirements memReqs = {};
    GetVulkan()->vkGetBufferMemoryRequirements(pVulkanDev->hDevice, memory.hBuffer, &memReqs);

    VkPhysicalDeviceMemoryProperties memProps;
    GetVulkan()->vkGetPhysicalDeviceMemoryProperties(pVulkanDev->hPhysicalDevice, &memProps);

    uint32_t memType = -1;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
    {
        if (memReqs.memoryTypeBits & (1 << i))
        {
            if ((memProps.memoryTypes[i].propertyFlags & (VkMemoryPropertyFlags)props) == (VkMemoryPropertyFlags)props)
            {
                memType = i;
                break;
            }
        }
    }

    CHECK_RETURN(memType != ((uint32_t)-1), AMF_FAIL, L"vkGetPhysicalDeviceMemoryProperties() failed to provide memory type");

    VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = memType;

    res = GetVulkan()->vkAllocateMemory(pVulkanDev->hDevice, &allocInfo, nullptr, &memory.hMemory);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkAllocateMemory() failed with error=" << res);

    GetVulkan()->vkBindBufferMemory(pVulkanDev->hDevice, memory.hBuffer, memory.hMemory, 0);

    if(data != NULL)
    { 
        void* bufferData = NULL;
        res = GetVulkan()->vkMapMemory(pVulkanDev->hDevice, memory.hMemory, 0, size, 0, &bufferData);
        CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkMapMemory() failed with error=" << res);

        memcpy(bufferData, data, size);
        GetVulkan()->vkUnmapMemory(pVulkanDev->hDevice, memory.hMemory);
    }
    memory.iSize = size;
    memory.iAllocatedSize = size;
    return AMF_OK;
}

AMF_RESULT SwapChainVulkan::DestroyBuffer(amf::AMFVulkanBuffer& memory)
{
    amf::AMFVulkanDevice* pVulkanDev = (amf::AMFVulkanDevice*)m_hVulkanDev;    
    if(memory.hBuffer != NULL)
    {
        GetVulkan()->vkDestroyBuffer(pVulkanDev->hDevice, memory.hBuffer, nullptr);
        GetVulkan()->vkFreeMemory(pVulkanDev->hDevice, memory.hMemory, nullptr);
        memset(&memory, 0, sizeof(memory));
    }
    return AMF_OK;
}

VulkanImportTable * SwapChainVulkan::GetVulkan()
{
    return &m_ImportTable;
}
