
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

Texture2D<uint> frameY : register(t0);
Texture2D<uint2> frameUV : register(t1);
Texture2D<float> map : register(t2);

RWBuffer<uint> pOutHist : register(u0);

#define amf_int32 int
#define WGX 32
#define WGY 8

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

cbuffer Params : register(b1)
{
    amf_int32 channel;
};

[numthreads(WGX, WGY, 1)]
void main(uint3 coord : SV_DispatchThreadID)
{
    uint2 posUV = uint2(coord.x, coord.y);
    uint2 posY = posUV * 2;

    uint2 dimensions;
    frameY.GetDimensions(dimensions.x, dimensions.y);

    if(posY.x < dimensions.x && posY.y < dimensions.y)
    {
        uint2 posMap = posY / 8;
        uint mapData = map[posMap].x * 255.;

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
            uint2 uv = frameUV[posUV].xy;

            for(uint i = 0; i< 4; i++)
            {
                uint y = frameY[uint2(posY.x + (i%2), posY.y + (i/2))];
                uint4 color = uint4( y, uv.x, uv.y , 0);
                if(sideX >= 0)
                {
                    int outHistY = channel * 4 *3 * HIST_SIZE + sideX * HIST_SIZE *3 + 0 * HIST_SIZE;

                    InterlockedAdd(pOutHist[outHistY + color.x], 1);
                    if(i == 0)
                    {
                        int outHistU = outHistY + HIST_SIZE;
                        int outHistV = outHistU + HIST_SIZE;

                        InterlockedAdd(pOutHist[outHistU + color.y], 1);
                        InterlockedAdd(pOutHist[outHistV + color.z], 1);
                    }
                }
                if(sideY >= 0)
                {
                   int outHistY = channel * 4 *3 * HIST_SIZE + sideY * HIST_SIZE *3 + 0 * HIST_SIZE;
                    InterlockedAdd(pOutHist[outHistY + color.x], 1);
                    if(i == 0)
                    {
                        int outHistU = outHistY + HIST_SIZE;
                        int outHistV = outHistU + HIST_SIZE;
                        InterlockedAdd(pOutHist[outHistU + color.y], 1);
                        InterlockedAdd(pOutHist[outHistV + color.z], 1);
                    }
                }            
            }
        }
    }
}
//--------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------
