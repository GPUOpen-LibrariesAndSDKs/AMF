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
//#include "stdafx.h"
#include "CmdLogger.h"
#include <iomanip>
#include <iostream>

#ifdef _WIN32

static WORD defaultAttributes = WORD(-1);
void ChangeTextColor(AMFLogLevel level) {
#if !defined(METRO_APP)
  HANDLE hCmd = GetStdHandle(STD_OUTPUT_HANDLE);

  if (defaultAttributes == WORD (- 1)) {
    CONSOLE_SCREEN_BUFFER_INFO info = {};
    GetConsoleScreenBufferInfo(hCmd, &info);
    defaultAttributes = info.wAttributes;
  }

  switch (level) {
    case AMFLogLevelDefault:
      SetConsoleTextAttribute(hCmd, defaultAttributes);
      break;
    case AMFLogLevelInfo:
      SetConsoleTextAttribute(hCmd, FOREGROUND_INTENSITY);
      break;
    case AMFLogLevelSuccess:
      SetConsoleTextAttribute(hCmd, FOREGROUND_GREEN);
      break;
    case AMFLogLevelError:
      //        SetConsoleTextAttribute(hCmd, FOREGROUND_RED);
      SetConsoleTextAttribute(
          hCmd, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
      break;
  }
#endif
}
#elif defined(__linux)
void ChangeTextColor(AMFLogLevel level) {
#define COL_RESET L"\033[0m"
  /*
  \033[22;30m - black
  \033[22;31m - red
  \033[22;32m - green
  \033[22;33m - brown
  \033[22;34m - blue
  \033[22;35m - magenta
  \033[22;36m - cyan
  \033[22;37m - gray
  \033[01;30m - dark gray
  \033[01;31m - light red
  \033[01;32m - light green
  \033[01;33m - yellow
  \033[01;34m - light blue
  \033[01;35m - light magenta
  \033[01;36m - light cyan
  \033[01;37m - white
  */
  switch (level) {
    case AMFLogLevelInfo:
      wprintf(COL_RESET);  // default
      break;
    case AMFLogLevelSuccess:
      wprintf(L"\033[22;32m");  // green
      break;
    case AMFLogLevelError:
      wprintf(L"\033[01;36m");  // light cyan
      break;
    default:
      break;
  }
}
#else
void ChangeTextColor(AMFLogLevel level) {}
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#pragma clang diagnostic ignored "-Wglobal-constructors"
#endif

amf::AMFCriticalSection s_std_out_cs;

#ifdef __clang__
#pragma clang diagnostic pop
#endif

void WriteLog(const wchar_t* message, AMFLogLevel level) {
#if 0
    std::wstringstream messageStream;

    SYSTEMTIME st;
    GetSystemTime(&st);
    messageStream << std::setfill(L'0') 
        << std::setw(2) << st.wHour 
        << L":" << std::setw(2) << st.wMinute 
        << L":" << std::setw(2) << st.wSecond 
        << L"." << std::setw(3) << st.wMilliseconds 
        << L" - " 
        << message;
    AMFLock lock(&s_std_out_cs);
    ChangeTextColor(type);
    wprintf(messageStream.str().c_str());
    ChangeTextColor(AMFLogLevelInfo);
#else

  amf::AMFLock lock(&s_std_out_cs);
  ChangeTextColor(level);
  wprintf(message);
  ChangeTextColor(AMFLogLevelDefault);
#endif
}
