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
//--------------------------------------------------------------------------------------
// Constant Buffer Variables                                                            
//--------------------------------------------------------------------------------------
cbuffer ConstantBuffer
{                                                                                       
    matrix WorldViewProjection;                                                         
}                        
cbuffer CubemapConstantBuffer
{                                                                                       
    matrix CubemapWorldViewProjection[6];                                                         
}                        
//--------------------------------------------------------------------------------------
Texture2D txDiffuse : register( t0 );                                                   
SamplerState samplerState : register( s0 );                                             
//--------------------------------------------------------------------------------------
struct VS_INPUT                                                                         
{                                                                                       
    float4 Pos : POSITION;                                                              
    float3 Tex : TEXCOORD0;                                                             
};                                              
//--------------------------------------------------------------------------------------
struct VS_OUTPUT
{                                                                                       
    float4 Pos : SV_POSITION;                                                              
    float3 Tex : TEXCOORD0;                                                             
};                                              
//--------------------------------------------------------------------------------------

struct GS_OUTPUT                                                                       
{                                                                                       
    float4  Pos     : SV_POSITION;                                                              
    float3  Tex     : TEXCOORD0;                                                             
    uint    RTIndex : SV_RenderTargetArrayIndex;
};                                              

//--------------------------------------------------------------------------------------
struct PS_OUTPUT
{                                                                                       
    float4 color : SV_TARGET;
                                                          
};                                         

//--------------------------------------------------------------------------------------
// Vertex Shader                                                                        
//--------------------------------------------------------------------------------------
VS_OUTPUT VS( VS_INPUT input )                                                           
{                                                                                       
    VS_OUTPUT output = (VS_OUTPUT)0;                                                      
    output.Pos = mul( input.Pos, WorldViewProjection );                                 
    output.Tex = input.Tex;                                                             
                                                                                        
    return output;                                                                      
}                                                                                       
//--------------------------------------------------------------------------------------
// Pixel Shader passing texture color                                                   
//--------------------------------------------------------------------------------------
PS_OUTPUT PS( VS_OUTPUT input)
{                                                                                       
    float2 pos = float2(input.Tex.x, input.Tex.y);
    PS_OUTPUT output = (PS_OUTPUT) 0;	
    output.color = txDiffuse.Sample( samplerState, pos );                       
    output.color.w = input.Tex.z;                                                         
    return output;                                                                     
}                                                                                       
//--------------------------------------------------------------------------------------
// Pixel Shader passing texture color                                                   
//--------------------------------------------------------------------------------------
PS_OUTPUT PS_Cube( GS_OUTPUT input)
{                                                                                       
    float2 pos = float2(input.Tex.x, input.Tex.y);
    PS_OUTPUT output = (PS_OUTPUT) 0;	
    output.color = txDiffuse.Sample( samplerState, pos );                       
    output.color.w = input.Tex.z;                                                         
    return output;                                                                     
}                                                                                       

//--------------------------------------------------------------------------------------
// Geometry Shader passing texture color                                                   
//--------------------------------------------------------------------------------------

[maxvertexcount(18)] 
void GS( triangle VS_OUTPUT input[3], inout TriangleStream<GS_OUTPUT> CubeMapStream ) 
{ 
    // For each face of the cube, create a new triangle [unroll]
    for( int f = 0; f < 6; ++f ) 
    { 
        GS_OUTPUT output = (GS_OUTPUT) 0; // Assign triangle to the render target corresponding to this cube face 
        output.RTIndex = f; 
        // For each vertex of the triangle, compute screen space position and pass distance [unroll] 
        for( int v = 0; v < 3; ++v )
        { 
            output.Pos= mul( input[v].Pos, CubemapWorldViewProjection[f] ); 
            output.Tex = input[v].Tex;                                                             
            CubeMapStream.Append( output ); 
        } 
        // New triangle 
        CubeMapStream.RestartStrip(); 
    } 
} 