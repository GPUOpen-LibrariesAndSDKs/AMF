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
#pragma once 

#include <D3D11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <float.h>
#include <atlbase.h>
#include <list>

#include "../StitchEngineBase.h"

namespace amf
{

#pragma warning(push)
#pragma warning(disable : 4324) // structure was padded due to alignment specifier

class StitchEngineDX11 : public StitchEngineBase, public AMFSurfaceObserver
{
public:
    StitchEngineDX11(AMFContext* pContext);
    virtual ~StitchEngineDX11();

    virtual AMF_RESULT AMF_STD_CALL      Init(AMF_SURFACE_FORMAT formatInput, amf_int32 widthInput, amf_int32 heightInput, AMF_SURFACE_FORMAT formatOutput, amf_int32 widthOutput, amf_int32 heightOutput, AMFPropertyStorage *pStorage, AMFPropertyStorage **ppStorageInputs);
    virtual AMF_RESULT AMF_STD_CALL      Terminate();
    virtual AMF_RESULT AMF_STD_CALL      StartFrame(AMFSurface *pSurfaceOutput);
    virtual AMF_RESULT AMF_STD_CALL      EndFrame(bool bWait);
    virtual AMF_RESULT AMF_STD_CALL      ProcessStream(int index, AMFSurface *pSurface);
    virtual AMF_RESULT AMF_STD_CALL      UpdateFOV(amf_int32 widthInput, amf_int32 heightInput, amf_int32 widthOutput, amf_int32 heightOutput, AMFPropertyStorage *pStorage);
    virtual AMF_RESULT AMF_STD_CALL      UpdateMesh(amf_int32 index, amf_int32 widthInput, amf_int32 heightInput, amf_int32 widthOutput, amf_int32 heightOutput, AMFPropertyStorage *pStorage, AMFPropertyStorage *pStorageMain);
    virtual AMF_RESULT AMF_STD_CALL      GetBorderRect(amf_int32 index, AMFRect &border);
    virtual AMF_RESULT AMF_STD_CALL      UpdateBrightness(amf_int32 index, AMFPropertyStorage *pStorage);
    virtual float*     AMF_STD_CALL      GetTexRect(amf_int32 index);
    virtual AMFSurface* AMF_STD_CALL     GetBorderMap(amf_int32 index);
    virtual AMF_RESULT AMF_STD_CALL      AllocCubeMap(AMF_SURFACE_FORMAT formatOut, amf_int32 width, amf_int32 height, AMFSurface **ppSurface);

    // AMFSurfaceObserver interface
    virtual void AMF_STD_CALL OnSurfaceDataRelease(AMFSurface* pSurface);

protected:
friend class Stream;

    //
    // The DirectX XMVECTOR and XMMATRIX structs require a certain alignment.
    // When they are declared as the member of another struct, that struct
    // will automatically require alignment as well, for example the 
    // Stream class below. 
    // 
    // When allocating aligned structs on the heap, the compiler will report a 
    // C4316 warning: object allocated on the heap may not be aligned.
    // To remediate this, the stream class is allocated using aligned malloc 
    // and aligned free. This will ensure the alignment of the entire structure.
    // The alignment of any XMMATRIX or XMVECTOR members within the struct memory
    // itself will be aligned and padded automatically by the VS compiler (tested)
    //
    // For aligned structs allocated with std::vector, the warning doesn't
    // pop up but since std::vector uses the default allocator with new, theres no
    // guarantee that it allocates the structs with the correct alignment. The aligned
    // allocator must be used with std::vector to ensure the struct allocations are
    // aligned on the heap.

    class Stream // per stream properties
    {
    public:
        ATL::CComPtr<ID3D11Buffer>                                      m_pVertexBuffer;
        ATL::CComPtr<ID3D11Buffer>                                      m_pIndexBuffer;
        AMFRect                                                         m_BorderRect;
        AlignedVector<DirectX::XMVECTOR, alignof(DirectX::XMVECTOR)>    m_Sides;
        DirectX::XMVECTOR                                               m_TexRect;
        AMFSurfacePtr                                                   m_pBorderMap;
        std::vector<TextureVertex>                                      m_Vertices;
        std::vector<amf_uint16>                                         m_Indexes;
        DirectX::XMVECTOR                                               m_Plane;
        DirectX::XMVECTOR                                               m_PlaneCenter;
        std::vector<TextureVertex>                                      m_VerticesProjected;
        std::vector<amf_uint32>                                         m_VerticesRowSize;
        AlignedVector<DirectX::XMVECTOR, alignof(DirectX::XMVECTOR)>    m_Corners;
    };

    // Used aligned vector (std vector with aligned allocator) 
    // instead of a regular vector since Stream requires aligned 
    // allocation on the heap
    typedef AlignedVector<Stream, alignof(Stream)> StreamList;

    AMF_RESULT          UpdateRibs(amf_int32 widthInput, amf_int32 heightInput, AMFPropertyStorage *pStorage);
    AMF_RESULT          RecreateBuffers(amf_int32 index);

    AMF_RESULT          ApplyControlPoints();
    AMF_RESULT          BuildMapForHistogram(amf_int32 widthInput, amf_int32 heightInput);
    AMF_RESULT          UpdateTransparency(AMFPropertyStorage *pStorage);


    StreamList      m_StreamList;
    AMFSurfacePtr   m_pSurfaceOutput;

    ATL::CComPtr<ID3D11Device>              m_pd3dDevice;
    ATL::CComPtr<ID3D11DeviceContext>       m_pd3dDeviceContext;
    ATL::CComPtr<ID3D11VertexShader>        m_pVertexShader;
    ATL::CComPtr<ID3D11PixelShader>         m_pPixelShader;
    ATL::CComPtr<ID3D11PixelShader>         m_pPixelShaderCube;
    ATL::CComPtr<ID3D11GeometryShader>      m_pGeometryShader;

    ATL::CComPtr<ID3D11InputLayout>         m_pVertexLayout;
    ATL::CComPtr<ID3D11SamplerState>        m_pSamplerLinear;
    ATL::CComPtr<ID3D11DepthStencilState>   m_pDepthStencilState;
    ATL::CComPtr<ID3D11RasterizerState>     m_pRasterizerState;
    ATL::CComPtr<ID3D11RasterizerState>     m_pRasterizerStateWire;
    ATL::CComPtr<ID3D11BlendState>          m_pBlendState;
    ATL::CComPtr<ID3D11RenderTargetView>    m_pRenderTargetView;
    ATL::CComPtr<ID3D11Texture2D>           m_pDepthStencil;
    ATL::CComPtr<ID3D11DepthStencilView>    m_pDepthStencilView;
    ATL::CComPtr<ID3D11Buffer>              m_pWorldCB;
    ATL::CComPtr<ID3D11Buffer>              m_pCubemapWorldCB;
    ATL::CComPtr<ID3D11Query>               m_pQuery;
    ATL::CComPtr<ID3D11Query>               m_pQueryOcclusion;

    bool                                    m_bWireRender;

    // Transform requires alignment which by extension means
    // this class requires alignment on the heap. Instead of allocating
    // entire class using aligned malloc, the m_pCameraOrientation transform
    // is allocated seperately on the heap using aligned malloc.
    Transform*                              m_pCameraOrientation;

    AMF_VIDEO_STITCH_OUTPUT_MODE_ENUM       m_eOutputMode;

    std::list< ATL::CComPtr<ID3D11Texture2D> > m_AllocationQueue;
    AMFCriticalSection                      m_Sect;
};

#pragma warning(pop)

} // namespace amf