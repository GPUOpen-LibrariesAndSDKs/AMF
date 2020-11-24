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
//
// Copyright (c) 2018 Advanced Micro Devices, Inc. All rights reserved.
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
///-------------------------------------------------------------------------
///  @file   VideoCompositor.h
///  @brief  AMFChromaKey interface
///-------------------------------------------------------------------------
#pragma once

#include "public/include/components/Component.h"
#include "public/common/PropertyStorageExImpl.h"
#include "public/include/components/ChromaKey.h"
#include "public/include/core/Context.h"

#include <d3d11.h>

namespace amf
{
    //-------------------------------------------------------------------------------------------------

    class AMFChromaKeyImpl : 
        public AMFInterfaceBase,
        public AMFPropertyStorageExImpl<AMFComponentEx>
    {
    //-------------------------------------------------------------------------------------------------
        typedef AMFInterfaceImpl < AMFPropertyStorageExImpl <AMFInput> > baseclassInputProperty;
        class AMFChromaKeyInputImpl :
            public baseclassInputProperty
        {
            friend class AMFChromaKeyImpl;
        public:
//            static const amf_uint32      KEYCOLORDEF = 0x00C25D56; ////YUV, 194, 135, 86
            static const amf_uint32      KEYCOLORDEF = 0x3085e164; ////YUV, 194, 135, 86 ->10bit
            AMF_BEGIN_INTERFACE_MAP
                AMF_INTERFACE_ENTRY(AMFInput)
                AMF_INTERFACE_CHAIN_ENTRY(AMFPropertyStorageExImpl <AMFInput>)
                AMF_INTERFACE_CHAIN_ENTRY(baseclassInputProperty)
                AMF_END_INTERFACE_MAP

            AMFChromaKeyInputImpl(AMFChromaKeyImpl* pHost, amf_int32 index);
            virtual ~AMFChromaKeyInputImpl();
            // AMFInput inteface
            virtual AMF_RESULT  AMF_STD_CALL SubmitInput(AMFData* pData);
            virtual void        AMF_STD_CALL OnPropertyChanged(const wchar_t* pName);
        protected:
            AMFChromaKeyImpl*   m_pHost;
            AMFSurfacePtr       m_pSurface;
        };

        typedef AMFInterfacePtr_T<AMFChromaKeyInputImpl>    AMFChromaKeyInputImplPtr;

    public:
        // interface access
        AMF_BEGIN_INTERFACE_MAP
            AMF_INTERFACE_MULTI_ENTRY(AMFComponent)
            AMF_INTERFACE_MULTI_ENTRY(AMFComponentEx)
            AMF_INTERFACE_CHAIN_ENTRY(AMFPropertyStorageExImpl <AMFComponentEx>)
            AMF_END_INTERFACE_MAP

        AMFChromaKeyImpl(AMFContext* pContext);
        virtual ~AMFChromaKeyImpl();

        // AMFComponent interface
        virtual AMF_RESULT  AMF_STD_CALL Init(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height);
        virtual AMF_RESULT  AMF_STD_CALL ReInit(amf_int32 width,amf_int32 height);
        virtual AMF_RESULT  AMF_STD_CALL Terminate();
        virtual AMF_RESULT  AMF_STD_CALL Drain();
        virtual AMF_RESULT  AMF_STD_CALL Flush();
        virtual AMF_RESULT  AMF_STD_CALL SubmitInput(AMFData* pData);
        virtual AMF_RESULT  AMF_STD_CALL QueryOutput(AMFData** ppData);
        virtual AMFContext* AMF_STD_CALL GetContext() { return m_pContext; }
        virtual AMF_RESULT  AMF_STD_CALL SetOutputDataAllocatorCB(AMFDataAllocatorCB* callback)
                                            { m_pDataAllocatorCB = callback; return AMF_OK; }
        virtual AMF_RESULT  AMF_STD_CALL GetCaps(AMFCaps** ppCaps);
        virtual AMF_RESULT  AMF_STD_CALL Optimize(AMFComponentOptimizationCallback* pCallback);

        //  AMFComponentPrivate methods
        virtual void       AMF_STD_CALL OnPropertyChanged(const wchar_t* pName);

        // AMFComponentEx interface
        virtual amf_int32   AMF_STD_CALL  GetInputCount()  { return m_iInputCount; };
        virtual amf_int32   AMF_STD_CALL  GetOutputCount() { return 0; };

        virtual AMF_RESULT  AMF_STD_CALL  GetInput(amf_int32 index, AMFInput** ppInput);
        virtual AMF_RESULT  AMF_STD_CALL  GetOutput(amf_int32 index, AMFOutput** ppOutput);
    private:
        AMFChromaKeyImpl(const AMFChromaKeyImpl&);
        AMF_RESULT AllocOutputSurface();
        AMF_RESULT AllocOutputSurface(amf_int32 width, amf_int32 height, amf_pts pts, amf_pts duration,
            AMF_SURFACE_FORMAT format, AMF_MEMORY_TYPE memoryType, AMF_FRAME_TYPE type, AMFSurface** ppSurface);
        AMF_RESULT AllocTempSurface(amf_int32 width, amf_int32 height, amf_pts pts, amf_pts duration,
            AMF_SURFACE_FORMAT format, AMF_MEMORY_TYPE memoryType, AMF_FRAME_TYPE type, AMFSurface** ppSurface);
        AMFChromaKeyImpl& operator=(const AMFChromaKeyImpl&);
        void ResetInputs();

        AMF_RESULT InitKernels();
        AMF_RESULT InitKernelsCL();

        AMF_RESULT UpdateKeyColor(AMFSurfacePtr pSurfaceIn);
        AMF_RESULT Process(AMFSurfacePtr pSurfaceIn, AMFSurfacePtr pSurfaceOut, AMFSurfacePtr pSurfaceMask); //generate mask
        AMF_RESULT Blur(AMFSurfacePtr pSurfaceIn, AMFSurfacePtr pSurfaceOut, amf_int32 iRadius);
        AMF_RESULT Erode(AMFSurfacePtr pSurfaceIn, AMFSurfacePtr pSurfaceOut, amf_int32 iErosionSize, bool bDiff);
        AMF_RESULT Dilate(AMFSurfacePtr pSurfaceIn, AMFSurfacePtr pSurfaceOut);
        AMF_RESULT Blend(AMFSurfacePtr pSurfaceIn, AMFSurfacePtr pSurfaceMaskSpill, AMFSurfacePtr pSurfaceMaskBlur,
            AMFSurfacePtr pSurfaceOut);
        AMF_RESULT BlendBK(AMFSurfacePtr pSurfaceIn, AMFSurfacePtr pSurfaceInBK, AMFSurfacePtr pSurfaceMaskSpill, 
            AMFSurfacePtr pSurfaceMaskBlur, AMFSurfacePtr pSurfaceOut);
        AMF_RESULT HistoUV(AMFSurfacePtr pSurfaceIn, AMFBufferPtr pSurfaceOut);
        AMF_RESULT HistoUVSort(AMFBufferPtr pBufferHistoIn, AMFBufferPtr pBufferHistoOut);
        AMF_RESULT HistoLocateLuma(AMFSurfacePtr pSurfaceIn, AMFBufferPtr pBufferLuma, amf_int32& keyColor);
        AMF_RESULT Bokeh(AMFSurfacePtr pSurfaceIn, AMFSurfacePtr pSurfaceOut);
        void       SetDX11Format(AMFSurfacePtr pSurface, AMF_PLANE_TYPE planeType);
        bool       IsYUV422(AMFSurfacePtr pSurface);
        bool       IsY210(AMFSurfacePtr pSurface);
        AMF_RESULT ConvertRGBtoYUV(AMFSurfacePtr pSurfaceIn, AMFSurfacePtr pSurfaceOut);
        AMF_RESULT ConvertV210toY210(AMFSurfacePtr pSurfaceIn, AMFSurfacePtr pSurfaceOut);
        AMF_RESULT ReleaseResource();
        bool Is8bit(AMFSurfacePtr pSurface);
        amf_uint32 GetColorTransferMode(AMFSurfacePtr pSurfaceIn);
        amf_uint32 GetColorTransferModeDst(AMFSurfacePtr pSurfaceOut, amf_int64 iBypass);
        AMF_RESULT ConvertFormat(AMFSurfacePtr pSurfaceIn, AMFSurface** ppSurface);

        //debug functions
        AMF_RESULT DumpSurface(AMFSurfacePtr pSurfaceIn);
        AMF_RESULT DumpBuffer(AMFBufferPtr pBufferIn);
        AMF_RESULT ReadData(AMFBufferPtr pBufferIn, amf_uint32 length, amf_uint8* pData);
        AMF_RESULT SaveSurface(AMFSurfacePtr pSurfaceIn, std::wstring fileName);
        AMF_RESULT SaveToBmp(amf_uint8* pData, std::wstring fileName, amf_int32 width, amf_int32 height, amf_int32 channels, amf_int32 pitch);


        AMFContextPtr           m_pContext;         //context
        AMFComputePtr           m_Compute;          //compute object
        AMFDataAllocatorCBPtr   m_pDataAllocatorCB; //allocator callback

        amf_vector<AMFChromaKeyInputImplPtr>   m_Inputs;    //input streams

        AMF_MEMORY_TYPE     m_outputMemoryType; //output mmeory type, default: DX11
        AMF_MEMORY_TYPE     m_deviceMemoryType; //device memory type, default: DX11

        AMF_SURFACE_FORMAT  m_formatIn;     //input format,  default: NV12
        AMF_SURFACE_FORMAT  m_formatOut;    //output format, default: RGBA
        AMFSize             m_sizeIn;       //input frame size
        AMFSize             m_sizeOut;      //output frame size
        AMFCriticalSection  m_sync;         //sync object

        AMFSurfacePtr       m_pSurfaceOut;        //output surface
        AMFBufferPtr        m_pBufferHistoUV;     //UV histogram 
        AMFBufferPtr        m_pBufferHistoSort;   //64 bin UV histogram 
        AMFBufferPtr        m_pBufferLuma;        //size of 64 for parallel processing, to find the luma value for pixels with the same chroma value
        AMFPoint            m_posKeyColor;        //mouse click postion for picking up the key color
        amf_int32           m_iKeyColor[3];       //key color list
        amf_int32           m_iKeyColorCount;     //key color count
        amf_int32           m_iKeyColorRangeMin;  //chroma threshold, minimum 
        amf_int32           m_iKeyColorRangeMax;  //chroma threshold, maximum
        amf_int32           m_iKeyColorRangeExt;  //chroma threshold, extended
        amf_int32           m_iLumaLow;           //luma threshold
        amf_int32           m_iSpillRange;        //spill suppression threshhold
        amf_int32           m_iInputCount;        //input count
        amf_int32           m_iFrameCount;        //processed frame count
        amf_uint32          m_iHistoMax;          //maximum value of histogram
        bool                m_bUpdateKeyColor;    //update keycolor flag. true:need to update
        bool                m_bUpdateKeyColorAuto;//auto update keycolor flag. true: auto
        bool                m_bEof;               //end of file flag
        amf_int32           m_iColorTransferSrc;  //color transfer function, source
        amf_int32           m_iColorTransferBK;   //color transfer function, background
        amf_int32           m_iColorTransferDst;  //color transfer function, target
        bool                m_bAlphaFromSrc;      //use the alpha data from source

        AMFSurfacePtr       m_pSurfaceMask;       //surface for mask 
        AMFSurfacePtr       m_pSurfaceMaskSpill;  //surface for spill surpression
        AMFSurfacePtr       m_pSurfaceMaskBlur;   //surface for mask blur
        AMFSurfacePtr       m_pSurfaceTemp;       //tempoary surface 
        AMFSurfacePtr       m_pSurfaceYUVTemp;    //tempoary surface for RGB input

        amf::AMFComputeKernelPtr  m_pKernelChromaKeyProcess;
        amf::AMFComputeKernelPtr  m_pKernelChromaKeyBlur;
        amf::AMFComputeKernelPtr  m_pKernelChromaKeyErode;
        amf::AMFComputeKernelPtr  m_pKernelChromaKeyDilate;
        amf::AMFComputeKernelPtr  m_pKernelChromaKeyBlend;
        amf::AMFComputeKernelPtr  m_pKernelChromaKeyBlendBK;
        amf::AMFComputeKernelPtr  m_pKernelChromaKeyHisto;
        amf::AMFComputeKernelPtr  m_pKernelChromaKeyHistoSort;
        amf::AMFComputeKernelPtr  m_pKernelChromaKeyHistoLocateLuma;
        amf::AMFComputeKernelPtr  m_pKernelChromaKeyBokeh;
        amf::AMFComputeKernelPtr  m_pKernelChromaKeyHisto422;
        amf::AMFComputeKernelPtr  m_pKernelChromaKeyProcess422;
        amf::AMFComputeKernelPtr  m_pKernelChromaKeyBlend422;
        amf::AMFComputeKernelPtr  m_pKernelChromaKeyBlendBK422;
        amf::AMFComputeKernelPtr  m_pKernelChromaKeyHisto444;
        amf::AMFComputeKernelPtr  m_pKernelChromaKeyProcess444;
        amf::AMFComputeKernelPtr  m_pKernelChromaKeyBlend444;
        amf::AMFComputeKernelPtr  m_pKernelChromaKeyBlendBK444;
        amf::AMFComputeKernelPtr  m_pKernelChromaKeyRGBtoYUV;
        amf::AMFComputeKernelPtr  m_pKernelChromaKeyV210toY210;
        amf::AMFComputeKernelPtr  m_pKernelChromaKeyBlendRGB;
        amf::AMFComputeKernelPtr  m_pKernelChromaKeyBlendBKRGB;
    };
}