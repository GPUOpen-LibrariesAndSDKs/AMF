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

#pragma once

#include "public/include/core/VulkanAMF.h"
#include "public/include/core/Context.h"
#include "public/common/VulkanImportTable.h"
#include <vector>
#include <list>

class SwapChainVulkan
{
public:
    SwapChainVulkan(amf::AMFContext* pContext);
    virtual ~SwapChainVulkan();

    virtual AMF_RESULT              Init(amf_handle hWnd, amf_handle hDisplay, bool bFullScreen, amf_int32 width, amf_int32 height, amf_uint32 format);
    virtual AMF_RESULT              Terminate();
    virtual AMF_RESULT              AcquireBackBuffer(amf_uint32 *pIndex);
    virtual AMF_RESULT              Present(amf_uint32 index);
protected:
	VulkanImportTable * GetVulkan();

    AMF_RESULT CreateSwapChain(amf_handle hWnd, amf_handle hDisplay, amf_uint32 format);
    AMF_RESULT CreateRenderPass();
    AMF_RESULT CreateFrameBuffers();
    AMF_RESULT CreateRenderTargets();
    AMF_RESULT CreateCommandPool();

    AMF_RESULT TransitionSurface(amf::AMFVulkanSurface    *surface, amf_int32 layout);
    AMF_RESULT MakeBuffer(void * data, amf_size size, amf_uint32 use, amf_uint32 props, amf::AMFVulkanBuffer& memory);
    AMF_RESULT DestroyBuffer(amf::AMFVulkanBuffer& memory);

    amf::AMFContext1Ptr             m_pContext;

    void*                           m_hVulkanDev;  
    VulkanImportTable               m_ImportTable;

    VkSurfaceKHR                    m_hSurfaceKHR;
    amf_uint32                      m_uQueuePresentFamilyIndex;
    amf_uint32                      m_uQueueComputeFamilyIndex;
    VkQueue                         m_hQueuePresent;
    amf_int32                       m_eSwapChainImageFormat; // VkFormat
    VkSwapchainKHR                    m_hSwapChain;
    VkRenderPass                    m_hRenderPass;
    VkCommandPool                    m_hCommandPool;

    struct BackBuffer
    {
        amf::AMFVulkanSurface    m_Surface;
        VkImageView                m_hImageView;
        VkFramebuffer           m_hFrameBuffer;
        VkCommandBuffer            m_hCommandBuffer;
    };
    std::vector<BackBuffer>            m_BackBuffers;
    std::list<VkSemaphore>            m_Semaphores;

    AMFSize                         m_SwapChainExtent;
    VkCommandBuffer                 m_hTransitionCommandBuffer;
    VkFence                         m_hTransitionWaitFence;
};

