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
// this sample decodes H.264 elmentary stream to NV12 frames using AMF Decoder and writes the frames into raw file
#include <stdio.h>
#ifdef _WIN32
#include <tchar.h>
#include <d3d9.h>
#include <d3d11.h>
#endif
#include "public/common/AMFFactory.h"
#include "public/common/AMFSTL.h"
#include "public/common/TraceAdapter.h"
#include "public/include/components/VideoDecoderUVD.h"
#include "public/common/DataStream.h"
#include "../common/BitStreamParser.h"
#include "public/samples/CPPSamples/common/MiscHelpers.h"
#include "public/samples/CPPSamples/common/SurfaceUtils.h"
#include "public/samples/CPPSamples/common/PollingThread.h"
#include <fstream>
#include <iostream>

#define AMF_FACILITY L"SimpleDecoder"

//static const wchar_t *fileNameIn          = L"./nasa_720p.264";
//static const wchar_t *fileNameIn          = L"./bbc_1080p.264";
static const wchar_t *fileNameIn            = NULL;
static const wchar_t *fileNameOut           = L"./output_%dx%d.nv12";
#if defined (_WIN32)
static amf::AMF_MEMORY_TYPE memoryTypeOut   = amf::AMF_MEMORY_DX11;
#elif defined (__linux)
static amf::AMF_MEMORY_TYPE memoryTypeOut   = amf::AMF_MEMORY_VULKAN;
#endif
static amf::AMF_SURFACE_FORMAT formatOut    = amf::AMF_SURFACE_NV12;
static amf_int32 frameCount                 = 500; // -1 means entire file
static amf_int32 submitted = 0;

// The memory transfer from DX9 to HOST and writing a raw file is longer than decode time. To measure decode time correctly disable convert and write here:
//static bool bWriteToFile = false;
static bool bWriteToFile = true;

class DecPollingThread : public PollingThread
{
public:
    DecPollingThread(amf::AMFContext* pContext, amf::AMFComponent* pDecoder, const wchar_t* pFileName);
protected:
    virtual bool  Init() override;
    void ProcessData(amf::AMFData* pData) override;
    void PrintResults() override;

    amf_pts m_ConvertDuration;
};


#ifdef _WIN32
int _tmain(int argc, _TCHAR* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    amf_wstring fileNameInW{};

    if(argc <= 1 && fileNameIn == NULL)
    {
        wprintf(L"input file name is missing in command line");
        return 1;
    }
    if(argc > 1)
    {
#if defined(_WIN32) && defined(_UNICODE)
        fileNameInW = argv[1];
#else
        fileNameInW = amf::amf_from_utf8_to_unicode(argv[1]);
#endif
        fileNameIn = fileNameInW.c_str();
    }
    AMF_RESULT              res = AMF_OK; // error checking can be added later
    res = g_AMFFactory.Init();
    if(res != AMF_OK)
    {
        wprintf(L"AMF Failed to initialize");
        return 1;
    }
    g_AMFFactory.GetTrace()->SetWriterLevel(AMF_TRACE_WRITER_DEBUG_OUTPUT, AMF_TRACE_TRACE);
    ::amf_increase_timer_precision();

    amf::AMFContextPtr      context;
    amf::AMFComponentPtr    decoder;
    amf::AMFDataStreamPtr   datastream;
    BitStreamParserPtr      parser;

    // initialize AMF
    res = amf::AMFDataStream::OpenDataStream(fileNameIn, amf::AMFSO_READ, amf::AMFFS_SHARE_READ, &datastream);

    if(datastream == NULL)
    {
        wprintf(L"file %s is missing", fileNameIn);
        return 1;
    }
    // context
    res = g_AMFFactory.GetFactory()->CreateContext(&context);
    switch (memoryTypeOut)
    {
    case amf::AMF_MEMORY_DX9:
        res = context->InitDX9(NULL); // can be DX9 or DX9Ex device
        break;
    case amf::AMF_MEMORY_DX11:
        res = context->InitDX11(NULL); // can be DX11 device
        break;
    case amf::AMF_MEMORY_DX12:
        {

        amf::AMFContext2Ptr context2(context);
        if(context2 == nullptr)
        {
            wprintf(L"amf::AMFContext2 is missing");
            return 1;
        }
        context2->InitDX12(NULL); // can be DX11 device
        }
        break;
    case amf::AMF_MEMORY_VULKAN:
        res = amf::AMFContext1Ptr(context)->InitVulkan(NULL); // can be Vulkan device
        break;
    default:
        break;
    }

	BitStreamType bsType = GetStreamType(fileNameIn);
    // H264/H265/AV1 IVF elementary stream parser from samples common
	parser = BitStreamParser::Create(datastream, bsType, context);

    // open output file with frame size in file name
    wchar_t fileNameOutWidthSize[2000];
    swprintf(fileNameOutWidthSize, amf_countof(fileNameOutWidthSize), fileNameOut, parser->GetPictureWidth(), parser->GetPictureHeight());

	// component: decoder
	if (bsType == BitStreamH264AnnexB)
		res = g_AMFFactory.GetFactory()->CreateComponent(context, AMFVideoDecoderUVD_H264_AVC, &decoder);
	else if (bsType == BitStream265AnnexB)
		res = g_AMFFactory.GetFactory()->CreateComponent(context, AMFVideoDecoderHW_H265_HEVC, &decoder);
    else if (bsType == BitStreamIVF)
        res = g_AMFFactory.GetFactory()->CreateComponent(context, AMFVideoDecoderHW_AV1, &decoder);

    res = decoder->SetProperty(AMF_TIMESTAMP_MODE, amf_int64(AMF_TS_DECODE)); // our sample H264 parser provides decode order timestamps - change this depend on demuxer

    if (parser->GetExtraDataSize())
    { // set SPS/PPS extracted from stream or container; Alternatively can use parser->SetUseStartCodes(true)
        amf::AMFBufferPtr buffer;
        context->AllocBuffer(amf::AMF_MEMORY_HOST, parser->GetExtraDataSize(), &buffer);

        memcpy(buffer->GetNative(), parser->GetExtraData(), parser->GetExtraDataSize());
        decoder->SetProperty(AMF_VIDEO_DECODER_EXTRADATA, amf::AMFVariant(buffer));
    }
    res = decoder->Init(formatOut, parser->GetPictureWidth(), parser->GetPictureHeight());

    DecPollingThread thread(context, decoder, fileNameOutWidthSize);
    thread.Start();

    amf::AMFDataPtr data;
    bool bNeedNewInput = true;
    //amf_int32 submitted = 0;

    while(submitted < frameCount || frameCount < 0)
    {
        if(bNeedNewInput)
        {
            data = NULL;
            res = parser->QueryOutput(&data); // read compressed frame into buffer
            if(res == AMF_EOF || data == NULL)
            {
                break;// end of file
            }
        }
        amf_pts start_time = amf_high_precision_clock();
        data->SetProperty(START_TIME_PROPERTY, start_time);

        if (res == AMF_REPEAT)
        { // current buffer contains more than one frame, process the remaining data before submitting new data
            res = decoder->SubmitInput(NULL);
        }
        else
        {
            res = decoder->SubmitInput(data);
        }

        if(res == AMF_NEED_MORE_INPUT)
        {
			// do nothing
        }
        else if(res == AMF_INPUT_FULL || res == AMF_DECODER_NO_FREE_SURFACES || res == AMF_REPEAT)
        { // queue is full; sleep, try to get ready surfaces  in polling thread and repeat submission
            bNeedNewInput = false;
            amf_sleep(1);
        }
        else
        { // submission succeeded. read new buffer from parser
			submitted++;
			bNeedNewInput = true;
        }
    }
    // drain decoder queue
    res = decoder->Drain();
    thread.RequestStop();
    thread.WaitForStop();

    switch (memoryTypeOut)
    {
    case amf::AMF_MEMORY_DX12:
    {
        amf::AMFComputePtr pCompute;
        context->GetCompute(amf::AMF_MEMORY_DX12, &pCompute);
        pCompute->FinishQueue();
        //        outputSurface->Convert(amf::AMF_MEMORY_HOST);
    }
    default:
        break;
    }

    // cleanup in this order
    data = NULL;
    decoder->Terminate();
    decoder = NULL;
    parser = NULL;
    datastream = NULL;
    context->Terminate();
    context = NULL; // context is the last

    g_AMFFactory.Terminate();
	return 0;
}

DecPollingThread::DecPollingThread(amf::AMFContext* pContext, amf::AMFComponent* pDecoder, const wchar_t* pFileName)
    : PollingThread(pContext, pDecoder, pFileName, bWriteToFile), m_ConvertDuration(0)
{}

bool DecPollingThread::Init()
{
    PollingThread::Init();
    m_ConvertDuration = 0;
    return true;
}

void DecPollingThread::ProcessData(amf::AMFData* pData)
{
    AMF_RESULT res = AMF_OK;

    SyncSurfaceToCPU(m_pContext, amf::AMFSurfacePtr(pData)); // Waits till decoder finishes decode the surface. Need for accurate profiling only. Do not use in the product!!!
    AdjustTimes(pData);

    if (bWriteToFile == true)
    {
        // this operation is slow nneed to remove it from stat
        res = pData->Convert(amf::AMF_MEMORY_HOST); // convert to system memory

        amf_pts convert_time = amf_high_precision_clock();
        m_ConvertDuration += convert_time - m_LastPollTime;

        amf::AMFSurfacePtr pSurface(pData); // query for surface interface

        res = WritePlane(pSurface->GetPlane(amf::AMF_PLANE_Y), m_pFile); // get y-plane pixels
        res = WritePlane(pSurface->GetPlane(amf::AMF_PLANE_UV), m_pFile); // get uv-plane pixels

        m_WriteDuration += amf_high_precision_clock() - convert_time;
    }
}

void DecPollingThread::PrintResults()
{
    PrintTimes("decode ", submitted);
    printf("convert per frame = %.4fms\n", double(m_ConvertDuration) / AMF_MILLISECOND / submitted);
}
