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
//
// Copyright (c) 2024 Advanced Micro Devices, Inc. All rights reserved.
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
// DVR.cpp : Defines the entry point for the application.
//

#include "public/common/AMFFactory.h"
#include "public/include/components/VideoEncoderVCE.h"
#include "public/include/components/VideoEncoderAV1.h"
#include "public/include/core/Debug.h"

#include "public/samples/CPPSamples/common/CmdLineParser.h"
#include "public/samples/CPPSamples/common/CmdLogger.h"
#include "public/samples/CPPSamples/common/ParametersStorage.h"
#include "public/samples/CPPSamples/common/DisplayDvrPipeline.h"
#include "public/samples/CPPSamples/common/CmdLineParser.h"
#include "public/samples/CPPSamples/common/CAmfInit.h"

#include <iostream>

std::string GetDefaultFilepath()
{
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

    char* pFilepath = nullptr;
    int result = asprintf(&pFilepath, "DVRRecording-%d-%02d-%02d-%02d-%02d-%02d.mp4",
        tm.tm_year + 1900,
        tm.tm_mon + 1,
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec);
    if (result < 0) {
        LOG_ERROR(L"Couldn't asprintf name for file!");
        return "DVRRecording.mp4";
    }
    std::string path(pFilepath);
    free(pFilepath);
    return path;
}

bool QueryUser(std::string& input, const std::string& caret, const std::string& query, const std::string& defaultValue = "")
{
    std::cout << query;
    if (defaultValue != "")
    {
        std::cout << " (press enter for the default: " << defaultValue << ")";
    }
    std::cout << std::endl;
    std::cout << caret;
    std::cout << " ";
    input = "";
    if (!std::getline(std::cin, input))
    {
        return false;
    }
    if (input == "")
    {
        input = defaultValue;
    }
    return true;
}

void LogPipelineError(DisplayDvrPipeline& pipeline)
{
    amf_string error = amf::amf_from_unicode_to_utf8(amf_wstring(pipeline.GetErrorMsg()));
    std::cout << "ERROR: " << error << std::endl;
}

class Command {
public:
    Command(char code, const std::string& help)
        : m_code(code)
        , m_help(help)
    {}

    void SetAvailable(bool available)
    {
        m_available = available;
    }

    bool GetAvailable() const
    {
        return m_available;
    }

    std::string GetHelp() const
    {
        return std::string(1, m_code) + " - " + m_help;
    }

    bool CheckCode(const std::string& input) const
    {
        return (m_available == true) && (input.length() == 1) && (input[0] == m_code);
    }
private:
    char m_code;
    std::string m_help;
    bool m_available = true;
};

void SetRecordStopAvailable(Command& record, Command& stop, bool isRecording)
{
    record.SetAvailable(!isRecording);
    stop.SetAvailable(isRecording);
}

int main(int argc, char** argv)
{
    CAmfInit  amfInit;
    AMF_RESULT res = amfInit.Init();
    if (res != AMF_OK)
    {
        LOG_ERROR(L"AMF failed to initialize");
        return 1;
    }
    g_AMFFactory.GetDebug()->AssertsEnable(false);
    g_AMFFactory.GetTrace()->SetWriterLevel(AMF_TRACE_WRITER_DEBUG_OUTPUT, AMF_TRACE_ERROR);

    DisplayDvrPipeline pipeline;

    std::wstring codec = L"AMFVideoEncoderVCE_AVC";
    pipeline.SetParam(DisplayDvrPipeline::PARAM_NAME_CODEC, AMFVideoEncoderVCE_AVC);
    RegisterEncoderParamsAVC(&pipeline);

    if (!parseCmdLineParameters(&pipeline, argc, argv)) {
        return -1;
    }

    std::string filepath;
    bool resp = QueryUser(filepath, ">", "Enter the filename for recording", GetDefaultFilepath());
    if (!resp)
    {
        return 0;
    }
    amf_wstring wfilepath = amf::amf_from_utf8_to_unicode(amf_string(filepath.c_str()));
    pipeline.SetParam(DisplayDvrPipeline::PARAM_NAME_OUTPUT, wfilepath.c_str());

    //todo: populate monitor ids from randr
    pipeline.SetMonitorIDs({0});

    bool isRecording = false;
    Command recordingCommand('r', "Start recording");
    Command stopCommand('s', "Stop Recording");
    Command quitCommand('q', "Quit");
    Command helpCommand('h', "Show help");

    // query user until CTRL+D or force quit
    std::string input;
    while (QueryUser(input, isRecording ? "[recording] >" : ">", "Enter a command (\"h\" for help)"))
    {
        SetRecordStopAvailable(recordingCommand, stopCommand, isRecording);

        if (recordingCommand.CheckCode(input))
        {
            res = pipeline.Init();
            if (res != AMF_OK)
            {
                LogPipelineError(pipeline);
                pipeline.Terminate();
                continue;
            }
            res = pipeline.Start();
            if (res != AMF_OK)
            {
                LogPipelineError(pipeline);
                pipeline.Terminate();
                continue;
            }
            std::cout << "Success! Now recording..." << std::endl;
            isRecording = true;
        }
        else if (stopCommand.CheckCode(input))
        {
            res = pipeline.Stop();
            if (res != AMF_OK)
            {
                LogPipelineError(pipeline);
                continue;
            }
            std::cout << "Recording stopped." << std::endl;
            isRecording = false;
        }
        else if (quitCommand.CheckCode(input))
        {
            break;
        }
        else if (helpCommand.CheckCode(input))
        {
            std::cout << recordingCommand.GetHelp() << std::endl;
            std::cout << stopCommand.GetHelp() << std::endl;
            std::cout << quitCommand.GetHelp() << std::endl;
            std::cout << helpCommand.GetHelp() << std::endl;
        }
        else
        {
            std::cout << "Command not recognized or not valid." << std::endl;
        }
    }

    pipeline.Stop();
    pipeline.Terminate();

    return 0;
}