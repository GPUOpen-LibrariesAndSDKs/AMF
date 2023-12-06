/* Copyright (C) 2008-2023 Advanced Micro Devices, Inc. All rights reserved. */

/**
***************************************************************************************************
* @file  amddxextapi.h
* @brief AMD D3D Extension API include file. This is the main include file for apps using extensions.
***************************************************************************************************
*/
#ifndef _AMDDXEXTAPI_H_
#define _AMDDXEXTAPI_H_

#include "AmdDxExt.hpp"
#include "AmdDxExtIface.hpp"

// forward declaration of main extension interface defined lower in file
class IAmdDxExt;

// forward declaration for d3d specific interfaces
interface ID3D10Device;
interface ID3D11Device;
interface ID3D10Resource;
interface ID3D11Resource;

// App must use GetProcAddress, etc. to retrieve this exported function
// The associated typedef provides a convenient way to define the function pointer
HRESULT __cdecl AmdDxExtCreate(ID3D10Device* pDevice, IAmdDxExt** ppExt);
typedef HRESULT (__cdecl *PFNAmdDxExtCreate)(ID3D10Device* pDevice, IAmdDxExt** ppExt);

HRESULT __cdecl AmdDxExtCreate11(ID3D11Device* pDevice, IAmdDxExt** ppExt);
typedef HRESULT (__cdecl *PFNAmdDxExtCreate11)(ID3D11Device* pDevice, IAmdDxExt** ppExt);

// Extension version information
struct AmdDxExtVersion
{
    unsigned int        majorVersion;
    unsigned int        minorVersion;
};

// forward declaration of classes referenced by IAmdDxExt
class IAmdDxExtInterface;

/**
***************************************************************************************************
* @brief This class serves as the main extension interface.
*
* AmdDxExtCreate returns a pointer to an instantiation of this interface.
* This object is used to retrieve extension version information
* and to get specific extension interfaces desired.
***************************************************************************************************
*/
class IAmdDxExt : public IAmdDxExtInterface
{
public:
    virtual HRESULT             GetVersion(AmdDxExtVersion* pExtVer) = 0;
    virtual IAmdDxExtInterface* GetExtInterface(unsigned int iface) = 0;

    // General extensions
    virtual HRESULT             IaSetPrimitiveTopology(unsigned int topology) = 0;
    virtual HRESULT             IaGetPrimitiveTopology(AmdDxExtPrimitiveTopology* pExtTopology) = 0;
    virtual HRESULT             SetSingleSampleRead(ID3D10Resource* pResource,
                                                    BOOL singleSample) = 0;
    virtual HRESULT             SetSingleSampleRead11(ID3D11Resource* pResource,
                                                      BOOL singleSample) = 0;

protected:
    IAmdDxExt() {};
    virtual ~IAmdDxExt() = 0 {};
};

#endif // _AMDDXEXTAPI_H_
