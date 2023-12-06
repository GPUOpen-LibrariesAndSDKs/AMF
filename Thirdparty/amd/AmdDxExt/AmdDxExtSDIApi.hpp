/* Copyright (C) 2013-2023 Advanced Micro Devices, Inc. All rights reserved. */

/**
***************************************************************************************************
* @file  amddxextsdiapi.h
* @brief
*    AMD D3D SDI Extension API include file.
*    This is the main include file for apps using Hi Z extension.
***************************************************************************************************
*/

#ifndef _AMDDXEXTSDIAPI_H_
#define _AMDDXEXTSDIAPI_H_

#include "AmdDxExtApi.hpp"
#include "AmdDxExtIface.hpp"
#include "AmdDxExtSDI.hpp"

// AMD extension ID passed to IAmdDxExt::GetExtInterface()
const unsigned int AmdDxExtSDIID = 6;

/**
***************************************************************************************************
* @brief Abstract SDI extension interface class
*
***************************************************************************************************
*/
class IAmdDxExtSDI : public IAmdDxExtInterface
{
public:
    virtual HRESULT QuerySDIAllocationAddress(AmdDxSDIQueryAllocInfo *pInfo) = 0;
    virtual HRESULT MakeResidentSDISurfaces(AmdDxLocalSDISurfaceList *pList) = 0;
    virtual BOOL WriteMarker(ID3D10Resource *pResource, AmdDxMarkerInfo *pMarkerInfo) = 0;
    virtual BOOL WaitMarker(ID3D10Resource *pResource, UINT val) = 0;
    virtual BOOL WriteMarker11(ID3D11Resource *pResource, AmdDxMarkerInfo *pMarkerInfo) = 0;
    virtual BOOL WaitMarker11(ID3D11Resource *pResource, UINT val) = 0;
    virtual HRESULT CreateSDIAdapterSurfaces(AmdDxRemoteSDISurfaceList *pList) = 0;
    virtual HRESULT SetPinnedSysMemAddress(UINT *pSysMem) = 0;

    // ESEN
    virtual HRESULT SyncPixelBuffer(ID3D10Resource *pResource, AmdDxSDISyncInfo* pSyncInfo) = 0;
    virtual HRESULT SyncPixelBuffer11(ID3D11Resource *pResource, AmdDxSDISyncInfo* pSyncInfo) = 0;
};

#endif //_AMDDXEXTSDIAPI_H_
