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
/**
*******************************************************************************
* @file   LoomConverter.cl
* @brief  Convert between RGBA and RGB
*
*******************************************************************************
*/
#pragma OPENCL EXTENSION cl_amd_media_ops : enable

constant sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST;

uint4 NV12toRGB(uint4 dataIn);
uint4 GreenReducing(uint4 dataIn, int threshold, int threshold2);
uint4 GreenReducing2(uint4 dataIn, int threshold, int threshold2, int mask);
uint4 GreenReducingExt(uint4 dataIn, uint4 dataBK, uint keycolor, uint delta);
uint4 GreenReducingExt2(uint4 dataIn, uint keycolor);
uint4 ColorMap(int2 pos, int width);
uint EdgeSub(__read_only  image2d_t planeInY, int2 pos);
uint3 BokehSub(__read_only  image2d_t planeY, __read_only  image2d_t planeUV, int2 pos, int radiusBokeh, int height);
uint3 BokehSub2(__read_only  image2d_t planeY, __read_only  image2d_t planeUV, int2 pos, int radiusBokeh);//square, much better performance
uint3 BokehSub4(__read_only  image2d_t planeY, __read_only  image2d_t planeUV, int2 posIn, int radiusBokeh);//circular, lds
uint BokehLuma(__read_only  image2d_t planeY, int2 posIn, int radiusBokeh);
uint2 BokehUV(__read_only  image2d_t planeUV, int2 posIn, int radiusBokeh);

uint BokehLuma2(__read_only  image2d_t planeY, int2 posIn, int radiusBokeh);//square, lds
uint BokehLuma3(__read_only  image2d_t planeY, int2 posIn, int radiusBokeh);//circular, lds
uint BokehLuma4(__read_only  image2d_t planeY, int2 posIn, int radiusBokeh);//circular, lds
uint BokehLuma5(__read_only  image2d_t planeY, __local float coef[], int2 posIn, int radiusBokeh);//circular, lds
uint2 BokehUV5(__read_only  image2d_t planeY, __local float coef[], int2 posIn, int radiusBokeh);

__kernel __attribute__((reqd_work_group_size(8, 8, 1)))
void Process(
    __write_only image2d_t planeOutY,
    __write_only image2d_t planeOutUV,
    __write_only image2d_t planeMask,
    __read_only  image2d_t planeInY,
    __read_only  image2d_t planeInUV,
    uint width,
    uint height,
    uint keycolor0,
    uint keycolor1,
    uint rangeMin,
    uint rangeMax,
    uint rangeExt,
    uint lumaMin,
    uint enableEdge,
    uint enableAdvanced,
    uint debug
    )
{
    int2 pos = (int2)(get_global_id(0), get_global_id(1));
    if ((pos.x >= width) || (pos.y >= height))  return;

    uint4 dataIn = 0;
    dataIn.s0 = read_imageui(planeInY, sampler, pos).s0;
    int2 posUV = pos / 2;
    dataIn.s12 = read_imageui(planeInUV, sampler, posUV).s01;
    uint4 dataOut = dataIn;

    int keycolorU = (keycolor0 & 0x00FF00) >> 8;
    int keycolorV = (keycolor0 & 0x0000FF);
    int diffU = dataIn.s2 - keycolorU;
    int diffV = dataIn.s1 - keycolorV;
    uint diff = diffU * diffU + diffV * diffV;

    keycolorU = (keycolor1 & 0x00FF00) >> 8;
    keycolorV = (keycolor1 & 0x0000FF);
    diffU = dataIn.z - keycolorU;
    diffV = dataIn.y - keycolorV;
    uint diff2 = diffU * diffU + diffV * diffV;
    diff = min(diff, diff2);

    uint alpha = 255;

    int keycolorY = (keycolor0 & 0xFF0000) >> 16;
    uint diffLuma = abs((int)dataIn.s0 - keycolorY);
    if (diff <= rangeMin)   //Green
    {
        alpha = 0;
        dataOut = debug ? (uint4)(230, 128, 128, alpha) : (uint4)(0, 128, 128, alpha);
    }
//handle transparent area
    else if (enableAdvanced && (dataIn.s1 < 148) && (dataIn.s2 < 148))   //green
    {
		alpha = 128;
    }
    else if (diff <= rangeMax)   //Middle
    {
//        alpha = rangeMin + (diff - rangeMin) * (255 - rangeMin) / (rangeMax - rangeMin);
        alpha = 1 + (diff - rangeMin) * (255 - 1) / (rangeMax - rangeMin);
//        dataOut = debug ? (uint4)(128, 128, 128, alpha) : (uint4)(dataIn.x, dataIn.y, dataIn.z, alpha);
    }
    else
    {
       //for extended green range
        diff = dataIn.s1 * dataIn.s1 + dataIn.s2 * dataIn.s2;   //UV data position
        if (diff < rangeExt) //green area
        {
            alpha = 128;
            dataOut = debug ? (uint4)(255, 255, 0, alpha) :  (uint4)(dataIn.s0, 128, 128, alpha);
        }
        else
        {
            dataOut.s012 = dataIn.s012;
            dataOut.s3 = alpha;
        }
    }

#if 0
    if (enableEdge)  //enable edges
    {
        uint edge = EdgeSub(planeInY, pos);
        if (edge > lumaMin)	//expand to include edges
        {
            //dataOut = (uint4)(255, dataIn.y, dataIn.z, 255);
            dataOut.xyz = dataIn.xyz;
			alpha = max(alpha, (uint)128);
        }
    }
#endif
    //Y
    write_imageui(planeOutY, pos, dataOut);
    //UV
    dataOut.s01 = dataOut.s12;
    write_imageui(planeOutUV, posUV, dataOut);

    //Mask
    dataOut.s0 = alpha;
    write_imageui(planeMask, pos, dataOut);
}
//-------------------------------------------------------------------------------------------------
__kernel __attribute__((reqd_work_group_size(8, 8, 1)))
void Blur(
__write_only image2d_t planeOut,
__read_only  image2d_t planeIn,
uint width,
uint height,
uint kernelLength
)
{
    int2 posOut = (int2)(get_global_id(0), get_global_id(1));
    if ((posOut.x >= width) || (posOut.y >= height))  return;

    int2 posIn = posOut - (int2)(kernelLength / 2, kernelLength / 2);

    uint4 dataOut = 0;
    int posInX = posIn.x;
    for (int y = 0; y < kernelLength; y++, posIn.y++)
    {
        posIn.x = posInX;
        for (int x = 0; x < kernelLength; x++, posIn.x++)
        {
            dataOut.s0 += read_imageui(planeIn, sampler, posIn).s0;
        }
    }
    dataOut.s0 /= kernelLength * kernelLength;
    write_imageui(planeOut, posOut, dataOut);
}
//-------------------------------------------------------------------------------------------------
__kernel __attribute__((reqd_work_group_size(8, 8, 1)))
void Erode(
__write_only image2d_t planeOut,
__read_only  image2d_t planeIn,
uint width,
uint height,
uint kernelLength,
uint doDiff
)
{
    int2 posOut = (int2)(get_global_id(0), get_global_id(1));
    if ((posOut.x >= width) || (posOut.y >= height))  return;

    int2 posIn = posOut - (int2)(kernelLength / 2, kernelLength / 2);

    uint4 dataOut = 255;
    int posInX = posIn.x;
    for (int y = 0; y < kernelLength; y++, posIn.y++)
    {
        posIn.x = posInX;
        for (int x = 0; x < kernelLength; x++, posIn.x++)
        {
            dataOut.s0 = min(read_imageui(planeIn, sampler, posIn).s0, dataOut.s0);
            if (dataOut.s0 == 0) goto DoDiff;   //better performance
        }
    }
DoDiff:
    if (doDiff)
    {
        dataOut.s0 = read_imageui(planeIn, sampler, posOut).s0 - dataOut.s0; //diff
    }

    write_imageui(planeOut, posOut, dataOut);
}
//-------------------------------------------------------------------------------------------------
__kernel __attribute__((reqd_work_group_size(8, 8, 1)))
void Dilate(
__write_only image2d_t planeOut,
__read_only  image2d_t planeIn,
uint width,
uint height,
uint kernelLength
)
{
    int2 posOut = (int2)(get_global_id(0), get_global_id(1));
    if ((posOut.x >= width) || (posOut.y >= height))  return;

    int2 posIn = posOut - (int2)(kernelLength / 2, kernelLength / 2);

    uint4 dataOut = 0;
    int posInX = posIn.x;
    for (int y = 0; y < kernelLength; y++, posIn.y++)
    {
        posIn.x = posInX;
        for (int x = 0; x < kernelLength; x++, posIn.x++)
        {
            dataOut.s0 = max(read_imageui(planeIn, sampler, posIn).s0, dataOut.s0);
        }
    }

    dataOut.s0 = dataOut.s0 - read_imageui(planeIn, sampler, posOut).s0; //diff
    write_imageui(planeOut, posOut, dataOut);
}
//-------------------------------------------------------------------------------------------------
__kernel __attribute__((reqd_work_group_size(8, 8, 1)))
void Blend(
__write_only image2d_t planeOutY,
__write_only image2d_t planeOutUV,
__read_only  image2d_t planeInY,
__read_only  image2d_t planeInUV,
__read_only  image2d_t planeMaskSpill,
__read_only  image2d_t planeMaskBlur,
uint width,
uint height,
uint debug
)
{
    int2 pos = (int2)(get_global_id(0), get_global_id(1));
    if ((pos.x >= width) || (pos.y >= height))  return;

    uint4 dataOut = 0;
    int2 posUV = pos / 2;
    dataOut.s0 = read_imageui(planeInY, sampler, pos).s0;
    dataOut.s12 = read_imageui(planeInUV, sampler, posUV).s01;

    uint spill = read_imageui(planeMaskSpill, sampler, pos).s0;
    if (spill > 6)
    {
        if ((dataOut.s1 < 128) && (dataOut.s2 < 128))
        {
            uint alpha = read_imageui(planeMaskBlur, sampler, pos).s0;
            dataOut.s0 = (dataOut.s0 * alpha + 128 * (255 - alpha)) / 255;
            dataOut.s12 = 128;
        }
    }

    write_imageui(planeOutY, pos, dataOut);
    write_imageui(planeOutUV, posUV, dataOut.s1230);
}
//-------------------------------------------------------------------------------------------------
__kernel __attribute__((reqd_work_group_size(8, 8, 1)))
void BlendRGB(
__write_only image2d_t planeOut,
__read_only  image2d_t planeInY,
__read_only  image2d_t planeInUV,
__read_only  image2d_t planeMaskSpill,
__read_only  image2d_t planeMaskBlur,
uint width,
uint height,
uint greenReducing,
uint threshold,
uint threshold2,
uint keycolor,
uint debug
)
{
    int2 pos = (int2)(get_global_id(0), get_global_id(1));
    if ((pos.x >= width) || (pos.y >= height))  return;

    uint4 dataOut = 0;
    int2 posUV = pos / 2;
    dataOut.s0 = read_imageui(planeInY, sampler, pos).s0;
    dataOut.s12 = read_imageui(planeInUV, sampler, posUV).s01;
    uint alpha = read_imageui(planeMaskBlur, sampler, pos).s0;
    uint spill = read_imageui(planeMaskSpill, sampler, pos).s0;
    if (spill > 6)
    {
        if ((dataOut.s1 < 128) && (dataOut.s2 < 128))
        {
            dataOut.s0 = (dataOut.s0 * alpha + 128 * (255 - alpha)) / 255;
            dataOut.s12 = 128;
        }
    }

    dataOut = NV12toRGB(dataOut);

    if (greenReducing == 1)
    {
        dataOut = GreenReducing(dataOut, threshold, threshold2);
    }
    else if (greenReducing == 2) //advanced
    {
		dataOut = GreenReducingExt2(dataOut, keycolor);
    }

    dataOut.s3 = alpha;
    write_imageui(planeOut, pos, dataOut);
}
//-------------------------------------------------------------------------------------------------
__kernel __attribute__((reqd_work_group_size(8, 8, 1)))
void BlendBK(
__write_only image2d_t planeOutY,
__write_only image2d_t planeOutUV,
__read_only  image2d_t planeInY,
__read_only  image2d_t planeInUV,
__read_only  image2d_t planeInBKY,
__read_only  image2d_t planeInBKUV,
__read_only  image2d_t planeMaskSpill,
__read_only  image2d_t planeMaskBlur,
uint width,
uint height,
uint debug
)
{
    int2 pos = (int2)(get_global_id(0), get_global_id(1));
    if ((pos.x >= width) || (pos.y >= height))  return;

    uint4 dataOut = 0;
    uint4 dataBK = 0;
    int2 posUV = pos / 2;
    dataOut.s0 = read_imageui(planeInY, sampler, pos).s0;
    dataOut.s12 = read_imageui(planeInUV, sampler, posUV).s01;
    dataBK.s0 = read_imageui(planeInBKY, sampler, pos).s0;
    dataBK.s12 = read_imageui(planeInBKUV, sampler, posUV).s01;

    uint spill = read_imageui(planeMaskSpill, sampler, pos).s0;
    uint alpha = read_imageui(planeMaskBlur, sampler, pos).s0;
    if (spill > 6)
    {
        if ((dataOut.s1 < 128) && (dataOut.s2 < 128))
        {
            dataOut.s0 = (dataOut.s0 * alpha + 128 * (255 - alpha)) / 255;
            dataOut.s12 = dataBK.s12;
        }
    }

    dataOut.s0 = (dataOut.s0 * alpha + dataBK.s0 * (255 - alpha)) / 255;
    dataOut.s12 = (dataOut.s12 * alpha + dataBK.s12 * (255 - alpha)) / 255;  //???

    write_imageui(planeOutY, pos, dataOut);
    write_imageui(planeOutUV, posUV, dataOut.s1230);
}
//-------------------------------------------------------------------------------------------------
__kernel __attribute__((reqd_work_group_size(64, 1, 1)))
void BlendBKRGB(
__write_only image2d_t planeOut,
__read_only  image2d_t planeInY,
__read_only  image2d_t planeInUV,
__read_only  image2d_t planeInBKY,
__read_only  image2d_t planeInBKUV,
__read_only  image2d_t planeMaskSpill,
__read_only  image2d_t planeMaskBlur,
uint width,
uint height,
uint widthBK,
uint heightBK,
uint greenReducing,
uint threshold,
uint threshold2,
uint enableBokeh,
uint radiusBokeh,
uint keycolor,
uint posX,
uint posY,
uint debug
)
{
    int2 pos = (int2)(get_global_id(0), get_global_id(1));
    if ((pos.x >= widthBK) || (pos.y >= heightBK))  return;
	int2 posOut = pos;
    int2 posSrc = (int2)(pos.x - posX, pos.y - posY);

    uint4 dataOut = 0;
    uint4 dataBK = 0;
    int2 posUV = pos / 2;
    dataBK.s0 = read_imageui(planeInBKY, sampler, pos).s0;
    dataBK.s12 = read_imageui(planeInBKUV, sampler, posUV).s01;

    //blur the background
    if (enableBokeh==1)
    {
#if 1
       dataBK.xyz = BokehSub(planeInBKY, planeInBKUV, pos, radiusBokeh, heightBK);
#else
        dataBK.x = BokehLuma4(planeInBKY, pos, radiusBokeh);
//        dataBK.yz = BokehUV(planeInBKUV, pos/2, radiusBokeh/2+1);
#endif
    }

	if ((pos.x < posX) || (pos.y < posY) || (posSrc.x >= width) || (posSrc.y >= height))	//only background
	{
		dataOut = NV12toRGB(dataBK);
		write_imageui(planeOut, posOut, dataOut);
		return;
	}

	posUV = posSrc/2;
    dataOut.s0 = read_imageui(planeInY, sampler, posSrc).s0;
    dataOut.s12 = read_imageui(planeInUV, sampler, posUV).s01;
    uint spill = read_imageui(planeMaskSpill, sampler, posSrc).s0;
    uint alpha = read_imageui(planeMaskBlur, sampler, posSrc).s0;

    if (enableBokeh == 2)
    {
        dataOut.xyz = BokehSub(planeInY, planeInUV, posSrc, radiusBokeh, height);
    }

    //replace the UV with background, smooth the luma 
    if (spill > 6)
    {
        if ((dataOut.s1 < 128) && (dataOut.s2 < 128))
        {
            dataOut.s0 = (dataOut.s0 * alpha + dataBK.s0 * (255 - alpha)) / 255;
            dataOut.s12 = dataBK.s12;
        }
    }

    if (greenReducing == 0)
    {  //YUV blending
        dataOut.s012 = (dataOut.s012 * alpha + dataBK.s012 * (255 - alpha)) / 255;
        dataOut = NV12toRGB(dataOut);
    }
    else if (greenReducing == 1)
    {
        //RGB blending
        dataOut = NV12toRGB(dataOut);
        dataOut = GreenReducing(dataOut, threshold, threshold2);
        dataBK = NV12toRGB(dataBK);
        dataOut.s012 = (dataOut.s012 * alpha + dataBK.s012 * (255 - alpha)) / 255;
    }
    else	//advanced
    {
        //RGB blending
        dataOut = NV12toRGB(dataOut);
        dataBK = NV12toRGB(dataBK);

		if (alpha == 0)
		{
			dataOut = dataBK;
		}
		else if (alpha >= 254)
		{
		;
		}
		else
		{
			dataOut = GreenReducingExt(dataOut, dataBK, keycolor, radiusBokeh);
		}
    }
    write_imageui(planeOut, posOut, dataOut);
}
//-------------------------------------------------------------------------------------------------
__kernel __attribute__((reqd_work_group_size(8, 8, 1)))
void HistoUV(
__global uint *pOutHist,        ///< [out] histogram data
__read_only  image2d_t planeIn,
uint width,
uint height,
uint histoSize
)
{
    int2 posIn = (int2)(get_global_id(0), get_global_id(1));
    if ((posIn.x >= width) || (posIn.y >= height))  return;

//    uint dataY = read_imageui(planeIn, sampler, posIn).s0;
//    atomic_inc(pOutHist + dataY);
//    return;
    uint2 dataUV = read_imageui(planeIn, sampler, posIn).s01;

    if ((dataUV.s0 < 128) && (dataUV.s1 < 128))
    {
        uint posOut = dataUV.s1 * histoSize + dataUV.s0;
        atomic_inc(pOutHist + posOut);
    }
}
//average the luma values that have the same UV
//-------------------------------------------------------------------------------------------------
__kernel __attribute__((reqd_work_group_size(64, 1, 1)))
void HistoSort(
__global uint *pHistOut,        ///< Out, histogram data, 64x1
__global uint *pHistIn,         ///< In, histogram data 256x256, 128x128 for green
uint histoSize
)
{
    uint lid = get_local_id(0);
    uint length = histoSize / 64;
    uint pos = lid * length;
    uint posMax = 0;
    uint histMax = 0;
    __local uint posMaxList[64];
    //128x128 --> 64x1
    for (uint x = 0; x < length; x++, pos++)
    {
        if (pHistIn[pos] >= histMax)
        {
            histMax = pHistIn[pos];
            posMax = pos;
        }
    }
    posMaxList[lid] = posMax;
    barrier(CLK_LOCAL_MEM_FENCE);

    //64x1 --> 1
    if (lid == 0)
    {
        uint posMax = 0;
        uint histMax = 0;
        for (uint pos = 0; pos < 64; pos++)
        {
            uint posIn = posMaxList[pos];
            if (pHistIn[posIn] >= histMax)
            {
                histMax = pHistIn[posIn];
                posMax = posIn;
            }
        }
        pHistOut[0] = posMax;
        pHistOut[1] = histMax;
    }
}

//average the luma values that have the same UV
//-------------------------------------------------------------------------------------------------
__kernel __attribute__((reqd_work_group_size(64, 1, 1)))
void HistoLocateLuma(
__global uint *pLumaOut,        ///< [out] histogram data
__read_only  image2d_t planeInY,
__read_only  image2d_t planeInUV,
uint width,
uint height,
uint histoU,
uint histoV
)
{
    int2 posY = (int2)(get_global_id(0), get_global_id(1));
    if (posY.y >= height)  return;

    if ((posY.x == 0) && (posY.y == 0))
    {
        pLumaOut[0] = 0;
        pLumaOut[1] = 0;
    }
 
    int2 posUV;
    uint lid = get_local_id(0);
    uint width2 = (width + 64 - 1) / 64;
    uint luma = 0;
    uint lumaCount = 0;
    uint2 dataUV = 0;
    uint dataY = 0;
    __local uint lumaLDS[64];
    __local uint lumaCountLDS[64];

    posY.x = width2 * lid;
    for (uint x = 0; x < width2; x++, posY.x++)
    {
        if (posY.x >= width)  break;
        posUV = posY / 2;
        dataUV = read_imageui(planeInUV, sampler, posUV).s01;

        if ((dataUV.s0 == histoU) && (dataUV.s1 == histoV))
        {
            dataY = read_imageui(planeInY, sampler, posY).s0;
            luma += dataY;
            lumaCount++;
        }
    }

    lumaLDS[lid] = luma;
    lumaCountLDS[lid] = lumaCount;
    barrier(CLK_LOCAL_MEM_FENCE);

    //64x1 --> 1
    if (lid == 0)
    {
        luma = 0;
        lumaCount = 0;
        for (uint x = 0; x < 64; x++)
        {
            luma += lumaLDS[x];
            lumaCount += lumaCountLDS[x];
        }
        if (lumaCount > 0)
        {
#if 1
            luma /= lumaCount;
            atomic_add(&pLumaOut[0], luma);
            atomic_inc(&pLumaOut[1]);
#else
            atomic_add(&pLumaOut[0], luma);
            atomic_add(&pLumaOut[1], lumaCount);
#endif
        }
    }
}

uint4 NV12toRGB(uint4 dataIn)
{
    //BT709_FULL_RANGE
    const float3 coef[4] =
    {
        (float3){ -16.f, -128.f, -128.f },                                      //RgbOffset
        (float3){ 0.00456621f*255.f, 0.0f*255.f, 0.00703137f*255.f },           //Red Coef
        (float3){ 0.00456621f*255.f, -0.00083529f*255.f, -0.00209019f*255.f },  //Green Coef
        (float3){ 0.00456621f*255.f, 0.00828235f*255.f, 0.0f*255.f },           //Blue Coef
    };

    float4 dataYUV = convert_float4(dataIn);

    dataYUV.s012 += coef[0].s012;

    float4 dataRGB;
    dataRGB.s0 = dataYUV.s0 *coef[1].s0 + dataYUV.s1 * coef[1].s1 + dataYUV.s2 * coef[1].s2;
    dataRGB.s1 = dataYUV.s0 * coef[2].s0 + dataYUV.s1 * coef[2].s1 + dataYUV.s2 * coef[2].s2;
    dataRGB.s2 = dataYUV.s0 * coef[3].s0 + dataYUV.s1 * coef[3].s1 + dataYUV.s2 * coef[3].s2;
    dataRGB.s3 = 255.f;
    dataRGB = clamp(dataRGB, 0.f, 255.f);
    return convert_uint4(dataRGB);
}

uint4 GreenReducing(uint4 dataIn, int threshold, int threshold2)
{
#if 0
     dataIn.y -= (dataIn.y > threshold) ? threshold : dataIn.y;
#elif 0  
    int diff1 = dataIn.s1 - dataIn.s2 * (20 + threshold2) / 40  ; //G-R
    int diff2 = dataIn.s1 - dataIn.s0 * (20 + threshold2) / 40; //G-B

    if ((diff1 > 0) && (diff2 > 0))
    {
        int diff = max(diff1, diff2);
        diff *= threshold;
        diff /= 10;
        dataIn.y -= (dataIn.y > diff) ? diff : dataIn.y;
    }
    else if (diff2 > 0)
    {
        threshold *= diff1;
        threshold /= 255;
    }
#else
    int diff1 = dataIn.s1 - dataIn.s2; //G-R
    int diff2 = dataIn.s1 - dataIn.s0; //G-B
    int diff = max(diff1, diff2) / 2;

    if ((diff1 > 0) && (diff2 > 0))
    {
        dataIn.y -= (diff > threshold) ? threshold : diff;
    }
    else if ((diff1 > 1) || (diff2 > 1))
    {
        float diff2 = (float)diff / threshold2;
        diff2 = diff2 * diff2 * diff2 * threshold2;     //reduce the adjustment in the less polluted area
        dataIn.y -= (diff > threshold2) ? diff : (int)diff2;
    }
#endif
    return dataIn;
}

//Pixel.r - alpha * PixelBack.r > 0
//Pixel.g - alpha * PixelBack.g > 0
//Pixel.b - alpha * PixelBack.b > 0

//Pixel.g - alpha * PixelBack.g < Pixel.r - alpha * PixelBack.r
//Pixel.g - alpha * PixelBack.g < Pixel.b - alpha * PixelBack.b
//Blend with backgrond
uint4 GreenReducingExt(uint4 dataIn, uint4 dataBK, uint keycolor, uint delta)
{
	//not green dominate
	if ((dataIn[1] < dataIn[0]) && (dataIn[1] < dataIn[2])) 
	{
		return dataIn;
	}

	uint4 dataKey;
	dataKey.s2 = (keycolor & 0x00FF00) >> 8;	//U
	dataKey.s1 = (keycolor & 0x0000FF);			//V
	dataKey.s0 = (keycolor & 0xFF0000) >> 16;	//Y
	dataKey = NV12toRGB(dataKey);

	float alphaMax = min((float)dataIn[0] / (float)dataKey[0], (float)dataIn[1] / (float)dataKey[1]);
	alphaMax = min(alphaMax, (float)dataIn[2] / (float)dataKey[2]);
#if 1
	float alphaMin1 = ((float)dataIn[1] - (float)dataIn[2]) / ((float)dataKey[1] - (float)dataKey[2]);	//(G-R)/Gkey-Rkey)
	float alphaMin2 = ((float)dataIn[1] - (float)dataIn[0]) / ((float)dataKey[1] - (float)dataKey[0]);	//(G-B)/Gkey-Bkey)
#else	//adjust threshold
	delta = delta;// * dataIn.s1 / 255;	//
	float alphaMin1 = ((float)dataIn[1] + delta - (float)dataIn[2]) / ((float)dataKey[1] - (float)dataKey[2]);	//(G-R)/Gkey-Rkey)
	float alphaMin2 = ((float)dataIn[1] + delta - (float)dataIn[0]) / ((float)dataKey[1] - (float)dataKey[0]);	//(G-B)/Gkey-Bkey)
#endif
	float alphaMin = max(0.0f, min(alphaMin1, alphaMin2));

	float alpha2 = min(alphaMin, alphaMax);
//aggressive
//	float alpha2 = min(alphaMin, 1.0f);

	dataKey.s0 = dataKey.s0 * alpha2;
	dataKey.s1 = dataKey.s1 * alpha2;
	dataKey.s2 = dataKey.s2 * alpha2;

	dataIn.s012 = dataIn.s012  - dataKey.s012;
//for aggressive case
//	dataIn.s0 = (dataIn.s0 < dataKey.s0) ? 0 : dataIn.s0 - dataKey.s0;
//	dataIn.s1 = (dataIn.s1 < dataKey.s1) ? 0 : dataIn.s1 - dataKey.s1;
//	dataIn.s2 = (dataIn.s2 < dataKey.s2) ? 0 : dataIn.s2 - dataKey.s2;


	dataBK.s0 = dataBK.s0 * alpha2;
	dataBK.s1 = dataBK.s1 * alpha2;
	dataBK.s2 = dataBK.s2 * alpha2;

	dataIn.s012 += dataBK.s012;
    return dataIn;
}

//Pixel.r - alpha * PixelBack.r > 0
//Pixel.g - alpha * PixelBack.g > 0
//Pixel.b - alpha * PixelBack.b > 0

//Pixel.g - alpha * PixelBack.g < Pixel.r - alpha * PixelBack.r
//Pixel.g - alpha * PixelBack.g < Pixel.b - alpha * PixelBack.b

uint4 GreenReducingExt2(uint4 dataIn, uint keycolor)
{
	//not green dominate
	if ((dataIn[1] < dataIn[0]) && (dataIn[1] < dataIn[2])) 
	{
		return dataIn;
	}

	uint4 dataKey;
	dataKey.s2 = (keycolor & 0x00FF00) >> 8;	//U
	dataKey.s1 = (keycolor & 0x0000FF);			//V
	dataKey.s0 = (keycolor & 0xFF0000) >> 16;	//Y
	dataKey = NV12toRGB(dataKey);

	float alphaMax = min((float)dataIn[0] / (float)dataKey[0], (float)dataIn[1] / (float)dataKey[1]);
	alphaMax = min(alphaMax, (float)dataIn[2] / (float)dataKey[2]);
	float alphaMin1 = ((float)dataIn[1] - (float)dataIn[2]) / ((float)dataKey[1] - (float)dataKey[2]);	//(G-R)/Gkey-Rkey)
	float alphaMin2 = ((float)dataIn[1] - (float)dataIn[0]) / ((float)dataKey[1] - (float)dataKey[0]);	//(G-B)/Gkey-Bkey)
	float alphaMin = max(0.0f, min(alphaMin1, alphaMin2));

	float alpha2 = min(alphaMin, alphaMax);

	dataKey.s0 = dataKey.s0 * alpha2;
	dataKey.s1 = dataKey.s1 * alpha2;
	dataKey.s2 = dataKey.s2 * alpha2;

	dataIn.s012 = dataIn.s012  - dataKey.s012;

    return dataIn;
}

uint4 GreenReducing2(uint4 dataIn, int threshold, int threshold2, int mask)
{
    if (mask < 6) return dataIn;
    threshold *= mask;
    threshold /= 255;
    dataIn.y -= (dataIn.y > threshold) ? threshold : dataIn.y;
    return dataIn;
}

//generate RGB color map 
uint4 ColorMap(int2 pos, int width)
{
    int zCountH = width / 255;
    uint4 dataRGBA = 0;
    dataRGBA.x = pos.x % 255;
    dataRGBA.y = pos.y % 255;
    dataRGBA.z = pos.y / 255 * zCountH + pos.x / 255;

    dataRGBA.z *= 2;
    return dataRGBA;
}

//-------------------------------------------------------------------------------------------------
__kernel __attribute__((reqd_work_group_size(8, 8, 1)))
void Edge(
__write_only image2d_t planeOut,
__read_only  image2d_t planeInY,
uint width,
uint height
)
{
    int2 posOut = (int2)(get_global_id(0), get_global_id(1));

    if ((posOut.x >= width) || (posOut.y >= height))  return;
    uint4 dataOut = EdgeSub(planeInY, posOut);
    write_imageui(planeOut, posOut, dataOut);
}

uint EdgeSub(__read_only  image2d_t planeInY, int2 pos)
{
    int weights[3] = { -1, -2, -1 };
    uint dx = 0;
    uint dy = 0;

    int2 posEdge = pos - 1;
    for (int y = 0; y < 2; y++, posEdge.y++)
    {
        dx += weights[y] * read_imageui(planeInY, sampler, posEdge).x;
    }

    posEdge.x += 2;
    posEdge.y = pos.y - 1;
    for (int y = 0; y < 2; y++, posEdge.y++)
    {
        dx -= weights[y] * read_imageui(planeInY, sampler, posEdge).x;
    }

    posEdge = pos - 1;
    for (int x = 0; x < 2; x++, posEdge.x++)
    {
        dy += weights[x] * read_imageui(planeInY, sampler, posEdge).x;
    }

    posEdge.y += 2;
    posEdge.x = pos.x - 1;
    for (int x = 0; x < 2; x++, posEdge.x++)
    {
        dy -= weights[x] * read_imageui(planeInY, sampler, posEdge).x;
    }

    return native_sqrt(dx*dx + dy*dy);
}

//Gaussian blur;

uint3 BokehSub(__read_only  image2d_t planeY, __read_only  image2d_t planeUV, int2 posIn, int radiusBokeh, int height)
{
    uint3 dataOut = 0;
    int2 pos = posIn;
#if 1 //circular
    int posX = pos.x - radiusBokeh;
    pos.y -= radiusBokeh;
    float3 data = 0;
    uint3 dataH = 0;
    float weight = 0;
    float coef = 0;
    for (int y = -radiusBokeh; y <= radiusBokeh; y++, pos.y++)
    {
//      pos.x = posX;
        if (pos.y >= height) continue; //CLK_ADDRESS_CLAMP_TO_EDGE not working?
      int radiusH = native_sqrt(radiusBokeh*radiusBokeh - y*y);
      pos.x = posIn.x - radiusH;
      for (int x = -radiusH; x <= radiusH; x++, pos.x++)
//        for (int x = -radiusBokeh; x <= radiusBokeh; x++, pos.x++)
        {
            //float myDistance = distance(convert_float2(pos), convert_float2(posIn));
//            float myDistance = length((float2)(x, y));
//            if (myDistance >= (float)radiusBokeh) continue;
            dataH.x = read_imageui(planeY, sampler, pos).x;
            dataH.yz = read_imageui(planeUV, sampler, pos / 2).xy;
            coef = (float)dataH.x * (float)dataH.x + 25.f;
            data.xyz += convert_float3(dataH.xyz) * coef;
            weight += coef;
        }
    }
    data.xyz /= weight;
    dataOut.xyz = convert_uint3(data.xyz);
#elif 0 //box filter
    kernelLength /= 2;
    int posX = pos.x - kernelLength;
    pos.y -= kernelLength;
    for (int y = -kernelLength; y <= kernelLength; y++, pos.y++)
    {
        pos.x = posX;
        for (int x = -kernelLength; x <= kernelLength; x++, pos.x++)
        {
            dataOut.x += read_imageui(planeY, sampler, pos).x;
            dataOut.yz += read_imageui(planeUV, sampler, pos / 2).xy;
        }
    }
    kernelLength = 2 * kernelLength + 1;
    uint weight = kernelLength * kernelLength;
    dataOut.xyz /= weight;
#else  //gaussian 5x5
//    const float coef[11] = { 0.0088122291f, 0.027143577f, 0.065114059f, 0.12164907f, 0.17699835f, 0.20056541f, 0.17699835f, 0.12164907f, 0.065114059f, 0.027143577f, 0.0088122291f};
    float4 data = 0;

    float coef[32];
    float theta = 10.0f;
    float weight = 1.0f;// / sqrt(2.0f * 3.1415926f) / theta;
    float sum = 0;

    kernelLength = 5;
    int idx = 0;
    for (int x = -kernelLength; x <= kernelLength; x++, idx++)
    {
        coef[idx] = weight * native_exp(-x * x / 2.0f / theta / theta);
        sum += coef[idx];
    }

    idx = 0;
    for (int x = -kernelLength; x <= kernelLength; x++, idx++)
    {
        coef[idx] /= sum;
    }

    pos.y -= kernelLength;
    int posX = pos.x - kernelLength;
    uint4 dataIn = 0;
    kernelLength = kernelLength * 2 + 1;
#if 0
        for (int y = 0; y < kernelLength; y++, pos.y++)
        {
            pos.x = posX;
            float4 dataH = 0;
            for (int x = 0; x < kernelLength; x++, pos.x++)
            {
                dataIn.x = read_imageui(planeY, sampler, pos).x;
                dataIn.yz = read_imageui(planeUV, sampler, pos / 2).xy;
                dataH.xyz += (float3)(dataIn.x, dataIn.y, dataIn.z) *coef[x];
            }
            data.xyz += dataH.xyz * coef[y];
        }
        dataOut.xyz = (uint3)(data.x, data.y, data.z);
    #else //gamma like correction first, enhance highlights
    float sumH = 0;
    float sumV = 0;
    for (int y = 0; y < kernelLength; y++, pos.y++)
        {
            pos.x = posX;
            float4 dataH = 0;
            sumH = 0;
            for (int x = 0; x < kernelLength; x++, pos.x++)
            {
                float myDistance = distance(convert_float2(pos), convert_float2(posIn));
                if (myDistance >= (float)kernelLength/2.0f) continue;
                dataIn.x = read_imageui(planeY, sampler, pos).x;
                dataIn.yz = read_imageui(planeUV, sampler, pos / 2).xy;
                dataIn.xyz *= dataIn.xyz*dataIn.xyz;
                dataH.xyz += convert_float3(dataIn.xyz) *coef[x];
                sumH += coef[x];
            }
            dataH.xyz /= sumH;
            data.xyz += dataH.xyz * coef[y];
            sumV += coef[y];
        }
    data.xyz /= sumV;
    data.xyz = native_powr(data.xyz, 1.0f / 3.0f);
        dataOut.xyz = (uint3)(data.x, data.y, data.z);
    #endif
#endif
    return dataOut;
}

//64x1 5x5 square optimization, from 7ms --> 1ms with 4k
uint3 BokehSub2(__read_only  image2d_t planeY, __read_only  image2d_t planeUV, int2 posIn, int radiusBokeh)
{
    uint4 dataOut = 0;
    int2 pos = posIn;
//not circular
    int posY = pos.y - radiusBokeh;
    float3 data = 0;
    uint3 dataH = 0;
    float weight = 0;
    float coef = 0;
    __local float3 dataV[64+64]; //max 32 radius
    int localX = get_local_id(0);
    pos.x -= radiusBokeh + localX; //workgroup startX

    int offset = 2 * localX;
    int width = 64 + 2 * radiusBokeh;
    pos.x += offset; //each work item process 2 colums

    for (int x = 0; x < 2; x++, pos.x++, offset++)
    {
        if (offset > width) break;
        pos.y = posY;
        weight = 0;
        for (int y = -radiusBokeh; y <= radiusBokeh; y++, pos.y++)
        {
            dataH.x = read_imageui(planeY, sampler, pos).x;
            dataH.yz = read_imageui(planeUV, sampler, pos / 2).xy;
            coef = (float)dataH.x * (float)dataH.x;
            data += convert_float3(dataH) * coef;
            weight += coef;
         }
        data /= weight;
        dataV[offset] = data;
    }

    barrier(CLK_LOCAL_MEM_FENCE);
    data = 0;
    pos.x = localX;
    weight = 0;
    for (int x = -radiusBokeh; x <= radiusBokeh; x++, pos.x++)
    {
        coef = 1;// radiusBokeh - abs(x) + 1;
        data += dataV[pos.x] * coef;
        weight += coef;
    }

    data /= weight;
    return convert_uint3(data);
}

//64x1 5x5 square optimization, from 7ms --> 1ms with 4k
uint3 BokehSub4(__read_only  image2d_t planeY, __read_only  image2d_t planeUV, int2 posIn, int radiusBokeh)
{
    int2 pos = posIn;
    uint3 dataH = 0;
    __local float3 dataLds[64 + 32 * 2]; //max 64 radius
    __local float coef[64 + 32 * 2];    //max 64 radius
    int localX = get_local_id(0);

    int posX = pos.x - radiusBokeh;
    int width = 64 + 2 * radiusBokeh;

    float3 data = 0;
    float weight = 0;
    pos.y -= radiusBokeh;
    for (int y = -radiusBokeh; y <= radiusBokeh; y++, pos.y++)
    {
        pos.x = posX;
        //read the whole line, left side 64 pixels
        dataH.x = read_imageui(planeY, sampler, pos).x;
        dataH.yz = read_imageui(planeUV, sampler, pos / 2).xy;
        dataLds[localX] = convert_float3(dataH);
        coef[localX] = (float)dataH.x*dataH.x;

        //read the whole line, rest pixels, 2 * radiusBokeh

        pos.x += 64;
        if ((64 + localX) < width)
        {
            dataH.x = read_imageui(planeY, sampler, pos).x;
            dataH.yz = read_imageui(planeUV, sampler, pos / 2).xy;
            dataLds[64 + localX] = convert_float3(dataH);
            coef[64 + localX] = (float)dataH.x*dataH.x;
        }
        barrier(CLK_LOCAL_MEM_FENCE);

        //accumulate
        //        int radiusH = radiusBokeh - abs(y); //diamond shap, performance 1.4ms
        int radiusH = native_sqrt(radiusBokeh*radiusBokeh - y*y);
        //        int radiusH = distance((float)radiusBokeh, (float)y);   //use lookup table for further optimization
        int posH = localX + radiusBokeh - radiusH;
        for (int x = -radiusH; x <= radiusH; x++, posH++)
        {
            data += dataLds[posH] * coef[posH];
            weight += coef[posH];
        }
    }
    data /= weight;
    return convert_uint3(data);
}

//64x1 5x5 square optimization, from 7ms --> 1ms with 4k
uint BokehLuma2(__read_only  image2d_t planeY, int2 posIn, int radiusBokeh)
{
    uint4 dataOut = 0;
    int2 pos = posIn;
    //not circular
    int posY = pos.y - radiusBokeh;
    float data = 0;
    uint dataH = 0;
    float weight = 0;
    float coef = 0;
    __local float dataV[64 + 64]; //max 32 radius
    int localX = get_local_id(0);
    pos.x -= radiusBokeh + localX; //workgroup startX

    int offset = 2 * localX;
    int width = 64 + 2 * radiusBokeh;
    pos.x += offset; //each work item process 2 colums

    for (int x = 0; x < 2; x++, pos.x++, offset++)
    {
        if (offset > width) break;
        pos.y = posY;
        weight = 0;
        for (int y = -radiusBokeh; y <= radiusBokeh; y++, pos.y++)
        {
            dataH = read_imageui(planeY, sampler, pos).x;
            coef = (float)dataH * (float)dataH;
            data += convert_float(dataH) * coef;
            weight += coef;
        }
        data /= weight;
        dataV[offset] = data;
    }

    barrier(CLK_LOCAL_MEM_FENCE);
    data = 0;
    pos.x = localX;
    weight = 0;
    for (int x = -radiusBokeh; x <= radiusBokeh; x++, pos.x++)
    {
        coef = 1;// radiusBokeh - abs(x) + 1;
        data += dataV[pos.x] * coef;
        weight += coef;
    }

    data /= weight;
    return convert_uint(data);
}
//load into LDS first, not much performance gain
uint BokehLuma3(__read_only  image2d_t planeY, int2 posIn, int radiusBokeh)
{
    uint4 dataOut = 0;
    int2 pos = posIn - radiusBokeh;
    //load into LDS first
    __local half dataV[12][64+12]; //max 6 radius
    int localX = get_local_id(0);
    int width = 64 + 2 * radiusBokeh;

    float dataH = 0;

        
    for (int y = -radiusBokeh; y <= radiusBokeh; y++, pos.y++)
    {
        dataH = (half)read_imageui(planeY, sampler, pos).x;
        dataV[radiusBokeh + y][localX] = dataH;
    }

    pos.y = posIn.y - radiusBokeh;
    pos.x += 64;
    if ((64 + localX) < width)
    {
        for (int y = -radiusBokeh; y <= radiusBokeh; y++, pos.y++)
        {
            dataH = (half)read_imageui(planeY, sampler, pos).x;
            dataV[radiusBokeh + y][64 + localX] = dataH;
        }
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    //circular
    int posX = posIn.x - radiusBokeh;
    pos.y = posIn.y - radiusBokeh;
    float data = 0;
    float weight = 0;
    float coef = 0;
    for (int y = -radiusBokeh; y <= radiusBokeh; y++, pos.y++)
    {
        pos.x = posX;
        for (int x = -radiusBokeh; x <= radiusBokeh; x++, pos.x++)
        {
            float myDistance = distance(convert_float2(pos), convert_float2(posIn));
            if (myDistance >= (float)radiusBokeh) continue;
            dataH = dataV[radiusBokeh + y][radiusBokeh + x + localX];
            coef = (float)dataH * (float)dataH;
            data += convert_float(dataH) * coef;
            weight += coef;
        }
    }
    data /= weight;
    return convert_uint(data);
}

//64x1 5x5 square optimization, from 7ms --> 1ms with 4k
uint BokehLuma4(__read_only  image2d_t planeY, int2 posIn, int radiusBokeh)
{
    int2 pos = posIn;
    float dataH = 0;
    __local float dataLds[64 + 32*2]; //max 64 radius
    __local float coef[64 + 32*2];    //max 64 radius
    int localX = get_local_id(0);
    int posX = pos.x - radiusBokeh;
    int width = 64 + 2 * radiusBokeh;

    float data = 0;
    float weight = 0;
    pos.y -= radiusBokeh;
    float dataTemp = 0;
    for (int y = -radiusBokeh; y <= radiusBokeh; y++, pos.y++)
    {
        pos.x = posX;
        //read the whole line, left side 64 pixels
        dataH = (float)read_imageui(planeY, sampler, pos).x;
        dataTemp = dataH*dataH;
        coef[localX] = dataTemp;
        dataLds[localX] = dataH * dataTemp;

        //read the whole line, rest pixels, 2 * radiusBokeh
        int localX2 = localX + 64;
        pos.x += 64;
        if (localX2 < width)
        {
            dataH = (float)read_imageui(planeY, sampler, pos).x;
            dataTemp = dataH*dataH;
            coef[localX2] = dataTemp;
            dataLds[localX2] = dataH * dataTemp;
        }
        barrier(CLK_LOCAL_MEM_FENCE);

        //accumulate
        int radiusH = native_sqrt(radiusBokeh*radiusBokeh - y*y);
//        int radiusH = distance((float)radiusBokeh, (float)y);   //use lookup table for further optimization
        int posH = localX + radiusBokeh - radiusH;
        for (int x = -radiusH; x <= radiusH; x++, posH++)
        {
            data += dataLds[posH] *coef[posH];
            weight += coef[posH];
        }
    }
    data /= weight;
    return convert_uint(data);
}

uint BokehLuma(__read_only  image2d_t planeY, int2 posIn, int radiusBokeh)
{
    uint4 dataOut = 0;
    int2 pos = posIn;
    //circular
    int posX = pos.x - radiusBokeh;
    pos.y -= radiusBokeh;
    float data = 0;
    uint dataH = 0;
    float weight = 0;
    float coef = 0;
    for (int y = -radiusBokeh; y <= radiusBokeh; y++, pos.y++)
    {
        pos.x = posX;
        for (int x = -radiusBokeh; x <= radiusBokeh; x++, pos.x++)
        {
            float myDistance = distance(convert_float2(pos), convert_float2(posIn));
            if (myDistance >= (float)radiusBokeh) continue;
            dataH = read_imageui(planeY, sampler, pos).x;
            coef = (float)dataH * (float)dataH;
            data += convert_float(dataH) * coef;
            weight += coef;
        }
    }
    data /= weight;
    return convert_uint(data);
}

uint2 BokehUV(__read_only  image2d_t planeUV, int2 posIn, int radiusBokeh)
{
    uint4 dataOut = 0;
    int2 pos = posIn;
    //circular
    int posX = pos.x - radiusBokeh;
    pos.y -= radiusBokeh;
    float2 data = 0;
    uint2 dataH = 0;
    float weight = 0;
    float coef = 0;
    for (int y = -radiusBokeh; y <= radiusBokeh; y++, pos.y++)
    {
        pos.x = posX;
        for (int x = -radiusBokeh; x <= radiusBokeh; x++, pos.x++)
        {
            float myDistance = distance(convert_float2(pos), convert_float2(posIn));
            if (myDistance >= (float)radiusBokeh) continue;
            dataH = read_imageui(planeUV, sampler, pos).xy;
            data += convert_float2(dataH);
            weight++;
        }
    }
    data /= weight;
    return convert_uint2(data);
}
//64x1 5x5 square optimization, from 7ms --> 1ms with 4k
uint BokehLuma5(__read_only  image2d_t planeY, __local float coef[], int2 posIn, int radiusBokeh)
{
    int2 pos = posIn;
    int localX = get_local_id(0);
    int localX2 = localX + 64;
    int posX = pos.x - radiusBokeh;
    int width = 64 + 2 * radiusBokeh;

    float data = 0;
    float weight = 0;
    float dataTemp = 0;
    float dataTemp2 = 0;
    //Y
    pos.y -= radiusBokeh;
    for (int y = -radiusBokeh; y <= radiusBokeh; y++, pos.y++)
    {
        pos.x = posX;
        //read the whole line, left side 64 pixels
        coef[localX] = (float)read_imageui(planeY, sampler, pos).x;

        //read the whole line, rest pixels, 2 * radiusBokeh
        pos.x += 64;
        if (localX2 < width)
        {
            coef[localX2] = (float)read_imageui(planeY, sampler, pos).x;
        }
        barrier(CLK_LOCAL_MEM_FENCE);

        //accumulate
        int radiusH = native_sqrt(radiusBokeh*radiusBokeh - y*y);
        int posH = localX + radiusBokeh - radiusH;
        for (int x = -radiusH; x <= radiusH; x++, posH++)
        {
            dataTemp = coef[posH];
            dataTemp2 = dataTemp * dataTemp;
            data += dataTemp*dataTemp2;// *coef[posH];
            weight += dataTemp2;// coef[posH];
        }
    }
    data /= weight;

    return convert_uint(data);
}

uint2 BokehUV5(__read_only  image2d_t planeUV, __local float coef[], int2 posIn, int radiusBokeh)
{
    int2 pos = posIn;
    int posX = pos.x - radiusBokeh;
    int localX = get_local_id(0);

    float2 data = 0;
    float2 dataH = 0;
    float weight = 0;
    float dataTemp = 0;
    //Y
    pos.y -= radiusBokeh;
    for (int y = -radiusBokeh; y <= radiusBokeh; y++, pos.y++)
    {
        pos.x = posX;
        //accumulate
        int radiusH = native_sqrt(radiusBokeh*radiusBokeh - y*y);
        int posH = localX + 2*radiusBokeh - radiusH;
        pos.x = posIn.x - radiusH;
        for (int x = -radiusH; x <= radiusH; x++, posH++, pos.x++)
        {
            dataH = convert_float2(read_imageui(planeUV, sampler, pos).xy);
            dataTemp = coef[posH];
            dataTemp *= dataTemp;
            data += dataH*dataTemp;// *coef[posH];
            weight += dataTemp;// coef[posH];
        }
    }
    data /= weight;
    return convert_uint2(data);
}
//-------------------------------------------------------------------------------------------------
__kernel __attribute__((reqd_work_group_size(64, 1, 1)))
void Bokeh(
__write_only image2d_t planeOutY,
__write_only image2d_t planeOutUV,
__read_only  image2d_t planeInY,
__read_only  image2d_t planeInUV,
uint width,
uint height,
uint radiusBokeh
)
{
    int2 pos = (int2)(get_global_id(0), get_global_id(1));
    if ((pos.x >= width) || (pos.y >= height))  return;
    __local float coef[64 + 64 * 2];    //max 64 radius

   //blur the background
    uint4 data = 0;
//    data.x = BokehLuma(planeInY, pos, radiusBokeh);
    data.x = BokehLuma5(planeInY, coef, pos, radiusBokeh);
    write_imageui(planeOutY, pos, data);

    if (!(pos.x % 2) && !(pos.y % 2))
    {
        pos /= 2;
//        data.yz = read_imageui(planeInUV, sampler, pos).xy;
        data.yz = BokehUV5(planeInUV, coef, pos, radiusBokeh / 2);
//        data.yz = BokehUV(planeInUV, pos, radiusBokeh/2);
        write_imageui(planeOutUV, pos, data.yzyz);
    }
}
