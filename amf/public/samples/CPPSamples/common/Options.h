#include "ParametersStorage.h"
#include "public/common/AMFSTL.h"
class Options
{
public:
    Options();
    ~Options();

    AMF_RESULT Reset();

    amf_wstring GeneratePathForProcess();
    AMF_RESULT LoadFromPath(const wchar_t *pFilePath);
    AMF_RESULT StoreToPath(const wchar_t *pFilePath);

    AMF_RESULT SetParameterWString(const wchar_t *section, const wchar_t *name, const wchar_t *value);
    AMF_RESULT SetParameterInt64(const wchar_t *section, const wchar_t *name, amf_int64  value);
    AMF_RESULT SetParameterDouble(const wchar_t *section, const wchar_t *name, double  value);
    AMF_RESULT SetParameterStorage(const wchar_t *section, const ParametersStorage* value);

    AMF_RESULT GetParameterWString(const wchar_t *section, const wchar_t *name, amf_wstring &value);
    AMF_RESULT GetParameterInt64(const wchar_t *section, const wchar_t *name, amf_int64  &value);
    AMF_RESULT GetParameterDouble(const wchar_t *section, const wchar_t *name, double  &value);
    AMF_RESULT GetParameterStorage(const wchar_t *section, ParametersStorage* value);

protected:
    typedef std::map<amf_wstring, amf_wstring> Section;
    typedef std::map<amf_wstring, Section> Storage;
    Storage     m_Storage;
};