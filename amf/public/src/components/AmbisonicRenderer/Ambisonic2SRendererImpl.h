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
#include "public/include/components/Component.h"
#include "public/include/components/Ambisonic2SRenderer.h"
#include "public/common/PropertyStorageExImpl.h"
#include "public/include/core/Context.h"
#include "public/common/ByteArray.h"

#include "convolution.h"

#include <stdio.h>
#include <memory.h>
#include <math.h>


#define PI 3.1415926535897932384626433

using namespace amf;

#define ICO_HRTF_LEN 512
#define ICO_NVERTICES 20
#define MAX_SPEAKERS 40
//#define EAR_FWD_AMBI_ANGLE 45.0
#define EAR_FWD_AMBI_ANGLE 45

namespace amf
{

    class Ambi2Stereo 
    {
    private:
        float *LeftResponseW, *LeftResponseX, *LeftResponseY, *LeftResponseZ;
        float *RightResponseW, *RightResponseX, *RightResponseY, *RightResponseZ;

        amf_int64    inSampleRate;
        unsigned int responseLength;
        AMF_AMBISONIC2SRENDERER_MODE_ENUM method;
        float *theta, *phi;
        float prevHeadTheta, prevHeadPhi;

        void getResponses(float theta, float phi,
            int channel,
            float *Wresponse, float *Xresponse, float *Yresponse, float *Zresponse);

        unsigned int bufSize;
        float *OutData[8];

        void loadTabulatedHRTFs();

        convolution *m_convolution;

    public:
        Ambi2Stereo(AMF_AMBISONIC2SRENDERER_MODE_ENUM  method, amf_int64 inSampleRate);
        ~Ambi2Stereo();

        void process(float theta, float phi, int nSamples, float *W, float *X, float *Y, float *Z, float *left, float *right);

        float *vSpkrNresponse_L[MAX_SPEAKERS];
        float *vSpkrNresponse_R[MAX_SPEAKERS];

    };

    //-------------------------------------------------------------------------------
    typedef AMFPropertyStorageExImpl <AMFComponent> baseclassCompositorProperty;
    //-------------------------------------------------------------------------------
    //-------------------------------------------------------------------------------------------------

    class AMFAmbisonic2SRendererImpl : 
        public AMFInterfaceBase,
        public AMFPropertyStorageExImpl <AMFComponent>
    {
    const static amf_int32 s_InputChannelCount = 4;
    const static amf_int32 s_OutputChannelCount = 2;

    public:
        // interface access
        AMF_BEGIN_INTERFACE_MAP
            AMF_INTERFACE_MULTI_ENTRY(AMFComponent)
            AMF_INTERFACE_CHAIN_ENTRY(baseclassCompositorProperty)
        AMF_END_INTERFACE_MAP


        AMFAmbisonic2SRendererImpl(AMFContext* pContext);
        virtual ~AMFAmbisonic2SRendererImpl();

        // AMFComponent interface
        virtual AMF_RESULT  AMF_STD_CALL  Init(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height);
        virtual AMF_RESULT  AMF_STD_CALL  ReInit(amf_int32 width, amf_int32 height);
        virtual AMF_RESULT  AMF_STD_CALL  Terminate();
        virtual AMF_RESULT  AMF_STD_CALL  Drain();
        virtual AMF_RESULT  AMF_STD_CALL  Flush();

        virtual AMF_RESULT  AMF_STD_CALL  SubmitInput(AMFData* pData);
        virtual AMF_RESULT  AMF_STD_CALL  QueryOutput(AMFData** ppData);
        virtual AMFContext* AMF_STD_CALL  GetContext()                                              {  return m_pContext;  };
        virtual AMF_RESULT  AMF_STD_CALL  SetOutputDataAllocatorCB(AMFDataAllocatorCB* callback)    {  return AMF_NOT_SUPPORTED;  };

        virtual AMF_RESULT  AMF_STD_CALL GetCaps(AMFCaps** ppCaps);

        virtual AMF_RESULT  AMF_STD_CALL Optimize(AMFComponentOptimizationCallback* pCallback);

        virtual void        AMF_STD_CALL OnPropertyChanged(const wchar_t* pName);

    private:
        mutable AMFCriticalSection          m_sync;
        mutable AMFCriticalSection          m_syncProperties;

        AMFContextPtr                       m_pContext;
        AMFAudioBufferPtr                   m_pInputData;

        // cache property values and update them on 
        // OnPropertyChanged so we don't have to get
        // every single time we need them as it 
        // might be quite expensive
        AMF_AUDIO_FORMAT                    m_inSampleFormat;
        amf_int64                           m_inChannels;

        AMF_AMBISONIC2SRENDERER_MODE_ENUM   m_eMode;

        amf_int64                           m_wIndex;
        amf_int64                           m_xIndex;
        amf_int64                           m_yIndex;
        amf_int64                           m_zIndex;

        amf_double                          m_Theta;
        amf_double                          m_Phi;
        amf_double                          m_Rho;

        AMF_AUDIO_FORMAT                    m_outSampleFormat;
        amf_int64                           m_outChannels;

        bool                                m_bEof;
        bool                                m_bDrained;
        amf_pts                             m_ptsNext;

        amf_int64                           m_audioFrameSubmitCount;
        amf_int64                           m_audioFrameQueryCount;

        Ambi2Stereo                         *m_ambi2S;
        int                                 m_ambiResponseLength;


        AMFByteArray                        m_InternmediateData;
        amf_pts                             m_ptsLastTime;

        AMFAmbisonic2SRendererImpl(const AMFAmbisonic2SRendererImpl&);
        AMFAmbisonic2SRendererImpl& operator=(const AMFAmbisonic2SRendererImpl&);
    };
    
}