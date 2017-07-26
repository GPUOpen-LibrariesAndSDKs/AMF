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
// this sample decodes H.264 elmentary stream to NV12 frames using AMF Decoder and writes the frames into raw file
#include <stdio.h>
#include <tchar.h>
#include <d3d9.h>
#include <d3d11.h>
#include "public/common/AMFFactory.h"
#include "public/include/components/VideoDecoderUVD.h"
#include "public/common/DataStream.h"
#include "../common/BitStreamParser.h"

//static wchar_t *fileNameIn                  = L"./nasa_720p.264";
//static wchar_t *fileNameIn                  = L"./bbc_1080p.264";
static wchar_t *fileNameIn                  = NULL;
static wchar_t *fileNameOut                 = L"./output_%dx%d.nv12";
static amf::AMF_MEMORY_TYPE memoryTypeOut   = amf::AMF_MEMORY_DX11;
static amf::AMF_SURFACE_FORMAT formatOut    = amf::AMF_SURFACE_NV12;
static amf_int32 frameCount                 = 500; // -1 means entire file
static amf_int32 submitted = 0;

static void WritePlane(amf::AMFPlane *plane, FILE *f);
static void WaitDecoder(amf::AMFContext *context, amf::AMFSurface *surface); // Waits till decoder finishes decode the surface. Need for accurate profiling only. Do not use in the product!!!

// The memory transfer from DX9 to HOST and writing a raw file is longer than decode time. To measure decode time correctly disable convert and write here: 
static bool bWriteToFile = false;
#define START_TIME_PROPERTY L"StartTimeProperty" // custom property ID to store submission time in a frame - all custom properties are copied from input to output

#define MILLISEC_TIME     10000

class PollingThread : public amf::AMFThread
{
protected:
    amf::AMFContextPtr      m_pContext;
    amf::AMFComponentPtr    m_pDecoder;
    FILE                    *m_pFile;
public:
    PollingThread(amf::AMFContext *context, amf::AMFComponent *decoder, const wchar_t *pFileName);
    ~PollingThread();
    virtual void Run();
};

int _tmain(int argc, _TCHAR* argv[])
{
    if(argc <= 1 && fileNameIn == NULL)
    {
        wprintf(L"input file name is missing in command line");
        return 1;
    }
    if(argc > 1)
    { 
        fileNameIn = argv[1];
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
    if(memoryTypeOut == amf::AMF_MEMORY_DX9)
    {
        res = context->InitDX9(NULL); // can be DX9 or DX9Ex device
    }
    if(memoryTypeOut == amf::AMF_MEMORY_DX11)
    {
        res = context->InitDX11(NULL); // can be DX11 device
    }

	BitStreamType bsType = GetStreamType(fileNameIn);
	// H264/H265 elemntary stream parser from samples common 
	parser = BitStreamParser::Create(datastream, bsType, context);

    // open output file with frame size in file name
    wchar_t fileNameOutWidthSize[2000];
    _swprintf(fileNameOutWidthSize, fileNameOut, parser->GetPictureWidth(), parser->GetPictureHeight());

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
    res = decoder->Init(formatOut, parser->GetPictureWidth(), parser->GetPictureHeight());

    PollingThread thread(context, decoder, fileNameOutWidthSize);
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

        res = decoder->SubmitInput(data);
        if(res == AMF_NEED_MORE_INPUT)
        {
			break;
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
            
        }
    }
    // drain decoder queue 
    res = decoder->Drain();
    thread.WaitForStop();

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
static void WritePlane(amf::AMFPlane *plane, FILE *f)
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
        fwrite(line + offsetX * pixelSize, pixelSize, width, f);
    }
}
// Waits till decoder finishes decode the surface. Need for accurate profiling only. Do not use in the product!!!
static void WaitDecoder(amf::AMFContext *context, amf::AMFSurface *surface) 
{
    // copy of four pixels will force DX to wait for UVD decoder and will not add a significant delay
    HRESULT hr = S_OK;
    amf::AMFSurfacePtr outputSurface;
    context->AllocSurface(surface->GetMemoryType(), surface->GetFormat(), 2, 2, &outputSurface); // NV12 must be devisible by 2

    switch(surface->GetMemoryType())
    {
    case amf::AMF_MEMORY_DX9:
        {
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
    }
}

PollingThread::PollingThread(amf::AMFContext *context, amf::AMFComponent *decoder, const wchar_t *pFileName) : m_pContext(context), m_pDecoder(decoder), m_pFile(NULL)
{
    if(bWriteToFile)
    {
        m_pFile = _wfopen(pFileName, L"wb");
    }
}
PollingThread::~PollingThread()
{
    if(m_pFile)
    {
        fclose(m_pFile);
    }
}
void PollingThread::Run()
{
    RequestStop();

    amf_pts latency_time = 0;
    amf_pts convert_duration = 0;
    amf_pts write_duration = 0;
    amf_pts decode_duration = 0;
    amf_pts last_poll_time = 0;

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

            amf_pts poll_time = amf_high_precision_clock();
            amf_pts start_time = 0;
            data->GetProperty(START_TIME_PROPERTY, &start_time);
            if(start_time < last_poll_time ) // correct if submission was faster then decode
            {
                start_time = last_poll_time;
            }
            last_poll_time = poll_time;

            decode_duration += poll_time - start_time;

            if(latency_time == 0)
            {
                latency_time = poll_time - start_time;
            }
            if(bWriteToFile)
            {
                // this operation is slow nneed to remove it from stat
                res = data->Convert(amf::AMF_MEMORY_HOST); // convert to system memory

                amf_pts convert_time = amf_high_precision_clock();
                convert_duration += convert_time - poll_time;

                amf::AMFSurfacePtr surface(data); // query for surface interface
    
                WritePlane(surface->GetPlane(amf::AMF_PLANE_Y), m_pFile); // get y-plane pixels
                WritePlane(surface->GetPlane(amf::AMF_PLANE_UV), m_pFile); // get uv-plane pixels
            
                write_duration += amf_high_precision_clock() - convert_time;
            }
        }
        else
        {
            amf_sleep(1);
        }

    }
    printf("latency           = %.4fms\ndecode  per frame = %.4fms\nconvert per frame = %.4fms\nwrite per frame   = %.4fms\n", 
        double(latency_time)/MILLISEC_TIME,
		double(decode_duration) / MILLISEC_TIME / submitted,
		double(convert_duration) / MILLISEC_TIME / submitted,
		double(write_duration) / MILLISEC_TIME / submitted);

    m_pDecoder = NULL;
    m_pContext = NULL;
}
