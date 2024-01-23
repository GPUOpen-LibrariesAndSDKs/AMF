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

#include "public/include/core/Context.h"
#include "public/common/AMFSTL.h"
#include "public/common/VulkanImportTable.h"
#include "SwapChain.h"
#include <vector>
#include <list>

class CommandBufferVulkan;
class VulkanContext
{
public:
    virtual ~VulkanContext()     { Terminate(); }

    virtual AMF_RESULT          Init(amf::AMFVulkanDevice* pDevice, const VulkanImportTable* pImportTable);
    virtual AMF_RESULT          Init(const VulkanContext* pOther) { return Init(pOther->m_pVulkanDevice, pOther->m_pImportTable); }

    virtual AMF_RESULT          Terminate() { m_pVulkanDevice = nullptr; m_pImportTable = nullptr; return AMF_OK; }

    AMF_RESULT                  AllocateMemory(const VkMemoryRequirements& memoryRequirements, VkMemoryPropertyFlags memoryProperties, VkDeviceMemory& hMemory) const;
    AMF_RESULT                  MakeBuffer(const void* pData, amf_size size, amf_uint32 use, amf_uint32 props, amf::AMFVulkanBuffer& buffer) const;
    AMF_RESULT                  UpdateBuffer(amf::AMFVulkanBuffer& buffer, const void** ppBuffers, amf_uint count, const amf_size* pSizes, const amf_size* pOffsets) const;
    AMF_RESULT                  UpdateBuffer(amf::AMFVulkanBuffer& buffer, const void* pData, amf_size size, amf_size offset) const;
    AMF_RESULT                  DestroyBuffer(amf::AMFVulkanBuffer& buffer) const;
    AMF_RESULT                  CreateSurface(amf_uint32 queueIndex, amf_uint32 width, amf_uint32 height, VkFormat format, amf_uint32 usage, amf_uint32 memoryProperties, amf::AMFVulkanSurface& surface) const;
    AMF_RESULT                  DestroySurface(amf::AMFVulkanSurface& surface) const;
    AMF_RESULT                  TransitionSurface(CommandBufferVulkan* pBuffer, amf::AMFVulkanSurface* pSurface, VkImageLayout layout) const;

protected:
    VulkanContext() : m_pVulkanDevice(nullptr), m_pImportTable(nullptr) {}

    const VulkanImportTable* GetVulkan() const { return m_pImportTable; }

    amf::AMFVulkanDevice* m_pVulkanDevice;
    const VulkanImportTable* m_pImportTable;
};

class CommandBufferVulkan : public VulkanContext
{
public:
    CommandBufferVulkan();
    virtual ~CommandBufferVulkan();

    virtual AMF_RESULT      Init(amf::AMFVulkanDevice* pDevice, const VulkanImportTable* pImportTable, VkCommandPool hCommandPool);
    virtual AMF_RESULT      Terminate() override;

    virtual AMF_RESULT      StartRecording(amf_bool reset=true);
    virtual AMF_RESULT      EndRecording();
    virtual AMF_RESULT      Execute(VkQueue hQueue);

    virtual AMF_RESULT      SyncResource(amf::AMFVulkanSync* pSync, VkPipelineStageFlags waitFlags);
    virtual AMF_RESULT      WaitForExecution();

    VkCommandBuffer         GetBuffer() { return m_hCmdBuffer; }

private:
    virtual AMF_RESULT      CreateBuffer();
    virtual AMF_RESULT      Reset();

    VkCommandPool                           m_hCommandPool;
    VkCommandBuffer                         m_hCmdBuffer;
    VkFence                                 m_hFence;
    amf_bool                                m_recording;
    amf_bool                                m_fenceSignaled;

    amf::amf_vector<VkSemaphore>            m_waitSemaphores;
    amf::amf_vector<VkSemaphore>            m_signalSemaphores;
    amf::amf_vector<VkPipelineStageFlags>   m_waitFlags;
    amf::amf_vector<VkFence*>               m_pSyncFences;

};

struct BackBufferVulkan : public BackBufferBase
{
    amf::AMFVulkanSurface       m_surface;
    VkImageView                 m_hImageView;
    VkFramebuffer               m_hFrameBuffer;
    VkCommandBuffer             m_hCommandBuffer;

    virtual void*                       GetNative()                 const override { return (void*)&m_surface; }
    virtual amf::AMF_MEMORY_TYPE        GetMemoryType()             const override { return amf::AMF_MEMORY_VULKAN; }
    virtual AMFSize                     GetSize()                   const override { return AMFConstructSize(m_surface.iWidth, m_surface.iHeight); }
};

class SwapChainVulkan : public SwapChain, public VulkanContext
{
public:
    typedef BackBufferVulkan BackBuffer;

    SwapChainVulkan(amf::AMFContext* pContext);
    virtual                             ~SwapChainVulkan();

    virtual AMF_RESULT                  Init(amf_handle hwnd, amf_handle hDisplay, amf::AMFSurface* pSurface, amf_int32 width, amf_int32 height,
                                             amf::AMF_SURFACE_FORMAT format, amf_bool fullscreen = false, amf_bool hdr = false, amf_bool stereo = false) override;

    virtual AMF_RESULT                  Terminate() override;

    virtual AMF_RESULT                  Present(amf_bool waitForVSync) override;


    virtual amf_uint                    GetBackBufferCount() const override { return (amf_uint)m_pBackBuffers.size(); }
    virtual amf_uint                    GetBackBuffersAcquireable() const override { return (amf_uint)m_pBackBuffers.size(); }
    virtual amf_uint                    GetBackBuffersAvailable()   const override { return (amf_uint)m_pBackBuffers.size() - (amf_uint)m_acquiredBuffers.size(); }

    virtual AMF_RESULT                  AcquireNextBackBufferIndex(amf_uint& index) override;
    virtual AMF_RESULT                  DropLastBackBuffer() override;
    virtual AMF_RESULT                  DropBackBufferIndex(amf_uint index) override;


    virtual AMF_RESULT                  Resize(amf_int32 width, amf_int32 height, amf_bool fullscreen, amf::AMF_SURFACE_FORMAT format = amf::AMF_SURFACE_UNKNOWN) override;
    virtual amf_bool                    FormatSupported(amf::AMF_SURFACE_FORMAT format) override;
    virtual VkFormat                    GetVkFormat() { return m_surfaceFormat.format; }

    AMF_RESULT                          CreateImageView(VkImage hImage, VkFormat format, VkImageView& hImageView);

    VkCommandPool                       GetCmdPool()    { return m_hCommandPool; }
    amf_uint32                          GetQueueIndex() { return m_uQueuePresentFamilyIndex; }
    VkQueue                             GetQueue()      { return m_hQueuePresent; }
    VkRenderPass                        GetRenderPass() { return m_hRenderPass; }

protected:
    AMF_RESULT                          TerminateSwapChain();
    AMF_RESULT                          CreateSwapChain(amf_int32 width, amf_int32 height, amf_bool fullscreen, amf::AMF_SURFACE_FORMAT format);
    AMF_RESULT                          CreateSurface();
    AMF_RESULT                          FindQueue();
    AMF_RESULT                          FindSurfaceFormat(const VkSurfaceFormatKHR* formats, amf_uint count, VkSurfaceFormatKHR& supportedFormat);
    AMF_RESULT                          FindPresentMode(const VkPresentModeKHR* modes, amf_uint count, VkPresentModeKHR& supportedMode);
    AMF_RESULT                          CreateRenderPass();
    AMF_RESULT                          CreateImageViews();
    AMF_RESULT                          CreateFrameBuffers();
    AMF_RESULT                          CreateCommandPool();

    virtual AMF_RESULT                  SetFormat(amf::AMF_SURFACE_FORMAT format) override;
    VkFormat                            GetSupportedVkFormat(amf::AMF_SURFACE_FORMAT format);

    virtual AMF_RESULT                  UpdateCurrentOutput() override { return AMF_OK; }

    virtual const BackBufferBasePtr*    GetBackBuffers() const override { return m_hSwapChain != NULL ? m_pBackBuffers.data() : nullptr; }
    virtual AMF_RESULT                  BackBufferToSurface(const BackBufferBase* pBuffer, amf::AMFSurface** ppSurface) const override;
    virtual void*                       GetSurfaceNative(amf::AMFSurface* pSurface) const override;


    amf::AMFContext1Ptr                 m_pContext1;
    VulkanImportTable                   m_ImportTable;

    VkSurfaceKHR                        m_hSurfaceKHR;

    amf_uint32                          m_uQueuePresentFamilyIndex;
    amf_uint32                          m_uQueueComputeFamilyIndex;
    VkQueue                             m_hQueuePresent;
    VkSurfaceFormatKHR                  m_surfaceFormat;
    VkSwapchainKHR                      m_hSwapChain;
    VkRenderPass                        m_hRenderPass;
    VkCommandPool                       m_hCommandPool;
    CommandBufferVulkan                 m_cmdBuffer;

    amf::amf_vector<BackBufferBasePtr>  m_pBackBuffers;
    amf::amf_list<amf_uint>             m_droppedBuffers;
    amf::amf_list<amf_uint>             m_acquiredBuffers;

    amf::amf_list<VkSemaphore>          m_Semaphores;
    amf_uint                            m_lastPresentedImageIndex;
};