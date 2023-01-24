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

#include "VideoTransfer.h"
#include "public/common/InterfaceImpl.h"
#include "public/common/TraceAdapter.h"
#include <atlbase.h>
#include <d3d11.h>
#include "../Thirdparty/amd/AmdDxExt/AmdDxExtSDIApi.hpp"

#define AMF_FACILITY L"DeckLinkMediaImpl"

namespace amf
{
    class AMFVideoTransferDX11 : public AMFInterfaceImpl<AMFVideoTransfer>
    {
    public:
        AMFVideoTransferDX11(AMFContext *pContext);
        virtual ~AMFVideoTransferDX11();

        virtual AMF_RESULT          AMF_STD_CALL AllocateSurface(size_t size, AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height, void **ppVirtualMemory, void **ppSurface);
        virtual AMF_RESULT          AMF_STD_CALL ReleaseSurface(void *ppSurface);
        virtual AMF_RESULT          AMF_STD_CALL Transfer(void *pVirtualMemory, amf_size inLineSizeInBytes, void *pSurface);
    protected:
        AMFContextPtr                   m_pContext;
        CComPtr<ID3D11Device>           m_pDeviceD3D;
        CComPtr<ID3D11DeviceContext>    m_pDeviceContextD3D;
        HMODULE                         m_hDrvDll;
        IAmdDxExt*                      m_pExt; // these interfaces are not compatible with CComPtr so use naked pointers
        IAmdDxExtSDI*                   m_pSDIExt;

    };
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AMFVideoTransferDX11::AMFVideoTransferDX11(AMFContext *pContext) :
m_pContext(pContext),
m_hDrvDll(NULL),
m_pExt(NULL),
m_pSDIExt(NULL)

{
    m_pDeviceD3D = (ID3D11Device*)m_pContext->GetDX11Device();
    m_pDeviceD3D->GetImmediateContext(&m_pDeviceContextD3D);
#if defined(_M_AMD64)
    m_hDrvDll = LoadLibraryW(L"atidxx64.dll");
#else 
    m_hDrvDll = LoadLibraryW(L"atidxx32.dll");
#endif
    if(m_hDrvDll != NULL)
    {
        PFNAmdDxExtCreate11 pAmdDxExtCreate = reinterpret_cast<PFNAmdDxExtCreate11>(GetProcAddress(m_hDrvDll, "AmdDxExtCreate11"));
        if(pAmdDxExtCreate != NULL)
        {
            pAmdDxExtCreate(m_pDeviceD3D, &m_pExt);
            if(m_pExt != NULL)
            {
                m_pSDIExt = static_cast<IAmdDxExtSDI*>((m_pExt)->GetExtInterface(AmdDxExtSDIID)); // GetExtInterface() returns reference count = 1
                if(m_pSDIExt == NULL)
                {
                    // from CCC: AMD Control Center -> AMD Firepro -> SDI/DirectGMA and restart the app
                    // to enable programmatically have to call KMD escape: LHSCAPE_UMDKMDIF_SDI_STATE to enable and set Aperture size and recreate DX11 device
                    // DXX limits this to workstations
                }
            }

        }
    }
}
//-------------------------------------------------------------------------------------------------
AMFVideoTransferDX11::~AMFVideoTransferDX11()
{
    if(m_pSDIExt != NULL)
    {
        m_pSDIExt->Release();
    }
    if(m_pExt != NULL)
    {
        m_pExt->Release();
    }
    m_pDeviceContextD3D.Release();
    m_pDeviceD3D.Release();
    m_pContext.Release();

    if(m_hDrvDll != NULL)
    {
        FreeLibrary(m_hDrvDll);
    }
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT          AMF_STD_CALL AMFVideoTransferDX11::AllocateSurface(size_t size, AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height, void **ppVirtualMemory, void **ppSurface)
{
    AMF_RETURN_IF_FALSE(m_pDeviceD3D != NULL, AMF_NOT_INITIALIZED, L"Not Initialized");

//    *ppVirtualMemory = amf_virtual_alloc(size);
    *ppVirtualMemory = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE | MEM_WRITE_WATCH, PAGE_READWRITE);
    AMF_RETURN_IF_FALSE(*ppVirtualMemory != NULL, AMF_OUT_OF_MEMORY, L"Out of memory");

    // pin memory
    VirtualLock(*ppVirtualMemory, size);

    HRESULT hr = S_OK;
    D3D11_TEXTURE2D_DESC desc;	
    memset(&desc, 0 , sizeof(D3D11_TEXTURE2D_DESC));
    D3D11_SUBRESOURCE_DATA InitialData;
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;

    switch(format)
    {
    case AMF_SURFACE_BGRA:
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; 
        break;
    case AMF_SURFACE_RGBA:
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; 
        break;
    case AMF_SURFACE_YUY2:
        desc.Format = DXGI_FORMAT_YUY2; 
        break;
    case AMF_SURFACE_UYVY:
        desc.Format = DXGI_FORMAT_YUY2; 
        break;
    case AMF_SURFACE_Y210:
        desc.Format = DXGI_FORMAT_R10G10B10A2_UINT;
        desc.Width = (width * 2 + 2) / 3; //V210, 10-10-10-2, 10-
//        desc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
//        desc.Width /= 2;
        break;
    default:
        AMF_RETURN_IF_FALSE(false, AMF_NOT_SUPPORTED, L"Format is not supported %s", AMFSurfaceGetFormatName(format));
        break;
    }
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;	
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    ID3D11Texture2D *pTexture = NULL;

    if(m_pSDIExt != NULL)
    {
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
        desc.Usage = D3D11_USAGE_DEFAULT;
        hr= m_pSDIExt->SetPinnedSysMemAddress((UINT*)*ppVirtualMemory);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SetPinnedSysMemAddress() - pinned failed");

        InitialData.SysMemPitch = desc.Width;
        InitialData.SysMemSlicePitch = 0;
        InitialData.pSysMem = &AmdDxSDIPinnedAlloc;

        hr = m_pDeviceD3D->CreateTexture2D( &desc, &InitialData, &pTexture);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateTexture2D() - pinned failed");
        /*
        AmdDxSDISurfaceAttributes info ={};
        AmdDxSDIQueryAllocInfo allocInfo = {};
        allocInfo.pInfo = &info;
        allocInfo.pResource11 = pTexture;
        AmdDxLocalSDISurfaceList surfaceList {};
        surfaceList.numSurfaces = 1;
        surfaceList.pInfo = &allocInfo;

        hr = m_pSDIExt->MakeResidentSDISurfaces(&surfaceList);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"MakeResidentSDISurfaces() - regular failed");
        */
    }
    else
    {
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.MiscFlags = 0;
        hr = m_pDeviceD3D->CreateTexture2D( &desc, NULL, &pTexture);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"CreateTexture2D() failed");
    }

    UINT formatUINT = format;
    pTexture->SetPrivateData(AMFFormatGUID, sizeof(formatUINT), &formatUINT);

    *ppSurface = pTexture;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT          AMF_STD_CALL AMFVideoTransferDX11::ReleaseSurface(void *pSurface)
{
    AMF_RETURN_IF_FALSE(m_pDeviceD3D != NULL, AMF_NOT_INITIALIZED, L"Not Initialized");
    AMF_RETURN_IF_FALSE(pSurface != NULL, AMF_INVALID_ARG, L"pSurface == NULL");

    ID3D11Texture2D *pTexture = (ID3D11Texture2D *)pSurface;
    pTexture->Release();
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT          AMF_STD_CALL AMFVideoTransferDX11::Transfer(void *pVirtualMemory, amf_size inLineSizeInBytes, void *pSurface)
{
    AMF_RETURN_IF_FALSE(m_pDeviceD3D != NULL, AMF_NOT_INITIALIZED, L"Not Initialized");
    AMF_RETURN_IF_FALSE(pSurface != NULL, AMF_INVALID_ARG, L"pSurface == NULL");

    HRESULT hr = S_OK;

    AMFContext::AMFDX11Locker lockerD3D11(m_pContext);

    ID3D11Texture2D *pTexture = (ID3D11Texture2D *)pSurface;
    if(m_pSDIExt != NULL)
    {
        /*
        AmdDxSDISyncInfo info = {};
        info.SurfToPixelBuffer = 1;
        hr = m_pSDIExt->SyncPixelBuffer11(pTexture, &info);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"SyncPixelBuffer11() failed");
        */
    }
    else
    {
        D3D11_TEXTURE2D_DESC desc = {};
        pTexture->GetDesc(&desc);

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        hr = m_pDeviceContextD3D->Map(pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_DIRECTX_FAILED, L"Map() failed");

        amf_uint8* src = (amf_uint8*)pVirtualMemory;
        amf_uint8* dst = (amf_uint8*)mapped.pData;

        amf_size lineWidth = desc.Width;
        bool bUnpack = false;

        switch(desc.Format)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            lineWidth *= 4;
            break;
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            lineWidth *= 4;
            break;
        case DXGI_FORMAT_YUY2:
            lineWidth *= 2;
            break;
        case DXGI_FORMAT_R16G16B16A16_UNORM:
        case DXGI_FORMAT_Y210:  //Y210
            bUnpack = true;
            lineWidth = inLineSizeInBytes;
            break;
        case DXGI_FORMAT_R10G10B10A2_UINT:  //V210
            lineWidth *= 4;
            break;
        default:
            AMF_RETURN_IF_FALSE(false, AMF_NOT_SUPPORTED, L"DXGI Format is not supported %d", desc.Format);
            break;
        }

        if (!bUnpack)
        {
            for (UINT y = 0; y < desc.Height; y++)
            {
                memcpy(dst, src, lineWidth);
                dst += mapped.RowPitch;
                src += inLineSizeInBytes;
            }
        }
        else //unpack V210 to Y210, xx-10-10-10 to 16-16-16
        {
            //            amf_uint32 countWord = (desc.Width * 4 + 2) / 3;  //4:2:2
            amf_uint32 countWord = ((amf_uint32)inLineSizeInBytes + 3) / 4;
            amf_uint16 element = 0;
            for (amf_uint32 y = 0; y < desc.Height; y++)
            {
                //V210 to Y210, 10-10-10-10 to 16-16-16-16, 32bit (xx10-10-10 to 16-16-16)
                amf_uint32* pSrc = (amf_uint32*)src;
                amf_uint16* pDst = (amf_uint16*)dst;

                for (amf_uint32 x = 0; x < countWord; x++, pSrc++)
                {
                    element = (*pSrc) & 0x00000003FF;
                    element = (element << 6) & 0xFFC0;
                    *pDst++ = element;

                    element = ((*pSrc) >> 10) & 0x00000003FF;
                    element = (element << 6) & 0xFFC0;
                    *pDst++ = element;

                    element = ((*pSrc) >> 20) & 0x00000003FF;
                    element = (element << 6) & 0xFFC0;
                    *pDst++ = element;
                }
                dst += mapped.RowPitch;
                src += inLineSizeInBytes;
            }
        }
        m_pDeviceContextD3D->Unmap(pTexture, 0);
    }
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AMF_RESULT          AMF_STD_CALL AMFVideoTransfer::CreateVideoTransfer(AMFContext *pContext, AMF_MEMORY_TYPE eType, AMFVideoTransfer **ppTransfer)
{
    AMFVideoTransferPtr transfer;

    switch(eType)
    {
    case AMF_MEMORY_DX11:
        transfer = new AMFVideoTransferDX11(pContext);
        break;
    }
    AMF_RETURN_IF_FALSE(transfer != NULL, AMF_NOT_SUPPORTED, L"Transfer for %s is not supported", AMFGetMemoryTypeName(eType));

    *ppTransfer = transfer.Detach();
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
} // namespace amf
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
