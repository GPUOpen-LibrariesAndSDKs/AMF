#include "Options.h"
#include "CmdLogger.h"
#include <fstream> 

static bool AMF_STD_CALL amf_make_dir(const amf_wstring& path)
{
    bool result = true;
    amf_wstring::size_type pos = 0;
    for(;; )
    {
        pos = path.find(PATH_SEPARATOR_WCHAR, pos + 1);
        if(pos == 0)
        {
            continue;
        }
        if((pos == 2) && (path[1] == L':')) // c:
        {
            continue;
        }
        if (pos == 1 && path[0] == L'.')  // For paths like '.\'.
        {
            continue;
        }
        if (pos == 1 && path[0] == PATH_SEPARATOR_WCHAR)  // For paths like '\\..'.
        {
            // Skip machine and share names.
            pos = path.find(PATH_SEPARATOR_WCHAR, pos + 1);
            pos = path.find(PATH_SEPARATOR_WCHAR, pos + 1);
            continue;
        }
        amf_wstring temp_path = path.substr(0, pos);
#if 0   //  GK: We don't know whether _wmkdir calls CreateDirectory, which is supported in WinStore apps or CreateDirectoryEx, which is not
        int mkDirResult = _wmkdir(temp_path.c_str());   //  GK: need to check if creation was successful and return false in case of an error
        if (mkDirResult == -1 && errno != EEXIST)
        {
            result = false;
            break;
        }
#else
        BOOL mkDirResult = CreateDirectoryW(temp_path.c_str(), NULL);
        if (mkDirResult == FALSE && GetLastError() != ERROR_ALREADY_EXISTS)
        {
            result = false;
            break;
        }
#endif
        if(pos == amf_wstring::npos)
        {
            break;
        }

        if(pos == path.length() - 1)
        {
            break;
        }
    }
    return result;
}
Options::Options()
{
}
//-------------------------------------------------------------------------------------------------
Options::~Options()
{
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT Options::Reset()
{
    m_Storage.clear();
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
amf_wstring Options::GeneratePathForProcess()
{
    wchar_t buf[3000];
    ::GetModuleFileNameW(NULL, buf, amf_countof(buf));
    amf_wstring path;
    amf_wstring filename(buf);
    amf_wstring::size_type pos = filename.find_last_of(L"\\/");
    if(pos != amf_wstring::npos)
    {
        path = filename.substr(0, pos);
        filename = filename.substr(pos + 1);
    }

    wchar_t* envvar = _wgetenv(L"APPDATA");
    if(envvar != 0)
    {
        path = envvar;
        path += L"\\AMD\\";
        path += filename;
        path += L"\\";
    }
    else
    {
        path += filename;
        path += L".";
    }
    path += L"options.txt";
    return path;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT Options::LoadFromPath(const wchar_t *pFilePath)
{
    amf_wstring path;
    if(pFilePath == NULL)
    {
        path = GeneratePathForProcess();
    }
    else
    {
        path = pFilePath;
    }

    std::ifstream f;
    f.open(path.c_str(), std::ifstream::in);
    CHECK_RETURN(f.is_open(), AMF_INVALID_ARG, "Fail to open file " << path);

//    f.unsetf(std::ios_base::skipws);
//    f.setf(std::ios_base::skipws);
    amf_wstring section;
    while(!f.eof())
    { 
        amf_string str;
        std::getline(f,str);
//        f >> std::noskipws >> str;
//        f >> str;
        if(str.length() == 0)
        {
            continue;
        }
        amf_wstring strW = amf_from_utf8_to_unicode(str);
        if(strW[0] == L'[')
        {
            section = strW.substr(1, strW.length() - 2);
            continue;
        }
        if(section.length() == 0)
        {
            continue;
        }
        amf_wstring::size_type pos = strW.find(L"=");
        if(pos == amf_wstring::npos)
        {
            continue;
        }
        amf_wstring name = strW.substr(0, pos);
        amf_wstring value = strW.substr(pos + 1);

        m_Storage[section][name] = value;
    }

    f.close();
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT Options::StoreToPath(const wchar_t *pFilePath)
{
    // create directory
    amf_wstring pathFile;
    if(pFilePath == NULL)
    {
        pathFile = GeneratePathForProcess();
    }
    else
    {
        pathFile = pFilePath;
    }
    amf_wstring::size_type pos = pathFile.find_last_of(L"\\/");
    if(pos != amf_wstring::npos)
    {
        amf_wstring path = pathFile.substr(0, pos);
        amf_make_dir(path.c_str());
    }

    std::ofstream f;
    f.open(pathFile.c_str());
    CHECK_RETURN(f.is_open(), AMF_INVALID_ARG, "Fail to open file " << pathFile);

    for(Storage::iterator storageIter = m_Storage.begin(); storageIter != m_Storage.end(); storageIter++)
    {
        amf_wstring sectionW = L"[" + storageIter->first + L"]\n";
        amf_string  section = amf::amf_from_unicode_to_utf8(sectionW);
        f << section;
        for(Section::iterator sectionIter = storageIter->second.begin(); sectionIter != storageIter->second.end(); sectionIter++)
        {
            amf_wstring valueW = sectionIter->first + L"=" + sectionIter->second + L"\n";
            amf_string value = amf::amf_from_unicode_to_utf8(valueW);
            f << value;
        }

    }

    f.close();
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT Options::SetParameterWString(const wchar_t *section, const wchar_t *name, const wchar_t *value)
{
    CHECK_RETURN(section != NULL && name != NULL && value != NULL, AMF_INVALID_ARG, "Invalid parameter");
    amf_wstring sectionUpper = amf::amf_string_to_upper(section);
    amf_wstring nameUpper = amf::amf_string_to_upper(name);
    m_Storage[sectionUpper][nameUpper] = value;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT Options::SetParameterInt64(const wchar_t *section, const wchar_t *name, amf_int64  value)
{
    wchar_t buf[100];
    swprintf(buf, amf_countof(buf), L"%" LPRId64, value);
    return SetParameterWString(section, name, buf);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT Options::SetParameterDouble(const wchar_t *section, const wchar_t *name, double  value)
{
    wchar_t buf[100];
    swprintf(buf, amf_countof(buf), L"%f", value);
    return SetParameterWString(section, name, buf);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT Options::SetParameterStorage(const wchar_t *section, const ParametersStorage* value)
{
    CHECK_RETURN(section != NULL && value != NULL , AMF_INVALID_ARG, "Invalid parameter");
    amf_size count = value->GetParamCount();
    for(amf_size i = 0; i <count; i++)
    {
        amf::AMFVariant var;
        std::wstring name;
        value->GetParamAt(i, name, &var);
        SetParameterWString(section, name.c_str(), var.ToWString().c_str());
    }
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT Options::GetParameterWString(const wchar_t *section, const wchar_t *name, amf_wstring &value)
{
    CHECK_RETURN(section != NULL && name != NULL , AMF_INVALID_ARG, "Invalid parameter");
    amf_wstring sectionUpper = amf::amf_string_to_upper(section);
    amf_wstring nameUpper = amf::amf_string_to_upper(name);
    Storage::iterator foundSection = m_Storage.find(sectionUpper);
    if(foundSection == m_Storage.end())
    {
        return AMF_NOT_FOUND;
    }
    Section::iterator foundParameter = foundSection->second.find(nameUpper);
    if(foundParameter == foundSection->second.end())
    {
        return AMF_NOT_FOUND;
    }
    value = foundParameter->second;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT Options::GetParameterInt64(const wchar_t *section, const wchar_t *name, amf_int64  &value)
{
    amf_wstring valueStr;
    AMF_RESULT res = GetParameterWString(section, name, valueStr);
    if(res != AMF_OK)
    {
        return res;
    }
    amf_int64 paraValue = 0;
    amf::AMFVariant tmp(valueStr.c_str());
    value = amf_int64(tmp);
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT Options::GetParameterDouble(const wchar_t *section, const wchar_t *name, double  &value)
{
    amf_wstring valueStr;
    AMF_RESULT res = GetParameterWString(section, name, valueStr);
    if(res != AMF_OK)
    {
        return res;
    }
    amf_int64 paraValue = 0;
    amf::AMFVariant tmp(valueStr.c_str());
    value = amf_double(tmp);
    return AMF_OK;

}
//-------------------------------------------------------------------------------------------------
AMF_RESULT Options::GetParameterStorage(const wchar_t *section, ParametersStorage* value)
{
    CHECK_RETURN(section != NULL && value != NULL , AMF_INVALID_ARG, "Invalid parameter");
    amf_wstring sectionUpper = amf::amf_string_to_upper(section);
    Storage::iterator foundSection = m_Storage.find(sectionUpper);
    if(foundSection == m_Storage.end())
    {
        return AMF_NOT_FOUND;
    }
    for(Section::iterator sectionIter = foundSection->second.begin(); sectionIter != foundSection->second.end(); sectionIter++)
    {
        value->SetParamAsString(sectionIter->first.c_str(), sectionIter->second.c_str());
    }
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
