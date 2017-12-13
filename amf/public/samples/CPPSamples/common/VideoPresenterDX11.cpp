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
#include "VideoPresenterDX11.h"
#include <d3dcompiler.h>
#include "public/common/TraceAdapter.h"

#pragma comment(lib, "d3dcompiler.lib")

const char DX11_FullScreenQuad[] = 
"//--------------------------------------------------------------------------------------\n"
"// Constant Buffer Variables                                                            \n"
"//--------------------------------------------------------------------------------------\n"
"Texture2D txDiffuse : register( t0 );                                                   \n"
"SamplerState samplerState : register( s0 );                                             \n"
"//--------------------------------------------------------------------------------------\n"
"cbuffer cbNeverChanges : register( b0 )                                                 \n"
"{                                                                                       \n"
"    matrix View;                                                                        \n"
"};                                                                                      \n"
"//--------------------------------------------------------------------------------------\n"
"struct VS_INPUT                                                                         \n"
"{                                                                                       \n"
"    float4 Pos : POSITION;                                                              \n"
"    float2 Tex : TEXCOORD0;                                                             \n"
"};                                                                                      \n"
"//--------------------------------------------------------------------------------------\n"
"struct PS_INPUT                                                                         \n"
"{                                                                                       \n"
"    float4 Pos : SV_POSITION;                                                           \n"
"    float2 Tex : TEXCOORD0;                                                             \n"
"};                                                                                      \n"
"//--------------------------------------------------------------------------------------\n"
"// Vertex Shader                                                                        \n"
"//--------------------------------------------------------------------------------------\n"
"PS_INPUT VS( VS_INPUT input )                                                           \n"
"{                                                                                       \n"
"    PS_INPUT output = (PS_INPUT)0;                                                      \n"
"    output.Pos = mul( input.Pos, View );                                                \n"
"//    output.Pos = mul( output.Pos, Projection );                                       \n"
"    output.Tex = input.Tex;                                                             \n"
"                                                                                        \n"
"    return output;                                                                      \n"
"}                                                                                       \n"
"//--------------------------------------------------------------------------------------\n"
"// Pixel Shader passing texture color                                                   \n"
"//--------------------------------------------------------------------------------------\n"
"float4 PS( PS_INPUT input) : SV_Target                                                  \n"
"{                                                                                       \n"
"   float4 color = txDiffuse.Sample( samplerState, input.Tex );                          \n"
"   color.w = 1.0f;                                                                      \n"
"   return color;                                                                        \n"
"}                                                                                       \n"
;

#if defined(METRO_APP)
VideoPresenterDX11::VideoPresenterDX11(ISwapChainBackgroundPanelNative* pSwapChainPanel, AMFSize swapChainPanelSize, amf::AMFContext* pContext) :
    VideoPresenter(NULL, pContext),
    m_stereo(false),
    m_fScale(1.0f),
    m_fPixelAspectRatio(1.0f),
    m_fOffsetX(0.0f),
    m_fOffsetY(0.0f),
    m_pSwapChainPanel(pSwapChainPanel),
    m_swapChainPanelSize(swapChainPanelSize),
    m_uiAvailableBackBuffer(0),
    m_uiBackBufferCount(4),
    m_bResizeSwapChain(false),
    m_eInputFormat(amf::AMF_SURFACE_BGRA)
{
    memset(&m_sourceVertexRect, 0, sizeof(m_sourceVertexRect));
}

#else

VideoPresenterDX11::VideoPresenterDX11(HWND hwnd, amf::AMFContext* pContext) :
    BackBufferPresenter(hwnd, pContext),
    m_stereo(false),
    m_fScale(1.0f),
    m_fPixelAspectRatio(1.0f),
    m_fOffsetX(0.0f),
    m_fOffsetY(0.0f),
    m_uiAvailableBackBuffer(0),
    m_uiBackBufferCount(4),
    m_bResizeSwapChain(false),
//    m_eInputFormat(amf::AMF_SURFACE_BGRA)
    m_eInputFormat(amf::AMF_SURFACE_RGBA)
//     m_eInputFormat(amf::AMF_SURFACE_RGBA_F16)
{
    memset(&m_sourceVertexRect, 0, sizeof(m_sourceVertexRect));
}
#endif

VideoPresenterDX11::~VideoPresenterDX11()
{
    Terminate();
}


AMF_RESULT VideoPresenterDX11::Present(amf::AMFSurface* pSurface)
{
    AMF_RESULT err = AMF_OK;

    if(m_pDevice == NULL)
    {
        return AMF_NO_DEVICE;
    }
    if(pSurface->GetFormat() != GetInputFormat())
    {
        return AMF_INVALID_FORMAT;
    }
    if( (err = pSurface->Convert(GetMemoryType())) != AMF_OK)
    {
        err;
    }
#if 0
    amf_pts timestamp;
    pSurface->GetProperty(L"Stitch", &timestamp);
    amf_pts latency = amf_high_precision_clock() - timestamp;
    AMFTraceWarning(L"stitch", L"Stitch - Latency = %5.2f", (double)latency / 10000.);
#endif
    {
        amf::AMFContext::AMFDX11Locker dxlock(m_pContext);

        AMFRect rectClient;
        #if defined(METRO_APP)
            //TODO: calculate rect
        {
            CComPtr<ID3D11Texture2D> pDestDxSurface;
            if(FAILED(m_pSwapChain->GetBuffer( 0,  __uuidof(pDestDxSurface), reinterpret_cast<void**>(&pDestDxSurface))))
            {
                return AMF_DIRECTX_FAILED;
            }

            D3D11_TEXTURE2D_DESC desc;
            pDestDxSurface->GetDesc(&desc);
            rectClient.left = 0;
            rectClient.top = 0;
            rectClient.right = desc.Width;
            rectClient.bottom = desc.Height;
        }
        #else
            RECT tmpRectClient = {0, 0, 500, 500};
            GetClientRect((HWND)m_hwnd, &tmpRectClient);
            rectClient = AMFConstructRect(tmpRectClient.left, tmpRectClient.top, tmpRectClient.right, tmpRectClient.bottom);
        #endif

        bool bResized = false;
        CheckForResize(false, &bResized);
        if(!m_bRenderToBackBuffer)
        {
            if(bResized)
            {
                ResizeSwapChain();
            }
            amf::AMFPlane* pPlane = pSurface->GetPlane(amf::AMF_PLANE_PACKED);
            CComPtr<ID3D11Texture2D> pSrcDxSurface = (ID3D11Texture2D*)pPlane->GetNative();
            if (pSrcDxSurface == NULL)
            {
                return AMF_INVALID_POINTER;
            }
            CComPtr<ID3D11Texture2D> pDestDxSurface;
            if(FAILED(m_pSwapChain->GetBuffer( 0,  __uuidof(pDestDxSurface), reinterpret_cast<void**>(&pDestDxSurface))))
            {
                return AMF_DIRECTX_FAILED;
            }
    
            AMFRect srcRect = {pPlane->GetOffsetX(), pPlane->GetOffsetY(), pPlane->GetOffsetX() + pPlane->GetWidth(), pPlane->GetOffsetY() + pPlane->GetHeight()};
            AMFRect outputRect;
            CalcOutputRect(&srcRect, &rectClient, &outputRect);
            //in case of ROI we should specify SrcRect
            err = BitBlt(pSurface->GetFrameType(), pSrcDxSurface, &srcRect, pDestDxSurface, &outputRect);
        }
    }
    
    WaitForPTS(pSurface->GetPts());


    amf::AMFLock lock(&m_sect);
    
    for(int i=0;i<100;i++)
    {
        HRESULT hr=m_pSwapChain->Present( 0, DXGI_PRESENT_DO_NOT_WAIT);
        if(hr != DXGI_ERROR_WAS_STILL_DRAWING )
        {
            //ASSERT_RETURN_IF_HR_FAILED(hr,AMF_DIRECTX_FAILED,L"Present() - Present() failed");
            break;
        }
        amf_sleep(1);
    }
    
    if(m_bRenderToBackBuffer)
    {
        m_uiAvailableBackBuffer--;
    }
    return err;
}

AMF_RESULT VideoPresenterDX11::BitBlt(amf::AMF_FRAME_TYPE eFrameType, ID3D11Texture2D* pSrcSurface, AMFRect* pSrcRect, ID3D11Texture2D* pDstSurface, AMFRect* pDstRect)
{
//    return BitBltCopy(pSrcSurface, pSrcRect, pDstSurface, pDstRect);
    return BitBltRender(eFrameType, pSrcSurface, pSrcRect, pDstSurface, pDstRect);
}

AMF_RESULT VideoPresenterDX11::BitBltCopy(ID3D11Texture2D* pSrcSurface, AMFRect* pSrcRect, ID3D11Texture2D* pDstSurface, AMFRect* pDstRect)
{
    AMF_RESULT err = AMF_OK;

    CComPtr<ID3D11DeviceContext> spContext;
    m_pDevice->GetImmediateContext( &spContext );

    D3D11_TEXTURE2D_DESC srcDesc;
    pSrcSurface->GetDesc( &srcDesc );
    D3D11_TEXTURE2D_DESC dstDesc;
    pDstSurface->GetDesc( &dstDesc );

    D3D11_BOX box;
    box.left = pSrcRect->left;
    box.top = pSrcRect->top;
    box.right = pSrcRect->right;
    box.bottom = pSrcRect->bottom;
    box.front=0;
    box.back=1;

    spContext->CopySubresourceRegion(pDstSurface, 0, pDstRect->left, pDstRect->top, 0, pSrcSurface, 0, &box);
    return err;
}

AMF_RESULT VideoPresenterDX11::BitBltRender(amf::AMF_FRAME_TYPE eFrameType, ID3D11Texture2D* pSrcSurface, AMFRect* pSrcRect, ID3D11Texture2D* pDstSurface, AMFRect* pDstRect)
{
    AMF_RESULT err = AMF_OK;
    HRESULT hr = S_OK;


    CComPtr<ID3D11DeviceContext> spContext;
    m_pDevice->GetImmediateContext( &spContext );

    D3D11_TEXTURE2D_DESC srcDesc;
    pSrcSurface->GetDesc( &srcDesc );

    D3D11_TEXTURE2D_DESC dstDesc;
    pDstSurface->GetDesc( &dstDesc );

    AMFRect  newSourceRect = *pSrcRect;
    AMFSize srcSize ={(amf_int32)srcDesc.Width, (amf_int32)srcDesc.Height};
    if((srcDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) != D3D11_BIND_SHADER_RESOURCE || (srcDesc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE))
    {
        CopySurface(eFrameType, pSrcSurface, pSrcRect);
        AMFRect  newSourceRect;

        newSourceRect.left = 0;
        newSourceRect.top = 0;
        newSourceRect.right = pSrcRect->Width();
        newSourceRect.bottom = pSrcRect->Height();
        srcSize.width = pSrcRect->Width();
        srcSize.height = pSrcRect->Height();

    }
    if(newSourceRect.left != m_sourceVertexRect.left || newSourceRect.top != m_sourceVertexRect.top || newSourceRect.right != m_sourceVertexRect.right || newSourceRect.bottom != m_sourceVertexRect.bottom)
    {
        AMFRect dstRect = {0, 0, (amf_int32)dstDesc.Width, (amf_int32)dstDesc.Height};
        UpdateVertices(newSourceRect, srcSize, dstRect);
        m_sourceVertexRect = newSourceRect;
    }

    // setup all states

    // setup shaders
    spContext->VSSetShader( m_pVertexShader, NULL, 0 );
    spContext->VSSetConstantBuffers( 0, 1, &m_pCBChangesOnResize.p );
    spContext->PSSetShader(m_pPixelShader, NULL, 0 );
    spContext->PSSetConstantBuffers(0, 1, &m_pCBChangesOnResize.p);
    spContext->PSSetSamplers( 0, 1, &m_pSampler.p );


    spContext->OMSetDepthStencilState(m_pDepthStencilState, 1);
    spContext->RSSetState(m_pRasterizerState);
    spContext->IASetInputLayout( m_pVertexLayout );
    spContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );
    spContext->RSSetViewports( 1, &m_CurrentViewport );
    float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    spContext->OMSetBlendState(m_pBlendState, blendFactor, 0xFFFFFFFF);

    // Set vertex buffer

    UINT stride = sizeof( SimpleVertex );
    UINT offset = 0;
    spContext->IASetVertexBuffers( 0, 1, &m_pVertexBuffer.p, &stride, &offset );


    // render left
    if( (eFrameType & amf::AMF_FRAME_LEFT_FLAG) == amf::AMF_FRAME_LEFT_FLAG || (eFrameType & amf::AMF_FRAME_STEREO_FLAG) == 0  || eFrameType == amf::AMF_FRAME_UNKNOWN)
    {
        DrawFrame(m_pCopyTexture_L != NULL ? m_pCopyTexture_L : pSrcSurface , true);
    }

    // render right
    if((eFrameType & amf::AMF_FRAME_RIGHT_FLAG) == amf::AMF_FRAME_RIGHT_FLAG && eFrameType != amf::AMF_FRAME_UNKNOWN)
    {
        DrawFrame(m_pCopyTexture_R != NULL ? m_pCopyTexture_R : pSrcSurface , false);
    }
    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::CopySurface(amf::AMF_FRAME_TYPE eFrameType, ID3D11Texture2D* pSrcSurface, AMFRect* pSrcRect)
{
    HRESULT hr = S_OK;
    D3D11_TEXTURE2D_DESC srcDesc;
    pSrcSurface->GetDesc( &srcDesc );

    CComPtr<ID3D11DeviceContext> spContext;
    m_pDevice->GetImmediateContext( &spContext );

    if(m_pCopyTexture_L == NULL)
    {
        D3D11_TEXTURE2D_DESC Desc={0};
        Desc.ArraySize = 1;
        Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Desc.Usage = D3D11_USAGE_DEFAULT;
        switch(srcDesc.Format)
        {
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            break;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            break;
        default:
            Desc.Format = srcDesc.Format;
            break;
        }


        Desc.Width = pSrcRect->Width();
        Desc.Height = pSrcRect->Height();
        Desc.MipLevels = 1;
        Desc.SampleDesc.Count = 1;
        Desc.CPUAccessFlags = 0;
        hr = m_pDevice->CreateTexture2D(&Desc, NULL, &m_pCopyTexture_L);    
        if(m_stereo)
        {
            hr = m_pDevice->CreateTexture2D(&Desc, NULL, &m_pCopyTexture_R);    
        }
    }
    D3D11_BOX box;
    box.left = pSrcRect->left;
    box.top = pSrcRect->top;
    box.right = pSrcRect->right;
    box.bottom= pSrcRect->bottom;
    box.front=0;
    box.back=1;

    switch(eFrameType)
    {
    case amf::AMF_FRAME_STEREO_RIGHT:
        spContext->CopySubresourceRegion(m_pCopyTexture_R,0,0,0,0,pSrcSurface,0,&box); // we expect that texture comes as a single slice
        break;
    case amf::AMF_FRAME_STEREO_BOTH:
        spContext->CopySubresourceRegion(m_pCopyTexture_L,0,0,0,0,pSrcSurface,0,&box); // we expect that texture comes as array of two silces
        spContext->CopySubresourceRegion(m_pCopyTexture_R,0,0,0,0,pSrcSurface,1,&box);
        break;
    default:
        if(srcDesc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE)
        {
            spContext->CopySubresourceRegion(m_pCopyTexture_L,0,0,0,0,pSrcSurface,m_iSubresourceIndex,&box); // TODO get subresource property
        }
        else
        {
            spContext->CopySubresourceRegion(m_pCopyTexture_L,0,0,0,0,pSrcSurface,0,&box);
        }
        break;
    }
    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::DrawFrame(ID3D11Texture2D* pSrcSurface, bool bLeft)
{
    HRESULT hr = S_OK;
    CComPtr<ID3D11DeviceContext> spContext;
    m_pDevice->GetImmediateContext( &spContext );


    D3D11_TEXTURE2D_DESC srcDesc;
    pSrcSurface->GetDesc( &srcDesc );

    CComPtr<ID3D11ShaderResourceView>   pTextureRV;
    if( srcDesc.ArraySize == 1)
    {
        hr = m_pDevice->CreateShaderResourceView( pSrcSurface, NULL, &pTextureRV);
    }
    else
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc;
        ZeroMemory( &viewDesc, sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC ) );
        viewDesc.Format = srcDesc.Format;
        // For stereo support
        viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY; // we expect that texture comes as array of two silces
        viewDesc.Texture2DArray.ArraySize = srcDesc.ArraySize;
        viewDesc.Texture2DArray.FirstArraySlice= bLeft ? 0 : 1; // left or right
        viewDesc.Texture2DArray.MipLevels = 1;
        viewDesc.Texture2DArray.MostDetailedMip = 0;
        hr = m_pDevice->CreateShaderResourceView( pSrcSurface, &viewDesc, &pTextureRV);

    }
    spContext->PSSetShaderResources( 0, 1, &pTextureRV.p );

    CComPtr<ID3D11RenderTargetView> pRenderTargetView = bLeft ? m_pRenderTargetView_L : m_pRenderTargetView_R;
    float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    spContext->ClearRenderTargetView(pRenderTargetView, ClearColor);
    spContext->OMSetRenderTargets( 1, &pRenderTargetView.p, NULL );

    spContext->Draw( 4, 0 );

    ID3D11RenderTargetView *pNULLRT = NULL;
    spContext->OMSetRenderTargets( 1, &pNULLRT, NULL );

    ID3D11ShaderResourceView* pNULL = NULL;
    spContext->PSSetShaderResources( 0, 1, &pNULL);
    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::Init(amf_int32 width, amf_int32 height)
{
    AMF_RESULT err = AMF_OK;

    VideoPresenter::Init(width, height);

    m_pDevice = static_cast<ID3D11Device*>(m_pContext->GetDX11Device());
    if(m_pDevice == NULL)
    {
        err = AMF_NO_DEVICE;
    }

    if(err == AMF_OK)
    {
        err = CreatePresentationSwapChain();
    }
    err = CompileShaders();
    PrepareStates();

    return err;
}

AMF_RESULT VideoPresenterDX11::Terminate()
{
    m_pRenderTargetView_L = NULL;
    m_pRenderTargetView_R = NULL;

    m_pVertexBuffer = NULL;
    m_pCBChangesOnResize = NULL;
    m_pVertexShader = NULL;
    m_pPixelShader = NULL;
    m_pVertexLayout = NULL;
    m_pSampler = NULL;
    m_pDepthStencilState = NULL;
    m_pRasterizerState = NULL;
    m_pBlendState = NULL;

    m_pCopyTexture_L = NULL;
    m_pCopyTexture_R = NULL;

    m_pContext = NULL;
    m_hwnd = NULL;
    
    m_pDevice = NULL;
    m_pSwapChain.Release();

    return VideoPresenter::Terminate();
}


AMF_RESULT VideoPresenterDX11::CreatePresentationSwapChain()
{
    AMF_RESULT err=AMF_OK;
    HRESULT hr=S_OK;

    amf::AMFLock lock(&m_sect);

    m_pSwapChain.Release();
    for(std::vector<amf::AMFSurface*>::iterator it = m_TrackSurfaces.begin(); it != m_TrackSurfaces.end(); it++)
    {
        (*it)->RemoveObserver(this);
    }
    m_TrackSurfaces.clear();
    m_uiAvailableBackBuffer = 0;

    CComQIPtr<IDXGIDevice> spDXGIDevice=m_pDevice;

    CComPtr<IDXGIAdapter> spDXGIAdapter;
    hr = spDXGIDevice->GetParent(__uuidof(IDXGIAdapter), (void **)&spDXGIAdapter);

    CComPtr<IDXGIFactory2> spIDXGIFactory2;
    spDXGIAdapter->GetParent(__uuidof(IDXGIFactory2), (void **)&spIDXGIFactory2);

    CComPtr<IDXGIOutput>  spOutput;

    m_stereo = false;//for future

    if(spIDXGIFactory2!=NULL)
    {
        // clean context if swap chain was created on the same window before
        CComPtr<ID3D11DeviceContext> spContext;
        m_pDevice->GetImmediateContext( &spContext );
        spContext->ClearState();
        spContext->Flush();

        DXGI_SWAP_CHAIN_DESC1 swapChainDescription = {0};
#if defined(METRO_APP)
        swapChainDescription.Width = (UINT)m_swapChainPanelSize.width;//(UINT)m_window->Bounds.Width;//AK:: it look like winstore mode requires Width Height initialized
        swapChainDescription.Height = (UINT)m_swapChainPanelSize.height;//(UINT)m_window->Bounds.Height;
#else
        swapChainDescription.Width = 0;                                     // use automatic sizing
        swapChainDescription.Height = 0;
#endif
        swapChainDescription.Format = GetDXGIFormat();
        swapChainDescription.Stereo = m_stereo ? TRUE : FALSE;                      // create swapchain in stereo if stereo is enabled
        swapChainDescription.SampleDesc.Count = 1;                          // don't use multi-sampling
        swapChainDescription.SampleDesc.Quality = 0;
//        swapChainDescription.BufferUsage = DXGI_USAGE_SHADER_INPUT | DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHARED;
        swapChainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDescription.BufferCount = m_uiBackBufferCount;                               // use a single buffer and render to it as a FS quad
#if defined(METRO_APP)
        swapChainDescription.Scaling = DXGI_SCALING_STRETCH;                   // set scaling to none//;//
#else
        swapChainDescription.Scaling = DXGI_SCALING_NONE;                   // set scaling to none//DXGI_SCALING_STRETCH;//
#endif
        swapChainDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; // we recommend using this swap effect for all applications - only this works for stereo
//        swapChainDescription.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;
//        swapChainDescription.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH ;

        CComQIPtr<IDXGIDevice1> spDXGIDevice1=spDXGIDevice;

#if defined(METRO_APP)
        {
            //hr=spIDXGIFactory2->CreateSwapChainForCoreWindow(m_pDevice,reinterpret_cast<IUnknown*>(m_window.Get()),&swapChainDescription,spOutput,&m_pSwapChain1);
            hr=spIDXGIFactory2->CreateSwapChainForComposition(m_pDevice, &swapChainDescription, NULL, &m_pSwapChain1);
            // Associate the new swap chain with the SwapChainBackgroundPanel element.

            //CComPtr<ISwapChainBackgroundPanelNative> panelNative;
            //reinterpret_cast<IUnknown*>(m_panel.Get())->QueryInterface(IID_PPV_ARGS(&panelNative));
            hr = m_pSwapChainPanel->SetSwapChain(m_pSwapChain1);

        }
#else
        {
            hr=spIDXGIFactory2->CreateSwapChainForHwnd(m_pDevice, (HWND)m_hwnd, &swapChainDescription, NULL, spOutput, &m_pSwapChain1);
        }
#endif
        if(FAILED(hr) && m_stereo)
        {
            return AMF_FAIL;
        }
    }
    m_pSwapChain = m_pSwapChain1;
    if(m_pSwapChain1 == NULL)
    {
        m_stereo=false;
        CComPtr<IDXGIFactory> spIDXGIFactory;
        spDXGIAdapter->GetParent(__uuidof(IDXGIFactory), (void **)&spIDXGIFactory);

        // setup params
        DXGI_SWAP_CHAIN_DESC sd;
        ZeroMemory( &sd, sizeof(sd) );
        sd.BufferCount = m_uiBackBufferCount;         // use a single buffer and render to it as a FS quad
        sd.BufferDesc.Width = 0;    // will get fr0m window
        sd.BufferDesc.Height = 0;   // will get fr0m window
        sd.BufferDesc.Format = GetDXGIFormat();
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
//        sd.BufferUsage = DXGI_USAGE_SHADER_INPUT | DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHARED;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

        sd.OutputWindow = (HWND)m_hwnd;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.Windowed = TRUE;
//        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        sd.Flags=DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        hr = spIDXGIFactory->CreateSwapChain( m_pDevice, &sd, &m_pSwapChain );
        //ASSERT_RETURN_IF_HR_FAILED(hr,AMF_DIRECTX_FAILED,L"CreatePresentationSwapChain() - CreateSwapChain() failed");
    }
    ResizeSwapChain();
    return err;
}
AMF_RESULT VideoPresenterDX11::CompileShaders()
{
    HRESULT hr = S_OK;
    DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined( _DEBUG )
    dwShaderFlags |= D3DCOMPILE_DEBUG;
#endif
    CComPtr<ID3DBlob> pBlobVertexShader;
    CComPtr<ID3DBlob> pBlobPixelShader;

    CComPtr<ID3DBlob> pErrorBlob;
    hr = D3DCompile( DX11_FullScreenQuad, sizeof(DX11_FullScreenQuad), "DX11_FullScreenQuad", NULL, NULL, "VS", "vs_4_0", 
        dwShaderFlags, 0, &pBlobVertexShader, &pErrorBlob);
    if(FAILED(hr))
    {
        char *data = (char *)pErrorBlob->GetBufferPointer();
        return AMF_FAIL;
    }
    hr = m_pDevice->CreateVertexShader( pBlobVertexShader->GetBufferPointer(), pBlobVertexShader->GetBufferSize(), NULL, &m_pVertexShader );
    if(FAILED(hr))
    {
        return AMF_FAIL;
    }
    // Define the input layout
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    UINT numElements = ARRAYSIZE( layout );
    // Create the input layout
    hr = m_pDevice->CreateInputLayout( layout, numElements, pBlobVertexShader->GetBufferPointer(),pBlobVertexShader->GetBufferSize(), &m_pVertexLayout );


    hr = D3DCompile( DX11_FullScreenQuad, sizeof(DX11_FullScreenQuad), "DX11_FullScreenQuad", NULL, NULL, "PS", "ps_4_0", 
        dwShaderFlags, 0, &pBlobPixelShader, &pErrorBlob);
    if(FAILED(hr))
    {
        char *data = (char *)pErrorBlob->GetBufferPointer();
        return AMF_FAIL;
    }
    hr = m_pDevice->CreatePixelShader( pBlobPixelShader->GetBufferPointer(), pBlobPixelShader->GetBufferSize(), NULL, &m_pPixelShader);
    if(FAILED(hr))
    {
        return AMF_FAIL;
    }


    return AMF_OK;
}

AMF_RESULT VideoPresenterDX11::PrepareStates()
{
    HRESULT hr = S_OK;
    // Create vertex buffer (quad will be set later)
    D3D11_BUFFER_DESC bd;
    ZeroMemory( &bd, sizeof(bd) );
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof( SimpleVertex ) * 4; // 4 - for video frame
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = 0;

    hr = m_pDevice->CreateBuffer( &bd, NULL, &m_pVertexBuffer );

    // Initialize the view matrix
    XMMATRIX worldViewProjection=XMMatrixIdentity();
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(CBNeverChanges);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA InitData={0};
    InitData.pSysMem = &worldViewProjection;

    hr = m_pDevice->CreateBuffer( &bd, &InitData, &m_pCBChangesOnResize );

    // Create the sample state

    D3D11_SAMPLER_DESC sampDesc;
    ZeroMemory( &sampDesc, sizeof(sampDesc) );
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = m_pDevice->CreateSamplerState( &sampDesc, &m_pSampler );

    // set Z-buffer off

    D3D11_DEPTH_STENCIL_DESC depthDisabledStencilDesc={0};
    depthDisabledStencilDesc.DepthEnable = FALSE;
    depthDisabledStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthDisabledStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;
    depthDisabledStencilDesc.StencilEnable = TRUE;
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

    hr = m_pDevice->CreateDepthStencilState(&depthDisabledStencilDesc, &m_pDepthStencilState);

// Create the rasterizer state which will determine how and what polygons will be drawn.
    D3D11_RASTERIZER_DESC rasterDesc;
    memset(&rasterDesc,0,sizeof(rasterDesc));
    rasterDesc.AntialiasedLineEnable = false;
    rasterDesc.CullMode = D3D11_CULL_BACK;
    rasterDesc.DepthBias = 0;
    rasterDesc.DepthBiasClamp = 0.0f;
    rasterDesc.DepthClipEnable = true;
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.FrontCounterClockwise = false;
    rasterDesc.MultisampleEnable = false;
    rasterDesc.ScissorEnable = false;
    rasterDesc.SlopeScaledDepthBias = 0.0f;

    // Create the rasterizer state from the description we just filled out.
    hr = m_pDevice->CreateRasterizerState(&rasterDesc, &m_pRasterizerState);

    D3D11_BLEND_DESC blendDesc;
    ZeroMemory( &blendDesc, sizeof(blendDesc));
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = 0x0F;


    hr = m_pDevice->CreateBlendState( &blendDesc, &m_pBlendState);

    return AMF_OK;
}

AMF_RESULT            VideoPresenterDX11::CheckForResize(bool bForce, bool *bResized)
{
    *bResized = false;
    AMF_RESULT err=AMF_OK;
    HRESULT hr = S_OK;


    CComPtr<ID3D11Texture2D> spBuffer;
    hr = m_pSwapChain->GetBuffer( 0,  __uuidof(spBuffer), reinterpret_cast<void**>(&spBuffer));


    D3D11_TEXTURE2D_DESC bufferDesc;
    spBuffer->GetDesc( &bufferDesc );

#if defined(METRO_APP)
    amf_int width = bufferDesc.Width;
    amf_int height = bufferDesc.Height;
#else
    RECT client;
    if(m_hwnd!=NULL)
    {
        ::GetClientRect((HWND)m_hwnd,&client);
    }
    else if(m_pSwapChain1!=NULL)
    {
        DXGI_SWAP_CHAIN_DESC1 SwapDesc;
        m_pSwapChain1->GetDesc1(&SwapDesc);
        client.left=0;
        client.top=0;
        client.right=SwapDesc.Width;
        client.bottom=SwapDesc.Height;
    }
    amf_int width=client.right-client.left;
    amf_int height=client.bottom-client.top;
#endif

    if(!bForce && ((width==(amf_int)bufferDesc.Width && height==(amf_int)bufferDesc.Height) || width == 0 || height == 0 ))
    {
        return AMF_OK;
    }
    *bResized = true;
    m_bResizeSwapChain = true;
    return AMF_OK;
}
AMF_RESULT VideoPresenterDX11::ResizeSwapChain()
{
    RECT client;

#if !defined(METRO_APP)
    if(m_hwnd!=NULL)
    {

        ::GetClientRect((HWND)m_hwnd,&client);
    }
    else
#endif
    if(m_pSwapChain1!=NULL)
    {
        DXGI_SWAP_CHAIN_DESC1 SwapDesc;
        m_pSwapChain1->GetDesc1(&SwapDesc);
        client.left=0;
        client.top=0;
        client.right=SwapDesc.Width;
        client.bottom=SwapDesc.Height;
    }
    amf_int width=client.right-client.left;
    amf_int height=client.bottom-client.top;

    // clear views and temp surfaces
    m_pRenderTargetView_L = NULL;
    m_pRenderTargetView_R = NULL;
    m_pCopyTexture_L = NULL;
    m_pCopyTexture_R = NULL;

    CComPtr<ID3D11DeviceContext> spContext;
    m_pDevice->GetImmediateContext( &spContext );

 //   spContext->ClearState();
    spContext->OMSetRenderTargets(0, 0, 0);

    // resize
    HRESULT hr=m_pSwapChain->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);

    // Create render target view
    CComPtr<ID3D11Texture2D> spBackBuffer;
    hr = m_pSwapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), (LPVOID*)&spBackBuffer );

    D3D11_RENDER_TARGET_VIEW_DESC RenderTargetViewDescription;
    ZeroMemory(&RenderTargetViewDescription, sizeof(RenderTargetViewDescription));
    RenderTargetViewDescription.Format = GetDXGIFormat();
    
    RenderTargetViewDescription.ViewDimension =D3D11_RTV_DIMENSION_TEXTURE2DARRAY;            // render target view is a Texture2D array
    RenderTargetViewDescription.Texture2DArray.MipSlice = 0;                                   // each array element is one Texture2D
    RenderTargetViewDescription.Texture2DArray.ArraySize = 1;
    RenderTargetViewDescription.Texture2DArray.FirstArraySlice = 0;                            // first Texture2D of the array is the left eye view

    hr = m_pDevice->CreateRenderTargetView( spBackBuffer, &RenderTargetViewDescription, &m_pRenderTargetView_L );

    if (m_stereo)
    {
        RenderTargetViewDescription.Texture2DArray.FirstArraySlice = 1;                        // second Texture2D of the array is the right eye view
        hr = m_pDevice->CreateRenderTargetView( spBackBuffer, &RenderTargetViewDescription, &m_pRenderTargetView_R );
    }

    m_CurrentViewport.TopLeftX=0;
    m_CurrentViewport.TopLeftY=0;
    m_CurrentViewport.Width=FLOAT(width);
    m_CurrentViewport.Height=FLOAT(height);
    m_CurrentViewport.MinDepth=0.0f;
    m_CurrentViewport.MaxDepth=1.0f;

    m_uiAvailableBackBuffer = 0;

    m_rectClient = AMFConstructRect(client.left, client.top, client.right, client.bottom);
    return AMF_OK;
}
AMF_RESULT VideoPresenterDX11::UpdateVertices(AMFRect srcRect, AMFSize srcSize, AMFRect dstRect)
{
    HRESULT hr=S_OK;

    SimpleVertex vertices[4];

    // stretch video rect to back buffer
    FLOAT  w=2.f;
    FLOAT  h=2.f;

    w *= m_fScale;
    h *= m_fScale;


    FLOAT fVideoRatio = static_cast<FLOAT>(srcRect.Width() * m_fPixelAspectRatio / srcRect.Height());
    FLOAT fScreenRatio = static_cast<FLOAT>(dstRect.Width()) / dstRect.Height();

    if(fVideoRatio > fScreenRatio)
    {
        h *= fScreenRatio;
        h /= fVideoRatio;
    }
    else
    {
        w /= fScreenRatio;
        w *= fVideoRatio;
    }

    FLOAT centerX = m_fOffsetX * 2.f / dstRect.Width();
    FLOAT centerY = - m_fOffsetY * 2.f/ dstRect.Height();

    FLOAT leftDst = centerX - w / 2;
    FLOAT rightDst = leftDst + w;
    FLOAT topDst = centerY - h / 2;
    FLOAT bottomDst = topDst + h;

    centerX = (srcRect.left + srcRect.right) / 2.f / srcRect.Width();
    centerY = (srcRect.top + srcRect.bottom) / 2.f / srcRect.Height();

    w = (FLOAT)(srcRect.Width()) / srcSize.width;
    h = (FLOAT)(srcRect.Height()) / srcSize.height;

    FLOAT leftSrc = centerX - w/2;
    FLOAT rightSrc = leftSrc + w;
    FLOAT topSrc = centerY - h/2;
    FLOAT bottomSrc = topSrc + h;

    vertices[0].position = XMFLOAT3(leftDst, bottomDst, 0.0f);
    vertices[0].texture = XMFLOAT2(leftSrc, topSrc);

    vertices[1].position = XMFLOAT3(rightDst, bottomDst, 0.0f);
    vertices[1].texture = XMFLOAT2(rightSrc, topSrc);

    vertices[2].position = XMFLOAT3(leftDst, topDst, 0.0f);
    vertices[2].texture = XMFLOAT2(leftSrc, bottomSrc);

    // Second triangle.
    vertices[3].position = XMFLOAT3(rightDst, topDst, 0.0f);
    vertices[3].texture = XMFLOAT2(rightSrc, bottomSrc);

    CComPtr<ID3D11DeviceContext> spContext;
    m_pDevice->GetImmediateContext( &spContext );
    spContext->UpdateSubresource( m_pVertexBuffer, 0, NULL, vertices, 0, 0 );

    return AMF_OK;
}

AMF_RESULT AMF_STD_CALL VideoPresenterDX11::AllocSurface(amf::AMF_MEMORY_TYPE type, amf::AMF_SURFACE_FORMAT format,
            amf_int32 width, amf_int32 height, amf_int32 hPitch, amf_int32 vPitch, amf::AMFSurface** ppSurface)
{
    if(!m_bRenderToBackBuffer)
    {
        return AMF_NOT_IMPLEMENTED;
    }
    // wait till buffers are released
    while( m_uiAvailableBackBuffer + 1 >= m_uiBackBufferCount)
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

    CComPtr<ID3D11Texture2D> pDestDxSurface;
    if(FAILED(m_pSwapChain->GetBuffer( m_uiAvailableBackBuffer,  __uuidof(pDestDxSurface), reinterpret_cast<void**>(&pDestDxSurface))))
    {
        return AMF_DIRECTX_FAILED;
    }
    m_uiAvailableBackBuffer++;
    m_pContext->CreateSurfaceFromDX11Native((void*)(ID3D11Texture2D*)pDestDxSurface, ppSurface, this);
    m_TrackSurfaces.push_back(*ppSurface);
    return res;
}

void AMF_STD_CALL VideoPresenterDX11::OnSurfaceDataRelease(amf::AMFSurface* pSurface)
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

AMF_RESULT              VideoPresenterDX11::SetInputFormat(amf::AMF_SURFACE_FORMAT format)
{
    if(format != amf::AMF_SURFACE_BGRA && format != amf::AMF_SURFACE_RGBA  && format != amf::AMF_SURFACE_RGBA_F16)
    {
        return AMF_FAIL;
    }
    m_eInputFormat = format;
    return AMF_OK;
}
DXGI_FORMAT VideoPresenterDX11::GetDXGIFormat() const
{ 
    DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM;
    switch(GetInputFormat())
    {
    case amf::AMF_SURFACE_BGRA:
        format = DXGI_FORMAT_B8G8R8A8_UNORM;
        break;
    case amf::AMF_SURFACE_RGBA:
        format = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    case amf::AMF_SURFACE_RGBA_F16:
        format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        break;
    }
    return format;
}

AMF_RESULT              VideoPresenterDX11::Flush()
{
    m_uiAvailableBackBuffer = 0;
    return BackBufferPresenter::Flush();
}
