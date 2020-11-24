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
// NV12 frames using AMF Decoder, then encode the frames into H.264 or H.265 stream based on ROI.

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
#include "public/include/components/VideoEncoderVCE.h"
#include "public/include/components/VideoEncoderHEVC.h"
#include "../common/BitStreamParser.h"
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

static wchar_t *pOutputCodec;
static amf_int64 bitRateOut					= 5000000L; // in bits, 5MBit
static bool bMaximumSpeed					= true;


static const wchar_t *fileNameDecOut        = L"./output_%dx%d.nv12";
static const wchar_t *fileNameEncOut        = L"./output_%dx%d.%d";

static void WritePlane(amf::AMFPlane *plane, FILE *f);
static void WaitDecoder(amf::AMFContext *context, amf::AMFSurface *surface); // Waits till decoder finishes decode the surface. Need for accurate profiling only. Do not use in the product!!!

// The memory transfer from DX9 to HOST and writing a raw file is longer than decode time. To measure decode time correctly disable convert and write here: 
static bool bWriteDecOutToFile = false;
#define START_TIME_PROPERTY L"StartTimeProperty" // custom property ID to store submission time in a frame - all custom properties are copied from input to output
#define MILLISEC_TIME     10000

class DecPollingThread : public amf::AMFThread
{
protected:
    amf::AMFContextPtr      m_pContext;
    amf::AMFComponentPtr    m_pEncoder;
    amf::AMFComponentPtr    m_pDecoder;
    std::ofstream           m_pFile;
public:
	DecPollingThread(amf::AMFContext *context, amf::AMFComponent *encoder, amf::AMFComponent *decoder, const wchar_t *pFileName);
    ~DecPollingThread();
    virtual void Run();
};

class EncPollingThread : public amf::AMFThread
{
protected:
	amf::AMFContextPtr      m_pContext;
	amf::AMFComponentPtr    m_pEncoder;
	std::ofstream           m_pFile;
public:
	EncPollingThread(amf::AMFContext *context, amf::AMFComponent *encoder, const wchar_t *pFileName);
	~EncPollingThread();
	virtual void Run();
};

template<class charT>
std::wstring convert2WString(const charT * p)
{
    std::basic_string<charT> str(p);
    return std::wstring(str.begin(), str.end());
} 

#ifdef _WIN32
int _tmain(int argc, _TCHAR* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    std::wstring  fileNameIn;

	switch (argc) 
    {
		case 2: {
			fileNameIn = convert2WString(argv[1]);
			pOutputCodec = AMFVideoEncoderVCE_AVC; // default to output AVC
			break; }
		case 3: {
			fileNameIn = convert2WString(argv[1]);
	
            std::wstring  param = convert2WString(argv[2]);
            if ((param.compare(L"h264") != 0) && (param.compare(L"avc") == 0))
			{
				pOutputCodec = AMFVideoEncoderVCE_AVC;
			}
            else if ((param.compare(L"h265") != 0) && (param.compare(L"hevc") == 0))
            {
				pOutputCodec = AMFVideoEncoder_HEVC;
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

	if (amf_wstring(pOutputCodec) == amf_wstring(AMFVideoEncoderVCE_AVC))
	{
		swprintf(fileNameEncOutWithSize, amf_countof(fileNameEncOutWithSize), fileNameEncOut, parser->GetPictureWidth(), parser->GetPictureHeight(), 264);
		res = encoder->SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_TRANSCONDING);
		AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_TRANSCONDING) failed");

		if (bMaximumSpeed)
		{
			res = encoder->SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0);
			// do not check error for AMF_VIDEO_ENCODER_B_PIC_PATTERN - can be not supported - check Capability Manager sample
			res = encoder->SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED);
			AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED) failed");
		}

		res = encoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, bitRateOut);
		AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, %" LPRId64 L") failed", bitRateOut);
		res = encoder->SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, ::AMFConstructSize(widthIn, heightIn));
		AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, %dx%d) failed", widthIn, heightIn);
		res = encoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, ::AMFConstructRate(frameRateIn, 1));
		AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, %dx%d) failed", frameRateIn, 1);

		if (widthIn > 1920 && heightIn > 1088)
		{
			res = encoder->SetProperty(AMF_VIDEO_ENCODER_PROFILE, AMF_VIDEO_ENCODER_PROFILE_HIGH);
			AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_PROFILE, AMF_VIDEO_ENCODER_PROFILE_HIGH) failed");

			res = encoder->SetProperty(AMF_VIDEO_ENCODER_PROFILE_LEVEL, 51);
			AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_PROFILE_LEVEL, 51)");
			res = encoder->SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0);
			AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0)");
		}
	}
	else
	{
		swprintf(fileNameEncOutWithSize, amf_countof(fileNameEncOutWithSize), fileNameEncOut, parser->GetPictureWidth(), parser->GetPictureHeight(), 265);
		res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCONDING);
		AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCONDING)");

		if (bMaximumSpeed)
		{
			res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED);
			AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED)");
		}

		res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, bitRateOut);
		AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, %" LPRId64 L") failed", bitRateOut);
		res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, ::AMFConstructSize(widthIn, heightIn));
		AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, %dx%d) failed", widthIn, heightIn);
		res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, ::AMFConstructRate(frameRateIn, 1));
		AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, %dx%d) failed", frameRateIn, 1);

		if (widthIn > 1920 && heightIn > 1088)
		{
			res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_TIER, AMF_VIDEO_ENCODER_HEVC_TIER_HIGH);
			AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_TIER, AMF_VIDEO_ENCODER_HEVC_TIER_HIGH) failed");
			res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_PROFILE_LEVEL, AMF_LEVEL_5_1);
			AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_PROFILE_LEVEL, AMF_LEVEL_5_1) failed");
		}
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
    threadDec.WaitForStop();

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
	threadEnc.WaitForStop();

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

static void WritePlane(amf::AMFPlane *plane, std::ofstream &f)
{
    // write NV12 surface removing offsets and alignments
    amf_uint8 *data     = reinterpret_cast<amf_uint8*>(plane->GetNative());
    amf_int32 offsetX   = plane->GetOffsetX();
    amf_int32 offsetY   = plane->GetOffsetY();
    amf_int32 pixelSize = plane->GetPixelSizeInBytes();
    amf_int32 height    = plane->GetHeight();
    amf_int32 width     = plane->GetWidth();
    amf_int32 pitchH    = plane->GetHPitch();

    for( amf_int32 y = 0; y < height; y++)
    {
        amf_uint8 *line = data + (y + offsetY) * pitchH;
        f.write(reinterpret_cast<char*>(line) + offsetX * pixelSize, pixelSize * width);
    }
}

// Waits till decoder finishes decode the surface. Need for accurate profiling only. Do not use in the product!!!
static void WaitDecoder(amf::AMFContext *context, amf::AMFSurface *surface) 
{
    // copy of four pixels will force DX to wait for UVD decoder and will not add a significant delay
    amf::AMFSurfacePtr outputSurface;
    context->AllocSurface(surface->GetMemoryType(), surface->GetFormat(), 2, 2, &outputSurface); // NV12 must be devisible by 2

    switch(surface->GetMemoryType())
    {
#ifdef _WIN32        
    case amf::AMF_MEMORY_DX9:
        {
            HRESULT hr = S_OK;
    
            IDirect3DDevice9 *deviceDX9 = (IDirect3DDevice9 *)context->GetDX9Device(); // no reference counting - do not Release()
            IDirect3DSurface9* surfaceDX9src = (IDirect3DSurface9*)surface->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()
            IDirect3DSurface9* surfaceDX9dst = (IDirect3DSurface9*)outputSurface->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()
            RECT rect = {0, 0, 2, 2};
            // a-sync copy
            hr = deviceDX9->StretchRect(surfaceDX9src,&rect ,surfaceDX9dst, &rect, D3DTEXF_NONE);
            // wait
            outputSurface->Convert(amf::AMF_MEMORY_HOST);
        }
        break;
    case amf::AMF_MEMORY_DX11:
        {
            HRESULT hr = S_OK;
    
            ID3D11Device *deviceDX11 = (ID3D11Device*)context->GetDX11Device(); // no reference counting - do not Release()
            ID3D11Texture2D *textureDX11src = (ID3D11Texture2D*)surface->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()
            ID3D11Texture2D *textureDX11dst = (ID3D11Texture2D*)outputSurface->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()
            ID3D11DeviceContext *contextDX11 = NULL;
            deviceDX11->GetImmediateContext(&contextDX11);
            D3D11_BOX srcBox = {0, 0, 0, 2, 2, 1};
            contextDX11->CopySubresourceRegion(textureDX11dst, 0, 0, 0, 0, textureDX11src, 0, &srcBox);
            contextDX11->Flush();
            // release temp objects
            contextDX11->Release();
            outputSurface->Convert(amf::AMF_MEMORY_HOST);
        }
        break;
#endif
    case amf::AMF_MEMORY_VULKAN:
        {
            // release temp objects
            outputSurface->Convert(amf::AMF_MEMORY_HOST);
        }
        break;
    }
}

DecPollingThread::DecPollingThread(amf::AMFContext *context, amf::AMFComponent *encoder, amf::AMFComponent *decoder, const wchar_t *pFileName) : m_pContext(context), m_pEncoder(encoder), m_pDecoder(decoder)
{
    if(bWriteDecOutToFile)
    {
        std::wstring wStr(pFileName);
        std::string str(wStr.begin(), wStr.end()); 
        m_pFile = std::ofstream(str, std::ofstream::binary | std::ofstream::out);

        if(!m_pFile.is_open())
        {
            std::cerr << "Error(" << strerror(errno) << ")" << "Unable to open file: " << str  << std::endl; 
        }
    }
}

DecPollingThread::~DecPollingThread()
{
    if(m_pFile)
    {
        m_pFile.close();
    }
}
void DecPollingThread::Run()
{
    RequestStop();

    AMF_RESULT res = AMF_OK; // error checking can be added later
    while(true)
    {
        amf::AMFDataPtr data;
        res = m_pDecoder->QueryOutput(&data);
        if(res == AMF_EOF)
        {
            break; // Drain complete
        }
        if(data != NULL)
        {
            WaitDecoder(m_pContext, amf::AMFSurfacePtr(data)); // Waits till decoder finishes decode the surface. Need for accurate profiling only. Do not use in the product!!!


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
			amf::AMFContext1Ptr  spContext1(m_pContext);
            AMF_RESULT res = spContext1->AllocSurfaceEx(amf::AMF_MEMORY_HOST, ROIMapformat, num_blocks_x, num_blocks_y,
				amf::AMF_SURFACE_USAGE_DEFAULT | amf::AMF_SURFACE_USAGE_LINEAR, amf::AMF_MEMORY_CPU_DEFAULT, &pROIMapSurface);
			if (res != AMF_OK)
			{
				printf("AMFContext::AllocSurface(amf::AMF_MEMORY_HOST) for ROI map failed!\n");
			}

			amf_uint32 *buf = (amf_uint32 *)pROIMapSurface->GetPlaneAt(0)->GetNative();
			amf_int32 pitch = pROIMapSurface->GetPlaneAt(0)->GetHPitch();
			memset((void *)buf, 0, pitch * num_blocks_y);
			
			for (int y = num_blocks_y / 4; y < num_blocks_y * 3 / 4; y++)
			{
				for (int x = num_blocks_x / 4; x < num_blocks_x * 3 / 4; x++)
				{
					buf[x + y * pitch / 4] = 10;
				}
			}

            amf::AMFSurfacePtr surface(data); // query for surface interface
            if (amf_wstring(pOutputCodec) == amf_wstring(AMFVideoEncoderVCE_AVC))
			{
				surface->SetProperty(AMF_VIDEO_ENCODER_ROI_DATA, pROIMapSurface.Detach());
			}
			else
			{
				surface->SetProperty(AMF_VIDEO_ENCODER_HEVC_ROI_DATA, pROIMapSurface.Detach());
			}

            m_pEncoder->SubmitInput(surface);

            if(bWriteDecOutToFile)
            {
                // this operation is slow nneed to remove it from stat
                res = data->Convert(amf::AMF_MEMORY_HOST); // convert to system memory

                WritePlane(surface->GetPlane(amf::AMF_PLANE_Y), m_pFile); // get y-plane pixels
                WritePlane(surface->GetPlane(amf::AMF_PLANE_UV), m_pFile); // get uv-plane pixels                
            }			
        }
        else
        {
            amf_sleep(1);
        }

    }

    m_pDecoder = NULL;
    m_pEncoder = NULL;
    m_pContext = NULL;
}


EncPollingThread::EncPollingThread(amf::AMFContext *context, amf::AMFComponent *encoder, const wchar_t *pFileName) : m_pContext(context), m_pEncoder(encoder)
{
	std::wstring wStr(pFileName);
	std::string str(wStr.begin(), wStr.end());
	m_pFile = std::ofstream(str, std::ios::binary);
}

EncPollingThread::~EncPollingThread()
{
	if (m_pFile)
	{
		m_pFile.close();
	}
}

void EncPollingThread::Run()
{
	RequestStop();

	AMF_RESULT res = AMF_OK; // error checking can be added later
	while (true)
	{
		amf::AMFDataPtr data;
		res = m_pEncoder->QueryOutput(&data);
		if (res == AMF_EOF)
		{
			break; // Drain complete
		}
		if (data != NULL)
		{
			amf::AMFBufferPtr buffer(data); // query for buffer interface
			m_pFile.write(reinterpret_cast<char*>(buffer->GetNative()), buffer->GetSize());
		}
		else
		{
			amf_sleep(1);
		}
	}

	m_pEncoder = NULL;
	m_pContext = NULL;
}
