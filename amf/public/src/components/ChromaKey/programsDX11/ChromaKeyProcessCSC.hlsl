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

float4 RGBtoYUV444(float4 dataRGB);
float4 NV12toRGB(float4 dataYUV);
float4 GreenReducing(float4 dataIn, float threshold, float threshold2, uint debug);
float4 DeGamma(float4 dataIn);

//--------------------------------------------------------------------------------------------------------------------
float4 RGBtoYUV444(float4 dataRGB)
{
    //BT709_FULL_RANGE
    const float3 coef[4] =
    {
        // y        u         v
        {0.062f,  0.500f,  0.500f},    //Offset
        //r         g       b
//HD rgb full range
        { 0.1826f,  0.6142f,  0.0620f}, //y
        {-0.1006f, -0.3386f,  0.4392f}, //u
        { 0.4392f, -0.3989f, -0.0403f}, //v
    };

    float4 dataYUV;
    dataYUV.x = dataRGB.x * coef[1].x + dataRGB.y * coef[1].y + dataRGB.z * coef[1].z;
    dataYUV.y = dataRGB.x * coef[2].x + dataRGB.y * coef[2].y + dataRGB.z * coef[2].z;
    dataYUV.z = dataRGB.x * coef[3].x + dataRGB.y * coef[3].y + dataRGB.z * coef[3].z;
    dataYUV.w = 1.0f;
    dataYUV.xyz += coef[0].xyz;
    dataYUV = clamp(dataYUV, 0.f, 1.0f);
    return dataYUV.yxzw;         //UYVA
}
//--------------------------------------------------------------------------------------------------------------------
float4 NV12toRGB(float4 dataYUV)
{
    //BT709_FULL_RANGE
    const float3 coef[4] =
    {
        { -16.f / 255.0f,     -0.5f,               -0.5f },                //RgbOffset
        { 0.00456621f*255.0f,  0.0f*255.0f,         0.00703137f*255.0f },  //Red Coef
        { 0.00456621f*255.0f, -0.00083529f*255.0f, -0.00209019f*255.0f },  //Green Coef
        { 0.00456621f*255.0f,  0.00828235f*255.0f,  0.0f*255.0f },         //Blue Coef
    };

    dataYUV.xyz += coef[0].xyz;

    float4 dataRGB;
    dataRGB.x = dataYUV.x * coef[1].x + dataYUV.y * coef[1].y + dataYUV.z * coef[1].z;
    dataRGB.y = dataYUV.x * coef[2].x + dataYUV.y * coef[2].y + dataYUV.z * coef[2].z;
    dataRGB.z = dataYUV.x * coef[3].x + dataYUV.y * coef[3].y + dataYUV.z * coef[3].z;
    dataRGB.w = 1.0f;
    dataRGB = clamp(dataRGB, 0.f, 1.0f);
    return dataRGB;
}

float4 GreenReducing(float4 dataIn, float threshold, float threshold2, uint debug)
{
    float diff1 = dataIn.y - dataIn.z; //G-R
    float diff2 = dataIn.y - dataIn.x; //G-B
    float diff = max(diff1, diff2) / 2.0f;

    if ((diff1 > 0) && (diff2 > 0))
    {
        dataIn.y -= (diff > threshold) ? threshold : diff;
    }
    else if((diff1 > 1.0 / 255.0f) || (diff2 > 1.0f / 255.0f))
    {
        float diff2 = diff / threshold2;
        diff2 = diff2 * diff2 * diff2 * threshold2;     //reduce the adjustment in the less polluted area
        dataIn.y -= (diff > threshold2) ? diff : diff2;
    }
    return dataIn;
}

float4 DeGamma(float4 dataIn)
{
    float4 dataOut;
    dataOut.xyz = dataIn.xyz > 0.04045f ? pow(dataIn.xyz / 1.055f + 0.0521327f, 2.4f) : dataIn.xyz / 12.92f;
    dataOut.w = dataIn.w;
    return dataOut;
}

float4 Gamma(float4 dataIn)
{
    float4 dataOut;
    dataOut.xyz = dataIn.xyz > 0.0031308f ? 1.055f * pow(dataIn.xyz, 1.0f/2.4f) - 0.055f : dataIn.xyz * 12.92f;
    dataOut.w = dataIn.w;
    return dataOut;
}

float DePQtoLinear(float value)//dePQ (convert to Linear)
{
    const float m1 = 0.1593017578125f;
    const float m2 = 78.84375f;
    const float c1 = 0.8359375f;
    const float c2 = 18.8515625f;
    const float c3 = 18.6875f;
    float NP = pow(value, (1.0f / m2));
    float t = (NP - c1);
    float t1 = max(t, 0.0f);
    float t2 = c2 - (c3*NP);
    float T = (t1 / t2);
    return pow(T, (1.0f / m1)) * 80.0f;
}

float4 DePQ(float4 dataIn)
{

    float4 dataOut=0;
    dataOut.x = DePQtoLinear(dataIn.x);
    dataOut.y = DePQtoLinear(dataIn.y);
    dataOut.z = DePQtoLinear(dataIn.z);
    return dataOut;
}
