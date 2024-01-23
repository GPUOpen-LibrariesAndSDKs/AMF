#include "AV1EncoderFFMPEGImpl.h"

#include "public/include/core/Platform.h"
#include "public/include/core/Context.h"
#include "public/include/core/Trace.h"
#include "public/common/TraceAdapter.h"
#include "public/common/AMFFactory.h"
#include "public/common/DataStream.h"
#include "UtilsFFMPEG.h"

#define AMF_FACILITY            L"AV1EncoderFFMPEGImpl"

using namespace amf;



//
//
// AV1EncoderFFMPEGImpl
//
//
const AMFEnumDescriptionEntry g_enumDescr_Av1Usages[] =
{
    { AMF_VIDEO_ENCODER_AV1_USAGE_TRANSCODING,                     L"Transcoding" },
    { AMF_VIDEO_ENCODER_AV1_USAGE_ULTRA_LOW_LATENCY,               L"Ultra Low Latency" },
    { AMF_VIDEO_ENCODER_AV1_USAGE_LOW_LATENCY,                     L"Low Latency" },
    { AMF_VIDEO_ENCODER_AV1_USAGE_WEBCAM,                          L"Webcam" },
    { AMF_VIDEO_ENCODER_AV1_USAGE_HIGH_QUALITY,                    L"High Quality"},
    { AMF_VIDEO_ENCODER_AV1_USAGE_LOW_LATENCY_HIGH_QUALITY,        L"Low Latency High Quality"},
    { 0, 0 }
};
const AMFEnumDescriptionEntry g_enumDescr_Av1Profile[] =
{
    { AMF_VIDEO_ENCODER_AV1_PROFILE_MAIN,    L"Main" },
    { 0, 0 }
};

const AMFEnumDescriptionEntry g_enumDescr_Av1RateControlMethod[] =
{
    { AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CONSTANT_QP,             L"Constrained QP" },
    { AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CBR,                     L"CBR" },
    { AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR,    L"Peak constrained VBR" },
    { AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR, L"Latency constrained VBR" },
    { AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_QUALITY_VBR,             L"Quality VBR" },
    { AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_HIGH_QUALITY_VBR,        L"High quality VBR" },
    { AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_HIGH_QUALITY_CBR,        L"High quality CBR" },
    { 0, 0 }
};

const AMFEnumDescriptionEntry g_enumDescr_Av1QualityPreset[] =
{
    { AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_HIGH_QUALITY,  L"HighQuality" },
    { AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_QUALITY,       L"Quality" },
    { AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_BALANCED,      L"Balanced" },
    { AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_SPEED,         L"Speed" },
    { 0, 0 }
};

const AMFEnumDescriptionEntry g_enumDescr_Av1CdefMode[] =
{
    { AMF_VIDEO_ENCODER_AV1_CDEF_DISABLE,         L"CdefModeDisable" },
    { AMF_VIDEO_ENCODER_AV1_CDEF_ENABLE_DEFAULT,  L"CdefModeEnableDefault" },
    { 0,  0 }
};

const AMFEnumDescriptionEntry g_enumDescr_Av1AqMode[] =
{
    {AMF_VIDEO_ENCODER_AV1_AQ_MODE_NONE,        L"AqModeNone"},
    {AMF_VIDEO_ENCODER_AV1_AQ_MODE_CAQ,         L"AqModeCaq"},
    { 0, 0 }
};

const AMFEnumDescriptionEntry g_enumDescr_Av1ForceFrameType[] =
{
    { AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE_NONE,          L"None" },
    { AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE_KEY,           L"Key" },
    { AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE_INTRA_ONLY,    L"IntraOnly"},
    { AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE_SWITCH,        L"Switch"},
    { AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE_SHOW_EXISTING, L"ShowExisting"},
    { 0, 0 }
};
//-------------------------------------------------------------------------------------------------
//
//
// AV1EncoderFFMPEGImpl
//
//

//-------------------------------------------------------------------------------------------------
AV1EncoderFFMPEGImpl::AV1EncoderFFMPEGImpl(AMFContext* pContext)
  : BaseEncoderFFMPEGImpl(pContext)
{
        AMFPrimitivePropertyInfoMapBegin
        //EncoderCoreH264PropertySet.cpp
        //InitPropertiesCommon
            //AMF_VIDEO_ENCODER_AV1_ENCODER_INSTANCE_INDEX
            //AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE
            //AMF_VIDEO_ENCODER_AV1_QUERY_TIMEOUT
        AMFPropertyInfoInt64(AMF_STREAM_CODEC_ID, AMF_STREAM_CODEC_ID, AMF_STREAM_CODEC_ID_AV1, AMF_STREAM_CODEC_ID_UNKNOWN, INT_MAX, false),
        AMFPropertyInfoEnum(AMF_VIDEO_ENCODER_AV1_USAGE, AMF_VIDEO_ENCODER_AV1_USAGE, AMF_VIDEO_ENCODER_AV1_USAGE_TRANSCODING, g_enumDescr_Av1Usages, AMF_PROPERTY_ACCESS_READ_WRITE),
        AMFPropertyInfoSize(AMF_VIDEO_ENCODER_AV1_FRAMESIZE, AMF_VIDEO_ENCODER_AV1_FRAMESIZE, AMFConstructSize(0, 0), AMFConstructSize(1, 1), AMFConstructSize(0x7fffffff, 0x7fffffff), AMF_PROPERTY_ACCESS_FULL),
            //AMF_VIDEO_ENCODER_AV1_COLOR_BIT_DEPTH
            //AMF_VIDEO_ENCODER_AV1_PROFILE
            //AMF_VIDEO_ENCODER_AV1_LEVEL
       AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_AV1_TILES_PER_FRAME, AMF_VIDEO_ENCODER_AV1_TILES_PER_FRAME, 1, 1, INT_MAX, AMF_PROPERTY_ACCESS_FULL),//tiles
            //AMF_VIDEO_ENCODER_AV1_OUTPUT_MODE
       AMFPropertyInfoEnum(AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET, AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET, AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_BALANCED, g_enumDescr_Av1QualityPreset, AMF_PROPERTY_ACCESS_FULL),
            //AMF_VIDEO_ENCODER_AV1_SCREEN_CONTENT_TOOLS
            //AMF_VIDEO_ENCODER_AV1_ORDER_HINT
            //AMF_VIDEO_ENCODER_AV1_FRAME_ID
            //AMF_VIDEO_ENCODER_AV1_TILE_GROUP_OBU
        AMFPropertyInfoEnum(AMF_VIDEO_ENCODER_AV1_CDEF_MODE, AMF_VIDEO_ENCODER_AV1_CDEF_MODE, AMF_VIDEO_ENCODER_AV1_CDEF_ENABLE_DEFAULT, g_enumDescr_Av1CdefMode, AMF_PROPERTY_ACCESS_FULL),//enable_cdef
        AMFPropertyInfoBool(AMF_VIDEO_ENCODER_AV1_ERROR_RESILIENT_MODE, AMF_VIDEO_ENCODER_AV1_ERROR_RESILIENT_MODE, false, AMF_PROPERTY_ACCESS_FULL),//error_resilient
            //AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD
            //AMF_VIDEO_ENCODER_AV1_QVBR_QUALITY_LEVEL
        AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_AV1_INITIAL_VBV_BUFFER_FULLNESS, AMF_VIDEO_ENCODER_AV1_INITIAL_VBV_BUFFER_FULLNESS, 64, 0, 64,AMF_PROPERTY_ACCESS_FULL),   // rc_buffer_size;
            //AMF_VIDEO_ENCODER_AV1_PRE_ANALYSIS_ENABLE
            //AMF_VIDEO_ENCODER_AV1_HIGH_MOTION_QUALITY_BOOST
        AMFPropertyInfoEnum(AMF_VIDEO_ENCODER_AV1_AQ_MODE, AMF_VIDEO_ENCODER_AV1_AQ_MODE, AMF_VIDEO_ENCODER_AV1_AQ_MODE_NONE, g_enumDescr_Av1AqMode, AMF_PROPERTY_ACCESS_FULL), //aq_mode none variance complexity cyclic
            //AMF_VIDEO_ENCODER_AV1_MAX_NUM_TEMPORAL_LAYERS
            //AMF_VIDEO_ENCODER_AV1_MAX_LTR_FRAMES
        AMFPropertyInfoInterface(AMF_VIDEO_ENCODER_AV1_EXTRA_DATA, AMF_VIDEO_ENCODER_AV1_EXTRA_DATA, (AMFInterface*) nullptr, AMF_PROPERTY_ACCESS_FULL),//uint8_t *extradata;
        AMFPropertyInfoBool(AMF_VIDEO_ENCODER_AV1_PALETTE_MODE, AMF_VIDEO_ENCODER_AV1_PALETTE_MODE, false, AMF_PROPERTY_ACCESS_FULL),//enable_palette
            //AMF_VIDEO_ENCODER_AV1_FORCE_INTEGER_MV
            //AMF_VIDEO_ENCODER_AV1_CDF_UPDATE
            //AMF_VIDEO_ENCODER_AV1_CDF_FRAME_END_UPDATE_MODE
        AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_AV1_GOP_SIZE, AMF_VIDEO_ENCODER_AV1_GOP_SIZE, 30, 0, INT_MAX, AMF_PROPERTY_ACCESS_FULL),//int gop_size
            //AMF_VIDEO_ENCODER_AV1_INTRA_PERIOD
            //AMF_VIDEO_ENCODER_AV1_HEADER_INSERTION_MODE
            //AMF_VIDEO_ENCODER_AV1_SWITCH_FRAME_INSERTION_MODE
            //AMF_VIDEO_ENCODER_AV1_SWITCH_FRAME_INTERVAL
            //AMF_VIDEO_ENCODER_AV1_NUM_TEMPORAL_LAYERS
            //AMF_VIDEO_ENCODER_AV1_INTRA_REFRESH_MODE
            //AMF_VIDEO_ENCODER_AV1_INTRAREFRESH_STRIPES
            //AMF_VIDEO_ENCODER_AV1_MAX_NUM_REFRAMES
            //AMF_VIDEO_ENCODER_AV1_RECONSTRUCTED_PICTURE
            //AMF_VIDEO_ENCODER_AV1_ENABLE_SMART_ACCESS_VIDEO
            //AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE

        //InitEFCParams()
            //AMF_VIDEO_ENCODER_AV1_INPUT_HDR_METADATA
            //AMF_VIDEO_ENCODER_AV1_INPUT_COLOR_PROFILE
            //AMF_VIDEO_ENCODER_AV1_INPUT_TRANSFER_CHARACTERISTIC
            //AMF_VIDEO_ENCODER_AV1_INPUT_COLOR_PRIMARIES
            //AMF_VIDEO_ENCODER_AV1_OUTPUT_COLOR_PROFILE
            //AMF_VIDEO_ENCODER_AV1_OUTPUT_TRANSFER_CHARACTERISTIC
            //AMF_VIDEO_ENCODER_AV1_OUTPUT_COLOR_PRIMARIES

        //InitPerLayerProperties
        AMFPropertyInfoRateEx(AMF_VIDEO_ENCODER_AV1_FRAMERATE, AMF_VIDEO_ENCODER_AV1_FRAMERATE, AMFConstructRate(30, 1), AMFConstructRate(1, 1), AMFConstructRate(INT_MAX, INT_MAX), AMF_PROPERTY_ACCESS_FULL),//framerate
        AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_AV1_TARGET_BITRATE, AMF_VIDEO_ENCODER_AV1_TARGET_BITRATE, 20000000, 1000, INT_MAX, AMF_PROPERTY_ACCESS_FULL),//bit_rate the average bitrate
        AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_AV1_PEAK_BITRATE, AMF_VIDEO_ENCODER_PEAK_BITRATE, 30000000, 1000, INT_MAX, AMF_PROPERTY_ACCESS_FULL),//rc_max_rate
        AMFPropertyInfoInt64(AMF_VIDEO_ENCODER_AV1_VBV_BUFFER_SIZE, AMF_VIDEO_ENCODER_VBV_BUFFER_SIZE, 20 * 1000000, 1 * 1000, 100 * 1000000, AMF_PROPERTY_ACCESS_FULL),   // rc_buffer_size;
            //AMF_VIDEO_ENCODER_AV1_MAX_COMPRESSED_FRAME_SIZE
            //AMF_VIDEO_ENCODER_AV1_MAX_Q_INDEX_INTRA
            //AMF_VIDEO_ENCODER_AV1_MIN_Q_INDEX_INTRA
            //AMF_VIDEO_ENCODER_AV1_MAX_Q_INDEX_INTER
            //AMF_VIDEO_ENCODER_AV1_MIN_Q_INDEX_INTER
            //AMF_VIDEO_ENCODER_AV1_Q_INDEX_INTRA
            //AMF_VIDEO_ENCODER_AV1_Q_INDEX_INTER
            //AMF_VIDEO_ENCODER_AV1_FILLER_DATA
            //AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_SKIP_FRAME
            //AMF_VIDEO_ENCODER_AV1_ENFORCE_HRD
        //InitFrameProperties


    AMFPrimitivePropertyInfoMapEnd
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL AV1EncoderFFMPEGImpl::Init(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height) {

    //Libaom-av1 supported pixel formats : yuv420p yuv422p yuv444p gbrp yuv420p10le yuv422p10le yuv444p10le yuv420p12le yuv422p12le yuv444p12le gbrp10le gbrp12le gray gray10le gray12le
    AMF_RETURN_IF_FAILED(BaseEncoderFFMPEGImpl::Init(format, width, height));

    if (strcasecmp(m_pCodecContext->codec->name, "libaom-av1") != 0)
    {
        AMFTraceError(AMF_FACILITY, L"H264EncoderFFMPEGImpl::Init() - Failed to find FFmpeg libaom-av1 encoder");
        Terminate();
        return AMF_NOT_SUPPORTED;
    }

    //
    // we've created the correct FFmpeg information, now
    // it's time to update the component properties with
    // the data that we obtained
    //
    // input properties
    char  errBuffer[AV_ERROR_MAX_STRING_SIZE] = { 0 };
    amf_int64  preset = 0;
    amf_int    ret = 0;
    AMF_RETURN_IF_FAILED(GetProperty(AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET, &preset));
    //Quality and compression efficiency vs speed trade-off
    if (preset == AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_SPEED)
    {//allintra
        ret = av_opt_set_int(m_pCodecContext->priv_data, "usage", 2, 0);
    }
    if (preset == AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_BALANCED)
    {//realtime
        ret = av_opt_set_int(m_pCodecContext->priv_data, "usage", 1, 0);
    }
    if (preset == AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_QUALITY)
    {//good - really slow
        ret = av_opt_set_int(m_pCodecContext->priv_data, "usage", 0, 0);
    }
    AMF_RETURN_IF_FALSE(ret >= 0, AMF_FAIL, L"Init() - Error setting quality usage for AV1 encoder - %S",
        av_make_error_string(errBuffer, sizeof(errBuffer) / sizeof(errBuffer[0]), ret));

    AMF_RETURN_IF_FAILED(GetProperty(AMF_VIDEO_ENCODER_AV1_FRAMERATE, &m_FrameRate));
    m_pCodecContext->framerate.num = m_FrameRate.num;
    m_pCodecContext->framerate.den = m_FrameRate.den;
    m_pCodecContext->time_base.num = 1;
    m_pCodecContext->time_base.den = AV_TIME_BASE;

    m_pCodecContext->width = m_width;
    m_pCodecContext->height = m_height;
    AMFSize framesize = { m_width, m_height };
    SetProperty(AMF_VIDEO_ENCODER_AV1_FRAMESIZE, framesize);

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

    //InitPerLayerProperties
    AMF_RETURN_IF_FAILED(GetProperty(AMF_VIDEO_ENCODER_AV1_TARGET_BITRATE, &m_pCodecContext->bit_rate));
    m_pCodecContext->bit_rate_tolerance = (int)m_pCodecContext->bit_rate;
    m_pCodecContext->rc_max_rate = m_pCodecContext->bit_rate;

    AMF_RETURN_IF_FAILED(GetProperty(AMF_VIDEO_ENCODER_AV1_GOP_SIZE, &m_pCodecContext->gop_size));
    m_pCodecContext->max_b_frames = 0;

    // setting up AV1 private properties

    //HW encoder only have total number of tiles, ffmpeg needs row and col
    //amf_int64 tiles = 0;
    //AMF_RETURN_IF_FAILED(GetProperty(AMF_VIDEO_ENCODER_AV1_TILES_PER_FRAME, &tiles));
    //ret = av_opt_set_image_size(m_pCodecContext->priv_data, "tiles", w, h, 0);

    amf_int64  cdef_mode = 0;
    AMF_RETURN_IF_FAILED(GetProperty(AMF_VIDEO_ENCODER_AV1_CDEF_MODE, &cdef_mode));
    if (cdef_mode == AMF_VIDEO_ENCODER_AV1_CDEF_DISABLE)
    {
        ret = av_opt_set_int(m_pCodecContext->priv_data, "enable-cdef", -1, 0);
    }
    else if (cdef_mode == AMF_VIDEO_ENCODER_AV1_CDEF_ENABLE_DEFAULT)
    {
        ret = av_opt_set_int(m_pCodecContext->priv_data, "enable-cdef", 1, 0);
    }
    AMF_RETURN_IF_FALSE(ret >= 0, AMF_FAIL, L"Init() - Error setting enable-cdef for AV1 encoder - %S",
        av_make_error_string(errBuffer, sizeof(errBuffer) / sizeof(errBuffer[0]), ret));

    amf_bool error_resilient = false;
    AMF_RETURN_IF_FAILED(GetProperty(AMF_VIDEO_ENCODER_AV1_ERROR_RESILIENT_MODE, &error_resilient));
    ret = av_opt_set_int(m_pCodecContext->priv_data, "error-resilience", error_resilient, 0);
    AMF_RETURN_IF_FALSE(ret >= 0, AMF_FAIL, L"Init() - Error setting quality error-resilience for lAV1 encoder - %S",
        av_make_error_string(errBuffer, sizeof(errBuffer) / sizeof(errBuffer[0]), ret));

    amf_bool enable_palette = false;
    AMF_RETURN_IF_FAILED(GetProperty(AMF_VIDEO_ENCODER_AV1_PALETTE_MODE, &enable_palette));
    ret = av_opt_set_int(m_pCodecContext->priv_data, "enable-palette", enable_palette, 0);
    AMF_RETURN_IF_FALSE(ret >= 0, AMF_FAIL, L"Init() - Error setting enable-palett for AV1 encoder - %S",
        av_make_error_string(errBuffer, sizeof(errBuffer) / sizeof(errBuffer[0]), ret));

    // ready to open codecs and set extradata
    CodecContextInit(AMF_VIDEO_ENCODER_AV1_EXTRA_DATA);

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AV1EncoderFFMPEGImpl::~AV1EncoderFFMPEGImpl()
{
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AV1EncoderFFMPEGImpl::InitializeFrame(AMFSurface* pInSurface, AVFrame& avFrame)
{
    AMF_RETURN_IF_FALSE(m_pCodecContext != nullptr, AMF_NOT_INITIALIZED, L"InitializeFrame() - Codec Context not Initialized");
    AMF_RETURN_IF_FALSE(pInSurface != nullptr, AMF_INVALID_ARG, L"InitializeFrame() - pInSurface == NULL");

    amf_int64 InputFrameType = 0;
    pInSurface->GetProperty(AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE, &InputFrameType);
    switch (InputFrameType)
    {
    case AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE_KEY:
        avFrame.pict_type = AV_PICTURE_TYPE_I;
        break;
    case AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE_SWITCH:
        avFrame.pict_type = AV_PICTURE_TYPE_SI;
        break;
    default:
        avFrame.pict_type = AV_PICTURE_TYPE_NONE;
    }

    BaseEncoderFFMPEGImpl::InitializeFrame(pInSurface, avFrame);


    return AMF_OK;
}

const char *AMF_STD_CALL AV1EncoderFFMPEGImpl::GetEncoderName()
{
    return "libaom-av1";
}

AMF_RESULT AMF_STD_CALL AV1EncoderFFMPEGImpl::SetEncoderOptions(void)
{
    return AMF_OK;
}