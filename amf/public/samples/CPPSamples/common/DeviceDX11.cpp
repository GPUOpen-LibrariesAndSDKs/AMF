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

#include "DeviceDX11.h"
#include "CmdLogger.h"
#include <set>

#pragma comment(lib, "d3d11.lib")

DeviceDX11::DeviceDX11()
    :m_adaptersCount(0)
{
    memset(m_adaptersIndexes, 0, sizeof(m_adaptersIndexes));
}

DeviceDX11::~DeviceDX11()
{
    Terminate();
}

ATL::CComPtr<ID3D11Device>      DeviceDX11::GetDevice()
{
    return m_pD3DDevice;
}

AMF_RESULT DeviceDX11::Init(amf_uint32 adapterID, bool onlyWithOutputs)
{
    HRESULT hr = S_OK;
    AMF_RESULT err = AMF_OK;
    // find adapter
    ATL::CComPtr<IDXGIAdapter> pAdapter;

#if !defined(METRO_APP)
    EnumerateAdapters(onlyWithOutputs);
    CHECK_RETURN(m_adaptersCount > adapterID, AMF_INVALID_ARG, L"Invalid Adapter ID");

    //convert logical id to real index
    adapterID = m_adaptersIndexes[adapterID];

    ATL::CComPtr<IDXGIFactory1> pFactory;
    hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&pFactory);
    if(FAILED(hr))
    {
        LOG_ERROR("CreateDXGIFactory failed. Error: " << std::hex << hr);
        return AMF_FAIL;
    }

    if(pFactory->EnumAdapters(adapterID, &pAdapter) == DXGI_ERROR_NOT_FOUND)
    {
        LOG_INFO("AdapterID = " << adapterID << " not found.");
        return AMF_FAIL;
    }

    DXGI_ADAPTER_DESC desc;
    pAdapter->GetDesc(&desc);

    char strDevice[100];
    _snprintf_s(strDevice, 100, "%X", desc.DeviceId);

    LOG_INFO("DX11 : Chosen Device " << adapterID <<": Device ID: " << strDevice << " [" << desc.Description << "]");

    ATL::CComPtr<IDXGIOutput> pOutput;
    if(SUCCEEDED(pAdapter->EnumOutputs(0, &pOutput)))
    {
        DXGI_OUTPUT_DESC outputDesc;
        pOutput->GetDesc(&outputDesc);
        m_displayDeviceName = outputDesc.DeviceName;
    }
#endif//#if !defined(METRO_APP)

/////
    ATL::CComPtr<ID3D11Device> pD3D11Device;
    ATL::CComPtr<ID3D11DeviceContext>  pD3D11Context;
    UINT createDeviceFlags = 0;

#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HMONITOR hMonitor = NULL;
    DWORD vp = 0;

    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };
    D3D_FEATURE_LEVEL featureLevel;

    D3D_DRIVER_TYPE eDriverType = pAdapter != NULL ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;
    hr = D3D11CreateDevice(pAdapter, eDriverType, NULL, createDeviceFlags, featureLevels, _countof(featureLevels),
                D3D11_SDK_VERSION, &pD3D11Device, &featureLevel, &pD3D11Context);
#ifdef _DEBUG
    if(FAILED(hr))
    {
        createDeviceFlags &= (~D3D11_CREATE_DEVICE_DEBUG);
        hr = D3D11CreateDevice(pAdapter, eDriverType, NULL, createDeviceFlags, featureLevels, _countof(featureLevels),
                D3D11_SDK_VERSION, &pD3D11Device, &featureLevel, &pD3D11Context);
    }
#endif
    if(FAILED(hr))
    {
        LOG_ERROR(L"InitDX11() failed to create HW DX11.1 device ");
        hr = D3D11CreateDevice(pAdapter, eDriverType, NULL, createDeviceFlags, featureLevels + 1, _countof(featureLevels) - 1,
                    D3D11_SDK_VERSION, &pD3D11Device, &featureLevel, &pD3D11Context);
    }
    else
    {
        LOG_INFO(L"InitDX11() created HW DX11.1 device");
    }
    if(FAILED(hr))
    {
        LOG_ERROR(L"InitDX11() failed to create HW DX11 device ");
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_SOFTWARE, NULL, createDeviceFlags, featureLevels, _countof(featureLevels),
                    D3D11_SDK_VERSION, &pD3D11Device, &featureLevel, &pD3D11Context);
    }
    else
    {
        LOG_INFO(L"InitDX11() created HW DX11 device");
    }

    if(FAILED(hr))
    {
        LOG_ERROR(L"InitDX11() failed to create SW DX11.1 device ");
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_SOFTWARE, NULL, createDeviceFlags, featureLevels + 1, _countof(featureLevels) - 1,
                    D3D11_SDK_VERSION, &pD3D11Device, &featureLevel, &pD3D11Context);
    }
    if(FAILED(hr))
    {
        LOG_ERROR(L"InitDX11() failed to create SW DX11 device ");
    }

    ATL::CComPtr<ID3D10Multithread> pMultithread = NULL;
    hr = pD3D11Device->QueryInterface(__uuidof(ID3D10Multithread), reinterpret_cast<void**>(&pMultithread));
    if(pMultithread)
    {
//        amf_bool isSafe = pMultithread->GetMultithreadProtected() ? true : false;
        pMultithread->SetMultithreadProtected(true);
    }

    m_pD3DDevice = pD3D11Device;

    return AMF_OK;
}

AMF_RESULT DeviceDX11::Terminate()
{
    m_pD3DDevice.Release();
    return AMF_OK;
}

void DeviceDX11::EnumerateAdapters(bool onlyWithOutputs)
{
#if !defined(METRO_APP)
    ATL::CComPtr<IDXGIFactory> pFactory;
    HRESULT hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void **)&pFactory);
    if(FAILED(hr))
    {
        LOG_ERROR("CreateDXGIFactory failed. Error: " << std::hex << hr);
        return;
    }

    LOG_INFO("DX11: List of adapters:");
    UINT count = 0;
    m_adaptersCount = 0;
    while(true)
    {
        ATL::CComPtr<IDXGIAdapter> pAdapter;
        if(pFactory->EnumAdapters(count, &pAdapter) == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }

        DXGI_ADAPTER_DESC desc;
        pAdapter->GetDesc(&desc);

        if(desc.VendorId != 0x1002)
        {
            count++;
            continue;
        }
        ATL::CComPtr<IDXGIOutput> pOutput;
        if(onlyWithOutputs && pAdapter->EnumOutputs(0, &pOutput) == DXGI_ERROR_NOT_FOUND)
        {
            count++;
            continue;
        }
        char strDevice[100];
        _snprintf_s(strDevice, 100, "%X", desc.DeviceId);

        LOG_INFO("          " << m_adaptersCount << ": Device ID: " << strDevice << " [" << desc.Description << "]");
        m_adaptersIndexes[m_adaptersCount] = count;
        m_adaptersCount++;
        count++;
    }
#endif//#if !defined(METRO_APP)

}

