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
#if defined(YUV422) || defined(YUV444) //YUV_PACKED
    RWTexture2D<unorm float4> planeOut      : register(u0);   //YUV422/YUV444
    RWTexture2D<unorm float>  planeOutMask  : register(u1);
    Texture2D<unorm float4>   planeIn       : register(t0);   //YUV422/YUV444
#else
    RWTexture2D<unorm float>  planeOut      : register(u0);
    RWTexture2D<unorm float2> planeOutUV    : register(u1);
    RWTexture2D<unorm float>  planeOutMask  : register(u2);
    Texture2D<unorm float>    planeIn       : register(t0);
    Texture2D<unorm float2>   planeInUV     : register(t1);
#endif

cbuffer Params : register(b0)
{
    uint width;
    uint height;
    uint keycolor0;
    uint keycolor1;
    float rangeMin;
    float rangeMax;
    float rangeExt;
    float lumaMin;
    uint enableEdge;
    uint enableAdvanced;
    uint debug;
};
uint EdgeSub(Texture2D<unorm float>  planeIn, int3 pos);

[numthreads(8, 8, 1)]
void CSProcess(uint3 coord : SV_DispatchThreadID)
{
    int3 pos = int3(coord.x, coord.y, 0);
#ifndef YUV422
    int3 posUV = int3(coord.x/2, coord.y/2, 0);
#endif

    int2 dimensions;
    planeIn.GetDimensions(dimensions.x, dimensions.y);
    if ((pos.x >= dimensions.x) || (pos.y >= dimensions.y)) return;

    float4 dataIn = 0;
#if defined(YUV444)
    dataIn = (float4)planeIn.Load(pos).yxzw; //UYVA
#elif defined(YUV422)
    dataIn = (float4)planeIn.Load(pos).yxzw;    //UYVY --> YUVY
#else
    dataIn.x = (float)planeIn.Load(pos);
    dataIn.yz = (float2)planeInUV.Load(posUV);
#endif

    float keycolorU = (float)((keycolor0 >> 10) & 0x000003FF) / 1024.f;
    float keycolorV = (float) (keycolor0 & 0x000003FF) / 1024.f;

    float4 dataOut = 0;
    float diffU = dataIn.y - keycolorU;
    float diffV = dataIn.z - keycolorV;

    float diff = diffU * diffU + diffV * diffV;

    keycolorU = (float)((keycolor1 >> 10) & 0x000003FF) / 1024.f;
    keycolorV = (float) (keycolor1 & 0x000003FF) / 1024.f;
    diffU = dataIn.y - keycolorU;
    diffV = dataIn.z - keycolorV;
    float diff2 = diffU * diffU + diffV * diffV;
    diff = min(diff, diff2);

    float alpha = 1.f;
    if (diff <= rangeMin)   //Green
    {
        alpha = 0;
        dataOut = (debug & 0x01) ? float4(230.f/255.f, .5f, .5f, 230.f / 255.f) : float4(0, .5f, .5f, 0);
    }
    //handle transparent area
    else if (enableAdvanced && (dataIn.y < (148.f/255.f)) && (dataIn.z < (148.f/255.f)))   //green
    {
        alpha = .5f;
        dataOut = dataIn;
    }
    else if (diff <= rangeMax)   //Middle
    {
        alpha = 1.f/255.f + (diff - rangeMin) / (rangeExt - rangeMin) * (254.f/255.f);
        dataOut = (debug&0x01) ? float4(.5f, .5f, .5f, .5f) : float4(dataIn.x, .5f, .5f, dataIn.w);
    }
    else
    {
        //for extended green range
        diff = dataIn.y * dataIn.y + dataIn.z * dataIn.z;   //UV data position
        if (diff < rangeExt) //green area
        {
            alpha = .5f;
            dataOut = (debug & 0x01) ? float4(1.f, 1.f, 0, 1.0f) : float4(dataIn.x, .5f, .5f, dataIn.w);
        }
        else
        {
            dataOut = dataIn;
        }
    }
#if 0
    if (enableEdge)
    {
        uint edge = EdgeSub(planeIn, pos);
        if (edge > alpha)
        {
            dataOut.xyz = dataIn.xyz;
        }
        alpha = max(alpha, edge);
    }
#endif

#if defined(YUV444)
    //YUV
    planeOut[pos.xy] = dataOut.yxzw;   //UYVA
   //Mask
    planeOutMask[pos.xy] = alpha;
#elif defined(YUV422)
    //YUYV
    planeOut[pos.xy] = dataOut.yxzw;

    //Mask, output two pixels
    pos.x *= 2;
    planeOutMask[pos.xy] = alpha;
    pos.x++;
    planeOutMask[pos.xy] = alpha;
#else
    //Y
    planeOut[pos.xy] = dataOut.x;
    //UV
    planeOutUV[posUV.xy] = dataOut.yz;

    //Mask
    planeOutMask[pos.xy] = alpha;
#endif
}

uint EdgeSub(Texture2D<unorm float>  planeIn, int3 pos)
{
    int weights[3] = { -1, -2, -1 };
    uint dx = 0;
    uint dy = 0;
    int x = 0;
    int y = 0;
    int3 posEdge = pos;

    posEdge.xy = pos.xy - 1;
    for (y = 0; y < 2; y++, posEdge.y++)
    {
        dx += weights[y] * planeIn.Load(posEdge).x;
    }

    posEdge.x += 2;
    posEdge.y = pos.y - 1;
    for (y = 0; y < 2; y++, posEdge.y++)
    {
        dx -= weights[y] * planeIn.Load(posEdge).x;
    }

    posEdge.xy = pos.xy - 1;
    for (x = 0; x < 2; x++, posEdge.x++)
    {
        dy += weights[x] * planeIn.Load(posEdge).x;
    }

    posEdge.y += 2;
    posEdge.x = pos.x - 1;
    for (x = 0; x < 2; x++, posEdge.x++)
    {
        dy -= weights[x] * planeIn.Load(posEdge).x;
    }

    return sqrt(dx*dx + dy*dy);
}
