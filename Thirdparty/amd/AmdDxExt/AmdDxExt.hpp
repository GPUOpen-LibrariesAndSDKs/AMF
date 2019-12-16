


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
* @file  amddxext.h
* @brief AMD D3D Exension API include file.
***************************************************************************************************
*/
#ifndef _AMDDXEXT_H_
#define _AMDDXEXT_H_


/// Extended Primitive Topology enumeration
enum AmdDxExtPrimitiveTopology
{
                                                       // D3D10_DDI_PRIMITIVE_TOPOLOGY_* values
    AmdDxExtPrimitiveTopology_Undefined          = 0,  // D3D10 UNDEFINED
    AmdDxExtPrimitiveTopology_PointList          = 1,  // D3D10 POINTLIST
    AmdDxExtPrimitiveTopology_LineList           = 2,  // D3D10 LINELIST
    AmdDxExtPrimitiveTopology_LineStrip          = 3,  // D3D10 LINESTRIP
    AmdDxExtPrimitiveTopology_TriangleList       = 4,  // D3D10 TRIANGLELIST
    AmdDxExtPrimitiveTopology_TriangleStrip      = 5,  // D3D10 TRIANGLESTRIP
                                                       // 6 is reserved for legacy triangle fans
    AmdDxExtPrimitiveTopology_ExtQuadList        = 7,  // No D3D10 equivalent
    AmdDxExtPrimitiveTopology_ExtPatch           = 8,  // No D3D10 equivalent
                                                       // 9 is unused
    AmdDxExtPrimitiveTopology_LineListAdj        = 10, // D3D10 LINELIST_ADJ
    AmdDxExtPrimitiveTopology_LineStripAdj       = 11, // D3D10 LINESTRIP_ADJ
    AmdDxExtPrimitiveTopology_TriangleListAdj    = 12, // D3D10 TRIANGLELIST_ADJ
    AmdDxExtPrimitiveTopology_TriangleStripAdj   = 13, // D3D10 TRIANGLESTRIP_ADJ
    AmdDxExtPrimitiveTopology_Max                = 14
};



#endif // _AMDDXEXT_H_