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


#include "SwapChainDX9.h"
#include "public/common/TraceAdapter.h"

using namespace amf;

#define AMF_FACILITY L"SwapChainDX9"

#define PRESENT_TIMEOUT_COUNTER 100

SwapChainDX9::SwapChainDX9(AMFContext* pContext) :
    SwapChain(pContext),
    m_d3dFormat(D3DFMT_UNKNOWN),
    m_nextBufferNum(0),
    m_currentOutput(-1)
{
    for (amf_uint i = 0; i < amf_countof(m_pBackBuffers); ++i)
    {
        m_pBackBuffers[i] = std::make_unique<BackBufferDX9>();
    }
    SetFormat(AMF_SURFACE_UNKNOWN); // Set default format
}

SwapChainDX9::~SwapChainDX9()
{
    Terminate();
}

AMF_RESULT SwapChainDX9::Init(amf_handle hwnd, amf_handle hDisplay, AMFSurface* /*pSurface*/, amf_int32 width, amf_int32 height, AMF_SURFACE_FORMAT format, amf_bool /*fullscreen*/, amf_bool hdr, amf_bool stereo)
{
    AMF_RETURN_IF_FALSE(hwnd != nullptr, AMF_INVALID_ARG, L"Init() - hwnd is NULL");

    m_hwnd = hwnd;
    m_hDisplay = hDisplay;

    m_pDevice = static_cast<IDirect3DDevice9Ex*>(m_pContext->GetDX9Device());
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NO_DEVICE, L"Init() - Failed to get DX9 device");

    IDirect3D9Ptr pD3D9;
    HRESULT hr = m_pDevice->GetDirect3D(&pD3D9);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"Init() - GetDirect3D() failed");
    m_pD3D9 = pD3D9;

    AMF_RESULT res = CreateSwapChain(width, height, format, stereo);
    AMF_RETURN_IF_FAILED(res, L"Init() - CreatSwapChain() failed");

    res = EnableHDR(hdr);
    AMF_RETURN_IF_FAILED(res, L"Init() - EnableHDR() failed");

    return AMF_OK;
}

AMF_RESULT SwapChainDX9::Terminate()
{
    m_pDevice = nullptr;
    m_pD3D9 = nullptr;

    m_outputs.clear();
    m_currentOutput = -1;

    TerminateSwapChain();
    return SwapChain::Terminate();
}

AMF_RESULT SwapChainDX9::CreateSwapChain(amf_int32 width, amf_int32 height, AMF_SURFACE_FORMAT format, amf_bool /*stereo*/)
{
    AMF_RETURN_IF_FALSE(m_pDevice != nullptr, AMF_NOT_INITIALIZED, L"CreateSwapChain() - m_pDevice is not initialized");
    AMF_RETURN_IF_FALSE(m_hwnd != nullptr, AMF_NOT_INITIALIZED, L"CreateSwapChain() - m_hwnd is not initialized");
    AMF_RETURN_IF_FALSE(m_pSwapChain == nullptr, AMF_ALREADY_INITIALIZED, L"CreateSwapChain() - m_pSwapChain is already initialized");

    AMF_RESULT res = SetFormat(format);
    AMF_RETURN_IF_FAILED(res, L"CreateSwapChain() - SetFormat() failed");

    m_stereoEnabled = false;

    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = width;
    pp.BackBufferHeight = height;
    pp.BackBufferCount = GetBackBufferCount();
    pp.Windowed = TRUE;
    //    pp.SwapEffect = D3DSWAPEFFECT_FLIP;
    pp.SwapEffect = D3DSWAPEFFECT_FLIPEX;
    pp.BackBufferFormat = GetD3DFormat();
    pp.hDeviceWindow = (HWND)m_hwnd;
    pp.Flags = D3DPRESENTFLAG_VIDEO;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    pp.FullScreen_RefreshRateInHz = 0; // Must be 0 in windowed mode

    D3DDEVICE_CREATION_PARAMETERS creationParams;
    HRESULT hr = m_pDevice->GetCreationParameters(&creationParams);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateSwapChain() - GetCreationParameters() failed");

    IDirect3DSwapChain9Ptr pSwapChain;
    hr = m_pDevice->CreateAdditionalSwapChain(&pp, &pSwapChain);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateSwapChain() - CreateAdditionalSwapChain() failed");

    m_pSwapChain = pSwapChain;

    hr = m_pDevice->SetGPUThreadPriority(7);
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateSwapChain() - SetGPUThreadPriority() failed");

    m_size = AMFConstructSize(width, height);

    for (amf_uint i = 0; i < GetBackBufferCount(); ++i)
    {
        hr = m_pSwapChain->GetBackBuffer(i, D3DBACKBUFFER_TYPE_MONO, &((BackBuffer*)m_pBackBuffers[i].get())->pBuffer);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateSwapChain() - GetBackBuffer() failed to get buffer %u", i);
    }

    return  AMF_OK;
}

AMF_RESULT SwapChainDX9::TerminateSwapChain()
{
    if (m_pSwapChain != nullptr)
    {
        SetExclusiveFullscreenState(false);
    }

    // release old swap chin
    m_pSwapChain = nullptr;

    for (amf_uint i = 0; i < amf_countof(m_pBackBuffers); ++i)
    {
        *((BackBuffer*)m_pBackBuffers[i].get()) = {};
    }
    m_nextBufferNum = 0;
    m_droppedBufferNums.clear();

    return AMF_OK;
}

AMF_RESULT SwapChainDX9::Resize(amf_int32 width, amf_int32 height, amf_bool /*fullscreen*/, AMF_SURFACE_FORMAT format)
{
    if (format != AMF_SURFACE_UNKNOWN)
    {
        AMF_RESULT res = SetFormat(format);
        AMF_RETURN_IF_FAILED(res, L"Resize() - SetFormat() failed");
    }

    AMF_RESULT res = TerminateSwapChain();
    AMF_RETURN_IF_FAILED(res, L"Resize() - TerminateSwapChain() failed");

    res = CreateSwapChain(width, height, format, m_stereoEnabled);
    AMF_RETURN_IF_FAILED(res, L"Resize() - CreatePresentationSwapChain() failed");
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"Resize() - DX12 SwapChain is not initialized");

    return AMF_OK;
}

AMFSize SwapChainDX9::GetSize()
{
    D3DPRESENT_PARAMETERS params = {};
    m_pSwapChain->GetPresentParameters(&params);
    m_size = AMFConstructSize(params.BackBufferWidth, params.BackBufferHeight);
    return m_size;
}

AMF_RESULT SwapChainDX9::Present(amf_bool waitForVSync)
{
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"Present() - SwapChain is not initialized");
    
    // Get rid of all dropped frames until we get to the first non-dropped frame
    // Using FORCEIMMEDIATE should allow us to skip over them easily
    amf_uint offset = 0;
    for (; offset < GetBackBufferCount(); ++offset)
    {
        if (m_droppedBufferNums.empty() || m_droppedBufferNums.front() - offset > 0)
        {
            break;
        }
        
        Present((DWORD)D3DPRESENT_FORCEIMMEDIATE);
        m_droppedBufferNums.pop_front();
    }

    DWORD presentFlags = D3DPRESENT_FORCEIMMEDIATE;
    if (waitForVSync == false)
    {
        presentFlags |= D3DPRESENT_DONOTWAIT;
    }

    AMF_RESULT res = Present(presentFlags);
    AMF_RETURN_IF_FAILED(res, L"Present() failed");
    offset++;

    for (amf_uint& index : m_droppedBufferNums)
    {
        index -= offset;
    }

    return AMF_OK;
}

AMF_RESULT SwapChainDX9::Present(DWORD presentFlags)
{
    amf_bool success = false;
    for (amf_int i = 0; i < PRESENT_TIMEOUT_COUNTER; i++)
    {
        HRESULT hr = m_pSwapChain->Present(nullptr, nullptr, nullptr, nullptr, presentFlags);
        if (SUCCEEDED(hr))
        {
            success = true;
            break;
        }

        AMF_RETURN_IF_FALSE(hr == D3DERR_WASSTILLDRAWING, AMF_DIRECTX_FAILED, L"Present() - m_pSwapChain->Present() failed");
        amf_sleep(1);
    }

    if (success == false)
    {
        AMFTraceDebug(AMF_FACILITY, L"Present() - Timedout waiting for GPU");
        // Todo: return an appropiate error code and ensure applications and presenters handle it appropiately
    }

    if (m_nextBufferNum > 0)
    {
        m_nextBufferNum--;
    }

    return AMF_OK;
}

AMF_RESULT SwapChainDX9::AcquireNextBackBufferIndex(amf_uint& index)
{
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"AcquireNextBackBuffer() - m_pSwapChain is not initialized");
    AMF_RETURN_IF_FALSE(index < GetBackBufferCount(), AMF_INVALID_ARG, L"AcquireNextBackBuffer() - index (%u) out of range, must be < %u", index, GetBackBufferCount());

    if (m_nextBufferNum >= GetBackBufferCount())
    {
        return AMF_NEED_MORE_INPUT; // Too many backbuffers acquired
    }

    index = m_nextBufferNum++;
    return AMF_OK;
}

AMF_RESULT SwapChainDX9::DropLastBackBuffer()
{
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"DropLastBackBuffer() - m_pSwapChain is not initialized");

    return DropBackBufferIndex(m_nextBufferNum - 1);
}

AMF_RESULT SwapChainDX9::DropBackBufferIndex(amf_uint index)
{
    AMF_RETURN_IF_FALSE(m_pSwapChain != nullptr, AMF_NOT_INITIALIZED, L"DropBackBuffer() - m_pSwapChain is not initialized");

    if (index >= m_nextBufferNum)
    {
        return AMF_OK;
    }

    // If dropping the last acquired buffer, we can just 
    // decrement the buffer num and pass it for next frame
    m_droppedBufferNums.push_back(index);
    m_droppedBufferNums.sort();
    m_droppedBufferNums.unique();

    while (m_droppedBufferNums.empty() == false && m_droppedBufferNums.back() == m_nextBufferNum - 1)
    {
        m_droppedBufferNums.pop_back();
        m_nextBufferNum--;
    }

    return AMF_OK;
}

amf_bool SwapChainDX9::FormatSupported(AMF_SURFACE_FORMAT format)
{
    return GetSupportedD3DFormat(format) != D3DFMT_UNKNOWN;
}

AMF_RESULT SwapChainDX9::SetFormat(AMF_SURFACE_FORMAT format)
{
    D3DFORMAT d3dFormat = GetSupportedD3DFormat(format);
    AMF_RETURN_IF_FALSE(d3dFormat != D3DFMT_UNKNOWN, AMF_NOT_SUPPORTED, L"SetFormat() - Format (%s) not supported", AMFSurfaceGetFormatName(format));

    m_format = format == AMF_SURFACE_UNKNOWN ? AMF_SURFACE_BGRA : format;
    m_d3dFormat = d3dFormat;
    return AMF_OK;
}

D3DFORMAT SwapChainDX9::GetSupportedD3DFormat(AMF_SURFACE_FORMAT format) const
{
    switch (format)
    {
    case AMF_SURFACE_UNKNOWN:   return D3DFMT_A8R8G8B8;
    case AMF_SURFACE_BGRA:      return D3DFMT_A8R8G8B8;
    }
    return D3DFMT_UNKNOWN;
}

AMF_RESULT SwapChainDX9::UpdateOutputs()
{
    AMF_RETURN_IF_FALSE(m_pD3D9 != nullptr, AMF_NOT_INITIALIZED, L"UpdateOutputs() - m_pD3D9 is not initialized");

    UINT adapterCount = m_pD3D9->GetAdapterCount();
    D3DFORMAT formats[] = { D3DFMT_X8R8G8B8 };
    // Get all outputs from adapters
    for (UINT adapter = 0; adapter < adapterCount; ++adapter)
    {
        // Todo: should we skip over non-amd adapters for presenters?

        //D3DADAPTER_IDENTIFIER9 adapterIdentifier;
        //m_pD3D9->GetAdapterIdentifier(adapter, 0, &adapterIdentifier);
        //if (adapterIdentifier.VendorId != 0x1002)
        //{
        //    continue;
        //}

        amf_uint64 biggestArea = 0; // Use biggest area to determine "fullscreen mode"
        for (D3DFORMAT format : formats)
        {
            HMONITOR hMonitor = m_pD3D9->GetAdapterMonitor(adapter);
            if (hMonitor == nullptr)
            {
                break;
            }

            m_outputs.emplace_back();
            OutputDescription& output = m_outputs.back();
            output.hMonitor = hMonitor;

            UINT modeCount = m_pD3D9->GetAdapterModeCount(adapter, format);
            amf_int fullscreenModeIndex = -1;
            for (UINT m = 0; m < modeCount; ++m)
            {
                D3DDISPLAYMODE mode;
                HRESULT hr = m_pD3D9->EnumAdapterModes(adapter, format, m, &mode);
                if (FAILED(hr))
                {
                    break;
                }
                output.modes.push_back(mode);

                amf_uint64 area = mode.Width * mode.Height;
                if (area > biggestArea)
                {
                    biggestArea = area;
                    fullscreenModeIndex = m;
                }
            }

            if (fullscreenModeIndex >= 0)
            {
                output.fullscreenMode = output.modes[fullscreenModeIndex];
            }
        }
    }

    return AMF_OK;
}

AMF_RESULT SwapChainDX9::UpdateCurrentOutput()
{
    AMF_RETURN_IF_FALSE(m_hwnd != nullptr, AMF_NOT_INITIALIZED, L"UpdateCurrentMonitorOutput() - m_hwnd is not initialized");

    // Sets the current output by checking which output
    // corresponds to the monitor displaying the window

    if (m_outputs.empty())
    {
        AMF_RESULT res = UpdateOutputs();
        AMF_RETURN_IF_FAILED(res, L"UpdateCurrentMonitorOutput() - UpdateOutputs() failed");
    }

    HMONITOR hMonitor = MonitorFromWindow((HWND)m_hwnd, MONITOR_DEFAULTTONEAREST);

    for (size_t i = 0; i < m_outputs.size(); ++i)
    {
        if (m_outputs[i].hMonitor == hMonitor)
        {
            m_currentOutput = (amf_int)i;
            return AMF_OK;
        }
    }

    return AMF_FAIL;
}

AMFRect SwapChainDX9::GetOutputRect()
{
    AMF_RESULT res = UpdateCurrentOutput();

    if (res != AMF_OK || m_currentOutput < 0 || (size_t)m_currentOutput >= m_outputs.size())
    {
        AMFTraceError(AMF_FACILITY, L"GetOutputMonitorRect() - UpdateCurrentMonitorOutput() failed");
        return AMFConstructRect(0, 0, 0, 0);
    }

    return AMFConstructRect(0, 0, m_outputs[m_currentOutput].fullscreenMode.Width, m_outputs[m_currentOutput].fullscreenMode.Height);
}

AMF_RESULT SwapChainDX9::BackBufferToSurface(const BackBufferBase* pBuffer, amf::AMFSurface** ppSurface) const
{
    return m_pContext->CreateSurfaceFromDX9Native(pBuffer->GetNative(), ppSurface, nullptr);
}

AMFSize BackBufferDX9::GetSize() const
{
    if (pBuffer == nullptr)
    {
        return {};
    }

    D3DSURFACE_DESC desc = {};
    HRESULT hr = pBuffer->GetDesc(&desc);
    if (FAILED(hr))
    {
        return {};
    }

    return AMFConstructSize(desc.Width, desc.Height);
}
