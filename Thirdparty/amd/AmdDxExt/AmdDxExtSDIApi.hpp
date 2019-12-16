


//--------------------------------------------------------------------------------------
//
//
// Copyright 2014 ADVANCED MICRO DEVICES, INC.  All Rights Reserved.
//
// AMD is granting you permission to use this software and documentation (if
// any) (collectively, the "Materials") pursuant to the terms and conditions
// of the Software License Agreement included with the Materials.  If you do
// not have a copy of the Software License Agreement, contact your AMD
// representative for a copy.
// You agree that you will not reverse engineer or decompile the Materials,
// in whole or in part, except as allowed by applicable law.
//
// WARRANTY DISCLAIMER: THE SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND.  AMD DISCLAIMS ALL WARRANTIES, EXPRESS, IMPLIED, OR STATUTORY,
// INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE, NON-INFRINGEMENT, THAT THE SOFTWARE
// WILL RUN UNINTERRUPTED OR ERROR-FREE OR WARRANTIES ARISING FROM CUSTOM OF
// TRADE OR COURSE OF USAGE.  THE ENTIRE RISK ASSOCIATED WITH THE USE OF THE
// SOFTWARE IS ASSUMED BY YOU.
// Some jurisdictions do not allow the exclusion of implied warranties, so
// the above exclusion may not apply to You.
//
// LIMITATION OF LIABILITY AND INDEMNIFICATION:  AMD AND ITS LICENSORS WILL
// NOT, UNDER ANY CIRCUMSTANCES BE LIABLE TO YOU FOR ANY PUNITIVE, DIRECT,
// INCIDENTAL, INDIRECT, SPECIAL OR CONSEQUENTIAL DAMAGES ARISING FROM USE OF
// THE SOFTWARE OR THIS AGREEMENT EVEN IF AMD AND ITS LICENSORS HAVE BEEN
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGES. 
// In no event shall AMD's total liability to You for all damages, losses,
// and causes of action (whether in contract, tort (including negligence) or
// otherwise) exceed the amount of $100 USD.  You agree to defend, indemnify
// and hold harmless AMD and its licensors, and any of their directors,
// officers, employees, affiliates or agents from and against any and all
// loss, damage, liability and other expenses (including reasonable attorneys'
// fees), resulting from Your use of the Software or violation of the terms and
// conditions of this Agreement. 
//
// U.S. GOVERNMENT RESTRICTED RIGHTS: The Materials are provided with "RESTRICTED
// RIGHTS." Use, duplication, or disclosure by the Government is subject to the
// restrictions as set forth in FAR 52.227-14 and DFAR252.227-7013, et seq., or
// its successor.  Use of the Materials by the Government constitutes
// acknowledgement of AMD's proprietary rights in them.
//
// EXPORT RESTRICTIONS: The Materials may be subject to export restrictions as
// stated in the Software License Agreement.
//
//--------------------------------------------------------------------------------------





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
