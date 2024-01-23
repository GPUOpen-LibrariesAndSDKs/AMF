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

#include "public/include/core/Trace.h"
#include "public/include/core/Context.h"
#include "public/common/InterfaceImpl.h"
#include "public/common/AMFSTL.h"
#include "public/include/components/VideoStitch.h"
#include "HistogramImpl.h"
#include <DirectXMath.h>

#include <vector>

//#define DEBUG_TRANSPARENT
namespace amf
{

template<class _Ty, int _Al>
class amf_aligned_allocator : public std::allocator<_Ty>
{
public:
    amf_aligned_allocator() : std::allocator<_Ty>()
    {}
    amf_aligned_allocator(const amf_aligned_allocator<_Ty, _Al>& rhs) : std::allocator<_Ty>(rhs)
    {}
    template<class _Other, int _OtherAl> amf_aligned_allocator(const amf_aligned_allocator<_Other, _OtherAl>& rhs) : std::allocator<_Ty>(rhs)
    {}
    template<class _Other> struct rebind // convert an allocator<_Ty> to an allocator <_Other>
    {
        typedef amf_aligned_allocator<_Other, alignof(_Other)> other;
    };
    void deallocate(_Ty* const _Ptr, const size_t _Count)
    {
        _Count;
        amf_aligned_free((void*)_Ptr);
    }
    _Ty* allocate(const size_t _Count, const void* = static_cast<const void*>(0))
    { // allocate array of _Count el ements
        return static_cast<_Ty*>(amf_aligned_alloc(_Count * sizeof(_Ty), _Al));
    }
};

template<class _Ty, int _Al>
using AlignedVector = std::vector<_Ty, amf_aligned_allocator<_Ty, _Al>>;


class StitchEngineBase : public AMFInterfaceImpl<AMFInterface>
{
public:
    StitchEngineBase(AMFContext* pContext);
    virtual ~StitchEngineBase();

    virtual AMF_RESULT AMF_STD_CALL      Init(AMF_SURFACE_FORMAT formatInput, amf_int32 widthInput, amf_int32 heightInput, AMF_SURFACE_FORMAT formatOutput, amf_int32 widthOutput, amf_int32 heightOutput, AMFPropertyStorage *pStorage, AMFPropertyStorage **ppStorageInputs) = 0;
    virtual AMF_RESULT AMF_STD_CALL      Terminate() = 0;
    virtual AMF_RESULT AMF_STD_CALL      StartFrame(AMFSurface *pSurfaceOutput) = 0;
    virtual AMF_RESULT AMF_STD_CALL      EndFrame(bool bWait) = 0;
    virtual AMF_RESULT AMF_STD_CALL      ProcessStream(int index, AMFSurface *pSurface) = 0;
    virtual AMF_RESULT AMF_STD_CALL      UpdateFOV(amf_int32 widthInput, amf_int32 heightInput, amf_int32 widthOutput, amf_int32 heightOutput, AMFPropertyStorage *pStorage) = 0;
    virtual AMF_RESULT AMF_STD_CALL      UpdateMesh(amf_int32 index, amf_int32 widthInput, amf_int32 heightInput, amf_int32 widthOutput, amf_int32 heightOutput, AMFPropertyStorage *pStorage, AMFPropertyStorage *pStorageMain) = 0;
    virtual AMF_RESULT AMF_STD_CALL      GetBorderRect(amf_int32 index, AMFRect &border) = 0;
    virtual AMF_RESULT AMF_STD_CALL      UpdateBrightness(amf_int32 index, AMFPropertyStorage *pStorage) = 0;
    virtual RibList&   AMF_STD_CALL      GetRibs(){return m_Ribs;}
    virtual CornerList&   AMF_STD_CALL   GetCorners(){return m_Corners;}
    virtual float*     AMF_STD_CALL      GetTexRect(amf_int32 index) = 0;
    virtual AMFSurface* AMF_STD_CALL     GetBorderMap(amf_int32 index) = 0;
    virtual AMF_RESULT AMF_STD_CALL      AllocCubeMap(AMF_SURFACE_FORMAT formatOut, amf_int32 width, amf_int32 height, AMFSurface **ppSurface) = 0;

protected:
#pragma pack(push, r1, 1)
    struct TextureVertex
    {
        TextureVertex(){}
        TextureVertex(float pos_x,float pos_y, float pos_z, float tex_x, float tex_y, float tex_alpha)
        {
            Pos[0] = pos_x;
            Pos[1] = pos_y;
            Pos[2] = pos_z;
            Tex[0] = tex_x;
            Tex[1] = tex_y;
            Tex[2] = tex_alpha;
        }
        float Pos[4];   //x, y, z, reserved
        float Tex[3];   //x,y, alpha
    };

    struct ControlPoint
    {
        ControlPoint() : index0(-1),index1(-1){}

        amf_int32       index0;   // input index
        amf_int32       index1;   // input index
        TextureVertex  point0;   // in world coordinate system - the same as mesh
        TextureVertex  point1;   // in world coordinate system - the same as mesh
    };
    typedef std::vector<ControlPoint> ControlPointList;


    __declspec(align(16)) struct Transform
    {
        float m_WorldViewProjection[4][4];
    };
#pragma pack(pop, r1)

protected:
    virtual AMF_RESULT AMF_STD_CALL      PrepareMesh(
        amf_int32 widthInput,
        amf_int32 heightInput,
        amf_int32 widthOutput,
        amf_int32 heightOutput,
        AMFPropertyStorage *pStorage,
        AMFPropertyStorage *pStorageMain,
        int channel,
        std::vector<TextureVertex> &vertices,
        std::vector<amf_uint32> &verticesRowSize,
        AMFRect &borderRect,
        DirectX::XMVECTOR &texRect,
        AlignedVector<DirectX::XMVECTOR, alignof(DirectX::XMVECTOR)> &sides,
        AlignedVector<DirectX::XMVECTOR, alignof(DirectX::XMVECTOR)> &corners,
        DirectX::XMVECTOR &plane,
        DirectX::XMVECTOR &planeCenter,
        AMFSurface **ppBorderMap
        );
    virtual AMF_RESULT AMF_STD_CALL GetTransform(amf_int32 widthInput, amf_int32 heightInput, amf_int32 widthOutput,
            amf_int32 heightOutput, AMFPropertyStorage *pStorage, Transform &camera, Transform &transform, Transform *cubemap);
    virtual AMF_RESULT AMF_STD_CALL ApplyMode(amf_int32 widthOutput, amf_int32 heightOutput, std::vector<TextureVertex> &vertices,
        std::vector<amf_uint32> &verticesRowSize, std::vector<TextureVertex> &verticesProjected,  std::vector<amf_uint16> &indexes,
        AMFPropertyStorage *pStorage);

    void MakeSphere(TextureVertex &v, float centerX,float centerY,float centerZ, float newRadius);
    DirectX::XMVECTOR MakeSphere(DirectX::XMVECTOR src, float centerX,float centerY,float centerZ, float newRadius);
    DirectX::XMVECTOR CartesianToSpherical(DirectX::XMVECTOR src);

    AMFContextPtr    m_pContext;
    RibList          m_Ribs;
    CornerList       m_Corners;
    ControlPointList m_ControlPoints;

    amf_int32 m_iWidthTriangle;
    amf_int32 m_iHeightTriangle;
};
    typedef AMFInterfacePtr_T<StitchEngineBase> StitchEngineBasePtr;
} // namespace amf