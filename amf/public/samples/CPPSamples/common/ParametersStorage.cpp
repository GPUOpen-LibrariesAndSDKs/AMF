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
#include <algorithm>
#include <iterator>
#include <cctype>

#include "ParametersStorage.h"
#include "CmdLogger.h"
#include "public/include/components/VideoEncoderVCE.h"
#include "public/include/components/VideoEncoderHEVC.h"
#include "public/include/components/VideoConverter.h"


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

static AMF_RESULT ParamConverterPictureType(const std::wstring& value, amf::AMFVariant& valueOut);

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
    if(uppValue == L"OPENGL"  || uppValue == L"5")
    {
        paramValue = amf::AMF_MEMORY_OPENGL;
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
    }
    valueOut = amf_int64(paramValue);
    return AMF_OK;
}

AMF_RESULT ParamConverterFormat(const std::wstring& value, amf::AMFVariant& valueOut)
{
    amf::AMF_SURFACE_FORMAT paramValue = amf::AMF_SURFACE_UNKNOWN;
    std::wstring uppValue = toUpper(value);
    if(uppValue == L"NV12" || uppValue == L"0")
    {
        paramValue =  amf::AMF_SURFACE_NV12;
    } else if(uppValue == L"YV12" || uppValue == L"1") {
        paramValue =  amf::AMF_SURFACE_YV12;
    } else if(uppValue == L"BGRA" || uppValue == L"2") {
        paramValue =  amf::AMF_SURFACE_BGRA;
    } else if(uppValue == L"ARGB" || uppValue == L"3") {
        paramValue =  amf::AMF_SURFACE_ARGB;
    } else if(uppValue == L"RGBA" || uppValue == L"4") {
        paramValue =  amf::AMF_SURFACE_RGBA;
    } else if(uppValue == L"GRAY8" || uppValue == L"5") {
        paramValue =  amf::AMF_SURFACE_GRAY8;
	} else if (uppValue == L"YUV420P" || uppValue == L"420P" || uppValue == L"6") {
        paramValue =  amf::AMF_SURFACE_YUV420P;
    } else if(uppValue == L"U8V8" || uppValue == L"7") {
        paramValue =  amf::AMF_SURFACE_U8V8;
    } else if(uppValue == L"P010" || uppValue == L"10") {
        paramValue =  amf::AMF_SURFACE_P010;
    } else if(uppValue == L"RGBAF16" || uppValue == L"RGBA_F16" || uppValue == L"11") {
        paramValue =  amf::AMF_SURFACE_RGBA_F16;
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
    amf_int64 paraValue = 0;
    amf::AMFVariant tmp(value.c_str());
    valueOut = amf_int64(tmp);
    return AMF_OK;
}
AMF_RESULT ParamConverterDouble(const std::wstring& value, amf::AMFVariant& valueOut)
{
    amf_int64 paraValue = 0;
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
    if (value == AMFVideoEncoderVCE_AVC || value == AMFVideoEncoder_HEVC ||
        uppValue == AMFVideoEncoderVCE_AVC || uppValue == AMFVideoEncoder_HEVC)
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
    else 
    {
        LOG_ERROR(L"Invalid codec name \"" << value << L"\" value.");
        return AMF_INVALID_ARG;
    }
    valueOut = paramValue.c_str();
    return AMF_OK;
}
