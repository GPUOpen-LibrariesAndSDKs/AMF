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

RWBuffer<uint> pHistOut   : register(u0);

Buffer<uint> pHistIn      : register(t0);

cbuffer Params : register(b0)
{
    uint histoSize;
};
groupshared uint posMaxList[64];
[numthreads(64, 1, 1)]
void CSHistSort(uint3 coord : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID)
{
    uint lid = GTid.x;
    uint length = histoSize / 64;
    uint pos = lid * length;
    uint posMax = 0;
    uint histMax = 0;

    //128x128 --> 64x1
    for (uint x = 0; x < length; x++, pos++)
    {
        if (pHistIn[pos] > histMax)
        {
            histMax = pHistIn[pos];
            posMax = pos;
        }
    }
    posMaxList[lid] = posMax;
//    pHistOut[lid] = posMax;
    GroupMemoryBarrierWithGroupSync();

    //64x1 --> 1
    if (lid == 0)
    {
        uint posMax = 0;
        uint histMax = 0;
        for (uint pos = 0; pos < 64; pos++)
        {
            uint posIn = posMaxList[pos];
//            uint posIn = pHistOut[pos];
           if (pHistIn[posIn] > histMax)
            {
                histMax = pHistIn[posIn];
                posMax = posIn;
            }
        }
        pHistOut[0] = posMax;
        pHistOut[1] = histMax;
    }

}

