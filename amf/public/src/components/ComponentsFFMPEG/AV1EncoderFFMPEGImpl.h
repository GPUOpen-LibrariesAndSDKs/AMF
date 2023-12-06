///-------------------------------------------------------------------------
///
///  Copyright © 2023 Advanced Micro Devices, Inc. All rights reserved.
///
///-------------------------------------------------------------------------
///  @file   AV1EncoderFFMPEGImpl.h
///  @brief  AV1 Encoder FFMPEG
///-------------------------------------------------------------------------
#pragma once


#include "public/include/components/VideoEncoderAV1.h"
#include "public/include/components/FFMPEGEncoderAV1.h"
#include "public/include/components/Component.h"
#include "public/common/PropertyStorageExImpl.h"
#include "public/include/core/Context.h"
#include "public/include/core/Compute.h"

#include "BaseEncoderFFMPEGImpl.h"



namespace amf
{

    ////-------------------------------------------------------------------------------------------------

    class AV1EncoderFFMPEGImpl :
        public BaseEncoderFFMPEGImpl
    {
    public:
        AV1EncoderFFMPEGImpl(AMFContext* pContext);
        virtual ~AV1EncoderFFMPEGImpl();
        AMF_RESULT  AMF_STD_CALL  Init(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height);
    protected:
        virtual AMF_RESULT  AMF_STD_CALL  InitializeFrame(AMFSurface* pInSurface, AVFrame& avFrame);
        virtual const char *AMF_STD_CALL GetEncoderName(void);
        virtual AMF_RESULT AMF_STD_CALL SetEncoderOptions(void);
    private:
        AV1EncoderFFMPEGImpl(const AV1EncoderFFMPEGImpl&);
        AV1EncoderFFMPEGImpl& operator=(const AV1EncoderFFMPEGImpl&);
    };

 //   typedef AMFInterfacePtr_T<AV1EncoderFFMPEGImpl>    AMFAV1EncoderFFMPEGPtr;

}