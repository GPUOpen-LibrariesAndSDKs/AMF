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

#include "DeviceDX12.h"
#include "CmdLogger.h"
#include <set>
#ifdef _DEBUG
#include <initguid.h>
#include<dxgidebug.h>
#pragma comment(lib, "dxgi.lib")
#endif

typedef     HRESULT(WINAPI *CreateDXGIFactory2_Fun)(UINT Flags, REFIID riid, _COM_Outptr_ void **ppFactory);

DeviceDX12::DeviceDX12()
    :m_adaptersCount(0),
    m_hDXGI_DLL(nullptr),
    m_hDX12_DLL(nullptr)
{
    m_hDXGI_DLL = ::LoadLibraryW(L"Dxgi.dll");
    m_hDX12_DLL = ::LoadLibraryW(L"D3D12.dll");
    
    memset(m_adaptersIndexes, 0, sizeof(m_adaptersIndexes));
}

DeviceDX12::~DeviceDX12()
{
    Terminate();
    if (m_hDXGI_DLL != nullptr)
    {
        FreeLibrary(m_hDXGI_DLL);
    }
    if (m_hDX12_DLL != nullptr)
    {
        FreeLibrary(m_hDX12_DLL);
    }
}

ATL::CComPtr<ID3D12Device>      DeviceDX12::GetDevice()
{
    return m_pD3DDevice;
}

AMF_RESULT DeviceDX12::Init(amf_uint32 adapterID, bool onlyWithOutputs)
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

	UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
	// Enable the debug layer (requires the Graphics Tools "optional feature").
	// NOTE: Enabling the debug layer after device creation will invalidate the active device.
	{
        PFN_D3D12_GET_DEBUG_INTERFACE fun_D3D12GetDebugInterface = (PFN_D3D12_GET_DEBUG_INTERFACE)::GetProcAddress(m_hDX12_DLL, "D3D12GetDebugInterface");

        CHECK_RETURN(fun_D3D12GetDebugInterface != nullptr, AMF_NOT_SUPPORTED, L"D3D12GetDebugInterface() is not available.");

		ATL::CComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(fun_D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();

			// Enable additional debug layers.
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

    CreateDXGIFactory2_Fun fun_CreateDXGIFactory2 = (CreateDXGIFactory2_Fun)::GetProcAddress(m_hDXGI_DLL, "CreateDXGIFactory2");

    CHECK_RETURN(fun_CreateDXGIFactory2 != nullptr, AMF_NOT_SUPPORTED, L"CreateDXGIFactory2() is not available.");

	ATL::CComPtr<IDXGIFactory4> pFactory;
	hr = fun_CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&pFactory));
	if (FAILED(hr))
	{
		LOG_ERROR("CreateDXGIFactory failed. Error: " << std::hex << hr);
		return AMF_FAIL;
	}

	if (pFactory->EnumAdapters(adapterID, &pAdapter) == DXGI_ERROR_NOT_FOUND)
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
	ATL::CComPtr<ID3D12Device> pD3D12Device;
	ATL::CComPtr<IDXGIAdapter1> adapter;

    PFN_D3D12_CREATE_DEVICE fun_D3D12CreateDevice = (PFN_D3D12_CREATE_DEVICE)GetProcAddress(m_hDX12_DLL, "D3D12CreateDevice");
    CHECK_RETURN(fun_D3D12CreateDevice != nullptr, AMF_NOT_SUPPORTED, L"D3D12CreateDevice() is not available");


	for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(adapterIndex, &adapter); ++adapterIndex)
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			// Don't select the Basic Render Driver adapter.
			// If you want a software adapter, pass in "/warp" on the command line.
			continue;
		}

		// Check to see if the adapter supports Direct3D 12, but don't create the
		// actual device yet.
		if (SUCCEEDED(fun_D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
		{
			break;
		}
	}

	hr = fun_D3D12CreateDevice(
		adapter,
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(&pD3D12Device)
	);
	if (FAILED(hr))
	{
		LOG_ERROR(L"InitDX12() failed to create HW DX12 device ");
		return AMF_FAIL;
	}

    m_pD3DDevice = pD3D12Device;

    return AMF_OK;
}

AMF_RESULT DeviceDX12::Terminate()
{
    m_pD3DDevice.Release();

#ifdef _DEBUG
    CComPtr<IDXGIDebug1> pDebugDevice;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebugDevice))))
    {
        pDebugDevice->ReportLiveObjects(DXGI_DEBUG_DX, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
    }
#endif

    return AMF_OK;
}

void DeviceDX12::EnumerateAdapters(bool onlyWithOutputs)
{
#if !defined(METRO_APP)
    ATL::CComPtr<IDXGIFactory4> pFactory;

    CreateDXGIFactory2_Fun fun_CreateDXGIFactory2 = (CreateDXGIFactory2_Fun)::GetProcAddress(m_hDXGI_DLL, "CreateDXGIFactory2");

    if (fun_CreateDXGIFactory2 == nullptr)
    {
        return;
    }

    UINT dxgiFactoryFlags = 0;

    HRESULT hr = fun_CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&pFactory));
    if(FAILED(hr))
    {
        LOG_ERROR("fun_CreateDXGIFactory2 failed. Error: " << std::hex << hr);
        return;
    }

    LOG_INFO("DX12: List of adapters:");
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

