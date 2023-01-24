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

#include "EncoderParamsAV1.h"

#include "public/include/components/VideoEncoderAV1.h"
#include "public/samples/CPPSamples/common/CmdLogger.h"

#include <algorithm>
#include <iterator>
#include <cctype>


static AMF_RESULT ParamConverterUsageAV1(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_ENCODER_AV1_USAGE_ENUM paramValue;

    std::wstring uppValue = toUpper(value);
    if (uppValue == L"TRANSCODING" || uppValue == L"0")
    {
        paramValue = AMF_VIDEO_ENCODER_AV1_USAGE_TRANSCODING;
    }
    else if (uppValue == L"LOWLATENCY" || uppValue == L"1")
    {
        paramValue = AMF_VIDEO_ENCODER_AV1_USAGE_LOW_LATENCY;
    }
    else {
        LOG_ERROR(L"AMF_VIDEO_ENCODER_AV1_USAGE_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

static AMF_RESULT ParamConverterAlignmentModeAV1(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE_ENUM paramValue;

    std::wstring uppValue = toUpper(value);
    if (uppValue == L"64X16ONLY" || uppValue == L"1")
    {
        paramValue = AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE_64X16_ONLY;
    }
    else if (uppValue == L"64X16A1080P" || uppValue == L"2")
    {
        paramValue = AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE_64X16_1080P_CODED_1082;
    }
    else if (uppValue == L"NORESTRICTIONS" || uppValue == L"3")
    {
        paramValue = AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE_NO_RESTRICTIONS;
    }
    else {
        LOG_ERROR(L"AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

static AMF_RESULT ParamConverterLatencyModeAV1(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE_ENUM paramValue;

    std::wstring uppValue = toUpper(value);
    if (uppValue == L"NONE" || uppValue == L"0")
    {
        paramValue = AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE_NONE;
    }
    else if (uppValue == L"PWRSAVING" || uppValue == L"1")
    {
        paramValue = AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE_POWER_SAVING_REAL_TIME;
    }
    else if (uppValue == L"REALTIME" || uppValue == L"2")
    {
        paramValue = AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE_REAL_TIME;
    }
    else if (uppValue == L"LOWLATENCY" || uppValue == L"3")
    {
        paramValue = AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE_LOWEST_LATENCY;
    }
    else {
        LOG_ERROR(L"AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

static AMF_RESULT ParamConverterProfileAV1(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_ENCODER_AV1_PROFILE_ENUM paramValue;
    std::wstring uppValue = toUpper(value);
    if (uppValue == L"MAIN" || uppValue == L"1")
    {   // only main profile is supported for AV1 encoder
        paramValue = AMF_VIDEO_ENCODER_AV1_PROFILE_MAIN;
    }
    else
    {
        LOG_ERROR(L"AMF_VIDEO_ENCODER_AV1_PROFILE_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

static AMF_RESULT ParamConverterProfileLevelAV1(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_ENCODER_AV1_LEVEL_ENUM paramValue;
    std::wstring uppValue = toUpper(value);
    if (uppValue == L"2.0" || uppValue == L"20" || uppValue == L"2" || uppValue == L"0") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_2_0;
    }
    else if (uppValue == L"2.1" || uppValue == L"21" || uppValue == L"1") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_2_1;
    }
    else if (uppValue == L"2.2" || uppValue == L"22" || uppValue == L"2") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_2_2;
    }
    else if (uppValue == L"2.3" || uppValue == L"23" || uppValue == L"3") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_2_3;
    }
    else if (uppValue == L"3.0" || uppValue == L"30" || uppValue == L"3" || uppValue == L"4") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_3_0;
    }
    else if (uppValue == L"3.1" || uppValue == L"31" || uppValue == L"5") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_3_1;
    }
    else if (uppValue == L"3.2" || uppValue == L"31" || uppValue == L"6") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_3_2;
    }
    else if (uppValue == L"3.3" || uppValue == L"31" || uppValue == L"7") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_3_3;
    }
    else if (uppValue == L"4.0" || uppValue == L"40" || uppValue == L"4" || uppValue == L"8") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_4_0;
    }
    else if (uppValue == L"4.1" || uppValue == L"41" || uppValue == L"9") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_4_1;
    }
    else if (uppValue == L"4.2" || uppValue == L"42" || uppValue == L"10") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_4_2;
    }
    else if (uppValue == L"4.3" || uppValue == L"42" || uppValue == L"11") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_4_3;
    }
    else if (uppValue == L"5.0" || uppValue == L"50" || uppValue == L"5" || uppValue == L"12") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_5_0;
    }
    else if (uppValue == L"5.1" || uppValue == L"51" || uppValue == L"13") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_5_1;
    }
    else if (uppValue == L"5.2" || uppValue == L"52" || uppValue == L"14") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_5_2;
    }
    else if (uppValue == L"5.3" || uppValue == L"53" || uppValue == L"15") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_5_3;
    }
    else if (uppValue == L"6.0" || uppValue == L"60" || uppValue == L"6" || uppValue == L"16") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_6_0;
    }
    else if (uppValue == L"6.1" || uppValue == L"61" || uppValue == L"17") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_6_1;
    }
    else if (uppValue == L"6.2" || uppValue == L"62" || uppValue == L"18") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_6_2;
    }
    else if (uppValue == L"6.3" || uppValue == L"63" || uppValue == L"19") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_6_3;
    }
    else if (uppValue == L"7.0" || uppValue == L"70" || uppValue == L"7" || uppValue == L"20") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_7_0;
    }
    else if (uppValue == L"7.1" || uppValue == L"71" || uppValue == L"21") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_7_1;
    }
    else if (uppValue == L"7.2" || uppValue == L"72" || uppValue == L"22") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_7_2;
    }
    else if (uppValue == L"7.3" || uppValue == L"73" || uppValue == L"23") {
        paramValue = AMF_VIDEO_ENCODER_AV1_LEVEL_7_3;
    }
    else {
        LOG_ERROR(L"ProfileLevel hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

static AMF_RESULT ParamConverterCDEFMode(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_ENCODER_AV1_CDEF_MODE_ENUM paramValue;

    std::wstring uppValue = toUpper(value);
    if (uppValue == L"DISABLE" || uppValue == L"0")
    {
        paramValue = AMF_VIDEO_ENCODER_AV1_CDEF_DISABLE;
    }
    else if (uppValue == L"ENABLE" || uppValue == L"1") {
        paramValue = AMF_VIDEO_ENCODER_AV1_CDEF_ENABLE_DEFAULT;
    }
    else {
        LOG_ERROR(L"AMF_VIDEO_ENCODER_AV1_CDEF_MODE_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

static AMF_RESULT ParamConverterQualityAV1(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_ENUM paramValue;

    std::wstring uppValue = toUpper(value);
    if (uppValue == L"HIGHQUALITY" || uppValue == L"0")
    {
        paramValue = AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_HIGH_QUALITY;
    }
    else if (uppValue == L"QUALITY" || uppValue == L"1") {
        paramValue = AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_QUALITY;
    }
    else if (uppValue == L"BALANCED" || uppValue == L"2") {
        paramValue = AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_BALANCED;
    }
    else if (uppValue == L"SPEED" || uppValue == L"3") {
        paramValue = AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_SPEED;
    }
    else {
        LOG_ERROR(L"AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

static AMF_RESULT ParamConverterRateControlAV1(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_ENUM paramValue;
    std::wstring uppValue = toUpper(value);
    if (uppValue == L"CQP" || uppValue == L"0")
    {
        paramValue = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CONSTANT_QP;
    }
    else if (uppValue == L"VBR_LAT" || uppValue == L"1") {
        paramValue = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR;
    }
    else if (uppValue == L"VBR" || uppValue == L"2") {
        paramValue = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR;
    }
    else if (uppValue == L"CBR" || uppValue == L"3") {
        paramValue = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CBR;
    }
    else if(uppValue == L"QVBR" || uppValue == L"4") {
        paramValue = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_QUALITY_VBR;
    }
    else if(uppValue == L"HQVBR" || uppValue == L"5") {
        paramValue = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_HIGH_QUALITY_VBR;
    }
    else if (uppValue == L"HQCBR" || uppValue == L"6") {
        paramValue = AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_HIGH_QUALITY_CBR;
    }
    else {
        LOG_ERROR(L"AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

static AMF_RESULT ParamConverterAQmodeAV1(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_ENCODER_AV1_AQ_MODE_ENUM paramValue;
    std::wstring uppValue = toUpper(value);
    if (uppValue == L"NONE" || uppValue == L"0"){
        paramValue = AMF_VIDEO_ENCODER_AV1_AQ_MODE_NONE;
    }
    else if (uppValue == L"CAQ" || uppValue == L"1") {
        paramValue = AMF_VIDEO_ENCODER_AV1_AQ_MODE_CAQ;
    }
    else
    {
        LOG_ERROR(L"AMF_VIDEO_ENCODER_AV1_AQ_MODE_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

static AMF_RESULT ParamConverterEndUpdateAV1(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_ENCODER_AV1_CDF_FRAME_END_UPDATE_MODE_ENUM paramValue;
    std::wstring uppValue = toUpper(value);
    if (uppValue == L"DISABLE" || uppValue == L"0") {
        paramValue = AMF_VIDEO_ENCODER_AV1_CDF_FRAME_END_UPDATE_MODE_DISABLE;
    }
    else if (uppValue == L"ENABLE" || uppValue == L"1") {
        paramValue = AMF_VIDEO_ENCODER_AV1_CDF_FRAME_END_UPDATE_MODE_ENABLE_DEFAULT;
    }
    else
    {
        LOG_ERROR(L"AMF_VIDEO_ENCODER_AV1_CDF_FRAME_END_UPDATE_MODE_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

static AMF_RESULT ParamConverterInsertionModeAV1(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_ENCODER_AV1_HEADER_INSERTION_MODE_ENUM paramValue;
    std::wstring uppValue = toUpper(value);
    if (uppValue == L"NONE" || uppValue == L"0")
    {
        paramValue = AMF_VIDEO_ENCODER_AV1_HEADER_INSERTION_MODE_NONE;
    }
    else if (uppValue == L"GOP" || uppValue == L"1") {
        paramValue = AMF_VIDEO_ENCODER_AV1_HEADER_INSERTION_MODE_GOP_ALIGNED;
    }
    else if (uppValue == L"IDR" || uppValue == L"2") {
        paramValue = AMF_VIDEO_ENCODER_AV1_HEADER_INSERTION_MODE_KEY_FRAME_ALIGNED;
    }
    else {
        LOG_ERROR(L"AMF_VIDEO_ENCODER_AV1_HEADER_INSERTION_MODE_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

static AMF_RESULT ParamConverterSwitchInsertionModeAV1(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_ENCODER_AV1_SWITCH_FRAME_INSERTION_MODE_ENUM paramValue;
    std::wstring uppValue = toUpper(value);
    if (uppValue == L"NONE" || uppValue == L"0")
    {
        paramValue = AMF_VIDEO_ENCODER_AV1_SWITCH_FRAME_INSERTION_MODE_NONE;
    }
    else if (uppValue == L"GOP" || uppValue == L"1") {
        paramValue = AMF_VIDEO_ENCODER_AV1_SWITCH_FRAME_INSERTION_MODE_FIXED_INTERVAL;
    }
    else {
        LOG_ERROR(L"AMF_VIDEO_ENCODER_AV1_SWITCH_FRAME_INSERTION_MODE_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

static AMF_RESULT ParamConverterFrameTypeAV1(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE_ENUM paramValue;
    std::wstring uppValue = toUpper(value);
    if (uppValue == L"NONE" || uppValue == L"0")
    {
        paramValue = AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE_NONE;
    }
    else if (uppValue == L"KEY" || uppValue == L"1") {
        paramValue = AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE_KEY;
    }
    else if (uppValue == L"INTRA" || uppValue == L"2") {
        paramValue = AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE_INTRA_ONLY;
    }
    else if (uppValue == L"SWITCH" || uppValue == L"3") {
        paramValue = AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE_SWITCH;
    }
    else if (uppValue == L"SHOW" || uppValue == L"4") {
        paramValue = AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE_SHOW_EXISTING;
    }
    else {
        LOG_ERROR(L"AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

static AMF_RESULT ParamConverterIntraRefreshAV1(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_ENCODER_AV1_INTRA_REFRESH_MODE_ENUM paramValue;
    std::wstring uppValue = toUpper(value);
    if (uppValue == L"DISABLE" || uppValue == L"0")
    {
        paramValue = AMF_VIDEO_ENCODER_AV1_INTRA_REFRESH_MODE__DISABLED;
    }
    else if (uppValue == L"GOP" || uppValue == L"1") {
        paramValue = AMF_VIDEO_ENCODER_AV1_INTRA_REFRESH_MODE__GOP_ALIGNED;
    }
    else if (uppValue == L"CONTINUOUS" || uppValue == L"2") {
        paramValue = AMF_VIDEO_ENCODER_AV1_INTRA_REFRESH_MODE__CONTINUOUS;
    }
    else {
        LOG_ERROR(L"AMF_VIDEO_ENCODER_AV1_INTRA_REFRESH_MODE_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

static AMF_RESULT ParamConverterTAQModeAV1(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_PA_TAQ_MODE_ENUM paramValue;

    std::wstring uppValue = toUpper(value);
    if (uppValue == L"NONE" || uppValue == L"0")
    {
        paramValue = AMF_PA_TAQ_MODE_NONE;
    }
    else if (uppValue == L"ONE" || uppValue == L"1") {
        paramValue = AMF_PA_TAQ_MODE_1;
    }
    else if (uppValue == L"TWO" || uppValue == L"2") {
        paramValue = AMF_PA_TAQ_MODE_2;
    }
    else {
        LOG_ERROR(L"AMF_PA_TAQ_MODE_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

AMF_RESULT RegisterEncoderParamsAV1(ParametersStorage* pParams)
{
    pParams->SetParamDescription(SETFRAMEPARAMFREQ_PARAM_NAME, ParamCommon, L"Frequency of applying frame parameters (in frames, default = 0 )", ParamConverterInt64);
    pParams->SetParamDescription(SETDYNAMICPARAMFREQ_PARAM_NAME, ParamCommon, L"Frequency of applying dynamic parameters. (in frames, default = 0 )", ParamConverterInt64);

    // ------------- Encoder params usage---------------
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_USAGE, ParamEncoderUsage, L"Encoder usage type. Set many default parameters. (TRANSCODING, LOWLATENCY, default = N/A)", ParamConverterUsageAV1);

    // ------------- Encoder params static---------------
    // Encoder Engine Settings
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE, ParamEncoderStatic, L"Enables low latency mode ( NONE, PWRSAVING, REALTIME, LOWLATENCY, default =  false)", ParamConverterLatencyModeAV1);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_ENCODER_INSTANCE_INDEX, ParamEncoderStatic, L" Index of VCN instance 0, 1 etc, default = 0", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_QUERY_TIMEOUT, ParamEncoderStatic, L" QueryOutput timeout in ms , default = 0", ParamConverterInt64);
    // Session Configuration
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_COLOR_BIT_DEPTH, ParamEncoderStatic, L"8 bit (integer, default = 8)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_PROFILE, ParamEncoderStatic, L"AV1 profile (Main, default = Main)", ParamConverterProfileAV1);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_TILES_PER_FRAME, ParamEncoderStatic, L"Number of tiles Per Frame. This is treated as suggestion (integer, default = 1)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_LEVEL, ParamEncoderStatic, L"AV1 profile level (float or integer, default = based on HW", ParamConverterProfileLevelAV1);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET, ParamEncoderStatic, L"Quality Preset (BALANCED, SPEED, QUALITY default = depends on USAGE)", ParamConverterQualityAV1);
    // Codec Configuration
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_SCREEN_CONTENT_TOOLS, ParamEncoderStatic, L"Allow enabling screen content tools (true, false default = depends on USAGE)", ParamConverterBoolean);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_ORDER_HINT, ParamEncoderStatic, L"Code order hint (true, false default =  depends on USAGE)", ParamConverterBoolean);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_FRAME_ID, ParamEncoderStatic, L"Code frame id (true, false default =  depends on USAGE)", ParamConverterBoolean);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_TILE_GROUP_OBU, ParamEncoderStatic, L"Code FrameHeaderObu + TileGroupObu and each TileGroupObu contains one tile (true, false default =  depends on USAGE)", ParamConverterBoolean);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_CDEF_MODE, ParamEncoderStatic, L"Cdef mode (ENABLE, DISABLE default = depends on USAGE)", ParamConverterCDEFMode);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_ERROR_RESILIENT_MODE, ParamEncoderStatic, L"Enable error resilient mode (true, false default =  depends on USAGE)", ParamConverterBoolean);
    // Rate Control and Quality Enhancement
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD, ParamEncoderStatic, L"Rate Control Method (CQP, CBR, VBR, VBR_LAT default = depends on USAGE)", ParamConverterRateControlAV1);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_INITIAL_VBV_BUFFER_FULLNESS, ParamEncoderStatic, L"Initial VBV Buffer Fullness (integer, 0=0% 64=100% ,default = depends on USAGE)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_PREENCODE, ParamEncoderStatic, L"Enable pre-encode assist in rate control (true, false default =  false)", ParamConverterBoolean);
    //QVBR Property
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_QVBR_QUALITY_LEVEL, ParamEncoderStatic, L"QVBR Quality Level (integer, default = 23)", ParamConverterInt64);

    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_HIGH_MOTION_QUALITY_BOOST, ParamEncoderStatic, L"High motion quality boost mode enabled(true, false default =  depends on USAGE)", ParamConverterBoolean);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_AQ_MODE, ParamEncoderStatic, L"AQ mode (NONE, CAQ default =  depends on USAGE)", ParamConverterAQmodeAV1);
    // Picture Management Configuration
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_MAX_NUM_TEMPORAL_LAYERS, ParamEncoderStatic, L" Max Of LTR frames (integer, default = depends on USAGE)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_MAX_LTR_FRAMES, ParamEncoderStatic, L" Max Of LTR frames (integer, default = depends on USAGE)", ParamConverterInt64);

    // AAA properties
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_ENABLE_SMART_ACCESS_VIDEO, ParamEncoderStatic, L"Enable encoder smart access video feature (bool, default = false)", ParamConverterBoolean);

    // AV1 Alignment mode
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE, ParamEncoderStatic, L"AV1 Alignment mode. (64X16ONLY=1, 64X16A1080P=2, NORESTRICTIONS=3, default = 1)", ParamConverterAlignmentModeAV1);

    // ------------- Encoder params dynamic ---------------
    // Codec Configuration
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_PALETTE_MODE, ParamEncoderDynamic, L"Enable palette mode (true, false default =  false)", ParamConverterBoolean);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_FORCE_INTEGER_MV, ParamEncoderDynamic, L"Enable force integer MV (true, false default =  false)", ParamConverterBoolean);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_CDF_UPDATE, ParamEncoderDynamic, L"Enable CDF update (true, false default =  false)", ParamConverterBoolean);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_CDF_FRAME_END_UPDATE_MODE, ParamEncoderDynamic, L"Enable CDF update (true, false default =  false)", ParamConverterEndUpdateAV1);
    // Rate Control and Quality Enhancement
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_VBV_BUFFER_SIZE, ParamEncoderDynamic, L"VBV Buffer Size (in bits, default = depends on USAGE)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_FRAMERATE, ParamEncoderDynamic, L"Frame Rate (num,den), default = depends on USAGE)", ParamConverterRate);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_ENFORCE_HRD, ParamEncoderDynamic, L"Enforce HRD (true, false default = depends on USAGE)", ParamConverterBoolean);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_FILLER_DATA, ParamEncoderDynamic, L"Filler Data Enable (true, false default =  false)", ParamConverterBoolean);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_TARGET_BITRATE, ParamEncoderDynamic, L"Target bit rate (in bits, default = depends on USAGE)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_PEAK_BITRATE, ParamEncoderDynamic, L"Peak bit rate (in bits, default = depends on USAGE)", ParamConverterInt64);

    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_MAX_COMPRESSED_FRAME_SIZE, ParamEncoderDynamic, L"Max compressed frame Size in bits (in bits, default = 0)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_MIN_Q_INDEX_INTRA, ParamEncoderDynamic, L"Min QIndex for intra frame (integer 0-255, default = depends on USAGE)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_MAX_Q_INDEX_INTRA, ParamEncoderDynamic, L"Max QIndex for intra frame (integer 0-255, default = depends on USAGE)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_MIN_Q_INDEX_INTER, ParamEncoderDynamic, L"Min QIndex for inter frame (integer 0-255, default = depends on USAGE)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_MAX_Q_INDEX_INTER, ParamEncoderDynamic, L"Max QIndex for inter frame (integer 0-255, default = depends on USAGE)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_Q_INDEX_INTRA, ParamEncoderDynamic, L"intra-frame QIndex (integer 0-255, default = depends on USAGE)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_Q_INDEX_INTER, ParamEncoderDynamic, L"inter-frame QIndex (integer 0-255, default = depends on USAGE)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_SKIP_FRAME, ParamEncoderDynamic, L"Rate Control Based Frame Skip (true, false default =  depends on USAGE)", ParamConverterBoolean);
    // Picture Management Configuration
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_GOP_SIZE, ParamEncoderDynamic, L"GOP Size (in frames, depends on USAGE)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_HEADER_INSERTION_MODE, ParamEncoderDynamic, L"Insertion mode (none, gop, idr default = depends on USAGE)", ParamConverterInsertionModeAV1);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_SWITCH_FRAME_INSERTION_MODE, ParamEncoderDynamic, L"Switch frame insertin mode (none, fixed default = depends on USAGE)", ParamConverterSwitchInsertionModeAV1);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_SWITCH_FRAME_INTERVAL, ParamEncoderDynamic, L"The interval between two inserted switch frames (integer, default = depends on USAGE)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_NUM_TEMPORAL_LAYERS, ParamEncoderDynamic, L"Number of temporal layers (integer, default = depends on USAGE)", ParamConverterInt64);
    //pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_TEMPORAL_LAYER_SELECT, ParamEncoderDynamic, L"Select temporal layer to apply parameter changes and queries (integer, default = 0)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_INTRA_REFRESH_MODE, ParamEncoderDynamic, L"Intra Refresh mode (disable, GOP aligned, continuous, default = disable)", ParamConverterIntraRefreshAV1);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_INTRAREFRESH_STRIPES, ParamEncoderDynamic, L"Frame numbers for an Intra Refresh cycle (integer, default = 0)", ParamConverterInt64);
    // ------------- Encoder params per frame ---------------
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_FORCE_FRAME_TYPE, ParamEncoderFrame, L"Generate particular frame type (key, intra, inter, switch, show default = N/A)", ParamConverterFrameTypeAV1);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_FORCE_INSERT_SEQUENCE_HEADER, ParamEncoderFrame, L"Force insert sequence header (true, false default =  false)", ParamConverterBoolean);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_MARK_CURRENT_WITH_LTR_INDEX, ParamEncoderFrame, L"Mark With LTR Index (integer, default -1)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_FORCE_LTR_REFERENCE_BITFIELD, ParamEncoderFrame, L"Force LTR Reference Bitfield (integer default = 0)", ParamConverterInt64);


    // PA parameters
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_AV1_PRE_ANALYSIS_ENABLE, ParamEncoderStatic, L"Enable PA (true, false default =  false)", ParamConverterBoolean);

    pParams->SetParamDescription(AMF_PA_ENGINE_TYPE, ParamEncoderStatic, L"Engine Type (DX11, OPENCL, HOST default = OPENCL)", ParamConverterMemoryType);

    pParams->SetParamDescription(AMF_PA_SCENE_CHANGE_DETECTION_ENABLE, ParamEncoderDynamic, L"Scene Change Detection Enable (true, false default =  true)", ParamConverterBoolean);
    pParams->SetParamDescription(AMF_PA_SCENE_CHANGE_DETECTION_SENSITIVITY, ParamEncoderDynamic, L"Scene Change Detection Sensitivity (LOW, MEDIUM, HIGH default = MEDIUM)", ParamConverterSceneChange);
    pParams->SetParamDescription(AMF_PA_STATIC_SCENE_DETECTION_ENABLE, ParamEncoderDynamic, L"Static Scene Detection Enable (true, false default =  false)", ParamConverterBoolean);
    pParams->SetParamDescription(AMF_PA_STATIC_SCENE_DETECTION_SENSITIVITY, ParamEncoderDynamic, L"Scene Change Detection Sensitivity (LOW, MEDIUM, HIGH default = HIGH)", ParamConverterStaticScene);
    pParams->SetParamDescription(AMF_PA_FRAME_SAD_ENABLE, ParamEncoderDynamic, L"Enable Frame SAD algorithm (true, false default = true", ParamConverterBoolean);
    pParams->SetParamDescription(AMF_PA_ACTIVITY_TYPE, ParamEncoderDynamic, L"Activity Type (Y, YUV default = Y)", ParamConverterActivityType);
    pParams->SetParamDescription(AMF_PA_LTR_ENABLE, ParamEncoderStatic, L"Auto LTR Enable (true, false default = false)", ParamConverterBoolean);

    pParams->SetParamDescription(AMF_PA_INITIAL_QP_AFTER_SCENE_CHANGE, ParamEncoderDynamic, L"QP After Scene Change (integer 0-51, default = 0)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_PA_MAX_QP_BEFORE_FORCE_SKIP, ParamEncoderDynamic, L"Max QP Before Force Skip (integer 0-51, default = 35)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_PA_CAQ_STRENGTH, ParamEncoderDynamic, L"CAQ Strength (LOW, MEDIUM, HIGH default = MEDIUM)", ParamConverterCAQStrength);
    pParams->SetParamDescription(AMF_PA_PAQ_MODE, ParamEncoderDynamic, L"PAQ Mode (NONE, CAQ, default = NONE)", ParamConverterPAQMode);
    pParams->SetParamDescription(AMF_PA_HIGH_MOTION_QUALITY_BOOST_MODE, ParamEncoderDynamic, L"High Motion Quality Boost Mode (NONE, AUTO, default = NONE)", ParamConverterHighMotionQualityBoostMode);

    pParams->SetParamDescription(AMF_PA_LOOKAHEAD_BUFFER_DEPTH, ParamEncoderDynamic, L"PA Buffer size (integer 0 - MAX_LOOKAHEAD_DEPTH, default = 0)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_PA_TAQ_MODE, ParamEncoderDynamic, L"TAQ Mode (NONE, 1, 2, default = NONE)", ParamConverterTAQModeAV1);


    return AMF_OK;
}