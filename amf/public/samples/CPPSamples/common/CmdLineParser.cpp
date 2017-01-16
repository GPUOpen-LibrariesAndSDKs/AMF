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
#include "CmdLineParser.h"
#include "public/common/Thread.h"
#include <iterator>
#include <functional>
#include <algorithm>
#include <iostream>
#include <cctype>
#include <vector>


typedef std::pair<std::wstring, std::wstring> CmdArg;
typedef std::list<CmdArg> CmdArgs;


bool isParamName(const std::wstring& str)
{
    return (str.size() > 1) && str.front() == '-';
}

bool parseCmdLine(CmdArgs* cmdArgs)
{
    bool ret = true;
    LPWSTR *szArglist;
    int nArgs;
    szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    if( NULL == szArglist)
    {
        ret = false;
    }
    else
    {
        for(int i = 1; i < nArgs; i++)
        {
            std::wstring name = toUpper(szArglist[i]);
            std::wstring value;
            if (!isParamName(name))
            {
                LOG_ERROR(L"Invalid command line parameter name: " << L"\"" << name << L"\"");
                return false;
            }
            if (i + 1 < nArgs && !isParamName(szArglist[i+1]))
            {
                i++;
                value = szArglist[i];
            }
            cmdArgs->push_back( CmdArg(name.substr(1), value));
        }
    }
    LocalFree(szArglist);
    return ret;
}


template<class T> bool FromString(const std::wstring& str, T& value)
{
    std::wstringstream stream;
    stream << str;
    stream >> value;
    if (!stream || !stream.eof())
    {
        LOG_ERROR(L"String \"" << str << L"\" is invalid.");
        return false;
    }
    return true;
}

bool parseCmdLineParameters(ParametersStorage* pParams)
{
    CmdArgs cmdArgs;
    if (parseCmdLine(&cmdArgs) && cmdArgs.size())
    {
        for (CmdArgs::iterator it = cmdArgs.begin(); it != cmdArgs.end(); it++)
        {
            const std::wstring& name = it->first;
            if(name == L"help" || name == L"?")
            {
                LOG_INFO(pParams->GetParamUsage());
            }
            else
            {
                pParams->SetParamAsString(name, it->second); // may be need process return value
            }
        }
        return true;
    }
    return false;
}