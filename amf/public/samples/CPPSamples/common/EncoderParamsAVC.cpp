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
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
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

#include "public/include/components/VideoEncoderVCE.h"
#include "EncoderParamsAVC.h"
#include "CmdLogger.h"
#include "ParametersStorage.h"

#include <algorithm>
#include <iterator>
#include <cctype>

static AMF_RESULT ParamConverterUsageAVC(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_ENCODER_USAGE_ENUM paramValue;

    std::wstring uppValue = toUpper(value);
    if(uppValue == L"TRANSCODING" || uppValue == L"0")
    {
        paramValue = AMF_VIDEO_ENCODER_USAGE_TRANSCONDING;
    } 
    else if(uppValue == L"ULTRALOWLATENCY"|| uppValue == L"1") 
    {
        paramValue = AMF_VIDEO_ENCODER_USAGE_ULTRA_LOW_LATENCY;
    } 
    else if(uppValue == L"LOWLATENCY"|| uppValue == L"2") 
    {
        paramValue = AMF_VIDEO_ENCODER_USAGE_LOW_LATENCY;
    } 
    else if(uppValue == L"WEBCAM"|| uppValue == L"3") 
    {
        paramValue = AMF_VIDEO_ENCODER_USAGE_WEBCAM;
    } 
    else {
        LOG_ERROR(L"AMF_VIDEO_ENCODER_USAGE_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}
static AMF_RESULT ParamConverterQualityAVC(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_ENCODER_QUALITY_PRESET_ENUM paramValue;

    std::wstring uppValue = toUpper(value);
    if(uppValue == L"BALANCED" || uppValue== L"0")
    {
        paramValue = AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED;
    } else if(uppValue == L"SPEED" || uppValue== L"1") {
        paramValue = AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED;
    } else if(uppValue == L"QUALITY" || uppValue== L"2") {
        paramValue = AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY;
    } else {
        LOG_ERROR(L"AMF_VIDEO_ENCODER_QUALITY_PRESET_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}
static AMF_RESULT ParamConverterProfileAVC(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_ENCODER_PROFILE_ENUM paramValue;
    std::wstring uppValue = toUpper(value);
    if(uppValue == L"BASELINE" || uppValue == L"66")
    {
        paramValue = AMF_VIDEO_ENCODER_PROFILE_BASELINE;
    } else if(uppValue == L"MAIN"|| uppValue == L"77") {
        paramValue = AMF_VIDEO_ENCODER_PROFILE_MAIN;
    } else if(uppValue == L"HIGH"|| uppValue == L"100") {
        paramValue = AMF_VIDEO_ENCODER_PROFILE_HIGH;
    } else {
        LOG_ERROR(L"AMF_VIDEO_ENCODER_PROFILE_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}
enum ProfileLevel 
{
    PV1     = 10,
    PV11    = 11,
    PV12    = 12,
    PV13    = 13,
    PV2     = 20,
    PV21    = 21,
    PV22    = 22,
    PV3     = 30,
    PV31    = 31,
    PV32    = 32,
    PV4     = 40,
    PV41    = 41,
    PV42    = 42,
    PV50    = 50,
    PV51    = 51,
    PV52    = 52
};

static AMF_RESULT ParamConverterProfileLevelAVC(const std::wstring& value, amf::AMFVariant& valueOut)
{
    ProfileLevel paramValue;
    std::wstring uppValue = toUpper(value);
    if(uppValue == L"1.0" || uppValue == L"10" || uppValue == L"1"){
        paramValue = PV1;
    } else if(uppValue == L"1.1" || uppValue == L"11") {
        paramValue = PV11;
    } else if(uppValue == L"1.2" || uppValue == L"12") {
        paramValue = PV12;
    } else if(uppValue == L"1.3" || uppValue == L"13") {
        paramValue = PV13;
    } else if(uppValue == L"2.0" || uppValue == L"20" || uppValue == L"2") {
        paramValue = PV2;
    } else if(uppValue == L"2.1" || uppValue == L"2.1") {
        paramValue = PV21;
    } else if(uppValue == L"2.2" || uppValue == L"22") {
        paramValue = PV22;
    } else if(uppValue == L"3.0" || uppValue == L"30" || uppValue == L"3") {
        paramValue = PV3;
    } else if(uppValue == L"3.1" || uppValue == L"31") {
        paramValue = PV31;
    } else if(uppValue == L"3.2" || uppValue == L"32") {
        paramValue = PV32;
    } else if(uppValue == L"4.0" || uppValue == L"40" || uppValue == L"4") {
        paramValue = PV4;
    } else if(uppValue == L"4.1" || uppValue == L"41") {
        paramValue = PV41;
    } else if(uppValue == L"4.2" || uppValue == L"42") {
        paramValue = PV42;
    } else if(uppValue == L"5.0" || uppValue == L"50" || uppValue == L"5") {
        paramValue = PV50;
    } else if(uppValue == L"5.1" || uppValue == L"51") {
        paramValue = PV51;
    } else if(uppValue == L"5.2" || uppValue == L"52") {
        paramValue = PV52;
    } else {
        LOG_ERROR(L"ProfileLevel hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}
static AMF_RESULT ParamConverterScanTypeAVC(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_ENCODER_SCANTYPE_ENUM paramValue;
    std::wstring uppValue = toUpper(value);
    if(uppValue == L"PROGRESSIVE" || uppValue == L"0")
    {
        paramValue = AMF_VIDEO_ENCODER_SCANTYPE_PROGRESSIVE;
    } else if(uppValue == L"INTERLACED" || uppValue == L"1") {
        paramValue = AMF_VIDEO_ENCODER_SCANTYPE_INTERLACED;
    } else {
        LOG_ERROR(L"AMF_VIDEO_ENCODER_SCANTYPE_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

static AMF_RESULT ParamConverterRateControlAVC(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_ENUM paramValue;
    std::wstring uppValue = toUpper(value);
    if(uppValue == L"CQP" || uppValue == L"0")
    {
        paramValue = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP;
    } else if(uppValue == L"CBR" || uppValue == L"1") {
        paramValue = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR;
    } else if(uppValue == L"VBR" || uppValue == L"2") {
        paramValue = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR;
    } else if(uppValue == L"VBR_LAT" || uppValue == L"3") {
        paramValue = AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR;
    } else {
        LOG_ERROR(L"AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}


static AMF_RESULT ParamConverterPictureTypeAVC(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_ENCODER_PICTURE_TYPE_ENUM paramValue;
    std::wstring uppValue = toUpper(value);
    if(uppValue == L"NONE" || uppValue == L"0")
    {
        paramValue =  AMF_VIDEO_ENCODER_PICTURE_TYPE_NONE;
    } else if(uppValue == L"SKIP" || uppValue == L"1") {
        paramValue =  AMF_VIDEO_ENCODER_PICTURE_TYPE_SKIP;
    } else if(uppValue == L"IDR" || uppValue == L"2") {
        paramValue =  AMF_VIDEO_ENCODER_PICTURE_TYPE_IDR;
    } else if(uppValue == L"I" || uppValue == L"3") {
        paramValue =  AMF_VIDEO_ENCODER_PICTURE_TYPE_I;
    } else if(uppValue == L"P" || uppValue == L"4") {
        paramValue =  AMF_VIDEO_ENCODER_PICTURE_TYPE_P;
    } else if(uppValue == L"B" || uppValue == L"5") {
        paramValue =  AMF_VIDEO_ENCODER_PICTURE_TYPE_B;
    } else {
        LOG_ERROR(L"AMF_VIDEO_ENCODER_PICTURE_TYPE_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

static AMF_RESULT ParamPreAnalysisAVC(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_ENCODER_PREENCODE_MODE_ENUM paramValue;
    std::wstring uppValue = toUpper(value);
	if (uppValue == L"NONE" || uppValue == L"0" || uppValue == L"DISABLED" || uppValue == L"FALSE")
    {
        paramValue =  AMF_VIDEO_ENCODER_PREENCODE_DISABLED;
    } else if(uppValue == L"ENABLED" || uppValue == L"1" || uppValue == L"TRUE") {
        paramValue =  AMF_VIDEO_ENCODER_PREENCODE_ENABLED;
    } else {
        LOG_ERROR(L"AMF_VIDEO_ENCODER_PREENCODE_MODE_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

static AMF_RESULT ParamEncoding(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_ENCODER_CODING_ENUM paramValue;
    std::wstring uppValue = toUpper(value);
    if(uppValue == L"NONE" || uppValue == L"0" || uppValue == L"UNDEFINED")
    {
        paramValue =  AMF_VIDEO_ENCODER_UNDEFINED;
    } else if(uppValue == L"CABAC" || uppValue == L"1") {
        paramValue =  AMF_VIDEO_ENCODER_CABAC;
    } else if(uppValue == L"CALV" || uppValue == L"2") {
        paramValue =  AMF_VIDEO_ENCODER_CALV;
    } else {
        LOG_ERROR(L"AMF_VIDEO_ENCODER_CODING_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

AMF_RESULT RegisterEncoderParamsAVC(ParametersStorage* pParams)
{
    pParams->SetParamDescription(SETFRAMEPARAMFREQ_PARAM_NAME, ParamCommon, L"Frequency of applying frame parameters (in frames, default = 0 )", ParamConverterInt64);
    pParams->SetParamDescription(SETDYNAMICPARAMFREQ_PARAM_NAME, ParamCommon, L"Frequency of applying dynamic parameters. (in frames, default = 0 )", ParamConverterInt64);


    // ------------- Encoder params usage---------------
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_USAGE, ParamEncoderUsage, L"Encoder usage type. Set many default parameters. (TRANSCONDING, ULTRALOWLATENCY, LOWLATENCY, WEBCAM, default = N/A)", ParamConverterUsageAVC);
    // ------------- Encoder params static---------------
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_PROFILE, ParamEncoderStatic, L"H264 profile (Main, Baseline,High, default = Main", ParamConverterProfileAVC);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_PROFILE_LEVEL, ParamEncoderStatic, L"H264 profile level (float or integer, default = 4.2 (or 42)", ParamConverterProfileLevelAVC);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_QUALITY_PRESET, ParamEncoderStatic, L"Quality Preset (BALANCED, SPEED, QUALITY default = depends on USAGE)", ParamConverterQualityAVC);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_SCANTYPE, ParamEncoderStatic, L"Scan Type (PROGRESSIVE, INTERLACED, default = PROGRESSIVE)", ParamConverterScanTypeAVC);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_MAX_LTR_FRAMES, ParamEncoderStatic, L"Max Of LTR frames (integer, default = 0)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_MAX_NUM_REFRAMES, ParamEncoderStatic, L"Max Of Reference frames (integer, default = 4)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_ENABLE_VBAQ, ParamEncoderStatic, L"Eanble VBAQ (integer, default = 0)", ParamConverterBoolean);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_RATE_CONTROL_PREANALYSIS_ENABLE, ParamEncoderStatic, L"Rate Control Preanalysis Enabled (EANBLED, DISABLED, default = DISABLED)", ParamPreAnalysisAVC);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_ASPECT_RATIO, ParamEncoderStatic, L"Controls aspect ratio, defulat (1,1)", ParamConverterRatio);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_FULL_RANGE_COLOR, ParamEncoderStatic, L"Inidicates that YUV input is (0,255) (bool, default = false)", ParamConverterBoolean);

    // ------------- Encoder params dynamic ---------------
//    pParams->SetParamDescription(AMF_VIDEO_ENCODER_WIDTH, ParamEncoderDynamic, L"Frame width (integer, default = 0)", ParamConverterInt64);
//    pParams->SetParamDescription(AMF_VIDEO_ENCODER_HEIGHT, ParamEncoderDynamic, L"Frame height (integer, default = 0)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_B_PIC_DELTA_QP, ParamEncoderDynamic, L"B-picture Delta  (integer, default = depends on USAGE)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_REF_B_PIC_DELTA_QP, ParamEncoderDynamic, L"Reference B-picture Delta  (integer, default = depends on USAGE)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_FRAMERATE, ParamEncoderDynamic, L"Frame Rate (num,den), default = depends on USAGE)", ParamConverterRate);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_MAX_AU_SIZE, ParamEncoderDynamic, L"Max AU Size (in bits, default = 0)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_TARGET_BITRATE, ParamEncoderDynamic, L"Target bit rate (in bits, default = depends on USAGE)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_PEAK_BITRATE, ParamEncoderDynamic, L"Peak bit rate (in bits, default = depends on USAGE)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_B_PIC_PATTERN, ParamEncoderDynamic, L"B-picture Pattern (number of B-Frames, default = 3)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_IDR_PERIOD, ParamEncoderDynamic, L"IDR Period, (in frames, default = depends on USAGE) ", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_SLICES_PER_FRAME, ParamEncoderDynamic, L"Slices Per Frame (integer, default = 1)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_INTRA_REFRESH_NUM_MBS_PER_SLOT, ParamEncoderDynamic, L"Intra Refresh MBs Number Per Slot (in Macroblocks, default = depends on USAGE)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_INITIAL_VBV_BUFFER_FULLNESS, ParamEncoderDynamic, L"Initial VBV Buffer Fullness (integer, 0=0% 64=100% , default = 64)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_VBV_BUFFER_SIZE, ParamEncoderDynamic, L"VBV Buffer Size (in bits, default = depends on USAGE)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_MIN_QP, ParamEncoderDynamic, L"Min QP (integer 0-51, default = depends on USAGE)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_MAX_QP, ParamEncoderDynamic, L"Max QP (integer 0-51, default = depends on USAGE)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_QP_I, ParamEncoderDynamic, L"QP I (integer 0-51, default = 22)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_QP_P, ParamEncoderDynamic, L"QP P (integer 0-51, default = 22)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_QP_B, ParamEncoderDynamic, L"QP B (integer 0-51, default = 22)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_HEADER_INSERTION_SPACING, ParamEncoderDynamic, L"Insertion spacing", ParamConverterInt64);

    pParams->SetParamDescription(AMF_VIDEO_ENCODER_ENFORCE_HRD, ParamEncoderDynamic, L"Enforce HRD (true, false default = depends on USAGE)", ParamConverterBoolean);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_FILLER_DATA_ENABLE, ParamEncoderDynamic, L"Filler Data Enable (true, false default =  false)", ParamConverterBoolean);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_RATE_CONTROL_SKIP_FRAME_ENABLE, ParamEncoderDynamic, L"Rate Control Based Frame Skip (true, false default =  depends on USAGE)", ParamConverterBoolean);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_DE_BLOCKING_FILTER, ParamEncoderDynamic, L"De-blocking Filter (true, false default =  depends on USAGE)" , ParamConverterBoolean);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_B_REFERENCE_ENABLE, ParamEncoderDynamic, L"Enable B Refrence (true, false default =  true)" , ParamConverterBoolean);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_MOTION_HALF_PIXEL, ParamEncoderDynamic, L"Half Pixel (true, false default =  true)" , ParamConverterBoolean);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_MOTION_QUARTERPIXEL, ParamEncoderDynamic, L"Quarter Pixel (true, false default =  true" , ParamConverterBoolean);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_NUM_TEMPORAL_ENHANCMENT_LAYERS, ParamEncoderDynamic, L"Num Of Temporal Enhancment Layers (SVC) (integer, default = 0, range = 0, min(2, caps->GetMaxNumOfTemporalLayers())", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_CABAC_ENABLE, ParamEncoderDynamic, L"Encoding method (UNDEFINED, CAABC, CALV) default =UNDEFINED", ParamEncoding);

    pParams->SetParamDescription(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, ParamEncoderDynamic, L"Rate Control Method (CQP, CBR, VBR, VBR_LAT default = depends on USAGE)", ParamConverterRateControlAVC);

    // ------------- Encoder params per frame ---------------
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_INSERT_SPS, ParamEncoderFrame, L"Insert SPS (true, false default =  false)", ParamConverterBoolean);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_INSERT_PPS, ParamEncoderFrame, L"Insert PPS (true, false default =  false)", ParamConverterBoolean);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_INSERT_AUD, ParamEncoderFrame, L"Insert AUD (true, false default =  false)", ParamConverterBoolean);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_MARK_CURRENT_WITH_LTR_INDEX, ParamEncoderFrame, L"Mark With LTR Index (integer, default -1)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_FORCE_LTR_REFERENCE_BITFIELD, ParamEncoderFrame, L"Force LTR Reference Bitfield (bitfield default = 0)", ParamConverterInt64);
    pParams->SetParamDescription(AMF_VIDEO_ENCODER_FORCE_PICTURE_TYPE, ParamEncoderFrame, L"Force Picture Type (NONE, SKIP, IDR, I, P, B, default = NONE)", ParamConverterPictureTypeAVC);
    return AMF_OK;
}
