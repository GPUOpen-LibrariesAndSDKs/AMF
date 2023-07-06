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
///-------------------------------------------------------------------------
///  @file   VideoStitchImpl.h
///  @brief  AMFVideoStitchImpl interface
///-------------------------------------------------------------------------
#pragma once

#include "public/include/components/Component.h"
#include "public/common/PropertyStorageExImpl.h"
#include "public/include/components/VideoStitch.h"
#include "public/include/core/Context.h"
#include "StitchEngineBase.h"
#include "HistogramImpl.h"

namespace amf
{
    //-------------------------------------------------------------------------------------------------

    typedef AMFPropertyStorageExImpl <AMFComponentEx> baseclassStitchProperty;

    class AMFVideoStitchImpl : 
        public AMFInterfaceBase,
        public baseclassStitchProperty 
    {
    //-------------------------------------------------------------------------------------------------
    typedef AMFInterfaceImpl < AMFPropertyStorageExImpl <AMFInput> > baseclassInputProperty;

        class AMFInputStitchImpl : 
            public baseclassInputProperty
        {
            friend class AMFVideoStitchImpl;
        public:
            AMF_BEGIN_INTERFACE_MAP
                AMF_INTERFACE_ENTRY(AMFInput)
                AMF_INTERFACE_CHAIN_ENTRY(AMFPropertyStorageExImpl <AMFInput>)
                AMF_INTERFACE_CHAIN_ENTRY(baseclassInputProperty)
            AMF_END_INTERFACE_MAP

            AMFInputStitchImpl(AMFVideoStitchImpl* pHost, amf_int32 index);
            virtual ~AMFInputStitchImpl();
            // AMFInput inteface
            virtual AMF_RESULT  AMF_STD_CALL SubmitInput(AMFData* pData);

            virtual void       AMF_STD_CALL OnPropertyChanged(const wchar_t* pName);

        protected:
            AMFVideoStitchImpl* m_pHost;
            AMFSurfacePtr       m_pSurface;
            AMFSurfacePtr       m_pSurfaceOut;
            amf_int32           m_iIndex;
            float               m_fColorCorrection[3];
        };
        typedef AMFInterfacePtr_T<AMFInputStitchImpl>    AMFInputStitchImplPtr;
    //-------------------------------------------------------------------------------------------------

    public:
        // interface access
        AMF_BEGIN_INTERFACE_MAP
            AMF_INTERFACE_MULTI_ENTRY(AMFComponent)
            AMF_INTERFACE_MULTI_ENTRY(AMFComponentEx)
            AMF_INTERFACE_CHAIN_ENTRY(baseclassStitchProperty)
        AMF_END_INTERFACE_MAP


        AMFVideoStitchImpl(AMFContext* pContext);
        virtual ~AMFVideoStitchImpl();

        // AMFComponent interface
        virtual AMF_RESULT   AMF_STD_CALL Init(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height);
        virtual AMF_RESULT   AMF_STD_CALL ReInit(amf_int32 width,amf_int32 height);
        virtual AMF_RESULT   AMF_STD_CALL Terminate();
        virtual AMF_RESULT   AMF_STD_CALL Drain();
        virtual AMF_RESULT   AMF_STD_CALL Flush();

        virtual AMF_RESULT   AMF_STD_CALL SubmitInput(AMFData* pData);
        virtual AMF_RESULT   AMF_STD_CALL QueryOutput(AMFData** ppData);
        virtual AMFContext*  AMF_STD_CALL GetContext() { return m_pContext; }
        virtual AMF_RESULT   AMF_STD_CALL SetOutputDataAllocatorCB(AMFDataAllocatorCB* callback){m_pDataAllocatorCB = callback; return AMF_OK;}
        virtual AMF_RESULT   AMF_STD_CALL GetCaps(AMFCaps** ppCaps);
        virtual AMF_RESULT   AMF_STD_CALL Optimize(AMFComponentOptimizationCallback* pCallback);

        // AMFComponentEx interface
        virtual amf_int32   AMF_STD_CALL GetInputCount();
        virtual amf_int32   AMF_STD_CALL GetOutputCount(){ return 0; }
        virtual AMF_RESULT  AMF_STD_CALL GetInput(amf_int32 index, AMFInput** ppInput);
        virtual AMF_RESULT  AMF_STD_CALL GetOutput(amf_int32 /* index */, AMFOutput** /* ppOutput */) { return AMF_NOT_SUPPORTED; }

        //  AMFComponentPrivate methods
        virtual void        AMF_STD_CALL SetName(const wchar_t* name) { m_name = name; }
        virtual void        AMF_STD_CALL OnPropertyChanged(const wchar_t* pName);

    private:
        amf_wstring         m_name;
        AMFContextPtr       m_pContext;
        AMF_SURFACE_FORMAT  m_formatIn;
        AMF_SURFACE_FORMAT  m_formatOut;
        amf_int32           m_width;
        amf_int32           m_height;
        AMFSize             m_outputSize;
        bool                m_bColorBalance;
        bool                m_bCombinedSource;

        AMFCriticalSection  m_sync;

        AMF_MEMORY_TYPE     m_outputMemoryType;
        AMF_MEMORY_TYPE     m_deviceMemoryType;
        AMF_MEMORY_TYPE     m_deviceHistogramMemoryType;

        bool                m_eof;
        amf_uint            m_frameInd;
        AMFSurfacePtr                       m_tmpOutputSurface;
        AMFDataAllocatorCBPtr               m_pDataAllocatorCB;
        amf_vector<AMFInputStitchImplPtr>   m_InputStatus;
        AMFSurfacePtr						m_pTempSurface;
        AMFComputePtr                       m_pDevice;

        StitchEngineBasePtr m_pEngine;
        HistogramImplPtr    m_pHistogram;

        AMF_RESULT AMF_STD_CALL Compose(AMFSurface **ppSurfaceOut);
        AMF_RESULT AMF_STD_CALL AllocOutputSurface(amf_pts pts, amf_pts duration, AMF_FRAME_TYPE type, AMFSurface** ppSurface);

        AMFVideoStitchImpl(const AMFVideoStitchImpl&);
        AMFVideoStitchImpl& operator=(const AMFVideoStitchImpl&);
        AMF_RESULT SubmitInput(amf_int32 index, AMFData* pData);
   };
}