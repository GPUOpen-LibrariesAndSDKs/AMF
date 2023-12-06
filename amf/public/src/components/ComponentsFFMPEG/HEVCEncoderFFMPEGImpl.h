///-------------------------------------------------------------------------
///
///  Copyright © 2023 Advanced Micro Devices, Inc. All rights reserved.
///
///-------------------------------------------------------------------------
///  @file   HevcEncoderFFMPEGImpl.h
///  @brief  HEVC Encoder FFMPEG
///-------------------------------------------------------------------------
#pragma once


#include "public/include/components/VideoEncoderHEVC.h"
#include "public/include/components/FFMPEGEncoderHEVC.h"
#include "public/include/components/Component.h"
#include "public/common/PropertyStorageExImpl.h"
#include "public/include/core/Context.h"
#include "public/include/core/Compute.h"

#include "BaseEncoderFFMPEGImpl.h"



namespace amf
{

    ////-------------------------------------------------------------------------------------------------

    class HEVCEncoderFFMPEGImpl : 
        public BaseEncoderFFMPEGImpl
    {
    public:
        HEVCEncoderFFMPEGImpl(AMFContext* pContext);
        virtual ~HEVCEncoderFFMPEGImpl();
        AMF_RESULT  AMF_STD_CALL  Init(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height);
    protected:
        virtual AMF_RESULT  AMF_STD_CALL  InitializeFrame(AMFSurface* pInSurface, AVFrame& avFrame);
        virtual const char *AMF_STD_CALL GetEncoderName(void);
        virtual AMF_RESULT AMF_STD_CALL SetEncoderOptions(void);
    private:
        HEVCEncoderFFMPEGImpl(const HEVCEncoderFFMPEGImpl&);
        HEVCEncoderFFMPEGImpl& operator=(const HEVCEncoderFFMPEGImpl&);
    };

 //   typedef AMFInterfacePtr_T<HEVCEncoderFFMPEGImpl>    AMFHEVCEncoderFFMPEGPtr;
    
}