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

Texture2D<float> borderMap : register(t0);

RWTexture2D<uint> frameY : register(u0);
RWTexture2D<uint2> frameUV : register(u1);

#define amf_int32 int
#define WGX 32
#define WGY 8

#define LEVEL 16.f
#define DIV_VAL   256.f

//--------------------------------------------------------------------------------------------------------------------
uint4 rgb_to_yuv( uint4 rgb )
{    
    float4 yuv;
    yuv.x = clamp((  46.742f  *rgb.x + 157.243f   *rgb.y +  15.874f  *rgb.z + LEVEL ) / DIV_VAL, 0.f, 255.f);
    yuv.y = clamp(( (-25.765f)*rgb.x + ( -86.674f)*rgb.y + 112.439f  *rgb.z) / DIV_VAL + 128.5f, 0.f, 255.f);
    yuv.z = clamp(( 112.439f  *rgb.x + (-102.129f)*rgb.y + (-10.310f)*rgb.z) / DIV_VAL + 128.5f, 0.f, 255.f);
    yuv.w = rgb.w;
    return uint4(yuv);
}

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
        uint map = borderMap[posMap] * 255.;
        if (map != 0)
        {
            uint4 rgb =  uint4(0, 0, 0, 255);
            if(((map >> 0) & 1) != 0)
            {
                rgb.x = 255;
            } 
            if(((map >> 1) & 1) != 0)
            {
                rgb.y = 255;
            }
            if(((map >> 2) & 1) != 0)
            {
                rgb.z = 255;
            }
            if(((map >> 3) & 1) != 0)
            {
                rgb.x = 128;
                rgb.y = 128;
            }
            uint4 yuv = rgb_to_yuv(rgb);

            frameY[uint2(posY.x + 0, posY.y + 0)] = yuv.x;
            frameY[uint2(posY.x + 1, posY.y + 0)] = yuv.x;
            frameY[uint2(posY.x + 0, posY.y + 1)] = yuv.x;
            frameY[uint2(posY.x + 1, posY.y + 1)] = yuv.x;

            frameUV[posUV] = yuv.yz;
        }
    }
}
