
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

cbuffer pParams : register(b0)
{
   HistogramParameters params;

};
static float lutCurveTable[LUT_CURVE_TABLE_SIZE] = (float[LUT_CURVE_TABLE_SIZE])params.lutCurveTableInt;


cbuffer Params : register(b1)
{
    amf_int32 channels;
    amf_int32 frameCount;
};

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
    amf_int32 channel  = coord.x;
    amf_int32 col  = coord.y;

    if(channel < channels)
    {

        float brightnessCenter = 0;
        for( amf_int32 side = 0; side < 4; side++)
        {
            brightnessCenter +=  pBrightness[channel * 4 * 3 + side *3 + col];
        }
        brightnessCenter/=4.0f;

         int lutCenter  = channel * 3 *5 * HIST_SIZE + 4 * 3* HIST_SIZE + col * HIST_SIZE;
        int  lutPrevCenter = channel * 3 *5 * HIST_SIZE + 4 * 3* HIST_SIZE + col * HIST_SIZE;
        BuildOneLUT( col, brightnessCenter / 255.f,  lutCenter, lutPrevCenter);
    }
}
