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
#include "VideoPresenterDX9.h"

VideoPresenterDX9::VideoPresenterDX9(HWND hwnd, amf::AMFContext* pContext) :
    BackBufferPresenter(hwnd, pContext),
    m_uiAvailableBackBuffer(0),
    m_uiBackBufferCount(4)
{

}

VideoPresenterDX9::~VideoPresenterDX9()
{
    Terminate();
}

AMF_RESULT VideoPresenterDX9::Init(amf_int32 width, amf_int32 height)
{
    AMF_RESULT err = AMF_OK;

    VideoPresenter::Init(width, height);

    m_pDevice = static_cast<IDirect3DDevice9Ex*>(m_pContext->GetDX9Device());
    if(m_pDevice == NULL)
    {
        err = AMF_NO_DEVICE;
    }

    if(err == AMF_OK)
    {
        err = CreatePresentationSwapChain();
    }

    return err;
}

AMF_RESULT VideoPresenterDX9::Terminate()
{
    m_pContext = NULL;
    m_hwnd = NULL;
    
    m_pDevice = NULL;
    m_pSwapChain = NULL;
    return VideoPresenter::Terminate();
}

AMF_RESULT VideoPresenterDX9::CreatePresentationSwapChain()
{
    AMF_RESULT err=AMF_OK;
    HRESULT hr=S_OK;

    // release old swap chin and remove observers from old back buffers

    amf::AMFLock lock(&m_sect);
    m_pSwapChain = NULL;
    for(std::vector<amf::AMFSurface*>::iterator it = m_TrackSurfaces.begin(); it != m_TrackSurfaces.end(); it++)
    {
        (*it)->RemoveObserver(this);
    }
    m_TrackSurfaces.clear();
    m_uiAvailableBackBuffer = 0;
    // setup params

    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = 0; // will get from window
    pp.BackBufferHeight = 0; // will get from window

    pp.BackBufferCount=m_uiBackBufferCount; //4 buffers to flip - 4 buffers get the best performance
    pp.Windowed = TRUE;
//    pp.SwapEffect = D3DSWAPEFFECT_FLIP;
    pp.SwapEffect = D3DSWAPEFFECT_FLIPEX;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.hDeviceWindow = (HWND)m_hwnd;
    pp.Flags = D3DPRESENTFLAG_VIDEO;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    
    
    D3DDEVICE_CREATION_PARAMETERS params;
    hr = m_pDevice->GetCreationParameters(&params);
    
    if (FAILED(hr)) 
    {
        return AMF_DIRECTX_FAILED;
    }

    IDirect3DSwapChain9Ptr   pSwapChain;

    hr = m_pDevice->CreateAdditionalSwapChain(&pp, &pSwapChain);
    if (FAILED(hr)) 
    {
        return AMF_DIRECTX_FAILED;
    }
    m_pSwapChain=pSwapChain;

    m_pDevice->SetGPUThreadPriority(7);

    D3DPRESENT_PARAMETERS presentationParameters;
    if(FAILED(m_pSwapChain->GetPresentParameters(&presentationParameters)))
    {
        return AMF_DIRECTX_FAILED;
    }
    m_rectClient = AMFConstructRect(0, 0, presentationParameters.BackBufferWidth, presentationParameters.BackBufferHeight);
    return err;
}

AMF_RESULT VideoPresenterDX9::Present(amf::AMFSurface* pSurface)
{
    AMF_RESULT err = AMF_OK;

    if(m_pDevice == NULL)
    {
        err = AMF_NO_DEVICE;
    }

    if(pSurface->GetFormat() != GetInputFormat())
    {
        return AMF_INVALID_FORMAT;
    }
    if((err = pSurface->Convert(GetMemoryType()))!= AMF_OK)
    {
        return err;
    }

    RECT tmpRectClient = {0, 0, 500, 500};
    GetClientRect((HWND)m_hwnd, &tmpRectClient);
    AMFRect rectClient = AMFConstructRect(tmpRectClient.left, tmpRectClient.top, tmpRectClient.right, tmpRectClient.bottom);

    D3DPRESENT_PARAMETERS presentationParameters;
    if(FAILED(m_pSwapChain->GetPresentParameters(&presentationParameters)))
    {
        return AMF_DIRECTX_FAILED;
    }

    
    if(rectClient.Width() > 0 &&  rectClient.Height() > 0 && ((presentationParameters.BackBufferHeight != rectClient.bottom - rectClient.top) ||
       (presentationParameters.BackBufferWidth != rectClient.right - rectClient.left)))
    {
        if(m_bRenderToBackBuffer)
        {
            m_bResizeSwapChain = true;
        }
        else
        {
            err = CreatePresentationSwapChain();
            UpdateProcessor();

        }
    }
    if(m_bRenderToBackBuffer && rectClient.Width() > 0 &&  rectClient.Height() > 0 && (
        pSurface->GetPlaneAt(0)->GetWidth() != rectClient.Width() ||
        pSurface->GetPlaneAt(0)->GetHeight() != rectClient.Height())
      )
    {
        if(m_uiAvailableBackBuffer != 0)
        {
            m_uiAvailableBackBuffer--;
        }
        return AMF_OK; //allocator changed size drop surface with oldsize
    }

    if(!m_bRenderToBackBuffer)
    {
        amf::AMFPlane* pPlane = pSurface->GetPlane(amf::AMF_PLANE_PACKED);
        IDirect3DSurface9Ptr pSrcDxSurface = (IDirect3DSurface9*)pPlane->GetNative();
        if(pSrcDxSurface == NULL)
        {
            err = AMF_INVALID_POINTER;
        }

        IDirect3DSurface9Ptr pDestDxSurface;
        if(err == AMF_OK)
        {
            err = SUCCEEDED(m_pSwapChain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pDestDxSurface)) ? AMF_OK : AMF_DIRECTX_FAILED;
        }
        AMFRect srcRect = {pPlane->GetOffsetX(), pPlane->GetOffsetY(), pPlane->GetOffsetX() + pPlane->GetWidth(), pPlane->GetOffsetY() + pPlane->GetHeight()};
        AMFRect outputRect;

        if(err == AMF_OK)
        {
            err = CalcOutputRect(&srcRect, &rectClient, &outputRect);
        }
        if(err == AMF_OK)
        {
            m_pDevice->SetRenderTarget(0, pDestDxSurface);
            //in case of ROI we should specify SrcRect
            err = BitBlt(pSrcDxSurface, &srcRect, pDestDxSurface, &outputRect);
        }
    }
    WaitForPTS(pSurface->GetPts());
    amf::AMFLock lock(&m_sect);
    for(int i=0;i<100;i++)
    {
       HRESULT hr = m_pSwapChain->Present(NULL, NULL, NULL, NULL, 0);

       if(SUCCEEDED (hr))
       {
           break;
       }
       if(hr != D3DERR_WASSTILLDRAWING )
       {
           return AMF_DIRECTX_FAILED;
       }
       amf_sleep(1);
    }
    if(m_bRenderToBackBuffer)
    {
        m_uiAvailableBackBuffer--;
    }
    return err;
}

AMF_RESULT VideoPresenterDX9::BitBlt(IDirect3DSurface9* pSrcSurface, AMFRect* pSrcRect, IDirect3DSurface9* pDstSurface, AMFRect* pDstRect)
{
    RECT srcRect = {pSrcRect->left, pSrcRect->top, pSrcRect->right, pSrcRect->bottom };
    RECT dstRect = {pDstRect->left, pDstRect->top, pDstRect->right, pDstRect->bottom };
    return SUCCEEDED(m_pDevice->StretchRect(pSrcSurface, &srcRect, pDstSurface, &dstRect,D3DTEXF_LINEAR)) ? AMF_OK : AMF_DIRECTX_FAILED;
}

AMF_RESULT AMF_STD_CALL VideoPresenterDX9::AllocSurface(amf::AMF_MEMORY_TYPE type, amf::AMF_SURFACE_FORMAT format,
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
        CreatePresentationSwapChain();
        UpdateProcessor();
        m_bResizeSwapChain  = false;
    }

    amf::AMFLock lock(&m_sect);
    AMF_RESULT res = AMF_OK;
    // Ignore sizes and return back buffer
    IDirect3DSurface9Ptr pDestDxSurface;
    if(FAILED(m_pSwapChain->GetBackBuffer(m_uiAvailableBackBuffer, D3DBACKBUFFER_TYPE_MONO, &pDestDxSurface)))
    {
        return AMF_DIRECTX_FAILED;
    }
/*
    m_pDevice->ColorFill(pDestDxSurface, NULL, D3DCOLOR_RGBA (0xFF, 0, 0, 0));
*/
    m_uiAvailableBackBuffer++;
    m_pContext->CreateSurfaceFromDX9Native((void*)(IDirect3DSurface9*)pDestDxSurface, ppSurface, this);
    m_TrackSurfaces.push_back(*ppSurface);
    return res;
}

void AMF_STD_CALL VideoPresenterDX9::OnSurfaceDataRelease(amf::AMFSurface* pSurface)
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

AMF_RESULT     VideoPresenterDX9::Flush()
{
    m_uiAvailableBackBuffer = 0;
    return BackBufferPresenter::Flush();
}
