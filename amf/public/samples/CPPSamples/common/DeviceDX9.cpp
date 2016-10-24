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

#include "DeviceDX9.h"
#include "CmdLogger.h"
#include <set>

#pragma comment(lib, "d3d9.lib")

DeviceDX9::DeviceDX9()
    :m_adaptersCount(0)
{
    memset(m_adaptersIndexes, 0, sizeof(m_adaptersIndexes));
}

DeviceDX9::~DeviceDX9()
{
    Terminate();
}

ATL::CComPtr<IDirect3DDevice9>     DeviceDX9::GetDevice()
{
    return m_pD3DDevice;
}

AMF_RESULT DeviceDX9::Init(bool dx9ex, amf_uint32 adapterID, bool bFullScreen, amf_int32 width, amf_int32 height)
{
    HRESULT hr = S_OK;
    ATL::CComPtr<IDirect3D9Ex> pD3DEx;
    if(dx9ex)
    {
        hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &pD3DEx);
        CHECK_HRESULT_ERROR_RETURN(hr, L"Direct3DCreate9Ex Failed");
        m_pD3D = pD3DEx;
    }
    else
    {
        m_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
        m_pD3D.p->Release(); // fixed leak
    }
    
    EnumerateAdapters();

    CHECK_RETURN(m_adaptersCount > adapterID, AMF_INVALID_ARG, L"Invalid Adapter ID");

    //convert logical id to real index
    adapterID = m_adaptersIndexes[adapterID];
    D3DADAPTER_IDENTIFIER9 adapterIdentifier = {0};
    hr = m_pD3D->GetAdapterIdentifier(adapterID, 0, &adapterIdentifier);
    CHECK_HRESULT_ERROR_RETURN(hr, L"m_pD3D->GetAdapterIdentifier Failed");
    
    std::wstringstream wstrDeviceName; wstrDeviceName << adapterIdentifier.DeviceName;
    m_displayDeviceName = wstrDeviceName.str();
    
    char strDevice[100];
    _snprintf_s(strDevice, 100, "%X", adapterIdentifier.DeviceId);

    LOG_INFO("DX9 : Chosen Device " << adapterID <<": Device ID: " << strDevice << " [" << adapterIdentifier.Description << "]");

    D3DDISPLAYMODE d3ddm;
    hr = m_pD3D->GetAdapterDisplayMode( (UINT)adapterID, &d3ddm );
    CHECK_HRESULT_ERROR_RETURN(hr, L"m_pD3D->GetAdapterDisplayMode Failed");

    D3DPRESENT_PARAMETERS               presentParameters;
    ZeroMemory(&presentParameters, sizeof(presentParameters));

    if(bFullScreen)
    {
        width= d3ddm.Width;
        height= d3ddm.Height;

        presentParameters.BackBufferWidth = width;
        presentParameters.BackBufferHeight = height;
        presentParameters.Windowed = FALSE;
        presentParameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        presentParameters.FullScreen_RefreshRateInHz = d3ddm.RefreshRate;
    }
    else
    {
        presentParameters.BackBufferWidth = 1;
        presentParameters.BackBufferHeight = 1;
        presentParameters.Windowed = TRUE;
        presentParameters.SwapEffect = D3DSWAPEFFECT_COPY;
    }
    presentParameters.BackBufferFormat = D3DFMT_A8R8G8B8;
    presentParameters.hDeviceWindow = GetDesktopWindow();
    presentParameters.Flags = D3DPRESENTFLAG_VIDEO;
    presentParameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    D3DDISPLAYMODEEX dismodeEx;
    dismodeEx.Size= sizeof(dismodeEx);
    dismodeEx.Format = presentParameters.BackBufferFormat;
    dismodeEx.Width = width;
    dismodeEx.Height = height;
    dismodeEx.RefreshRate = d3ddm.RefreshRate;
    dismodeEx.ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
    
    D3DCAPS9    ddCaps;
    ZeroMemory(&ddCaps, sizeof(ddCaps));
    hr = m_pD3D->GetDeviceCaps((UINT)adapterID, D3DDEVTYPE_HAL, &ddCaps);

    CHECK_HRESULT_ERROR_RETURN(hr, L"m_pD3D->GetDeviceCaps Failed");

    DWORD       vp = 0;
    if(ddCaps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT)
    {
        vp = D3DCREATE_HARDWARE_VERTEXPROCESSING;
    }
    else
    {
        vp = D3DCREATE_SOFTWARE_VERTEXPROCESSING;
    }
    if(dx9ex)
    {
        ATL::CComPtr<IDirect3DDevice9Ex>      pD3DDeviceEx;

        hr = pD3DEx->CreateDeviceEx(
            adapterID,
            D3DDEVTYPE_HAL,
            presentParameters.hDeviceWindow,
            vp | D3DCREATE_NOWINDOWCHANGES | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
            &presentParameters, 
            bFullScreen ? &dismodeEx : NULL,
            &pD3DDeviceEx
        );
        CHECK_HRESULT_ERROR_RETURN(hr, L"m_pD3D->CreateDeviceEx() failed");
        m_pD3DDevice = pD3DDeviceEx;
    }
    else
    {
        hr = m_pD3D->CreateDevice(
            adapterID,
            D3DDEVTYPE_HAL,
            presentParameters.hDeviceWindow,
            vp | D3DCREATE_NOWINDOWCHANGES | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
            &presentParameters, 
            &m_pD3DDevice
        );
        CHECK_HRESULT_ERROR_RETURN(hr, L"m_pD3D->CreateDevice() failed");
    }
    return AMF_OK;;
}
AMF_RESULT DeviceDX9::Terminate()
{
    m_pD3DDevice.Release();
    m_pD3D.Release();
    return AMF_OK;
}

AMF_RESULT DeviceDX9::EnumerateAdapters()
{
    LOG_INFO("DX9: List of adapters:");
    UINT count=0;
    m_adaptersCount = 0;
    ATL::CComPtr<IDirect3D9Ex> pD3DEx;
    {
        HRESULT hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &pD3DEx);
        CHECK_HRESULT_ERROR_RETURN(hr, L"Direct3DCreate9Ex Failed");
    }
    std::list<LUID> enumeratedAdapterLUIDs;
    while(true)
    {
        D3DDISPLAYMODE displayMode;
        HRESULT hr = pD3DEx->EnumAdapterModes(count, D3DFMT_X8R8G8B8, 0, &displayMode);
        
        if(hr != D3D_OK && hr != D3DERR_NOTAVAILABLE)
        {
            break;
        }
        D3DADAPTER_IDENTIFIER9 adapterIdentifier = {0};
        pD3DEx->GetAdapterIdentifier(count, 0, &adapterIdentifier);

        if(adapterIdentifier.VendorId != 0x1002)
        {
            count++;
            continue;
        }

        LUID adapterLuid;
        pD3DEx->GetAdapterLUID(count, &adapterLuid);
        bool enumerated = false;
        for(std::list<LUID>::iterator it = enumeratedAdapterLUIDs.begin(); it != enumeratedAdapterLUIDs.end(); it++)
        {
            if(adapterLuid.HighPart == it->HighPart && adapterLuid.LowPart == it->LowPart)
            {
                enumerated = true;
                break;
            }
        }
        if(enumerated)
        {
            count++;
            continue;
        }

        enumeratedAdapterLUIDs.push_back(adapterLuid);

        char strDevice[100];
        _snprintf_s(strDevice, 100, "%X", adapterIdentifier.DeviceId);

        LOG_INFO("          " << m_adaptersCount << ": Device ID: " << strDevice << " [" << adapterIdentifier.Description << "]");
        m_adaptersIndexes[m_adaptersCount] = count;
        m_adaptersCount++;
        count++;
    }
    return AMF_OK;
}
