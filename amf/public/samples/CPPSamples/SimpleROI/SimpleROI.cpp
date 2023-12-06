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
// This sample demostrate how to use ROI feature. It decodes H.264 or H.265 elmentary stream to  
// NV12 frames using AMF Decoder, then encodes the frames into H.264, H.265, or AV1 stream based on ROI.

#include <stdio.h>
#ifdef _WIN32
#include <tchar.h>
#include <d3d9.h>
#include <d3d11.h>
#endif
#include "public/common/AMFFactory.h"
#include "public/include/components/VideoDecoderUVD.h"
#include "public/common/DataStream.h"
#include "public/common/TraceAdapter.h"
#include "public/common/AMFSTL.h"
#include "public/include/components/VideoEncoderVCE.h"
#include "public/include/components/VideoEncoderHEVC.h"
#include "public/include/components/VideoEncoderAV1.h"
#include "../common/BitStreamParser.h"
#include "public/samples/CPPSamples/common/MiscHelpers.h"
#include "public/samples/CPPSamples/common/SurfaceUtils.h"
#include "public/samples/CPPSamples/common/PollingThread.h"
#include <fstream>
#include <iostream>

#define AMF_FACILITY L"SimpleROI"


#if defined (_WIN32)
static amf::AMF_MEMORY_TYPE memoryType = amf::AMF_MEMORY_DX11;
#elif defined (__linux)
static amf::AMF_MEMORY_TYPE memoryType = amf::AMF_MEMORY_VULKAN;
#endif
static amf::AMF_SURFACE_FORMAT pixelFormat = amf::AMF_SURFACE_NV12;

static amf_int32 frameCount                 = 100; // -1 means entire file
static amf_int32 widthIn					= 0;
static amf_int32 heightIn					= 0;
static amf_int32 frameRateIn				= 0;
static amf_int32 submitted					= 0;

static const wchar_t *pOutputCodec;
static amf_int64 bitRateOut					= 5000000L; // in bits, 5MBit
static bool bMaximumSpeed					= true;


static const wchar_t *fileNameDecOut        = L"./output_%dx%d.nv12";
static const wchar_t *fileNameEncOut        = L"./output_%dx%d.264";

// The memory transfer from DX9 to HOST and writing a raw file is longer than decode time. To measure decode time correctly disable convert and write here: 
static bool bWriteDecOutToFile = false;

class DecPollingThread : public PollingThread
{
public:
    DecPollingThread(amf::AMFContext* pContext, amf::AMFComponent* pEncoder, amf::AMFComponent* pDecoder, const wchar_t* pFileName);
protected:
    void ProcessData(amf::AMFData* pData) override;
    virtual bool Terminate() override;

    amf::AMFComponentPtr m_pEncoder;
};

class EncPollingThread : public PollingThread
{
public:
    EncPollingThread(amf::AMFContext* pContext, amf::AMFComponent* pEncoder, const wchar_t* pFileName);
protected:
    void ProcessData(amf::AMFData* pData) override;
};


#ifdef _WIN32
int _tmain(int argc, _TCHAR* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    amf_wstring  fileNameIn;

    switch (argc) 
    {
        case 2: {
#if defined(_WIN32) && defined(_UNICODE)
            fileNameIn = argv[1];
#else
            fileNameIn = amf::amf_from_utf8_to_unicode(argv[1]);
#endif
            pOutputCodec = AMFVideoEncoderVCE_AVC; // default to output AVC
            break; }
        case 3: {
            amf_wstring param;
#if defined(_WIN32) && defined(_UNICODE)
            fileNameIn = argv[1];
            param = argv[2];
#else
            fileNameIn = amf::amf_from_utf8_to_unicode(argv[1]);
            param = amf::amf_from_utf8_to_unicode(argv[2]);
#endif
            if ((param.compare(L"h264") == 0) || (param.compare(L"avc") == 0))
            {
                pOutputCodec = AMFVideoEncoderVCE_AVC;
            }
            else if ((param.compare(L"h265") == 0) || (param.compare(L"hevc") == 0))
            {
                pOutputCodec = AMFVideoEncoder_HEVC;
                fileNameEncOut = L"./output_%dx%d.265";
            }
            else if ((param.compare(L"av1") == 0))
            {
                pOutputCodec = AMFVideoEncoder_AV1;
                fileNameEncOut = L"./output_%dx%d.av1";
            }
            else
            {
                wprintf(L"unsupported output format!");
                return 1;
            }
            break; }
        default:
            wprintf(L"Usage: %s inputFileName [outputCodecType]\n", argv[0]);
            return 1;
    }

    AMF_RESULT res = g_AMFFactory.Init();
    if(res != AMF_OK)
    {
        wprintf(L"AMF Failed to initialize");
        return 1;
    }
    g_AMFFactory.GetTrace()->SetWriterLevel(AMF_TRACE_WRITER_DEBUG_OUTPUT, AMF_TRACE_TRACE); 
    ::amf_increase_timer_precision();

    amf::AMFContextPtr      context;
    amf::AMFComponentPtr    encoder;
    amf::AMFComponentPtr    decoder;
    amf::AMFDataStreamPtr   datastream;
    BitStreamParserPtr      parser;

    // initialize AMF
    res = amf::AMFDataStream::OpenDataStream(fileNameIn.c_str(), amf::AMFSO_READ, amf::AMFFS_SHARE_READ, &datastream);

    if(datastream == NULL)
    {
        wprintf(L"file %s is missing", fileNameIn.c_str());
        return 1;
    }
    // context
    res = g_AMFFactory.GetFactory()->CreateContext(&context);
    switch(memoryType)
    {
    case amf::AMF_MEMORY_DX9:
        res = context->InitDX9(NULL); // can be DX9 or DX9Ex device
        break;
    case amf::AMF_MEMORY_DX11:
        res = context->InitDX11(NULL); // can be DX11 device
        break;
    case amf::AMF_MEMORY_VULKAN:
        res = amf::AMFContext1Ptr(context)->InitVulkan(NULL); // can be Vulkan device
        break;
    default:
        break;
    }

	BitStreamType bsType = GetStreamType(fileNameIn.c_str());
	// H264/H265 elementary stream parser from samples common 
	parser = BitStreamParser::Create(datastream, bsType, context);

    // open output file with frame size in file name
    wchar_t fileNameDecOutWithSize[2000];
    swprintf(fileNameDecOutWithSize, amf_countof(fileNameDecOutWithSize), fileNameDecOut, parser->GetPictureWidth(), parser->GetPictureHeight());

	// component: decoder
	if (bsType == BitStreamH264AnnexB)
		res = g_AMFFactory.GetFactory()->CreateComponent(context, AMFVideoDecoderUVD_H264_AVC, &decoder);
	else if (bsType == BitStream265AnnexB)
		res = g_AMFFactory.GetFactory()->CreateComponent(context, AMFVideoDecoderHW_H265_HEVC, &decoder);

    res = decoder->SetProperty(AMF_TIMESTAMP_MODE, amf_int64(AMF_TS_DECODE)); // our sample H264 parser provides decode order timestamps - change this depend on demuxer

    if (parser->GetExtraDataSize()) 
    { // set SPS/PPS extracted from stream or container; Alternatively can use parser->SetUseStartCodes(true)
        amf::AMFBufferPtr buffer;
        context->AllocBuffer(amf::AMF_MEMORY_HOST, parser->GetExtraDataSize(), &buffer);

        memcpy(buffer->GetNative(), parser->GetExtraData(), parser->GetExtraDataSize());
        decoder->SetProperty(AMF_VIDEO_DECODER_EXTRADATA, amf::AMFVariant(buffer));
    }
    res = decoder->Init(pixelFormat, parser->GetPictureWidth(), parser->GetPictureHeight());


	// component: encoder
	res = g_AMFFactory.GetFactory()->CreateComponent(context, pOutputCodec, &encoder);
	AMF_RETURN_IF_FAILED(res, L"CreateComponent(%s) failed", pOutputCodec);

	widthIn = parser->GetPictureWidth();
	heightIn = parser->GetPictureHeight();
	frameRateIn = (amf_int32)parser->GetFrameRate();
	wchar_t fileNameEncOutWithSize[2000];

    swprintf(fileNameEncOutWithSize, amf_countof(fileNameEncOutWithSize), fileNameEncOut, parser->GetPictureWidth(), parser->GetPictureHeight());

    // set encoder params
	if (amf_wstring(pOutputCodec) == amf_wstring(AMFVideoEncoderVCE_AVC))
	{
        res = SetEncoderDefaultsAVC(encoder, bitRateOut, frameRateIn, bMaximumSpeed, widthIn > 1920 && heightIn > 1088, false);
        AMF_RETURN_IF_FAILED(res, L"SetEncoderDefaultsAVC() failed");
	}
	else if (amf_wstring(pOutputCodec) == amf_wstring(AMFVideoEncoder_HEVC))
	{
        res = SetEncoderDefaultsHEVC(encoder, bitRateOut, frameRateIn, false, bMaximumSpeed, widthIn > 1920 && heightIn > 1088, false);
        AMF_RETURN_IF_FAILED(res, L"SetEncoderDefaultsHEVC() failed");
	}
    else if (amf_wstring(pOutputCodec) == amf_wstring(AMFVideoEncoder_AV1))
    {
        res = SetEncoderDefaultsAV1(encoder, bitRateOut, frameRateIn, false, bMaximumSpeed, widthIn > 1920 && heightIn > 1088, false);
        AMF_RETURN_IF_FAILED(res, L"SetEncoderDefaultsAV1() failed");
    }
	res = encoder->Init(pixelFormat, widthIn, heightIn);
	AMF_RETURN_IF_FAILED(res, L"encoder->Init() failed");

	DecPollingThread threadDec(context, encoder, decoder, fileNameDecOutWithSize);
	threadDec.Start();

	EncPollingThread threadEnc(context, encoder, fileNameEncOutWithSize);
	threadEnc.Start();

    amf::AMFDataPtr data;
    bool bNeedNewInput = true;

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

        res = decoder->SubmitInput(data);
        if(res == AMF_NEED_MORE_INPUT)
        {
			// do nothing
        }
        else if(res == AMF_INPUT_FULL || res == AMF_DECODER_NO_FREE_SURFACES)
        { // queue is full; sleep, try to get ready surfaces  in polling thread and repeat submission
            bNeedNewInput = false;
            amf_sleep(1); 
        }
        else
        { // submission succeeded. read new buffer from parser
			submitted++;
			bNeedNewInput = true;
			printf("\rFrame submitted: %d", submitted);
			fflush(stdout);
        }
    }
    // drain decoder queue 
    res = decoder->Drain();

    // Need to request stop before waiting for stop
    if (threadDec.RequestStop() == false)
    {
        AMFTraceError(AMF_FACILITY, L"threadDec.RequestStop() Failed");
    }

    if (threadDec.WaitForStop() == false)
    {
        AMFTraceError(AMF_FACILITY, L"threadDec.WaitForStop() Failed");
    }

	// drain encoder; input queue can be full
	while (true)
	{
		res = encoder->Drain();
		if (res != AMF_INPUT_FULL) // handle full queue
		{
			break;
		}
		amf_sleep(1); // input queue is full: wait and try again
	}

    // Need to request stop before waiting for stop
    if (threadEnc.RequestStop() == false)
    {
        AMFTraceError(AMF_FACILITY, L"threadEnc.RequestStop() Failed");
    }

    if (threadEnc.WaitForStop() == false)
    {
        AMFTraceError(AMF_FACILITY, L"threadEnc.WaitForStop() Failed");
    }

    // cleanup in this order
    data = NULL;
    decoder->Terminate();
    decoder = NULL;
	encoder->Terminate();
	encoder = NULL;
    parser = NULL;
    datastream = NULL;
    context->Terminate();
    context = NULL; // context is the last

    g_AMFFactory.Terminate();
	return 0;
}

DecPollingThread::DecPollingThread(amf::AMFContext *pContext, amf::AMFComponent *pEncoder, amf::AMFComponent *pDecoder, const wchar_t *pFileName) 
    : PollingThread(pContext, pDecoder, pFileName, bWriteDecOutToFile), m_pEncoder(pEncoder)
{}

void DecPollingThread::ProcessData(amf::AMFData* pData)
{
    AMF_RESULT res = AMF_OK;
    SyncSurfaceToCPU(m_pContext, amf::AMFSurfacePtr(pData)); // Waits till decoder finishes decode the surface. Need for accurate profiling only. Do not use in the product!!!

    //ROI map size calculations
    amf::AMF_SURFACE_FORMAT ROIMapformat = amf::AMF_SURFACE_GRAY32;
    amf_int32 num_blocks_x;
    amf_int32 num_blocks_y;

    if (amf_wstring(pOutputCodec) == amf_wstring(AMFVideoEncoderVCE_AVC))
    {
        num_blocks_x = (widthIn + 15) / 16;
        num_blocks_y = (heightIn + 15) / 16;
    }
    else
    {
        num_blocks_x = (widthIn + 63) / 64;
        num_blocks_y = (heightIn + 63) / 64;
    }

    //Allocate ROI map surface
    amf::AMFSurfacePtr pROIMapSurface;
    amf::AMFContext1Ptr spContext1(m_pContext);
    res = spContext1->AllocSurfaceEx(amf::AMF_MEMORY_HOST, ROIMapformat, num_blocks_x, num_blocks_y,
        static_cast<amf::AMF_SURFACE_USAGE>(amf::AMF_SURFACE_USAGE_DEFAULT | amf::AMF_SURFACE_USAGE_LINEAR),
        static_cast<amf::AMF_MEMORY_CPU_ACCESS>(amf::AMF_MEMORY_CPU_DEFAULT), &pROIMapSurface);

    if (res != AMF_OK)
    {
        printf("AMFContext::AllocSurface(amf::AMF_MEMORY_HOST) for ROI map failed!\n");
    }

    amf_uint32* pBuf = (amf_uint32 *)pROIMapSurface->GetPlaneAt(0)->GetNative();
    amf_int32 pitch = pROIMapSurface->GetPlaneAt(0)->GetHPitch();
    memset((void *)pBuf, 0, pitch * num_blocks_y);

    for (int y = num_blocks_y / 4; y < num_blocks_y * 3 / 4; y++)
    {
        for (int x = num_blocks_x / 4; x < num_blocks_x * 3 / 4; x++)
        {
            pBuf[x + y * pitch / 4] = 10;
        }
    }

    amf::AMFSurfacePtr pSurface(pData); // query for surface interface
    if (amf_wstring(pOutputCodec) == amf_wstring(AMFVideoEncoderVCE_AVC))
    {
        pSurface->SetProperty(AMF_VIDEO_ENCODER_ROI_DATA, pROIMapSurface.Detach());
    }
    else if (amf_wstring(pOutputCodec) == amf_wstring(AMFVideoEncoder_HEVC))
    {
        pSurface->SetProperty(AMF_VIDEO_ENCODER_HEVC_ROI_DATA, pROIMapSurface.Detach());
    }
    else if (amf_wstring(pOutputCodec) == amf_wstring(AMFVideoEncoder_AV1))
    {
        pSurface->SetProperty(AMF_VIDEO_ENCODER_AV1_ROI_DATA, pROIMapSurface.Detach());
    }

    m_pEncoder->SubmitInput(pSurface);

    if(bWriteDecOutToFile == true)
    {
        // this operation is slow need to remove it from stat
        res = pData->Convert(amf::AMF_MEMORY_HOST); // convert to system memory

        res = WritePlane(pSurface->GetPlane(amf::AMF_PLANE_Y), m_pFile); // get y-plane pixels
        res = WritePlane(pSurface->GetPlane(amf::AMF_PLANE_UV), m_pFile); // get uv-plane pixels                
    }
}

bool DecPollingThread::Terminate()
{
    bool ret = PollingThread::Terminate();

    m_pEncoder = NULL;
    return ret;
}

EncPollingThread::EncPollingThread(amf::AMFContext *pContext, amf::AMFComponent *pEncoder, const wchar_t *pFileName)
    : PollingThread(pContext, pEncoder, pFileName, true)
{}

void EncPollingThread::ProcessData(amf::AMFData* pData)
{
    if (m_pFile != NULL)
    {
        amf::AMFBufferPtr pBuffer(pData); // query for buffer interface
        m_pFile->Write(pBuffer->GetNative(), pBuffer->GetSize(), NULL);
    }
}
