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

#define amf_int32 int
#define WGX 32
#define WGY 8

//--------------------------------------------------------------------------------------------------------------------
struct Corner
{
    amf_int32 count;
    amf_int32 align1;
    amf_int32 align2;
    amf_int32 align3;

    int4    channel;
    int4    corner;             // 0 - lt, 1 - rt, 2 - rb, 3 - lb

    amf_int32 index;
    float3    pos;
};

RWBuffer<float> pShifts               : register(u0);
Buffer<uint> pHistogram               : register(t0);

#define HIST_SIZE   256
#define LEVEL 16.f
#define DIV_VAL   256.f
#define LUT_CURVE_TABLE_SIZE    20

#define MAX_CORNERS     100
cbuffer Corners : register(b0)
{
    Corner pCorners[MAX_CORNERS];
}

//--------------------------------------------------------------------------------------------------------------------
struct HistogramParameters
{
    int3 maxDistanceBetweenPeaks;
    amf_int32 whiteCutOffY;
    amf_int32 blackCutOffY;
    amf_int32 borderHistWidth;
    float   alphaLUT;
    float4 lutCurveTableInt[LUT_CURVE_TABLE_SIZE/4]; // curve from 0 to 1
};

cbuffer pParams : register(b1)
{
   HistogramParameters params;
};

cbuffer Params : register(b2)
{
    amf_int32 corners;
    amf_int32 frameCount;
};

//--------------------------------------------------------------------------------------------------------------------
float OneCrossCorrelation(
    int data1,    // [in]
    int data2,    // [in]
    amf_int32 col,
    amf_int32 delay,
    amf_int32 maxdelay
    );

groupshared float corrs[HIST_SIZE * 2];

[numthreads(256, 1, 1)]
void main(uint3 coord : SV_DispatchThreadID)
{
    amf_int32 delay = coord.x; // from 0 to max of params.maxDistanceBetweenPeaks
    uint id = coord.y;
    amf_int32 corner  = id / 3;
    amf_int32 side  = id % 3;
    amf_int32 col = coord.z;

    if(corner >= corners || delay >= params.maxDistanceBetweenPeaks[col] * 2 || col >= 3) 
    {
        return;
    }

    Corner it_corner = pCorners [corner];
    if(side >= it_corner.count)
    {
        return;
    }

    amf_int32 i;
    amf_int32 local_id = 2 * (coord.x % 256);
    corrs[local_id] = 0;
    corrs[local_id + 1] = 0;

    int pHist[3];

    for(i = 0; i < it_corner.count; i++)
    {
        pHist[i] = it_corner.channel[i] * 4 * 3 * HIST_SIZE  + it_corner.corner[i] * HIST_SIZE * 3 +  col * HIST_SIZE;
    }
    int h1 = 0;
    int h2 = 0;
    if(it_corner.count == 3)
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
    if(it_corner.count == 2)
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

    if(delay < params.maxDistanceBetweenPeaks[col] * 2)
    {

        GroupMemoryBarrier();
        corrs[delay] = OneCrossCorrelation(pHist[h1], pHist[h2], col, delay, params.maxDistanceBetweenPeaks[col]);
        GroupMemoryBarrier();

        if(delay == 0)
        {
            float corrMax = -1.0e5f;
            for(amf_int32 i = 0; i < params.maxDistanceBetweenPeaks[col] * 2; i++)
            {
                if(corrMax < corrs[i])
                {    
                    corrMax = corrs[i];
                    pShifts[corner * 3 * it_corner.count + col * it_corner.count + side] = (float)i - params.maxDistanceBetweenPeaks[col];
                }
            }
        }
    }
}

//--------------------------------------------------------------------------------------------------------------------
float OneCrossCorrelation(
     int data1,    // [in]
     int data2,    // [in]
    amf_int32 col,
    amf_int32 delay,
    amf_int32 maxdelay
    )
{
    float r = 0;

    if ((delay - maxdelay) < maxdelay)
    {
        float x;
        float y;


        // Calculate the mean of the two series x[], y[]
        float mx = 0;
        float my = 0;
        amf_int32 i = 0;
        for (i = params.blackCutOffY; i < params.whiteCutOffY; i++)
        {
            mx += (float)pHistogram[data1 + i];
            my += (float)pHistogram[data2 + i];
        }
        mx /= (float)HIST_SIZE;
        my /= (float)HIST_SIZE;
        // Calculate the denominator
        float sx = 0;
        float sy = 0;
        for (i = params.blackCutOffY; i < params.whiteCutOffY; i++)
        {
            x = (float)pHistogram[data1 + i] - mx;
            y = (float)pHistogram[data2 + i] - my;
            sx += x * x;
            sy += y * y;
        }
        float denom = sqrt(sx*sy);

        // Calculate the correlation
        float sxy = 0;

        for (i = params.blackCutOffY; i < params.whiteCutOffY; i++)
        {
            int j = i + delay - maxdelay;

            if (j < params.blackCutOffY || j >= params.whiteCutOffY)
            {
                continue;
            }
            else
            {
                x = (float)pHistogram[data1 + i] - mx;
                y = (float)pHistogram[data2 + j] - my;
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