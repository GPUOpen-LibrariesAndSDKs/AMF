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
// Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.
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
#include "StitchEngineDX11.h"
#include "StitchD3D11_ps.bin.h"
#include "StitchD3D11_psc.bin.h"
#include "StitchD3D11_vs.bin.h"
#include "StitchD3D11_gs.bin.h"

#include "public\common\TraceAdapter.h"

#include <DirectXMath.h>
#include <math.h>
#include <omp.h> 

using namespace DirectX;
using namespace amf;
using namespace DirectX;

#pragma comment(lib, "d3dcompiler.lib")

#define AMF_FACILITY L"StitchEngineDX11"

#define ALPHA_BLEND_ENABLED 1
#define HIST_SIZE   256

inline static amf_uint32 AlignValue(amf_uint32 value, amf_uint32 alignment)
{
    return ((value + (alignment - 1)) & ~(alignment - 1));
}

extern XMVECTOR MakeSphere(XMVECTOR src, float centerX,float centerY,float centerZ, float newRadius);

StitchEngineDX11::StitchEngineDX11(AMFContext* pContext) : 
StitchEngineBase(pContext),
    m_bWireRender(false),
    m_eOutputMode(AMF_VIDEO_STITCH_OUTPUT_MODE_PREVIEW)
{

    m_pCameraOrientation = (Transform*)amf_aligned_alloc(sizeof(Transform), alignof(Transform));
    if (m_pCameraOrientation != nullptr)
    {
        ::memset(m_pCameraOrientation, 0, sizeof(Transform));
    }
}
//-------------------------------------------------------------------------------------------------
StitchEngineDX11::~StitchEngineDX11()
{
    if (m_pCameraOrientation != nullptr)
    {
        amf_aligned_free(m_pCameraOrientation);
        m_pCameraOrientation = nullptr;
    }

    Terminate();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL StitchEngineDX11::Init(AMF_SURFACE_FORMAT formatInput, amf_int32 widthInput, amf_int32 heightInput, AMF_SURFACE_FORMAT formatOutput, amf_int32 widthOutput, amf_int32 heightOutput, AMFPropertyStorage *pStorage, AMFPropertyStorage **ppStorageInputs)
{
    static bool bOMPInit = false;
    if(!bOMPInit)
    {
        bOMPInit = true;
        omp_set_num_threads(10);  
    }

    AMF_RESULT res = AMF_OK;
    HRESULT hr = S_OK;

    AMF_RETURN_IF_FALSE(formatInput == AMF_SURFACE_NV12, AMF_INVALID_FORMAT, L"Invalid input format. Expected AMF_SURFACE_BGRA");
    AMF_RETURN_IF_FALSE(formatOutput == AMF_SURFACE_BGRA || formatOutput == AMF_SURFACE_RGBA || formatOutput == AMF_SURFACE_RGBA_F16, AMF_INVALID_FORMAT, L"Invalid output format. Expected AMF_SURFACE_BGRA");

    m_pd3dDevice = (ID3D11Device*)m_pContext->GetDX11Device();
    AMF_RETURN_IF_FALSE(m_pd3dDevice != NULL, AMF_NOT_INITIALIZED);

    m_pd3dDevice->GetImmediateContext(&m_pd3dDeviceContext);

    //---------------------------------------------------------------------------------------------
    // Create the vertex shader
    //---------------------------------------------------------------------------------------------

    hr = m_pd3dDevice->CreateVertexShader(StitchD3D11_vs, sizeof(StitchD3D11_vs), NULL, &m_pVertexShader);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateVertexShader() failed");

    hr = m_pd3dDevice->CreateGeometryShader(StitchD3D11_gs, sizeof(StitchD3D11_gs), NULL, &m_pGeometryShader);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateVertexShader() failed");

    //---------------------------------------------------------------------------------------------
    // Create the input layout
    //---------------------------------------------------------------------------------------------
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    hr = m_pd3dDevice->CreateInputLayout(layout, _countof(layout), StitchD3D11_vs, sizeof(StitchD3D11_vs), &m_pVertexLayout);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateInputLayout() failed");

    //---------------------------------------------------------------------------------------------
    // Create pixel shader
    //---------------------------------------------------------------------------------------------
    hr = m_pd3dDevice->CreatePixelShader(StitchD3D11_ps, sizeof(StitchD3D11_ps), NULL, &m_pPixelShader);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreatePixelShader(PS) failed");

    hr = m_pd3dDevice->CreatePixelShader(StitchD3D11_psc, sizeof(StitchD3D11_psc), NULL, &m_pPixelShaderCube);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreatePixelShader(PS) failed");

    // Create the sample state
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;

    sampDesc.AddressW = sampDesc.AddressV = sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;

    sampDesc.BorderColor[0] = 1.0f;
    sampDesc.BorderColor[1] = 0.0f;
    sampDesc.BorderColor[2] = 0.0f;
    sampDesc.BorderColor[3] = 0.0f;

    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = m_pd3dDevice->CreateSamplerState( &sampDesc, &m_pSamplerLinear );
    ASSERT_RETURN_IF_HR_FAILED(hr,AMF_DIRECTX_FAILED,L"InitializeRenderData() - CreateSamplerState() failed");

    D3D11_DEPTH_STENCIL_DESC depthDisabledStencilDesc={};
    depthDisabledStencilDesc.DepthEnable = FALSE;
    depthDisabledStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthDisabledStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;
    depthDisabledStencilDesc.StencilEnable = FALSE;
    depthDisabledStencilDesc.StencilReadMask = 0xFF;
    depthDisabledStencilDesc.StencilWriteMask = 0xFF;
    depthDisabledStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    depthDisabledStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
    depthDisabledStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    depthDisabledStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    depthDisabledStencilDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    depthDisabledStencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
    depthDisabledStencilDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    depthDisabledStencilDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

    hr = m_pd3dDevice->CreateDepthStencilState(&depthDisabledStencilDesc, &m_pDepthStencilState);
    ASSERT_RETURN_IF_HR_FAILED(hr,AMF_DIRECTX_FAILED,L"InitializeRenderData() - CreateDepthStencilState() failed");

// Create the rasterizer state which will determine how and what polygons will be drawn.
    D3D11_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.AntialiasedLineEnable = TRUE;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.DepthBias = 0;
    rasterDesc.DepthBiasClamp = 0.0f;
    rasterDesc.DepthClipEnable = TRUE;
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.FrontCounterClockwise = FALSE;
    rasterDesc.MultisampleEnable = FALSE;
    rasterDesc.ScissorEnable = FALSE;
    rasterDesc.SlopeScaledDepthBias = 0.0f;

    // Create the rasterizer state from the description we just filled out.
    hr = m_pd3dDevice->CreateRasterizerState(&rasterDesc, &m_pRasterizerState);
    ASSERT_RETURN_IF_HR_FAILED(hr,AMF_DIRECTX_FAILED,L"InitializeRenderData() - CreateRasterizerState() failed");

    rasterDesc.FillMode = D3D11_FILL_WIREFRAME;
    hr = m_pd3dDevice->CreateRasterizerState(&rasterDesc, &m_pRasterizerStateWire);
    ASSERT_RETURN_IF_HR_FAILED(hr,AMF_DIRECTX_FAILED,L"InitializeRenderData() - CreateRasterizerState() failed");
    

    D3D11_BLEND_DESC blendDesc = {};
#if ALPHA_BLEND_ENABLED
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
#else
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
#endif
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_DEST_ALPHA;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = 0x0F;

    hr = m_pd3dDevice->CreateBlendState( &blendDesc, &m_pBlendState);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateBlendState failed %x", hr);

    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA InitData={};
    amf_int32 inputCount=0;
    pStorage->GetProperty(AMF_VIDEO_STITCH_INPUTCOUNT, &inputCount);

    m_StreamList.resize(inputCount);

    XMMATRIX orientation = XMMatrixIdentity();

    AMF_RETURN_IF_INVALID_POINTER(m_pCameraOrientation, L"Invalid m_pCameraOrientation pointer");
    memcpy(m_pCameraOrientation->m_WorldViewProjection, &orientation, sizeof(m_pCameraOrientation->m_WorldViewProjection));

    Transform trnasform;
    Transform cubemap[6];
    res = GetTransform(widthInput, heightInput, widthOutput, heightOutput, pStorage, *m_pCameraOrientation, trnasform, cubemap);

    amf_int64 mode = AMF_VIDEO_STITCH_OUTPUT_MODE_PREVIEW;
    pStorage->GetProperty(AMF_VIDEO_STITCH_OUTPUT_MODE, &mode);
    m_eOutputMode = (AMF_VIDEO_STITCH_OUTPUT_MODE_ENUM)mode;

    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.ByteWidth = sizeof(XMMATRIX);
    bd.StructureByteStride = 0;
    InitData.pSysMem = &trnasform;

    hr = m_pd3dDevice->CreateBuffer( &bd, &InitData, &m_pWorldCB );
    ASSERT_RETURN_IF_HR_FAILED(hr,AMF_DIRECTX_FAILED,L"InitializeRenderData() - CreateBuffer() failed");

    bd.ByteWidth = sizeof(XMMATRIX) * 6;
    bd.StructureByteStride = 0;
    InitData.pSysMem = cubemap;

    hr = m_pd3dDevice->CreateBuffer( &bd, &InitData, &m_pCubemapWorldCB );
    ASSERT_RETURN_IF_HR_FAILED(hr,AMF_DIRECTX_FAILED,L"InitializeRenderData() - CreateBuffer() failed");

    m_ControlPoints.clear();

    for(int i = 0; i < inputCount; i++)
    {
        res = PrepareMesh(widthInput, heightInput, widthOutput, heightOutput, ppStorageInputs[i], pStorage, i, m_StreamList[i].m_Vertices, m_StreamList[i].m_VerticesRowSize,
            m_StreamList[i].m_BorderRect, m_StreamList[i].m_TexRect, m_StreamList[i].m_Sides, m_StreamList[i].m_Corners, m_StreamList[i].m_Plane, m_StreamList[i].m_PlaneCenter, &m_StreamList[i].m_pBorderMap);
    }
    res = ApplyControlPoints();

    res = UpdateRibs(widthInput, heightInput, ppStorageInputs[0]);
    res = UpdateTransparency(ppStorageInputs[0]);
    res = BuildMapForHistogram(widthInput, heightInput);

    for(int i = 0; i < inputCount; i++)
    {
        res = ApplyMode(widthOutput, heightOutput, m_StreamList[i].m_Vertices, m_StreamList[i].m_VerticesRowSize, m_StreamList[i].m_VerticesProjected, m_StreamList[i].m_Indexes, pStorage);
    }

    for(int i = 0; i < inputCount; i++)
    {
        RecreateBuffers(i);
    }
    
    D3D11_QUERY_DESC query_desc;
    query_desc.Query=D3D11_QUERY_EVENT;
    query_desc.MiscFlags=0; 

    ASSERT_RETURN_IF_HR_FAILED(m_pd3dDevice->CreateQuery(&query_desc,&m_pQuery), AMF_DIRECTX_FAILED);
    
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT StitchEngineDX11::RecreateBuffers(amf_int32 index)
{
    HRESULT hr = S_OK;
    m_StreamList[index].m_pIndexBuffer.Release();
    m_StreamList[index].m_pVertexBuffer.Release();

    D3D11_BUFFER_DESC bd = {};
    bd.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA InitData={};
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.StructureByteStride = 0;

    bd.ByteWidth = sizeof(m_StreamList[index].m_VerticesProjected[0]) * (UINT)m_StreamList[index].m_VerticesProjected.size();
    InitData.pSysMem = &m_StreamList[index].m_VerticesProjected[0];

    hr = m_pd3dDevice->CreateBuffer(&bd, &InitData, &m_StreamList[index].m_pVertexBuffer);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateBuffer() - vertex failed");            

    if(m_StreamList[index].m_Indexes.size() > 0)
    {
        bd.ByteWidth = sizeof(m_StreamList[index].m_Indexes[0]) * (UINT)m_StreamList[index].m_Indexes.size();       
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bd.StructureByteStride = 0;

        InitData.pSysMem = &m_StreamList[index].m_Indexes[0];
        hr = m_pd3dDevice->CreateBuffer(&bd, &InitData, &m_StreamList[index].m_pIndexBuffer);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateBuffer() - index failed");            
    }
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL StitchEngineDX11::UpdateMesh(amf_int32 index, amf_int32 widthInput, amf_int32 heightInput, amf_int32 widthOutput, amf_int32 heightOutput, AMFPropertyStorage *pStorage, AMFPropertyStorage *pStorageMain)
{
    AMF_RESULT res = AMF_OK;

    res = PrepareMesh(widthInput, heightInput, widthOutput, heightOutput, pStorage, pStorageMain, index, 
        m_StreamList[index].m_Vertices, m_StreamList[index].m_VerticesRowSize, m_StreamList[index].m_BorderRect, m_StreamList[index].m_TexRect, 
        m_StreamList[index].m_Sides, m_StreamList[index].m_Corners, m_StreamList[index].m_Plane, m_StreamList[index].m_PlaneCenter, &m_StreamList[index].m_pBorderMap);
    res = UpdateRibs(widthInput, heightInput, pStorage);
    res = UpdateTransparency(pStorage);
    res = BuildMapForHistogram(widthInput, heightInput);

    for(int i = 0; i < (int)m_StreamList.size(); i++)
    {
        res = ApplyMode(widthOutput, heightOutput, m_StreamList[i].m_Vertices, m_StreamList[i].m_VerticesRowSize, m_StreamList[i].m_VerticesProjected, m_StreamList[i].m_Indexes, pStorage);
        RecreateBuffers(i);
    }
   
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL      StitchEngineDX11::UpdateFOV(amf_int32 widthInput, amf_int32 heightInput, amf_int32 widthOutput, amf_int32 heightOutput, AMFPropertyStorage *pStorage)
{
    AMF_RESULT res = AMF_OK;
    HRESULT hr = S_OK;
    pStorage->GetProperty(AMF_VIDEO_STITCH_WIRE_RENDER, &m_bWireRender);

    amf_int64 mode = AMF_VIDEO_STITCH_OUTPUT_MODE_PREVIEW;
    pStorage->GetProperty(AMF_VIDEO_STITCH_OUTPUT_MODE, &mode);
    m_eOutputMode = (AMF_VIDEO_STITCH_OUTPUT_MODE_ENUM)mode;


    Transform transform;
    Transform cubemap[6];
    AMF_RETURN_IF_INVALID_POINTER(m_pCameraOrientation, L"Invalid m_pCameraOrientation pointer");
    res = GetTransform(widthInput, heightInput, widthOutput, heightOutput, pStorage, *m_pCameraOrientation, transform, cubemap);
   
    D3D11_MAPPED_SUBRESOURCE MappedResource;
    hr = m_pd3dDeviceContext->Map(m_pWorldCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"Map() - CB failed");            
    memcpy(MappedResource.pData, &transform, sizeof(transform));
    m_pd3dDeviceContext->Unmap(m_pWorldCB, 0);

    if(m_eOutputMode == AMF_VIDEO_STITCH_OUTPUT_MODE_CUBEMAP)
    {
        hr = m_pd3dDeviceContext->Map(m_pCubemapWorldCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"Map() - CB failed");            
        memcpy(MappedResource.pData, cubemap, sizeof(cubemap));
        m_pd3dDeviceContext->Unmap(m_pCubemapWorldCB, 0);
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL      StitchEngineDX11::Terminate()
{
    m_pSurfaceOutput = NULL;
    m_StreamList.clear();

    m_pCubemapWorldCB.Release();
    m_pWorldCB.Release();
    m_pDepthStencil.Release();
    m_pDepthStencilView.Release();
    m_pRenderTargetView.Release();
    m_pBlendState.Release();
    m_pRasterizerState.Release();
    m_pRasterizerStateWire.Release();
    m_pDepthStencilState.Release();
    m_pSamplerLinear.Release();
    m_pVertexLayout.Release();
    m_pPixelShader.Release();
    m_pPixelShaderCube.Release();
    m_pGeometryShader.Release();
    m_pVertexShader.Release();
    m_pQuery.Release();
    m_pQueryOcclusion.Release();
    m_pd3dDeviceContext.Release();
    m_pd3dDevice.Release();
    AMFLock loc(&m_Sect);
    m_AllocationQueue.clear();

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL      StitchEngineDX11::StartFrame(AMFSurface *pSurfaceOutput)
{
    HRESULT hr = S_OK;

    pSurfaceOutput->Convert(AMF_MEMORY_DX11);
    m_pSurfaceOutput = pSurfaceOutput;

    AMFPlane* plane = m_pSurfaceOutput->GetPlane(AMF_PLANE_PACKED);

    // Create render target view
    CComPtr<ID3D11Texture2D> spBackBuffer = (ID3D11Texture2D*)plane->GetNative();

    D3D11_TEXTURE2D_DESC backBufferDesc;
    spBackBuffer->GetDesc(&backBufferDesc);

    D3D11_RENDER_TARGET_VIEW_DESC RenderTargetViewDescription = {};
    switch(backBufferDesc.Format)
    {
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        RenderTargetViewDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        break;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        RenderTargetViewDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    default:
        RenderTargetViewDescription.Format = backBufferDesc.Format;
        break;
    }

    m_pRenderTargetView = NULL;

    if(m_eOutputMode == AMF_VIDEO_STITCH_OUTPUT_MODE_CUBEMAP)
    {
        RenderTargetViewDescription.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        RenderTargetViewDescription.Texture2DArray.ArraySize = 6;
        RenderTargetViewDescription.Texture2DArray.FirstArraySlice = 0;
        RenderTargetViewDescription.Texture2DArray.MipSlice = 0;
    }
    else
    {
        RenderTargetViewDescription.ViewDimension =D3D11_RTV_DIMENSION_TEXTURE2D;                   // render target view is a Texture2D array
        RenderTargetViewDescription.Texture2D.MipSlice = 0;                                   // each array element is one Texture2D
    }
    hr = m_pd3dDevice->CreateRenderTargetView( spBackBuffer, &RenderTargetViewDescription, &m_pRenderTargetView.p );
    ASSERT_RETURN_IF_HR_FAILED(hr,AMF_DIRECTX_FAILED,L"CreateRenderTargetView() - GetBuffer() failed");
    
    D3D11_TEXTURE2D_DESC depthBufferDesc = {};

    if(m_pDepthStencil != NULL)
    {
        m_pDepthStencil->GetDesc(&depthBufferDesc);
    }

    float ClearColor[4] = { 1.0f, 1.0f, 1.0f, 0.0f };
    m_pd3dDeviceContext->ClearRenderTargetView( m_pRenderTargetView, ClearColor );

    // setup view
    m_pd3dDeviceContext->OMSetRenderTargets( 1, &m_pRenderTargetView.p, m_pDepthStencilView );
    m_pd3dDeviceContext->IASetInputLayout(m_pVertexLayout);
    m_pd3dDeviceContext->PSSetSamplers( 0, 1, &m_pSamplerLinear.p );
    m_pd3dDeviceContext->OMSetDepthStencilState(m_pDepthStencilState, 1);
    m_pd3dDeviceContext->RSSetState(m_bWireRender ? m_pRasterizerStateWire : m_pRasterizerState);
    float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_pd3dDeviceContext->OMSetBlendState(m_pBlendState, blendFactor, 0xFFFFFFFF);

    m_pd3dDeviceContext->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_pd3dDeviceContext->VSSetShader(m_pVertexShader, NULL, 0);
    m_pd3dDeviceContext->VSSetConstantBuffers( 0, 1, &m_pWorldCB.p );

    if(m_eOutputMode == AMF_VIDEO_STITCH_OUTPUT_MODE_CUBEMAP)
    {
        m_pd3dDeviceContext->GSSetShader(m_pGeometryShader, NULL, 0);
        m_pd3dDeviceContext->PSSetShader(m_pPixelShaderCube, NULL, 0);
        m_pd3dDeviceContext->GSSetConstantBuffers( 0, 1, &m_pCubemapWorldCB.p );
    }
    else
    {
         m_pd3dDeviceContext->PSSetShader(m_pPixelShader, NULL, 0);
    }

    AMFPlane* planeOutput = m_pSurfaceOutput->GetPlane(AMF_PLANE_PACKED);

    D3D11_VIEWPORT viewport = {};
    viewport.TopLeftX = (FLOAT)planeOutput->GetOffsetX();
    viewport.TopLeftY = (FLOAT)planeOutput->GetOffsetY();
    viewport.Width = (FLOAT)planeOutput->GetWidth();
    viewport.Height = (FLOAT)planeOutput->GetHeight();
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    m_pd3dDeviceContext->RSSetViewports( 1, &viewport );

    if(m_pQueryOcclusion != NULL)
    { 
        m_pd3dDeviceContext->Begin(m_pQueryOcclusion);
    }
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL StitchEngineDX11::EndFrame(bool bWait)
{
    if(m_pQueryOcclusion != NULL)
    {
        m_pd3dDeviceContext->End(m_pQueryOcclusion);
    }
    m_pd3dDeviceContext->OMSetRenderTargets( 0, NULL, NULL );

    if(m_eOutputMode == AMF_VIDEO_STITCH_OUTPUT_MODE_CUBEMAP)
    {
        m_pd3dDeviceContext->GSSetShader(NULL, NULL, 0);
    }
    m_pd3dDeviceContext->PSSetShader(NULL, NULL, 0);

    if(bWait)
    {
        m_pd3dDeviceContext->Flush();
        amf_sleep(1); //let DXX queue submit the job.
    }
    else
    {
        m_pd3dDeviceContext->Flush();
    }

    if(m_pQueryOcclusion != NULL)
    {
        UINT64 sampleCount = 0;
        while(S_FALSE == m_pd3dDeviceContext->GetData(m_pQueryOcclusion, &sampleCount, sizeof(sampleCount), 0) )
        {
        }
        AMFTraceWarning(AMF_FACILITY, L"Occlusion: = %" LPRId64, sampleCount);
    }   

    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL StitchEngineDX11::ProcessStream(int index, AMFSurface *pSurface)
{
    HRESULT hr = S_OK;

    AMFPlane* planeInput = pSurface->GetPlane(AMF_PLANE_PACKED);
    CComPtr<ID3D11Texture2D> textureInput = (ID3D11Texture2D*)planeInput->GetNative();
    ATL::CComPtr<ID3D11ShaderResourceView> pShaderResource;
    hr = m_pd3dDevice->CreateShaderResourceView( textureInput, NULL, &pShaderResource );
    m_pd3dDeviceContext->PSSetShaderResources( 0, 1, &pShaderResource.p);

    UINT stride  = sizeof(TextureVertex);
    UINT offset = 0;
    m_pd3dDeviceContext->IASetVertexBuffers(0, 1, &m_StreamList[index].m_pVertexBuffer.p, &stride, &offset);
    if(m_StreamList[index].m_pIndexBuffer == NULL)
    {
        m_pd3dDeviceContext->Draw( (UINT)m_StreamList[index].m_VerticesProjected.size(), 0 );
    }
    else
    {
        m_pd3dDeviceContext->IASetIndexBuffer(m_StreamList[index].m_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
        m_pd3dDeviceContext->DrawIndexed((UINT)m_StreamList[index].m_Indexes.size(), 0 , 0);
    }

    void* pNULL[1] = {0};

    m_pd3dDeviceContext->PSSetShaderResources( 0, 1, (ID3D11ShaderResourceView**)pNULL);
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL StitchEngineDX11::GetBorderRect(amf_int32 index, AMFRect &border)
{
    border = m_StreamList[index].m_BorderRect;
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL StitchEngineDX11::UpdateBrightness(amf_int32 /* index */, AMFPropertyStorage* /* pStorage */)
{
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
static  int QuadrantByAngle(float angle, float &center)
{
    int quadrant = -1;
    if(angle >= (float)M_PI / 2.0f && angle < (float)M_PI)
    {
        center = (float)M_PI * 3.0f /4.0f;
        return 0;
    }
    if(angle >= 0.0f && angle < (float)M_PI / 2.0f)
    {
        center = (float)M_PI /4.0f;
        return 1;
    }
    if(angle >=  -(float)M_PI / 2.0f && angle < 0)
    {
        center = -(float)M_PI /4.0f;
        return 2;
    }
    if(angle >= -(float)M_PI && angle < -(float)M_PI / 2.0f)
    {
        center = -(float)M_PI * 3.0f /4.0f;
        return 3;
    }

    return quadrant;
}

//re-generate the Rib and corner data based on the vertices
//-------------------------------------------------------------------------------------------------
AMF_RESULT StitchEngineDX11::UpdateRibs(amf_int32 /* widthInput */, amf_int32 /* heightInput */, AMFPropertyStorage* pStorage)
{
    amf_int64 lensCorrectionMode = AMF_VIDEO_STITCH_LENS_RECTILINEAR;
    pStorage->GetProperty(AMF_VIDEO_STITCH_LENS_MODE, &lensCorrectionMode);

    m_Ribs.clear();

    //create rib pairs
    for(int i1 = 0; i1 < (int)m_StreamList.size(); i1++)
    {
        XMVECTOR plane1 = m_StreamList[i1].m_Plane;
        for(int i2 = i1 + 1; i2 < (int)m_StreamList.size(); i2++)
        {
            XMVECTOR plane2 = m_StreamList[i2].m_Plane;
            XMVECTOR line1;
            XMVECTOR line2;

            //find the intersection between planes
            XMPlaneIntersectPlane(&line1, &line2, plane1, plane2);

            float distFromRibToCenter = XMVectorGetX(XMVector3LinePointDistance(line1, line2, XMVectorSet(0, 0, 0, 0)));
            if(distFromRibToCenter > 2.0f )
            {
                continue; //wrong pair
            }

            //check if already inserted
            bool bFound = false;
            for(RibList::iterator it_rib = m_Ribs.begin();  it_rib != m_Ribs.begin(); it_rib++)
            {
                if( 
                    (it_rib->channel1 == i1 && it_rib->channel1 == i2)  ||
                    (it_rib->channel2 == i2 && it_rib->channel2 == i1)
                    )
                {
                    bFound = true;
                    break;
                }
            }
            if(bFound)
            {
                continue;
            }

            //find the minimum distance from middle of line1&line2 to the sides for the first stream
            XMVECTOR middle = XMVectorLerp(line1, line2, 0.5f);
            int s1 = 0;
            int s2 = 0;
            float distanceMin = 1000.0f;
            for(int i = 0; i < (int)m_StreamList[i1].m_Sides.size(); i++)
            {
                float distance = XMVectorGetX(XMVector3Length(XMVectorSubtract(m_StreamList[i1].m_Sides[i], middle)));
                if(distance < distanceMin)
                {
                    distanceMin = distance;
                    s1 = i;
                }
            }
            //find the minimum distance from middle of line1&line2 to the sides for the second stream
            distanceMin = 1000.0f;
            for(int i = 0; i < (int)m_StreamList[i2].m_Sides.size(); i++)
            {
                float distance = XMVectorGetX(XMVector3Length(XMVectorSubtract(m_StreamList[i2].m_Sides[i], middle)));
                if(distance < distanceMin)
                {
                    distanceMin = distance;
                    s2 = i;
                }
            }

            // insert pair
            Rib rib;
            rib.channel1 = i1;
            rib.channel2 = i2;
            rib.side1 = s1;
            rib.side2 = s2;
            rib.index = (amf_int32)m_Ribs.size();
            m_Ribs.push_back(rib);

        }
    }
    m_Corners.clear();

    //build the list of corners
    if (lensCorrectionMode == AMF_VIDEO_STITCH_LENS_FISHEYE_FULLFRAME || lensCorrectionMode == AMF_VIDEO_STITCH_LENS_FISHEYE_CIRCULAR)
    {
        //calculate corners for each stream
        amf_vector<amf_vector<int>> planes;
        for(int i = 0; i < (int)m_StreamList.size(); i++)
        {
            amf_vector<int>  corners;
            corners.resize(4, -1);
            float  minAngle[4] = {1000.f, 1000.f, 1000.f, 1000.f};

            //go through the vertices for the stream
            for (amf_int32 v = 0; v < (amf_int32)m_StreamList[i].m_Vertices.size(); v++)
            {
                TextureVertex &vertex = m_StreamList[i].m_Vertices[v];
                XMVECTOR point = XMVectorSet(vertex.Pos[0], vertex.Pos[1], vertex.Pos[2], 0);
                if(vertex.Tex[2] == 0)
                {
                    continue;
                }
                float distanceOwn = 0;
                float distanceMin = 10000.f;
                int nearest = -1;

                //find the minimun distance to the center plane of each stream
                for (int c = 0; c < (int)m_StreamList.size(); c++)
                {
                    float distance = XMVectorGetX(XMVector3Length(XMVectorSubtract(point, m_StreamList[c].m_PlaneCenter)));
                    if(distanceMin > distance )
                    {
                        distanceMin = distance;
                        nearest = c;
                    }
                    if(c == i)
                    {
                        distanceOwn = distance;
                    }
                }

                if(nearest == i)
                {
                    continue;
                }

                float middle = (distanceOwn + distanceMin) / 2.0f;
                float dist = fabsf(distanceOwn - middle);

                //skip if the distance is greater than the threshold
                if(dist < 0.1f)
                {
                    float diff = 1000.0;
                    int quadrant = -1;
                    float angleEpsilon = 0.1f;

                    //check against the distance to the 4 cornes of the stream
                    for (int p = 0; p < 4; p++)
                    {
                        float distToCorner = XMVectorGetX(XMVector3Length(XMVectorSubtract(point, m_StreamList[i].m_Corners[p])));
                        if(diff > distToCorner)
                        {
                            diff = distToCorner;
                            quadrant = p;
                        }
                    }

                    //find the distance to the plane constructed with the 3 vectors
                    {
                       XMVECTOR point1 = XMVectorSet( 0.0f, 0.0f, 0.0f, 0.0f);
                       XMVECTOR point2 = m_StreamList[i].m_PlaneCenter;
                       XMVECTOR point3 = m_StreamList[i].m_Corners[quadrant];

                        XMVECTOR plane = XMPlaneFromPoints(point1, point2, point3);
                        float distToPlane = (float) fabs(XMVectorGetX(XMPlaneDotCoord(plane, point)));
                        diff = distToPlane;
                    }

                    //update the minimum angle
                    if (diff < angleEpsilon)
                    {
                        if(minAngle[quadrant] > diff)
                        {
                            minAngle[quadrant] = diff;
                            corners[quadrant] = v;
                        }
                    }
                }
            }
            planes.push_back(corners);
        }

        // match corners
        for(int i1 = 0; i1 < (int)planes.size(); i1++)
        {
            //go through all the corners
            for (int i2 = i1 + 1; i2 < (int)planes.size(); i2++)
            {
                //go through the 4 corners for each plane
                for(int c1 = 0; c1 < 4; c1++)
                {
                    //go through the 4 corners for each plane
                    for (int c2 = 0; c2 < 4; c2++)
                    {
                        if(planes[i1][c1] == -1 || planes[i2][c2] == -1)
                        {
                            continue;
                        }
                        TextureVertex &v1 = m_StreamList[i1].m_Vertices[planes[i1][c1]];
                        TextureVertex &v2 = m_StreamList[i2].m_Vertices[planes[i2][c2]];
                        float diff0 = v1.Pos[0] - v2.Pos[0];
                        float diff1 = v1.Pos[1] - v2.Pos[1];
                        float diff2 = v1.Pos[2] - v2.Pos[2];
                        float distance12 = sqrtf(diff0 * diff0 + diff1 * diff1 + diff2 * diff2);
                        float distanceEpsilon = 0.30f;

                        if(distance12 < distanceEpsilon)
                        {
                            DirectX::XMVECTOR  texRect1 = m_StreamList[i1].m_TexRect;
                            DirectX::XMVECTOR  texRect2 = m_StreamList[i2].m_TexRect;
                            Corner corner; 
                            corner.count = 2;
                            corner.channel[0] = i1;
                            corner.channel[1] = i2;

                            // on texture 
                            float x = (v1.Tex[0] - 0.5f);
                            float y = (v1.Tex[1] - 0.5f);
                            if(x <= 0 && y <= 0)
                            {
                                corner.corner[0] = 0;
                            }
                            else if(x > 0 && y <= 0)
                            {
                                corner.corner[0] = 1;
                            }
                            else if(x > 0 && y > 0)
                            {
                                corner.corner[0] = 2;
                            }
                            else if(x <= 0 && y > 0)
                            {
                                corner.corner[0] = 3;
                            }

                            // update tex rect
                            if(x<0)
                            {
                                ((float*)&texRect1)[0] = AMF_MAX(((float*)&texRect1)[0], v1.Tex[0]);
                            }
                            else 
                            {
                                ((float*)&texRect1)[2] = AMF_MIN(((float*)&texRect1)[2], v1.Tex[0]);
                            }
                            if(y < 0 )
                            {
                                ((float*)&texRect1)[1] = AMF_MAX(((float*)&texRect1)[1], v1.Tex[1]);
                            }
                            else
                            {
                                ((float*)&texRect1)[3] = AMF_MIN(((float*)&texRect1)[3], v1.Tex[1]);
                            }

                           //assign the value to corner
                           x = (v2.Tex[0] - 0.5f) *2.0f;;
                           y = (v2.Tex[1] - 0.5f) *2.0f;;
                            if(x <= 0 && y <= 0)
                            {
                                corner.corner[1] = 0;
                            }
                            else if(x > 0 && y <= 0)
                            {
                                corner.corner[1] = 1;
                            }
                            else if(x > 0 && y > 0)
                            {
                                corner.corner[1] = 2;
                            }
                            else if(x <= 0 && y > 0)
                            {
                                corner.corner[1] = 3;
                            }

                            // update tex rect
                            if(x<0)
                            {
                                ((float*)&texRect2)[0] = AMF_MAX(((float*)&texRect2)[0], v2.Tex[0]);
                            }
                            else 
                            {
                                ((float*)&texRect2)[2] = AMF_MIN(((float*)&texRect2)[2], v2.Tex[0]);
                            }
                            if(y < 0 )
                            {
                                ((float*)&texRect2)[1] = AMF_MAX(((float*)&texRect2)[1], v2.Tex[1]);
                            }
                            else
                            {
                                ((float*)&texRect2)[3] = AMF_MIN(((float*)&texRect2)[3], v2.Tex[1]);
                            }

                            corner.pos[0] = v1.Pos[0];
                            corner.pos[1] = v1.Pos[1];
                            corner.pos[2] = v1.Pos[2];

                            //search if this corner was added
                            bool bFound = false;
                            for(int c = 0; c < (int)m_Corners.size(); c++)
                            {
                                diff0 = corner.pos[0] - m_Corners[c].pos[0];
                                diff1 = corner.pos[1] - m_Corners[c].pos[1];
                                diff2 = corner.pos[2] - m_Corners[c].pos[2];
                                float distance = sqrtf(diff0 * diff0 + diff1 * diff1 + diff2 * diff2);

                                if(distance < distanceEpsilon)
                                {
                                    bool bCornerFound = false;
                                    for(int f = 0; f < m_Corners[c].count; f++)
                                    {
                                        if(m_Corners[c].channel[f] == i2)
                                        {
                                            bCornerFound = true;
                                            break;
                                        }
                                    }
                                    bFound = true;
                                    //construct new one
                                    if(!bCornerFound)
                                    {
                                        m_Corners[c].channel[m_Corners[c].count] = corner.channel[corner.count - 1];
                                        m_Corners[c].corner[m_Corners[c].count] = corner.corner[corner.count - 1];
                                        m_Corners[c].count++;
                                        m_StreamList[i1].m_TexRect = texRect1;
                                        m_StreamList[i2].m_TexRect = texRect2;
                                    }
                                    break;
                                }
                            }

                            //add new one
                            if (!bFound)
                            {
                                m_StreamList[i1].m_TexRect = texRect1;
                                m_StreamList[i2].m_TexRect = texRect2;
                                m_Corners.push_back(corner);
                            }
                        }

                    }
                }
            }
        }

        //to handle 2 cameras case
        if (m_Corners.size() == 2)
        {
            Corner corner[4] = { 0 };
            corner[0].corner[0] = 0;
            corner[0].corner[1] = 1;

            corner[1].corner[0] = 1;
            corner[1].corner[1] = 0;

            corner[2].corner[0] = 3;
            corner[2].corner[1] = 2;

            corner[3].corner[0] = 2;
            corner[3].corner[1] = 3;

            //find the matching corner and update the index
            for (amf_int32 idx = 0; idx < 4; idx++)
            {
                if ((corner[idx].corner[0] == m_Corners[0].corner[0]) && (corner[idx].corner[1] == m_Corners[0].corner[1]))
                {
                    corner[idx].count = 1;
                }
                else if ((corner[idx].corner[0] == m_Corners[1].corner[0]) && (corner[idx].corner[1] == m_Corners[1].corner[1]))
                {
                    corner[idx].count = 1;
                }
            }
            
            //add corners 
            for (amf_int32 idx = 0; idx < 4; idx++)
            {
                if (corner[idx].count != 0)
                {
                    continue;
                }

                corner[idx].count = 2;
                corner[idx].channel[0] = 0;
                corner[idx].channel[1] = 1;

                corner[idx].pos[0] = m_StreamList[0].m_Corners[corner[idx].corner[0]].m128_f32[0];
                corner[idx].pos[1] = m_StreamList[0].m_Corners[corner[idx].corner[0]].m128_f32[1];
                corner[idx].pos[2] = m_StreamList[0].m_Corners[corner[idx].corner[0]].m128_f32[2];
                corner[idx].index = (amf_int32)m_Corners.size();
                m_Corners.push_back(corner[idx]);
            }
        }

    }
    else if (lensCorrectionMode == AMF_VIDEO_STITCH_LENS_RECTILINEAR)
    { 
        //calculate corners
        XMVECTOR center = XMVectorSet(0, 0, 0, 0);
        // define each vertex for cube or other form
        for(int i1 = 0; i1 < (int)m_StreamList.size(); i1++)
        {
            XMVECTOR plane1 = m_StreamList[i1].m_Plane;

            //go through each stream
            for(int i2 = i1 + 1; i2 < (int)m_StreamList.size(); i2++)
            {
                XMVECTOR plane2 = m_StreamList[i2].m_Plane;
                XMVECTOR line_12_1;
                XMVECTOR line_12_2;
                XMPlaneIntersectPlane(&line_12_1, &line_12_2, plane1, plane2);

                //go through each stream
                for (int i3 = i2 + 1; i3 < (int)m_StreamList.size(); i3++)
                {
                    XMVECTOR plane3 = m_StreamList[i3].m_Plane;
                    XMVECTOR line_13_1;
                    XMVECTOR line_13_2;
                    XMPlaneIntersectPlane(&line_13_1, &line_13_2, plane1, plane3);

                    XMVECTOR line_23_1;
                    XMVECTOR line_23_2;
                    XMPlaneIntersectPlane(&line_23_1, &line_23_2, plane2, plane3);


                   XMVECTOR point1 = XMPlaneIntersectLine(plane1, line_23_1, line_23_2);
                   XMVECTOR point2 = XMPlaneIntersectLine(plane2, line_13_1, line_13_2);
                   XMVECTOR point3 = XMPlaneIntersectLine(plane3, line_12_1, line_12_2);

                  float distance1 = XMVectorGetX(XMVector3Length(XMVectorSubtract(point1, center)));
                  float distance2 = XMVectorGetX(XMVector3Length(XMVectorSubtract(point2, center)));
                  float distance3 = XMVectorGetX(XMVector3Length(XMVectorSubtract(point3, center)));

                  if(m_StreamList.size() == 6)
                  { 
                      if(distance1 > 2.0f || distance2 > 2.0f  || distance3 > 2.0f ) // not close enough
                      {
                          continue;
                      }
                  }

                //check if the corner is already in the list
                  bool bFound = false;
                  for(CornerList::iterator it_corner = m_Corners.begin(); it_corner != m_Corners.end(); it_corner++)
                  {
                      if( it_corner->channel[0] == i1 && it_corner->channel[1] == i2 && it_corner->channel[3] == i3)
                      {
                          bFound = true;
                          break;
                      }
                  }

                  if(bFound)
                  {
                      continue;
                  }

                  // define corner index
                  amf_int32 c1 = 0;
                  amf_int32 c2 = 0;
                  amf_int32 c3 = 0;
                  float distanceMin = 1000.0f;

                  //find the minimum distance
                  for (amf_int32 i = 0; i < (amf_int32)m_StreamList[i1].m_Corners.size(); i++)
                  {
                    float distance = XMVectorGetX(XMVector3Length(XMVectorSubtract(m_StreamList[i1].m_Corners[i], point1)));
                    if(distance < distanceMin)
                    {
                        distanceMin = distance;
                        c1 = i;
                    }
                  }
                  //find the minimum distance
                  distanceMin = 1000.0f;
                  for(amf_int32 i = 0; i < (amf_int32)m_StreamList[i2].m_Corners.size(); i++)
                  {
                    float distance = XMVectorGetX(XMVector3Length(XMVectorSubtract(m_StreamList[i2].m_Corners[i], point2)));
                    if(distance < distanceMin)
                    {
                        distanceMin = distance;
                        c2 = i;
                    }
                  }
                  //find the minimum distance
                  distanceMin = 1000.0f;
                  for(amf_int32 i = 0; i < (amf_int32)m_StreamList[i3].m_Corners.size(); i++)
                  {
                    float distance = XMVectorGetX(XMVector3Length(XMVectorSubtract(m_StreamList[i3].m_Corners[i], point3)));
                    if(distance < distanceMin)
                    {
                        distanceMin = distance;
                        c3 = i;
                    }
                  }

                  // insert vertex
                  Corner c;
                  c.count = 3;
                  c.channel[0] = i1;
                  c.channel[1] = i2;
                  c.channel[2] = i3;
                  c.corner[0] = c1;
                  c.corner[1] = c2;
                  c.corner[2] = c3;
                  c.index  = (amf_int32)m_Corners.size();

                  XMVECTOR channelPlane0 = m_StreamList[c.channel[0]].m_Plane;
                  XMVECTOR channelPlane1 = m_StreamList[c.channel[1]].m_Plane;
                  XMVECTOR channelPlane2 = m_StreamList[c.channel[2]].m_Plane;

                  XMVECTOR line_01_0;
                  XMVECTOR line_01_1;
                  XMPlaneIntersectPlane(&line_01_0, &line_01_1, channelPlane0, channelPlane1);
                  XMVECTOR corner = XMPlaneIntersectLine(channelPlane2, line_01_0, line_01_1);
                  c.pos[0] = XMVectorGetX(corner);
                  c.pos[1] = XMVectorGetY(corner);
                  c.pos[2] = XMVectorGetZ(corner);
                  m_Corners.push_back(c);
                }
            }
        }
    }

    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
float* AMF_STD_CALL StitchEngineDX11::GetTexRect(amf_int32 index)
{
    return (float*)&m_StreamList[index].m_TexRect;
}

//-------------------------------------------------------------------------------------------------
AMFSurface* AMF_STD_CALL StitchEngineDX11::GetBorderMap(amf_int32 index)
{
    return m_StreamList[index].m_pBorderMap;
}

//-------------------------------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4324) // structure was padded due to alignment specifier
struct IntermediatePoint
{
    float distance;
    XMVECTOR add;
};
#pragma warning(pop)

typedef std::vector<IntermediatePoint, amf_aligned_allocator<IntermediatePoint, alignof(IntermediatePoint)> > IntermediatePointList;

//-------------------------------------------------------------------------------------------------
AMF_RESULT          StitchEngineDX11::ApplyControlPoints()
{
    std::vector< std::map<amf_int32, IntermediatePointList> > holders;

    holders.resize(m_StreamList.size());

    int modifiedVerttexes = 0;
    for(ControlPointList::iterator it = m_ControlPoints.begin(); it != m_ControlPoints.end(); it++)
    {
        if(it->index0 < 0 || it->index1 < 0)
        {
            continue;
        }

        // get points on the plane
        TextureVertex v0 = it->point0;
        TextureVertex v1 = it->point1;

        // get points on the sphere
        XMVECTOR center = XMVectorSet(0, 0, 0, 1.0f);
        XMVECTOR pointOnShere0 = XMVectorSet(v0.Pos[0], v0.Pos[1], v0.Pos[2], 1.0f);
        XMVECTOR pointOnShere1 = XMVectorSet(v1.Pos[0], v1.Pos[1], v1.Pos[2], 1.0f);

        XMVECTOR middleOnShere = XMVectorLerp(pointOnShere0, pointOnShere1, 0.5f);

        // process one point
        for( int k = 0; k < 2; k++)
        { 
            int index = k == 0 ? it->index0 : it->index1;

            XMVECTOR plane = m_StreamList[index].m_Plane;

            // project middle on each plane from the center
            XMVECTOR middleOnPlane = XMPlaneIntersectLine(plane, center, middleOnShere);
            XMVECTOR pointOnShere = k == 0 ? pointOnShere0 : pointOnShere1;

            // get control point on the plane 
            TextureVertex  pointVertex = k == 0 ? it->point0 : it->point1;
            XMVECTOR pointOnPlane = XMVectorSet(pointVertex.Pos[0], pointVertex.Pos[1], pointVertex.Pos[2], 1.0f);

            float distanceShift = (float) fabs(XMVectorGetX(XMVector3Length(XMVectorSubtract(pointOnPlane, middleOnPlane))));

            // value to move from control point to middle point
            XMVECTOR add = XMVectorSubtract(middleOnPlane, pointOnPlane);

            std::vector<TextureVertex>& vertices = m_StreamList[index].m_Vertices;

            for(amf_int32 i = 0; i < (amf_int32)vertices.size(); i++)
            { 
                TextureVertex v = vertices[i];
                XMVECTOR veretexOnPlane = XMVectorSet(v.Pos[0], v.Pos[1], v.Pos[2], 1.0f);

                // distance from control point to the current point
                float distance = (float) fabs(XMVectorGetX(XMVector3Length(XMVectorSubtract(pointOnPlane, veretexOnPlane))));
                float maxRadius = 0.05f;

                maxRadius = max(maxRadius, distanceShift * 2.0f);

                if(distance < maxRadius && distance != 0)
                {
                    // from this XMVector3LinePointDistance();
                    XMVECTOR a = pointOnPlane;
                    XMVECTOR b = middleOnPlane;
                    XMVECTOR p = veretexOnPlane;

                    XMVECTOR ap = XMVectorSubtract(a, p);
                    XMVECTOR ab = XMVectorSubtract(a, b);

                    XMVECTOR ptProjection = XMVectorAdd(a, 
                                XMVectorMultiply( 
                                                XMVectorDivide(
                                                                XMVector3Dot(ap, ab), 
                                                                XMVector3Dot(ab, ab)
                                                              ), 
                                                ab)
                       );
                    XMVECTOR addThis = XMVectorSubtract(middleOnPlane, ptProjection);

                    IntermediatePoint intermediatePoint;
                    intermediatePoint.distance = distance;

                    float addX = (float) (1.0f - fabs(XMVectorGetX(ptProjection) - v.Pos[0] ) / maxRadius) * XMVectorGetX(addThis);
                    float addY = (float) (1.0f - fabs(XMVectorGetY(ptProjection) - v.Pos[1] ) / maxRadius) * XMVectorGetY(addThis);
                    float addZ = (float) (1.0f - fabs(XMVectorGetZ(ptProjection) - v.Pos[2] ) / maxRadius) * XMVectorGetZ(addThis);
                    intermediatePoint.add = XMVectorSet(addX, addY, addZ, 0);
                    holders[index][i].push_back(intermediatePoint);
                    modifiedVerttexes++;
                }
            }
        }
    }

    int channel = 0;
    for(std::vector< std::map<amf_int32, IntermediatePointList> >::iterator it_h = holders.begin(); it_h != holders.end(); it_h++, channel++)
    {
        for(std::map<amf_int32, IntermediatePointList>::iterator it_v = it_h->begin(); it_v != it_h->end(); it_v++)
        { 
            TextureVertex &v = m_StreamList[channel].m_Vertices[it_v->first];
            XMVECTOR veretexOnPlane = XMVectorSet(v.Pos[0], v.Pos[1], v.Pos[2], 1.0f);
            XMVECTOR add = XMVectorSet(0, 0, 0, 0);;

            for(IntermediatePointList::iterator it_p = it_v->second.begin(); it_p != it_v->second.end(); it_p++)
            {
                add = XMVectorAdd(add, it_p->add);
            }

            add = XMVectorScale(add, 1.0f / (float)it_v->second.size());
            XMVECTOR newPoint = XMVectorAdd(veretexOnPlane, add);
            v.Pos[0] = XMVectorGetX(newPoint);
            v.Pos[1] = XMVectorGetY(newPoint);
            v.Pos[2] = XMVectorGetZ(newPoint);
        }
    }

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT StitchEngineDX11::BuildMapForHistogram(amf_int32 widthInput, amf_int32 heightInput)
{
    AMF_RESULT res = AMF_OK;
    amf_int32 mapWidth = widthInput / 8;
    amf_int32 mapHeight = heightInput / 8;

    for(int i = 0; i < (int)m_StreamList.size(); i++)
    {
         AMFSurfacePtr pBorderMap;
        res = m_pContext->AllocSurface(AMF_MEMORY_HOST, AMF_SURFACE_GRAY8, mapWidth, mapHeight, &pBorderMap);
        AMF_RETURN_IF_FAILED(res, L"AllocSurface() failed");

         AMFPlane *pMap = pBorderMap->GetPlaneAt(0);
         memset(pMap->GetNative(), 0, pMap->GetHPitch() * pMap->GetVPitch());
         m_StreamList[i].m_pBorderMap = pBorderMap;
    }

    for(CornerList::iterator it_corner = m_Corners.begin(); it_corner != m_Corners.end(); it_corner++)
    {
        XMVECTOR corner = XMVectorSet(it_corner->pos[0], it_corner->pos[1], it_corner->pos[2], 0);

        for( amf_int32 side = 0; side < it_corner->count; side++)
        {
            AMFSurfacePtr pBorderMap = m_StreamList[it_corner->channel[side]].m_pBorderMap;
            AMFPlane *pMap = pBorderMap->GetPlaneAt(0);
            amf_uint8* borderData = (amf_uint8*)pMap->GetNative();
            amf_int32 pitch = pMap->GetHPitch();

            for(amf_int32 v = 0; v < (amf_int32)m_StreamList[it_corner->channel[side]].m_Vertices.size(); v++)
            {
                TextureVertex &vertex = m_StreamList[it_corner->channel[side]].m_Vertices[v];
                XMVECTOR point = XMVectorSet(vertex.Pos[0], vertex.Pos[1], vertex.Pos[2], 0);
                float distance = XMVectorGetX(XMVector3Length(XMVectorSubtract(corner, point)));

                if(distance < 0.3)
                {

                    amf_uint8 sizeMask =  (amf_uint8) (1 << it_corner->corner[side]);

                      amf_int32 tex_x = amf_int32(vertex.Tex[0] * widthInput) / 8;
                      amf_int32 tex_y = amf_int32(vertex.Tex[1] * heightInput) / 8;
                      for(amf_int32 y = 0; y < 8; y++)
                      {
                          for(amf_int32 x = 0; x < 8; x++)
                          {
                              amf_int32 map_x = tex_x + x - 4;
                              amf_int32 map_y = tex_y + y - 4;
                              if(map_x >= 0 && map_y >= 0 && map_x < mapWidth && map_y < mapHeight)
                              {
                                  *(borderData + map_y * pitch + map_x)  |= sizeMask;
                              }
                          }
                      }
                }
            }
        }
    }
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT          StitchEngineDX11::UpdateTransparency(AMFPropertyStorage *pStorage)
{
    amf_int64 lensCorrectionMode = AMF_VIDEO_STITCH_LENS_RECTILINEAR;
    pStorage->GetProperty(AMF_VIDEO_STITCH_LENS_MODE, &lensCorrectionMode);

    // update transparency  for each rib
    if (lensCorrectionMode == AMF_VIDEO_STITCH_LENS_FISHEYE_CIRCULAR || lensCorrectionMode == AMF_VIDEO_STITCH_LENS_FISHEYE_FULLFRAME) //MM TODO stupid switch for now 
    {
     float transparencySize = 0.05f;

#if defined(DEBUG_TRANSPARENT)
     float transparencyMax = 0.3f;
     float transparencyMin = 0.3f;
#else
     float transparencyMin = 0.0f;
     float transparencyMax = 1.0f;
#endif


    for(int i = 0; i < (int)m_StreamList.size(); i++)
    {
        for(amf_int32 v = 0; v < (amf_int32)m_StreamList[i].m_Vertices.size(); v++)
        {
            TextureVertex &vertex = m_StreamList[i].m_Vertices[v];
            XMVECTOR point = XMVectorSet(vertex.Pos[0], vertex.Pos[1], vertex.Pos[2], 0);
            float distanceOwn = 0;
            float distanceMin = 10000.f;
            int nearest = -1;
            for(int c = 0; c < (int)m_StreamList.size(); c++)
            {
                float distance = XMVectorGetX(XMVector3Length(XMVectorSubtract(point, m_StreamList[c].m_PlaneCenter)));
                if(distanceMin > distance )
                {
                    distanceMin = distance;
                    nearest = c;
                }
                if(c == i)
                {
                    distanceOwn = distance;
                }
            }
            float middle = (distanceOwn + distanceMin) / 2.0f;
            float dist = distanceOwn - middle;

            if(nearest != i)
            {
                if(dist < transparencySize)
                { 
                    float x0 = transparencySize;
                    float x1 = 0;
                    float y1 = transparencyMax;
                    float y0 = transparencyMin;
                    vertex.Tex[2] = y0 + (y1 - y0) * ( dist - x0) / (x1 - x0);
                }
                else 
                { 
                    vertex.Tex[2] = transparencyMin;
                }
            }
            else
            {
                vertex.Tex[2] = transparencyMax;
            }
            
        }
    }
    }
    else if (lensCorrectionMode == AMF_VIDEO_STITCH_LENS_RECTILINEAR) //MM TODO stupid switch for now 
    {
     float transparencySize = 0.1f;

#if defined(DEBUG_TRANSPARENT)
     float transparencyMax = 0.3f;
     float transparencyMin = 0.3f;
#else
     float transparencyMin = 0.0f;
     float transparencyMax = 1.0f;
#endif


    for(RibList::iterator rib = m_Ribs.begin(); rib != m_Ribs.end(); rib++)
    {
        XMVECTOR center = XMVectorSet(0, 0, 0, 0);

        for(amf_int32 loop = 0; loop < 2; loop++)
        {
            amf_int32 channelPoint = loop == 0 ? rib->channel1 : rib->channel2;
            amf_int32 channelPlane = loop == 0 ? rib->channel2 : rib->channel1;
            XMVECTOR plane = m_StreamList[channelPlane].m_Plane;

            float a = XMVectorGetX(plane);
            float b = XMVectorGetY(plane);
            float c = XMVectorGetZ(plane);
            float d = XMVectorGetW(plane);

            // check each point in the mesh
            for(amf_int32 v = 0; v < (amf_int32)m_StreamList[channelPoint].m_Vertices.size(); v++)
            {
                TextureVertex &vertex = m_StreamList[channelPoint].m_Vertices[v];
                float dist = (a * vertex.Pos[0] + b * vertex.Pos[1] + c * vertex.Pos[2] + d) / sqrtf(a * a + b * b + c * c + d * d);

#if defined(DEBUG_TRANSPARENT)
                vertex.Tex[2] = transparencyMax; //MM - this line is temporary - do not check in
#endif                
                if(dist < -transparencySize )
                {
                    vertex.Tex[2] = transparencyMin;
                }
                if(dist > 0 )
                {
                }
                else 
                {
                    float x0 = -transparencySize;
                    float x1 = 0;
                    float y1 = transparencyMax;
                    float y0 = transparencyMin;
                    float val = y0 + (y1 - y0) * (dist - x0) / (x1 - x0);
                    vertex.Tex[2] *= val;
                }

            }
        }
    }
    }
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL StitchEngineDX11::OnSurfaceDataRelease(AMFSurface* pSurface)
{
    if(m_pd3dDevice == NULL)
    {
        return;
    }
    AMFLock loc(&m_Sect);
    if(m_AllocationQueue.size() >= 2)
    {
        m_AllocationQueue.pop_front();
    }
    m_AllocationQueue.push_back((ID3D11Texture2D*)pSurface->GetPlaneAt(0)->GetNative());
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL StitchEngineDX11::AllocCubeMap(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height, AMFSurface **ppSurface)
{
    AMF_RETURN_IF_FALSE(m_pd3dDevice != NULL, AMF_NOT_INITIALIZED, L"Not initialized");

    ATL::CComPtr<ID3D11Texture2D> pSurface;

    AMFLock loc(&m_Sect);
    if(m_AllocationQueue.size() > 0)
    {
        pSurface = m_AllocationQueue.front();
        m_AllocationQueue.pop_front();
    }
    else
    {
        D3D11_TEXTURE2D_DESC desc = {};

        DXGI_FORMAT dxformat = DXGI_FORMAT_B8G8R8A8_UNORM;
        switch(format)
        {
        case AMF_SURFACE_BGRA:
            dxformat = DXGI_FORMAT_B8G8R8A8_TYPELESS;
            break;
        case AMF_SURFACE_RGBA_F16:
            dxformat = DXGI_FORMAT_R16G16B16A16_FLOAT;
            break;
        case AMF_SURFACE_RGBA:
            dxformat = DXGI_FORMAT_R8G8B8A8_TYPELESS;
            break;

        case AMF_SURFACE_GRAY8:
            dxformat = DXGI_FORMAT_R8_UNORM;
            break;

        case AMF_SURFACE_U8V8:
            dxformat = DXGI_FORMAT_R8G8_UNORM;
            break;
        default:
            return AMF_NOT_SUPPORTED;
        }

        //we allocate alligned to 2 for all formats
        width = AlignValue(width, 2);
        height = AlignValue(height, 2);

        if(width > height)
        {
            height = width;
        }
        else
        {
            width = height;
        }

        desc.ArraySize = 6; //CUBEMAP
        desc.BindFlags = 0;

        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        // This if/else is added because we can't create DX11 texture of BGRA format with D3D11_BIND_UNORDERED_ACCESS flag.
        if((format == AMF_SURFACE_BGRA || format == AMF_SURFACE_RGBA || format == AMF_SURFACE_RGBA_F16))
        {
            desc.BindFlags |=  D3D11_BIND_RENDER_TARGET; // request renderer D3D11_BIND_RENDER_TARGET
            if(format == AMF_SURFACE_RGBA || format == AMF_SURFACE_RGBA_F16)
            { 
                desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
            }
        }
        else
        {
            desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
        }

        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.Format = dxformat;
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;

        desc.SampleDesc.Count = 1;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;
        desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED; //MM for fast GPU interop
        desc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;

        HRESULT hr = m_pd3dDevice->CreateTexture2D(&desc, NULL, &pSurface);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateTexture2D() failed");
    }
    m_pContext->CreateSurfaceFromDX11Native(pSurface, ppSurface, this);

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
