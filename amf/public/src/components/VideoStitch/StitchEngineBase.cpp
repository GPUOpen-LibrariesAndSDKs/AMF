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

#include "StitchEngineBase.h"
#include <DirectXMath.h>
#include <math.h>

using namespace amf;
using namespace DirectX;

#define AMF_FACILITY L"StitchEngineBase"
static XMVECTOR CorrectLensCircularFishEye(XMVECTOR src, double hfov, double f, float &transparency);
static XMVECTOR CorrectLensRadial(XMVECTOR src, double a, double b, double c);
static XMVECTOR CorrectLensRadialInverse(XMVECTOR src, double a, double b, double c);
static float CalcTransparencyTex(float posx, float posy, float zoom_z);
static XMVECTOR CartesianToEquirectangular(XMVECTOR src);

//-------------------------------------------------------------------------------------------------
StitchEngineBase::StitchEngineBase(AMFContext* pContext) :
m_pContext(pContext),
m_iWidthTriangle(128),
m_iHeightTriangle(128)
{
}

//-------------------------------------------------------------------------------------------------
StitchEngineBase::~StitchEngineBase()
{
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL StitchEngineBase::PrepareMesh(
    amf_int32 widthInput, amf_int32 heightInput,
    amf_int32 /* widthOutput */, amf_int32 /* heightOutput */,
    AMFPropertyStorage *pStorage,
    AMFPropertyStorage *pStorageMain,
    int /* channel */,
    std::vector<TextureVertex> &vertices,
    std::vector<amf_uint32> &verticesRowSize,
    AMFRect &borderRect,
    DirectX::XMVECTOR &texRect,
    AlignedVector<XMVECTOR, alignof(XMVECTOR)> &sides,
    AlignedVector<XMVECTOR, alignof(XMVECTOR)> &corners,
    DirectX::XMVECTOR &plane,
    DirectX::XMVECTOR &planeCenter,
    AMFSurface ** /* ppBorderMap */
    )
{
    vertices.clear();
    verticesRowSize.clear();

    AMF_RESULT res = AMF_OK;

    // get parameters
    amf_int32 streamCount=0;
    pStorageMain->GetProperty(AMF_VIDEO_STITCH_INPUTCOUNT, &streamCount);

    double lensCorrK1 = 0.0;
    res = pStorage->GetProperty(AMF_VIDEO_STITCH_LENS_CORR_K1, &lensCorrK1);
    double lensCorrK2 = 0.0;
    res = pStorage->GetProperty(AMF_VIDEO_STITCH_LENS_CORR_K2, &lensCorrK2);
    double lensCorrK3 = 0.0;
    res = pStorage->GetProperty(AMF_VIDEO_STITCH_LENS_CORR_K3, &lensCorrK3);
    double lensCorrOffX = 0.0;
    res = pStorage->GetProperty(AMF_VIDEO_STITCH_LENS_CORR_OFFX, &lensCorrOffX);
    double lensCorrOffY = 0.0;
    res = pStorage->GetProperty(AMF_VIDEO_STITCH_LENS_CORR_OFFY, &lensCorrOffY);
    double offset_x = 0;
    double offset_y = 0;
    double offset_z = 0;
    pStorage->GetProperty(AMF_VIDEO_CAMERA_OFFSET_X, &offset_x);
    pStorage->GetProperty(AMF_VIDEO_CAMERA_OFFSET_Y, &offset_y);
    pStorage->GetProperty(AMF_VIDEO_CAMERA_SCALE, &offset_z);

    amf_int64 lensCorrectionMode = AMF_VIDEO_STITCH_LENS_RECTILINEAR;
    pStorage->GetProperty(AMF_VIDEO_STITCH_LENS_MODE, &lensCorrectionMode);

    AMFRect crop = {};
    res = pStorage->GetProperty(AMF_VIDEO_STITCH_CROP, &crop);
    amf_int32 widthInputOrg  = widthInput;
    amf_int32 heightInputOrg = heightInput;

    double crop_offset_x = 0;
    double crop_offset_y = 0;

    if(crop.Width() > 0 && crop.Height() > 0)
    {
        crop_offset_x = 0;
        crop_offset_y = 0;
        widthInput = crop.Width();
        heightInput = crop.Height();
    }

    offset_x /= widthInputOrg / 2.0;
    offset_y /= heightInputOrg / 2.0;

    crop_offset_x /= widthInputOrg / 2.0;
    crop_offset_y /= heightInputOrg / 2.0;

    double pitch = 0.0;
    double yaw = 0.0;
    double roll = 0.0;

    pStorage->GetProperty(AMF_VIDEO_CAMERA_ANGLE_PITCH, &pitch);
    pStorage->GetProperty(AMF_VIDEO_CAMERA_ANGLE_YAW, &yaw);
    pStorage->GetProperty(AMF_VIDEO_CAMERA_ANGLE_ROLL, &roll);

    double hfov = M_PI / 2.0;
    pStorage->GetProperty(AMF_VIDEO_CAMERA_HFOV, &hfov);

    if(lensCorrectionMode == AMF_VIDEO_STITCH_LENS_RECTILINEAR)
    {
        offset_z = 1.0 / tan(hfov / 2.0);
    }

    offset_z = 1.0 - ((double)widthInput/ heightInput) * offset_z;

    double fishEyeZoom = 1.0;
    switch(lensCorrectionMode)
    {
    case AMF_VIDEO_STITCH_LENS_RECTILINEAR:
        lensCorrOffX /= widthInputOrg / 2.0;
        lensCorrOffY /= heightInputOrg / 2.0;
        break;
    case AMF_VIDEO_STITCH_LENS_FISHEYE_FULLFRAME:
        fishEyeZoom = 2.0;
        lensCorrOffX /= widthInputOrg / 2.0;
        lensCorrOffY /= heightInputOrg / 2.0;
        offset_z *= 1.11;
        break;
    case AMF_VIDEO_STITCH_LENS_FISHEYE_CIRCULAR:
        fishEyeZoom = 2.0;
        lensCorrOffX /= widthInputOrg / 2.0;
        lensCorrOffY /= widthInputOrg / 2.0;
        break;
    }

    //---------------------------------------------------------------------------------------------
    // square texture
    //---------------------------------------------------------------------------------------------

    float tex_l = 0.0f;
    float tex_t = 0.0f;
    float tex_w = 1.0f;
    float tex_h = 1.0f;

    float w = 2.0f;
    float h = 2.0f;

    float l = -1.0f;
    float t = -1.0f;
    float f = -1.0f;

    if(crop.Width() > 0 && crop.Height() > 0)
    {
        tex_l = (float)crop.left / widthInputOrg;
        tex_t = (float)crop.top / heightInputOrg;

        tex_w = (float)crop.Width() / widthInputOrg;
        tex_h = (float)crop.Height() / heightInputOrg;
    }

    double aspectX = 1.0f;
    double aspectY = 1.0f;
    if(widthInput > heightInput)
    {
        aspectX = (float)widthInput / heightInput;
    }
    else
    {
        aspectY = (float)heightInput / widthInput;
    }

    //---------------------------------------------------------------------------------------------
    // Create and set vertex buffer
    //---------------------------------------------------------------------------------------------
    XMMATRIX orientation = XMMatrixRotationRollPitchYaw((float)pitch, (float)yaw, (float)roll);
    XMMATRIX orientationRollPitch = XMMatrixRotationRollPitchYaw((float)pitch, (float)0, (float)roll);
    XMMATRIX orientationYaw = XMMatrixRotationRollPitchYaw((float)0, (float)yaw, (float)0);
    XMMATRIX textureReverse = XMMatrixRotationRollPitchYaw((float)0, (float)0, (float)M_PI);
    XMMATRIX aspect = XMMatrixScaling((float)aspectX , (float)aspectY , 1.0f);
    XMMATRIX aspectInv = XMMatrixScaling(1.0f/(float)aspectX , 1.0f/(float)aspectY , 1.0f);
    XMMATRIX translation = XMMatrixTranslation((float)lensCorrOffX, (float)lensCorrOffY, 0);
    XMMATRIX zoom = XMMatrixTranslation(0, 0, (float)offset_z);
    XMMATRIX crop_translation = XMMatrixTranslation((float)crop_offset_x, (float)crop_offset_y, 0);

    // normalized rect
    double leftB = -1.0;
    double topB = -1.0;
    double rightB = 1.0;
    double bottomB = 1.0;

    // scale
    leftB /= aspectX;
    topB /= aspectY;
    rightB /= aspectX;
    bottomB /= aspectY;

    // translate XY
    leftB -= lensCorrOffX;
    topB -= lensCorrOffY;
    rightB -= lensCorrOffX;
    bottomB -= lensCorrOffY;

    // fill DirectX::XMVECTOR* corners
    switch(streamCount)
    {
     case 2:
     case 4:
     case 6:
         corners.resize(4);
         corners[0] = XMVectorSet( 1.0f, 1.0f, -1.0f, 0.0f); //lt
         corners[1] = XMVectorSet( -1.0f, 1.0f, -1.0f, 0.0f); //rt
         corners[2] = XMVectorSet( -1.0f,  -1.0f, -1.0f, 0.0f); //rb
         corners[3] = XMVectorSet( 1.0f,  -1.0f, -1.0f, 0.0f); //lb

         corners[0] = XMVector3Transform(corners[0], orientation);
         corners[1] = XMVector3Transform(corners[1], orientation);
         corners[2] = XMVector3Transform(corners[2], orientation);
         corners[3] = XMVector3Transform(corners[3], orientation);

         // fill DirectX::XMVECTOR* center of sides
         sides.resize(4);
         sides[0] = XMVectorSet( 1.0f,  0.0f, -1.0f, 0.0f); //right
         sides[1] = XMVectorSet( 0.0f,  1.0f, -1.0f, 0.0f); //bottom
         sides[2] = XMVectorSet(-1.0f,  0.0f, -1.0f, 0.0f); //left
         sides[3] = XMVectorSet( 0.0f, -1.0f, -1.0f, 0.0f); //top

         sides[0] = XMVector3Transform(sides[0], orientation);
         sides[1] = XMVector3Transform(sides[1], orientation);
         sides[2] = XMVector3Transform(sides[2], orientation);
         sides[3] = XMVector3Transform(sides[3], orientation);

         break;
    }

    // scale based on Z
    leftB /= 1.0 + offset_z;
    topB /= 1.0 + offset_z;
    rightB /= 1.0 + offset_z;
    bottomB /= 1.0 + offset_z;

    // back to image
    texRect = XMVectorSet(float( (leftB + 1.0) / 2.0),  float((topB + 1.0) / 2.0), float((rightB + 1.0) / 2.0), float((bottomB + 1.0) / 2.0));
    borderRect.left = amf_int32((leftB + 1.0) / 2.0  * widthInput);
    borderRect.top = amf_int32((topB + 1.0) / 2.0 * heightInput);
    borderRect.right = amf_int32((rightB + 1.0) / 2.0 * widthInput);
    borderRect.bottom = amf_int32((bottomB + 1.0) / 2.0 * heightInput);

    // define camera plane
    XMVECTOR point1 = XMVectorSet( -1.0f, -1.0f, -1.0f, 0.0f);
    XMVECTOR point2 = XMVectorSet( 1.0f,  0.0f, -1.0f, 0.0f);
    XMVECTOR point3 = XMVectorSet( 1.0f,  1.0f, -1.0f, 0.0f);

    point1 = XMVector3Transform(point1, textureReverse);
    point1 = XMVector3Transform(point1, aspect);
    point1 = XMVector3Transform(point1, translation);
    point1 = XMVector3Transform(point1, zoom);
    point1 = XMVector3Transform(point1, orientation);

    point2 = XMVector3Transform(point2, textureReverse);
    point2 = XMVector3Transform(point2, aspect);
    point2 = XMVector3Transform(point2, translation);
    point2 = XMVector3Transform(point2, zoom);
    point2 = XMVector3Transform(point2, orientation);

    point3 = XMVector3Transform(point3, textureReverse);
    point3 = XMVector3Transform(point3, aspect);
    point3 = XMVector3Transform(point3, translation);
    point3 = XMVector3Transform(point3, zoom);
    point3 = XMVector3Transform(point3, orientation);

    plane = XMPlaneFromPoints(point1, point2, point3);

    planeCenter = XMVectorSet( 0.0f, 0.0f, -1.0f, 0.0f);
    planeCenter = XMVector3Transform(planeCenter, textureReverse);
    planeCenter = XMVector3Transform(planeCenter, aspect);
    planeCenter = XMVector3Transform(planeCenter, translation);
    planeCenter = XMVector3Transform(planeCenter, zoom);
    planeCenter = XMVector3Transform(planeCenter, orientation);


    float radiusCircular = 0;
    if(widthInput > heightInput)
    {
        radiusCircular = tex_w / 2.0f;
    }
    else
    {
        radiusCircular = tex_h / 2.0f;
    }
    radiusCircular*=1.2f;

    for(int y = 0; y <= m_iHeightTriangle; y++)
    {
        int countInRow = 0;
        for(int x = 0; x <= m_iWidthTriangle; x++)
        {
            TextureVertex v;
            float posx = l + (float) x / m_iWidthTriangle * w;
            float posy = t +(float) y / m_iHeightTriangle * h;
            float posz = f;

            v.Tex[0] = (tex_l + ((float) x / m_iWidthTriangle) * tex_w);
            v.Tex[1] = (tex_t + ((float) y / m_iHeightTriangle) * tex_h);

            if(v.Tex[0] < 0 || v.Tex[1] < 0 || v.Tex[0] > 1.0f || v.Tex[1] > 1.0f)
            {
                continue;
            }

          v.Tex[2] = CalcTransparencyTex(v.Tex[0], v.Tex[1], 1.0f);
          XMVECTOR vec= XMVectorSet(posx, posy, posz, 0.0f);

          vec = XMVector3Transform(vec, textureReverse);
          vec = XMVector3Transform(vec, crop_translation);
          vec = XMVector3Transform(vec, translation);
          vec = XMVector3Transform(vec, aspect);

          switch(lensCorrectionMode)
          {
          case AMF_VIDEO_STITCH_LENS_RECTILINEAR:
              vec = CorrectLensRadial(vec, lensCorrK1, lensCorrK2, lensCorrK3);
              break;
          case AMF_VIDEO_STITCH_LENS_FISHEYE_FULLFRAME:
              vec = CorrectLensRadialInverse(vec, lensCorrK1, lensCorrK2, lensCorrK3);
              vec = CorrectLensCircularFishEye(vec, hfov, 1.0, v.Tex[2]);
              break;
          case AMF_VIDEO_STITCH_LENS_FISHEYE_CIRCULAR:
              vec = CorrectLensRadialInverse(vec, lensCorrK1, lensCorrK2, lensCorrK3);
              vec = CorrectLensCircularFishEye(vec, hfov, 1.0, v.Tex[2]);
              break;
          default:
              break;
          }

          vec = XMVector3Transform(vec, zoom);
          vec = XMVector3Transform(vec, orientation);
          v.Pos[0] = XMVectorGetX(vec);
          v.Pos[1] = XMVectorGetY(vec);
          v.Pos[2] = XMVectorGetZ(vec);
          vertices.push_back(v);
          countInRow++;
        }

        if(countInRow > 0)
        {
            verticesRowSize.push_back(countInRow);
        }
    }

    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL StitchEngineBase::GetTransform(amf_int32 /* widthInput */, amf_int32 /* heightInput */, amf_int32 widthOutput, amf_int32 heightOutput, AMFPropertyStorage* pStorage, Transform& camera, Transform& transform, Transform* cubemap)
{
    amf_int32 streamCount=0;
    pStorage->GetProperty(AMF_VIDEO_STITCH_INPUTCOUNT, &streamCount);
    amf_int64 modeTmp = AMF_VIDEO_STITCH_OUTPUT_MODE_PREVIEW;
    pStorage->GetProperty(AMF_VIDEO_STITCH_OUTPUT_MODE, &modeTmp);
    AMF_VIDEO_STITCH_OUTPUT_MODE_ENUM outputMode = (AMF_VIDEO_STITCH_OUTPUT_MODE_ENUM)modeTmp;

    switch(outputMode)
    {
    case AMF_VIDEO_STITCH_OUTPUT_MODE_CUBEMAP:
    {
        XMMATRIX &worldViewProjection = *((XMMATRIX*)&transform);
        worldViewProjection = XMMatrixTranspose(XMMatrixIdentity());

        for(int f = 0; f < 6; f++)
        {
            XMVECTOR Eye = XMVectorSet(0.0f ,0.0f, 0.0f, 0.0f);
            XMVECTOR At = XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f);
            XMMATRIX correction = XMMatrixRotationZ(XM_PI) * XMMatrixRotationY(XM_PI);
            XMMATRIX rotation = XMMatrixIdentity();
            switch(f)
            {
            case 0:  //OK
                rotation = XMMatrixRotationY(-XM_PIDIV2) * XMMatrixRotationZ(-XM_PIDIV2);
                break; // +X
            case 1: //OK
                rotation = XMMatrixRotationY(XM_PIDIV2) * XMMatrixRotationZ(XM_PIDIV2);
                break; // -X
            case 2:  //OK
                rotation = XMMatrixIdentity();
                break; // +Y
            case 3: //OK
                rotation = XMMatrixRotationY(XM_PI) * XMMatrixRotationZ(XM_PI);
                break; // -Y
            case 4: // OK
                rotation = XMMatrixRotationX(-XM_PIDIV2);
                break; // +Z
            case 5:  //OK
                rotation = XMMatrixRotationX(-XM_PIDIV2) * XMMatrixRotationY(XM_PI);
                break; // -Z
            }

            XMVECTOR Up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            XMMATRIX orientation = XMMatrixLookToLH(Eye, At, Up);
            XMMATRIX projection = XMMatrixPerspectiveFovLH(XM_PIDIV2, (float)1 / 1, 0.1f, 10.0f);
            XMMATRIX &cubeMapWorldViewProjection = *((XMMATRIX*)&cubemap[f]);
            cubeMapWorldViewProjection = XMMatrixTranspose(orientation * correction * rotation * projection);
       }
    }
        break;
    case AMF_VIDEO_STITCH_OUTPUT_MODE_PREVIEW:
    {
        double angle = 0.0;
        pStorage->GetProperty(AMF_VIDEO_STITCH_VIEW_ROTATE_X, &angle);
        float orientationX = (float)angle;
        pStorage->GetProperty(AMF_VIDEO_STITCH_VIEW_ROTATE_Y, &angle);
        float orientationY = (float)angle;
        pStorage->GetProperty(AMF_VIDEO_STITCH_VIEW_ROTATE_Z, &angle);
        float orientationZ = (float)angle;
        XMMATRIX &rotation = *((XMMATRIX *)&camera);
        XMVECTOR Eye = XMVectorSet(0.0f ,0.0f, 0.0f, 0.0f);
        XMVECTOR At = XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f);
        XMVECTOR Up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        XMMATRIX orientation = XMMatrixLookToLH(Eye, At, Up);
        XMMATRIX viewX = XMMatrixRotationX(orientationX);
        XMMATRIX viewY = XMMatrixRotationY(orientationY);
        XMMATRIX viewZ = XMMatrixRotationZ(orientationZ);
        rotation =  viewY * (rotation * viewZ * viewX); // calc and store
        XMMATRIX projection = XMMatrixPerspectiveFovLH(XM_PIDIV2, (float)widthOutput / heightOutput, 0.1f, 10.0f);
        XMMATRIX &worldViewProjection = *((XMMATRIX*)&transform);
        worldViewProjection = XMMatrixTranspose(orientation * rotation * projection);
    }
        break;
    case AMF_VIDEO_STITCH_OUTPUT_MODE_EQUIRECTANGULAR:
    {
        XMMATRIX worldViewProjection = XMMatrixIdentity();
        memcpy(transform.m_WorldViewProjection, &worldViewProjection, sizeof(transform.m_WorldViewProjection));
    }
        break;
    }

    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
static XMVECTOR CorrectLensRadial(XMVECTOR src, double a, double b, double c)
{
    double d = 1.0 - a - b - c; //balanced scale
    double x = XMVectorGetX(src);
    double y = XMVectorGetY(src);
    double r2 = (x*x) + (y*y);
    double r1 = sqrt(r2);
    double r3 = r2 * r1;
    double cDist = d + a * r3 + b * r2 + c * r1;
    double xd = x * cDist;
    double yd = y * cDist;
    double ud = xd;
    double vd = yd;
    XMVECTOR dst = XMVectorSet((float)ud, (float)vd, XMVectorGetZ(src), XMVectorGetW(src));
    return dst;
}

static void matrix_inv_mult( double m[3][3], double vector[3] )
{
    int i;
    double v0 = vector[0];
    double v1 = vector[1];
    double v2 = vector[2];

    for(i=0; i<3; i++)
    {
        vector[i] = m[0][i] * v0 + m[1][i] * v1 + m[2][i] * v2;
    }
}

static void matrix_matrix_mult( double m1[3][3],double m2[3][3],double result[3][3])
{
    int i,k;

    for(i=0;i<3;i++)
    {
        for(k=0; k<3; k++)
        {
            result[i][k] = m1[i][0] * m2[0][k] + m1[i][1] * m2[1][k] + m1[i][2] * m2[2][k];
        }
    }
}

static void SetMatrix( double a, double b, double c , double m[3][3], int cl )
{
    double mx[3][3], my[3][3], mz[3][3], dummy[3][3];
    // Calculate Matrices;
    mx[0][0] = 1.0 ;                mx[0][1] = 0.0 ;              mx[0][2] = 0.0;
    mx[1][0] = 0.0 ;                mx[1][1] = cos(a) ;           mx[1][2] = sin(a);
    mx[2][0] = 0.0 ;                mx[2][1] =-mx[1][2] ;         mx[2][2] = mx[1][1];

    my[0][0] = cos(b);              my[0][1] = 0.0 ;              my[0][2] =-sin(b);
    my[1][0] = 0.0 ;                my[1][1] = 1.0 ;              my[1][2] = 0.0;
    my[2][0] = -my[0][2];           my[2][1] = 0.0 ;              my[2][2] = my[0][0];

    mz[0][0] = cos(c) ;             mz[0][1] = sin(c) ;           mz[0][2] = 0.0;
    mz[1][0] =-mz[0][1] ;           mz[1][1] = mz[0][0] ;         mz[1][2] = 0.0;
    mz[2][0] = 0.0 ;                mz[2][1] = 0.0 ;              mz[2][2] = 1.0;

    if( cl )
    {
        matrix_matrix_mult( mz, mx,     dummy);
    }
    else
    {
        matrix_matrix_mult( mx, mz,     dummy);
    }

    matrix_matrix_mult( dummy, my, m);
}

static XMVECTOR CorrectLensCircularFishEye(XMVECTOR src , double hfov, double f, float &transparency)
{
    double x = XMVectorGetX(src);
    double y = XMVectorGetY(src);

    double fov2 = hfov / 2.0;
    double pheta = atan2(y , x);

    double r = sqrt(x * x + y * y) ;
    double theta = r / f * fov2; // equidistance projection

    double new_r = 1.0;
    double new_x = 0;
    double new_y = 0;
    double new_z = -new_r * cos(theta);

    if(fabs(theta) > M_PI / 2.0)
    {
        transparency = 0.0f;
        new_x = new_r * sin(theta) * cos(pheta);
        new_y = new_r * sin(theta) * sin(pheta);
    }
    else
    {
        transparency = 1.0f;
        new_x = new_r * sin(theta) * cos(pheta);
        new_y = new_r * sin(theta) * sin(pheta);
    }

    XMVECTOR dst = XMVectorSet((float)new_x, (float)new_y, (float)new_z, XMVectorGetW(src));
    return dst;
}

//-------------------------------------------------------------------------------------------------
static XMVECTOR CorrectLensRadialInverse(XMVECTOR src, double a, double b, double c)
{
    double d = 1.0 - a - b - c; //balanced scale
    double x = XMVectorGetX(src) ;
    double y = XMVectorGetY(src) ;
    double r2 = (x*x) + (y*y);
    double r1 = sqrt(r2);
    double rd = r1;
    double rs = rd;
    double f = (((a * rs + b) * rs + c) * rs + d) * rs;

#define MAXITER 100
#define R_EPS 1.0e-6

    int iter = 0;
    while( std::abs(f - rd) > R_EPS && iter++ < MAXITER )
    {
        rs = rs - (f - rd) / ((( 4 * a * rs + 3 * b) * rs  + 2 * c) * rs + 1 * d);
        f = (((a * rs + b) * rs + c) * rs + d) * rs;
    }

    double scale = rd == 0.0  || iter >= MAXITER  ? 1.0 : rs / rd;
    double xd = x * scale;
    double yd = y * scale;
    double ud = xd;
    double vd = yd;
    XMVECTOR dst = XMVectorSet((float)ud, (float)vd, XMVectorGetZ(src), XMVectorGetW(src));
    return dst;
}

double my_round(double x)
{
    return (int)x;
}

double cubicroot(double num )
{
  double flag = 1.0;
  if (num < 0)
  {
        flag = -1.0;
  }
  num = num - num - num;
  double x0 = num / 2.;
  double x1 = x0 - (((x0 * x0 * x0) - num) / (3. * x0 * x0));
  while(my_round(x0) != my_round(x1))
  {
    x0 = x1;
    x1 = x0 - (((x0 * x0 * x0) - num) / (3. * x0 * x0));
  }
  return x1 * flag;
}

DirectX::XMVECTOR StitchEngineBase::CartesianToSpherical(DirectX::XMVECTOR src)
{
    double x = XMVectorGetX(src);
    double y = XMVectorGetY(src);
    double z = XMVectorGetZ(src);
    double r = sqrt(x *x + y * y + z * z);
    double theta = acos(z / r); // elevation
    double pheta = atan2(y, x); //azimuth
    XMVECTOR dst = XMVectorSet((float)theta, (float)pheta, (float)r, XMVectorGetW(src));
    return dst;
}

XMVECTOR StitchEngineBase::MakeSphere(XMVECTOR src, float centerX,float centerY,float centerZ, float newRadius)
{
    double x = XMVectorGetX(src) - centerX;
    double y = XMVectorGetY(src) - centerY;
    double z = XMVectorGetZ(src) - centerZ;
    double r = sqrt(x *x + y * y + z * z);
    double theta = acos(z / r); // elevation
    double pheta = atan2(y, x); //azimuth
    double new_r = newRadius;
    double new_x = new_r * sin(theta) * cos(pheta);
    double new_y = new_r * sin(theta) * sin(pheta);
    double new_z = new_r * cos(theta);
    XMVECTOR dst = XMVectorSet((float)new_x + centerX, (float)new_y + centerY, (float)new_z + centerZ, XMVectorGetW(src));
    return dst;
}

void StitchEngineBase::MakeSphere(TextureVertex &v, float centerX,float centerY,float centerZ, float newRadius)
{
    double x = v.Pos[0] - centerX;
    double y = v.Pos[1] - centerY;
    double z = v.Pos[2] - centerZ;
    double r = sqrt(x *x + y * y + z * z);
    double theta = acos(z / r); // elevation
    double pheta = atan2(y, x); //azimuth
    double new_r = newRadius;
    double new_x = new_r * sin(theta) * cos(pheta);
    double new_y = new_r * sin(theta) * sin(pheta);
    double new_z = new_r * cos(theta);

    v.Pos[0] = (float)new_x + centerX;
    v.Pos[1] = (float)new_y + centerY;
    v.Pos[2] = (float)new_z + centerZ;
}

//-------------------------------------------------------------------------------------------------
static float CalcTransparencyTex(float posx, float posy, float zoom_z)
{
    float transparency = 0.01f;
    float transparencyBorder = 1.0f;

#if defined(DEBUG_TRANSPARENT)
    float transparencyMax = 0.3f;
    float transparencyMin = 0.3f;
#else
    float transparencyMax = 1.0f;
    float transparencyMin = 0.0f;
#endif

     transparencyBorder /= zoom_z;
     float transparencyVertex = transparencyMax;

     if(posx < 0 || posx > transparencyBorder )
     {
         transparencyVertex *= transparencyMin;
     }
     else if(posx < 0 + transparency)
     {
         float x0 = transparency;
         float x1 = 0;
         float y0 = transparencyMax;
         float y1 = transparencyMin;
         float val = y0 + (y1 - y0) * (posx - x0) / (x1 - x0);
         transparencyVertex *= val;
     }
     else if(posx > transparencyBorder - transparency)
     {
         float x0 = transparencyBorder - transparency;
         float x1 = transparencyBorder;
         float y0 = transparencyMax;
         float y1 = transparencyMin;
         float val = y0 + (y1 - y0) * (posx - x0) / (x1 - x0);
         transparencyVertex *= val;
     }

     if(posy < 0 || posy > transparencyBorder )
     {
         transparencyVertex = transparencyMin;
     }
     else if(posy < 0 + transparency)
     {
         float x0 = transparency;
         float x1 = 0;
         float y0 = transparencyMax;
         float y1 = transparencyMin;
         float val = y0 + (y1 - y0) * (posy - x0) / (x1 - x0);
         transparencyVertex *= val;
     }
     else if(posy > transparencyBorder - transparency)
     {
         float x0 = transparencyBorder - transparency;
         float x1 = transparencyBorder;
         float y0 = transparencyMax;
         float y1 = transparencyMin;
         float val = y0 + (y1 - y0) * (posy - x0) / (x1 - x0);
         transparencyVertex *= val;
     }
     return transparencyVertex;
}

//-------------------------------------------------------------------------------------------------
#define MY_SIGN(a) (a == 0 ? 0 : (a < 0 ? -1 : 1)  )
#define EPSILON 0.00001f
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL StitchEngineBase::ApplyMode(
    amf_int32 /* widthOutput */ , amf_int32 /* heightOutput */,
    std::vector<TextureVertex> &vertices,
    std::vector<amf_uint32> &verticesRowSize,
    std::vector<TextureVertex> &verticesProjected,
    std::vector<amf_uint16> &indexes,
    AMFPropertyStorage *pStorage)
{
    amf_int64 modeTmp = AMF_VIDEO_STITCH_OUTPUT_MODE_PREVIEW;
    pStorage->GetProperty(AMF_VIDEO_STITCH_OUTPUT_MODE, &modeTmp);
    AMF_VIDEO_STITCH_OUTPUT_MODE_ENUM outputMode = (AMF_VIDEO_STITCH_OUTPUT_MODE_ENUM)modeTmp;

    verticesProjected.clear();
    indexes.clear();

    switch(outputMode)
    {
    case AMF_VIDEO_STITCH_OUTPUT_MODE_CUBEMAP:
    case AMF_VIDEO_STITCH_OUTPUT_MODE_PREVIEW:
        for(amf_uint32 y = 0; y < (amf_uint32)verticesRowSize.size() - 1; y++)
        {
            int base1 = y * verticesRowSize[y];
            int base2 = (y + 1) * verticesRowSize[y];

            for(amf_uint32 x = 0; x < verticesRowSize[y] - 1; x++)
            {
                // 2 trinagles
                for( int i = 0; i < 2; i++)
                {
                    int index1 = i == 0 ? base1 + x : base2 + x;
                    int index2 = i == 0 ? base2 + x : base1 + x + 1;
                    int index3 = i == 0 ? base1 + x + 1 : base2 + x + 1;

                    if(vertices[index1].Tex[2] != 0.0f || vertices[index2].Tex[2] != 0.0f || vertices[index3].Tex[2] != 0.0f) // cull transparent vertexes
                    {
                        verticesProjected.push_back(vertices[index1]);
                        verticesProjected.push_back(vertices[index2]);
                        verticesProjected.push_back(vertices[index3]);
                    }
                }
            }
        }
        break;

    case AMF_VIDEO_STITCH_OUTPUT_MODE_EQUIRECTANGULAR:
        {
        // make vertical plane along Z
        XMVECTOR planePoint1 = XMVectorSet( 0.0f, 0.0f, 0.0f, 0.0f);
        XMVECTOR planePoint2 = XMVectorSet( 0.0f, 0.0f, 1.0f, 0.0f);
        XMVECTOR planePoint3 = XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f);
        XMVECTOR plane = XMPlaneFromPoints(planePoint1, planePoint2, planePoint3);

        float a = XMVectorGetX(plane);
        float b = XMVectorGetY(plane);
        float c = XMVectorGetZ(plane);
        float d = XMVectorGetW(plane);
        float sqrtabsd = sqrtf(a * a + b * b + c * c + d * d);

        for(amf_uint32 y = 0; y < (amf_uint32)verticesRowSize.size() - 1; y++)
        {
            int base1 = y * verticesRowSize[y];
            int base2 = (y + 1) * verticesRowSize[y];

            for(amf_uint32 x = 0; x < verticesRowSize[y] - 1; x++)
            {
                // 2 trinagles
                for( int i = 0; i < 2; i++)
                {
                    int index1 = i == 0 ? base1 + x     : base2 + x;
                    int index2 = i == 0 ? base2 + x     : base1 + x + 1;
                    int index3 = i == 0 ? base1 + x + 1 : base2 + x + 1;

                    TextureVertex v1 = vertices[index1];
                    TextureVertex v2 = vertices[index2];
                    TextureVertex v3 = vertices[index3];

                    MakeSphere(v1, 0, 0, 0, 1.0f);
                    MakeSphere(v2, 0, 0, 0, 1.0f);
                    MakeSphere(v3, 0, 0, 0, 1.0f);


                    float dist1 = (a * v1.Pos[0] + b * v1.Pos[1] + c * v1.Pos[2] + d) / sqrtabsd;
                    float dist2 = (a * v2.Pos[0] + b * v2.Pos[1] + c * v2.Pos[2] + d) / sqrtabsd;
                    float dist3 = (a * v3.Pos[0] + b * v3.Pos[1] + c * v3.Pos[2] + d) / sqrtabsd;

                    if(dist1 >= 0 && dist1 < EPSILON)
                    {
                        v1.Pos[0] = -EPSILON;
                    }
                    else if(dist1 < 0 && dist1 > -EPSILON)
                    {
                        v1.Pos[0] = EPSILON;
                    }
                    if(dist2 >= 0 && dist2 < EPSILON)
                    {
                        v2.Pos[0] = -EPSILON;
                    }
                    else if(dist2 < 0 && dist2 > -EPSILON)
                    {
                        v2.Pos[0] = EPSILON;
                    }
                    if(dist3 >= 0 && dist3 < EPSILON)
                    {
                        v3.Pos[0] = -EPSILON;
                    }
                    else if(dist3 < 0 && dist3 > -EPSILON)
                    {
                        v3.Pos[0] = EPSILON;
                    }

                    int sumOfSigns = abs(MY_SIGN(dist1) + MY_SIGN(dist2) + MY_SIGN(dist3));


                    if((v1.Pos[2] < 0 || v2.Pos[2] < 0 || v3.Pos[2] < 0) &&  sumOfSigns != 3 )
                    {
                        if(MY_SIGN(dist1) != MY_SIGN(dist2) && MY_SIGN(dist1) != MY_SIGN(dist3))
                        {
                        }
                        else if(MY_SIGN(dist2) != MY_SIGN(dist1) && MY_SIGN(dist2) != MY_SIGN(dist3))
                        {
                            TextureVertex tmp = v1;
                            v1 = v2;
                            v2 = v3;
                            v3 = tmp;
                        }
                        else if(MY_SIGN(dist3) != MY_SIGN(dist1) && MY_SIGN(dist3) != MY_SIGN(dist2))
                        {
                            TextureVertex tmp = v1;
                            v1 = v3;
                            v3 = v2;
                            v2 = tmp;
                        }
                        XMVECTOR point1 = XMVectorSet( v1.Pos[0], v1.Pos[1], v1.Pos[2], 0.0f);
                        XMVECTOR point2 = XMVectorSet( v2.Pos[0], v2.Pos[1], v2.Pos[2], 0.0f);
                        XMVECTOR point3 = XMVectorSet( v3.Pos[0], v3.Pos[1], v3.Pos[2], 0.0f);

                        XMVECTOR point4 = XMPlaneIntersectLine(plane, point1, point2);
                        XMVECTOR point5 = XMPlaneIntersectLine(plane, point1, point3);

                        // move points to one side on a bit
                        XMVECTOR point4_1 =XMVectorAdd(point4 , XMVectorScale(XMVectorSubtract(point1, point4), EPSILON));
                        XMVECTOR point5_1 =XMVectorAdd(point5 , XMVectorScale(XMVectorSubtract(point1, point5), EPSILON));

                        XMVECTOR point4_2 =  XMVectorSubtract(point4 , XMVectorScale(XMVectorSubtract(point1, point4), EPSILON));
                        XMVECTOR point5_2 =  XMVectorSubtract(point5 , XMVectorScale(XMVectorSubtract(point1, point5), EPSILON));

                        // move points to another one side on a bit

                        TextureVertex v4;
                        v4.Pos[0] = XMVectorGetX(point4_1);
                        v4.Pos[1] = XMVectorGetY(point4_1);
                        v4.Pos[2] = XMVectorGetZ(point4_1);

                        float Distance1 = XMVectorGetX(XMVector2Length(XMVectorSubtract(point1, point4)));
                        float Distance2 = XMVectorGetX(XMVector2Length(XMVectorSubtract(point1, point2)));


                        v4.Tex[0] = v1.Tex[0] - (v1.Tex[0] - v2.Tex[0])  * Distance1 / Distance2;
                        v4.Tex[1] = v1.Tex[1] - (v1.Tex[1] - v2.Tex[1])  * Distance1 / Distance2;
                        v4.Tex[2] = v1.Tex[2] - (v1.Tex[2] - v2.Tex[2])  * Distance1 / Distance2;

                        TextureVertex v5;
                        v5.Pos[0] = XMVectorGetX(point5_1);
                        v5.Pos[1] = XMVectorGetY(point5_1);
                        v5.Pos[2] = XMVectorGetZ(point5_1);

                        float Distance3 = XMVectorGetX(XMVector2Length(XMVectorSubtract(point1, point5)));
                        float Distance4 = XMVectorGetX(XMVector2Length(XMVectorSubtract(point1, point3)));


                        v5.Tex[0] = v1.Tex[0] - (v1.Tex[0] - v3.Tex[0])  * Distance3 / Distance4;
                        v5.Tex[1] = v1.Tex[1] - (v1.Tex[1] - v3.Tex[1])  * Distance3 / Distance4;
                        v5.Tex[2] = v1.Tex[2] - (v1.Tex[2] - v3.Tex[2])  * Distance3 / Distance4;


                        if(v1.Tex[2] != 0.0f || v4.Tex[2] != 0.0f || v5.Tex[2] != 0.0f) // cull transparent vertexes
                        {
                            verticesProjected.push_back(v1);
                            verticesProjected.push_back(v4);
                            verticesProjected.push_back(v5);
                        }
                        v4.Pos[0] = XMVectorGetX(point4_2);
                        v4.Pos[1] = XMVectorGetY(point4_2);
                        v4.Pos[2] = XMVectorGetZ(point4_2);
                        v5.Pos[0] = XMVectorGetX(point5_2);
                        v5.Pos[1] = XMVectorGetY(point5_2);
                        v5.Pos[2] = XMVectorGetZ(point5_2);


                        if(v2.Tex[2] != 0.0f || v4.Tex[2] != 0.0f || v5.Tex[2] != 0.0f) // cull transparent vertexes
                        {
                            verticesProjected.push_back(v2);
                            verticesProjected.push_back(v4);
                            verticesProjected.push_back(v5);
                        }
                        if(v2.Tex[2] != 0.0f || v5.Tex[2] != 0.0f || v3.Tex[2] != 0.0f) // cull transparent vertexes
                        {
                            verticesProjected.push_back(v2);
                            verticesProjected.push_back(v5);
                            verticesProjected.push_back(v3);
                        }
                    }
                    else
                    {
                        if(v1.Tex[2] != 0.0f || v2.Tex[2] != 0.0f || v3.Tex[2] != 0.0f) // cull transparent vertexes
                        {
                            verticesProjected.push_back(v1);
                            verticesProjected.push_back(v2);
                            verticesProjected.push_back(v3);
                        }
                    }
                }
            }
        }

        for(amf_int32 i = 0; i < (amf_int32)verticesProjected.size(); i++)
        {
            TextureVertex &v = verticesProjected[i];

            XMVECTOR vec = XMVectorSet(v.Pos[0], v.Pos[1], v.Pos[2], 0);

            vec = CartesianToEquirectangular(vec);

            v.Pos[0] = XMVectorGetX(vec);
            v.Pos[1] = XMVectorGetY(vec);
            v.Pos[2] = XMVectorGetZ(vec);

        }
        }
        break;
    }
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
XMVECTOR CartesianToEquirectangular(XMVECTOR src)
{
    double x = XMVectorGetX(src);
    double y = XMVectorGetY(src);
    double z = XMVectorGetZ(src);

    // from cartesan to spheric
    double r = sqrt(x *x + y * y + z * z);
    double pheta = atan2(x, z); // azimuth - Longitude  -PI : PI
    double theta = acos(y / r); // elevation - Latitude   0 : PI

    // handle imprefections of acos
    if(fabs(y / r + 1.0) < 0.001)
    {
        theta = XM_PI;
    }
    if(fabs(y / r - 1.0) < 0.001)
    {
        theta = 0;
    }

    // Equirectangular mapping from spherical to flat
    double out_x = pheta / XM_PI;
    double out_y = -2.0 * theta / XM_PI +1.0;
    double new_x = (float)out_x;
    double new_y = (float)out_y;
    double new_z = 0;
    XMVECTOR dst = XMVectorSet((float)new_x, (float)new_y, (float)new_z, XMVectorGetW(src));
    return dst;
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
