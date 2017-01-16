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
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
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

#include "VideoRenderDX11.h"
#include "../common/CmdLogger.h"
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <float.h>

const char DX11_Shader[] = 
"//--------------------------------------------------------------------------------------\n"
"// File: Tutorial04.fx                                                                  \n"
"//                                                                                      \n"
"// Copyright (c) Microsoft Corporation. All rights reserved.                            \n"
"//--------------------------------------------------------------------------------------\n"
"                                                                                        \n"
"//--------------------------------------------------------------------------------------\n"
"// Constant Buffer Variables                                                            \n"
"//--------------------------------------------------------------------------------------\n"
"cbuffer ConstantBuffer : register( b0 )                                                 \n"
"{                                                                                       \n"
"    matrix World;                                                                       \n"
"    matrix View;                                                                        \n"
"    matrix Projection;                                                                  \n"
"}                                                                                       \n"
"                                                                                        \n"
"//--------------------------------------------------------------------------------------\n"
"struct VS_OUTPUT                                                                        \n"
"{                                                                                       \n"
"    float4 Pos : SV_POSITION;                                                           \n"
"    float4 Color : COLOR0;                                                              \n"
"};                                                                                      \n"
"                                                                                        \n"
"//--------------------------------------------------------------------------------------\n"
"// Vertex Shader                                                                        \n"
"//--------------------------------------------------------------------------------------\n"
"VS_OUTPUT VS( float4 Pos : POSITION, float4 Color : COLOR )                             \n"
"{                                                                                       \n"
"    VS_OUTPUT output = (VS_OUTPUT)0;                                                    \n"
"    output.Pos = mul( Pos, World );                                                     \n"
"    output.Pos = mul( output.Pos, View );                                               \n"
"    output.Pos = mul( output.Pos, Projection );                                         \n"
"    //output.Pos = Pos;                                                                 \n"
"    output.Color = Color;                                                               \n"
"    return output;                                                                      \n"
"}                                                                                       \n"
"                                                                                        \n"
"                                                                                        \n"
"//--------------------------------------------------------------------------------------\n"
"// Pixel Shader                                                                         \n"
"//--------------------------------------------------------------------------------------\n"
"float4 PS( VS_OUTPUT input ) : SV_Target                                                \n"
"{                                                                                       \n"
"    return input.Color;                                                                 \n"
"}                                                                                       \n"
;
using namespace DirectX;

struct Vertex
{
    XMFLOAT3 Pos;
    XMFLOAT4 Col;
};

struct ConstantBuffer
{
    XMMATRIX mWorld;
    XMMATRIX mView;
    XMMATRIX mProjection;
};


VideoRenderDX11::VideoRenderDX11(amf_int width, amf_int height, bool bInterlaced, amf_int frames, amf::AMFContext* pContext)
    :VideoRender(width, height, bInterlaced, frames, pContext),
    m_fAnimation(0),
    m_bWindow(false)
{
}

VideoRenderDX11::~VideoRenderDX11()
{
    Terminate();
}

AMF_RESULT VideoRenderDX11::Init(HWND hWnd, bool bFullScreen)
{
    AMF_RESULT res = AMF_OK;
    HRESULT hr = S_OK;
    m_fAnimation = 0;

    if(m_width == 0 || m_height == 0)
    {
        LOG_ERROR(L"Bad width/height: width=" << m_width << L"height=" << m_height);
        return AMF_FAIL;
    }

    m_bWindow = hWnd != ::GetDesktopWindow();

    m_pD3DDevice = static_cast<ID3D11Device*>(m_pContext->GetDX11Device());


    DXGI_SWAP_CHAIN_DESC sd = {0};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = m_width;
    sd.BufferDesc.Height = m_height;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
//    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // this works if needed
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;

    ATL::CComQIPtr<IDXGIDevice> pDevice = m_pD3DDevice;

    ATL::CComPtr<IDXGIAdapter> pAdapter;
    hr = pDevice->GetParent(__uuidof(IDXGIAdapter), (void **)&pAdapter);
    CHECK_HRESULT_ERROR_RETURN(hr, L"pDevice->GetParent(__uuidof(IDXGIAdapter)) failed");

    ATL::CComPtr<IDXGIFactory> pFactory;
    hr = pAdapter->GetParent(__uuidof(IDXGIFactory), (void **)&pFactory);
    CHECK_HRESULT_ERROR_RETURN(hr, L"pDevice->GetParent(__uuidof(IDXGIFactory)) failed");

    hr = pFactory->CreateSwapChain(pDevice, &sd, &m_pSwapChain);
    CHECK_HRESULT_ERROR_RETURN(hr, L"CreateSwapChain() failed");

    res = InitScene();
    CHECK_AMF_ERROR_RETURN(res, L"InitScene() failed");

    return AMF_OK;
}

AMF_RESULT VideoRenderDX11::Terminate()
{
    m_pSwapChain.Release();

    m_pVertexLayout.Release();
    m_pVertexBuffer.Release();
    m_pIndexBuffer.Release();
    m_pConstantBuffer.Release();

    m_pVertexShader.Release();
    m_pPixelShader.Release();

    m_pRenderTargetView.Release();

    m_pD3DDevice.Release();

    return AMF_OK;
}

AMF_RESULT VideoRenderDX11::InitScene()
{
    amf::AMFContext::AMFDX11Locker lock(m_pContext);
    AMF_RESULT res = AMF_OK;
    HRESULT hr = S_OK;
    D3D11_VIEWPORT vp;
    vp.Width = (FLOAT)m_width;
    vp.Height = (FLOAT)m_height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    ATL::CComPtr<ID3D11DeviceContext>        pD3D11Context;
    m_pD3DDevice->GetImmediateContext(&pD3D11Context);
    pD3D11Context->RSSetViewports( 1, &vp );

    ATL::CComPtr<ID3D11Texture2D>     pBackBuffer;

    hr = m_pSwapChain->GetBuffer(0,__uuidof( ID3D11Texture2D ), ( LPVOID* ) &pBackBuffer);
    CHECK_HRESULT_ERROR_RETURN(hr, L"m_pSwapChain->GetBuffer() failed");

    hr = m_pD3DDevice->CreateRenderTargetView(pBackBuffer, NULL, &m_pRenderTargetView);
    CHECK_HRESULT_ERROR_RETURN(hr, L"m_pD3DDevice->CreateRenderTargetView() failed");

    pD3D11Context->OMSetRenderTargets( 1, &m_pRenderTargetView.p, NULL );

    res = CreateObject();
    CHECK_AMF_ERROR_RETURN(res, L"CreateObject() failed");

    return AMF_OK;
}

AMF_RESULT VideoRenderDX11::CreateObject()
{
    amf::AMFContext::AMFDX11Locker lock(m_pContext);
    AMF_RESULT res = AMF_OK;
    HRESULT hr = S_OK;
    // Compile the vertex shader
    ATL::CComPtr<ID3DBlob> pVSBlob;
    res = CreateShader( "VS", "vs_4_0", &pVSBlob ); 
    CHECK_HRESULT_ERROR_RETURN(hr, L"CreateShaderFromResource(VS) failed");

    ATL::CComPtr<ID3D11DeviceContext>        pD3D11Context;
    m_pD3DDevice->GetImmediateContext(&pD3D11Context);

    // Create the vertex shader
    hr = m_pD3DDevice->CreateVertexShader( pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), NULL, &m_pVertexShader );
    CHECK_HRESULT_ERROR_RETURN(hr, L"CreateVertexShader() failed");

    // Define the input layout
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    UINT numElements = ARRAYSIZE( layout );

    // Create the input layout
    hr = m_pD3DDevice->CreateInputLayout( layout, numElements, pVSBlob->GetBufferPointer(),
                                          pVSBlob->GetBufferSize(), &m_pVertexLayout );
    CHECK_HRESULT_ERROR_RETURN(hr, L"CreateInputLayout() failed");

    // Set the input layout
    pD3D11Context->IASetInputLayout( m_pVertexLayout );

    // Compile the pixel shader
    ATL::CComPtr<ID3DBlob> pPSBlob;
    res = CreateShader( "PS", "ps_4_0", &pPSBlob );
    CHECK_HRESULT_ERROR_RETURN(hr, L"CreateShaderFromResource(PS) failed");

    // Create the pixel shader
    hr = m_pD3DDevice->CreatePixelShader( pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), NULL, &m_pPixelShader );
    CHECK_HRESULT_ERROR_RETURN(hr, L"CreatePixelShader(PS) failed");

    // Create vertex buffer
    Vertex vertices[] =
    {
        { XMFLOAT3( -1.0f, 1.0f, -1.0f ),   XMFLOAT4( 0.0f, 0.0f, 1.0f, 1.0f ) },
        { XMFLOAT3( 1.0f, 1.0f, -1.0f ),    XMFLOAT4( 0.0f, 1.0f, 0.0f, 1.0f ) },
        { XMFLOAT3( 1.0f, 1.0f, 1.0f ),     XMFLOAT4( 0.0f, 1.0f, 1.0f, 1.0f ) },
        { XMFLOAT3( -1.0f, 1.0f, 1.0f ),    XMFLOAT4( 1.0f, 0.0f, 0.0f, 1.0f ) },
        { XMFLOAT3( -1.0f, -1.0f, -1.0f ),  XMFLOAT4( 1.0f, 0.0f, 1.0f, 1.0f ) },
        { XMFLOAT3( 1.0f, -1.0f, -1.0f ),   XMFLOAT4( 1.0f, 1.0f, 0.0f, 1.0f ) },
        { XMFLOAT3( 1.0f, -1.0f, 1.0f ),    XMFLOAT4( 1.0f, 1.0f, 1.0f, 1.0f ) },
        { XMFLOAT3( -1.0f, -1.0f, 1.0f ),   XMFLOAT4( 0.0f, 0.0f, 0.0f, 1.0f ) },
    };
    D3D11_BUFFER_DESC bd;
    ZeroMemory( &bd, sizeof(bd) );
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof( Vertex ) * 8;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = 0;
    D3D11_SUBRESOURCE_DATA InitData;
    ZeroMemory( &InitData, sizeof(InitData) );
    InitData.pSysMem = vertices;
    
    hr = m_pD3DDevice->CreateBuffer( &bd, &InitData, &m_pVertexBuffer );
    CHECK_HRESULT_ERROR_RETURN(hr, L"CreateBuffer() - vertex failed");

    // Set vertex buffer
    UINT stride = sizeof( Vertex );
    UINT offset = 0;
    pD3D11Context->IASetVertexBuffers( 0, 1, &m_pVertexBuffer.p, &stride, &offset );

    // Create index buffer
    WORD indices[] =
    {   3,1,0,    2,1,3,
        0,5,4,    1,5,0,
        3,4,7,  0,4,3,
        1,6,5,  2,6,1,
        2,7,6,  3,7,2,
        6,4,5,  7,4,6,
    };
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof( WORD ) * 36;        // 36 vertices needed for 12 triangles in a triangle list
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.CPUAccessFlags = 0;
    InitData.pSysMem = indices;
    hr = m_pD3DDevice->CreateBuffer( &bd, &InitData, &m_pIndexBuffer );
    CHECK_HRESULT_ERROR_RETURN(hr, L"CreateBuffer() - index failed");

    // Set index buffer
    pD3D11Context->IASetIndexBuffer( m_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0 );

    // Set primitive topology
    pD3D11Context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

    // Create the constant buffer
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(ConstantBuffer);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = 0;
    hr = m_pD3DDevice->CreateBuffer( &bd, NULL, &m_pConstantBuffer );
    CHECK_HRESULT_ERROR_RETURN(hr, L"CreateBuffer() - constant failed");

    return AMF_OK;
}
AMF_RESULT VideoRenderDX11::CreateShader(LPCSTR szEntryPoint, LPCSTR szModel, ID3DBlob** ppBlobOut)
{
    HRESULT hr = S_OK;
    DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
    ATL::CComPtr<ID3DBlob> pErrorBlob;


    hr = D3DCompile((LPCSTR)DX11_Shader, sizeof(DX11_Shader),
                    NULL, NULL, NULL, szEntryPoint, szModel, 
                    dwShaderFlags, 0, ppBlobOut, &pErrorBlob);

    CHECK_HRESULT_ERROR_RETURN(hr, L"D3DCompile() failed");

    return AMF_OK;
}
AMF_RESULT VideoRenderDX11::RenderScene()
{
    amf::AMFContext::AMFDX11Locker lock(m_pContext);

    ATL::CComPtr<ID3D11DeviceContext>        pD3D11Context;
    m_pD3DDevice->GetImmediateContext(&pD3D11Context);

    float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    pD3D11Context->ClearRenderTargetView( m_pRenderTargetView, ClearColor );
    

    // Animate the cube
    m_fAnimation += XM_2PI/240;
    XMMATRIX    world = XMMatrixTranspose( XMMatrixRotationRollPitchYaw(m_fAnimation, -m_fAnimation, 0));

    XMVECTOR Eye = XMVectorSet( 0.0f, 1.0f, -5.0f, 0.0f );
    XMVECTOR At = XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f );
    XMVECTOR Up = XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f );
    XMMATRIX view = XMMatrixTranspose( XMMatrixLookAtLH( Eye, At, Up ) );

    // Update variables
    ConstantBuffer cb;
    cb.mWorld = world;
    cb.mView = view;
    cb.mProjection = XMMatrixTranspose( XMMatrixPerspectiveFovLH( XM_PIDIV2, m_width / (FLOAT)m_height, 0.01f, 100.0f ) );
    pD3D11Context->UpdateSubresource( m_pConstantBuffer, 0, NULL, &cb, 0, 0 );

    //
    // Renders a triangle
    //
    pD3D11Context->VSSetShader( m_pVertexShader, NULL, 0 );
    pD3D11Context->VSSetConstantBuffers( 0, 1, &m_pConstantBuffer.p );
    pD3D11Context->PSSetShader( m_pPixelShader, NULL, 0 );
    pD3D11Context->DrawIndexed( 36, 0, 0 );        // 36 vertices needed for 12 triangles in a triangle list
    return AMF_OK;
}


#define SQUARE_SIZE 50

AMF_RESULT VideoRenderDX11::Render(amf::AMFData** ppData)
{
#if !defined(_WIN64 )
// this is done to get identical results on 32 nad 64 bit builds
    _controlfp(_PC_24, MCW_PC);
#endif
    AMF_RESULT res = AMF_OK;
    HRESULT hr = S_OK;
    ATL::CComPtr<ID3D11Texture2D>     pBackBuffer;

    hr = m_pSwapChain->GetBuffer(0,__uuidof( ID3D11Texture2D ), ( LPVOID* ) &pBackBuffer);
    CHECK_HRESULT_ERROR_RETURN(hr, L"m_pSwapChain->GetBuffer() failed");

    res = RenderScene();
    CHECK_AMF_ERROR_RETURN(res, L"RenderScene() failed");

    amf::AMFSurfacePtr pTmpSurface;
    res = m_pContext->CreateSurfaceFromDX11Native(pBackBuffer.p, &pTmpSurface, NULL);
    CHECK_AMF_ERROR_RETURN(res, L"AMFContext::CreateSurfaceFromDX11Native() failed");

    amf::AMFDataPtr pDuplicated;
    pTmpSurface->Duplicate(pTmpSurface->GetMemoryType(), &pDuplicated);
    *ppData = pDuplicated.Detach();
    if(m_bWindow)
    {
        hr = m_pSwapChain->Present(0,0);
        CHECK_HRESULT_ERROR_RETURN(hr, L"m_pSwapChain->Present() failed");
    }
    return res;
}
