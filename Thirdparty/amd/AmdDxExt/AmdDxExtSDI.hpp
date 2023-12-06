/* Copyright (C) 2013-2023 Advanced Micro Devices, Inc. All rights reserved. */

/**
***************************************************************************************************
* @file  amddxextSDI.h
* @brief AMD D3D SDI Extension API include file.
***************************************************************************************************
*/

#ifndef _AMDDXEXTSDI_H_
#define _AMDDXEXTSDI_H_

//ASCII "???This is an AMD SDI allocation?"
const UINT AmdDxSDIAlloc[8] = {0x17198154, 0x68697320, 0x69732061, 0x6e20414d,
                               0x44205344, 0x4920616c, 0x6c6f6361, 0x74696f6e};
//ASCII "q??????AMD SDI pinned allocation"
const UINT AmdDxSDIPinnedAlloc[8] = {0x71818911, 0x83910741, 0x4d442053, 0x44492070,
                                     0x696e6e65, 0x6420616c, 0x6c6f6361, 0x74696f6e};
//ASCII "???This is an AMD Remote SDI allocation?"
const UINT AmdDxRemoteSDIAlloc[8] = {0x73696854, 0x20736920, 0x41206e61, 0x5220444d,
                                     0x6c612049, 0x61636f6c, 0x6e6f6974, 0x74696f6e};

struct ID3D11Resource;
struct ID3D10Resource;

//forward declaration of D3D classes

// This include file contains all the SDI extension definitions
// (structures, enums, constants) shared between the driver and the application

/**
***************************************************************************************************
* @brief
*    AmdDxSDISurfaceAttributes struct - SDI input struct. Contains the information needed to 
*    construct a SDI surface and return it to the app. When querying a local SDI surface, KMD 
*    returns the surface bus address and marker bus address. When querying a remote SDI surface,
*    KMD returns a handle to the remote adapter surface. Only one of the two can be valid.
***************************************************************************************************
*/
struct AmdDxSDISurfaceAttributes
{
    union {
        struct {
            ULONG64  SurfaceBusAddr; ///< MC address of local SDI surface
            ULONG64  MarkerBusAddr;  ///< MC address of marker boundary for local SDI surface
        };

        struct {
            ULONG64 hSDIAdapterSurface;  ///< handle to remote SDI adapter surface
            ULONG64 SDIAdapterSurfaceVA; ///< associated remote SDI surface virtual address
                                         ///< only set when TargetOfQuery == SDI_QUERY_TARGET__REMOTE
        };
    };
};

/**
***************************************************************************************************
* @brief
*    AmdDxSDISyncInfo struct - SyncPixelBuffer input struct.  Used to indicate the direction of
*    sync required between a local SDI surface and its pixel buffer (if one exists).
***************************************************************************************************
*/
union AmdDxSDISyncInfo
{
    struct
    {
        UINT SurfToPixelBuffer :  1; ///< Sync direction (to/from the pixel buffer of a resource)
        UINT reserved          : 31;
    };
    UINT value;
};

/**
***************************************************************************************************
* @brief
*    AmdDxSDIQueryAllocInfo struct - SDI Query struct. Used to query the MC address of SDI surface
*    as well as the marker address for a given resource. Only one of pResource10 and pResource11
*    should be non-NULL.
***************************************************************************************************
*/
struct AmdDxSDIQueryAllocInfo
{
    ID3D10Resource*            pResource10; ///< actual resource that was created by app
    ID3D11Resource*            pResource11; ///< actual resource that was created by app
    AmdDxSDISurfaceAttributes* pInfo;       ///< Input/output surface attributes for SDI surface
};

/**
***************************************************************************************************
* @brief
*    AmdDxSDIAdapterSurfaceInfo struct - SDI Adapter surface attributes. Used to create the remote
*    SDI adapter surface. Only one of pResource10 and pResource11 should be non-NULL
***************************************************************************************************
*/
struct AmdDxSDIAdapterSurfaceInfo
{
    ID3D10Resource* pResource10;   ///< actual resource that was created by app
    ID3D11Resource* pResource11;   ///< actual resource that was created by app
    UINT            sizeOfSurface; ///< size of the surface
    UINT            markerSize;    ///< size of marker(usually a page)
    AmdDxSDISurfaceAttributes surfAttrib; ///< Input/output surface attributes for SDI surface
};

/**
***************************************************************************************************
* @brief
*    AmdDxRemoteSDISurfaceList struct - List of SDI adapter surfaces to be created
*
***************************************************************************************************
*/
struct AmdDxRemoteSDISurfaceList
{
    UINT                        numSurfaces; ///< number of SDI surfaces
    AmdDxSDIAdapterSurfaceInfo* pInfo;       ///< Array of peer SDI surface infos
};

/**
***************************************************************************************************
* @brief
*    AmdDxLocalSDISurfaceList struct - List of local SDI surfaces to be created
*
***************************************************************************************************
*/
struct AmdDxLocalSDISurfaceList
{
    UINT                        numSurfaces; ///< number of local SDI surfaces
    AmdDxSDIQueryAllocInfo*     pInfo;       ///< Array of local SDI surface infos
};

/**
***************************************************************************************************
* @brief
*    AmdDxMarkerInfo struct - Attributes of an SDI marker
*
***************************************************************************************************
*/
struct AmdDxMarkerInfo
{
    UINT  markerValue;  ///< value of marker to be written
    UINT  markerOffset; ///< address offset from start of surface where marker is written to
};

#endif //_AMDDXEXTSDI_H_
