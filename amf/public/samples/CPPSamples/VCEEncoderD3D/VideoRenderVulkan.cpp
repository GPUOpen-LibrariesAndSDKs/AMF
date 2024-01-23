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

#include "VideoRenderVulkan.h"
#include "../common/CmdLogger.h"
#include "public/common/ByteArray.h"
#include "public/common/TraceAdapter.h"
#include "public/common/AMFSTL.h"
#include "public/common/AMFMath.h"
#include <iostream>
#include <fstream>
#if defined(_WIN32)
#else
   #include <unistd.h>
#endif

//#define VK_NO_PROTOTYPES
#if defined(_WIN32)
    #define VK_USE_PLATFORM_WIN32_KHR
#endif
#include "vulkan/vulkan.h"

using namespace amf;

//Cube data
struct Vertex{
    float pos[3];
    float color[4];
};

struct ModelView {
    float model[16];
    float view[16];
    float proj[16];
};

VideoRenderVulkan::VideoRenderVulkan(amf_int width, amf_int height, bool bInterlaced, amf_int frames, amf::AMFContext* pContext)
    :VideoRender(width, height, bInterlaced, frames, pContext),
    SwapChainVulkan(pContext),
    m_fAnimation(0),
    m_hPipelineLayout(NULL),
    m_hPipeline(NULL),
    m_hDescriptorSet(NULL),
    m_hDescriptorPool(NULL)
{
    memset(&m_VertexBuffer, 0, sizeof(m_VertexBuffer));
    memset(&m_IndexBuffer, 0, sizeof(m_IndexBuffer));
    memset(&m_MVPBuffer, 0, sizeof(m_MVPBuffer));
}
VideoRenderVulkan::~VideoRenderVulkan()
{
    Terminate();
}

AMF_RESULT VideoRenderVulkan::Init(amf_handle hWnd, amf_handle hDisplay, bool bFullScreen)
{
    AMF_RESULT res = AMF_OK;
    m_fAnimation = 0;

    VkFormat format = VK_FORMAT_UNDEFINED;
    switch(GetFormat())
    {
    case amf::AMF_SURFACE_BGRA: format = VK_FORMAT_B8G8R8A8_UNORM; break;
    case amf::AMF_SURFACE_RGBA: format = VK_FORMAT_R8G8B8A8_UNORM; break;
    }

    CHECK_RETURN(m_width != 0 && m_height != 0 && format != VK_FORMAT_UNDEFINED, AMF_FAIL, L"Bad width/height: width=" << m_width << L" height=" << m_height << L" format" << amf::AMFSurfaceGetFormatName(GetFormat()));

    res = SwapChainVulkan::Init(hWnd, hDisplay, nullptr, m_width, m_height, GetFormat(), bFullScreen);
    CHECK_AMF_ERROR_RETURN(res, L"SwapChainVulkan::Init() failed");

    res = CreatePipelineInput();
    CHECK_AMF_ERROR_RETURN(res, L"CreatePipelineInput() failed");

    res = CreateDescriptorSetLayout();
    CHECK_AMF_ERROR_RETURN(res, L"CreateDescriptorSetLayout() failed");

    res = CreatePipeline();
    CHECK_AMF_ERROR_RETURN(res, L"CreatePipeline() failed");

    res = CreateCommands();
    CHECK_AMF_ERROR_RETURN(res, L"CreateCommands() failed");

    return AMF_OK;
}


AMF_RESULT VideoRenderVulkan::Terminate()
{
    if(m_pVulkanDevice == NULL)
    {
        return AMF_OK;
    }

    //VkSampler                        m_hTextureSampler;
    if(m_hDescriptorSet != NULL)
    {
        GetVulkan()->vkFreeDescriptorSets(m_pVulkanDevice->hDevice, m_hDescriptorPool, 1, &m_hDescriptorSet);
        m_hDescriptorPool = NULL;
    }
    if (m_hDescriptorPool != NULL)
    {
        GetVulkan()->vkDestroyDescriptorPool(m_pVulkanDevice->hDevice, m_hDescriptorPool, nullptr);
        m_hDescriptorPool = NULL;
    }
    DestroyBuffer(m_VertexBuffer);
    DestroyBuffer(m_IndexBuffer);
    DestroyBuffer(m_MVPBuffer);


    if (m_hPipeline != NULL)
    {
        GetVulkan()->vkDestroyPipeline(m_pVulkanDevice->hDevice, m_hPipeline, nullptr);
        m_hPipeline = NULL;
    }
    if (m_hPipelineLayout != NULL)
    {
        GetVulkan()->vkDestroyPipelineLayout(m_pVulkanDevice->hDevice, m_hPipelineLayout, nullptr);
        m_hPipelineLayout = NULL;
    }
    if (m_hUniformLayout != NULL)
    {
        GetVulkan()->vkDestroyDescriptorSetLayout(m_pVulkanDevice->hDevice, m_hUniformLayout, nullptr);
        m_hUniformLayout = NULL;
    }
    if(m_CommandBuffers.size() > 0)
    {
        GetVulkan()->vkFreeCommandBuffers(m_pVulkanDevice->hDevice, m_hCommandPool, (uint32_t)m_CommandBuffers.size(), m_CommandBuffers.data());
    }
    m_CommandBuffers.clear();




    return SwapChainVulkan::Terminate();
}


AMF_RESULT VideoRenderVulkan::CreatePipelineInput()
{
    return AMF_OK;
}
AMF_RESULT VideoRenderVulkan::CreateDescriptorSetLayout()
{
    VkResult res = VK_INCOMPLETE;

    VkDescriptorSetLayoutBinding MVPLayoutBinding = {};
        MVPLayoutBinding.binding = 0;
        MVPLayoutBinding.descriptorCount = 1;
        MVPLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        MVPLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        /*
    VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
        samplerLayoutBinding.binding = 1;
        samplerLayoutBinding.descriptorCount = 1;
        samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        samplerLayoutBinding.pImmutableSamplers = nullptr;

    std::vector<VkDescriptorSetLayoutBinding> bindings = { MVPLayoutBinding, samplerLayoutBinding };
    */
    std::vector<VkDescriptorSetLayoutBinding> bindings = { MVPLayoutBinding};
    VkDescriptorSetLayoutCreateInfo layoutCreateInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutCreateInfo.bindingCount = (uint32_t)bindings.size();
    layoutCreateInfo.pBindings = bindings.data();
    layoutCreateInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsCreateInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    std::vector<VkDescriptorBindingFlags> bindingFlags;
    bindingFlags.push_back(VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT /*| VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT*/);

    flagsCreateInfo.bindingCount = (uint32_t)bindingFlags.size();
    flagsCreateInfo.pBindingFlags = bindingFlags.data();

    layoutCreateInfo.pNext = &flagsCreateInfo;

    res = GetVulkan()->vkCreateDescriptorSetLayout(m_pVulkanDevice->hDevice, &layoutCreateInfo, nullptr, &m_hUniformLayout);
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
AMF_RESULT VideoRenderVulkan::CreatePipeline()
{
    VkResult res = VK_INCOMPLETE;
    AMF_RESULT resAMF = AMF_OK;

    const wchar_t*  pCubeShaderFileNameVert = L"cube.vert.spv";
    const wchar_t*  pCubeShaderFileNameFraq = L"cube.frag.spv";

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

    res = GetVulkan()->vkCreateShaderModule(m_pVulkanDevice->hDevice, &vertModuleCreateInfo, nullptr, &vertModule);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkCreateShaderModule(" << pCubeShaderFileNameVert << L") failed with error=" << res);

    res = GetVulkan()->vkCreateShaderModule(m_pVulkanDevice->hDevice, &fragModuleCreateInfo, nullptr, &fragModule);
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

    //Color
    vertexAttribDesc[1].binding = 0;
    vertexAttribDesc[1].location = 1;
    vertexAttribDesc[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    vertexAttribDesc[1].offset = offsetof(Vertex, color);


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
        viewport.width = (float)m_size.width;
        viewport.height = (float)m_size.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
        scissor.offset = { 0, 0 };
        scissor.extent.width = m_size.width;
        scissor.extent.height = m_size.height;

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
        rasterizerInfo.cullMode = VK_CULL_MODE_BACK_BIT;
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

    VkDescriptorSetLayout setLayouts[] = {m_hUniformLayout};
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = setLayouts;

    VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
        depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilState.depthTestEnable = VK_TRUE;
        depthStencilState.depthWriteEnable = VK_TRUE;
        depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        depthStencilState.depthBoundsTestEnable = VK_FALSE;
        depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.passOp = VK_STENCIL_OP_KEEP;
        depthStencilState.back.compareOp = VK_COMPARE_OP_ALWAYS;
        depthStencilState.stencilTestEnable = VK_FALSE;
        depthStencilState.front = depthStencilState.back;


    res = GetVulkan()->vkCreatePipelineLayout(m_pVulkanDevice->hDevice, &pipelineLayoutInfo, nullptr, &m_hPipelineLayout);
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

    res = GetVulkan()->vkCreateGraphicsPipelines(m_pVulkanDevice->hDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_hPipeline);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkCreateGraphicsPipelines() failed with error=" << res);
    //cleanup
    GetVulkan()->vkDestroyShaderModule(m_pVulkanDevice->hDevice, vertModule, nullptr);
    GetVulkan()->vkDestroyShaderModule(m_pVulkanDevice->hDevice, fragModule, nullptr);

    return AMF_OK;
}

AMF_RESULT VideoRenderVulkan::CreateCommands()
{
    VkResult res = VK_INCOMPLETE;
    AMF_RESULT resAMF = AMF_OK;


    resAMF = CreateVertexBuffers();
    CHECK_AMF_ERROR_RETURN(resAMF, L"CreateVertexBuffers() failed");

    resAMF = CreateDescriptorSetPool();
    CHECK_AMF_ERROR_RETURN(resAMF, L"CreateDescriptorSetPool() failed");

    m_CommandBuffers.resize(GetBackBufferCount());

    VkCommandBufferAllocateInfo bufferAllocInfo = {};
        bufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        bufferAllocInfo.commandPool = m_hCommandPool;
        bufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        bufferAllocInfo.commandBufferCount = (uint32_t)m_CommandBuffers.size();

    res = GetVulkan()->vkAllocateCommandBuffers(m_pVulkanDevice->hDevice, &bufferAllocInfo, m_CommandBuffers.data());
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkAllocateCommandBuffers() failed with error=" << res);

    for (uint32_t i = 0; i < m_CommandBuffers.size(); i++)
    {
        BackBufferVulkan* pBackBuffer = (BackBufferVulkan*)m_pBackBuffers[i].get();
        VkCommandBuffer commandBuffer = m_CommandBuffers[i];

        VkCommandBufferBeginInfo beginInfo = {};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

        GetVulkan()->vkBeginCommandBuffer(commandBuffer, &beginInfo);

        VkClearValue clearColor = { 0.0f, 0.5f, 0.0f, 1.0f };
        VkRenderPassBeginInfo renderPassInfo = {};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass = m_hRenderPass;
            renderPassInfo.framebuffer = pBackBuffer->m_hFrameBuffer;
            renderPassInfo.renderArea.offset = { 0, 0 };
            renderPassInfo.renderArea.extent.width = m_size.width;
            renderPassInfo.renderArea.extent.height = m_size.height;
            renderPassInfo.clearValueCount = 1;
            renderPassInfo.pClearValues = &clearColor;

        GetVulkan()->vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        GetVulkan()->vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_hPipeline);

        VkBuffer vertexBuffers[] = { m_VertexBuffer.hBuffer };
        VkDeviceSize offsets[] = {0};
        GetVulkan()->vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        GetVulkan()->vkCmdBindIndexBuffer(commandBuffer, m_IndexBuffer.hBuffer, 0, VK_INDEX_TYPE_UINT32);

        GetVulkan()->vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_hPipelineLayout, 0, 1, &m_hDescriptorSet, 0, nullptr);

        GetVulkan()->vkCmdDrawIndexed(commandBuffer, uint32_t(m_IndexBuffer.iSize / sizeof(uint32_t)), 1, 0, 0, 0);

        GetVulkan()->vkCmdEndRenderPass(commandBuffer);
        res = GetVulkan()->vkEndCommandBuffer(commandBuffer);
        CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkEndCommandBuffer() failed with error=" << res);
    }
    return AMF_OK;
}

AMF_RESULT VideoRenderVulkan::CreateVertexBuffers()
{
    // Create vertex buffer
    
    Vertex vertexes[] =
    {
        { { -1.0f, 1.0f, -1.0f  },   { 0.0f, 0.0f, 1.0f, 1.0f } },
        { { 1.0f, 1.0f, -1.0f   },   { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { 1.0f, 1.0f, 1.0f    },   { 0.0f, 1.0f, 1.0f, 1.0f } },
        { { -1.0f, 1.0f, 1.0f   },   { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { -1.0f, -1.0f, -1.0f },   { 1.0f, 0.0f, 1.0f, 1.0f } },
        { { 1.0f, -1.0f, -1.0f  },   { 1.0f, 1.0f, 0.0f, 1.0f } },
        { { 1.0f, -1.0f, 1.0f   },   { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { -1.0f, -1.0f, 1.0f  },   { 0.0f, 0.0f, 0.0f, 1.0f } },
    };
// Create index buffer
    uint32_t indexes[] =
    {   3,1,0,    2,1,3,
        0,5,4,    1,5,0,
        3,4,7,  0,4,3,
        1,6,5,  2,6,1,
        2,7,6,  3,7,2,
        6,4,5,  7,4,6,
    };


    MakeBuffer((void*)vertexes, sizeof(vertexes), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_VertexBuffer);
    MakeBuffer((void*)indexes, sizeof(indexes), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_IndexBuffer);
    MakeBuffer(NULL, sizeof(ModelView), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_MVPBuffer);
    
    return AMF_OK;
}
AMF_RESULT VideoRenderVulkan::CreateDescriptorSetPool()
{
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
        poolCreateInfo.maxSets = 1;
        poolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

    res = GetVulkan()->vkCreateDescriptorPool(m_pVulkanDevice->hDevice, &poolCreateInfo, nullptr, &m_hDescriptorPool);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkCreateDescriptorPool() failed with error=" << res);

    VkDescriptorSetLayout layouts[] = { m_hUniformLayout };
    VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_hDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = layouts;

    res = GetVulkan()->vkAllocateDescriptorSets(m_pVulkanDevice->hDevice, &allocInfo, &m_hDescriptorSet);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkAllocateDescriptorSets() failed with error=" << res);

    VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = m_MVPBuffer.hBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(ModelView);

    VkWriteDescriptorSet descriptorWrite = {};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_hDescriptorSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

    std::vector<VkWriteDescriptorSet> descriptors = { descriptorWrite };

    GetVulkan()->vkUpdateDescriptorSets(m_pVulkanDevice->hDevice, (uint32_t)descriptors.size(), descriptors.data(), 0, nullptr);

    return AMF_OK;
}

AMF_RESULT VideoRenderVulkan::UpdateMVP()
{
    VkResult res = VK_INCOMPLETE;

    amf::Vector Eye ( 0.0f, 1.0f, -5.0f, 0.0f );
    amf::Vector At  ( 0.0f, 1.0f, 0.0f, 0.0f );
    amf::Vector Up  ( 0.0f, 1.0f, 0.0f, 0.0f );

    amf::Matrix view; view.LookAtLH( Eye, At, Up );
    amf::Matrix proj; proj.PerspectiveFovLH( amf::AMF_PI / 2.0f, m_width / (float)m_height, 0.01f, 1000.0f );
    amf::Matrix world; world.RotationRollPitchYaw(m_fAnimation, -m_fAnimation, 0);

    // Update variables

    ModelView mvp = {};

    memcpy(mvp.model, world.k, sizeof(mvp.model));
    memcpy(mvp.proj, proj.k, sizeof(mvp.proj));
    memcpy(mvp.view, view.k, sizeof(mvp.view));

    void* bufferData = NULL;
    res = GetVulkan()->vkMapMemory(m_pVulkanDevice->hDevice, m_MVPBuffer.hMemory, 0, sizeof(ModelView), 0, &bufferData);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkMapMemory() failed with error=" << res);

    memcpy(bufferData, &mvp, sizeof(ModelView));
    GetVulkan()->vkUnmapMemory(m_pVulkanDevice->hDevice, m_MVPBuffer.hMemory);

    m_fAnimation += amf::AMF_PI *2.0f /240.f;

    return AMF_OK;
}



AMF_RESULT VideoRenderVulkan::Render(amf::AMFData** ppData)
{
    UpdateMVP();

    VkResult res = VK_INCOMPLETE;
    AMF_RESULT resAMF = AMF_OK;

    amf_uint32 imageIndex = 0;
    resAMF = AcquireNextBackBufferIndex(imageIndex);
    CHECK_AMF_ERROR_RETURN(resAMF, L"AcquireBackBuffer() failed");

    BackBufferVulkan* pBackBuffer = (BackBufferVulkan*)m_pBackBuffers[imageIndex].get();

    VkPipelineStageFlags waitFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

/*
    AMFTraceInfo(AMF_FACILITY, L"+++++++++++++++vkQueueSubmit(GFX ) Start +++++++++++++");
    if(pBackBuffer->m_surface.Sync.bSubmitted)
    {
        AMFTraceInfo(AMF_FACILITY, L"    Wait:   0x%llu", pBackBuffer->m_surface.Sync.hSemaphore);
    }
    AMFTraceInfo(AMF_FACILITY, L"    Signal: 0x%llu", pBackBuffer->m_surface.Sync.hSemaphore);
    AMFTraceInfo(AMF_FACILITY, L"+++++++++++++++vkQueueSubmit(GFX ) End  +++++++++++++");
*/

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    if(pBackBuffer->m_surface.Sync.hSemaphore != VK_NULL_HANDLE)
    {
        if(pBackBuffer->m_surface.Sync.bSubmitted)
        { 
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &pBackBuffer->m_surface.Sync.hSemaphore;
            submitInfo.pWaitDstStageMask = &waitFlags;
        }
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &pBackBuffer->m_surface.Sync.hSemaphore;
        pBackBuffer->m_surface.Sync.bSubmitted = true;
    }
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_CommandBuffers[imageIndex];

    res = GetVulkan()->vkQueueSubmit(m_hQueuePresent, 1, &submitInfo, NULL);
    CHECK_RETURN(res == VK_SUCCESS, AMF_FAIL, L"vkQueueSubmit() failed with error=" << res);

    amf::AMFSurfacePtr pSwapChainSurface;
    resAMF = SwapChainVulkan::m_pContext1->CreateSurfaceFromVulkanNative(&pBackBuffer->m_surface, &pSwapChainSurface, NULL);
    CHECK_AMF_ERROR_RETURN(resAMF, L"CreateSurfaceFromVulkanNative() failed");

    resAMF = pSwapChainSurface->Duplicate(pSwapChainSurface->GetMemoryType(), ppData);
    CHECK_AMF_ERROR_RETURN(resAMF, L"Duplicate() failed");

    resAMF = Present(true);
    CHECK_AMF_ERROR_RETURN(resAMF, L"Present() failed");

    return AMF_OK;
}
