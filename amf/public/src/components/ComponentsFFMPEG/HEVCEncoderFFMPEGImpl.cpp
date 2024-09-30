#include "HEVCEncoderFFMPEGImpl.h"

#include "public/include/core/Platform.h"
#include "public/include/core/Context.h"
#include "public/include/core/Trace.h"
#include "public/common/TraceAdapter.h"
#include "public/common/AMFFactory.h"
#include "public/common/DataStream.h"
#include "UtilsFFMPEG.h"


#define AMF_FACILITY            L"HEVCEncoderFFMPEGImpl"

using namespace amf;

//
//
// HEVCEncoderFFMPEGImpl
//
//

# define MAX_SW_HEVC_GOP       250;

const AMFEnumDescriptionEntry g_enumDescr_Usages[] =
{
    { AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCODING,		                L"Transcoding" },
    { AMF_VIDEO_ENCODER_HEVC_USAGE_ULTRA_LOW_LATENCY,	            L"Ultra Low Latency" },
    { AMF_VIDEO_ENCODER_HEVC_USAGE_LOW_LATENCY,			            L"Low Latency" },
    { AMF_VIDEO_ENCODER_HEVC_USAGE_WEBCAM,				            L"Webcam" },
    { AMF_VIDEO_ENCODER_HEVC_USAGE_HIGH_QUALITY,                    L"High Quality"},
    { AMF_VIDEO_ENCODER_HEVC_USAGE_LOW_LATENCY_HIGH_QUALITY,        L"Low Latency High Quality"},
    { 0, 0 }
};

const AMFEnumDescriptionEntry g_enumDescr_Profile[] =
{
    { AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN,    L"Main" },
    { AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN_10, L"Main10" },
    { 0, 0 }
};

const AMFEnumDescriptionEntry g_enumDescr_ProfileLevel[] =
{
    { 30,	L"1" },
    { 60,	L"2" },
    { 63,	L"2.1" },
    { 90,	L"3" },
    { 93,	L"3.1" },
    { 120,	L"4" },
    { 123,	L"4.1" },
    { 150,	L"5" },
    { 153,	L"5.1" },
    { 156,	L"5.2" },
    { 180,	L"6" },
    { 183,	L"6.1" },
    { 186,	L"6.2" },
    { 0, 0 }
};

const AMFEnumDescriptionEntry g_enumDescr_RateControlMethod[] =
{
    { AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CONSTANT_QP, L"Constrained QP" },
    { AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR, L"CBR" },
    { AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR, L"Peak constrained VBR" },
    { AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR, L"Latency constrained VBR" },
    { AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_QUALITY_VBR, L"Quality VBR" },
    { AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_HIGH_QUALITY_VBR, L"High quality VBR" },
    { AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_HIGH_QUALITY_CBR, L"High quality CBR" },
    { 0, 0 }
};

const AMFEnumDescriptionEntry g_enumDescr_QualityPreset[] =
{
    { AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_BALANCED, L"Balanced" },
    { AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED, L"Speed" },
    { AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_QUALITY, L"Quality" },
    { 0, 0 }
};

const AMFEnumDescriptionEntry g_enumDescr_ForcePicType[] =
{
    { AMF_VIDEO_ENCODER_HEVC_PICTURE_TYPE_NONE, L"None" },
    { AMF_VIDEO_ENCODER_HEVC_PICTURE_TYPE_SKIP, L"Skip" },
    { AMF_VIDEO_ENCODER_HEVC_PICTURE_TYPE_IDR, L"IDR"},
    { AMF_VIDEO_ENCODER_HEVC_PICTURE_TYPE_I, L"I"},
    { AMF_VIDEO_ENCODER_HEVC_PICTURE_TYPE_P, L"P"},
    { 0, 0 }
};

//
//
// HEVCEncoderFFMPEGImpl
//
//

//-------------------------------------------------------------------------------------------------
HEVCEncoderFFMPEGImpl::HEVCEncoderFFMPEGImpl(AMFContext* pContext)
  : BaseEncoderFFMPEGImpl(pContext)
{
    AMFPrimitivePropertyInfoMapBegin
        //EncoderCoreHEVCPropertySet.cpp
        // Static properties - can be set before Init()
//InitPropertiesCommon
        AMFPropertyInfoInt64(AMF_STREAM_CODEC_ID, AMF_STREAM_CODEC_ID, AMF_STREAM_CODEC_ID_H265_HEVC, AMF_STREAM_CODEC_ID_UNKNOWN, INT_MAX, false),
        AMFPropertyInfoEnum(AMF_VIDEO_ENCODER_HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCONDING, g_enumDescr_Usages, AMF_PROPERTY_ACCESS_READ_WRITE),//internal usage no implementation needed
        AMFPropertyInfoEnum(AMF_VIDEO_ENCODER_HEVC_PROFILE, AMF_VIDEO_ENCODER_HEVC_PROFILE, AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN, g_enumDescr_Profile, AMF_PROPERTY_ACCESS_FULL),
        AMFPropertyInfoEnum(AMF_VIDEO_ENCODER_HEVC_PROFILE_LEVEL, AMF_VIDEO_ENCODER_HEVC_PROFILE_LEVEL, 186, g_enumDescr_ProfileLevel, AMF_PROPERTY_ACCESS_FULL),
        AMFPropertyInfoSize(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, AMFConstructSize(0, 0), AMFConstructSize(1, 1), AMFConstructSize(0x7fffffff, 0x7fffffff), AMF_PROPERTY_ACCESS_FULL),
        // AMF_VIDEO_ENCODER_HEVC_MAX_LTR_FRAMES                       L"HevcMaxOfLTRFrames"           // amf_int64; default = 0; Max number of LTR frames
        // AMF_VIDEO_ENCODER_HEVC_LTR_MODE                             L"HevcLTRMode"                  // amf_int64(AMF_VIDEO_ENCODER_HEVC_LTR_MODE_ENUM); default = AMF_VIDEO_ENCODER_HEVC_LTR_MODE_RESET_UNUSED; remove/keep unused LTRs (not specified in property AMF_VIDEO_ENCODER_HEVC_FORCE_LTR_REFERENCE_BITFIELD)
        AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_HEVC_MAX_NUM_REFRAMES, AMF_VIDEO_ENCODER_HEVC_MAX_NUM_REFRAMES, 1, 1, 16, AMF_PROPERTY_ACCESS_FULL),//AVCodecContext int refs amf_int64; default = 1; Maximum number of reference frames
        //AMF_VIDEO_ENCODER_HEVC_ENABLE_VBAQ
        //AMF_VIDEO_ENCODER_HEVC_HIGH_MOTION_QUALITY_BOOST_ENABLE
        //AMF_VIDEO_ENCODER_HEVC_PREENCODE_ENABLE
        //AMF_VIDEO_ENCODER_HEVC_MOTION_HALF_PIXEL
        //AMF_VIDEO_ENCODER_HEVC_MOTION_QUARTERPIXEL
// Rate control properties
        AMFPropertyInfoEnum(AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD, AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD, AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR, g_enumDescr_RateControlMethod, AMF_PROPERTY_ACCESS_FULL), //nal-hrd=vbr cbr cqp default = depends on USAGE; Rate Control Method
        //AMF_VIDEO_ENCODER_HEVC_QVBR_QUALITY_LEVEL
        //AMF_VIDEO_ENCODER_HEVC_COLOR_BIT_DEPTH
        AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_HEVC_INITIAL_VBV_BUFFER_FULLNESS, AMF_VIDEO_ENCODER_HEVC_INITIAL_VBV_BUFFER_FULLNESS, 64, 0, 64, AMF_PROPERTY_ACCESS_FULL),//AVCodecContext f_vbv_buffer_init = rc_initial_buffer_occupancy/rc_buffer_size// AMF_VIDEO_ENCODER_HEVC_VBV_BUFFER_SIZE
        AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_HEVC_SLICES_PER_FRAME, AMF_VIDEO_ENCODER_HEVC_SLICES_PER_FRAME, 1, 1, INT_MAX, AMF_PROPERTY_ACCESS_FULL),//AVCodecContext int slices
        //AMF_VIDEO_ENCODER_HEVC_HEADER_INSERTION_MODE
        AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_HEVC_GOP_SIZE, AMF_VIDEO_ENCODER_HEVC_GOP_SIZE, 60, 0, INT_MAX, AMF_PROPERTY_ACCESS_FULL),//AVCodecContext int gop_size // amf_int64; default = 60; GOP Size, in frames
        //AMF_VIDEO_ENCODER_HEVC_NUM_GOPS_PER_IDR
        //AMF_VIDEO_ENCODER_HEVC_NUM_TEMPORAL_LAYERS
        AMFPropertyInfoRatio(AMF_VIDEO_ENCODER_HEVC_ASPECT_RATIO, AMF_VIDEO_ENCODER_HEVC_ASPECT_RATIO, 1, 1, AMF_PROPERTY_ACCESS_FULL),//AVRational sample_aspect_ratio;
        //AMF_VIDEO_ENCODER_HEVC_NOMINAL_RANGE
        AMFPropertyInfoEnum(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_BALANCED, g_enumDescr_QualityPreset, AMF_PROPERTY_ACCESS_FULL),//priv_data preset
        //ultrafast, superfast, veryfast, faster, fast, medium, slow, slower, veryslow, placebo
        AMFPropertyInfoInterface(AMF_VIDEO_ENCODER_HEVC_EXTRADATA, AMF_VIDEO_ENCODER_HEVC_EXTRADATA, (AMFInterface*) nullptr, AMF_PROPERTY_ACCESS_FULL),//uint8_t *extradata;
        //AMF_VIDEO_ENCODER_PERF_CNT_INT performance counter
        //AMF_VIDEO_ENCODER_HEVC_INTRA_REFRESH_NUM_CTBS_PER_SLOT
        //AMF_VIDEO_ENCODER_HEVC_LOWLATENCY_MODE
        //AMF_VIDEO_ENCODER_HEVC_PICTURE_TRANSFER_MODE
        //AMF_VIDEO_ENCODER_HEVC_RECONSTRUCTED_PICTURE

//InitEFCParams()
        //AMF_VIDEO_ENCODER_HEVC_INPUT_HDR_METADATA
        //converter

//InitPerLayerProperties
        AMFPropertyInfoRateEx(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, AMF_VIDEO_ENCODER_HEVC_FRAMERATE, AMFConstructRate(30, 1), AMFConstructRate(1, 1), AMFConstructRate(INT_MAX, INT_MAX), AMF_PROPERTY_ACCESS_FULL),//framerate
        AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, 20000000, 10000, INT_MAX, AMF_PROPERTY_ACCESS_FULL),//bit_rate the average bitrate
        AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_HEVC_PEAK_BITRATE, AMF_VIDEO_ENCODER_HEVC_PEAK_BITRATE, 30000000, 10000, INT_MAX, AMF_PROPERTY_ACCESS_FULL),//rc_max_rate
        AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_HEVC_VBV_BUFFER_SIZE, AMF_VIDEO_ENCODER_HEVC_VBV_BUFFER_SIZE, 20 * 1000000, 1 * 1000, 500 * 1000000, AMF_PROPERTY_ACCESS_FULL),   // rc_buffer_size;
        //AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_SKIP_FRAME_ENABLE
        //AMF_VIDEO_ENCODER_HEVC_QP_I
        //AMF_VIDEO_ENCODER_HEVC_QP_P
        //AMF_VIDEO_ENCODER_HEVC_ENFORCE_HRD
        //AMF_VIDEO_ENCODER_HEVC_FILLER_DATA_ENABLE
        //AMF_VIDEO_ENCODER_HEVC_MIN_QP_I
        //AMF_VIDEO_ENCODER_HEVC_MAX_QP_I
        //AMF_VIDEO_ENCODER_HEVC_MIN_QP_P
        //AMF_VIDEO_ENCODER_HEVC_MAX_QP_P
        //AMF_VIDEO_ENCODER_HEVC_MAX_AU_SIZE
//InitFrameProperties
        //AMF_VIDEO_ENCODER_HEVC_INSERT_HEADER
        AMFPropertyInfoEnum(AMF_VIDEO_ENCODER_HEVC_FORCE_PICTURE_TYPE, AMF_VIDEO_ENCODER_HEVC_FORCE_PICTURE_TYPE, 0, g_enumDescr_ForcePicType, AMF_PROPERTY_ACCESS_FULL),


    AMFPrimitivePropertyInfoMapEnd
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL HEVCEncoderFFMPEGImpl::Init(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height) {

    //Libx265 supported pixel formats: yuv420p yuvj420p yuv422p yuvj422p yuv444p yuvj444p gbrp yuv420p10le yuv422p10le yuv444p10le gbrp10le yuv420p12le yuv422p12le yuv444p12le gbrp12le gray gray10le gray12le
    AMF_RETURN_IF_FAILED(BaseEncoderFFMPEGImpl::Init(format, width, height));

    //
    // we've created the correct FFmpeg information, now
    // it's time to update the component properties with
    // the data that we obtained
    //
    // input properties
    amf_int64 ret = 0;

    AMF_RETURN_IF_FAILED(GetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, &m_FrameRate));
    m_pCodecContext->framerate.num = m_FrameRate.num;
    m_pCodecContext->framerate.den = m_FrameRate.den;
    m_pCodecContext->time_base.num = 1;
    m_pCodecContext->time_base.den = AV_TIME_BASE;

    m_pCodecContext->width = m_width;
    m_pCodecContext->height = m_height;
    AMFSize framesize = { m_width, m_height };
    SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, framesize);

    // check if image size is valid
    ret = av_image_check_size2(m_pCodecContext->width, m_pCodecContext->height, m_pCodecContext->max_pixels, AV_PIX_FMT_NONE, 0, m_pCodecContext);// not sure if we need it in encoder
    if (ret < 0)
    {
        m_pCodecContext->width = m_pCodecContext->height = 0;
    }
    m_pCodecContext->coded_width = m_pCodecContext->width;//unused in encoder
    m_pCodecContext->coded_height = m_pCodecContext->height;//unused in encoder
    m_pCodecContext->width = AV_CEIL_RSHIFT(m_pCodecContext->width, m_pCodecContext->lowres);
    m_pCodecContext->height = AV_CEIL_RSHIFT(m_pCodecContext->height, m_pCodecContext->lowres);

    AMFRatio aspect_ratio = {};
    AMF_RETURN_IF_FAILED(GetProperty(AMF_VIDEO_ENCODER_HEVC_ASPECT_RATIO, &aspect_ratio));
    m_pCodecContext->sample_aspect_ratio.num = aspect_ratio.num;
    m_pCodecContext->sample_aspect_ratio.den = aspect_ratio.den;

    //InitPerLayerProperties
    AMF_RETURN_IF_FAILED(GetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, &m_pCodecContext->bit_rate));
    m_pCodecContext->bit_rate_tolerance = (int)m_pCodecContext->bit_rate;
    m_pCodecContext->rc_max_rate = m_pCodecContext->bit_rate;

    // 0 to disable automatic periodic IDR in encode core for streaming
    // however, ffmpeg can't achieve that, instead we force gop to be max
    AMF_RETURN_IF_FAILED(GetProperty(AMF_VIDEO_ENCODER_HEVC_GOP_SIZE, &m_pCodecContext->gop_size));
    if (m_pCodecContext->gop_size == 0)
    {
        m_pCodecContext->gop_size = MAX_SW_HEVC_GOP;
    }
    m_pCodecContext->max_b_frames = 0; // AMF doesn't support hevc b frame yet

    // ready to open codecs and set extradata
    CodecContextInit(AMF_VIDEO_ENCODER_HEVC_EXTRADATA);

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
HEVCEncoderFFMPEGImpl::~HEVCEncoderFFMPEGImpl()
{
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  HEVCEncoderFFMPEGImpl::InitializeFrame(AMFSurface* pInSurface, AVFrame& avFrame)
{
    AMF_RETURN_IF_FALSE(m_pCodecContext != nullptr, AMF_NOT_INITIALIZED, L"InitializeFrame() - Codec Context not Initialized");
    AMF_RETURN_IF_FALSE(pInSurface != nullptr, AMF_INVALID_ARG, L"InitializeFrame() - pInSurface == NULL");

    amf_int64 InputFrameType = 0;
    pInSurface->GetProperty(AMF_VIDEO_ENCODER_FORCE_PICTURE_TYPE, &InputFrameType);
    switch (InputFrameType)
    {
    case AMF_VIDEO_ENCODER_HEVC_PICTURE_TYPE_IDR:
    case AMF_VIDEO_ENCODER_HEVC_PICTURE_TYPE_I:
        avFrame.pict_type = AV_PICTURE_TYPE_I;
        break;
    case AMF_VIDEO_ENCODER_HEVC_PICTURE_TYPE_P:
        avFrame.pict_type = AV_PICTURE_TYPE_P;
        break;
    default:
        avFrame.pict_type = AV_PICTURE_TYPE_NONE;
    }

    BaseEncoderFFMPEGImpl::InitializeFrame(pInSurface, avFrame);


    return AMF_OK;
}

const char *AMF_STD_CALL HEVCEncoderFFMPEGImpl::GetEncoderName()
{
    return "libx265";
}

AMF_RESULT AMF_STD_CALL HEVCEncoderFFMPEGImpl::SetEncoderOptions(void)
{
    char  errBuffer[AV_ERROR_MAX_STRING_SIZE] = { 0 };
    amf_int64  present = 0;
    amf_int    ret = 0;
    AMF_RETURN_IF_FAILED(GetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, &present));
    //hevc quiality ultrafast, superfast, veryfast, faster, fast, medium, slow, slower, veryslow, placebo
    if (present == AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_BALANCED)
    {
        ret = av_opt_set(m_pCodecContext->priv_data, "preset", "medium", 0);
    }
    if (present == AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED)
    {
        ret = av_opt_set(m_pCodecContext->priv_data, "preset", "fast", 0);
    }
    if (present == AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_QUALITY)
    {
        ret = av_opt_set(m_pCodecContext->priv_data, "preset", "slow", 0);
    }
    AMF_RETURN_IF_FALSE(ret >= 0, AMF_FAIL, L"Init() - Error setting quality preset for H264 encoder - %S",
        av_make_error_string(errBuffer, sizeof(errBuffer) / sizeof(errBuffer[0]), ret));

    ret = av_opt_set(m_pCodecContext->priv_data, "tune", "zerolatency", 0);
    AMF_RETURN_IF_FALSE(ret >= 0, AMF_FAIL, L"Init() - Error setting tune for H264 encoder - %S",
    av_make_error_string(errBuffer, sizeof(errBuffer) / sizeof(errBuffer[0]), ret));

    return AMF_OK;
}
