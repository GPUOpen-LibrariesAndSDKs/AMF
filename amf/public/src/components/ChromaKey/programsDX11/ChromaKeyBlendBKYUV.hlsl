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
// Copyright (c) 2019 Advanced Micro Devices, Inc. All rights reserved.
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

RWTexture2D<unorm float4> planeOut    : register(u0);
#if defined(YUV422) || defined(YUV444) || defined(RGB_FP16) //YUV_PACKED, RGB
    Texture2D<unorm float4> planeIn        : register(t0);
    Texture2D<unorm float>  planeInBKY     : register(t1);
    Texture2D<unorm float2> planeInBKUV    : register(t2);
    Texture2D<unorm float>  planeMaskSpill : register(t3);
    Texture2D<unorm float>  planeMaskBlur  : register(t4);
#else
    Texture2D<unorm float>  planeInY      : register(t0);
    Texture2D<unorm float2> planeInUV     : register(t1);
    Texture2D<unorm float>  planeInBKY    : register(t2);
    Texture2D<unorm float2> planeInBKUV   : register(t3);
    Texture2D<unorm float>  planeMaskSpill: register(t4);
    Texture2D<unorm float>  planeMaskBlur : register(t5);
#endif

    cbuffer Params : register(b0)
{
    uint width;
    uint height;
    uint widthBK; 
    uint heightBK;
    uint greenReducing;
    uint threshold;
    uint threshold2;
    uint enableBokeh;
    uint radiusBokeh;
    uint keycolor;
    int posXStart;
    int posYStart;
    uint colorTransferSrc;
    uint colorTransferBK;
    uint colorTransferDst;
    uint alphaFromSrc;
    uint debug;
};

#include "ChromaKeyProcessCSC.hlsl"

//Pixel.r - alpha * PixelBack.r > 0
//Pixel.g - alpha * PixelBack.g > 0
//Pixel.b - alpha * PixelBack.b > 0

//Pixel.g - alpha * PixelBack.g < Pixel.r - alpha * PixelBack.r
//Pixel.g - alpha * PixelBack.g < Pixel.b - alpha * PixelBack.b

float4 GreenReducingExt(float4 dataIn, float4 dataBK, uint keycolor, uint delta)
{
    //not green dominate
    if ((dataIn.y < dataIn.x) && (dataIn.y < dataIn.z))
    {
        return dataIn;
    }

    float4 dataKey;
    dataKey.z = (float)((keycolor >> 10) & 0x000003FF) / 1024.f;	//U
    dataKey.y = (float)(keycolor & 0x000003FF) / 1024.f;			//V
    dataKey.x = (float)((keycolor >> 20) & 0x000003FF) / 1024.f;	//Y
    dataKey = NV12toRGB(dataKey);

    float alphaMax = min((float)dataIn.x / (float)dataKey.x, (float)dataIn.y / (float)dataKey.y);
    alphaMax = min(alphaMax, (float)dataIn.z / (float)dataKey.z);
#if 1
    float alphaMin1 = ((float)dataIn.y - (float)dataIn.z) / ((float)dataKey.y - (float)dataKey.z);	//(G-R)/Gkey-Rkey)
    float alphaMin2 = ((float)dataIn.y - (float)dataIn.x) / ((float)dataKey.y - (float)dataKey.x);	//(G-B)/Gkey-Bkey)
#else	//adjust threshold
    delta = delta;// * dataIn.s1 / 255;	//
    float alphaMin1 = ((float)dataIn.y + delta - (float)dataIn.z) / ((float)dataKey.y - (float)dataKey.z);	//(G-R)/Gkey-Rkey)
    float alphaMin2 = ((float)dataIn.y + delta - (float)dataIn.x) / ((float)dataKey.y - (float)dataKey.x);	//(G-B)/Gkey-Bkey)
#endif
    float alphaMin = max(0.0f, min(alphaMin1, alphaMin2));

    float alpha2 = min(alphaMin, alphaMax);
    //aggressive
    //	float alpha2 = min(alphaMin, 1.0f);

    dataKey.x = dataKey.x * alpha2;
    dataKey.y = dataKey.y * alpha2;
    dataKey.z = dataKey.z * alpha2;

    dataIn.xyz = dataIn.xyz - dataKey.xyz;
    //for aggressive case
    //	dataIn.s0 = (dataIn.s0 < dataKey.s0) ? 0 : dataIn.s0 - dataKey.s0;
    //	dataIn.s1 = (dataIn.s1 < dataKey.s1) ? 0 : dataIn.s1 - dataKey.s1;
    //	dataIn.s2 = (dataIn.s2 < dataKey.s2) ? 0 : dataIn.s2 - dataKey.s2;


    dataBK.x = dataBK.x * alpha2;
    dataBK.y = dataBK.y * alpha2;
    dataBK.z = dataBK.z * alpha2;

    dataIn.xyz += dataBK.xyz;
    return dataIn;
}

float3 BokehSub(Texture2D<unorm float>  planeY, Texture2D<unorm float2>  planeUV, int3 posIn, int radiusBokeh, int height)
{
    int3 pos = posIn;
    //circular
    int posX = pos.x - radiusBokeh;
    pos.y -= radiusBokeh;
    float4 data = 0;
    float4 dataH = 0;
    float weight = 0;
    float coef = 0;
    int3 posUV = 0;
#if 1
    //optimized circular filter
    for (int y = -radiusBokeh; y <= radiusBokeh; y++, pos.y++)
    {
        if (pos.y >= height) continue; //CLK_ADDRESS_CLAMP_TO_EDGE not working?

        int radiusH = sqrt(radiusBokeh*radiusBokeh - y * y);
        pos.x = posIn.x - radiusH;
        for (int x = -radiusH; x <= radiusH; x++, pos.x++)
        {
            posUV = int3(pos.x / 2, pos.y / 2, 0);
            dataH.x = (float)planeY.Load(pos);
            dataH.yz = (float2)planeUV.Load(posUV);
            coef = (float)dataH.x * (float)dataH.x + 25.0f / 255.0f;
            data.xyz += dataH.xyz * coef;
            weight += coef;
        }
    }
#else
    //circular filter
    for (int y = -radiusBokeh; y <= radiusBokeh; y++, pos.y++)
    {
        pos.x = posX;
        for (int x = -radiusBokeh; x <= radiusBokeh; x++, pos.x++)
        {
            posUV = int3(pos.x / 2, pos.y / 2, 0);
            float myDistance = distance(pos, posIn);
            if (myDistance >= (float)radiusBokeh) continue;
            dataH.x = (float)planeY.Load(pos);
            dataH.yz = (float2)planeUV.Load(posUV);
            coef = (float)dataH.x * (float)dataH.x + 25.0f/255.0f;
            data.xyz += dataH.xyz * coef;
            weight += coef;
        }
    }
#endif
    data.xyz /= weight;
    return data.xyz;
}

[numthreads(8, 8, 1)]
void CSBlendBKRGB(uint3 coord : SV_DispatchThreadID)
{
    int3 posY = int3(coord.x, coord.y, 0);
    int3 posUV = int3(coord.x/2, coord.y/2, 0);

    if ((posY.x >= (int)widthBK) || (posY.y >= (int)heightBK)) return;

    int3 posOut = posY;
    int3 posYSrc = posY;
    posYSrc.x -= (int)posXStart;
    posYSrc.y -= (int)posYStart;

    float4 dataOut = 0.0f;
    float4 dataBK = 0.0f;
    dataBK.x = (float)planeInBKY.Load(posY);
    dataBK.yz = (float2)planeInBKUV.Load(posUV);

    //blur the background
    if (enableBokeh==1)
    {
        dataBK.xyz = BokehSub(planeInBKY, planeInBKUV, posY, radiusBokeh, (int)heightBK);
    }

    if ((posY.x < (int)posXStart) || (posY.y < (int)posYStart) || (posYSrc.x >= (int)width) || (posYSrc.y >= (int)height))	//only background
    {
        dataOut = NV12toRGB(dataBK);
        dataOut = (colorTransferDst == 1) ? dataOut : ((colorTransferBK == 1) ? DeGamma(dataOut) : dataOut);
        planeOut[posOut.xy] = dataOut;
        return;
    }

#if defined(RGB_FP16)
    dataOut = ((float4)planeIn.Load(posYSrc));
#elif defined(YUV444)
    dataOut = ((float4)planeIn.Load(posYSrc)).yxzw;  //UYVA
#elif defined(YUV422)
    int3 posUYVY = posYSrc;
    //    int3 posUYVY = posY;
    posUYVY.x /= 2; //YUYV format
    dataOut.yxzw = (float4)planeIn.Load(posUYVY);
    dataOut.x = ((posYSrc.x % 2) == 0) ? dataOut.x : dataOut.w;  //UYVY
#else
    posUV = posYSrc / 2, 0;
    dataOut.x = (float)planeInY.Load(posYSrc);
    dataOut.yz = (float2)planeInUV.Load(posUV);
#endif
    float spill = planeMaskSpill.Load(posYSrc);
    float alpha = alphaFromSrc ? dataOut.w : planeMaskBlur.Load(posYSrc);

    if (enableBokeh == 2)
    {
#if defined(YUV422) || defined(YUV444) || defined(RGB_FP16)
#else
        dataOut.xyz = BokehSub(planeInY, planeInUV, posYSrc, radiusBokeh, height);
#endif
    }

#if !defined(RGB_FP16)
    //replace the UV with background, smooth the luma 
    if (spill > (6.0f / 255.0f))
    {
        if ((dataOut.y < .5f) && (dataOut.z < .5f))
        {
            dataOut.x = (dataOut.x * alpha + dataBK.x * (1.0f - alpha));
            dataOut.yz = dataBK.yz;
        }
    }
#endif

    if (greenReducing == 0)
    {
        //YUV blending, better performance
        if ((colorTransferSrc == 0) && (colorTransferBK == 0) && (colorTransferDst == 0))
        {
            dataOut.xyz = (dataOut.xyz * alpha + dataBK.xyz * (1.0f - alpha));
            //        dataOut.xyz = dataBK.xyz;// clamp(dataOut.xyz, 0.f, 1.0f);
            dataOut = NV12toRGB(dataOut);
            //        dataOut.xyz = alpha;
        }
        else
        {
#if !defined(RGB_FP16)
            dataOut = NV12toRGB(dataOut);
#endif
            if (colorTransferSrc == 1)
            {
                dataOut = DeGamma(dataOut);
            }
            else if (colorTransferSrc == 2)
            {
                dataOut = DePQ(dataOut);
            }

            dataBK = NV12toRGB(dataBK);
            if (colorTransferBK == 1)
            {
                dataBK = DeGamma(dataBK);
            }
            dataOut.xyz = (dataOut.xyz * alpha + dataBK.xyz * (1.0f - alpha));
//            dataOut.xyz = dataBK.xyz;
        }
    }
    else
    {
#if !defined(RGB_FP16)
        dataOut = NV12toRGB(dataOut);
#endif
        if (colorTransferSrc == 1)
        {
            dataOut = DeGamma(dataOut);
        }
        else if (colorTransferSrc == 2)
        {
            dataOut = DePQ(dataOut);
        }
        dataBK = NV12toRGB(dataBK);
        if (colorTransferBK == 1)
        {
            dataBK = DeGamma(dataBK);
        }

        if (greenReducing == 1)
        {
            dataOut = GreenReducing(dataOut, threshold / 255.0f, threshold2 / 255.0f, debug);
            dataOut.xyz = (dataOut.xyz * alpha + dataBK.xyz * (1.0f - alpha));
        }
        else if (alpha < 1.0f / 255.0f)
        {
            dataOut = dataBK;
        }
        else if (alpha >= 0.99f)
        {
            ;//			dataOut = NV12toRGB(dataOut);
        }
        else
        {
            dataOut = GreenReducingExt(dataOut, dataBK, keycolor, radiusBokeh);
        }
    }

    planeOut[posOut.xy] = (colorTransferDst == 0) ? dataOut : Gamma(dataOut);
}

