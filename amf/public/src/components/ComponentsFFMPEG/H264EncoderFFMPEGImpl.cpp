#include "H264EncoderFFMPEGImpl.h"

#include "public/include/core/Platform.h"
#include "public/include/core/Context.h"
#include "public/include/core/Trace.h"
#include "public/common/TraceAdapter.h"
#include "public/common/AMFFactory.h"
#include "public/common/DataStream.h"
#include "UtilsFFMPEG.h"

#define AMF_FACILITY            L"H264EncoderFFMPEGImpl"

using namespace amf;



//
//
// H264EncoderFFMPEGImpl
//
//

# define MAX_SW_AVC_GOP       250;

const AMFEnumDescriptionEntry g_enumDescr_Usages[] =
{
    { AMF_VIDEO_ENCODER_USAGE_TRANSCONDING,		L"Transcoding" },
    { AMF_VIDEO_ENCODER_USAGE_ULTRA_LOW_LATENCY,	L"Ultra Low Latency" },
    { AMF_VIDEO_ENCODER_USAGE_LOW_LATENCY,			L"Low Latency" },
    { AMF_VIDEO_ENCODER_USAGE_WEBCAM,				L"Webcam" },
    { 0, 0 }
};

const AMFEnumDescriptionEntry g_enumDescr_Profile[] =
{
    { AMF_VIDEO_ENCODER_PROFILE_MAIN, L"Main" },//FF_PROFILE_H264_MAIN
    { AMF_VIDEO_ENCODER_PROFILE_BASELINE, L"Baseline" },//FF_PROFILE_H264_BASELINE
    { AMF_VIDEO_ENCODER_PROFILE_HIGH, L"High" },//FF_PROFILE_H264_HIGH
    { AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_HIGH, L"ConstrainedHigh"},//?
    { AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_BASELINE, L"ConstrainedBase"},//FF_PROFILE_H264_CONSTRAINED_BASELINE
    { AMF_VIDEO_ENCODER_PROFILE_UNKNOWN, 0 }
};

const AMFEnumDescriptionEntry g_enumDescr_ProfileLevel[] =
{
    { 10, L"1" },
    { 11, L"1.1" },
    { 12, L"1.2" },

    { 13, L"1.3" },
    { 20, L"2" },
    { 21, L"2.1" },
    { 22, L"2.2" },
    { 30, L"3" },
    { 31, L"3.1" },
    { 32, L"3.2" },
    { 40, L"4" },
    { 41, L"4.1" },
    { 42, L"4.2" },
    { 50, L"5.0" },

    { 51, L"5.1" },
    { 52, L"5.2" },
    { 0, 0 }
};
const AMFEnumDescriptionEntry g_enumDescr_RateControlMethod[] =
{
    { AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP, L"Constrained QP" },
    { AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR, L"CBR" },
    { AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR, L"Peak constrained VBR" },
    { AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR, L"Latency constrained VBR" },
    { AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_QUALITY_VBR, L"Quality VBR" },
    { 0, 0 }
};
const AMFEnumDescriptionEntry g_enumDescr_QualityPreset[] =
{
    { AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED, L"medium" },
    { AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED, L"fast" },
    { AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY, L"slow" },
    { 0, 0 }
};
//-------------------------------------------------------------------------------------------------
//
//
// H264EncoderFFMPEGImpl
//
//

//-------------------------------------------------------------------------------------------------
H264EncoderFFMPEGImpl::H264EncoderFFMPEGImpl(AMFContext* pContext)
  : BaseEncoderFFMPEGImpl(pContext)
{
        AMFPrimitivePropertyInfoMapBegin
        //EncoderCoreH264PropertySet.cpp
        //InitPropertiesCommon
        AMFPropertyInfoInt64(AMF_STREAM_CODEC_ID, AMF_STREAM_CODEC_ID, AMF_STREAM_CODEC_ID_H264_AVC, AMF_STREAM_CODEC_ID_UNKNOWN, INT_MAX, false),
        AMFPropertyInfoEnum(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_TRANSCONDING, g_enumDescr_Usages, AMF_PROPERTY_ACCESS_READ_WRITE),
        AMFPropertyInfoEnum(AMF_VIDEO_ENCODER_PROFILE, AMF_VIDEO_ENCODER_PROFILE, AMF_VIDEO_ENCODER_PROFILE_UNKNOWN, g_enumDescr_Profile, AMF_PROPERTY_ACCESS_FULL),
        AMFPropertyInfoEnum(AMF_VIDEO_ENCODER_PROFILE_LEVEL, AMF_VIDEO_ENCODER_PROFILE_LEVEL, 42, g_enumDescr_ProfileLevel, AMF_PROPERTY_ACCESS_FULL),
        AMFPropertyInfoSize(AMF_VIDEO_ENCODER_FRAMESIZE, AMF_VIDEO_ENCODER_FRAMESIZE, AMFConstructSize(0, 0), AMFConstructSize(1, 1), AMFConstructSize(0x7fffffff, 0x7fffffff), AMF_PROPERTY_ACCESS_FULL),
        //AMF_VIDEO_ENCODER_DE_BLOCKING_FILTER
        //AMF_VIDEO_ENCODER_LTR_MODE
        //AMF_VIDEO_ENCODER_MAX_LTR_FRAMES
        AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_MAX_NUM_REFRAMES, AMF_VIDEO_ENCODER_MAX_NUM_REFRAMES, 4, 1, 16, AMF_PROPERTY_ACCESS_FULL),///AMF_VIDEO_ENCODER_MAX_NUM_REFRAMES    int refs;
        //AMF_VIDEO_ENCODER_ENABLE_VBAQ
        //AMF_VIDEO_ENCODER_HIGH_MOTION_QUALITY_BOOST_ENABLE
        //AMF_VIDEO_ENCODER_PREENCODE_ENABLE
        //AMF_VIDEO_ENCODER_MOTION_HALF_PIXEL AMF_VIDEO_ENCODER_MOTION_QUARTERPIXEL\\motion_est
        AMFPropertyInfoEnum(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR, g_enumDescr_RateControlMethod, AMF_PROPERTY_ACCESS_FULL), //nal-hrd=vbr cbr cqp
        AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_INITIAL_VBV_BUFFER_FULLNESS, AMF_VIDEO_ENCODER_INITIAL_VBV_BUFFER_FULLNESS, 64, 0, 64, AMF_PROPERTY_ACCESS_FULL),//f_vbv_buffer_init = rc_initial_buffer_occupancy/rc_buffer_size
        AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_SLICES_PER_FRAME, AMF_VIDEO_ENCODER_SLICES_PER_FRAME, 1, 1, INT_MAX, AMF_PROPERTY_ACCESS_FULL),//int slices
        //AMF_VIDEO_ENCODER_CABAC_ENABLE
        //AMF_VIDEO_ENCODER_INSERT_AUD
        //AMF_VIDEO_ENCODER_HEADER_INSERTION_SPACING
        AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_IDR_PERIOD, AMF_VIDEO_ENCODER_IDR_PERIOD, 30, 0, INT_MAX, AMF_PROPERTY_ACCESS_FULL),//int gop_size // encode core H264 doesn't support the GOP size property
        //AMF_VIDEO_ENCODER_NUM_TEMPORAL_ENHANCMENT_LAYERS
        AMFPropertyInfoRatio(AMF_VIDEO_ENCODER_ASPECT_RATIO, AMF_VIDEO_ENCODER_ASPECT_RATIO, 1, 1, AMF_PROPERTY_ACCESS_FULL),
        //AMF_VIDEO_ENCODER_FULL_RANGE_COLOR
        //AMF_VIDEO_ENCODER_SCANTYPE
        AMFPropertyInfoEnum(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED, g_enumDescr_QualityPreset, AMF_PROPERTY_ACCESS_FULL),//priv_data preset
        //ultrafast, superfast, veryfast, faster, fast, medium, slow, slower, veryslow, placebo
        AMFPropertyInfoInterface(AMF_VIDEO_ENCODER_EXTRADATA, AMF_VIDEO_ENCODER_EXTRADATA, (AMFInterface*) nullptr, AMF_PROPERTY_ACCESS_FULL),//uint8_t *extradata;
        //AMF_VIDEO_ENCODER_QUERY_TIMEOUT query output timeout
        //AMF_VIDEO_ENCODER_INTRA_REFRESH_NUM_MBS_PER_SLOT
        AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_B_PIC_PATTERN, AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0, 0, 16, AMF_PROPERTY_ACCESS_FULL),//max_b_frames
        AMFPropertyInfoBool(AMF_VIDEO_ENCODER_B_REFERENCE_ENABLE, AMF_VIDEO_ENCODER_B_REFERENCE_ENABLE, true, AMF_PROPERTY_ACCESS_FULL),
        //AMF_VIDEO_ENCODER_LOWLATENCY_MODE //AMF_VIDEO_ENCODER_MF_LOW_LATENCY
        //AMF_VIDEO_ENCODER_PICTURE_TRANSFER_MODE
        //AMF_VIDEO_ENCODER_RECONSTRUCTED_PICTURE

        //InitEFCParams

        //InitPerLayerProperties
        AMFPropertyInfoRateEx(AMF_VIDEO_ENCODER_FRAMERATE, AMF_VIDEO_ENCODER_FRAMERATE, AMFConstructRate(30, 1), AMFConstructRate(1, 1), AMFConstructRate(INT_MAX, INT_MAX), AMF_PROPERTY_ACCESS_FULL),//framerate
        AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_TARGET_BITRATE, AMF_VIDEO_ENCODER_TARGET_BITRATE, 20000000, 1000, INT_MAX, AMF_PROPERTY_ACCESS_FULL),//bit_rate the average bitrate
        AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_PEAK_BITRATE, AMF_VIDEO_ENCODER_PEAK_BITRATE, 30000000, 1000, INT_MAX, AMF_PROPERTY_ACCESS_FULL),//rc_max_rate
        AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_VBV_BUFFER_SIZE, AMF_VIDEO_ENCODER_VBV_BUFFER_SIZE, 20 * 1000000, 1 * 1000, 100 * 1000000, AMF_PROPERTY_ACCESS_FULL),   // rc_buffer_size;
        //AMF_VIDEO_ENCODER_RATE_CONTROL_SKIP_FRAME_ENABLE  /** @deprecated use encoder private options instead */ but I can't find it in private settings
        //AMF_VIDEO_ENCODER_QP_I priv->cqp
        //AMF_VIDEO_ENCODER_QP_P
        //AMF_VIDEO_ENCODER_QP_B
        //AMF_VIDEO_ENCODER_ENFORCE_HRD //nal-hrd",       "Signal HRD information (requires vbv-bufsize;
        //AMF_VIDEO_ENCODER_FILLER_DATA_ENABLE
        AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_MIN_QP, AMF_VIDEO_ENCODER_MIN_QP, 0, 0, 51, AMF_PROPERTY_ACCESS_FULL),//AMF_VIDEO_ENCODER_MIN_QPqmin
        AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_MAX_QP, AMF_VIDEO_ENCODER_MAX_QP, 0, 0, 51, AMF_PROPERTY_ACCESS_FULL)//AMF_VIDEO_ENCODER_MAX_QP qmax
        //AMF_VIDEO_ENCODER_MAX_AU_SIZE

        //InitFrameProperties
        //AMF_VIDEO_ENCODER_INSERT_SPS
        //AMF_VIDEO_ENCODER_INSERT_PPS
        //AMF_VIDEO_ENCODER_FORCE_PICTURE_TYPE
        //AMF_VIDEO_ENCODER_END_OF_SEQUENCE
        //AMF_VIDEO_ENCODER_END_OF_STREAM
        //AMF_VIDEO_ENCODER_MARK_CURRENT_WITH_LTR_INDEX
        //AMF_VIDEO_ENCODER_FORCE_LTR_REFERENCE_BITFIELD
        //AMF_VIDEO_ENCODER_PRE_ANALYSIS_ENABLE PA not need


    AMFPrimitivePropertyInfoMapEnd
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL H264EncoderFFMPEGImpl::Init(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height) {

    //Libx264 supported pixel formats: yuv420p yuvj420p yuv422p yuvj422p yuv444p yuvj444p nv12 nv16 nv21 yuv420p10le yuv422p10le yuv444p10le nv20le gray gray10le
    AMF_RETURN_IF_FAILED(BaseEncoderFFMPEGImpl::Init(format, width, height));

    //
    // we've created the correct FFmpeg information, now
    // it's time to update the component properties with
    // the data that we obtained
    //
    // input properties
    amf_int64 ret = 0;

    AMF_RETURN_IF_FAILED(GetProperty(AMF_VIDEO_ENCODER_FRAMERATE, &m_FrameRate));
    m_pCodecContext->framerate.num = m_FrameRate.num;
    m_pCodecContext->framerate.den = m_FrameRate.den;
    m_pCodecContext->time_base.num = 1;
    m_pCodecContext->time_base.den = AV_TIME_BASE;

    m_pCodecContext->width = m_width;
    m_pCodecContext->height = m_height;
    AMFSize framesize = { m_width, m_height };
    SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, framesize);

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

    AMFRatio aspect_ratio;
    AMF_RETURN_IF_FAILED(GetProperty(AMF_VIDEO_ENCODER_ASPECT_RATIO, &aspect_ratio));
    m_pCodecContext->sample_aspect_ratio.num = aspect_ratio.num;
    m_pCodecContext->sample_aspect_ratio.den = aspect_ratio.den;

    AMF_RETURN_IF_FAILED(GetProperty(AMF_VIDEO_ENCODER_PROFILE, &m_pCodecContext->profile));
    AMF_RETURN_IF_FAILED(GetProperty(AMF_VIDEO_ENCODER_PROFILE_LEVEL, &m_pCodecContext->level));

    //InitPerLayerProperties
    AMF_RETURN_IF_FAILED(GetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, &m_pCodecContext->bit_rate));

    AMF_RETURN_IF_FAILED(GetProperty(AMF_VIDEO_ENCODER_IDR_PERIOD, &m_pCodecContext->gop_size));
    // 0 to disable automatic periodic IDR in encode core for streaming
    // however, ffmpeg can't achieve that, instead we force gop to be max
    if (m_pCodecContext->gop_size == 0)
    {
        m_pCodecContext->gop_size = MAX_SW_AVC_GOP;
        m_pCodecContext->keyint_min = 25;
    }
    bool isReferenced = false;
    AMF_RETURN_IF_FAILED(GetProperty(AMF_VIDEO_ENCODER_B_REFERENCE_ENABLE, &isReferenced));
    if (isReferenced)
    {
        AMF_RETURN_IF_FAILED(GetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, &m_pCodecContext->max_b_frames));
    }
    else
    {
        m_pCodecContext->max_b_frames = 0;
    }

    // ready to open codecs and set extradata
    CodecContextInit(AMF_VIDEO_ENCODER_EXTRADATA);

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
H264EncoderFFMPEGImpl::~H264EncoderFFMPEGImpl()
{
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  H264EncoderFFMPEGImpl::InitializeFrame(AMFSurface* pInSurface, AVFrame& avFrame)
{
    AMF_RETURN_IF_FALSE(m_pCodecContext != nullptr, AMF_NOT_INITIALIZED, L"InitializeFrame() - Codec Context not Initialized");
    AMF_RETURN_IF_FALSE(pInSurface != nullptr, AMF_INVALID_ARG, L"InitializeFrame() - pInSurface == NULL");

    amf_int64 InputFrameType = 0;
    pInSurface->GetProperty(AMF_VIDEO_ENCODER_FORCE_PICTURE_TYPE, &InputFrameType);
    switch (InputFrameType)
    {
    case AMF_VIDEO_ENCODER_PICTURE_TYPE_IDR:
    case AMF_VIDEO_ENCODER_PICTURE_TYPE_I:
        avFrame.pict_type = AV_PICTURE_TYPE_I;
        break;
    case AMF_VIDEO_ENCODER_PICTURE_TYPE_P:
        avFrame.pict_type = AV_PICTURE_TYPE_P;
        break;
    case AMF_VIDEO_ENCODER_PICTURE_TYPE_B:
        avFrame.pict_type = AV_PICTURE_TYPE_B;
        break;
    default:
        avFrame.pict_type = AV_PICTURE_TYPE_NONE;
    }

    BaseEncoderFFMPEGImpl::InitializeFrame(pInSurface, avFrame);


    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
const char *AMF_STD_CALL H264EncoderFFMPEGImpl::GetEncoderName()
{
    return "libx264";
}

AMF_RESULT AMF_STD_CALL H264EncoderFFMPEGImpl::SetEncoderOptions(void)
{
    char  errBuffer[AV_ERROR_MAX_STRING_SIZE] = { 0 };
    amf_int64  preset = 0;
    amf_int    ret = 0;

    AMF_RETURN_IF_FAILED(GetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, &preset));
    //h264 quiality ultrafast, superfast, veryfast, faster, fast, medium, slow, slower, veryslow, placebo
    if (preset == AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED)
    {
        ret = av_opt_set(m_pCodecContext->priv_data, "preset", "medium", 0);
    }
    if (preset == AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED)
    {
        ret = av_opt_set(m_pCodecContext->priv_data, "preset", "veryfast", 0);
    }
    if (preset == AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY)
    {
        ret = av_opt_set(m_pCodecContext->priv_data, "preset", "slow", 0);
    }
    AMF_RETURN_IF_FALSE(ret >= 0, AMF_FAIL, L"Init() - Error setting quality preset for H264 encoder - %S",
        av_make_error_string(errBuffer, sizeof(errBuffer) / sizeof(errBuffer[0]), ret));

    // disable frame hierarchical mode since HW encoder doesn't support it
    ret = av_opt_set_int(m_pCodecContext->priv_data, "b-pyramid", 0, 0);
    AMF_RETURN_IF_FALSE(ret >= 0, AMF_FAIL, L"Init() - Error disabling frame hierarchical for H264 encoder - %S",
        av_make_error_string(errBuffer, sizeof(errBuffer) / sizeof(errBuffer[0]), ret));
    // force it always use the max number of b frames
    ret = av_opt_set_int(m_pCodecContext->priv_data, "b_strategy", 0, 0);
    AMF_RETURN_IF_FALSE(ret >= 0, AMF_FAIL, L"Init() - Error setting b frame number for H264 encoder - %S",
        av_make_error_string(errBuffer, sizeof(errBuffer) / sizeof(errBuffer[0]), ret));

    // force encode output without buffering
    ret = av_opt_set(m_pCodecContext->priv_data, "tune", "zerolatency", 0);
    AMF_RETURN_IF_FALSE(ret >= 0, AMF_FAIL, L"Init() - Error setting tune for H264 encoder - %S",
        av_make_error_string(errBuffer, sizeof(errBuffer) / sizeof(errBuffer[0]), ret));

    return AMF_OK;
}
