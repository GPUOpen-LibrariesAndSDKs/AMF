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
#include <algorithm>
#include <iterator>
#include <cctype>

#include "ParametersStorage.h"
#include "CmdLogger.h"
#include "public/include/components/VideoDecoderUVD.h"
#include "public/include/components/VideoEncoderVCE.h"
#include "public/include/components/VideoEncoderHEVC.h"
#include "public/include/components/VideoEncoderAV1.h"
#include "public/include/components/VideoConverter.h"
#include "public/include/components/HQScaler.h"
#include "public/include/components/FRC.h"


std::wstring AddIndexToPath(const std::wstring& path, amf_int32 index)
{
    std::wstring::size_type pos_dot = path.rfind(L'.');
    if(pos_dot == std::wstring::npos)
    {
        LOG_ERROR(L"Bad file name (no extension): " << path);
    }
    std::wstringstream prntstream;
    prntstream << index;
    std::wstring new_path = path.substr(0, pos_dot) + L"_" + prntstream.str() + path.substr(pos_dot);
    return new_path;
}


static std::wstring SplitSvcParamName(const std::wstring &fullName);

AMF_RESULT PushParamsToPropertyStorage(ParametersStorage* pParams, ParamType ptype, amf::AMFPropertyStorage *storage)
{
    AMF_RESULT err = AMF_OK;
    amf_size count = pParams->GetParamCount();
    for(amf_size i = 0; i < count; i++)
    {
        std::wstring name;
        amf::AMFVariant value;
        if(pParams->GetParamAt(i, name, &value) == AMF_OK)
        {
            ParametersStorage::ParamDescription description;
            pParams->GetParamDescription(name.c_str(), description);
            if(description.m_Type == ptype)
            {
                err = storage->SetProperty(description.m_Name.c_str(), value); // use original name
                LOG_AMF_ERROR(err, L"storage->SetProperty(" << description.m_Name << L") failed " );
            }
        }
    }
    return err;
}

ParametersStorage::ParametersStorage()
{
}

amf_size    ParametersStorage::GetParamCount() const
{
    amf::AMFLock lock(&m_csSect);
    return m_parameters.size();
}
AMF_RESULT  ParametersStorage::GetParamAt(amf_size index, std::wstring& name, amf::AMFVariantStruct* value) const
{
    amf::AMFLock lock(&m_csSect);
    for(ParametersMap::const_iterator it = m_parameters.begin(); it != m_parameters.end(); it++)
    {
        if(index == 0)
        {
            name = it->first;
            amf::AMFVariantCopy(value, &it->second);
            return AMF_OK;
        }
        index--;
    }

    return AMF_NOT_FOUND;
}

AMF_RESULT ParametersStorage::SetParam(const wchar_t* name, amf::AMFVariantStruct value)
{
    amf::AMFLock lock(&m_csSect);
    // check description
    std::wstring nameUpper = toUpper(name);
    ParamDescriptionMap::iterator found = m_descriptionMap.find(SplitSvcParamName(nameUpper));
    if(found == m_descriptionMap.end())
    {
        return AMF_NOT_FOUND;
    }
    m_parameters[nameUpper] = value;
    OnParamChanged(nameUpper.c_str());
    return AMF_OK;
}
AMF_RESULT ParametersStorage::GetParam(const wchar_t* name, amf::AMFVariantStruct* value) const
{
    amf::AMFLock lock(&m_csSect);
    std::wstring nameUpper = toUpper(name);
    ParametersMap::const_iterator found = m_parameters.find(nameUpper);
    if(found == m_parameters.end())
    {
        return AMF_NOT_FOUND;
    }
    amf::AMFVariantCopy(value, &found->second);
    return AMF_OK;
}

AMF_RESULT ParametersStorage::SetParamAsString(const std::wstring& name, const std::wstring& value)
{
    amf::AMFLock lock(&m_csSect);
    std::wstring nameUpper = toUpper(name);
    ParamDescriptionMap::iterator found = m_descriptionMap.find(SplitSvcParamName(nameUpper));
    if(found == m_descriptionMap.end())
    {
        return AMF_NOT_FOUND;
    }
    if(found->second.m_Converter != NULL)
    {
        amf::AMFVariant valueConverted;

        AMF_RESULT res = found->second.m_Converter(value, valueConverted);
        if(res != AMF_OK)
        {
            return res;
        }
        m_parameters[nameUpper] = valueConverted;
    }
    else
    {
        m_parameters[nameUpper] = amf::AMFVariant(value.c_str());
    }
    OnParamChanged(nameUpper.c_str());
    return AMF_OK;
}

AMF_RESULT ParametersStorage::GetParamDescription(const wchar_t* name, ParamDescription& description)
{
    std::wstring nameUpper = toUpper(name);
    ParamDescriptionMap::iterator found = m_descriptionMap.find(SplitSvcParamName(nameUpper));
    if(found == m_descriptionMap.end())
    {
        return AMF_NOT_FOUND;
    }
    description = found->second;
    return AMF_OK;
}

AMF_RESULT ParametersStorage::SetParamDescription(const wchar_t* name, ParamType type, const wchar_t* description, ParamConverter converter)
{
    m_descriptionMap[toUpper(name)] = ParamDescription(name, type, description, converter);
    return AMF_OK;
}

std::wstring  ParametersStorage::GetParamUsage()
{
    std::wstring options;
    options += L"\n----- Common parameters ------\n";
    options+=L"   -help output this help\n";

    options += GetParamUsage(ParamCommon);

    options += L"\n----- Encoder Usage parameter ------\n";
    options += GetParamUsage(ParamEncoderUsage);

    options += L"\n----- Encoder Static parameters ------\n";
    options += GetParamUsage(ParamEncoderStatic);

    options += L"\n----- Encoder Dynamic parameters ------\n";
    options += GetParamUsage(ParamEncoderDynamic);

    options += L"\n----- Encoder Frame parameters ------\n";
    options += GetParamUsage(ParamEncoderFrame);
    options += L"\n----- End of parameter list ------\n";
    return options;
}
std::wstring  ParametersStorage::GetParamUsage(ParamType type)
{
    std::wstring options;
    for(ParamDescriptionMap::iterator it = m_descriptionMap.begin(); it != m_descriptionMap.end(); it++)
    {
        if(it->second.m_Type == type)
        {
            options+=L"   -";
            options+=it->second.m_Name;
            options+=L" ";
            options+=it->second.m_Description;
            options+=L"\n";
        }
    }
    return options;
}

AMF_RESULT  ParametersStorage::CopyTo(ParametersStorage* params)
{
    amf_size   count = GetParamCount();
    for (amf_size i = 0; i < count; i++)
    {
        std::wstring name;
        amf::AMFVariant value;
        GetParamAt(i, name, &value);
        params->SetParam(name.c_str(), value);
    }
    return AMF_OK;
}

AMF_RESULT ParamConverterVideoPresenter(const std::wstring& value, amf::AMFVariant& valueOut)
{
    amf::AMF_MEMORY_TYPE paramValue = amf::AMF_MEMORY_UNKNOWN;
    std::wstring uppValue = toUpper(value);
    if(uppValue == L"DX9" || uppValue == L"2")
    {
        paramValue = amf::AMF_MEMORY_DX9;
    }else
    if(uppValue == L"DX11" || uppValue == L"3")
    {
        paramValue = amf::AMF_MEMORY_DX11;
    }else
    if (uppValue == L"DX12" || uppValue == L"11")
    {
        paramValue = amf::AMF_MEMORY_DX12;
    }
    else
    if(uppValue == L"OPENGL"  || uppValue == L"5")
    {
        paramValue = amf::AMF_MEMORY_OPENGL;
    }else
    if(uppValue == L"VULKAN"  || uppValue == L"10")
    {
        paramValue = amf::AMF_MEMORY_VULKAN;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

AMF_RESULT ParamConverterMemoryType(const std::wstring& value, amf::AMFVariant& valueOut)
{
    amf::AMF_MEMORY_TYPE paramValue = amf::AMF_MEMORY_UNKNOWN;
    std::wstring uppValue = toUpper(value);
    if(uppValue == L"DX9"  || uppValue == L"2")
    {
        paramValue = amf::AMF_MEMORY_DX9;
    }else
    if(uppValue == L"DX11"  || uppValue == L"3")
    {
        paramValue = amf::AMF_MEMORY_DX11;
    }else
    if(uppValue == L"OPENGL"  || uppValue == L"5")
    {
        paramValue = amf::AMF_MEMORY_OPENGL;
    }else
    if(uppValue == L"OPENCL"  || uppValue == L"4")
    {
        paramValue = amf::AMF_MEMORY_OPENCL;
    }else
    if(uppValue == L"HOST"  || uppValue == L"1")
    {
        paramValue = amf::AMF_MEMORY_HOST;
    }else
    if (uppValue == L"VULKAN" || uppValue == L"10")
    {
        paramValue = amf::AMF_MEMORY_VULKAN;
    }else
    if (uppValue == L"DX12" || uppValue == L"11")
    {
        paramValue = amf::AMF_MEMORY_DX12;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

AMF_RESULT ParamConverterFormat(const std::wstring& value, amf::AMFVariant& valueOut)
{
    amf::AMF_SURFACE_FORMAT paramValue = amf::AMF_SURFACE_UNKNOWN;
    std::wstring uppValue = toUpper(value);
    if(uppValue == L"NV12" || uppValue == L"1")
    {
        paramValue =  amf::AMF_SURFACE_NV12;
    } else if(uppValue == L"YV12" || uppValue == L"2") {
        paramValue =  amf::AMF_SURFACE_YV12;
    } else if(uppValue == L"BGRA" || uppValue == L"3") {
        paramValue =  amf::AMF_SURFACE_BGRA;
    } else if(uppValue == L"ARGB" || uppValue == L"4") {
        paramValue =  amf::AMF_SURFACE_ARGB;
    } else if(uppValue == L"RGBA" || uppValue == L"5") {
        paramValue =  amf::AMF_SURFACE_RGBA;
    } else if(uppValue == L"GRAY8" || uppValue == L"6") {
        paramValue =  amf::AMF_SURFACE_GRAY8;
	} else if (uppValue == L"YUV420P" || uppValue == L"420P" || uppValue == L"7") {
        paramValue =  amf::AMF_SURFACE_YUV420P;
    } else if(uppValue == L"U8V8" || uppValue == L"8") {
        paramValue =  amf::AMF_SURFACE_U8V8;
    } else if(uppValue == L"YUY2" || uppValue == L"9") {
        paramValue =  amf::AMF_SURFACE_YUY2;
    } else if(uppValue == L"P010" || uppValue == L"10") {
        paramValue =  amf::AMF_SURFACE_P010;
    } else if (uppValue == L"P012" || uppValue == L"12") {
        paramValue = amf::AMF_SURFACE_P012;
    }else if (uppValue == L"P016" || uppValue == L"16") {
        paramValue = amf::AMF_SURFACE_P016;
    } else if(uppValue == L"RGBAF16" || uppValue == L"RGBA_F16" || uppValue == L"11") {
        paramValue =  amf::AMF_SURFACE_RGBA_F16;
    } else if(uppValue == L"UYVY" || uppValue == L"12") {
        paramValue =  amf::AMF_SURFACE_UYVY;
    }else if (uppValue == L"R10G10B10A2" || uppValue == L"13") {
        paramValue = amf::AMF_SURFACE_R10G10B10A2;
    } else {

            LOG_ERROR(L"AMF_SURFACE_FORMAT hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}
AMF_RESULT ParamConverterColorProfile(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_VIDEO_CONVERTER_COLOR_PROFILE_ENUM paramValue = AMF_VIDEO_CONVERTER_COLOR_PROFILE_UNKNOWN;
    std::wstring uppValue = toUpper(value);
    if(uppValue == L"601" || uppValue == L"0"){
        paramValue =  AMF_VIDEO_CONVERTER_COLOR_PROFILE_601;
    } else if(uppValue == L"709" || uppValue == L"1") {
        paramValue =  AMF_VIDEO_CONVERTER_COLOR_PROFILE_709;
    } else if(uppValue == L"2020" || uppValue == L"2") {
        paramValue =  AMF_VIDEO_CONVERTER_COLOR_PROFILE_2020;
    } else if(uppValue == L"JPEG" || uppValue == L"3") {
        paramValue =  AMF_VIDEO_CONVERTER_COLOR_PROFILE_JPEG;
    } else {
        LOG_ERROR(L"AMF_VIDEO_CONVERTER_COLOR_PROFILE_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

AMF_RESULT ParamConverterTransferCharacteristic(const std::wstring& value, amf::AMFVariant& valueOut)
{
	AMF_COLOR_TRANSFER_CHARACTERISTIC_ENUM paramValue = AMF_COLOR_TRANSFER_CHARACTERISTIC_UNDEFINED;
	std::wstring uppValue = toUpper(value);
	if (uppValue == L"BT709" || uppValue == L"1") {
		paramValue = AMF_COLOR_TRANSFER_CHARACTERISTIC_BT709;
	} else if (uppValue == L"UNSPECIFIED" || uppValue == L"2") {
		paramValue = AMF_COLOR_TRANSFER_CHARACTERISTIC_UNSPECIFIED;
	} else if (uppValue == L"RESERVED" || uppValue == L"3") {
		paramValue = AMF_COLOR_TRANSFER_CHARACTERISTIC_RESERVED;
	} else if (uppValue == L"GAMMA22" || uppValue == L"4") {
		paramValue = AMF_COLOR_TRANSFER_CHARACTERISTIC_GAMMA22;
	} else if (uppValue == L"GAMMA28" || uppValue == L"5") {
		paramValue = AMF_COLOR_TRANSFER_CHARACTERISTIC_GAMMA28;
	} else if (uppValue == L"SMPTE170M" || uppValue == L"6") {
		paramValue = AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE170M;
	} else if (uppValue == L"SMPTE240M" || uppValue == L"7") {
		paramValue = AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE240M;
	} else if (uppValue == L"LINEAR" || uppValue == L"8") {
		paramValue = AMF_COLOR_TRANSFER_CHARACTERISTIC_LINEAR;
	} else if (uppValue == L"LOG" || uppValue == L"9") {
		paramValue = AMF_COLOR_TRANSFER_CHARACTERISTIC_LOG;
	} else if (uppValue == L"LOG_SQRT" || uppValue == L"10") {
		paramValue = AMF_COLOR_TRANSFER_CHARACTERISTIC_LOG_SQRT;
	} else if (uppValue == L"IEC61966_2_4" ||  uppValue == L"11") {
		paramValue = AMF_COLOR_TRANSFER_CHARACTERISTIC_IEC61966_2_4;
	} else if (uppValue == L"BT1361_ECG" || uppValue == L"12") {
		paramValue = AMF_COLOR_TRANSFER_CHARACTERISTIC_BT1361_ECG;
	} else if (uppValue == L"IEC61966_2_1" || uppValue == L"13") {
		paramValue = AMF_COLOR_TRANSFER_CHARACTERISTIC_IEC61966_2_1;
	} else if (uppValue == L"BT2020_10" || uppValue == L"14") {
		paramValue = AMF_COLOR_TRANSFER_CHARACTERISTIC_BT2020_10;
	} else if (uppValue == L"BT2020_12" || uppValue == L"15") {
		paramValue = AMF_COLOR_TRANSFER_CHARACTERISTIC_BT2020_12;
	} else if (uppValue == L"SMPTE2084" || uppValue == L"16") {
		paramValue = AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE2084;
	} else if (uppValue == L"SMPTE428" || uppValue == L"17") {
		paramValue = AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE428;
	} else if (uppValue == L"ARIB_STD_B67" || uppValue == L"18") {
		paramValue = AMF_COLOR_TRANSFER_CHARACTERISTIC_ARIB_STD_B67;
	} else {
		LOG_ERROR(L"AMF_COLOR_TRANSFER_CHARACTERISTIC_ENUM hasn't \"" << value << L"\" value.");
		return AMF_INVALID_ARG;
	}
	valueOut = amf_int64(paramValue);
	return AMF_OK;
}

AMF_RESULT ParamConverterColorPrimaries(const std::wstring& value, amf::AMFVariant& valueOut)
{
	AMF_COLOR_PRIMARIES_ENUM paramValue = AMF_COLOR_PRIMARIES_UNDEFINED;
	std::wstring uppValue = toUpper(value);
	if (uppValue == L"BT709" || uppValue == L"1") {
		paramValue = AMF_COLOR_PRIMARIES_BT709;
	}
	else if (uppValue == L"UNSPECIFIED" || uppValue == L"2") {
		paramValue = AMF_COLOR_PRIMARIES_UNSPECIFIED;
	}
	else if (uppValue == L"RESERVED" || uppValue == L"3") {
		paramValue = AMF_COLOR_PRIMARIES_RESERVED;
	}
	else if (uppValue == L"BT470M" || uppValue == L"4") {
		paramValue = AMF_COLOR_PRIMARIES_BT470M;
	}
	else if (uppValue == L"BT470BG" || uppValue == L"5") {
		paramValue = AMF_COLOR_PRIMARIES_BT470BG;
	}
	else if (uppValue == L"SMPTE170M" || uppValue == L"6") {
		paramValue = AMF_COLOR_PRIMARIES_SMPTE170M;
	}
	else if (uppValue == L"SMPTE240M" || uppValue == L"7") {
		paramValue = AMF_COLOR_PRIMARIES_SMPTE240M;
	}
	else if (uppValue == L"FILM" || uppValue == L"8") {
		paramValue = AMF_COLOR_PRIMARIES_FILM;
	}
	else if (uppValue == L"BT2020" || uppValue == L"9") {
		paramValue = AMF_COLOR_PRIMARIES_BT2020;
	}
	else if (uppValue == L"SMPTE428" || uppValue == L"10") {
		paramValue = AMF_COLOR_PRIMARIES_SMPTE428;
	}
	else if (uppValue == L"SMPTE431" || uppValue == L"11") {
		paramValue = AMF_COLOR_PRIMARIES_SMPTE431;
	}
	else if (uppValue == L"SMPTE432" || uppValue == L"12") {
		paramValue = AMF_COLOR_PRIMARIES_SMPTE432;
	}
	else if (uppValue == L"JEDEC_P22" || uppValue == L"22") {
		paramValue = AMF_COLOR_PRIMARIES_JEDEC_P22;
	}
	else if (uppValue == L"CCCS" || uppValue == L"1000") {
		paramValue = AMF_COLOR_PRIMARIES_CCCS;
	}
	else {
		LOG_ERROR(L"AMF_COLOR_PRIMARIES_ENUM hasn't \"" << value << L"\" value.");
		return AMF_INVALID_ARG;
	}
	valueOut = amf_int64(paramValue);
	return AMF_OK;
}

AMF_RESULT ParamConverterColorRange(const std::wstring& value, amf::AMFVariant& valueOut)
{
	AMF_COLOR_RANGE_ENUM paramValue = AMF_COLOR_RANGE_UNDEFINED;
	std::wstring uppValue = toUpper(value);
	if (uppValue == L"STUDIO" || uppValue == L"1") {
		paramValue = AMF_COLOR_RANGE_STUDIO;
	}
	else if (uppValue == L"FULL" || uppValue == L"2") {
		paramValue = AMF_COLOR_RANGE_FULL;
	}
	else {
		LOG_ERROR(L"AMF_COLOR_RANGE_ENUM hasn't \"" << value << L"\" value.");
		return AMF_INVALID_ARG;
	}
	valueOut = amf_int64(paramValue);
	return AMF_OK;
}

AMF_RESULT ParamConverterBoolean(const std::wstring& value, amf::AMFVariant& valueOut)
{
    bool paramValue = true; // if parameter is present default is true
    if(value.length() > 0)
    {
        std::wstring uppValue = toUpper(value);
        if(uppValue == L"TRUE" || uppValue == L"1")
        {
            paramValue =  true;
        } else if(uppValue == L"FALSE" || uppValue == L"0") {
            paramValue =  false;
        } else {
            LOG_ERROR(L"BOOLEAN hasn't \"" << value << L"\" value.");
            return AMF_INVALID_ARG;
        }
    }
    valueOut = paramValue;
    return AMF_OK;
}
AMF_RESULT ParamConverterRatio(const std::wstring& value, amf::AMFVariant& valueOut)
{
    amf::AMFVariant valueIn(value.c_str());

    AMFVariantChangeType(&valueOut, &valueIn, amf::AMF_VARIANT_RATIO);

    return AMF_OK;
}
AMF_RESULT ParamConverterRate(const std::wstring& value, amf::AMFVariant& valueOut)
{
    amf::AMFVariant valueIn(value.c_str());

    AMFVariantChangeType(&valueOut, &valueIn, amf::AMF_VARIANT_RATE);
    if (valueOut.rateValue.den == 0)
    {
        valueOut.rateValue.den = 1;
    }

    return AMF_OK;
}
AMF_RESULT ParamConverterSize(const std::wstring& value, amf::AMFVariant& valueOut)
{
    amf::AMFVariant valueIn(value.c_str());
    AMFVariantChangeType(&valueOut, &valueIn, amf::AMF_VARIANT_SIZE);
    return AMF_OK;
}

AMF_RESULT ParamConverterInt64(const std::wstring& value, amf::AMFVariant& valueOut)
{
    amf::AMFVariant tmp(value.c_str());
    valueOut = amf_int64(tmp);
    return AMF_OK;
}
AMF_RESULT ParamConverterDouble(const std::wstring& value, amf::AMFVariant& valueOut)
{
    amf::AMFVariant tmp(value.c_str());
    valueOut = amf_double(tmp);
    return AMF_OK;
}


// In: "TL2.QL2.PropName"
// return :  svcParamName == "PropName"
std::wstring SplitSvcParamName(const std::wstring &fullName)
{
    std::wstring::size_type posQL = fullName.find(L'.');
    if(posQL != std::wstring::npos)
    {
        std::wstring::size_type posTL = 0;
        if(fullName[posTL] == L'T' && fullName[posTL+1] == L'L')
        { // TL found
            posQL++;
            std::wstring::size_type posName = fullName.find(L'.', posQL);
            if(posName != std::wstring::npos)
            {
                if(fullName[posQL] == L'Q' && fullName[posQL+1] == L'L')
                { // QL fund
                    posName++;
                    std::wstring paramName = fullName.substr(posName+1);
                    return paramName;
                }
            }
        }
    }
    return fullName;
}

AMF_RESULT ParamConverterCodec(const std::wstring& value, amf::AMFVariant& valueOut)
{
    std::wstring paramValue;

    std::wstring uppValue = toUpper(value);
    if (value == AMFVideoEncoderVCE_AVC || value == AMFVideoEncoder_HEVC || value == AMFVideoEncoder_AV1 ||
        uppValue == AMFVideoEncoderVCE_AVC || uppValue == AMFVideoEncoder_HEVC || uppValue == AMFVideoEncoder_AV1)
    {
        paramValue = value;
    }
    else if (uppValue == L"AVC" || uppValue == L"H264" || uppValue == L"H.264")
    {
        paramValue = AMFVideoEncoderVCE_AVC;
    }
    else if(uppValue == L"HEVC" || uppValue == L"H265" || uppValue == L"H.265")
    {
        paramValue = AMFVideoEncoder_HEVC;
    }
    else if (uppValue == L"AV1")
    {
        paramValue = AMFVideoEncoder_AV1;
    }
    else
    {
        LOG_ERROR(L"Invalid codec name \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = paramValue.c_str();
    return AMF_OK;
}

const wchar_t *StreamCodecIDtoDecoderID(AMF_STREAM_CODEC_ID_ENUM eCodec)
{
    switch(eCodec)
    {
    case AMF_STREAM_CODEC_ID_UNKNOWN: return L"";
    case AMF_STREAM_CODEC_ID_MPEG2: return AMFVideoDecoderUVD_MPEG2;
    case AMF_STREAM_CODEC_ID_MPEG4: return AMFVideoDecoderUVD_MPEG4;
    case AMF_STREAM_CODEC_ID_WMV3: return AMFVideoDecoderUVD_WMV3;
    case AMF_STREAM_CODEC_ID_VC1: return AMFVideoDecoderUVD_VC1;
    case AMF_STREAM_CODEC_ID_H264_AVC: return AMFVideoDecoderUVD_H264_AVC;
    case AMF_STREAM_CODEC_ID_H264_MVC: return AMFVideoDecoderUVD_H264_MVC;
    case AMF_STREAM_CODEC_ID_H264_SVC: return AMFVideoDecoderUVD_H264_SVC;
    case AMF_STREAM_CODEC_ID_MJPEG: return AMFVideoDecoderUVD_MJPEG;
    case AMF_STREAM_CODEC_ID_H265_HEVC: return AMFVideoDecoderHW_H265_HEVC;
    case AMF_STREAM_CODEC_ID_H265_MAIN10: return AMFVideoDecoderHW_H265_MAIN10;
    case AMF_STREAM_CODEC_ID_VP9: return AMFVideoDecoderHW_VP9;
    case AMF_STREAM_CODEC_ID_VP9_10BIT: return AMFVideoDecoderHW_VP9_10BIT;
    case AMF_STREAM_CODEC_ID_AV1: return AMFVideoDecoderHW_AV1;
    case AMF_STREAM_CODEC_ID_AV1_12BIT: return AMFVideoDecoderHW_AV1_12BIT;
    }
    return L"";
}

AMF_RESULT ParamConverterHQScalerAlgorithm(const std::wstring& value, amf::AMFVariant& valueOut)
{
    amf_int64    paramValue = -1;
    std::wstring uppValue   = toUpper(value);
    if ((uppValue == L"BILINEAR") || (uppValue == L"0"))
    {
        paramValue = AMF_HQ_SCALER_ALGORITHM_BILINEAR;
    }
    else if ((uppValue == L"BICUBIC") || (uppValue == L"1"))
    {
        paramValue = AMF_HQ_SCALER_ALGORITHM_BICUBIC;
    }
    else if ((uppValue == L"VIDEOSR1.0") || (uppValue == L"2"))
    {
        paramValue = AMF_HQ_SCALER_ALGORITHM_VIDEOSR1_0;
    }
    else if ((uppValue == L"POINT") || (uppValue == L"3"))
    {
        paramValue = AMF_HQ_SCALER_ALGORITHM_POINT;
    }
    else if ((uppValue == L"VIDEOSR1.1") || (uppValue == L"4"))
    {
        paramValue = AMF_HQ_SCALER_ALGORITHM_VIDEOSR1_1;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

AMF_RESULT ParamConverterHighMotionQualityBoostMode(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_PA_HIGH_MOTION_QUALITY_BOOST_MODE_ENUM paramValue;

    std::wstring uppValue = toUpper(value);
    if (uppValue == L"NONE" || uppValue == L"0")
    {
        paramValue = AMF_PA_HIGH_MOTION_QUALITY_BOOST_MODE_NONE;
    }
    else if (uppValue == L"AUTO" || uppValue == L"1")
    {
        paramValue = AMF_PA_HIGH_MOTION_QUALITY_BOOST_MODE_AUTO;
    }
    else
    {
        LOG_ERROR(L"AMF_PA_HIGH_MOTION_QUALITY_BOOST_MODE_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

AMF_RESULT ParamConverterSceneChange(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_PA_SCENE_CHANGE_DETECTION_SENSITIVITY_ENUM paramValue;

    std::wstring uppValue = toUpper(value);
    if (uppValue == L"LOW" || uppValue == L"0")
    {
        paramValue = AMF_PA_SCENE_CHANGE_DETECTION_SENSITIVITY_LOW;
    }
    else if (uppValue == L"MEDIUM" || uppValue == L"1") {
        paramValue = AMF_PA_SCENE_CHANGE_DETECTION_SENSITIVITY_MEDIUM;
    }
    else if (uppValue == L"HIGH" || uppValue == L"2") {
        paramValue = AMF_PA_SCENE_CHANGE_DETECTION_SENSITIVITY_HIGH;
    }
    else {
        LOG_ERROR(L"AMF_PA_SCENE_CHANGE_DETECTION_SENSITIVITY_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

AMF_RESULT ParamConverterStaticScene(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_PA_STATIC_SCENE_DETECTION_SENSITIVITY_ENUM paramValue;

    std::wstring uppValue = toUpper(value);
    if (uppValue == L"LOW" || uppValue == L"0")
    {
        paramValue = AMF_PA_STATIC_SCENE_DETECTION_SENSITIVITY_LOW;
    }
    else if (uppValue == L"MEDIUM" || uppValue == L"1") {
        paramValue = AMF_PA_STATIC_SCENE_DETECTION_SENSITIVITY_MEDIUM;
    }
    else if (uppValue == L"HIGH" || uppValue == L"2") {
        paramValue = AMF_PA_STATIC_SCENE_DETECTION_SENSITIVITY_HIGH;
    }
    else {
        LOG_ERROR(L"AMF_PA_STATIC_SCENE_DETECTION_SENSITIVITY_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

AMF_RESULT ParamConverterActivityType(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_PA_ACTIVITY_TYPE_ENUM paramValue;

    std::wstring uppValue = toUpper(value);
    if (uppValue == L"Y")
    {
        paramValue = AMF_PA_ACTIVITY_Y;
    }
    else if (uppValue == L"YUV") {
        paramValue = AMF_PA_ACTIVITY_YUV;
    }
    else {
        LOG_ERROR(L"AMF_PA_ACTIVITY_TYPE_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

AMF_RESULT ParamConverterCAQStrength(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_PA_CAQ_STRENGTH_ENUM paramValue;

    std::wstring uppValue = toUpper(value);
    if (uppValue == L"LOW" || uppValue == L"0")
    {
        paramValue = AMF_PA_CAQ_STRENGTH_LOW;
    }
    else if (uppValue == L"MEDIUM" || uppValue == L"1") {
        paramValue = AMF_PA_CAQ_STRENGTH_MEDIUM;
    }
    else if (uppValue == L"HIGH" || uppValue == L"2") {
        paramValue = AMF_PA_CAQ_STRENGTH_HIGH;
    }
    else {
        LOG_ERROR(L"AMF_PA_CAQ_STRENGTH_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

AMF_RESULT ParamConverterPAQMode(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_PA_PAQ_MODE_ENUM paramValue;

    std::wstring uppValue = toUpper(value);
    if (uppValue == L"NONE" || uppValue == L"0")
    {
        paramValue = AMF_PA_PAQ_MODE_NONE;
    }
    else if (uppValue == L"CAQ" || uppValue == L"1") {
        paramValue = AMF_PA_PAQ_MODE_CAQ;
    }
    else {
        LOG_ERROR(L"AMF_PA_PAQ_MODE_ENUM hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

AMF_RESULT ParamConverterFRCEngine(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_FRC_ENGINE    paramValue;
    std::wstring uppValue = toUpper(value);
    if ((uppValue == L"OFF") || (uppValue == L"0"))
    {
        paramValue = FRC_ENGINE_OFF;
    }
    else if ((uppValue == L"DX12") || (uppValue == L"1"))
    {
        paramValue = FRC_ENGINE_DX12;
    }
    else if ((uppValue == L"OPENCL") || (uppValue == L"2"))
    {
        paramValue = FRC_ENGINE_OPENCL;
    }
    else if ((uppValue == L"DX11") || (uppValue == L"3"))
    {
        paramValue = FRC_ENGINE_DX11;
    }
    else {
        LOG_ERROR(L"AMF_FRC_ENGINE hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

AMF_RESULT ParamConverterFRCMode(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_FRC_MODE_TYPE    paramValue;
    std::wstring uppValue = toUpper(value);
    if ((uppValue == L"OFF") || (uppValue == L"0"))
    {
        paramValue = FRC_OFF;
    }
    else if ((uppValue == L"ON") || (uppValue == L"1"))
    {
        paramValue = FRC_ON;
    }
    else if ((uppValue == L"INTERPOLATED") || (uppValue == L"2"))
    {
        paramValue = FRC_ONLY_INTERPOLATED;
    }
    else if ((uppValue == L"PRESENT") || (uppValue == L"3"))
    {
        paramValue = FRC_x2_PRESENT;
    }
    else {
        LOG_ERROR(L"AMF_FRC_MODE_TYPE hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }

    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

AMF_RESULT ParamConverterFRCPerformance(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_FRC_MV_SEARCH_MODE_TYPE    paramValue;
    std::wstring uppValue = toUpper(value);
    if ((uppValue == L"NATIVE") || (uppValue == L"0"))
    {
        paramValue = FRC_MV_SEARCH_NATIVE;
    }
    else if ((uppValue == L"PERFORMANCE") || (uppValue == L"1"))
    {
        paramValue = FRC_MV_SEARCH_PERFORMANCE;
    }
    else {
        LOG_ERROR(L"AMF_FRC_MV_SEARCH_MODE_TYPE hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }

    valueOut = amf_int64(paramValue);
    return AMF_OK;
}
AMF_RESULT ParamConverterFRCProfile(const std::wstring& value, amf::AMFVariant& valueOut)
{
    AMF_FRC_PROFILE_TYPE    paramValue;
    std::wstring uppValue = toUpper(value);
    if ((uppValue == L"LOW") || (uppValue == L"0"))
    {
        paramValue = FRC_PROFILE_LOW;
    }
    else if ((uppValue == L"HIGH") || (uppValue == L"1"))
    {
        paramValue = FRC_PROFILE_HIGH;
    }
    else if ((uppValue == L"SUPER") || (uppValue == L"2"))
    {
        paramValue = FRC_PROFILE_SUPER;
    }
    else {
        LOG_ERROR(L"AMF_FRC_PROFILE_TYPE hasn't \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }

    valueOut = amf_int64(paramValue);
    return AMF_OK;
}





AMF_RESULT ParamConverterFRCSnapshotMode(const std::wstring& value, amf::AMFVariant& valueOut)
{
    amf_int64    paramValue = -1;
    std::wstring uppValue = toUpper(value);
    if ((uppValue == L"OFF") || (uppValue == L"0"))
    {
        paramValue = FRC_SNAPSHOT_OFF;
    }
    else if ((uppValue == L"LOAD") || (uppValue == L"1"))
    {
        paramValue = FRC_SNAPSHOT_LOAD;
    }
    else if ((uppValue == L"STORE") || (uppValue == L"2"))
    {
        paramValue = FRC_SNAPSHOT_STORE;
    }
    else if ((uppValue == L"TEST") || (uppValue == L"3"))
    {
        paramValue = FRC_SNAPSHOT_REGRESSION_TEST;
    }
    else if ((uppValue == L"STORE_NO_PADDING") || (uppValue == L"4"))
    {
        paramValue = FRC_SNAPSHOT_STORE_NO_PADDING;
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}