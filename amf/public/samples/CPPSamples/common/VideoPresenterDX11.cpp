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
#include "VideoPresenterDX11.h"

#include <d3dcompiler.h>
#include "public/common/TraceAdapter.h"
#include "public/include/components/VideoDecoderUVD.h"
#include "public/include/components/VideoConverter.h"

// to enble this code switch to Windows 10 SDK for Windows 10 RS2 (10.0.15063.0) or later.
#if defined(NTDDI_WIN10_RS2)
#include <DXGI1_6.h>
#endif

#define USE_COLOR_TWITCH_IN_DISPLAY 0


#pragma comment(lib, "d3dcompiler.lib")

#define  AMF_FACILITY  L"VideoPresenterDX11"

struct SimpleVertex
{
	XMFLOAT3 position;
	XMFLOAT2 texture;
};
struct CBNeverChanges
{
	XMMATRIX mView;
};

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
"    output.Pos = mul(View, float4(input.Pos.xyz, 1));                                   \n"
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

typedef     HRESULT(WINAPI *DCompositionCreateSurfaceHandle_Fn)(DWORD desiredAccess, SECURITY_ATTRIBUTES *securityAttributes, HANDLE *surfaceHandle);
typedef     HRESULT(WINAPI *DCompositionCreateDevice_Fn)(IDXGIDevice *dxgiDevice, REFIID iid, void **dcompositionDevice);
typedef     HRESULT(WINAPI *DCompositionCreateDevice2_Fn)(IUnknown *renderingDevice,REFIID iid,void **dcompositionDevice);
typedef     HRESULT(WINAPI *DCompositionCreateDevice3_Fn)(IUnknown *renderingDevice, REFIID iid, void **dcompositionDevice);

typedef     HRESULT(WINAPI *DCompositionAttachMouseDragToHwnd_Fn)(IDCompositionVisual* visual,HWND hwnd, BOOL enable);
VideoPresenterDX11::VideoPresenterDX11(amf_handle hwnd, amf::AMFContext* pContext) :
    BackBufferPresenter(hwnd, pContext),
    m_stereo(false),
    m_fScale(1.0f),
    m_fPixelAspectRatio(1.0f),
    m_fOffsetX(0.0f),
    m_fOffsetY(0.0f),
    m_uiAvailableBackBuffer(0),
    m_uiBackBufferCount(4),
    m_bResizeSwapChain(false),
    m_bFirstFrame(true),
    m_hDcompDll(0),
    m_hDCompositionSurfaceHandle(nullptr),
//    m_eInputFormat(amf::AMF_SURFACE_BGRA)
    m_eInputFormat(amf::AMF_SURFACE_RGBA)
//     m_eInputFormat(amf::AMF_SURFACE_RGBA_F16)
//    m_eInputFormat(amf::AMF_SURFACE_R10G10B10A2)

{
}

VideoPresenterDX11::~VideoPresenterDX11()
{
    Terminate();
    m_pContext = NULL;
    m_hwnd = NULL;

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
    }

    ApplyCSC(pSurface);
	UINT presentFlags = DXGI_PRESENT_RESTART;
	UINT syncInterval = 0;
	if (m_bWaitForVSync == false)
	{
		presentFlags |= DXGI_PRESENT_DO_NOT_WAIT;
	}

    {
        amf::AMFContext::AMFDX11Locker dxlock(m_pContext);

        if (m_pSwapChainVideo != nullptr)
        {

            if (m_pDecodeTexture != (ID3D11Texture2D*)pSurface->GetPlaneAt(0)->GetNative())
            {
                Terminate();
                Init(0, 0, pSurface);
            }
            amf_int64 index = 0;
            pSurface->GetProperty(L"TextureArrayIndex", &index);

            amf::AMFLock lock(&m_sect);


            for (int i = 0; i < 100; i++)
            {
//                HRESULT hr = m_pSwapChainVideo->PresentBuffer((UINT)index, syncInterval, presentFlags);
                HRESULT hr = m_pSwapChainVideo->PresentBuffer((UINT)index, 1, DXGI_PRESENT_RESTART);

                if (FAILED(hr))
                {

//                    AMFTraceWarning(AMF_FACILITY, L"Present() - Present() failed hr=HR = %0X", hr);
                }
                if (hr != DXGI_ERROR_WAS_STILL_DRAWING)
                {
                    //ASSERT_RETURN_IF_HR_FAILED(hr,AMF_DIRECTX_FAILED,L"Present() - Present() failed");
                    break;
                }
                amf_sleep(1);
            }
            return AMF_OK;
        }

        AMFRect rectClient;

        rectClient = GetClientRect();

        bool bResized = false;
        CheckForResize(false, &bResized);
        if(!m_bRenderToBackBuffer)
        {
            if(bResized)
            {
                ResizeSwapChain();
            }
            amf::AMFPlane* pPlane = pSurface->GetPlaneAt(0);
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

    CComPtr<ID3D11DeviceContext> spContext;
    m_pDevice->GetImmediateContext(&spContext);
    spContext->OMSetRenderTargets(1, &m_pRenderTargetView_L.p, NULL);

    CustomDraw();

    HRESULT hr = S_OK;
    for(int i=0;i<100;i++)
    {
//        
        hr = m_pSwapChain->Present(syncInterval, presentFlags);
        
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
    //if (GetInputFormat() == amf::AMF_SURFACE_NV12)
    //{
    //    return BitBltCopy(pSrcSurface, pSrcRect, pDstSurface, pDstRect);
    //}
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
    spContext->Flush();

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
    AMFSize dstSize = {(amf_int32)dstDesc.Width, (amf_int32)dstDesc.Height};
    UpdateVertices(&newSourceRect, &srcSize, pDstRect, &dstSize);

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

AMF_RESULT VideoPresenterDX11::Init(amf_int32 width, amf_int32 height, amf::AMFSurface* pSurface)
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
        err = CreatePresentationSwapChain(pSurface);
    }
    if (m_hDCompositionSurfaceHandle == nullptr)
    {
        err = CompileShaders();
        PrepareStates();
    }
    m_bFirstFrame = true;

    return err;
}

AMF_RESULT VideoPresenterDX11::Terminate()
{
    m_pCurrentOutput = NULL;

    if (m_pSwapChain1 != nullptr)
    {
        m_pSwapChain1->SetFullscreenState(FALSE, NULL);
    }

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

    
    m_pSwapChainVideo.Release();
    m_pSwapChain1.Release();
    m_pSwapChain.Release();

    m_pDecodeTexture.Release();
    m_pVisualSurfaceRoot.Release();

    m_pDCompTarget.Release();
    m_pDCompDevice.Release();

    if (m_hDCompositionSurfaceHandle != nullptr)
    {
        CloseHandle(m_hDCompositionSurfaceHandle);
        m_hDCompositionSurfaceHandle = nullptr;
    }
    if (m_hDcompDll != 0)
    {
        amf_free_library(m_hDcompDll);
        m_hDcompDll = 0;
    }

    m_pDevice = NULL;

    return VideoPresenter::Terminate();
}


AMF_RESULT VideoPresenterDX11::CreatePresentationSwapChain(amf::AMFSurface* pSurface)
{
    AMF_RESULT err=AMF_OK;
    HRESULT hr=S_OK;

    amf::AMFLock lock(&m_sect);

    m_pCurrentOutput.Release();
    m_pSwapChainVideo.Release();
    m_pSwapChain1.Release();
    m_pSwapChain.Release();

    m_pDecodeTexture.Release();

    m_pVisualSurfaceRoot.Release();

    m_pDCompTarget.Release();
    m_pDCompDevice.Release();
    if (m_hDCompositionSurfaceHandle != nullptr)
    {
        CloseHandle(m_hDCompositionSurfaceHandle);
        m_hDCompositionSurfaceHandle = nullptr;
    }


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

    HMONITOR hMonitor = MonitorFromWindow((HWND)m_hwnd, MONITOR_DEFAULTTONEAREST);

    for (UINT i = 0;; i++)
    {
        CComPtr<IDXGIOutput>                pOutput;
        hr = spDXGIAdapter->EnumOutputs(i, &pOutput);
        if (pOutput == nullptr)
        {
            break;
        }
        DXGI_OUTPUT_DESC outputDesc = {};
        pOutput->GetDesc(&outputDesc);
        if (outputDesc.Monitor == hMonitor)
        {
            m_pCurrentOutput = pOutput;
            break;
        }
    }
    if (m_pCurrentOutput == nullptr)
    {
        hr = spDXGIAdapter->EnumOutputs(0, &m_pCurrentOutput);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"EnumOutputs(0) failed");
    }
    m_stereo = false;//for future

    if(spIDXGIFactory2!=NULL)
    {
        // clean context if swap chain was created on the same window before
        CComPtr<ID3D11DeviceContext> spContext;
        m_pDevice->GetImmediateContext( &spContext );
        spContext->ClearState();
        spContext->Flush();

        if (GetInputFormat() == amf::AMF_SURFACE_NV12 && pSurface != nullptr)
        {
            DXGI_OUTPUT_DESC outputDesc = {};
            m_pCurrentOutput->GetDesc(&outputDesc);
            ::MoveWindow((HWND)m_hwnd, 0, 0, 
                outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left,
                outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top, TRUE);

            m_pDecodeTexture = (ID3D11Texture2D*)pSurface->GetPlaneAt(0)->GetNative();
            
            CComQIPtr<IDXGIDevice1> spDXGIDevice1=spDXGIDevice;

            if (m_hDcompDll == 0)
            {
#ifdef _WIN32
                m_hDcompDll = amf_load_library(L"Dcomp.dll");
#else
                m_hDcompDll = amf_load_library1(L"Dcomp.dll", true); //global flag set to true
#endif
                AMF_RETURN_IF_FALSE(m_hDcompDll != nullptr, AMF_DIRECTX_FAILED, L"Dcomp.dll is not available");
            }
            DCompositionCreateSurfaceHandle_Fn fDCompositionCreateSurfaceHandle = (DCompositionCreateSurfaceHandle_Fn)amf_get_proc_address(m_hDcompDll, "DCompositionCreateSurfaceHandle");
            AMF_RETURN_IF_FALSE(fDCompositionCreateSurfaceHandle != nullptr, AMF_DIRECTX_FAILED, L"DCompositionCreateSurfaceHandle is not available");
            
            DCompositionCreateDevice_Fn fDCompositionCreateDevice = (DCompositionCreateDevice_Fn)amf_get_proc_address(m_hDcompDll, "DCompositionCreateDevice");
            AMF_RETURN_IF_FALSE(fDCompositionCreateDevice != nullptr, AMF_DIRECTX_FAILED, L"DCompositionCreateDevice is not available");

            DCompositionCreateDevice3_Fn fDCompositionCreateDevice3 = (DCompositionCreateDevice3_Fn)amf_get_proc_address(m_hDcompDll, "DCompositionCreateDevice3");
            AMF_RETURN_IF_FALSE(fDCompositionCreateDevice3 != nullptr, AMF_DIRECTX_FAILED, L"DCompositionCreateDevice3 is not available");

            DCompositionAttachMouseDragToHwnd_Fn fDCompositionAttachMouseDragToHwnd= (DCompositionAttachMouseDragToHwnd_Fn)amf_get_proc_address(m_hDcompDll, "DCompositionAttachMouseDragToHwnd");
            AMF_RETURN_IF_FALSE(fDCompositionAttachMouseDragToHwnd != nullptr, AMF_DIRECTX_FAILED, L"DCompositionAttachMouseDragToHwnd is not available");

            hr = fDCompositionCreateDevice3(spDXGIDevice, __uuidof(IDCompositionDesktopDevice), (void**)&m_pDCompDevice);
//            hr = fDCompositionCreateDevice(spDXGIDevice, __uuidof(IDCompositionDevice), (void**)&m_pDCompDevice);
            AMF_RETURN_IF_FALSE(m_pDCompDevice != nullptr, AMF_DIRECTX_FAILED, L"DCompositionCreateDevice() failed");

            hr = m_pDCompDevice->CreateTargetForHwnd((HWND)m_hwnd, TRUE, &m_pDCompTarget);
            ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"DCompDevice->CreateTargetForHwnd() failed");


            #define COMPOSITIONSURFACE_ALL_ACCESS  0x0003L

            hr = fDCompositionCreateSurfaceHandle(COMPOSITIONSURFACE_ALL_ACCESS, nullptr, &m_hDCompositionSurfaceHandle);
            ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"DCompositionCreateSurfaceHandle() failed");

            hr = m_pDCompDevice->CreateVisual(&m_pVisualSurfaceRoot);

            hr = m_pDCompTarget->SetRoot(m_pVisualSurfaceRoot);
            ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetRoot() failed");

            CComQIPtr<IDXGIFactoryMedia> pDXGIDeviceMedia;

            spDXGIAdapter->GetParent(__uuidof(IDXGIFactoryMedia), (void **)&pDXGIDeviceMedia);
            AMF_RETURN_IF_FALSE(pDXGIDeviceMedia != nullptr, AMF_DIRECTX_FAILED, L"CComQIPtr<IDXGIFactoryMedia> is not available");

            DXGI_DECODE_SWAP_CHAIN_DESC descVideoSwap = {};

            CComQIPtr<IDXGIResource> spDXGResource = m_pDecodeTexture;
            hr = pDXGIDeviceMedia->CreateDecodeSwapChainForCompositionSurfaceHandle(m_pDevice, m_hDCompositionSurfaceHandle, &descVideoSwap, spDXGResource, nullptr, &m_pSwapChainVideo);
            
            if (FAILED(hr))
            {
                return AMF_FAIL;
            }
            m_pSwapChainVideo->SetColorSpace(DXGI_MULTIPLANE_OVERLAY_YCbCr_FLAG_BT709);

            RECT sourceRect = { 0,0,pSurface->GetPlaneAt(0)->GetWidth(), pSurface->GetPlaneAt(0)->GetHeight() };

            m_pSwapChainVideo->SetSourceRect(&sourceRect);
            RECT dstRect = { 0,0,
                outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left,
                outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top };

            m_pSwapChainVideo->SetTargetRect(&dstRect);

//            CComPtr<IUnknown> pUnknownSurface;
//            hr = m_pDCompDevice->CreateSurfaceFromHandle(m_hDCompositionSurfaceHandle, &pUnknownSurface);
//            ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateSurfaceFromHandle() failed");

            //            CComPtr<IDCompositionVisual2>        pVisualSurfaceRoot;
            //            hr = m_pDCompDevice->CreateVisual(&pVisualSurfaceRoot);
            //            ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateVisual() failed");
            // hr = m_pVisualSurface->SetContent(pUnknownSurface);
//            hr = pVisualSurfaceRoot->AddVisual(m_pVisualSurface, TRUE, nullptr);


            hr = m_pVisualSurfaceRoot->SetContent(m_pSwapChainVideo);
            ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetContent(m_pSwapChainVideo) failed");
            CComQIPtr<IDCompositionVisual3>   pVisualSurface3(m_pVisualSurfaceRoot);
            if (pVisualSurface3 != nullptr)
            {
                pVisualSurface3->SetVisible(TRUE);
            }
            fDCompositionAttachMouseDragToHwnd(m_pVisualSurfaceRoot, (HWND)m_hwnd, TRUE);

            hr = m_pDCompDevice->Commit();
            ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"Commit() failed");

            ShowCursor(TRUE);
        }
        else
        {
            DXGI_SWAP_CHAIN_DESC1 swapChainDescription = { 0 };
            swapChainDescription.Width = 0;                                     // use automatic sizing
            swapChainDescription.Height = 0;

            swapChainDescription.Format = GetDXGIFormat();
            swapChainDescription.Stereo = m_stereo ? TRUE : FALSE;                      // create swapchain in stereo if stereo is enabled
            swapChainDescription.SampleDesc.Count = 1;                          // don't use multi-sampling
            swapChainDescription.SampleDesc.Quality = 0;
            //        swapChainDescription.BufferUsage = DXGI_USAGE_SHADER_INPUT | DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHARED;
            swapChainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swapChainDescription.BufferCount = m_uiBackBufferCount;                               // use a single buffer and render to it as a FS quad
            swapChainDescription.Scaling = DXGI_SCALING_NONE;                   // set scaling to none//DXGI_SCALING_STRETCH;//

            swapChainDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; // we recommend using this swap effect for all applications - only this works for stereo
//            swapChainDescription.Flags = DXGI_SWAP_CHAIN_FLAG_FULLSCREEN_VIDEO; //DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

            hr = spIDXGIFactory2->CreateSwapChainForHwnd(m_pDevice, (HWND)m_hwnd, &swapChainDescription, NULL, nullptr, &m_pSwapChain1);
            if (FAILED(hr) && m_stereo)
            {
                return AMF_FAIL;
            }
            if (SUCCEEDED(hr))
            {
                hr = m_pSwapChain1->SetFullscreenState(m_bFullScreen ? TRUE : FALSE, NULL);
            }

            if (SUCCEEDED(hr))
            {

                ATL::CComQIPtr<IDXGIOutput6> spDXGIOutput6(m_pCurrentOutput);
                if (spDXGIOutput6 != NULL)
                {
                    DXGI_OUTPUT_DESC1 desc = {};
                    spDXGIOutput6->GetDesc1(&desc);

                    if (desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) // HDR
                    {
                        ATL::CComQIPtr<IDXGISwapChain3> pSwapChain3(m_pSwapChain1);
                        if (pSwapChain3 != NULL)
                        {
                            if (GetInputFormat() == amf::AMF_SURFACE_RGBA_F16)
                            {
                                pSwapChain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
                            }
                            else if (GetInputFormat() == amf::AMF_SURFACE_R10G10B10A2)
                            {
                                pSwapChain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
                            }
                        }
                    }
                }
            }
        }
        

    }
    m_pSwapChain = m_pSwapChain1;
    if(m_pSwapChain1 == nullptr && m_pSwapChainVideo == nullptr)
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
        sd.Windowed = m_bFullScreen ? FALSE : TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        sd.Flags=DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        hr = spIDXGIFactory->CreateSwapChain( m_pDevice, &sd, &m_pSwapChain );
        ASSERT_RETURN_IF_HR_FAILED(hr,AMF_DIRECTX_FAILED,L"CreatePresentationSwapChain() - CreateSwapChain() failed");
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

    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(CBNeverChanges);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA InitData={0};
    InitData.pSysMem = &m_mSrcToScreenMatrix;

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

    if (m_hDCompositionSurfaceHandle != nullptr)
    {
        return AMF_OK;
    }

    CComPtr<ID3D11Texture2D> spBuffer;

    if (m_pSwapChainVideo != nullptr)
    {
        return AMF_OK;
    }
    hr = m_pSwapChain->GetBuffer( 0,  __uuidof(spBuffer), reinterpret_cast<void**>(&spBuffer));


    D3D11_TEXTURE2D_DESC bufferDesc;
    spBuffer->GetDesc( &bufferDesc );

    BOOL bFullScreen = FALSE;
    CComPtr<IDXGIOutput> pOutput;
    m_pSwapChain->GetFullscreenState(&bFullScreen, &pOutput);

    amf_uint width = 0;
    amf_uint height = 0;

    if(m_hwnd!=NULL && bFullScreen == FALSE)
    {
        AMFRect client = GetClientRect();
        width = client.right - client.left;
        height = client.bottom - client.top;
    }
    else 
    {
        DXGI_SWAP_CHAIN_DESC SwapDesc;
        m_pSwapChain->GetDesc(&SwapDesc);
        width = SwapDesc.BufferDesc.Width;
        width = SwapDesc.BufferDesc.Height;
    }


    if(!bForce && ((width==(amf_int)bufferDesc.Width && height==(amf_int)bufferDesc.Height) || width == 0 || height == 0 ) &&
        (bool)bFullScreen == m_bFullScreen)
    {
        return AMF_OK;
    }
    *bResized = true;
    m_bResizeSwapChain = true;
    return AMF_OK;
}

AMFSize             VideoPresenterDX11::GetSwapchainSize()
{
    AMFSize size = {};
    if (m_pSwapChain != nullptr)
    {
        DXGI_SWAP_CHAIN_DESC SwapDesc;
        m_pSwapChain->GetDesc(&SwapDesc);
        size.width = amf_int32(SwapDesc.BufferDesc.Width);
        size.height = amf_int32(SwapDesc.BufferDesc.Height);
    }
    return size;
}

AMF_RESULT VideoPresenterDX11::ResizeSwapChain()
{
    if (m_hDCompositionSurfaceHandle != nullptr)
    {
        return AMF_OK;
    }

    // clear views and temp surfaces
    m_pRenderTargetView_L = NULL;
    m_pRenderTargetView_R = NULL;
    m_pCopyTexture_L = NULL;
    m_pCopyTexture_R = NULL;

    HRESULT hr = S_OK;
    if (m_pSwapChain != nullptr)
    {
        hr = m_pSwapChain->SetFullscreenState(m_bFullScreen ? TRUE : FALSE, NULL);
    }

    CComPtr<ID3D11DeviceContext> spContext;
    m_pDevice->GetImmediateContext( &spContext );

 //   spContext->ClearState();
    spContext->OMSetRenderTargets(0, 0, 0);

    DXGI_OUTPUT_DESC outputDesc = {};
    m_pCurrentOutput->GetDesc(&outputDesc);

    UINT width = 0;
    UINT height = 0;

    if (m_bFullScreen)
    {
        width = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
        height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
    }

    // resize
    hr = m_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0); // DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);

    // verify
    DXGI_SWAP_CHAIN_DESC SwapDesc;
    m_pSwapChain->GetDesc(&SwapDesc);
    width = SwapDesc.BufferDesc.Width;
    height = SwapDesc.BufferDesc.Height;

    // Create render target view
    CComPtr<ID3D11Texture2D> spBackBuffer;
    hr = m_pSwapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), (LPVOID*)&spBackBuffer );
    D3D11_TEXTURE2D_DESC BackBufferDesc = {};
    spBackBuffer->GetDesc(&BackBufferDesc);

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

    AMFRect client;

    if (m_bFullScreen)
    {
        client.left = 0;
        client.top = 0;
        client.right = width;
        client.bottom = height;
    }
    else
    {
        client = GetClientRect();
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
AMF_RESULT VideoPresenterDX11::UpdateVertices(AMFRect *srcRect, AMFSize *srcSize, AMFRect *dstRect, AMFSize *dstSize)
{
    if(*srcRect == m_sourceVertexRect  &&  *dstRect == m_destVertexRect)
    {
        return AMF_OK;   
    }
    m_sourceVertexRect = *srcRect;
    m_destVertexRect = *dstRect;

    FLOAT srcCenterX = (FLOAT)(srcRect->left + srcRect->right) / 2.f;
    FLOAT srcCenterY = (FLOAT)(srcRect->top + srcRect->bottom) / 2.f;

    FLOAT srcWidth = (FLOAT)srcRect->Width() / srcSize->width;
    FLOAT srcHeight = (FLOAT)srcRect->Height() / srcSize->height;

    FLOAT leftSrcTex = srcCenterX / srcRect->Width() - srcWidth / 2;
    FLOAT rightSrcTex = leftSrcTex + srcWidth;
    FLOAT topSrcTex = srcCenterY / srcRect->Height() - srcHeight / 2;
    FLOAT bottomSrcTex = topSrcTex + srcHeight;

    FLOAT leftSrc = (FLOAT)srcRect->left;
    FLOAT rightSrc = (FLOAT)srcRect->right;
    FLOAT topSrc = (FLOAT)srcRect->top;
    FLOAT bottomSrc = (FLOAT)srcRect->bottom;

    SimpleVertex vertices[4];

    srcWidth = (FLOAT)srcRect->Width();
    srcHeight = (FLOAT)srcRect->Height();

    if (m_iOrientation % 2 == 1)
    {
        std::swap(srcWidth, srcHeight);
        std::swap(rightSrcTex, leftSrcTex);
        std::swap(bottomSrcTex, topSrcTex);
    }

    vertices[0].position = XMFLOAT3(leftSrc, bottomSrc, 0.0f);
    vertices[0].texture = XMFLOAT2(leftSrcTex, topSrcTex);

    vertices[1].position = XMFLOAT3(rightSrc, bottomSrc, 0.0f);
    vertices[1].texture = XMFLOAT2(rightSrcTex, topSrcTex);

    vertices[2].position = XMFLOAT3(leftSrc, topSrc, 0.0f);
    vertices[2].texture = XMFLOAT2(leftSrcTex, bottomSrcTex);

    // Second triangle.
    vertices[3].position = XMFLOAT3(rightSrc, topSrc, 0.0f);
    vertices[3].texture = XMFLOAT2(rightSrcTex, bottomSrcTex);

    FLOAT clientCenterX = (FLOAT)(m_rectClient.left + m_rectClient.right) / 2.f;
    FLOAT clientCenterY = (FLOAT)(m_rectClient.top + m_rectClient.bottom) / 2.f;


    FLOAT scaleX = (FLOAT)m_rectClient.Width() / (FLOAT)srcWidth;
    FLOAT scaleY = (FLOAT)m_rectClient.Height() / (FLOAT)srcHeight;
    FLOAT scaleMin = AMF_MIN(scaleX, scaleY);

    XMMATRIX srcToClientMatrix = XMMatrixTranslation(-srcCenterX, -srcCenterY, 0.0f)
        * XMMatrixRotationZ(::XMConvertToRadians(90.0f * m_iOrientation))
        * XMMatrixScaling(scaleMin, scaleMin, 1.0f)
        * XMMatrixTranslation(clientCenterX, clientCenterY, 0.0f);
    XMStoreFloat4x4(&m_mSrcToClientMatrix, srcToClientMatrix);

    XMMATRIX screenMatrix = XMMatrixScaling(1.0f / dstSize->width, 1.0f / dstSize->height, 1.0f)
        * XMMatrixTranslation(-0.5f, -0.5f, 1.0f)
        * XMMatrixScaling(2.0f, 2.0f, 1.0f);

    XMStoreFloat4x4(&m_mSrcToScreenMatrix, srcToClientMatrix * screenMatrix);

    CComPtr<ID3D11DeviceContext> spContext;
    m_pDevice->GetImmediateContext( &spContext );
    spContext->UpdateSubresource( m_pVertexBuffer, 0, NULL, vertices, 0, 0 );
    spContext->UpdateSubresource(m_pCBChangesOnResize, 0, NULL, &m_mSrcToScreenMatrix, 0, 0);
    
    return AMF_OK;
}

AMFPoint VideoPresenterDX11::MapClientToSource(const AMFPoint& point)
{
    if (m_pProcessor == nullptr)
    {
        XMVECTOR pt = XMVectorSet((FLOAT)point.x, (FLOAT)point.y, 0.0f, 1.0f);
        XMMATRIX inverse = XMMatrixInverse(nullptr, XMLoadFloat4x4(&m_mSrcToClientMatrix));
        pt = XMVector4Transform(pt, inverse);

        return AMFConstructPoint((amf_int32)XMVectorGetX(pt), (amf_int32)XMVectorGetY(pt));
    }
    return point;
}

AMFPoint VideoPresenterDX11::MapSourceToClient(const AMFPoint& point)
{
    if (m_pProcessor == nullptr)
    {
        XMVECTOR pt = XMVectorSet((FLOAT)point.x, (FLOAT)point.y, 0.0f, 1.0f);
        pt = XMVector4Transform(pt, XMLoadFloat4x4(&m_mSrcToClientMatrix));

        return AMFConstructPoint((amf_int32)XMVectorGetX(pt), (amf_int32)XMVectorGetY(pt));
    }
    return point;
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
    if(format != amf::AMF_SURFACE_NV12 && format != amf::AMF_SURFACE_BGRA && format != amf::AMF_SURFACE_RGBA  && format != amf::AMF_SURFACE_RGBA_F16 && format != amf::AMF_SURFACE_R10G10B10A2)
    {
        return AMF_NOT_SUPPORTED;
    }
    // test for MPO
    if (format == amf::AMF_SURFACE_NV12)
    {
        CComPtr<ID3D11Device>    pDevice(static_cast<ID3D11Device*>(m_pContext->GetDX11Device()));
        CComQIPtr<IDXGIDevice> spDXGIDevice(pDevice);
        if (spDXGIDevice == nullptr)
        {
            return AMF_NOT_SUPPORTED;
        }

        CComPtr<IDXGIAdapter> spDXGIAdapter;
        HRESULT hr = spDXGIDevice->GetParent(__uuidof(IDXGIAdapter), (void **)&spDXGIAdapter);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"GetParent(__uuidof(IDXGIAdapter)) failed");

        CComPtr<IDXGIOutput> pCurrentOutput;
        hr = spDXGIAdapter->EnumOutputs(0, &pCurrentOutput);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"EnumOutputs(0) failed");

        CComQIPtr<IDXGIOutput3>  spOutput3(pCurrentOutput);
        UINT flagsOverlay = 0;
        hr = spOutput3->CheckOverlaySupport(DXGI_FORMAT_NV12, pDevice, &flagsOverlay);
        if (flagsOverlay == 0)
        {
            return AMF_NOT_SUPPORTED;
        }
    }
    m_eInputFormat = format;
    return AMF_OK;
}
AMFRate		VideoPresenterDX11::GetDisplayRefreshRate()
{
	AMFRate rate = VideoPresenter::GetDisplayRefreshRate();
	CComPtr<ID3D11Device>    pDevice(static_cast<ID3D11Device*>(m_pContext->GetDX11Device()));
	CComQIPtr<IDXGIDevice> spDXGIDevice(pDevice);
	if (spDXGIDevice == nullptr)
	{
		return rate;
	}

	CComPtr<IDXGIAdapter> spDXGIAdapter;
	HRESULT hr = spDXGIDevice->GetParent(__uuidof(IDXGIAdapter), (void **)&spDXGIAdapter);
	ASSERT_RETURN_IF_HR_FAILED(hr, rate, L"GetParent(__uuidof(IDXGIAdapter)) failed");

	CComPtr<IDXGIOutput> pCurrentOutput;
	hr = spDXGIAdapter->EnumOutputs(0, &pCurrentOutput);
	ASSERT_RETURN_IF_HR_FAILED(hr, rate, L"EnumOutputs(0) failed");

	DXGI_OUTPUT_DESC outputDesc = {};
	pCurrentOutput->GetDesc(&outputDesc);

	MONITORINFOEX monitorInfo = {};
	monitorInfo.cbSize = sizeof(MONITORINFOEX);
	GetMonitorInfo(outputDesc.Monitor, &monitorInfo);

	DEVMODE devMode = {};
	devMode.dmSize = sizeof(DEVMODE);
	devMode.dmDriverExtra = 0;
	EnumDisplaySettings(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &devMode);

	if (1 == devMode.dmDisplayFrequency || 0 == devMode.dmDisplayFrequency)
	{
		return rate;
	}
	rate.num = (amf_int32)devMode.dmDisplayFrequency;
	rate.den = 1;
	return rate;
}

DXGI_FORMAT VideoPresenterDX11::GetDXGIFormat() const
{ 
    DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM;
    switch(GetInputFormat())
    {
    case amf::AMF_SURFACE_NV12:
        format = DXGI_FORMAT_NV12;
        break;
    case amf::AMF_SURFACE_BGRA:
        format = DXGI_FORMAT_B8G8R8A8_UNORM;
        break;
    case amf::AMF_SURFACE_RGBA:
        format = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    case amf::AMF_SURFACE_RGBA_F16:
        format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        break;
    case amf::AMF_SURFACE_R10G10B10A2:
        format = DXGI_FORMAT_R10G10B10A2_UNORM;
        break;
    }
    return format;
}

AMF_RESULT              VideoPresenterDX11::Flush()
{
    m_uiAvailableBackBuffer = 0;
    return BackBufferPresenter::Flush();
}


void        VideoPresenterDX11::UpdateProcessor()
{
    VideoPresenter::UpdateProcessor();
#if defined(NTDDI_WIN10_RS2)
    if (m_pProcessor != NULL && m_pCurrentOutput != NULL)
    {
#if USE_COLOR_TWITCH_IN_DISPLAY
        ATL::CComQIPtr<IDXGISwapChain3> pSwapChain3(m_pSwapChain);
        if (pSwapChain3 != NULL)
        {
            UINT supported[20];
            for (int i = 0; i < 20; i++)
            {
                pSwapChain3->CheckColorSpaceSupport((DXGI_COLOR_SPACE_TYPE)i, &supported[i]);
            }
            if ((GetInputFormat() == amf::AMF_SURFACE_RGBA_F16 || GetInputFormat() == amf::AMF_SURFACE_R10G10B10A2) &&
                (supported[DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020] & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
            {

//                DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
//                pSwapChain3->SetColorSpace1(colorSpace);
//                m_pProcessor->SetProperty(AMF_VIDEO_CONVERTER_USE_DECODER_HDR_METADATA, false);
            }
        }
#endif
    // check and set color space and HDR support

    ATL::CComQIPtr<IDXGIOutput6> spDXGIOutput6(m_pCurrentOutput);
    if (spDXGIOutput6 != NULL)
    {
        DXGI_OUTPUT_DESC1 desc = {};
        spDXGIOutput6->GetDesc1(&desc);
        if (desc.MaxLuminance != 0)
        {
            amf::AMFBufferPtr pBuffer;
            m_pContext->AllocBuffer(amf::AMF_MEMORY_HOST, sizeof(AMFHDRMetadata), &pBuffer);
            AMFHDRMetadata* pHDRData = (AMFHDRMetadata*)pBuffer->GetNative();

            pHDRData->redPrimary[0] = amf_uint16(desc.RedPrimary[0] * 50000.f);
            pHDRData->redPrimary[1] = amf_uint16(desc.RedPrimary[1] * 50000.f);

            pHDRData->greenPrimary[0] = amf_uint16(desc.GreenPrimary[0] * 50000.f);
            pHDRData->greenPrimary[1] = amf_uint16(desc.GreenPrimary[1] * 50000.f);

            pHDRData->bluePrimary[0] = amf_uint16(desc.BluePrimary[0] * 50000.f);
            pHDRData->bluePrimary[1] = amf_uint16(desc.BluePrimary[1] * 50000.f);

            pHDRData->whitePoint[0] = amf_uint16(desc.WhitePoint[0] * 50000.f);
            pHDRData->whitePoint[1] = amf_uint16(desc.WhitePoint[1] * 50000.f);

            pHDRData->maxMasteringLuminance = amf_uint32(desc.MaxLuminance * 10000.f);
            pHDRData->minMasteringLuminance = amf_uint32(desc.MinLuminance * 10000.f);
            pHDRData->maxContentLightLevel = 0;
            pHDRData->maxFrameAverageLightLevel = amf_uint32(desc.MaxFullFrameLuminance* 10000.f);


            m_pProcessor->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_HDR_METADATA, pBuffer);
        }
        AMF_COLOR_TRANSFER_CHARACTERISTIC_ENUM colorTransfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_BT709;
        AMF_COLOR_PRIMARIES_ENUM primaries = AMF_COLOR_PRIMARIES_UNDEFINED;

        switch (desc.ColorSpace)
        {
        case  DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
            colorTransfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_GAMMA22;
            primaries = AMF_COLOR_PRIMARIES_BT709;
            AMFTraceInfo(AMF_FACILITY, L"DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709");
            break;
        case  DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:
            colorTransfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_LINEAR;
            primaries = AMF_COLOR_PRIMARIES_BT709;
            AMFTraceInfo(AMF_FACILITY, L"DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709");
            break;
        case  DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709:
            colorTransfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_GAMMA22;
            primaries = AMF_COLOR_PRIMARIES_BT709;
            AMFTraceInfo(AMF_FACILITY, L"DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709");
            break;
        case  DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020:
            colorTransfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_GAMMA22;
            primaries = AMF_COLOR_PRIMARIES_BT2020;
            AMFTraceInfo(AMF_FACILITY, L"DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020");
            break;

            //    case  DXGI_COLOR_SPACE_RESERVED:
            //    case  DXGI_COLOR_SPACE_YCBCR_FULL_G22_NONE_P709_X601:
            //    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601:
            //    case  DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601:
            //    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709:
            //    case  DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709:
            //    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020:
            //    case  DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020:
        case  DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
            colorTransfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE2084;

            primaries = AMF_COLOR_PRIMARIES_BT2020;

            if (GetInputFormat() == amf::AMF_SURFACE_RGBA_F16)
            {
                primaries = AMF_COLOR_PRIMARIES_CCCS;
            }

            AMFTraceInfo(AMF_FACILITY, L"DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020");
            break;
            //    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020:
        case  DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020:
            colorTransfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE2084;
            primaries = AMF_COLOR_PRIMARIES_BT2020;
            if (GetInputFormat() == amf::AMF_SURFACE_RGBA_F16)
            {
                primaries = AMF_COLOR_PRIMARIES_CCCS;
            }
            AMFTraceInfo(AMF_FACILITY, L"DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020");
            break;
            //    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020:
    //    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020:
        case  DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020:
            colorTransfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_GAMMA22;
            primaries = AMF_COLOR_PRIMARIES_BT2020;
            AMFTraceInfo(AMF_FACILITY, L"DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020");
            break;
            //    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020:
    //    case  DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020:
        case  DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709:
            //        colorTransfer =
            primaries = AMF_COLOR_PRIMARIES_BT709;
            AMFTraceInfo(AMF_FACILITY, L"DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709");
            break;
        case  DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020:
            //        colorTransfer =
            primaries = AMF_COLOR_PRIMARIES_BT2020;
            AMFTraceInfo(AMF_FACILITY, L"DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020");
            break;
            //    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P709:
            //    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P2020:
            //    case  DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_TOPLEFT_P2020:

        }
        if (GetInputFormat() == amf::AMF_SURFACE_RGBA_F16)
        {
            colorTransfer = AMF_COLOR_TRANSFER_CHARACTERISTIC_LINEAR;
        }

        m_pProcessor->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_TRANSFER_CHARACTERISTIC, colorTransfer);
        m_pProcessor->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_COLOR_PRIMARIES, primaries);
    }
#endif
    }
}

void VideoPresenterDX11::CustomDraw()
{
}

AMF_RESULT VideoPresenterDX11::ApplyCSC(amf::AMFSurface* pSurface)
{
#if defined(NTDDI_WIN10_RS2)
#if USE_COLOR_TWITCH_IN_DISPLAY
    if(m_bFirstFrame)
    {
        amf::AMFVariant varBuf;
        if(pSurface->GetProperty(AMF_VIDEO_DECODER_HDR_METADATA, &varBuf) == AMF_OK)
        {
            amf::AMFBufferPtr pBuffer(varBuf.pInterface);
            if(pBuffer != NULL)
            {
                AMFHDRMetadata* pHDRData = (AMFHDRMetadata*)pBuffer->GetNative();
                // check and set color space
                ATL::CComQIPtr<IDXGISwapChain4> pSwapChain4(m_pSwapChain);
                if(pSwapChain4 != NULL)
                {

                    DXGI_HDR_METADATA_HDR10 metadata = {};
                    metadata.WhitePoint[0] = pHDRData->whitePoint[0];
                    metadata.WhitePoint[1] = pHDRData->whitePoint[1];
                    metadata.RedPrimary[0] = pHDRData->redPrimary[0];
                    metadata.RedPrimary[1] = pHDRData->redPrimary[1];
                    metadata.GreenPrimary[0] = pHDRData->greenPrimary[0];
                    metadata.GreenPrimary[1] = pHDRData->greenPrimary[1];
                    metadata.BluePrimary[0] = pHDRData->bluePrimary[0];
                    metadata.BluePrimary[1] = pHDRData->bluePrimary[1];
                    metadata.MaxMasteringLuminance = pHDRData->maxMasteringLuminance;
                    metadata.MinMasteringLuminance = pHDRData->minMasteringLuminance;
                    metadata.MaxContentLightLevel = pHDRData->maxContentLightLevel;
                    metadata.MaxFrameAverageLightLevel = pHDRData->maxFrameAverageLightLevel;

                    HRESULT hr = pSwapChain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(metadata), &metadata);
                    int a = 1;
                }
                m_bFirstFrame = false;
            }
        }
    }
#endif
#endif
    return AMF_OK;
}
