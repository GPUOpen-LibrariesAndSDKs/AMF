
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
//struct __attribute__ ((packed)) Corner
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

RWBuffer<float> pLUT                : register(u0);
RWBuffer<float> pLUTPrev            : register(u1);
RWBuffer<float> pBrightness         : register(u2);

Buffer<float> pShifts               : register(t0);

#define HIST_SIZE   256
#define LEVEL 16.f
#define DIV_VAL   256.f
#define LUT_CURVE_TABLE_SIZE    20


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

#define MAX_CORNERS     100
cbuffer Corners : register(b0)
{
    Corner pCorners[MAX_CORNERS];
}

cbuffer pParams : register(b1)
{
   HistogramParameters params;

};

cbuffer Params : register(b2)
{
    amf_int32 corners;
    amf_int32 frameCount;
};

static   float lutCurveTable[LUT_CURVE_TABLE_SIZE] = (float[LUT_CURVE_TABLE_SIZE])params.lutCurveTableInt;
void BuildOneLUT(amf_int32 col, float brightness, int lut, int prev)
{
    for(amf_int32 k=0; k < HIST_SIZE; k++)
    {
        float lutCurr = brightness;
        
        if(col == 0)
        {
            if(k < params.blackCutOffY)
            {
                lutCurr = 0.0f;
            }
            else if(k < LUT_CURVE_TABLE_SIZE + params.blackCutOffY)
            {
                lutCurr *= 1.0f - lutCurveTable[k - params.blackCutOffY];
            }
            else if(k > params.whiteCutOffY)
            {
                lutCurr = 0.0f;
            }
            else if(k >= params.whiteCutOffY - LUT_CURVE_TABLE_SIZE)
            {
                lutCurr *= lutCurveTable[k - (params.whiteCutOffY - LUT_CURVE_TABLE_SIZE) ];
            }
        }
        
        lutCurr += (float)k /255.f;

        float lutPrev = pLUTPrev[prev + k];
        if(frameCount != 0 )
        {
            lutCurr = lutPrev + params.alphaLUT * ( lutCurr - lutPrev);
        }
        lutCurr = clamp(lutCurr, 0, 1.0);
        pLUTPrev[prev + k] = lutCurr;
        pLUT[lut + k] = lutCurr;
   }
}

[numthreads(256, 1, 1)]
void main(uint3 coord : SV_DispatchThreadID)
{
    amf_int32 corner  = coord.x;
    amf_int32 side  = coord.y;
    amf_int32 col = coord.z;

    if(corner >= corners || side >= 3 || col >= 3) 
    {
        return;
    }
    Corner it_corner = pCorners[corner];

    if(side >= it_corner.count)
    {
        return;
    }


    float       brightness[3];
    brightness[0] = 0;
    brightness[1] = 0;
    brightness[2] = 0;

    int shiftOffset = corner * 3 * it_corner.count + col * it_corner.count;

    if(it_corner.count == 2)
    {
        brightness[0] =  pShifts[shiftOffset + 0] / 2.0f;
        brightness[1] =  pShifts[shiftOffset + 1] / 2.0f;
    }
    else if(it_corner.count == 3)
    {

        int shiftMaxIndex = 0;
        int shiftMinIndex = HIST_SIZE *2;
        float shiftMin = 10000.0f;
        float shiftMax = 0.0f;


        for(int side1 = 0; side1 < it_corner.count; side1++)
        {
            if(abs(shiftMin) > abs(pShifts[shiftOffset + side1]))
            {
                shiftMin = pShifts[shiftOffset + side1];
                shiftMinIndex = side1;
            }
            if(abs(shiftMax) < abs(pShifts[shiftOffset + side1]))
            {
                shiftMax = pShifts[shiftOffset + side1];
                shiftMaxIndex = side1;
            }
        }
        int shiftMinIndexSecond = 0;
        for(int side2 = 0; side2 < it_corner.count; side2++)
        {
            if(side2 != shiftMinIndex && side2 != shiftMaxIndex)
            {
                shiftMinIndexSecond = side2;
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

    int lut  = it_corner.channel[side] * 3 *5 * HIST_SIZE + it_corner.corner[side] * 3* HIST_SIZE + col * HIST_SIZE;
    int lutPrev = it_corner.channel[side] * 3 *5 * HIST_SIZE + it_corner.corner[side] * 3* HIST_SIZE + col * HIST_SIZE;

    BuildOneLUT( col, brightness[side]/ 255.f,  lut, lutPrev);

    pBrightness[it_corner.channel[side] * 4 *3 + side *3 + col]  = brightness[side];
}
