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
#include "VideoPresenterVulkan.h"
#include "../common/CmdLogger.h"
#include "public/common/ByteArray.h"
#include "public/common/TraceAdapter.h"
#include "public/include/core/Compute.h"
#include <iostream>
#include <fstream>

//#include <DirectXMath.h>

using namespace amf;

//#define VK_NO_PROTOTYPES
#if defined(_WIN32)
#else
   #include <unistd.h>
#endif

#define AMF_FACILITY L"VideoPresenterVulkan"

//Cube data
struct Vertex{
    float pos[3];
    float tex[2];
};

struct ModelView {
    float ModelViewProj[16];
};

VideoPresenterVulkan::VideoPresenterVulkan(amf_handle hwnd, amf::AMFContext* pContext, amf_handle display) :
    BackBufferPresenter(hwnd, pContext, display),
    SwapChainVulkan(pContext),
    m_fScale(1.0f),
    m_fPixelAspectRatio(1.0f),
    m_fOffsetX(0.0f),
    m_fOffsetY(0.0f),
    m_uiAvailableBackBuffer(0),
    m_bResizeSwapChain(false),
#if defined(_WIN32)
    m_eInputFormat(amf::AMF_SURFACE_RGBA),
#elif defined(__linux)    
    m_eInputFormat(amf::AMF_SURFACE_BGRA),
#endif
    m_hPipelineLayout(NULL),
    m_hPipeline(NULL),
    m_hDescriptorSet(NULL),
    m_hDescriptorPool(NULL),
    m_hSampler(NULL),
    m_hCommandBuffer(NULL),
    m_hCommandBufferFence(NULL)

{
    memset(&m_VertexBuffer, 0, sizeof(m_VertexBuffer));
    memset(&m_IndexBuffer, 0, sizeof(m_IndexBuffer));
    memset(&m_MVPBuffer, 0, sizeof(m_MVPBuffer));
}

VideoPresenterVulkan::~VideoPresenterVulkan()
{
    Terminate();
}

AMF_RESULT VideoPresenterVulkan::Init(amf_int32 width, amf_int32 height)
{

    VkFormat format = VK_FORMAT_UNDEFINED;
    switch(m_eInputFormat)
    {
    case amf::AMF_SURFACE_BGRA: format = VK_FORMAT_B8G8R8A8_UNORM; break;
    case amf::AMF_SURFACE_RGBA: format = VK_FORMAT_R8G8B8A8_UNORM; break;
    }

    CHECK_RETURN(width != 0 && height != 0 && format != VK_FORMAT_UNDEFINED, AMF_FAIL, L"Bad width/height: width=" << width << L" height=" << height << L" format" << amf::AMFSurfaceGetFormatName(m_eInputFormat));

    AMF_RESULT res = AMF_OK;

    res = VideoPresenter::Init(width, height);
    CHECK_AMF_ERROR_RETURN(res, L"= VideoPresenter::Init() failed");

    res = SwapChainVulkan::Init(m_hwnd, m_hDisplay, false, width, height, format);
    CHECK_AMF_ERROR_RETURN(res, L"SwapChainVulkan::Init() failed");

    res = CreateDescriptorSetLayout();
    CHECK_AMF_ERROR_RETURN(res, L"CreateDescriptorSetLayout() failed");

    res = CreatePipeline();
    CHECK_AMF_ERROR_RETURN(res, L"CreatePipeline() failed");

    res = CreateVertexBuffers();
    CHECK_AMF_ERROR_RETURN(res, L"CreateVertexBuffers() failed");

    res = CreateDescriptorSetPool();
    CHECK_AMF_ERROR_RETURN(res, L"CreateDescriptorSetPool() failed");

    res = CreateCommandBuffer();
    CHECK_AMF_ERROR_RETURN(res, L"CreateCommandBuffer() failed");

    if(m_bResizeSwapChain == false)
    {
        m_rectClient = GetClientRect();
    }
    return AMF_OK;
}

AMF_RESULT VideoPresenterVulkan::Terminate()
{
    if(m_hVulkanDev == NULL)
    {
        return AMF_OK;
    }

//    m_pRenderTargetView_L = NULL;
//    m_pRenderTargetView_R = NULL;

    amf::AMFVulkanDevice* pVulkanDev = (amf::AMFVulkanDevice*)m_hVulkanDev;    


    if (m_hSampler != NULL)
    {
		GetVulkan()->vkDestroySampler(pVulkanDev->hDevice, m_hSampler, nullptr);
        m_hSampler = NULL;
    }

    if(m_hDescriptorSet != NULL)
    {
		GetVulkan()->vkFreeDescriptorSets(pVulkanDev->hDevice, m_hDescriptorPool, 1, &m_hDescriptorSet);
        m_hDescriptorSet = NULL;
    }
    if (m_hDescriptorPool != NULL)
    {
		GetVulkan()->vkDestroyDescriptorPool(pVulkanDev->hDevice, m_hDescriptorPool, nullptr);
        m_hDescriptorPool = NULL;
    }
    DestroyBuffer(m_VertexBuffer);
    DestroyBuffer(m_IndexBuffer);
    DestroyBuffer(m_MVPBuffer);


    if (m_hPipeline != NULL)
    {
		GetVulkan()->vkDestroyPipeline(pVulkanDev->hDevice, m_hPipeline, nullptr);
        m_hPipeline = NULL;
    }
    if (m_hPipelineLayout != NULL)
    {
		GetVulkan()->vkDestroyPipelineLayout(pVulkanDev->hDevice, m_hPipelineLayout, nullptr);
        m_hPipelineLayout = NULL;
    }
    if (m_hDescriptorSetLayout != NULL)
    {
		GetVulkan()->vkDestroyDescriptorSetLayout(pVulkanDev->hDevice, m_hDescriptorSetLayout, nullptr);
        m_hDescriptorSetLayout = NULL;
    }
    if(m_hCommandBuffer != NULL)
    {
		GetVulkan()->vkFreeCommandBuffers(pVulkanDev->hDevice, m_hCommandPool, 1, &m_hCommandBuffer);
        m_hCommandBuffer = NULL;
    }
    if(m_hCommandBufferFence != NULL)
    {
		GetVulkan()->vkWaitForFences(pVulkanDev->hDevice, 1,  &m_hCommandBufferFence, VK_FALSE, 1000000000LL ); // timeout is in nanoseconds
		GetVulkan()->vkResetFences(pVulkanDev->hDevice, 1, &m_hCommandBufferFence);
		GetVulkan()->vkDestroyFence(pVulkanDev->hDevice, m_hCommandBufferFence, nullptr);
        m_hCommandBufferFence = NULL;
    }

    SwapChainVulkan::Terminate();
    return VideoPresenter::Terminate();
}

AMF_RESULT VideoPresenterVulkan::CreateDescriptorSetLayout()
{
    amf::AMFVulkanDevice* pVulkanDev = (amf::AMFVulkanDevice*)m_hVulkanDev;    

    VkResult res = VK_INCOMPLETE;

    VkDescriptorSetLayoutBinding MVPLayoutBinding = {};
        MVPLayoutBinding.binding = 0;
        MVPLayoutBinding.descriptorCount = 1;
        MVPLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        MVPLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

       
    VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
        samplerLayoutBinding.binding = 1;
        samplerLayoutBinding.descriptorCount = 1;
        samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; //VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkDescriptorSetLayoutBinding> bindings = { MVPLayoutBinding, samplerLayoutBinding };
   
    VkDescriptorSetLayoutCreateInfo layoutCreateInfo = {};
        layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutCreateInfo.bindingCount = (uint32_t)bindings.size();
        layoutCreateInfo.pBindings = bindings.data();

    res = GetVulkan()->vkCreateDescriptorSetLayout(pVulkanDev->hDevice, &layoutCreateInfo, nullptr, &m_hDescriptorSetLayout);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkCreateDescriptorSetLayout() failed with error=" << res);

    return AMF_OK;
}
static AMF_RESULT LoadShaderFile(const wchar_t* pFileName, AMFByteArray &data)
{
#if defined(_WIN32)
    wchar_t path[5 * 1024];
    ::GetModuleFileNameW(NULL, path, amf_countof(path));
    std::wstring filepath(path);
    std::wstring::size_type slash = filepath.find_last_of(L"\\/");
    filepath = filepath.substr(0, slash + 1);
    std::wstring fileName = filepath + pFileName;
    //Load data from file
    std::ifstream fs(fileName, std::ios::ate | std::ios::binary);
    
#elif defined(__linux)    
    char path[5 * 1024] = {0};
    ssize_t len = readlink("/proc/self/exe", path, amf_countof(path));
    std::string filepath(path);
    std::string::size_type slash = filepath.find_last_of("\\/");
    filepath = filepath.substr(0, slash + 1);
    std::string fileName = filepath + amf_from_unicode_to_utf8(pFileName).c_str();
    //Load data from file
    std::ifstream fs(fileName, std::ios::ate | std::ios::binary);
#endif
    
    if (!fs.is_open())
    {
        return AMF_FAIL;
    }
    size_t fileSize = (size_t)fs.tellg();
    data.SetSize(fileSize);
    fs.seekg(0);
    fs.read((char*)data.GetData(), fileSize);
    fs.close();
    return AMF_OK;
}

AMF_RESULT VideoPresenterVulkan::CreatePipeline()
{
    amf::AMFVulkanDevice* pVulkanDev = (amf::AMFVulkanDevice*)m_hVulkanDev;    

    VkResult res = VK_INCOMPLETE;
    AMF_RESULT resAMF = AMF_OK;

    const wchar_t*  pCubeShaderFileNameVert = L"quad.vert.spv";
    const wchar_t*  pCubeShaderFileNameFraq = L"quad.frag.spv";

    AMFByteArray vertShader;
    AMFByteArray fraqShader;
    resAMF = LoadShaderFile(pCubeShaderFileNameVert, vertShader);
    CHECK_AMF_ERROR_RETURN(resAMF, L"LoadShaderFile(" << pCubeShaderFileNameVert << L") failed");

    resAMF = LoadShaderFile(pCubeShaderFileNameFraq, fraqShader);
    CHECK_AMF_ERROR_RETURN(resAMF, L"LoadShaderFile(" << pCubeShaderFileNameFraq <<L") failed");

    VkShaderModule vertModule;
    VkShaderModule fragModule;

    VkShaderModuleCreateInfo vertModuleCreateInfo = {};
        vertModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vertModuleCreateInfo.codeSize = vertShader.GetSize();
        vertModuleCreateInfo.pCode = (uint32_t*)vertShader.GetData();

    VkShaderModuleCreateInfo fragModuleCreateInfo = {};
        fragModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        fragModuleCreateInfo.codeSize = fraqShader.GetSize();
        fragModuleCreateInfo.pCode = (uint32_t*)fraqShader.GetData();

    res = GetVulkan()->vkCreateShaderModule(pVulkanDev->hDevice, &vertModuleCreateInfo, nullptr, &vertModule);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkCreateShaderModule(" << pCubeShaderFileNameVert << L") failed with error=" << res);

    res = GetVulkan()->vkCreateShaderModule(pVulkanDev->hDevice, &fragModuleCreateInfo, nullptr, &fragModule);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkCreateShaderModule(" << pCubeShaderFileNameFraq << L") failed with error=" << res);

    VkPipelineShaderStageCreateInfo vertStageInfo = {};
        vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStageInfo.module = vertModule;
        vertStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragStageInfo = {};
        fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStageInfo.module = fragModule;
        fragStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertStageInfo, fragStageInfo };

    //Fixed Stages
    VkVertexInputBindingDescription vertexBindingDesc = {};
    vertexBindingDesc.binding = 0;
    vertexBindingDesc.stride = sizeof(Vertex);
    vertexBindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertexAttribDesc[2] = {0, 0};

    //Position
    vertexAttribDesc[0].binding = 0;
    vertexAttribDesc[0].location = 0;
    vertexAttribDesc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexAttribDesc[0].offset = offsetof(Vertex, pos);

    //Texture
    vertexAttribDesc[1].binding = 0;
    vertexAttribDesc[1].location = 1;
    vertexAttribDesc[1].format = VK_FORMAT_R32G32_SFLOAT;
    vertexAttribDesc[1].offset = offsetof(Vertex, tex);


    VkPipelineVertexInputStateCreateInfo vertInputInfo = {};
        vertInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertInputInfo.vertexBindingDescriptionCount = 1;
        vertInputInfo.pVertexBindingDescriptions = &vertexBindingDesc;
        vertInputInfo.vertexAttributeDescriptionCount = (uint32_t)amf_countof(vertexAttribDesc);
        vertInputInfo.pVertexAttributeDescriptions = vertexAttribDesc;

    VkPipelineInputAssemblyStateCreateInfo inputAssemply = {};
        inputAssemply.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemply.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssemply.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)m_SwapChainExtent.width;
        viewport.height = (float)m_SwapChainExtent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
        scissor.offset = { 0, 0 };
        scissor.extent.width = m_SwapChainExtent.width;
        scissor.extent.height = m_SwapChainExtent.height;

    VkPipelineViewportStateCreateInfo viewportState = {};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizerInfo = {};
        rasterizerInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
//        rasterizerInfo.depthClampEnable = VK_TRUE;
        rasterizerInfo.depthClampEnable = VK_FALSE;
        rasterizerInfo.rasterizerDiscardEnable = VK_FALSE;
        rasterizerInfo.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizerInfo.cullMode = VK_CULL_MODE_NONE; //VK_CULL_MODE_BACK_BIT;
        rasterizerInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;//VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizerInfo.depthBiasEnable = VK_FALSE;
        rasterizerInfo.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAtt = {}; // Sets up color / alpha blending.
        colorBlendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAtt.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo blendInfo = {};
        blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blendInfo.logicOpEnable = VK_FALSE;
        blendInfo.logicOp = VK_LOGIC_OP_COPY;
        blendInfo.attachmentCount = 1;
        blendInfo.pAttachments = &colorBlendAtt;

    VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
        depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilState.depthTestEnable = VK_FALSE;
        depthStencilState.depthWriteEnable = VK_FALSE;
        depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        depthStencilState.depthBoundsTestEnable = VK_FALSE;
        depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.passOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.compareOp = VK_COMPARE_OP_ALWAYS;
        depthStencilState.stencilTestEnable = VK_FALSE;
        depthStencilState.front = depthStencilState.back;

    VkDescriptorSetLayout setLayouts[] = {m_hDescriptorSetLayout};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = setLayouts;



    res = GetVulkan()->vkCreatePipelineLayout(pVulkanDev->hDevice, &pipelineLayoutInfo, nullptr, &m_hPipelineLayout);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkCreatePipelineLayout() failed with error=" << res);

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssemply;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizerInfo;
        pipelineInfo.pDepthStencilState = &depthStencilState;
        pipelineInfo.pColorBlendState = &blendInfo;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.layout = m_hPipelineLayout;
        pipelineInfo.renderPass = m_hRenderPass;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    res = GetVulkan()->vkCreateGraphicsPipelines(pVulkanDev->hDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_hPipeline);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkCreateGraphicsPipelines() failed with error=" << res);
    //cleanup
	GetVulkan()->vkDestroyShaderModule(pVulkanDev->hDevice, vertModule, nullptr);
	GetVulkan()->vkDestroyShaderModule(pVulkanDev->hDevice, fragModule, nullptr);

    return AMF_OK;
}

AMF_RESULT VideoPresenterVulkan::CreateCommandBuffer()
{
    amf::AMFVulkanDevice* pVulkanDev = (amf::AMFVulkanDevice*)m_hVulkanDev;    

    VkResult res = VK_INCOMPLETE;
    AMF_RESULT resAMF = AMF_OK;


    VkCommandBufferAllocateInfo bufferAllocInfo = {};
        bufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        bufferAllocInfo.commandPool = m_hCommandPool;
        bufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        bufferAllocInfo.commandBufferCount = 1;

    res = GetVulkan()->vkAllocateCommandBuffers(pVulkanDev->hDevice, &bufferAllocInfo, &m_hCommandBuffer);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkAllocateCommandBuffers() failed with error=" << res);



    return AMF_OK;
}

AMF_RESULT VideoPresenterVulkan::ResetCommandBuffer()
{
    VkResult res = VK_INCOMPLETE;
    amf::AMFVulkanDevice* pVulkanDev = (amf::AMFVulkanDevice*)m_hVulkanDev;

    if(m_hCommandBufferFence == NULL)
    { 
        VkFenceCreateInfo fenceCreateInfo = {};
        fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        res = GetVulkan()->vkCreateFence(pVulkanDev->hDevice, &fenceCreateInfo, nullptr, &m_hCommandBufferFence);
    }
    else 
    { 
        // wait for the previous command buffer to complete - just in case
		GetVulkan()->vkWaitForFences(pVulkanDev->hDevice, 1,  &m_hCommandBufferFence, VK_FALSE, 1000000000LL ); // timeout is in nanoseconds
    }
	GetVulkan()->vkResetFences(pVulkanDev->hDevice, 1, &m_hCommandBufferFence);
    res = GetVulkan()->vkResetCommandBuffer(m_hCommandBuffer, 0 );
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkResetCommandBuffer() failed with error=" << res);

    return AMF_OK;
}

AMF_RESULT VideoPresenterVulkan::RecordCommandBuffer(amf_uint32 imageIndex)
{
    VkResult res = VK_INCOMPLETE;
    amf::AMFVulkanDevice* pVulkanDev = (amf::AMFVulkanDevice*)m_hVulkanDev;


    BackBuffer &backBuffer = m_BackBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

		GetVulkan()->vkBeginCommandBuffer(m_hCommandBuffer, &beginInfo);

    VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)m_SwapChainExtent.width;
        viewport.height = (float)m_SwapChainExtent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

		GetVulkan()->vkCmdSetViewport(m_hCommandBuffer, 0, 1, &viewport);

    VkClearValue clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
    VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_hRenderPass;
        renderPassInfo.framebuffer = backBuffer.m_hFrameBuffer;
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent.width = m_SwapChainExtent.width;
        renderPassInfo.renderArea.extent.height = m_SwapChainExtent.height;
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

		GetVulkan()->vkCmdBeginRenderPass(m_hCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
		GetVulkan()->vkCmdBindPipeline(m_hCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_hPipeline);

    VkBuffer vertexBuffers[] = { m_VertexBuffer.hBuffer };
    VkDeviceSize offsets[] = {0};
	GetVulkan()->vkCmdBindVertexBuffers(m_hCommandBuffer, 0, 1, vertexBuffers, offsets);
	GetVulkan()->vkCmdBindIndexBuffer(m_hCommandBuffer, m_IndexBuffer.hBuffer, 0, VK_INDEX_TYPE_UINT32);

	GetVulkan()->vkCmdBindDescriptorSets(m_hCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_hPipelineLayout, 0, 1, &m_hDescriptorSet, 0, nullptr);

	GetVulkan()->vkCmdDrawIndexed(m_hCommandBuffer, uint32_t(m_IndexBuffer.iSize / sizeof(uint32_t)), 1, 0, 0, 0);

	GetVulkan()->vkCmdEndRenderPass(m_hCommandBuffer);
    res = GetVulkan()->vkEndCommandBuffer(m_hCommandBuffer);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkEndCommandBuffer() failed with error=" << res);
    
    return AMF_OK;
}

AMF_RESULT VideoPresenterVulkan::CreateVertexBuffers()
{
    amf::AMFVulkanDevice* pVulkanDev = (amf::AMFVulkanDevice*)m_hVulkanDev;    

    VkResult res = VK_INCOMPLETE;

    
    // Create vertex buffer
    
    Vertex vertexes[] =
    {
        { { -1.0f,  1.0f, 0.0f },   { 0.0f, 0.0f} },
        { {  1.0f,  1.0f, 0.0f },   { 1.0f, 0.0f} },
        { { -1.0f, -1.0f, 0.0f },   { 0.0f, 1.0f} },
        { {  1.0f, -1.0f, 0.0f },   { 1.0f, 1.0f} },
    };
// Create index buffer
    uint32_t indexes[] =
    {   0,1,2,    1,2,3,
    };


    MakeBuffer((void*)vertexes, sizeof(vertexes), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_VertexBuffer);
    MakeBuffer((void*)indexes, sizeof(indexes), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_IndexBuffer);

//    DirectX::XMMATRIX worldViewProjection = DirectX::XMMatrixIdentity();
    ModelView mvp = {};

//    for (int i = 0; i < 4; i++){
//        for (int j = 0; j < 4; j++){
//            mvp.ModelViewProj[4 * i + j] = DirectX::XMVectorGetByIndex(worldViewProjection.r[i], j);
//        }
//    }
    mvp.ModelViewProj[4 * 0 + 0] = 1.0f;
    mvp.ModelViewProj[4 * 1 + 1] = 1.0f;
    mvp.ModelViewProj[4 * 2 + 2] = 1.0f;
    mvp.ModelViewProj[4 * 3 + 3] = 1.0f;

    MakeBuffer(&mvp, sizeof(ModelView), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_MVPBuffer);
    
    return AMF_OK;
}
AMF_RESULT VideoPresenterVulkan::CreateDescriptorSetPool()
{
    amf::AMFVulkanDevice* pVulkanDev = (amf::AMFVulkanDevice*)m_hVulkanDev;    

    VkResult res = VK_INCOMPLETE;

    VkDescriptorPoolSize poolSize = {};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = 1;

    VkDescriptorPoolSize samplerSize = {};
        samplerSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerSize.descriptorCount = 1;

    std::vector<VkDescriptorPoolSize> sizes = { poolSize, samplerSize };
    VkDescriptorPoolCreateInfo poolCreateInfo = {};
        poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolCreateInfo.poolSizeCount = (uint32_t)sizes.size();
        poolCreateInfo.pPoolSizes = sizes.data();
        poolCreateInfo.maxSets = 2;
        poolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;


    res = GetVulkan()->vkCreateDescriptorPool(pVulkanDev->hDevice, &poolCreateInfo, nullptr, &m_hDescriptorPool);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkCreateDescriptorPool() failed with error=" << res);

    VkDescriptorSetLayout layouts[] = { m_hDescriptorSetLayout };
    VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_hDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = layouts;

    res = GetVulkan()->vkAllocateDescriptorSets(pVulkanDev->hDevice, &allocInfo, &m_hDescriptorSet);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkAllocateDescriptorSets() failed with error=" << res);


    VkSamplerCreateInfo sampler = {};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler.addressModeV = sampler.addressModeU;
    sampler.addressModeW = sampler.addressModeU;
    sampler.mipLodBias = 0.0f;
    sampler.compareOp = VK_COMPARE_OP_NEVER;
    sampler.minLod = 0.0f;
    sampler.maxLod = 0.0f;
    sampler.maxAnisotropy = 1.0f;
    sampler.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE; //VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    sampler.unnormalizedCoordinates = VK_FALSE;
    sampler.compareEnable = VK_FALSE;

    res = GetVulkan()->vkCreateSampler(pVulkanDev->hDevice, &sampler, nullptr, &m_hSampler);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkCreateSampler() failed with error=" << res);


    return AMF_OK;
}

AMF_RESULT VideoPresenterVulkan::UpdateTextureDescriptorSet(amf::AMFVulkanView* pView)
{
    amf::AMFVulkanDevice* pVulkanDev = (amf::AMFVulkanDevice*)m_hVulkanDev;    

    VkResult res = VK_INCOMPLETE;

    VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = m_MVPBuffer.hBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(ModelView);

    VkWriteDescriptorSet descriptorWriteMVP = {};
        descriptorWriteMVP.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWriteMVP.pNext = NULL;
        descriptorWriteMVP.dstSet = m_hDescriptorSet;
        descriptorWriteMVP.dstBinding = 0;
        descriptorWriteMVP.dstArrayElement = 0;
        descriptorWriteMVP.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWriteMVP.descriptorCount = 1;
        descriptorWriteMVP.pBufferInfo = &bufferInfo;


    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;//(VkImageLayout)pView->pSurface->eCurrentLayout;
    imageInfo.imageView = pView->hView;
    imageInfo.sampler = m_hSampler;

    VkWriteDescriptorSet descriptorWriteTexture = {};

        descriptorWriteTexture.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWriteTexture.pNext = NULL;
        descriptorWriteTexture.dstSet = m_hDescriptorSet;
        descriptorWriteTexture.dstBinding = 1;
        descriptorWriteTexture.dstArrayElement = 0;
        descriptorWriteTexture.descriptorCount = 1;
        descriptorWriteTexture.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
        descriptorWriteTexture.pImageInfo = &imageInfo;


    std::vector<VkWriteDescriptorSet> descriptors = { descriptorWriteMVP, descriptorWriteTexture };

	GetVulkan()->vkUpdateDescriptorSets(pVulkanDev->hDevice, (uint32_t)descriptors.size(), descriptors.data(), 0, NULL);

    return AMF_OK;
}

AMF_RESULT VideoPresenterVulkan::Present(amf::AMFSurface* pSurface)
{
    AMF_RESULT err = AMF_OK;

    if(m_hVulkanDev == NULL)
    {
        return AMF_NO_DEVICE;
    }
   
//    amf::AMFContext1::AMFVulkanLocker lockDev(SwapChainVulkan::m_pContext);

//    pSurface->Convert(amf::AMF_MEMORY_HOST);
//    amf_uint8 *mem =  (amf_uint8 *)pSurface->GetPlaneAt(0)->GetNative();

    if(pSurface->GetFormat() != GetInputFormat())
    {
        return AMF_INVALID_FORMAT;
    }
    if( (err = pSurface->Convert(GetMemoryType())) != AMF_OK)
    {
        err;
    }
    amf_uint32 imageIndex = 0;


    bool bResized = false;

    amf::AMFLock lock(&m_sect);

#if defined(_WIN32)
    CheckForResize(false, &bResized);
    AMFRect rectClient = GetClientRect();
    if(bResized)
    {
        m_rectClientResize = rectClient;
    }
    
#elif defined(__linux)
    //MM TODO
    AMFRect rectClient = m_rectClient;
#endif

    amf::AMFPlane* pPlane = pSurface->GetPlane(amf::AMF_PLANE_PACKED);
    amf::AMFVulkanSurface* pSrcSurface = ((amf::AMFVulkanView*)pPlane->GetNative())->pSurface;
    if (pSrcSurface == NULL)
    {
        return AMF_INVALID_POINTER;
    }


    if(!m_bRenderToBackBuffer)
    {
#if defined(_WIN32)
        if(bResized)
        {
            ResizeSwapChain();
        }
#endif        

        err = AcquireBackBuffer(&imageIndex);
        CHECK_AMF_ERROR_RETURN(err, L"AcquireBackBuffer() failed");

        BackBuffer &backBuffer = m_BackBuffers[imageIndex];
    
        AMFRect srcRect = {pPlane->GetOffsetX(), pPlane->GetOffsetY(), pPlane->GetOffsetX() + pPlane->GetWidth(), pPlane->GetOffsetY() + pPlane->GetHeight()};
        AMFRect outputRect;
        CalcOutputRect(&srcRect, &rectClient, &outputRect);
        //in case of ROI we should specify SrcRect
        err = BitBlt(pSurface, &srcRect, imageIndex, &outputRect);
    }
    else
    {
        for(; imageIndex < (amf_int32)m_BackBuffers.size(); imageIndex++)
        {
            if(&m_BackBuffers[imageIndex].m_Surface == pSrcSurface)
            {
                break;
            }
        }
    }

    
    WaitForPTS(pSurface->GetPts());


    err = SwapChainVulkan::Present(imageIndex);
    CHECK_AMF_ERROR_RETURN(err, L"Present() failed");
    
    if(m_bRenderToBackBuffer)
    {
        m_uiAvailableBackBuffer--;
    }

//    AMFTraceInfo(AMF_FACILITY, L"Presented backbuffer %d", imageIndex);
#if defined(__linux)
    XFlush((Display*)m_hDisplay);
#endif
    return err;
}

AMF_RESULT VideoPresenterVulkan::BitBlt(amf::AMFSurface* pSurface,AMFRect *srcRect, amf_uint32 imageIndex,AMFRect *outputRect)
{
//    return BitBltCopy(pSurface, srcRect, imageIndex, outputRect);
    return BitBltRender(pSurface,srcRect, imageIndex,outputRect);
}

AMF_RESULT VideoPresenterVulkan::BitBltCopy(amf::AMFSurface* pSurface, AMFRect *srcRect, amf_uint32 imageIndex,AMFRect *outputRect)
{
    AMF_RESULT err = AMF_OK;

    BackBuffer &backBuffer = m_BackBuffers[imageIndex];

    amf::AMFSurfacePtr pSwapChainSurface;
    err = SwapChainVulkan::m_pContext->CreateSurfaceFromVulkanNative(&backBuffer.m_Surface, &pSwapChainSurface, NULL);
    CHECK_AMF_ERROR_RETURN(err, L"CreateSurfaceFromVulkanNative() failed");


    amf::AMFComputePtr pCompute;
    SwapChainVulkan::m_pContext->GetCompute(amf::AMF_MEMORY_VULKAN, &pCompute);
    
    amf_size originSrc[3] = { (size_t)srcRect->left, (size_t)srcRect->top, 0};
    amf_size originDst[3] = { (size_t)outputRect->left, (size_t)outputRect->top, 0};
    amf_size region[3] = { (size_t)outputRect->Width(), (size_t)outputRect->Height(), 1};
    err = pCompute->CopyPlane(pSurface->GetPlaneAt(0), originSrc, region, pSwapChainSurface->GetPlaneAt(0), originDst);
    CHECK_AMF_ERROR_RETURN(err, L"CopyPlane() failed");
    return err;
}

AMF_RESULT VideoPresenterVulkan::BitBltRender(amf::AMFSurface* pSurface, AMFRect *srcRect, amf_uint32 imageIndex, AMFRect *outputRect)
{
    AMF_RESULT err = AMF_OK;

    BackBuffer &backBuffer = m_BackBuffers[imageIndex];

    amf::AMFVulkanDevice* pVulkanDev = (amf::AMFVulkanDevice*)m_hVulkanDev;    

    VkResult res = VK_INCOMPLETE;
    AMF_RESULT resAMF = AMF_OK;

   
    ResetCommandBuffer();

    // update descriptor set with surface
    amf::AMFVulkanView* pView = (amf::AMFVulkanView*)pSurface->GetPlaneAt(0)->GetNative();
    TransitionSurface(pView->pSurface, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    AMFSize srcSize = {pView->pSurface->iWidth, pView->pSurface->iHeight};
    AMFSize dstSize = {m_SwapChainExtent.width, m_SwapChainExtent.height};
    UpdateVertices(srcRect, &srcSize, outputRect, &dstSize);

    err = UpdateTextureDescriptorSet(pView);

    err = RecordCommandBuffer(imageIndex);

    // render
    std::vector<VkSemaphore> waitSemaphores;
    std::vector<VkSemaphore> signalSemaphores;
    std::vector<VkPipelineStageFlags> waitFlags;

    if(pView->pSurface->Sync.hSemaphore != VK_NULL_HANDLE)
    {
        if(pView->pSurface->Sync.bSubmitted)
        {
            waitSemaphores.push_back(pView->pSurface->Sync.hSemaphore);
            waitFlags.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
            pView->pSurface->Sync.bSubmitted = false;
        }
        signalSemaphores.push_back(pView->pSurface->Sync.hSemaphore);
        pView->pSurface->Sync.bSubmitted = true;
    }
    
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    if(waitSemaphores.size() > 0 )
    {
        submitInfo.waitSemaphoreCount = (uint32_t)waitSemaphores.size();
        submitInfo.pWaitSemaphores = &waitSemaphores[0];
        submitInfo.pWaitDstStageMask = &waitFlags[0];
    }
    if(signalSemaphores.size() > 0)
    { 
        submitInfo.signalSemaphoreCount = (uint32_t)signalSemaphores.size();
        submitInfo.pSignalSemaphores = &signalSemaphores[0];
    }
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_hCommandBuffer;

        
    res = GetVulkan()->vkQueueSubmit(m_hQueuePresent, 1, &submitInfo, m_hCommandBufferFence);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkQueueSubmit() failed with error=" << res);
    pView->pSurface->Sync.hFence = m_hCommandBufferFence;

    return AMF_OK;
}

AMF_RESULT VideoPresenterVulkan::UpdateVertices(AMFRect *srcRect, AMFSize *srcSize, AMFRect *dstRect, AMFSize *dstSize)
{

    amf::AMFVulkanDevice* pVulkanDev = (amf::AMFVulkanDevice*)m_hVulkanDev;    

    VkResult res = VK_INCOMPLETE;

    if(*srcRect == m_sourceVertexRect  &&  *dstRect == m_destVertexRect)
    {
        return AMF_OK;   
    }
    m_sourceVertexRect = *srcRect;
    m_destVertexRect = *dstRect;

    // stretch video rect to back buffer
    float  w=2.f;
    float  h=2.f;

    w *= m_fScale;
    h *= m_fScale;

    w *= (float)dstRect->Width() / dstSize->width;
    h *= (float)dstRect->Height() / dstSize->height;

    if(h < 0 || w < 0)
    {
        int a = 1;
    }

    float centerX = m_fOffsetX * 2.f / dstRect->Width();
    float centerY = - m_fOffsetY * 2.f/ dstRect->Height();

    float leftDst = centerX - w / 2;
    float rightDst = leftDst + w;
    float topDst = centerY - h / 2;
    float bottomDst = topDst + h;

    centerX = (float)(srcRect->left + srcRect->right) / 2.f / srcRect->Width();
    centerY = (float)(srcRect->top + srcRect->bottom) / 2.f / srcRect->Height();

    w = (float)srcRect->Width() / srcSize->width;
    h = (float)srcRect->Height() / srcSize->height;

    float leftSrc = centerX - w/2;
    float rightSrc = leftSrc + w;
    float topSrc = centerY - h/2;
    float bottomSrc = topSrc + h;

    Vertex vertices[4] = {};

    vertices[0].pos[0] = leftDst;
    vertices[0].pos[1] = bottomDst;
    vertices[0].tex[0] = leftSrc;
    vertices[0].tex[1] = bottomSrc;

    vertices[1].pos[0] = rightDst;
    vertices[1].pos[1] = bottomDst;
    vertices[1].tex[0] = rightSrc;
    vertices[1].tex[1] = bottomSrc;

    vertices[2].pos[0] = leftDst;
    vertices[2].pos[1] = topDst;
    vertices[2].tex[0] = leftSrc;
    vertices[2].tex[1] = topSrc;

    vertices[3].pos[0] = rightDst;
    vertices[3].pos[1] = topDst;
    vertices[3].tex[0] = rightSrc;
    vertices[3].tex[1] = topSrc;

	GetVulkan()->vkDeviceWaitIdle(pVulkanDev->hDevice);
    
    void* bufferData = NULL;
    res = GetVulkan()->vkMapMemory(pVulkanDev->hDevice, m_VertexBuffer.hMemory, 0, sizeof(vertices), 0, &bufferData);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkMapMemory() failed with error=" << res);

    memcpy(bufferData, &vertices, sizeof(vertices));
	GetVulkan()->vkUnmapMemory(pVulkanDev->hDevice, m_VertexBuffer.hMemory);

    return AMF_OK;
}

AMF_RESULT AMF_STD_CALL VideoPresenterVulkan::AllocSurface(amf::AMF_MEMORY_TYPE type, amf::AMF_SURFACE_FORMAT format,
            amf_int32 width, amf_int32 height, amf_int32 hPitch, amf_int32 vPitch, amf::AMFSurface** ppSurface)
{
    if(!m_bRenderToBackBuffer)
    {
        return AMF_NOT_IMPLEMENTED;
    }
    AMF_RESULT err = AMF_OK;
    // wait till buffers are released
    while( m_uiAvailableBackBuffer + 1 >= (amf_uint32)m_BackBuffers.size())
    {
        if(m_bFrozen)
        {
            return AMF_INPUT_FULL;
        }
        amf_sleep(1);
    }

    if(m_bResizeSwapChain)
    {
        // wait till all buffers are released
        while( m_uiAvailableBackBuffer > 0)
        {
            amf_sleep(1);
        }
        ResizeSwapChain();
        UpdateProcessor();
        m_bResizeSwapChain = false;
    }

    amf::AMFLock lock(&m_sect);
    AMF_RESULT res = AMF_OK;
    // Ignore sizes and return back buffer
    amf_uint32 imageIndex = 0;

    err = AcquireBackBuffer(&imageIndex);
    CHECK_AMF_ERROR_RETURN(err, L"AcquireBackBuffer() failed");

    BackBuffer &backBuffer = m_BackBuffers[imageIndex];

    m_uiAvailableBackBuffer++;
    SwapChainVulkan::m_pContext->CreateSurfaceFromVulkanNative((void*)&backBuffer.m_Surface, ppSurface, this);

    amf::AMFComputePtr pCompute;
    SwapChainVulkan::m_pContext->GetCompute(amf::AMF_MEMORY_VULKAN, &pCompute);
    AMFPlane *pPlane = (*ppSurface)->GetPlaneAt(0);
    amf_size origin[3] = {0, 0, 0};
    amf_size region[3] = {(amf_size)pPlane->GetWidth(), (amf_size)pPlane->GetHeight(), 0};
    amf_uint8 color[4]= {0,0,0,0};
    pCompute->FillPlane(pPlane, origin, region, color);

    m_TrackSurfaces.push_back(*ppSurface);
    return res;
}

void AMF_STD_CALL VideoPresenterVulkan::OnSurfaceDataRelease(amf::AMFSurface* pSurface)
{
    amf::AMFLock lock(&m_sect);
    for(std::vector<amf::AMFSurface*>::iterator it = m_TrackSurfaces.begin(); it != m_TrackSurfaces.end(); it++)
    {
        if( *it == pSurface)
        {
            pSurface->RemoveObserver(this);
            m_TrackSurfaces.erase(it);
            break;
        }
    }
}

AMF_RESULT              VideoPresenterVulkan::SetInputFormat(amf::AMF_SURFACE_FORMAT format)
{
    if(format != amf::AMF_SURFACE_BGRA && format != amf::AMF_SURFACE_RGBA)
    {
        return AMF_FAIL;
    }
    m_eInputFormat = format;
    return AMF_OK;
}

AMF_RESULT              VideoPresenterVulkan::Flush()
{
    m_uiAvailableBackBuffer = 0;
    return BackBufferPresenter::Flush();
}


AMF_RESULT VideoPresenterVulkan::CheckForResize(bool bForce, bool *bResized)
{
    *bResized = false;
    AMF_RESULT err = AMF_OK;

    AMFRect clientRect= GetClientRect();

    amf_int width=clientRect.right-clientRect.left;
    amf_int height=clientRect.bottom-clientRect.top;

    if(!bForce && ((width==(amf_int)m_SwapChainExtent.width && height==(amf_int)m_SwapChainExtent.height) || width == 0 || height == 0 ))
    {
        return AMF_OK;
    }
    *bResized = true;
    m_bResizeSwapChain = true;
    return AMF_OK;

}

AMF_RESULT VideoPresenterVulkan::ResizeSwapChain()
{
    AMF_RESULT res = AMF_OK;

    amf::AMFVulkanDevice* pVulkanDev = (amf::AMFVulkanDevice*)m_hVulkanDev;    
	GetVulkan()->vkDeviceWaitIdle(pVulkanDev->hDevice);

    AMFRect clientRect = m_rectClientResize;

    amf_int width=clientRect.right-clientRect.left;
    amf_int height=clientRect.bottom-clientRect.top;    //MM TODO
    amf_handle hWnd = m_hwnd;
    amf_handle hDisplay = m_hDisplay;

    Terminate();
    m_hwnd = hWnd;
    m_hDisplay = hDisplay;
    Init(width, height);

    m_rectClient = clientRect;
    return AMF_OK;
}
void AMF_STD_CALL  VideoPresenterVulkan::CheckForResize() // call from UI thread (for VulkanPresenter on Linux)
{
    amf::AMFLock lock(&m_sect);

    bool bResized = false;
    CheckForResize(false, &bResized);

    if(bResized)
    {
        amf::AMFLock lock(&m_sect);
        m_rectClientResize = GetClientRect();
    }
    
    if(!m_bRenderToBackBuffer)
    {
        if(bResized)
        {
            ResizeSwapChain();
            m_bResizeSwapChain = false;
        }
    }    
}
