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

///-------------------------------------------------------------------------
///  @file   PropertyStorageExImpl.h
///  @brief  AMFPropertyStorageExImpl header
///-------------------------------------------------------------------------
#ifndef __AMFPropertyStorageExImpl_h__
#define __AMFPropertyStorageExImpl_h__
#pragma once

#include "../include/core/PropertyStorageEx.h"
#include "Thread.h"
#include "InterfaceImpl.h"
#include "ObservableImpl.h"
#include "TraceAdapter.h"

namespace amf
{

    AMF_RESULT CastVariantToAMFProperty(AMFVariantStruct* pDest, const AMFVariantStruct* pSrc, AMF_VARIANT_TYPE eType,
        AMF_PROPERTY_CONTENT_TYPE contentType,
        const AMFEnumDescriptionEntry* pEnumDescription = 0);

    //---------------------------------------------------------------------------------------------
    template<typename _TBase> class AMFPropertyStorageExImpl :
        public _TBase,
        public AMFObservableImpl<AMFPropertyStorageObserver>
    {
    public:
        AMFPropertyStorageExImpl() : m_pPropertiesInfo(NULL), m_szPropertiesInfoCount(0), m_PropertyValues()
        {
        }

        virtual ~AMFPropertyStorageExImpl()
        {
        }

        // interface access
        AMF_BEGIN_INTERFACE_MAP
            AMF_INTERFACE_ENTRY(AMFPropertyStorage)
            AMF_INTERFACE_ENTRY(AMFPropertyStorageEx)
        AMF_END_INTERFACE_MAP


        using _TBase::GetProperty;
        using _TBase::SetProperty;

        // interface
        //-------------------------------------------------------------------------------------------------
        virtual AMF_RESULT  AMF_STD_CALL Clear()
        {
            m_PropertyValues.clear();
            return AMF_OK;
        }
        //-------------------------------------------------------------------------------------------------
        virtual AMF_RESULT  AMF_STD_CALL AddTo(AMFPropertyStorage* pDest, bool overwrite, bool deep) const
        {
            deep;
            AMF_RETURN_IF_INVALID_POINTER(pDest);

            AMF_RESULT err = AMF_OK;
            for(amf_map<amf_wstring, AMFVariant>::const_iterator it = m_PropertyValues.begin(); it != m_PropertyValues.end(); it++)
            {
                if(!overwrite)
                {
                    if(pDest->HasProperty(it->first.c_str()))
                    {
                        continue;
                    }
                }
                /*
                AMFClonablePtr pClonable;
                AMFInterfacePtr pInterface = static_cast<AMFInterface*>(it->second);
                if(pInterface && deep)
                {
                    pClonable = AMFClonablePtr(pInterface);
                }
                if(pClonable)
                {
                    AMFClonablePtr pCloned;
                    err = pClonable->Clone(&pCloned);
                    AMF_RETURN_IF_FAILED(err, L"AddTo() - failed to duplicate buffer=%s", it->first.c_str());
                    err = pDest->SetProperty(it->first.c_str(), pCloned);
                    AMF_RETURN_IF_FAILED(err, L"AddTo() - failed to copy property=%s", it->first.c_str());
                }
                else
                */
                {
                    err = pDest->SetProperty(it->first.c_str(), it->second);
                    if(err != AMF_INVALID_ARG) // not validated - skip it
                    {
                        AMF_RETURN_IF_FAILED(err, L"AddTo() - failed to copy property=%s", it->first.c_str());
                    }
                }
            }
            return AMF_OK;
        }
        //-------------------------------------------------------------------------------------------------
        virtual AMF_RESULT  AMF_STD_CALL CopyTo(AMFPropertyStorage* pDest, bool deep) const
        {
            AMF_RETURN_IF_INVALID_POINTER(pDest);

            if(pDest != this)
            {
                pDest->Clear();
                return AddTo(pDest, true, deep);
            }
            else
            {
                return AMF_OK;
            }
        }
        //-------------------------------------------------------------------------------------------------
        virtual AMF_RESULT  AMF_STD_CALL SetProperty(const wchar_t* name, AMFVariantStruct value)
        {
            AMF_RETURN_IF_INVALID_POINTER(name);

            const AMFPropertyInfo* pParamInfo = NULL;
            AMF_RESULT err = GetPropertyInfo(name, &pParamInfo);
            if(err != AMF_OK)
            {
                return err;
            }

            if(!pParamInfo->AllowedWrite())
            {
                return AMF_ACCESS_DENIED;
            }
            return SetPrivateProperty(name, value);
        }
        //-------------------------------------------------------------------------------------------------
        virtual AMF_RESULT  AMF_STD_CALL GetProperty(const wchar_t* name, AMFVariantStruct* pValue) const
        {
            AMF_RETURN_IF_INVALID_POINTER(name);
            AMF_RETURN_IF_INVALID_POINTER(pValue);

            const AMFPropertyInfo* pParamInfo = NULL;
            AMF_RESULT err = GetPropertyInfo(name, &pParamInfo);
            if(err != AMF_OK)
            {
                return err;
            }

            if(!pParamInfo->AllowedRead())
            {
                return AMF_ACCESS_DENIED;
            }
            return GetPrivateProperty(name, pValue);
        }
        //-------------------------------------------------------------------------------------------------
        virtual bool        AMF_STD_CALL HasProperty(const wchar_t* name) const
        {
            const AMFPropertyInfo* pParamInfo = NULL;
            AMF_RESULT err = GetPropertyInfo(name, &pParamInfo);
            if(err != AMF_OK)
            {
                return false;
            }
            return true;
        }
        //-------------------------------------------------------------------------------------------------
        virtual amf_size    AMF_STD_CALL GetPropertyCount() const
        {
            return m_PropertyValues.size();
        }
        //-------------------------------------------------------------------------------------------------
        virtual AMF_RESULT  AMF_STD_CALL GetPropertyAt(amf_size index, wchar_t* name, amf_size nameSize, AMFVariantStruct* pValue) const
        {
            AMF_RETURN_IF_INVALID_POINTER(name);
            AMF_RETURN_IF_INVALID_POINTER(pValue);
            AMF_RETURN_IF_FALSE(nameSize != 0, AMF_INVALID_ARG);
            amf_map<amf_wstring, AMFVariant>::const_iterator found = m_PropertyValues.begin();
            if(found == m_PropertyValues.end())
            {
                return AMF_INVALID_ARG;
            }
            for( amf_size i = 0; i < index; i++)
            {
                found++;
                if(found == m_PropertyValues.end())
                {
                    return AMF_INVALID_ARG;
                }
            }
            size_t copySize = AMF_MIN(nameSize-1, found->first.length());
            memcpy(name, found->first.c_str(), copySize * sizeof(wchar_t));
            name[copySize] = 0;
            AMFVariantCopy(pValue, &found->second);
            return AMF_OK;
        }
        //-------------------------------------------------------------------------------------------------
        virtual amf_size    AMF_STD_CALL GetPropertiesInfoCount() const
        {
            return m_szPropertiesInfoCount;
        }
        //-------------------------------------------------------------------------------------------------
        virtual AMF_RESULT  AMF_STD_CALL GetPropertyInfo(amf_size szInd, const AMFPropertyInfo** ppParamInfo) const
        {
            AMF_RETURN_IF_INVALID_POINTER(ppParamInfo);

            *ppParamInfo = &m_pPropertiesInfo[szInd];
            return AMF_OK;
        }
        //-------------------------------------------------------------------------------------------------
        virtual AMF_RESULT  AMF_STD_CALL GetPropertyInfo(const wchar_t* name, const AMFPropertyInfo** ppParamInfo) const
        {
            AMF_RETURN_IF_INVALID_POINTER(name);
            AMF_RETURN_IF_INVALID_POINTER(ppParamInfo);

            for(amf_size i = 0; i < m_szPropertiesInfoCount; ++i)
            {
                if(wcscmp(m_pPropertiesInfo[i].name, name) == 0)
                {
                    *ppParamInfo = &m_pPropertiesInfo[i];
                    return AMF_OK;
                }
            }
            return AMF_NOT_FOUND;
        }
        //-------------------------------------------------------------------------------------------------
        virtual AMF_RESULT  AMF_STD_CALL ValidateProperty(const wchar_t* name, AMFVariantStruct value, AMFVariantStruct* pOutValidated) const
        {
            AMF_RETURN_IF_INVALID_POINTER(name);
            AMF_RETURN_IF_INVALID_POINTER(pOutValidated);

            AMF_RESULT err = AMF_OK;
            const AMFPropertyInfo* pParamInfo = NULL;

            AMF_RETURN_IF_FAILED(GetPropertyInfo(name, &pParamInfo), L"Property=%s", name);
            AMF_RETURN_IF_FAILED(CastVariantToAMFProperty(pOutValidated, &value, pParamInfo->type, pParamInfo->contentType, pParamInfo->pEnumDescription), L"Property=%s", name);

            switch(pParamInfo->type)
            {
            case AMF_VARIANT_INT64:
                if((pParamInfo->minValue.type != AMF_VARIANT_EMPTY && AMFVariantGetInt64(pOutValidated) < AMFVariantGetInt64(&pParamInfo->minValue)) ||
                    (pParamInfo->maxValue.type != AMF_VARIANT_EMPTY && AMFVariantGetInt64(pOutValidated) > AMFVariantGetInt64(&pParamInfo->maxValue)) )
                {
                    err = AMF_OUT_OF_RANGE;
                }
                break;

            case AMF_VARIANT_DOUBLE:
                if((AMFVariantGetDouble(pOutValidated) < AMFVariantGetDouble(&pParamInfo->minValue)) ||
                   (AMFVariantGetDouble(pOutValidated) > AMFVariantGetDouble(&pParamInfo->maxValue)) )
                {
                    err = AMF_OUT_OF_RANGE;
                }
                break;
            case AMF_VARIANT_SIZE:
                {
                    AMFSize validatedSize = AMFVariantGetSize(pOutValidated);
                    AMFSize minSize = AMFConstructSize(0, 0);
                    AMFSize maxSize = AMFConstructSize(INT_MAX, INT_MAX);
                    if (pParamInfo->minValue.type != AMF_VARIANT_EMPTY)
                    {
                        minSize = AMFVariantGetSize(&pParamInfo->minValue);
                    }
                    if (pParamInfo->maxValue.type != AMF_VARIANT_EMPTY)
                    {
                        maxSize = AMFVariantGetSize(&pParamInfo->maxValue);
                    }
                    if (validatedSize.width < minSize.width || validatedSize.height < minSize.height ||
                        validatedSize.width > maxSize.width || validatedSize.height > maxSize.height)
                    {
                        err = AMF_OUT_OF_RANGE;
                    }

                }

            }
            return err;
        }
        //-------------------------------------------------------------------------------------------------
        virtual AMF_RESULT  AMF_STD_CALL RegisterProperties(class AMFPropertyInfoImpl* pPropertiesInfo, amf_size szPropertiesCount)
        {
            AMF_RETURN_IF_INVALID_POINTER(pPropertiesInfo);

            m_pPropertiesInfo = pPropertiesInfo;
            m_szPropertiesInfoCount = szPropertiesCount;
            return AMF_OK;
        }
        //-------------------------------------------------------------------------------------------------
        virtual void        AMF_STD_CALL OnPropertyChanged(const wchar_t* name){ name; }
        //-------------------------------------------------------------------------------------------------
        virtual void        AMF_STD_CALL AddObserver(AMFPropertyStorageObserver* pObserver) { AMFObservableImpl<AMFPropertyStorageObserver>::AddObserver(pObserver); }
        //-------------------------------------------------------------------------------------------------
        virtual void        AMF_STD_CALL RemoveObserver(AMFPropertyStorageObserver* pObserver) { AMFObservableImpl<AMFPropertyStorageObserver>::RemoveObserver(pObserver); }
        //-------------------------------------------------------------------------------------------------
    protected:
        //-------------------------------------------------------------------------------------------------
        AMF_RESULT SetAccessType(const wchar_t* name, AMF_PROPERTY_ACCESS_TYPE accessType)
        {
            AMF_RETURN_IF_INVALID_POINTER(name);

            AMFPropertyInfo* pPropertyInfo = NULL;
            for(amf_size i = 0; i < m_szPropertiesInfoCount; ++i)
            {
                if(wcscmp(m_pPropertiesInfo[i].name, name) == 0)
                {
                    pPropertyInfo = &m_pPropertiesInfo[i];
                    break;
                }
            }

            if(pPropertyInfo == NULL)
            {
                return AMF_NOT_FOUND;
            }

            pPropertyInfo->accessType = accessType;
            OnPropertyChanged(name);
            NotifyObservers<const wchar_t*>(&AMFPropertyStorageObserver::OnPropertyChanged, name);
            return AMF_OK;
        }
        //-------------------------------------------------------------------------------------------------
        AMF_RESULT SetPrivateProperty(const wchar_t* name, AMFVariantStruct value)
        {
            AMF_RETURN_IF_INVALID_POINTER(name);

            AMFVariant validatedValue;
            AMF_RESULT validateResult = ValidateProperty(name, value, &validatedValue);
            if(AMF_OK == validateResult)
            {
                amf_map<amf_wstring, AMFVariant>::iterator found = m_PropertyValues.find(name);
                if(found != m_PropertyValues.end())
                {
                    if(found->second == value)
                    {
                        return AMF_OK;
                    }
                    found->second = value;
                }
                else
                {
                    m_PropertyValues[name] = validatedValue;
                }
                OnPropertyChanged(name);
                NotifyObservers<const wchar_t*>(&AMFPropertyStorageObserver::OnPropertyChanged, name);
            }
            return validateResult;
        }
        //-------------------------------------------------------------------------------------------------
        AMF_RESULT GetPrivateProperty(const wchar_t* name, AMFVariantStruct* pValue) const
        {
            AMF_RETURN_IF_INVALID_POINTER(name);
            AMF_RETURN_IF_INVALID_POINTER(pValue);

            amf_map<amf_wstring, AMFVariant>::const_iterator found = m_PropertyValues.find(name);
            if(found != m_PropertyValues.end())
            {
                AMFVariantCopy(pValue, &found->second);
                return AMF_OK;
            }
            else
            {
                const AMFPropertyInfo* pParamInfo;
                if(GetPropertyInfo(name, &pParamInfo) == AMF_OK)
                {
                    AMFVariantCopy(pValue, &pParamInfo->defaultValue);
                    return AMF_OK;
                }
            }
            return AMF_NOT_FOUND;
        }
        //-------------------------------------------------------------------------------------------------
        template<typename _T>
        AMF_RESULT          AMF_STD_CALL SetPrivateProperty(const wchar_t* name, const _T& value)
        {
            AMF_RESULT err = SetPrivateProperty(name, static_cast<const AMFVariantStruct&>(AMFVariant(value)));
            return err;
        }
        //-------------------------------------------------------------------------------------------------
        template<typename _T>
        AMF_RESULT          AMF_STD_CALL GetPrivateProperty(const wchar_t* name, _T* pValue) const
        {
            AMFVariant var;
            AMF_RESULT err = GetPrivateProperty(name, static_cast<AMFVariantStruct*>(&var));
            if(err == AMF_OK)
            {
                *pValue = static_cast<_T>(var);
            }
            return err;
        }
        //-------------------------------------------------------------------------------------------------
        template<>
        inline AMF_RESULT AMF_STD_CALL GetPrivateProperty(const wchar_t* name, AMFInterface** ppValue) const
        {
            AMFVariant var;
            AMF_RESULT err = GetPrivateProperty(name, static_cast<AMFVariantStruct*>(&var));
            if(err == AMF_OK)
            {
                *ppValue = static_cast<AMFInterface*>(var);
            }
            if(*ppValue)
            {
                (*ppValue)->Acquire();
            }
            return err;
        }

        //-------------------------------------------------------------------------------------------------
        bool HasPrivateProperty(const wchar_t* name) const
        {
            return m_PropertyValues.find(name) != m_PropertyValues.end();
        }
        //-------------------------------------------------------------------------------------------------
        class AMFPropertyInfoImpl * m_pPropertiesInfo;
        amf_size m_szPropertiesInfoCount;

        amf_map<amf_wstring, AMFVariant> m_PropertyValues;
    private:
        AMFPropertyStorageExImpl(const AMFPropertyStorageExImpl&);
        AMFPropertyStorageExImpl& operator=(const AMFPropertyStorageExImpl&);
    };
    extern AMFCriticalSection ms_csAMFPropertyStorageExImplMaps;

    //---------------------------------------------------------------------------------------------
    class AMFPropertyInfoImpl : public AMFPropertyInfo
    {
    private:
        amf_wstring m_name;
        amf_wstring m_desc;

        void Init(const wchar_t* name, const wchar_t* desc, AMF_VARIANT_TYPE type, AMF_PROPERTY_CONTENT_TYPE contentType,
            AMFVariantStruct defaultValue, AMFVariantStruct minValue, AMFVariantStruct maxValue, AMF_PROPERTY_ACCESS_TYPE accessType,
            const AMFEnumDescriptionEntry* pEnumDescription);
    public:
        AMFPropertyInfoImpl(const wchar_t* name, const wchar_t* desc, AMF_VARIANT_TYPE type, AMF_PROPERTY_CONTENT_TYPE contentType,
            AMFVariantStruct defaultValue, AMFVariantStruct minValue, AMFVariantStruct maxValue, bool allowChangeInRuntime,
            const AMFEnumDescriptionEntry* pEnumDescription);
        AMFPropertyInfoImpl(const wchar_t* name, const wchar_t* desc, AMF_VARIANT_TYPE type, AMF_PROPERTY_CONTENT_TYPE contentType,
            AMFVariantStruct defaultValue, AMFVariantStruct minValue, AMFVariantStruct maxValue, AMF_PROPERTY_ACCESS_TYPE accessType,
            const AMFEnumDescriptionEntry* pEnumDescription);
        AMFPropertyInfoImpl();

        AMFPropertyInfoImpl(const AMFPropertyInfoImpl& propertyInfo);
        AMFPropertyInfoImpl& operator=(const AMFPropertyInfoImpl& propertyInfo);

        ~AMFPropertyInfoImpl();
    };


    #define AMFPrimitivePropertyInfoMapBegin \
        { \
            amf::AMFLock __lock(&ms_csAMFPropertyStorageExImplMaps);\
            static AMFPropertyInfoImpl s_PropertiesInfo[] = \
            { \

    #define AMFPrimitivePropertyInfoMapEnd \
        }; \
        RegisterProperties(s_PropertiesInfo, sizeof(s_PropertiesInfo) / sizeof(AMFPropertyInfoImpl)); \
        } \


    #define AMFPropertyInfoBool(_name, _desc, _defaultValue, _AllowChangeInRuntime) \
        AMFPropertyInfoImpl(_name, _desc, AMF_VARIANT_BOOL, 0, AMFVariant(_defaultValue), \
            AMFVariant(), AMFVariant(), _AllowChangeInRuntime, 0)

    #define AMFPropertyInfoEnum(_name, _desc, _defaultValue, pEnumDescription, _AllowChangeInRuntime) \
        AMFPropertyInfoImpl(_name, _desc, AMF_VARIANT_INT64, 0, AMFVariant(amf_int64(_defaultValue)), \
            AMFVariant(), AMFVariant(), _AllowChangeInRuntime, pEnumDescription)

    #define AMFPropertyInfoInt64(_name, _desc, _defaultValue, _minValue, _maxValue, _AllowChangeInRuntime) \
        AMFPropertyInfoImpl(_name, _desc, AMF_VARIANT_INT64, 0, AMFVariant(amf_int64(_defaultValue)), \
            AMFVariant(amf_int64(_minValue)), AMFVariant(amf_int64(_maxValue)), _AllowChangeInRuntime, 0)

    #define AMFPropertyInfoDouble(_name, _desc, _defaultValue, _minValue, _maxValue, _AllowChangeInRuntime) \
        AMFPropertyInfoImpl(_name, _desc, AMF_VARIANT_DOUBLE, 0, AMFVariant(amf_double(_defaultValue)), \
            AMFVariant(amf_double(_minValue)), AMFVariant(amf_double(_maxValue)), _AllowChangeInRuntime, 0)


    #define AMFPropertyInfoRect(_name, _desc, defaultLeft, defaultTop, defaultRight, defaultBottom, _AllowChangeInRuntime) \
        AMFPropertyInfoImpl(_name, _desc, AMF_VARIANT_RECT, 0, AMFVariant(AMFConstructRect(defaultLeft, defaultTop, defaultRight, defaultBottom)), \
            AMFVariant(), AMFVariant(), _AllowChangeInRuntime, 0)

    #define AMFPropertyInfoPoint(_name, _desc, defaultX, defaultY, _AllowChangeInRuntime) \
        AMFPropertyInfoImpl(_name, _desc, AMF_VARIANT_POINT, 0, AMFVariant(AMFConstructPoint(defaultX, defaultY)), \
            AMFVariant(), AMFVariant(), _AllowChangeInRuntime, 0)

    #define AMFPropertyInfoSize(_name, _desc, _defaultValue, _minValue, _maxValue, _AllowChangeInRuntime) \
        AMFPropertyInfoImpl(_name, _desc, AMF_VARIANT_SIZE, 0, AMFVariant(AMFSize(_defaultValue)), \
            AMFVariant(AMFSize(_minValue)), AMFVariant(AMFSize(_maxValue)), _AllowChangeInRuntime, 0)

    #define AMFPropertyInfoRate(_name, _desc, defaultNum, defaultDen, _AllowChangeInRuntime) \
        AMFPropertyInfoImpl(_name, _desc, AMF_VARIANT_RATE, 0, AMFVariant(AMFConstructRate(defaultNum, defaultDen)), \
            AMFVariant(), AMFVariant(), _AllowChangeInRuntime, 0)

    #define AMFPropertyInfoRatio(_name, _desc, defaultNum, defaultDen, _AllowChangeInRuntime) \
        AMFPropertyInfoImpl(_name, _desc, AMF_VARIANT_RATIO, 0, AMFVariant(AMFConstructRatio(defaultNum, defaultDen)), \
            AMFVariant(), AMFVariant(), _AllowChangeInRuntime, 0)

    #define AMFPropertyInfoColor(_name, _desc, defaultR, defaultG, defaultB, defaultA, _AllowChangeInRuntime) \
        AMFPropertyInfoImpl(_name, _desc, AMF_VARIANT_COLOR, 0, AMFVariant(AMFConstructColor(defaultR, defaultG, defaultB, defaultA)), \
            AMFVariant(), AMFVariant(), _AllowChangeInRuntime, 0)


    #define AMFPropertyInfoString(_name, _desc, _defaultValue, _AllowChangeInRuntime) \
        AMFPropertyInfoImpl(_name, _desc, AMF_VARIANT_STRING, 0, AMFVariant(_defaultValue), \
            AMFVariant(), AMFVariant(), _AllowChangeInRuntime, 0)

    #define AMFPropertyInfoWString(_name, _desc, _defaultValue, _AllowChangeInRuntime) \
        AMFPropertyInfoImpl(_name, _desc, AMF_VARIANT_WSTRING, 0, AMFVariant(_defaultValue), \
            AMFVariant(), AMFVariant(), _AllowChangeInRuntime, 0)

    #define AMFPropertyInfoInterface(_name, _desc, _defaultValue, _AllowChangeInRuntime) \
        AMFPropertyInfoImpl(_name, _desc, AMF_VARIANT_INTERFACE, 0, AMFVariant(AMFInterfacePtr(_defaultValue)), \
            AMFVariant(AMFInterfacePtr()), AMFVariant(AMFInterfacePtr()), _AllowChangeInRuntime, 0)


    #define AMFPropertyInfoXML(_name, _desc, _defaultValue, _AllowChangeInRuntime) \
            AMFPropertyInfoImpl(_name, _desc, AMF_VARIANT_STRING, AMF_PROPERTY_CONTENT_XML, AMFVariant(_defaultValue), \
                AMFVariant(), AMFVariant(), _AllowChangeInRuntime, 0)

    #define AMFPropertyInfoPath(_name, _desc, _defaultValue, _AllowChangeInRuntime) \
            AMFPropertyInfoImpl(_name, _desc, AMF_VARIANT_WSTRING, AMF_PROPERTY_CONTENT_FILE_OPEN_PATH, AMFVariant(_defaultValue), \
                AMFVariant(), AMFVariant(), _AllowChangeInRuntime, 0)

    #define AMFPropertyInfoSavePath(_name, _desc, _defaultValue, _AllowChangeInRuntime) \
            AMFPropertyInfoImpl(_name, _desc, AMF_VARIANT_WSTRING, AMF_PROPERTY_CONTENT_FILE_SAVE_PATH, AMFVariant(_defaultValue), \
                AMFVariant(), AMFVariant(), _AllowChangeInRuntime, 0)

} // namespace amf

#endif // #ifndef __AMFPropertyStorageExImpl_h__
