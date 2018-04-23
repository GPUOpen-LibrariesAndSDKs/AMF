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

Texture2D<uint> frameY   : register(t0);
Texture2D<uint2> frameUV : register(t1);
Buffer<float> pLUT : register(t2);

RWTexture2D<float4> frameRGB : register(u0);

#define amf_int32 int

cbuffer Params : register(b0)
{
    float borderL;
    float borderT;
    float borderR;
    float borderB;
    amf_int32 channel;
    amf_int32 linearRGB;
};

#define WGX 32
#define WGY 8

#define HIST_SIZE   256
#define LEVEL 16.f
#define DIV_VAL   256.f
#define LUT_CURVE_TABLE_SIZE    20

#define XM_PI           3.141592654f
#define XM_2PI          6.283185307f
#define XM_1DIVPI       0.318309886f
#define XM_1DIV2PI      0.159154943f
#define XM_PIDIV2       1.570796327f
#define XM_PIDIV4       0.785398163f

#define ATAN_POINTS 1000

//--------------------------------------------------------------------------------------------------------------------
float applyLUT(int iLUT, amf_int32 col, float2 pos, float color, float4 border)
{
    float colorf = color;


    float l = border.x;
    float t = border.y;
    float r = border.z;
    float b = border.w;

    float x = (pos.x - 0.5f) * 2.0f;
    float y = (pos.y - 0.5f) * 2.0f;
    
    
    if(x < 0.0f)
    {
        x /= abs(l);
    }
    else
    {
        x /= abs(r);
    }
    if(y < 0.0f)
    {
        y /= abs(t);
    }
    else
    {
        y /= abs(b);
    }

    float x_abs = abs(x);
    float y_abs = abs(y);

    int index1 = 0;
    int index2 = 0;

    float weight1 = 1.0;

    if(x != 0)
    {
        if(y_abs < x_abs)
        {
            float q = y_abs / x_abs;
            weight1 = q / (1.0f + 0.28125f * q * q ); // fast atan() implementation
        }
        else
        {
            float q = x_abs / y_abs;
            weight1 = (XM_PIDIV2 - q / (1.0f + 0.28125f * q * q )); // fast atan() implementation
        }
        weight1 /= XM_PIDIV2;
    }
    if(weight1 >= 0.5)
    {
        weight1 = 1.0 - weight1;
    }
    weight1 = weight1 + 0.5;

    float weight2 = 1.0 - weight1;

    if(x <= 0.0f && y <= 0.0f)
    { //LeftTop
        index1 = 0;
        if(-x < -y)
        {
            index2 = 1;
        }
        else
        {
            index2 = 3;
        }
    }
    
    else if(x > 0.0f && y <= 0.0f)
    { //RightTop
        index1 = 1;
        if(x < -y)
        {
            index2 = 0;
        }
        else
        {
            index2 = 2;
        }
    }
    else if(x > 0.0f && y > 0.0f)
    { // RightBottom
        index1 = 2;
        if(x < y)
        {
            index2 = 3;
        }
        else
        {
            index2 = 1;
        }
    }
    else if(x <= 0.0f && y > 0.0f)
    { // LeftBottom
        index1 = 3;
        if(-x < y)
        {
            index2 = 2;
        }
        else
        {
            index2 = 0;
        }
    }

    int colIndex =  col * HIST_SIZE + color;
    
    int HistCol = HIST_SIZE * 3;

    float lut1 =      pLUT[iLUT + index1 * HistCol + colIndex];
    float lut2 =      pLUT[iLUT + index2 * HistCol + colIndex];
    float lutCenter = pLUT[iLUT + 4      * HistCol + colIndex];
   

    float radius = sqrt(x * x + y * y) * 1.3;
    float weightColor = 1.0 - radius;
    if(weightColor < 0.0)
    {
        weightColor = 0.0;
    }

    float ready;
    if(weight1 + weight2  + weightColor== 0)
    {
        ready = (lut1 + lut2) / 2.0;
    }
    else
    {
        ready = lutCenter * weightColor + ((lut1 * weight1 + lut2 * weight2) / (weight1 + weight2)) * (1.0 - weightColor);
    }
    ready *= 255.0f;
    return clamp(ready, 0.f, 255.f);
}

//--------------------------------------------------------------------------------------------------------------------
float4 yuv_to_rgb( float4 yuv )
{
    uint4 rgb;
    float y = yuv.x - LEVEL;
    float u = yuv.y - 128.f; 
    float v = yuv.z - 128.f;

    rgb.x = clamp((1.1643828125f * y +   0.0f     * u +   1.7927421875f * v) , 0.f, 255.f);
    rgb.y = clamp((1.1643828125f * y -  0.21325f   * u -   0.53291015625f * v), 0.f, 255.f);
    rgb.z = clamp((1.1643828125f * y + 2.11240234375f   * u +     0.0f   * v), 0.f, 255.f);
    rgb.w = yuv.w;
    return rgb;
}

float4 srgb_to_linear(float4 C)
{
    C *= 1.0f / 255.0f;
    C = C > 0.04045f ? pow( C * (1.0f / 1.055f) + 0.0521327f, 2.4f ) : C * (1.0f / 12.92f);
    return C;
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
        float2 uv = frameUV[posUV];

        // apply LUT
        float4 border = float4( borderL, borderT, borderR, borderB);

        int iLUTChannel =  channel * 5 *3 * HIST_SIZE;

         float4 colorLUT;
         for(uint i = 0; i < 4; i++)
         {
            uint2 pos = uint2(posY.x + (i%2), posY.y + (i/2));
            float2 posF = float2(float(pos.x) / dimensions.x , float(pos.y) / dimensions.y );

            float y = frameY[pos];

            float4 color = float4( y, uv.x, uv.y , 0);
            colorLUT.x = applyLUT(iLUTChannel, 0,  posF, color.x, border);
            if(i == 0)
            {
                colorLUT.y = applyLUT(iLUTChannel, 1,  posF, color.y, border);
                colorLUT.z = applyLUT(iLUTChannel, 2,  posF, color.z, border);
                colorLUT.w = 0;
            }
            float4 colorOUT = yuv_to_rgb(colorLUT);
            if (linearRGB)
            {
                colorOUT = srgb_to_linear(colorOUT);
            }
            else
            {
                colorOUT = colorOUT / 255.;
            }
            frameRGB[pos] = clamp(colorOUT, 0, 1.0);
        }
    }
}
