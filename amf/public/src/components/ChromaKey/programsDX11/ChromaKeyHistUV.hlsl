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

RWBuffer<uint> pOutHist             : register(u0);

#if defined(YUV422) || defined(YUV444) //YUV_PACKED
Texture2D<unorm float4> planeIn   : register(t0);
#else
Texture2D<unorm float2> planeIn   : register(t0);
#endif

cbuffer Params : register(b0)
{
    uint width;
    uint height;
    uint histoSize;
};

[numthreads(8, 8, 1)]
void CSHistUV(uint3 coord : SV_DispatchThreadID)
{
    int3 pos = int3(coord.x, coord.y, 0);

    int2 dimensions;
    planeIn.GetDimensions(dimensions.x, dimensions.y);
    if ((pos.x >= dimensions.x) || (pos.y >= dimensions.y)) return;

#if defined(YUV444)
    float2 dataUV = planeIn.Load(pos).xz;  //UYVA
#elif defined(YUV422)
    float2 dataUV = planeIn.Load(pos).xz;  //UYVY
#else
    float2 dataUV = planeIn.Load(pos);     //UV
#endif
    if ((dataUV.x < .5f) && (dataUV.y < .5f))
    {
        uint posOut = (uint)(dataUV.y * 255.f) * histoSize + (uint)(dataUV.x * 255.f);
        InterlockedAdd(pOutHist[posOut], 1);
    }
}

