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

#ifndef AMF_PropertyStorageEx_h
#define AMF_PropertyStorageEx_h
#pragma once

#include "PropertyStorage.h"

#if defined(__cplusplus)
namespace amf
{
#endif
    //----------------------------------------------------------------------------------------------
    typedef enum AMF_PROPERTY_CONTENT_ENUM
    {
        AMF_PROPERTY_CONTENT_DEFAULT = 0,
        AMF_PROPERTY_CONTENT_XML,               // m_eType is AMF_VARIANT_STRING

        AMF_PROPERTY_CONTENT_FILE_OPEN_PATH,    // m_eType AMF_VARIANT_WSTRING
        AMF_PROPERTY_CONTENT_FILE_SAVE_PATH,    // m_eType AMF_VARIANT_WSTRING

        AMF_PROPERTY_CONTENT_INTEGER_ARRAY,     // m_eType AMF_VARIANT_INTERFACE
        AMF_PROPERTY_CONTENT_FLOAT_ARRAY        // m_eType AMF_VARIANT_INTERFACE
    } AMF_PROPERTY_CONTENT_ENUM;
    //----------------------------------------------------------------------------------------------
    typedef enum AMF_PROPERTY_ACCESS_TYPE
    {
        AMF_PROPERTY_ACCESS_PRIVATE             = 0,
        AMF_PROPERTY_ACCESS_READ                = 0x1,
        AMF_PROPERTY_ACCESS_WRITE               = 0x2,
        AMF_PROPERTY_ACCESS_READ_WRITE          = (AMF_PROPERTY_ACCESS_READ | AMF_PROPERTY_ACCESS_WRITE),
        AMF_PROPERTY_ACCESS_WRITE_RUNTIME       = 0x4,
        AMF_PROPERTY_ACCESS_FULL                = 0xFF,
		AMF_PROPERTY_ACCESS_NON_PERSISTANT		= 0x4000,
		AMF_PROPERTY_ACCESS_NON_PERSISTANT_READ = (AMF_PROPERTY_ACCESS_NON_PERSISTANT | AMF_PROPERTY_ACCESS_READ),
		AMF_PROPERTY_ACCESS_NON_PERSISTANT_READ_WRITE = (AMF_PROPERTY_ACCESS_NON_PERSISTANT | AMF_PROPERTY_ACCESS_READ_WRITE),
		AMF_PROPERTY_ACCESS_NON_PERSISTANT_FULL = (AMF_PROPERTY_ACCESS_NON_PERSISTANT | AMF_PROPERTY_ACCESS_FULL),
		AMF_PROPERTY_ACCESS_INVALID				= 0x8000
    } AMF_PROPERTY_ACCESS_TYPE;

    //----------------------------------------------------------------------------------------------
    typedef struct AMFEnumDescriptionEntry
    {
        amf_int             value;
        const wchar_t*      name;
    } AMFEnumDescriptionEntry;
    //----------------------------------------------------------------------------------------------
    typedef amf_uint32 AMF_PROPERTY_CONTENT_TYPE;

    typedef struct AMFPropertyInfo
    {
        // Previously, AMFPropertyInfo declared a virtual destructor. This placed a vtable pointer at offset 0, 
        // shifting all other fields by the size of this pointer. This destructor was removed by default.
        // To retain ABI compatibility, a dummy pointer is prepended to the AMFPropertyInfo definition, occupying
        // the space previously taken by the vtable pointer. This pointer is intended to never be read or written.
        // Without it, fields would be read from the incorrect offset.
        // 
        // To opt into the previous behavior, the macro 'AMF_PROPERTYINFO_USE_VIRTUAL_DESTRUCTOR' can be defined 
        // as a project-wide flag.
#if !defined(AMF_PROPERTYINFO_USE_VIRTUAL_DESTRUCTOR) || !defined(__cplusplus)
        void*                           reserved;
#endif
        const wchar_t*                  name;
        const wchar_t*                  desc;
        AMF_VARIANT_TYPE                type;
        AMF_PROPERTY_CONTENT_TYPE       contentType;

        AMFVariantStruct                defaultValue;
        AMFVariantStruct                minValue;
        AMFVariantStruct                maxValue;
        AMF_PROPERTY_ACCESS_TYPE        accessType;
        const AMFEnumDescriptionEntry*  pEnumDescription;

#if defined(__cplusplus)
        AMFPropertyInfo() :
#if !defined(AMF_PROPERTYINFO_USE_VIRTUAL_DESTRUCTOR)
            reserved(nullptr),
#endif
            name(nullptr),
            desc(nullptr),
            type(),
            contentType(),
            defaultValue(),
            minValue(),
            maxValue(),
            accessType(AMF_PROPERTY_ACCESS_FULL),
            pEnumDescription(nullptr)
        {}

#if defined(AMF_PROPERTYINFO_USE_VIRTUAL_DESTRUCTOR)
        AMFPropertyInfo(const AMFPropertyInfo& property) = default;
        AMFPropertyInfo& operator=(const AMFPropertyInfo& property) = default;

        // The AMFPropertyInfo virtual destructor was removed by default. To remain
        // backwards compatible with code that relies upon this virtual destructor,
        // the 'AMF_PROPERTYINFO_USE_VIRTUAL_DESTRUCTOR' macro can be defined as a project-wide flag,
        // which will add the virtual destructor back into the AMFPropertyInfo definition.
        virtual ~AMFPropertyInfo(){}
#endif

        amf_bool AMF_STD_CALL AllowedRead() const
        {
            return (accessType & AMF_PROPERTY_ACCESS_READ) != 0;
        }
        amf_bool AMF_STD_CALL AllowedWrite() const
        {
            return (accessType & AMF_PROPERTY_ACCESS_WRITE) != 0;
        }
        amf_bool AMF_STD_CALL AllowedChangeInRuntime() const
        {
            return (accessType & AMF_PROPERTY_ACCESS_WRITE_RUNTIME) != 0;
        }


#endif // #if defined(__cplusplus)
    } AMFPropertyInfo;
    //----------------------------------------------------------------------------------------------
    // AMFPropertyStorageEx interface
    //----------------------------------------------------------------------------------------------
#if defined(__cplusplus)
    class AMF_NO_VTABLE AMFPropertyStorageEx : public AMFPropertyStorage
    {
    public:
        AMF_DECLARE_IID(0x16b8958d, 0xe943, 0x4a33, 0xa3, 0x5a, 0x88, 0x5a, 0xd8, 0x28, 0xf2, 0x67)

        virtual amf_size            AMF_STD_CALL GetPropertiesInfoCount() const = 0;

        // The AMF runtime is compiled with MSVC on Windows. MSVC places overloaded virtual methods into the vtable in reverse declaration order.
        // Therefore, the declaration order of these overloads is reversed for callers on Windows that use compilers that do not use the MSVC ABI.
        // Otherwise, calls to these overloads could map to the wrong method within the AMF runtime.
#if defined(_MSC_VER) || !defined(_WIN32)
        virtual AMF_RESULT          AMF_STD_CALL GetPropertyInfo(amf_size index, const AMFPropertyInfo** ppInfo) const = 0;
        virtual AMF_RESULT          AMF_STD_CALL GetPropertyInfo(const wchar_t* name, const AMFPropertyInfo** ppInfo) const = 0;
#else
        virtual AMF_RESULT          AMF_STD_CALL GetPropertyInfo(const wchar_t* name, const AMFPropertyInfo** ppInfo) const = 0;
        virtual AMF_RESULT          AMF_STD_CALL GetPropertyInfo(amf_size index, const AMFPropertyInfo** ppInfo) const = 0;
#endif
        virtual AMF_RESULT          AMF_STD_CALL ValidateProperty(const wchar_t* name, AMFVariantStruct value, AMFVariantStruct* pOutValidated) const = 0;
    };
    //----------------------------------------------------------------------------------------------
    // smart pointer
    //----------------------------------------------------------------------------------------------
    typedef AMFInterfacePtr_T<AMFPropertyStorageEx> AMFPropertyStorageExPtr;
#else // #if defined(__cplusplus)
    AMF_DECLARE_IID(AMFPropertyStorageEx, 0x16b8958d, 0xe943, 0x4a33, 0xa3, 0x5a, 0x88, 0x5a, 0xd8, 0x28, 0xf2, 0x67)
    typedef struct AMFPropertyStorageEx AMFPropertyStorageEx;

    typedef struct AMFPropertyStorageExVtbl
    {
        // AMFInterface interface
        amf_long            (AMF_STD_CALL *Acquire)(AMFPropertyStorageEx* pThis);
        amf_long            (AMF_STD_CALL *Release)(AMFPropertyStorageEx* pThis);
        enum AMF_RESULT     (AMF_STD_CALL *QueryInterface)(AMFPropertyStorageEx* pThis, const struct AMFGuid *interfaceID, void** ppInterface);

        // AMFPropertyStorage interface
        AMF_RESULT          (AMF_STD_CALL *SetProperty)(AMFPropertyStorageEx* pThis, const wchar_t* name, AMFVariantStruct value);
        AMF_RESULT          (AMF_STD_CALL *GetProperty)(AMFPropertyStorageEx* pThis, const wchar_t* name, AMFVariantStruct* pValue);
        amf_bool            (AMF_STD_CALL *HasProperty)(AMFPropertyStorageEx* pThis, const wchar_t* name);
        amf_size            (AMF_STD_CALL *GetPropertyCount)(AMFPropertyStorageEx* pThis);
        AMF_RESULT          (AMF_STD_CALL *GetPropertyAt)(AMFPropertyStorageEx* pThis, amf_size index, wchar_t* name, amf_size nameSize, AMFVariantStruct* pValue);
        AMF_RESULT          (AMF_STD_CALL *Clear)(AMFPropertyStorageEx* pThis);
        AMF_RESULT          (AMF_STD_CALL *AddTo)(AMFPropertyStorageEx* pThis, AMFPropertyStorage* pDest, amf_bool overwrite, amf_bool deep);
        AMF_RESULT          (AMF_STD_CALL *CopyTo)(AMFPropertyStorageEx* pThis, AMFPropertyStorage* pDest, amf_bool deep);
        void                (AMF_STD_CALL *AddObserver)(AMFPropertyStorageEx* pThis, AMFPropertyStorageObserver* pObserver);
        void                (AMF_STD_CALL *RemoveObserver)(AMFPropertyStorageEx* pThis, AMFPropertyStorageObserver* pObserver);

        // AMFPropertyStorageEx interface
        amf_size            (AMF_STD_CALL *GetPropertiesInfoCount)(AMFPropertyStorageEx* pThis);
        // The AMF runtime is compiled with MSVC on Windows. MSVC places overloaded virtual methods into the vtable in reverse declaration order.
        // Therefore, on Windows, the order of these function pointers must be reversed to reflect the vtable layout of their corresponding methods in the AMF runtime.
        // On Linux, in the AMF runtime, overloaded virtual methods are placed into the vtable in the order of declaration.
        // Therefore, on Linux, these function pointers do not have their order reversed.
#if defined(_WIN32)
        AMF_RESULT          (AMF_STD_CALL *GetPropertyInfo)(AMFPropertyStorageEx* pThis, const wchar_t* name, const AMFPropertyInfo** ppInfo);
        AMF_RESULT          (AMF_STD_CALL *GetPropertyInfoAt)(AMFPropertyStorageEx* pThis, amf_size index, const AMFPropertyInfo** ppInfo);
#else
        AMF_RESULT          (AMF_STD_CALL *GetPropertyInfoAt)(AMFPropertyStorageEx* pThis, amf_size index, const AMFPropertyInfo** ppInfo);
        AMF_RESULT          (AMF_STD_CALL *GetPropertyInfo)(AMFPropertyStorageEx* pThis, const wchar_t* name, const AMFPropertyInfo** ppInfo);
#endif
        AMF_RESULT          (AMF_STD_CALL *ValidateProperty)(AMFPropertyStorageEx* pThis, const wchar_t* name, AMFVariantStruct value, AMFVariantStruct* pOutValidated);

    } AMFPropertyStorageExVtbl;

    struct AMFPropertyStorageEx
    {
        const AMFPropertyStorageExVtbl *pVtbl;
    };
#endif // #if defined(__cplusplus)
    //----------------------------------------------------------------------------------------------
#if defined(__cplusplus)
} //namespace amf
#endif


#endif //#ifndef AMF_PropertyStorageEx_h
