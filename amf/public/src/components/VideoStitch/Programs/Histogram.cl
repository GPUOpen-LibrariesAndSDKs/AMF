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
// Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.
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
#pragma OPENCL EXTENSION cl_amd_media_ops : enable

__constant sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP | CLK_FILTER_NEAREST; 

#define amf_int32 int

#define WGX 32
#define WGY 8


#define HIST_SIZE   256
#define LEVEL 16.f
#define DIV_VAL   256.f
#define LUT_CURVE_TABLE_SIZE    20

#define XM_PI           3.141592654f
#define XM_2PI          6.283185307f
#define XM_1DIVPI       0.318309886f
#define XM_1DIV2PI      0.159154943f
#define XM_PIDIV2       1.570796327f
#define XM_PIDIV4       0.785398163f


//--------------------------------------------------------------------------------------------------------------------
struct __attribute__ ((packed)) HistogramParameters
{
    amf_int32 maxDistanceBetweenPeaks[3];
    amf_int32 whiteCutOffY;
    amf_int32 blackCutOffY;
    amf_int32 borderHistWidth; //TODO make image size - dependent
    float   alphaLUT;
    float lutCurveTable[LUT_CURVE_TABLE_SIZE]; // curve from 0 to 1
};

//--------------------------------------------------------------------------------------------------------------------
struct __attribute__ ((packed)) Rib
{
    amf_int32 channel1;
    amf_int32 side1;             // 0 - left, 1, top, 2 - right, 3 - bottom
    amf_int32 channel2;
    amf_int32 side2;                 // 0 - left, 1, top, 2 - right, 3 - bottom
    amf_int32 index;
};
//--------------------------------------------------------------------------------------------------------------------
struct __attribute__ ((packed)) Corner
{
    amf_int32 count;
    amf_int32 align1[3];
    amf_int32 channel[4];
    amf_int32 corner[4];             // 0 - lt, 1 - rt, 2 - rb, 3 - lb
    amf_int32 index;
    float     pos[3];
};
//--------------------------------------------------------------------------------------------------------------------
amf_int32 CrossCorrelation(__global amf_int32 *data1, __global amf_int32 *data2, amf_int32 maxdelay);
static float OneCrossCorrelation(
    __global amf_int32 *data1,    // [in]
    __global amf_int32 *data2,    // [in]
    amf_int32 col,
    amf_int32 delay,
    amf_int32 maxdelay,
    __global struct HistogramParameters *params
    );
//--------------------------------------------------------------------------------------------------------------------
static void BuildOneLUT(amf_int32 col, float brightness, __global float *lut, __global float *prev, __global struct HistogramParameters *params, amf_int32 frameCount);

//--------------------------------------------------------------------------------------------------------------------
uint4 rgb_to_yuv( uint4 rgb )
{    
    float4 yuv;
    yuv.x = clamp((  46.742f  *rgb.x + 157.243f   *rgb.y +  15.874f  *rgb.z + LEVEL ) / DIV_VAL, 0.f, 255.f);
    yuv.y = clamp(( (-25.765f)*rgb.x + ( -86.674f)*rgb.y + 112.439f  *rgb.z) / DIV_VAL + 128.5f, 0.f, 255.f);
    yuv.z = clamp(( 112.439f  *rgb.x + (-102.129f)*rgb.y + (-10.310f)*rgb.z) / DIV_VAL + 128.5f, 0.f, 255.f);
    yuv.w = rgb.w;
    return convert_uint4(yuv);
}
//--------------------------------------------------------------------------------------------------------------------
float4 srgb_to_linear(float4 C)
{
    C *= 1.0f / 255.0f;
    C = C > 0.04045f ? pow( C * (1.0f / 1.055f) + 0.0521327f, 2.4f ) : C * (1.0f / 12.92f);
    return 255.0f * C;
}

uint4 yuv_to_rgb( uint4 yuv , amf_int32 linear)
{
    float y = convert_float(yuv.x) - LEVEL;
    float u = convert_float(yuv.y) - 128.f; 
    float v = convert_float(yuv.z) - 128.f;

    float4 rgbf;
    rgbf.x = 1.1643828125f * y +   0.0f     * u +   1.7927421875f * v;
    rgbf.y = 1.1643828125f * y -  0.21325f   * u -   0.53291015625f * v;
    rgbf.z = 1.1643828125f * y + 2.11240234375f   * u +     0.0f   * v;
    if(linear)
    {
        rgbf = srgb_to_linear(rgbf);
    }

    uint4 rgb;
    rgb.x = clamp(rgbf.x, 0.f, 255.f);
    rgb.y = clamp(rgbf.y, 0.f, 255.f);
    rgb.z = clamp(rgbf.z, 0.f, 255.f);
    rgb.w = yuv.w;

    return convert_uint4(rgb);
}

__kernel __attribute__((reqd_work_group_size(WGX, WGY, 1)))
void StitchHistogramMapNV12(
    __global amf_int32 *pOutHist,        ///< [out] histogram data  - 256 x 3 for RG, G and B
    __read_only image2d_t frameY,    ///< [in] current frame
    __read_only image2d_t frameUV,    ///< [in] current frame
    __read_only image2d_t map,    ///< [in] current frame
    __global uint *pParams,            ///< [in]  HistogramParameters*
    amf_int32 channel
)
{
    int2 posUV = (int2)(get_global_id(0), get_global_id(1));
    int2 posY = posUV * 2;

    if(posY.x < get_image_width(frameY) && posY.y < get_image_height(frameY))
    {
        int2 posMap = posY / 8;
        uchar mapData = read_imageui(map, sampler, posMap).x;
        amf_int32 sideX = -1;
        amf_int32 sideY = -1;
        if(((mapData >> 0)  & 1) != 0)
        {
            sideX = 0;
        } 
        if(((mapData >> 1)  & 1) != 0)
        {
            sideX = 1;
        }
        if(((mapData >> 2)  & 1) != 0)
        {
            sideY = 2;
        }
        if(((mapData >> 3)  & 1) != 0)
        {
            sideY = 3;
        }
        
        if (sideX >= 0 || sideY >= 0)
        {
            // 4 pixel from Y - 1 from UV
            uint2 uv = read_imageui(frameUV, sampler, posUV).xy;

            for(int i = 0; i< 4; i++)
            {
                uint y = read_imageui(frameY, sampler, (int2)(posY.x + (i%2), posY.y + (i/2)) ).x;
                uint4 color = (uint4)( y, uv.x, uv.y , 0);

                if(sideX >= 0)
                {
                    __global amf_int32 *pOutHistY = pOutHist + channel * 4 *3 * HIST_SIZE + sideX * HIST_SIZE *3 + 0 * HIST_SIZE;

                    atomic_inc(pOutHistY + color.x);

                    if(i == 0)
                    {
                        __global amf_int32 *pOutHistU = pOutHistY + HIST_SIZE;
                        __global amf_int32 *pOutHistV = pOutHistU + HIST_SIZE;

                        atomic_inc(pOutHistU + color.y);
                        atomic_inc(pOutHistV + color.z);
                    }
                }
                if(sideY >= 0)
                {
                   __global amf_int32 *pOutHistY = pOutHist + channel * 4 *3 * HIST_SIZE + sideY * HIST_SIZE *3 + 0 * HIST_SIZE;
                    atomic_inc(pOutHistY + color.x);

                    if(i == 0)
                    {
                        __global amf_int32 *pOutHistU = pOutHistY + HIST_SIZE;
                        __global amf_int32 *pOutHistV = pOutHistU + HIST_SIZE;
                        atomic_inc(pOutHistU + color.y);
                        atomic_inc(pOutHistV + color.z);
                    }
                }            
            }
        }
    }
}

#define ATAN_POINTS 1000
//--------------------------------------------------------------------------------------------------------------------
uint applyLUT(__global float *pLUT, amf_int32 col, float2 pos, uint color, float4 border)
{
    float colorf = convert_float(color);


    float l = border.x;
    float t = border.y;
    float r = border.z;
    float b = border.w;

    float x = (pos.x - 0.5f) * 2.0f;
    float y = (pos.y - 0.5f) * 2.0f;
    
    
    if(x < 0.0f)
    {
        x /= fabs(l);
    }
    else
    {
        x /= fabs(r);
    }
    if(y < 0.0f)
    {
        y /= fabs(t);
    }
    else
    {
        y /= fabs(b);
    }

    float x_abs = fabs(x);
    float y_abs = fabs(y);

    int index1 = 0;
    int index2 = 0;

    float weight1 = 1.0f;

    if(x != 0)
    {
        if(y_abs < x_abs)
        {
            float q = y_abs / x_abs;
            weight1 = q / (1.0f + 0.28125f * q * q ); // fast atan() implementation
        }
        else
        {
            float q = x_abs / y_abs;
            weight1 = (XM_PIDIV2 - q / (1.0f + 0.28125f * q * q )); // fast atan() implementation
        }
        weight1 /= XM_PIDIV2;
    }
    if(weight1 >= 0.5f)
    {
        weight1 = 1.0f - weight1;
    }
    weight1 = weight1 + 0.5f;

    float weight2 = 1.0f - weight1;

    if(x <= 0.0f && y <= 0.0f)
    { //LeftTop
        index1 = 0;
        if(-x < -y)
        {
            index2 = 1;
        }
        else
        {
            index2 = 3;
        }
    }
    
    else if(x > 0.0f && y <= 0.0f)
    { //RightTop
        index1 = 1;
        if(x < -y)
        {
            index2 = 0;
        }
        else
        {
            index2 = 2;
        }
    }
    else if(x > 0.0f && y > 0.0f)
    { // RightBottom
        index1 = 2;
        if(x < y)
        {
            index2 = 3;
        }
        else
        {
            index2 = 1;
        }
    }
    else if(x <= 0.0f && y > 0.0f)
    { // LeftBottom
        index1 = 3;
        if(-x < y)
        {
            index2 = 2;
        }
        else
        {
            index2 = 0;
        }
    }

    int colIndex =  col * HIST_SIZE + color;
    
    int HistCol = HIST_SIZE * 3;

    float lut1 =      pLUT[index1 * HistCol + colIndex];
    float lut2 =      pLUT[index2 * HistCol + colIndex];
    float lutCenter = pLUT[4      * HistCol + colIndex];
   

    float radius = sqrt(x * x + y * y) * 1.3f;
    float weightColor = 1.0f - radius;
    if(weightColor < 0.0f)
    {
        weightColor = 0.0f;
    }

    float ready;
    if(weight1 + weight2  + weightColor== 0)
    {
        ready = (lut1 + lut2) / 2.0f;
    }
    else
    {
        ready = lutCenter * weightColor + ((lut1 * weight1 + lut2 * weight2) / (weight1 + weight2)) * (1.0f - weightColor);
    }
    ready *= 255.0f;
    return convert_uint(clamp(ready, 0.f, 255.f));
}

//--------------------------------------------------------------------------------------------------------------------
__kernel __attribute__((reqd_work_group_size(WGX, WGY, 1)))
void StitchConvertNV12ToRGB(
    __write_only image2d_t frameRGB,    ///< [out] current frame
    __read_only image2d_t frameY,    ///< [in] current frame
    __read_only image2d_t frameUV,    ///< [in] current frame
    __global float *pLUT,        ///< [in] histogram data  - 256 x 3 for RG, G and B
    float borderL,
    float borderT,
    float borderR,
    float borderB,
    amf_int32 channel,
    amf_int32 linear
)
{

    int2 posUV = (int2)(get_global_id(0), get_global_id(1));
    int2 posY = posUV * 2;

    int imageWidth = get_image_width(frameY);
    int imageHeight = get_image_height(frameY);

    if(posY.x < imageWidth && posY.y < imageHeight)
    {
        uint2 uv = read_imageui(frameUV, sampler, posUV).xy;
        // apply LUT
        float4 border = (float4)( borderL, borderT, borderR, borderB);
         __global float *pLUTChannel =  pLUT + channel * 5 *3 * HIST_SIZE;

         for(int i = 0; i < 4; i++)
         {
            int2 pos = (int2)(posY.x + (i%2), posY.y + (i/2));
            float2 posF = (float2)(convert_float(pos.x) / imageWidth , convert_float(pos.y) / imageHeight );

            uint y = read_imageui(frameY, sampler, pos).x;

            uint4 color = (uint4)( y, uv.x, uv.y , 0);
            uint4 colorLUT;
            colorLUT.x = applyLUT(pLUTChannel, 0,  posF, color.x, border);
            if(i == 0)
            {
                colorLUT.y = applyLUT(pLUTChannel, 1,  posF, color.y, border);
                colorLUT.z = applyLUT(pLUTChannel, 2,  posF, color.z, border);
                colorLUT.w = 0;
            }

            uint4 colorOUT = yuv_to_rgb(colorLUT, linear);
            write_imagef(frameRGB, pos , colorOUT / 255.f);
        }
    }
}

//--------------------------------------------------------------------------------------------------------------------
__kernel __attribute__((reqd_work_group_size(WGX, WGY, 1)))
void WriteBorderRGB(
    __write_only image2d_t frameY,    ///< [out] current frame
    __write_only image2d_t frameUV,    ///< [out] current frame
    __read_only image2d_t borderMap    ///< [in] current map
)
{

    int2 posUV = (int2)(get_global_id(0), get_global_id(1));
    int2 posY = posUV * 2;

    if(posY.x < get_image_width(frameY) && posY.y < get_image_height(frameY))
    {
        int2 posMap = posY / 8;
        uint map = read_imageui(borderMap, sampler, posMap).x;
        if(map != 0 )
        {
            uint4 rgb =  (uint4)(0, 0, 0, 255);

            if(((map >> 0) & 1) != 0)
            {
                rgb.x = 255;
            } 
            if(((map >> 1) & 1) != 0)
            {
                rgb.y = 255;
            }
            if(((map >> 2) & 1) != 0)
            {
                rgb.z = 255;
            }
            if(((map >> 3) & 1) != 0)
            {
                rgb.x = 128;
                rgb.y = 128;
            }
            
            uint4 yuv = rgb_to_yuv( rgb );

            uint4 y = (uint4)(yuv.x,  0, 0, 0);
            uint4 uv = (uint4)(yuv.y,  yuv.z, 0, 0);


            write_imageui(frameY, (int2)(posY.x + 0, posY.y + 0 ), y);
            write_imageui(frameY, (int2)(posY.x + 1, posY.y + 0 ), y);
            write_imageui(frameY, (int2)(posY.x + 0, posY.y + 1 ), y);
            write_imageui(frameY, (int2)(posY.x + 1, posY.y + 1 ), y);
            write_imageui(frameUV, posUV, uv);
        }
    }
}

//--------------------------------------------------------------------------------------------------------------------
__kernel __attribute__((reqd_work_group_size(256, 1,  1)))
void BuildShifts(
    __global float  *pShifts,
    __global amf_int32 *pHistogram,        ///< [in] maximum data
    __global int *pCorners,        ///< [in] maximum data
    __global int *pParams,			///< [in]  HistogramParameters*
    amf_int32 corners,
    amf_int32 frameCount
)
{
    amf_int32 delay = get_global_id(0); // from 0 to max of params->maxDistanceBetweenPeaks
    amf_int32 id = get_global_id(1);
    amf_int32 corner  = id / 3;
    amf_int32 side  = id % 3;
    amf_int32 col = get_global_id(2);

    __global struct HistogramParameters *params = (__global struct HistogramParameters *)pParams;

    if(corner >= corners || delay >= params->maxDistanceBetweenPeaks[col] * 2 || col >= 3) 
    {
        return;
    }
    
    __global struct Corner* it_corner = (__global struct Corner*)pCorners + corner;
    if(side >= it_corner->count)
    {
        return;
    }
    __local float corrs[HIST_SIZE * 2];

    amf_int32 local_id = 2 * get_local_id(0);
    corrs[local_id] = 0;
    corrs[local_id+1] = 0;
    __global amf_int32* pHist[3];

    for(amf_int32 i = 0; i < it_corner->count; i++)
    {
        pHist[i] = pHistogram + it_corner->channel[i] * 4 * 3 * HIST_SIZE  + it_corner->corner[i] * HIST_SIZE * 3 +  col * HIST_SIZE;
    }

    int h1 = 0;
    int h2 = 0;
    if(it_corner->count == 3)
    { 
        if(side == 0)
        {
            h1 = 0;
            h2 = 1;
        }
        else if(side == 1)
        {
            h1 = 1;
            h2 = 2;
        }
        else if(side == 2)
        {
            h1 = 2;
            h2 = 0;
        }
    }
    if(it_corner->count == 2)
    {
        if(side == 0)
        { 
            h1 = 0;
            h2 = 1;
        }
        else 
        { 
            h1 = 1;
            h2 = 0;
        }
    }

    if(delay < params->maxDistanceBetweenPeaks[col] * 2)
    {
        barrier(CLK_LOCAL_MEM_FENCE);
        corrs[delay] = OneCrossCorrelation(pHist[h1], pHist[h2], col, delay, params->maxDistanceBetweenPeaks[col], params);
        barrier  (CLK_LOCAL_MEM_FENCE);

        if(delay == 0)
        {
            float corrMax = -1.0e5f;
            for(amf_int32 i = 0; i < params->maxDistanceBetweenPeaks[col] * 2; i++)
            {
                if(corrMax < corrs[i])
                {    
                    corrMax = corrs[i];
                    pShifts[corner * 3 * it_corner->count + col * it_corner->count + side] = (float)i - params->maxDistanceBetweenPeaks[col];
                }
            }
        }
    }
}

//--------------------------------------------------------------------------------------------------------------------
__kernel __attribute__((reqd_work_group_size(256, 1,  1)))
void BuildLUT(
    __global float *pLUT,        ///< [out] histogram data  - 256 x 3 for RG, G and B
    __global float *pLUTPrev,    ///< [in] histogram data  - 256 x 3 for RG, G and B
    __global float *pBrightness,
    __global float  *pShifts,
    __global int *pCorners,
    __global int *pParams,
    amf_int32 corners,
    amf_int32 frameCount
)
{
    amf_int32 corner  = get_global_id(0);
    amf_int32 side  = get_global_id(1);
    amf_int32 col = get_global_id(2);

    __global struct HistogramParameters *params = (__global struct HistogramParameters *)pParams;

    if(corner >= corners || side >= 3 || col >= 3) 
    {
        return;
    }
    __global struct Corner* it_corner = (__global struct Corner*)pCorners + corner;

    if(side >= it_corner->count)
    {
        return;
    }


    float       brightness[3];
    brightness[0] = 0;
    brightness[1] = 0;
    brightness[2] = 0;

    int shiftOffset = corner * 3 * it_corner->count + col * it_corner->count;

    if(it_corner->count == 2)
    {
        brightness[0] =  pShifts[shiftOffset + 0] / 2.0f;
        brightness[1] =  pShifts[shiftOffset + 1] / 2.0f;
    }
    else if(it_corner->count == 3)
    {

        int shiftMaxIndex = 0;
        int shiftMinIndex = HIST_SIZE *2;
        float shiftMin = 10000.0f;
        float shiftMax = 0.0f;


        for(int side = 0; side < it_corner->count; side++)
        {
            if(fabs(shiftMin) > fabs(pShifts[shiftOffset + side]))
            {
                shiftMin = pShifts[shiftOffset + side];
                shiftMinIndex = side;
            }
            if(fabs(shiftMax) < fabs(pShifts[shiftOffset + side]))
            {
                shiftMax = pShifts[shiftOffset + side];
                shiftMaxIndex = side;
            }
        }
        int shiftMinIndexSecond = 0;
        for(int side = 0; side < it_corner->count; side++)
        {
            if(side != shiftMinIndex && side != shiftMaxIndex)
            {
                shiftMinIndexSecond = side;
                break;
            }
        }


        shiftMin = pShifts[shiftOffset + shiftMinIndex];
        float shiftMinSecond = pShifts[shiftOffset + shiftMinIndexSecond];

            if(shiftMinIndex == 0 )
            {
                brightness[0] =  shiftMin/ 2.0f;
                brightness[1] =  - shiftMin / 2.0f;

                if(shiftMinIndexSecond == 1)
                {
                    brightness[2] = -shiftMinSecond *2.0f / 3.0f - shiftMin/ 2.0f;

                    brightness[0] += shiftMinSecond / 3.0f;
                    brightness[1] += shiftMinSecond / 3.0f;
                }
                else //2
                {
                    brightness[2] = shiftMinSecond *2.0f / 3.0f + shiftMin/ 2.0f;

                    brightness[0] -= shiftMinSecond / 3.0f;
                    brightness[1] -= shiftMinSecond / 3.0f;
                }

            }
            else  if(shiftMinIndex == 1 )
            {
                brightness[1] =  shiftMin/ 2.0f;
                brightness[2] =  -shiftMin / 2.0f;

                if(shiftMinIndexSecond == 0)
                {
                    brightness[0] = shiftMinSecond *2.0f / 3.0f + shiftMin/ 2.0f;

                    brightness[1] -=  shiftMinSecond / 3.0f;
                    brightness[2] -=  shiftMinSecond / 3.0f;
                }
                else //2
                {
                    brightness[0] = -shiftMinSecond *2.0f / 3.0f - shiftMin/ 2.0f;

                    brightness[1] +=  shiftMinSecond / 3.0f;
                    brightness[2] +=  shiftMinSecond / 3.0f;
                }
            }
            else if(shiftMinIndex ==  2)
            {
                brightness[2] =  shiftMin/ 2.0f;
                brightness[0] =  - shiftMin / 2.0f;

                if(shiftMinIndexSecond == 0)
                {
                    brightness[1] = -shiftMinSecond *2.0f / 3.0f - shiftMin/ 2.0f;

                    brightness[2] +=  shiftMinSecond / 3.0f;
                    brightness[0] +=  shiftMinSecond / 3.0f;
                }
                else // 1
                {
                    brightness[1] = shiftMinSecond *2.0f / 3.0f + shiftMin/ 2.0f;

                    brightness[2] -=  shiftMinSecond / 3.0f;
                    brightness[0] -=  shiftMinSecond / 3.0f;
                }
            }
    }
    __global float *lut  = pLUT + it_corner->channel[side] * 3 *5 * HIST_SIZE + it_corner->corner[side] * 3* HIST_SIZE + col * HIST_SIZE;
    __global float *lutPrev = pLUTPrev + it_corner->channel[side] * 3 *5 * HIST_SIZE + it_corner->corner[side] * 3* HIST_SIZE + col * HIST_SIZE;

    BuildOneLUT( col, brightness[side]/ 255.f,  lut, lutPrev, params, frameCount);

    pBrightness[it_corner->channel[side] * 4 *3 + side *3 + col]  = brightness[side];

}

//--------------------------------------------------------------------------------------------------------------------
__kernel __attribute__((reqd_work_group_size(256, 1,  1)))
void BuildLUTCenter(
    __global float *pLUT,        ///< [out] histogram data  - 256 x 3 for RG, G and B
    __global float *pLUTPrev,        ///< [in] histogram data  - 256 x 3 for RG, G and B
    __global float *pBrightness,
    __global int *pParams,			///< [in]  HistogramParameters*
    amf_int32 channels,
    amf_int32 frameCount
)
{
    __global struct HistogramParameters *params = (__global struct HistogramParameters *)pParams;
    amf_int32 channel  = get_global_id(0);
    amf_int32 col = get_global_id(1);

    if(channel < channels)
    {

        float brightnessCenter = 0;
        for( amf_int32 side = 0; side < 4; side++)
        {
            brightnessCenter +=  pBrightness[channel * 4 * 3 + side *3 + col];
        }
        brightnessCenter/=4.0f;

        __global float *lutCenter  = pLUT + channel * 3 *5 * HIST_SIZE + 4 * 3* HIST_SIZE + col * HIST_SIZE;
        __global float *lutPrevCenter = pLUTPrev + channel * 3 *5 * HIST_SIZE + 4 * 3* HIST_SIZE + col * HIST_SIZE;
        BuildOneLUT( col, brightnessCenter / 255.f,  lutCenter, lutPrevCenter, params, frameCount);
    }
}

//--------------------------------------------------------------------------------------------------------------------
static void BuildOneLUT(amf_int32 col, float brightness, __global float *lut, __global float *prev, __global struct HistogramParameters *params, amf_int32 frameCount)
{

    for(amf_int32 k=0; k < HIST_SIZE; k++)
    {
        float lutCurr = brightness;

        if(col == 0)
        {
            if(k < params->blackCutOffY)
            {
                lutCurr = 0.0f;
            }
            else if(k < LUT_CURVE_TABLE_SIZE + params->blackCutOffY)
            {
                lutCurr *= 1.0f - params->lutCurveTable[k - params->blackCutOffY];
            }
            else if(k > params->whiteCutOffY)
            {
                lutCurr = 0.0f;
            }
            else if(k >= params->whiteCutOffY - LUT_CURVE_TABLE_SIZE)
            {
                lutCurr *= params->lutCurveTable[k - (params->whiteCutOffY - LUT_CURVE_TABLE_SIZE) ];
            }
        }

        lutCurr += (float)k /255.f;

        float lutPrev = prev[k];
        if(frameCount != 0 )
        {
            lutCurr = lutPrev + params->alphaLUT * ( lutCurr - lutPrev);
        }
        prev[k] = lutCurr;
        lut[k] = lutCurr;
   }
}

//--------------------------------------------------------------------------------------------------------------------
amf_int32 CrossCorrelation(__global amf_int32 *data1, __global amf_int32 *data2, amf_int32 maxdelay)
{

    float x[HIST_SIZE];
    float y[HIST_SIZE];
    for(amf_int32 i = 0; i < HIST_SIZE; i++)
    {
        x[i]  = (float)data1[i];
        y[i]  = (float)data2[i];
    }

   float sx,sy,sxy,denom;
   
   /* Calculate the mean of the two series x[], y[] */
   float mx = 0;
   float my = 0;   
   for (amf_int32 i=0 ; i < HIST_SIZE; i++) 
   {
      mx += x[i];
      my += y[i];
   }
   mx /= HIST_SIZE;
   my /= HIST_SIZE;

   /* Calculate the denominator */
   sx = 0;
   sy = 0;
   for (amf_int32 i=0; i < HIST_SIZE; i++) 
   {
      sx += (x[i] - mx) * (x[i] - mx);
      sy += (y[i] - my) * (y[i] - my);
   }
   denom = sqrt(sx*sy);

   /* Calculate the correlation series */
   float rMax = 0;
   amf_int32 delayMax = 0;
   for (amf_int32 delay=-maxdelay; delay < maxdelay; delay++) 
   {
      sxy = 0;
      for (int i = 0; i < HIST_SIZE; i++) 
      {
         int j = i + delay;
         if (j < 0 || j >= HIST_SIZE)
         {
            continue;
         }
         else
         {
            sxy += (x[i] - mx) * (y[j] - my);
         }
      }
      float r = sxy / denom;
      
      /* r is the correlation coefficient at "delay" */
      if(r > rMax)
      {
          rMax = r; 
          delayMax = delay;
      }
   }
   return delayMax;
}

//--------------------------------------------------------------------------------------------------------------------
static float OneCrossCorrelation(
    __global amf_int32 *data1,    // [in]
    __global amf_int32 *data2,    // [in]
    amf_int32 col,
    amf_int32 delay,
    amf_int32 maxdelay,
    __global struct HistogramParameters *params
    )
{
    float r = 0;
    float x;
    float y;


    if(delay  - maxdelay < maxdelay)
    {
        // Calculate the mean of the two series x[], y[]
        float mx = 0;
        float my = 0;   
        for (amf_int32 i = params->blackCutOffY; i < params->whiteCutOffY; i++)
        {
            mx += data1[i];
            my += data2[i];
        }
        mx /= HIST_SIZE;
        my /= HIST_SIZE;
        // Calculate the denominator
        float sx = 0;
        float sy = 0;
        for (amf_int32 i = params->blackCutOffY; i < params->whiteCutOffY; i++)
        {
            x = (float)data1[i] - mx;
            y = (float)data2[i] - my;
            sx += x * x;
            sy += y * y;
        }
        float denom = sqrt(sx*sy);

        // Calculate the correlation
        float sxy = 0;

        for (amf_int32 i = params->blackCutOffY; i < params->whiteCutOffY; i++)
        {
            int j = i + delay  - maxdelay;
            
            if (j < params->blackCutOffY || j >= params->whiteCutOffY)
            {
                continue;
            }
            else
            {
                x = (float)data1[i] - mx;
                y = (float)data2[j] - my;
                sxy += x * y;
            }                
        }
        r = (float)(sxy / denom);
    }
    return r;
}
//--------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------
