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

#include "VideoRender.h"
#include "../common/SwapChainDX12.h"
#include <atlbase.h>
#include <d3d11.h>
#include "public/samples/CPPSamples/common/d3dx12.h"
#include <dxgi1_4.h>

struct BufferDX12
{
    ATL::CComPtr<ID3D12Resource>    pBuffer;
    ATL::CComPtr<ID3D12Resource>    pUpload;
};

struct VertexBufferDX12 : public BufferDX12
{
    D3D12_VERTEX_BUFFER_VIEW        view;
};
struct IndexBufferDX12 : public BufferDX12
{
    D3D12_INDEX_BUFFER_VIEW       view;
};

typedef BufferDX12 ConstantBufferDX12;


class VideoRenderDX12 : public VideoRender, public SwapChainDX12
{
public:
    VideoRenderDX12(amf_int width, amf_int height, bool bInterlaced, amf_int frames, amf::AMFContext* pContext);
    virtual ~VideoRenderDX12();

    virtual AMF_RESULT              Init(amf_handle hWnd, amf_handle hDisplay, bool bFullScreen);
    virtual AMF_RESULT              Terminate();
    virtual AMF_RESULT              Render(amf::AMFData** ppData);
    virtual amf::AMF_SURFACE_FORMAT GetFormat() { return amf::AMF_SURFACE_BGRA; }
protected:
    AMF_RESULT RegisterDescriptors();
    AMF_RESULT CreateBuffer(ID3D12Resource** ppBuffer, ID3D12Resource** ppBufferUpload, const D3D12_RESOURCE_DESC& resourceDesc);
    AMF_RESULT UpdateBuffer(ID3D12Resource* pBuffer, ID3D12Resource* pBufferUpload, const void* pData, amf_size size, amf_size dstOffset, amf_bool immediate);
    AMF_RESULT PrepareStates();
    AMF_RESULT CreatePipeline();
    AMF_RESULT RenderScene(BackBufferDX12* pTarget);
    AMF_RESULT CreateShader(LPCSTR szEntryPoint, LPCSTR szModel, ID3DBlob** ppBlobOut);


    VertexBufferDX12                        m_VertexBuffer;
    IndexBufferDX12                         m_IndexBuffer;
    ConstantBufferDX12                      m_ConstantBuffer;

    float                                   m_fAnimation;

    ATL::CComPtr<ID3D12Device>              m_pDX12Device;
    ATL::CComPtr<ID3D12PipelineState>       m_pPipelineState;
    ATL::CComPtr<ID3D12RootSignature>       m_pRootSignature;
    DescriptorHeapPoolDX12                  m_RenderdesHeapPool;
    bool                                    m_bWindow;
    CommandBufferDX12                       m_RenderCmdBuffer;

};