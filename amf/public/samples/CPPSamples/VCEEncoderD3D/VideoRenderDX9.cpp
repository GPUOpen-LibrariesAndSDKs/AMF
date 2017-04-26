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

#include "VideoRenderDX9.h"
#include "../common/CmdLogger.h"

struct Vertex
{
  float x, y, z;
  DWORD color;
};
static const int g_NumVerts = 24;
static const int g_NumTriangles = 12;
static const int g_NumInds = 36;


VideoRenderDX9::VideoRenderDX9(amf_int width, amf_int height, bool bInterlaced, amf_int frames, amf::AMFContext* pContext)
    :VideoRender(width, height, bInterlaced, frames, pContext),
    m_fAnimation(0)
{
}

VideoRenderDX9::~VideoRenderDX9()
{
    Terminate();
}

AMF_RESULT VideoRenderDX9::Init(HWND hWnd, bool bFullScreen)
{
    AMF_RESULT res = AMF_OK;
    HRESULT hr = S_OK;
    m_fAnimation = 0;

    if(m_width == 0 || m_height == 0)
    {
        LOG_ERROR(L"Bad width/height: width=" << m_width << L"height=" << m_height);
        return AMF_FAIL;
    }

    m_pD3DDevice = static_cast<IDirect3DDevice9*>(m_pContext->GetDX9Device());
    // secondary swap chain improves performance and points to our window
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));

    pp.BackBufferWidth = m_width;
    pp.BackBufferHeight = m_height;
    pp.Windowed = bFullScreen ? FALSE: TRUE;

    if(m_pContext->GetDX9Device(amf::AMF_DX9_EX)!=NULL && hWnd != ::GetDesktopWindow())
    { // flip mode with 4 buffers for DX9Ex significantly improve performance
        pp.BackBufferCount = 4; 
        pp.SwapEffect = D3DSWAPEFFECT_FLIPEX;
    }
    else
    {
        pp.SwapEffect = D3DSWAPEFFECT_COPY;
    }
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.hDeviceWindow = hWnd;
    pp.Flags = D3DPRESENTFLAG_VIDEO;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    if(bFullScreen)
    {
        hr = m_pD3DDevice->GetSwapChain(0,&m_pSwapChain);
        CHECK_HRESULT_ERROR_RETURN(hr, L"m_pD3DDevice->GetSwapChain() failed");
    }
    else
    {
        hr = m_pD3DDevice->CreateAdditionalSwapChain(&pp, &m_pSwapChain);
        CHECK_HRESULT_ERROR_RETURN(hr, L"m_pD3DDevice->CreateAdditionalSwapChain() failed");
    }
    res = InitScene();
    CHECK_AMF_ERROR_RETURN(res, L"InitScene() failed");

    return AMF_OK;

}
AMF_RESULT    VideoRenderDX9::Terminate()
{
    m_pVB.Release();
    m_pIB.Release();
    m_pSwapChain.Release();
    m_pD3DDevice.Release();
    return AMF_OK;
}



AMF_RESULT VideoRenderDX9::InitScene()
{
    const Vertex pVerts[] = {
    -1.0f, -1.0f, -1.0f, 0xffffffff,
    -1.0f,  1.0f, -1.0f, 0xffffffff,
    1.0f,  1.0f, -1.0f, 0xffffffff,
    1.0f, -1.0f, -1.0f, 0xffffffff,

    1.0f, -1.0f, -1.0f, 0xff00ffff,
    1.0f,  1.0f, -1.0f, 0xff00ffff,
    1.0f,  1.0f,  1.0f, 0xff00ffff,
    1.0f, -1.0f,  1.0f, 0xff00ffff,

    1.0f, -1.0f,  1.0f, 0xffffff00,
    1.0f,  1.0f,  1.0f, 0xffffff00,
    -1.0f,  1.0f,  1.0f, 0xffffff00,
    -1.0f, -1.0f,  1.0f, 0xffffff00,

    -1.0f, -1.0f,  1.0f, 0xffff0000,
    -1.0f,  1.0f,  1.0f, 0xffff0000,
    -1.0f,  1.0f, -1.0f, 0xffff0000,
    -1.0f, -1.0f, -1.0f, 0xffff0000,

    -1.0f,  1.0f, -1.0f, 0xff00ff00,
    -1.0f,  1.0f,  1.0f, 0xff00ff00,
    1.0f,  1.0f,  1.0f, 0xff00ff00,
    1.0f,  1.0f, -1.0f, 0xff00ff00,

    1.0f, -1.0f, -1.0f, 0xff0000ff,
    1.0f, -1.0f,  1.0f, 0xff0000ff,
    -1.0f, -1.0f,  1.0f, 0xff0000ff,
    -1.0f, -1.0f, -1.0f, 0xff0000ff,
    };
  
    const unsigned short pInds[]=
    {
        0,1,3,3,1,2,
        4,5,7,7,5,6,
        8,9,11,11,9,10,
        12,13,15,15,13,14,
        16,17,19,19,17,18,
        20,21,23,23,21,22,
    };

    void * pBuf;

    HRESULT hr = m_pD3DDevice->CreateVertexBuffer( sizeof(Vertex) * g_NumVerts,
        0 , D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_DEFAULT, &m_pVB, 0 );
    CHECK_HRESULT_ERROR_RETURN(hr, L"CreateVertexBuffer() failed");

    hr = m_pVB->Lock( 0, sizeof(Vertex) * g_NumVerts, &pBuf, 0 );
    CHECK_HRESULT_ERROR_RETURN(hr, L"m_pVB->Lock() failed");

    memcpy( pBuf, pVerts, sizeof(Vertex) * g_NumVerts);

    hr = m_pVB->Unlock();
    CHECK_HRESULT_ERROR_RETURN(hr, L"m_pVB->Unlock() failed");


    hr = m_pD3DDevice->CreateIndexBuffer( sizeof(short) * g_NumInds, 
        0, D3DFMT_INDEX16, D3DPOOL_DEFAULT,&m_pIB, 0);
    CHECK_HRESULT_ERROR_RETURN(hr, L"CreateIndexBuffer() failed");

    hr = m_pIB->Lock( 0, sizeof(short) * g_NumInds, &pBuf, 0 );
    CHECK_HRESULT_ERROR_RETURN(hr, L"m_pIB->Lock() failed");

    memcpy( pBuf, pInds, sizeof(short) * g_NumInds);

    hr = m_pIB->Unlock();
    CHECK_HRESULT_ERROR_RETURN(hr, L"m_pIB->Unlock() failed");

    hr = m_pD3DDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    CHECK_HRESULT_ERROR_RETURN(hr, L"SetRenderState() failed");

    D3DMATRIX View = {
        1, 0, 0, 0,
        0, 0, 1, 0,
        0, -1, 0, 0,
        0, 0, 4, 1,
    };
    hr = m_pD3DDevice->SetTransform(D3DTS_VIEW, &View);
    CHECK_HRESULT_ERROR_RETURN(hr, L"SetTransform(D3DTS_VIEW) failed");

    float Zn = 0.5f;
    float Zf = 100.0f;
    float Vw = 1;
    float Vh = 3.0f/4;
    D3DMATRIX Projection = {
        2*Zn/Vw, 0, 0, 0,
        0, 2*Zn/Vh, 0, 0,
        0, 0, Zf/(Zf-Zn), 1,
        0, 0, -Zf/(Zf-Zn)*Zn, 0,
    };
    hr = m_pD3DDevice->SetTransform(D3DTS_PROJECTION, &Projection);
    CHECK_HRESULT_ERROR_RETURN(hr, L"SetTransform(D3DTS_PROJECTION) failed");

    return AMF_OK;
}


AMF_RESULT VideoRenderDX9::RenderScene()
{
    m_fAnimation +=3.141593f/60;

    float cs = (float)cos(m_fAnimation);
    float sn = (float)sin(m_fAnimation);

    float cs2 = (float)cos(m_fAnimation/2);
    float sn2 = (float)sin(m_fAnimation/2);

    D3DMATRIX World = {
        cs*cs2, cs*sn2, sn, 0,
        -sn2, cs2,  0, 0,
        -sn*cs2, -sn*sn2, cs, 0,
        0, 0,  0, 1,
    };

    HRESULT hr = m_pD3DDevice->SetTransform(D3DTS_WORLD, &World);
    CHECK_HRESULT_ERROR_RETURN(hr, L"SetTransform(D3DTS_WORLD) failed");

    hr = m_pD3DDevice->SetFVF( D3DFVF_XYZ | D3DFVF_DIFFUSE );
    CHECK_HRESULT_ERROR_RETURN(hr, L"SetFVF() failed");

    hr = m_pD3DDevice->SetStreamSource( 0, m_pVB, 0, sizeof(Vertex) );
    CHECK_HRESULT_ERROR_RETURN(hr, L"SetStreamSource() failed");

    hr = m_pD3DDevice->SetIndices(m_pIB);
    CHECK_HRESULT_ERROR_RETURN(hr, L"SetIndices() failed");

    hr = m_pD3DDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, g_NumVerts, 0, g_NumTriangles);
    CHECK_HRESULT_ERROR_RETURN(hr, L"DrawIndexedPrimitive() failed");

    return AMF_OK;
}


#define SQUARE_SIZE 50

AMF_RESULT VideoRenderDX9::Render(amf::AMFData** ppData)
{
#if !defined(_WIN64 )
// this is done to get identical results on 32 nad 64 bit builds
    _controlfp(_PC_24, MCW_PC);
#endif

    AMF_RESULT res = AMF_OK;
    HRESULT hr = S_OK;

    amf::AMFSurfacePtr pSurface;
    ATL::CComPtr<IDirect3DSurface9>     pBackBuffer;

    hr = m_pSwapChain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO,  &pBackBuffer);
    CHECK_HRESULT_ERROR_RETURN(hr, L"m_pSwapChain->GetBackBuffer() failed");
    do
    {

        hr = m_pD3DDevice->ColorFill(pBackBuffer,NULL,D3DCOLOR_XRGB( 0, 0, 0 ));
        CHECK_HRESULT_ERROR_RETURN(hr, L"m_pD3DDevice->ColorFill() failed");


        hr = m_pD3DDevice->SetRenderTarget(0, pBackBuffer);
        CHECK_HRESULT_ERROR_RETURN(hr, L"m_pD3DDevice->SetRenderTarget() failed");
   
        hr = m_pD3DDevice->BeginScene();
        CHECK_HRESULT_ERROR_RETURN(hr, L"m_pD3DDevice->BeginScene() failed");


        res= RenderScene();
        CHECK_AMF_ERROR_RETURN(res, L"DX9Base::Init() failed");

        hr = m_pD3DDevice->EndScene();
        CHECK_HRESULT_ERROR_RETURN(hr, L"m_pD3DDevice->EndScene() failed");

        // wrap backbuffer surface into AMFSurface
        amf::AMFSurfacePtr pTmpSurface;
        res = m_pContext->CreateSurfaceFromDX9Native(pBackBuffer.p, &pTmpSurface, NULL);
        CHECK_AMF_ERROR_RETURN(res, L"AMFContext::CreateSurfaceFromDX9Native() failed");

        amf::AMFDataPtr pDuplicated;
        pTmpSurface->Duplicate(pTmpSurface->GetMemoryType(), &pDuplicated);
        *ppData = pDuplicated.Detach();

        hr = m_pSwapChain->Present( NULL, NULL, NULL, NULL, D3DPRESENT_INTERVAL_IMMEDIATE );

        if(hr == D3DERR_DEVICELOST)
        {
            D3DPRESENT_PARAMETERS               presentParameters;
            m_pSwapChain->GetPresentParameters(&presentParameters);
            for(int i=0;i<100;i++)
            {
                hr = m_pD3DDevice->TestCooperativeLevel();
                if(hr == D3DERR_DEVICELOST)
                {
                    amf_sleep(100);
                }
                if(hr == D3DERR_DEVICENOTRESET)
                {
                    break;
                }
            }
            if(hr == D3DERR_DEVICENOTRESET)
            {
                hr = m_pD3DDevice->Reset(&presentParameters);
                CHECK_HRESULT_ERROR_RETURN(hr, L"m_pD3DDevice->Reset() failed");
            }
        }
        else
        {
            CHECK_HRESULT_ERROR_RETURN(hr, L"m_pSwapChain->Present() failed");
//            LOG_INFO(L"m_pSwapChain->Present() succeded");
/*
            MSG msg={0};
            while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            } 
*/
            break;
        }
    }while(true);
    return res;

}
