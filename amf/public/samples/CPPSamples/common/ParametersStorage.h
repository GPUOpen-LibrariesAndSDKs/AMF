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
#include <map>
#include <string>
#include <locale>
#include <algorithm>
#include <cctype>
#include <iterator>
#include "public/include/core/PropertyStorage.h"
#include "public/common/Thread.h"
#include "CmdLogger.h"


enum ParamType
{
    ParamUnknown = -1,
    ParamCommon = 0,
    ParamEncoderUsage,       // sets to encoder first
    ParamEncoderStatic,     // sets to encoder before initialization
    ParamEncoderDynamic,    // sets to encoder at any time 
    ParamEncoderFrame,       // sets to frame before frame submission
    ParamVideoProcessor
};

AMF_RESULT ParamConverterInt64(const std::wstring& value, amf::AMFVariant& valueOut);
AMF_RESULT ParamConverterDouble(const std::wstring& value, amf::AMFVariant& valueOut);
AMF_RESULT ParamConverterBoolean(const std::wstring& value, amf::AMFVariant& valueOut);
AMF_RESULT ParamConverterRatio(const std::wstring& value, amf::AMFVariant& valueOut);
AMF_RESULT ParamConverterRate(const std::wstring& value, amf::AMFVariant& valueOut);
AMF_RESULT ParamConverterSize(const std::wstring& value, amf::AMFVariant& valueOut);
AMF_RESULT ParamConverterVideoPresenter(const std::wstring& value, amf::AMFVariant& valueOut);
AMF_RESULT ParamConverterMemoryType(const std::wstring& value, amf::AMFVariant& valueOut);
AMF_RESULT ParamConverterFormat(const std::wstring& value, amf::AMFVariant& valueOut);
AMF_RESULT ParamConverterCodec(const std::wstring& value, amf::AMFVariant& valueOut);
AMF_RESULT ParamConverterColorProfile(const std::wstring& value, amf::AMFVariant& valueOut);

std::wstring AddIndexToPath(const std::wstring& path, amf_int32 index);

inline std::wstring toUpper(const std::wstring& str)
{
    std::wstring result;
    std::transform(str.begin(), str.end(), std::back_inserter(result), toupper);
    return result;
}

class ParametersStorage
{
public:
    ParametersStorage();
    virtual ~ParametersStorage() {}

    AMF_RESULT  SetParam(const wchar_t* name, amf::AMFVariantStruct value);
    AMF_RESULT  GetParam(const wchar_t* name, amf::AMFVariantStruct* value) const;
    AMF_RESULT  SetParamAsString(const std::wstring& name, const std::wstring& value);

    template<typename _T>
    AMF_RESULT  SetParam(const wchar_t* name, const _T& value);
    template<typename _T>
    AMF_RESULT  GetParam(const wchar_t* name, _T& value) const;
    template<typename _T>
    AMF_RESULT GetParamWString(const wchar_t* name, _T& value) const;

    amf_size    GetParamCount() const;
    AMF_RESULT  GetParamAt(amf_size index, std::wstring& name, amf::AMFVariantStruct* value) const;

    typedef AMF_RESULT (*ParamConverter)(const std::wstring& value, amf::AMFVariant& valueOut);

    struct ParamDescription
    {
        std::wstring    m_Name;
        ParamType       m_Type;
        std::wstring    m_Description;
        ParamConverter  m_Converter;

        ParamDescription() : m_Type(ParamUnknown), m_Converter(NULL)
        {
        }
        ParamDescription(const std::wstring &name, ParamType type,const std::wstring &description, ParamConverter converter) : 
            m_Name(name),
            m_Type(type),
            m_Description(description),
            m_Converter(converter)
        {
        }
    };
    AMF_RESULT GetParamDescription(const wchar_t* name, ParamDescription& description);
    AMF_RESULT SetParamDescription(const wchar_t* name, ParamType type, const wchar_t* description, ParamConverter converter);
    std::wstring  GetParamUsage();
protected:
    virtual void OnParamChanged(const wchar_t* name) {}

    std::wstring  GetParamUsage(ParamType type);

    typedef std::map<std::wstring, amf::AMFVariant> ParametersMap; // name / value
    
    ParametersMap m_parameters;
    typedef std::map<std::wstring, ParamDescription> ParamDescriptionMap; // name / description
    mutable amf::AMFCriticalSection m_csSect;

    ParamDescriptionMap m_descriptionMap;
};

typedef std::shared_ptr<ParametersStorage> ParametersStoragePtr;

//----------------------------------------------------------------------------------------------
// template methods implementations
//----------------------------------------------------------------------------------------------
template<typename _T> inline
AMF_RESULT ParametersStorage::SetParam(const wchar_t* name, const _T& value)
{
    AMF_RESULT res = SetParam(name, static_cast<const amf::AMFVariantStruct&>(amf::AMFVariant(value)));
    return res;
}

template<typename _T> inline
AMF_RESULT ParametersStorage::GetParam(const wchar_t* name, _T& value) const
{
    amf::AMFVariant var;
    AMF_RESULT err = GetParam(name, static_cast<amf::AMFVariantStruct*>(&var));
    if(err == AMF_OK)
    {
        value = static_cast<_T>(var);
    }
    return err;
}
template<typename _T> inline
AMF_RESULT ParametersStorage::GetParamWString(const wchar_t* name, _T& value) const
{
    amf::AMFVariant var;
    AMF_RESULT err = GetParam(name, static_cast<amf::AMFVariantStruct*>(&var));
    if(err == AMF_OK)
    {
        value = var.ToWString().c_str();
    }
    return err;
}

AMF_RESULT PushParamsToPropertyStorage(ParametersStorage* pParams, ParamType ptype, amf::AMFPropertyStorage *storage);

#define SETFRAMEPARAMFREQ_PARAM_NAME L"SETFRAMEPARAMFREQ"
#define SETDYNAMICPARAMFREQ_PARAM_NAME L"SETDYNAMICPARAMFREQ"
