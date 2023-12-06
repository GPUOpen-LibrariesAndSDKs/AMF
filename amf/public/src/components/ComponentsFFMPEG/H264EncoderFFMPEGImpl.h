///-------------------------------------------------------------------------
///
///  Copyright © 2023 Advanced Micro Devices, Inc. All rights reserved.
///
///-------------------------------------------------------------------------
///  @file   H264EncoderFFMPEGImpl.h
///  @brief  H264 Encoder FFMPEG
///-------------------------------------------------------------------------
#pragma once


#include "public/include/components/VideoEncoderVCE.h"
#include "public/include/components/FFMPEGEncoderH264.h"
#include "public/include/components/Component.h"
#include "public/common/PropertyStorageExImpl.h"
#include "public/include/core/Context.h"
#include "public/include/core/Compute.h"

#include "BaseEncoderFFMPEGImpl.h"



namespace amf
{

    ////-------------------------------------------------------------------------------------------------

    class H264EncoderFFMPEGImpl : 
        public BaseEncoderFFMPEGImpl
    {
    public:
        H264EncoderFFMPEGImpl(AMFContext* pContext);
        virtual ~H264EncoderFFMPEGImpl();
        AMF_RESULT  AMF_STD_CALL  Init(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height);
    protected:
        virtual AMF_RESULT  AMF_STD_CALL  InitializeFrame(AMFSurface* pInSurface, AVFrame& avFrame);
        virtual const char *AMF_STD_CALL GetEncoderName(void);
        virtual AMF_RESULT AMF_STD_CALL SetEncoderOptions(void);
    private:
        H264EncoderFFMPEGImpl(const H264EncoderFFMPEGImpl&);
        H264EncoderFFMPEGImpl& operator=(const H264EncoderFFMPEGImpl&);
    };

 //   typedef AMFInterfacePtr_T<H264EncoderFFMPEGImpl>    AMFH264EncoderFFMPEGPtr;
    
}
