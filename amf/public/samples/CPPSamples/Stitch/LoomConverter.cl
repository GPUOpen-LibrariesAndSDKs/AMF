/******************************************************************************
*
*  Trade secret of Advanced Micro Devices, Inc.
*  Copyright 2014, Advanced Micro Devices, Inc., (unpublished)
*
*  All rights reserved.  This notice is intended as a precaution against
*  inadvertent publication and does not imply publication or any waiver
*  of confidentiality.  The year included in the foregoing notice is the
*  year of creation of the work.
*
******************************************************************************/
/**
*******************************************************************************
* @file   LoomConverter.cl
* @brief  Convert between RGBA and RGB
*
*******************************************************************************
*/

#pragma OPENCL EXTENSION cl_amd_media_ops : enable

#ifndef WORK_GROUP_SIZE_X
#define WORK_GROUP_SIZE_X 8
#endif

#ifndef WORK_GROUP_SIZE_Y
#define WORK_GROUP_SIZE_Y 8
#endif
//-------------------------------------------------------------------------------------------------
constant sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST;
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
__kernel
//__attribute__((reqd_work_group_size(WORK_GROUP_SIZE_X, WORK_GROUP_SIZE_Y, 1)))
void BGRAtoRGB(
    __read_only image2d_t imageIn,
    __global    uchar*   pImageOut, 
    int width,
    int height,
    int pitch,
    int offset
    )
{
    int2 pos = (int2)(get_global_id(0), get_global_id(1));
 //   return;
    if ((pos.x < width) && (pos.y < height))
    {
        uint4 rgba = read_imageui(imageIn, sampler, pos);
        uint posDst = offset + pos.y * pitch + 3 * pos.x;
        *(pImageOut + posDst++) = rgba.s2 & 0x000000ff;
        *(pImageOut + posDst++) = rgba.s1 & 0x000000ff;
        *(pImageOut + posDst++) = rgba.s0 & 0x000000ff;
    }
//    write_imageui(pDst, (int2)(position.x, position.y), bytes);
}

//-------------------------------------------------------------------------------------------------
__kernel
//__attribute__((reqd_work_group_size(WORK_GROUP_SIZE_X, WORK_GROUP_SIZE_Y, 1)))
void RGBtoBGRA(
__write_only image2d_t imageOut,
__global    uchar*   pImageIn,
int width,
int height,
int pitch,
int offset
)
{
    int2 pos = (int2)(get_global_id(0), get_global_id(1));
    //   return;
    if ((pos.x < width) && (pos.y < height))
    {
        uint4 rgba;
        uint posSrc = offset + pos.y * pitch + 3 * pos.x;
        rgba.s3 = 255;
        rgba.s2 = *(pImageIn + posSrc++);
        rgba.s1 = *(pImageIn + posSrc++);
        rgba.s0 = *(pImageIn + posSrc++);

        write_imageui(imageOut, pos, rgba);
    }
}
//-------------------------------------------------------------------------------------------------
