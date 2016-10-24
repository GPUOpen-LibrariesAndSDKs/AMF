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

#include <climits>
#include "PropertyStorageExImpl.h"
#include "PropertyStorageImpl.h"
#include "TraceAdapter.h"

#pragma warning(disable: 4996)

using namespace amf;

#define AMF_FACILITY L"AMFPropertyStorageExImpl"
amf::AMFCriticalSection amf::ms_csAMFPropertyStorageExImplMaps;

//-------------------------------------------------------------------------------------------------
AMF_RESULT amf::CastVariantToAMFProperty(amf::AMFVariantStruct* pDest, const amf::AMFVariantStruct* pSrc, amf::AMF_VARIANT_TYPE eType,
        amf::AMF_PROPERTY_CONTENT_TYPE contentType,
        const amf::AMFEnumDescriptionEntry* pEnumDescription)
{
    contentType;
    AMF_RETURN_IF_INVALID_POINTER(pDest);

    AMF_RESULT err = AMF_OK;
    switch(eType)
    {

    case AMF_VARIANT_INT64:
    {
        if(pEnumDescription)
        {
            const AMFEnumDescriptionEntry* pEnumDescriptionCache = pEnumDescription;
            err = AMFVariantChangeType(pDest, pSrc, AMF_VARIANT_INT64);
            bool found = false;
            if(err == AMF_OK)
            {
                //mean numeric came. validating
                while(pEnumDescriptionCache->name)
                {
                    if(pEnumDescriptionCache->value == AMFVariantGetInt64(pDest))
                    {
                        AMFVariantAssignInt64(pDest, pEnumDescriptionCache->value);
                        found = true;
                        break;
                    }
                    pEnumDescriptionCache++;
                }
                err = found ? AMF_OK : AMF_INVALID_ARG;
            }
            if(!found)
            {
                pEnumDescriptionCache = pEnumDescription;
                err = AMFVariantChangeType(pDest, pSrc, AMF_VARIANT_WSTRING);
                if(err == AMF_OK)
                {
                    //string came. validating and assigning numeric
                    bool found = false;
                    while(pEnumDescriptionCache->name)
                    {
                        if(amf_wstring(pEnumDescriptionCache->name) == AMFVariantGetWString(pDest))
                        {
                            AMFVariantAssignInt64(pDest, pEnumDescriptionCache->value);
                            found = true;
                            break;
                        }
                        pEnumDescriptionCache++;
                    }
                    err = found ? AMF_OK : AMF_INVALID_ARG;
                }
            }
        }
        else
        {
            err = AMFVariantChangeType(pDest, pSrc, AMF_VARIANT_INT64);
        }
    }
    break;

    default:
        err = AMFVariantChangeType(pDest, pSrc, eType);
    break;
    }
    return err;
}
//-------------------------------------------------------------------------------------------------
AMFPropertyInfoImpl::AMFPropertyInfoImpl(const wchar_t* name, const wchar_t* desc, AMF_VARIANT_TYPE type, AMF_PROPERTY_CONTENT_TYPE contentType,
        AMFVariantStruct defaultValue, AMFVariantStruct minValue, AMFVariantStruct maxValue, bool allowChangeInRuntime,
        const AMFEnumDescriptionEntry* pEnumDescription) : m_name(), m_desc()
{
    AMF_PROPERTY_ACCESS_TYPE accessType = allowChangeInRuntime ? AMF_PROPERTY_ACCESS_FULL : AMF_PROPERTY_ACCESS_READ_WRITE;
    Init(name, desc, type, contentType, defaultValue, minValue, maxValue, accessType, pEnumDescription);
}
//-------------------------------------------------------------------------------------------------
AMFPropertyInfoImpl::AMFPropertyInfoImpl(const wchar_t* name, const wchar_t* desc, AMF_VARIANT_TYPE type, AMF_PROPERTY_CONTENT_TYPE contentType,
        AMFVariantStruct defaultValue, AMFVariantStruct minValue, AMFVariantStruct maxValue, AMF_PROPERTY_ACCESS_TYPE accessType,
        const AMFEnumDescriptionEntry* pEnumDescription) : m_name(), m_desc()
{
    Init(name, desc, type, contentType, defaultValue, minValue, maxValue, accessType, pEnumDescription);
}
//-------------------------------------------------------------------------------------------------
AMFPropertyInfoImpl::AMFPropertyInfoImpl() : m_name(), m_desc()
{
    AMFVariantInit(&this->defaultValue);
    AMFVariantInit(&this->minValue);
    AMFVariantInit(&this->maxValue);

    name = L"";
    desc = L"";
    type = AMF_VARIANT_EMPTY;
    contentType = AMF_PROPERTY_CONTENT_TYPE(-1);
    accessType = AMF_PROPERTY_ACCESS_FULL;
}
//-------------------------------------------------------------------------------------------------
void AMFPropertyInfoImpl::Init(const wchar_t* name, const wchar_t* desc, AMF_VARIANT_TYPE type, AMF_PROPERTY_CONTENT_TYPE contentType,
        AMFVariantStruct defaultValue, AMFVariantStruct minValue, AMFVariantStruct maxValue, AMF_PROPERTY_ACCESS_TYPE accessType,
        const AMFEnumDescriptionEntry* pEnumDescription)
{
    m_name = name;
    this->name = m_name.c_str();

    m_desc = desc;
    this->desc = m_desc.c_str();

    this->type = type;
    this->contentType = contentType;
    this->accessType = accessType;
    AMFVariantInit(&this->defaultValue);
    AMFVariantInit(&this->minValue);
    AMFVariantInit(&this->maxValue);
    this->pEnumDescription = pEnumDescription;

    switch(type)
    {
    case AMF_VARIANT_BOOL:
    {
        if(CastVariantToAMFProperty(&this->defaultValue, &defaultValue, type, contentType, pEnumDescription) != AMF_OK)
        {
            AMFVariantAssignBool(&this->defaultValue, false);
        }
    }
    break;
    case AMF_VARIANT_RECT:
    {
        if(CastVariantToAMFProperty(&this->defaultValue, &defaultValue, type, contentType, pEnumDescription) != AMF_OK)
        {
            AMFVariantAssignRect(&this->defaultValue, AMFConstructRect(0, 0, 0, 0));
        }
    }
    break;
    case AMF_VARIANT_SIZE:
    {
        if(CastVariantToAMFProperty(&this->defaultValue, &defaultValue, type, contentType, pEnumDescription) != AMF_OK)
        {
            AMFVariantAssignSize(&this->defaultValue, AMFConstructSize(0, 0));
        }
        if (CastVariantToAMFProperty(&this->minValue, &minValue, type, contentType, pEnumDescription) != AMF_OK)
        {
            AMFVariantAssignSize(&this->minValue, AMFConstructSize(0, 0));
        }
        if (CastVariantToAMFProperty(&this->maxValue, &maxValue, type, contentType, pEnumDescription) != AMF_OK)
        {
            AMFVariantAssignSize(&this->maxValue, AMFConstructSize(0, 0));
        }
    }
    break;
    case AMF_VARIANT_POINT:
    {
        if(CastVariantToAMFProperty(&this->defaultValue, &defaultValue, type, contentType, pEnumDescription) != AMF_OK)
        {
            AMFVariantAssignPoint(&this->defaultValue, AMFConstructPoint(0, 0));
        }
    }
    break;
    case AMF_VARIANT_RATE:
    {
        if(CastVariantToAMFProperty(&this->defaultValue, &defaultValue, type, contentType, pEnumDescription) != AMF_OK)
        {
            AMFVariantAssignRate(&this->defaultValue, AMFConstructRate(0, 0));
        }
    }
    break;
    case AMF_VARIANT_RATIO:
    {
        if(CastVariantToAMFProperty(&this->defaultValue, &defaultValue, type, contentType, pEnumDescription) != AMF_OK)
        {
            AMFVariantAssignRatio(&this->defaultValue, AMFConstructRatio(0, 0));
        }
    }
    break;
    case AMF_VARIANT_COLOR:
    {
        if(CastVariantToAMFProperty(&this->defaultValue, &defaultValue, type, contentType, pEnumDescription) != AMF_OK)
        {
            AMFVariantAssignColor(&this->defaultValue, AMFConstructColor(0, 0, 0, 255));
        }
    }
    break;

    case AMF_VARIANT_INT64:
    {
        if(pEnumDescription)
        {
            if(CastVariantToAMFProperty(&this->defaultValue, &defaultValue, type, contentType, pEnumDescription) != AMF_OK)
            {
                AMFVariantAssignInt64(&this->defaultValue, pEnumDescription->value);
            }
        }
        else //AMF_PROPERTY_CONTENT_DEFAULT
        {
            if(CastVariantToAMFProperty(&this->defaultValue, &defaultValue, type, contentType, pEnumDescription) != AMF_OK)
            {
                AMFVariantAssignInt64(&this->defaultValue, 0);
            }
            if(CastVariantToAMFProperty(&this->minValue, &minValue, type, contentType, pEnumDescription) != AMF_OK)
            {
                AMFVariantAssignInt64(&this->minValue, INT_MIN);
            }
            if(CastVariantToAMFProperty(&this->maxValue, &maxValue, type, contentType, pEnumDescription) != AMF_OK)
            {
                AMFVariantAssignInt64(&this->maxValue, INT_MAX);
            }
        }
    }
    break;

    case AMF_VARIANT_DOUBLE:
    {
        if(CastVariantToAMFProperty(&this->defaultValue, &defaultValue, type, contentType, pEnumDescription) != AMF_OK)
        {
            AMFVariantAssignDouble(&this->defaultValue, 0);
        }
        if(CastVariantToAMFProperty(&this->minValue, &minValue, type, contentType, pEnumDescription) != AMF_OK)
        {
            AMFVariantAssignDouble(&this->minValue, INT_MIN);
        }
        if(CastVariantToAMFProperty(&this->maxValue, &maxValue, type, contentType, pEnumDescription) != AMF_OK)
        {
            AMFVariantAssignDouble(&this->maxValue, INT_MAX);
        }
    }
    break;

    case AMF_VARIANT_STRING:
    {
        if(CastVariantToAMFProperty(&this->defaultValue, &defaultValue, type, contentType, pEnumDescription) != AMF_OK)
        {
            AMFVariantAssignString(&this->maxValue, "");
        }
    }
    break;

    case AMF_VARIANT_WSTRING:
    {
        if(CastVariantToAMFProperty(&this->defaultValue, &defaultValue, type, contentType, pEnumDescription) != AMF_OK)
        {
            AMFVariantAssignWString(&this->maxValue, L"");
        }
    }
    break;

    case AMF_VARIANT_INTERFACE:
        if(CastVariantToAMFProperty(&this->defaultValue, &defaultValue, type, contentType, pEnumDescription) != AMF_OK)
        {
            AMFVariantAssignWString(&this->maxValue, L"");
        }
        break;

    default:
        break;
    }
}

AMFPropertyInfoImpl::AMFPropertyInfoImpl(const AMFPropertyInfoImpl& propertyInfo) : AMFPropertyInfo(), m_name(), m_desc()
{
    Init(propertyInfo.name, propertyInfo.desc, propertyInfo.type, propertyInfo.contentType, propertyInfo.defaultValue, propertyInfo.minValue, propertyInfo.maxValue, propertyInfo.accessType, propertyInfo.pEnumDescription);
}
//-------------------------------------------------------------------------------------------------
AMFPropertyInfoImpl& AMFPropertyInfoImpl::operator=(const AMFPropertyInfoImpl& propertyInfo)
{
    // store name and desc inside instance in m_sName and m_sDesc recpectively;
    // m_pName and m_pDesc are pointed to our local copies
    this->m_name = propertyInfo.name;
    this->m_desc = propertyInfo.desc;
    this->name = m_name.c_str();
    this->desc = m_desc.c_str();

    this->type = propertyInfo.type;
    this->contentType = propertyInfo.contentType;
    this->accessType = propertyInfo.accessType;
    AMFVariantCopy(&this->defaultValue, &propertyInfo.defaultValue);
    AMFVariantCopy(&this->minValue, &propertyInfo.minValue);
    AMFVariantCopy(&this->maxValue, &propertyInfo.maxValue);
    this->pEnumDescription = propertyInfo.pEnumDescription;

    return *this;
}
//-------------------------------------------------------------------------------------------------
AMFPropertyInfoImpl::~AMFPropertyInfoImpl()
{
    AMFVariantClear(&this->defaultValue);
    AMFVariantClear(&this->minValue);
    AMFVariantClear(&this->maxValue);
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
