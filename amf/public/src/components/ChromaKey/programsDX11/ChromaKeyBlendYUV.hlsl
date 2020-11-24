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
#if defined(YUV422) || defined(YUV444) || defined(RGB_FP16) //YUV_PACKED, RGB
    Texture2D<unorm float4> planeIn        : register(t0);
    Texture2D<unorm float>  planeMaskSpill : register(t1);
    Texture2D<unorm float>  planeMaskBlur  : register(t2);
#else
    Texture2D<unorm float>  planeIn        : register(t0);
    Texture2D<unorm float2> planeInUV      : register(t1);
    Texture2D<unorm float> planeMaskSpill  : register(t2);
    Texture2D<unorm float> planeMaskBlur   : register(t3);
#endif

RWTexture2D<unorm float4> planeOut    : register(u0);

#include "ChromaKeyProcessCSC.hlsl"

cbuffer Params : register(b0)
{
    uint width;
    uint height;
    uint greenReducing;
    uint threshold;
    uint threshold2;
    uint keycolor;
    uint colorTransferSrc;
    uint colorTransferDst;
    uint alphaFromSrc;
    uint debug;
};

//Pixel.r - alpha * PixelBack.r > 0
//Pixel.g - alpha * PixelBack.g > 0
//Pixel.b - alpha * PixelBack.b > 0

//Pixel.g - alpha * PixelBack.g < Pixel.r - alpha * PixelBack.r
//Pixel.g - alpha * PixelBack.g < Pixel.b - alpha * PixelBack.b

float4 GreenReducingExt2(float4 dataIn, uint keycolor)
{
    //	return dataIn;
    //not green dominate
    if ((dataIn.y < dataIn.x) && (dataIn.y < dataIn.z))
    {
        return dataIn;
    }

    float4 dataOut = dataIn;
    float4 dataKey;
    dataKey.z = (float)((keycolor >> 10) & 0x000003FF) / 1024.f;	//U
    dataKey.y = (float)(keycolor & 0x000003FF) / 1024.f;			//V
    dataKey.x = (float)((keycolor >> 20) & 0x000003FF) / 1024.f;	//Y
    dataKey = NV12toRGB(dataKey);

    float alphaMax = min((float)dataIn.x / (float)dataKey.x, (float)dataIn.y / (float)dataKey.y);
    alphaMax = min(alphaMax, (float)dataIn.z / (float)dataKey.z);
    float alphaMin1 = ((float)dataIn.y - (float)dataIn.z) / ((float)dataKey.y - (float)dataKey.z);	//(G-R)/Gkey-Rkey)
    float alphaMin2 = ((float)dataIn.y - (float)dataIn.x) / ((float)dataKey.y - (float)dataKey.x);	//(G-B)/Gkey-Bkey)

    float alphaMin = max(0.0f, min(alphaMin1, alphaMin2));
    float alpha2 = min(1.0f, min(alphaMin, alphaMax));

    dataOut.xyz -= dataKey.xyz * alpha2;
    return dataOut;
}

[numthreads(8, 8, 1)]
void CSBlendRGB(uint3 coord : SV_DispatchThreadID)
{
    int3 pos = int3(coord.x, coord.y, 0);
    int3 posUV = int3(coord.x/2, coord.y/2, 0);

    int2 dimensions;
    planeOut.GetDimensions(dimensions.x, dimensions.y);
    if ((pos.x >= dimensions.x) || (pos.y >= dimensions.y)) return;

    float4 dataOut = 0.0f;

#if defined(RGB_FP16)
    dataOut = ((float4)planeIn.Load(pos));
#elif defined(YUV444)
    dataOut = ((float4)planeIn.Load(pos)).yxzw;
#elif defined(YUV422)
    int3 posY = pos;
    posY.x /= 2; //YUYV format
    dataOut.yxzw = (float4)planeIn.Load(posY);
    dataOut.x = ((coord.x % 2) == 0) ? dataOut.x : dataOut.w;
#else
    dataOut.x  = (float)planeIn.Load(pos);
    dataOut.yz = (float2)planeInUV.Load(posUV);
#endif
    float spill = planeMaskSpill.Load(pos);
    float alpha = alphaFromSrc ? dataOut.w : planeMaskBlur.Load(pos);

#if !defined(RGB_FP16)
    if (spill > (6.0f/255.0f))
    {
        if ((dataOut.y < .5f) && (dataOut.z < .5f))
        {
            dataOut.x = (dataOut.x * alpha + .5f * (1.0f - alpha));
            dataOut.yz = .5f;
        }
    }

    dataOut   = NV12toRGB(dataOut);
#endif

    if (greenReducing == 1)
    {
        dataOut = GreenReducing(dataOut, threshold / 255.0f, threshold2 / 255.0f, debug);
    }
    else if (greenReducing == 2) //advanced
    {
        dataOut = GreenReducingExt2(dataOut, keycolor);
    }

    dataOut.w = alpha;

    if (colorTransferSrc == 1)
    {
        dataOut = DeGamma(dataOut);
    }
    else if (colorTransferSrc == 2)
    {
        dataOut = DePQ(dataOut);
    }

    planeOut[pos.xy] = (colorTransferDst == 0) ? dataOut : Gamma(dataOut);
}

