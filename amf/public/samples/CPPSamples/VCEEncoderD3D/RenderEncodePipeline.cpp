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

#include "RenderEncodePipeline.h"
#include <sstream>

#pragma warning(disable:4355)

#define ENCODER_SUBMIT_TIME     L"EncoderSubmitTime"  // private property to track submit tyme



class RenderEncodePipeline::PipelineElementEncoder : public AMFComponentElement
{
public:
    PipelineElementEncoder(amf::AMFComponentPtr pComponent, ParametersStorage* pParams, amf_int64 frameParameterFreq, amf_int64 dynamicParameterFreq)
        :AMFComponentElement(pComponent),
        m_pParams(pParams),
        m_framesSubmitted(0),
        m_framesQueried(0),
        m_frameParameterFreq(frameParameterFreq),
        m_dynamicParameterFreq(dynamicParameterFreq),
        m_maxLatencyTime(0),
        m_TotalLatencyTime(0),
        m_maxLatencyFrame(0),
        m_LastReadyFrameTime(0)
    {

    }

    virtual ~PipelineElementEncoder(){}

    virtual AMF_RESULT SubmitInput(amf::AMFData* pData)
    {
        AMF_RESULT res = AMF_OK;
        if(pData == NULL) // EOF
        {
            res = m_pComponent->Drain();
        }
        else
        {
            // memorize submission time for statistics
            amf_int64 submitTime = 0;
            amf_int64 currentTime = amf_high_precision_clock();
            if(pData->GetProperty(ENCODER_SUBMIT_TIME, &submitTime) != AMF_OK)
            {
                pData->SetProperty(ENCODER_SUBMIT_TIME, currentTime);
            }

            if(m_frameParameterFreq != 0 && m_framesSubmitted !=0 && (m_framesSubmitted % m_frameParameterFreq) == 0)
            { // apply frame-specific properties to the current frame
                PushParamsToPropertyStorage(m_pParams, ParamEncoderFrame, pData);
            }
            if(m_dynamicParameterFreq != 0 && m_framesSubmitted !=0 && (m_framesSubmitted % m_dynamicParameterFreq) == 0)
            { // apply dynamic properties to the encoder
                PushParamsToPropertyStorage(m_pParams, ParamEncoderDynamic, m_pComponent);
            }
            res = m_pComponent->SubmitInput(pData);
            if(res == AMF_DECODER_NO_FREE_SURFACES || res == AMF_INPUT_FULL)
            {
                return AMF_INPUT_FULL;
            }
            m_framesSubmitted++;
        }
        return res;
    }

    virtual AMF_RESULT QueryOutput(amf::AMFData** ppData)
    {
        AMF_RESULT ret = AMFComponentElement::QueryOutput(ppData);

        if(ret == AMF_OK && *ppData != NULL)
        {
            amf_int64 currentTime = amf_high_precision_clock();
            amf_int64 submitTime = 0;

            if((*ppData)->GetProperty(ENCODER_SUBMIT_TIME, &submitTime) == AMF_OK)
            {
                amf_int64 latencyTime = currentTime - AMF_MAX(submitTime , m_LastReadyFrameTime);
                if(m_maxLatencyTime < latencyTime)
                {
                    m_maxLatencyTime = latencyTime;
                    m_maxLatencyFrame = m_framesQueried;
                }
                m_TotalLatencyTime += latencyTime;
            }
            m_framesQueried++;
            m_LastReadyFrameTime = currentTime;
        }

        return ret;
    }

    virtual std::wstring       GetDisplayResult()
    {
        std::wstring ret;
        if(m_framesSubmitted > 0)
        {
            std::wstringstream messageStream;

            messageStream.precision(1);
            messageStream.setf(std::ios::fixed, std::ios::floatfield);

            double averageLatency = double(m_TotalLatencyTime) / 10000. / m_framesQueried;
            double maxLatency = double(m_maxLatencyTime) / 10000.;

            messageStream << L" Average (Max, fr#) Encode Latency: " << averageLatency << L" ms (" << maxLatency << " ms frame# "<<m_maxLatencyFrame << L")";

            ret = messageStream.str();
        }
        return ret;
    }

protected:
    ParametersStorage*      m_pParams;
    amf_int                 m_framesSubmitted;
    amf_int                 m_framesQueried;
    amf_int64               m_frameParameterFreq;
    amf_int64               m_dynamicParameterFreq;
    amf_int64               m_maxLatencyTime;
    amf_int64               m_TotalLatencyTime;
    amf_int64               m_LastReadyFrameTime;
    amf_int                 m_maxLatencyFrame;
};

const wchar_t* RenderEncodePipeline::PARAM_NAME_CODEC          = L"CODEC";
const wchar_t* RenderEncodePipeline::PARAM_NAME_OUTPUT         = L"OUTPUT";
const wchar_t* RenderEncodePipeline::PARAM_NAME_RENDER         = L"RENDER";

const wchar_t* RenderEncodePipeline::PARAM_NAME_WIDTH          = L"WIDTH";
const wchar_t* RenderEncodePipeline::PARAM_NAME_HEIGHT         = L"HEIGHT";
const wchar_t* RenderEncodePipeline::PARAM_NAME_FRAMES         = L"FRAMES";

const wchar_t* RenderEncodePipeline::PARAM_NAME_ADAPTERID      = L"ADAPTERID";
const wchar_t* RenderEncodePipeline::PARAM_NAME_WINDOW_MODE    = L"WINDOWMODE";
const wchar_t* RenderEncodePipeline::PARAM_NAME_FULLSCREEN     = L"FULLSCREEN";

const wchar_t* RenderEncodePipeline::PARAM_NAME_QUERY_INST_COUNT = L"QueryInstanceCount";
const wchar_t* RenderEncodePipeline::PARAM_NAME_SELECT_INSTANCE  = L"UseInstance";

RenderEncodePipeline::RenderEncodePipeline()
    :m_pContext()
{
}

RenderEncodePipeline::~RenderEncodePipeline()
{
    Terminate();
}

void RenderEncodePipeline::Terminate()
{
    Pipeline::Stop();

    m_pStreamOut = NULL;

    if(m_pVideoRender != NULL)
    {
        m_pVideoRender->Terminate();
        m_pVideoRender = NULL;
    }
    if(m_pConverter != NULL)
    {
        m_pConverter->Terminate();
        m_pConverter = NULL;
    }
    if(m_pEncoder != NULL)
    {
        m_pEncoder->Terminate();
        m_pEncoder = NULL;
    }

    if(m_pContext != NULL)
    {
        m_pContext->Terminate();
        m_pContext = NULL;
    }

    m_pStreamWriter = NULL;

    m_deviceDX9.Terminate();
    m_deviceDX11.Terminate();
    m_deviceOpenGL.Terminate();
    m_deviceOpenCL.Terminate();
}

AMF_RESULT RenderEncodePipeline::Init(ParametersStorage* pParams, int threadID)
{
    Terminate();
    AMF_RESULT res = AMF_OK;

    //---------------------------------------------------------------------------------------------
    // Read Options

    bool bFullScreen = false;

    pParams->GetParam(PARAM_NAME_FULLSCREEN, bFullScreen);

    amf_int width = 1280;
    amf_int height = 720;
    amf_int frames = 100;
    amf_uint32 adapterID = 0;

    pParams->GetParam(PARAM_NAME_WIDTH, width);
    CHECK_RETURN(width > 0 , AMF_FAIL, L"Invalid parameter " << PARAM_NAME_WIDTH << L" : " << width);

    pParams->GetParam(PARAM_NAME_HEIGHT, height);
    CHECK_RETURN(height > 0 , AMF_FAIL, L"Invalid parameter " << PARAM_NAME_HEIGHT << L" : " << width);

    bool bInterlaced = false;
    pParams->GetParam(AMF_VIDEO_ENCODER_SCANTYPE, bInterlaced);

    pParams->GetParam(PARAM_NAME_FRAMES, frames);
    pParams->GetParam(PARAM_NAME_ADAPTERID, adapterID);

    std::wstring renderType = L"DX9";
    pParams->GetParamWString(PARAM_NAME_RENDER, renderType);
    std::wstring uppValue = toUpper(renderType);

    amf::AMF_MEMORY_TYPE renderMemoryType = amf::AMF_MEMORY_DX9;
    amf::AMF_MEMORY_TYPE secondaryMemoryType = amf::AMF_MEMORY_UNKNOWN;

    amf::AMF_MEMORY_TYPE encoderMemoryType = amf::AMF_MEMORY_DX9;
    bool deviceEX = true;

    if(uppValue == L"DX9")
    {
        deviceEX = false;
        renderMemoryType = amf::AMF_MEMORY_DX9;
        encoderMemoryType = amf::AMF_MEMORY_DX9;
    }else
    if(uppValue == L"DX9EX")
    {
        deviceEX = true;
        renderMemoryType = amf::AMF_MEMORY_DX9;
        encoderMemoryType = amf::AMF_MEMORY_DX9;
    }else
    if(uppValue == L"DX11")
    {
        renderMemoryType = amf::AMF_MEMORY_DX11;
        encoderMemoryType = amf::AMF_MEMORY_DX11;
    }else
    if(uppValue == L"OPENGL")
    {
        renderMemoryType = amf::AMF_MEMORY_OPENGL;
        encoderMemoryType = amf::AMF_MEMORY_DX9;
    }else
    if(uppValue == L"OPENCL")
    {
        renderMemoryType = amf::AMF_MEMORY_OPENCL;
        encoderMemoryType = amf::AMF_MEMORY_DX9;
    }else
    if(uppValue == L"HOST")
    {
        renderMemoryType = amf::AMF_MEMORY_HOST;
        encoderMemoryType = amf::AMF_MEMORY_DX9;
    }else
    if(uppValue == L"OPENCLDX9")
    {
        renderMemoryType = amf::AMF_MEMORY_OPENCL;
        encoderMemoryType = amf::AMF_MEMORY_DX9;
    }else
    if(uppValue == L"OPENCLDX11")
    {
        renderMemoryType = amf::AMF_MEMORY_OPENCL;
        encoderMemoryType = amf::AMF_MEMORY_DX11;
    }else
    if(uppValue == L"OPENGLDX9")
    {
        renderMemoryType = amf::AMF_MEMORY_OPENGL;
        encoderMemoryType = amf::AMF_MEMORY_DX9;
    }else
    if(uppValue == L"OPENGLDX11")
    {
        renderMemoryType = amf::AMF_MEMORY_OPENGL;
        encoderMemoryType = amf::AMF_MEMORY_DX11;
    }else
    if(uppValue == L"OPENCLOPENGLDX9")
    {
        renderMemoryType = amf::AMF_MEMORY_OPENCL;
        secondaryMemoryType = amf::AMF_MEMORY_OPENGL;
        encoderMemoryType = amf::AMF_MEMORY_DX9;
    }else
    if(uppValue == L"OPENCLOPENGLDX11")
    {
        renderMemoryType = amf::AMF_MEMORY_OPENCL;
        secondaryMemoryType = amf::AMF_MEMORY_OPENGL;
        encoderMemoryType = amf::AMF_MEMORY_DX11;
    }else
    if(uppValue == L"HOSTDX9")
    {
        renderMemoryType = amf::AMF_MEMORY_HOST;
        encoderMemoryType = amf::AMF_MEMORY_DX9;
    }else
    if(uppValue == L"HOSTDX11")
    {
        renderMemoryType = amf::AMF_MEMORY_HOST;
        encoderMemoryType = amf::AMF_MEMORY_DX11;
    }else
    if(uppValue == L"DX11DX9")
    {
        renderMemoryType = amf::AMF_MEMORY_DX11;
        encoderMemoryType = amf::AMF_MEMORY_DX9;
        deviceEX = true;
    }
    else
    {
        CHECK_RETURN(false, AMF_FAIL, L"Invalid parameter -render " << uppValue);
    }

    amf_int frameParameterFreq = 0;
    amf_int dynamicParameterFreq = 0;
    pParams->GetParam(SETFRAMEPARAMFREQ_PARAM_NAME, frameParameterFreq);
    pParams->GetParam(SETDYNAMICPARAMFREQ_PARAM_NAME, dynamicParameterFreq);

    bool bWnd = false;
    pParams->GetParam(PARAM_NAME_WINDOW_MODE, bWnd);

    //---------------------------------------------------------------------------------------------
    // Create window if needed
    HWND hWnd = NULL;
    if(bWnd)
    {
        if(!m_window.CreateD3Window(width, height, adapterID, bFullScreen))
        {
            CHECK_RETURN(false, AMF_FAIL, L"CreateD3Window() failed. Error " << GetLastError());
        }
        hWnd = m_window.GetHwnd();
    }
    else
    {
        hWnd = ::GetDesktopWindow();
    }

    //---------------------------------------------------------------------------------------------
    // Init context and devices

    g_AMFFactory.GetFactory()->CreateContext(&m_pContext);
    std::wstring displayDeviceName;
    if(renderMemoryType == amf::AMF_MEMORY_DX9 || encoderMemoryType == amf::AMF_MEMORY_DX9)
    {
        res = m_deviceDX9.Init(deviceEX, adapterID, bFullScreen, width, height);
        CHECK_AMF_ERROR_RETURN(res, L"m_deviceDX9.Init() failed");
        displayDeviceName = m_deviceDX9.GetDisplayDeviceName();
        res = m_pContext->InitDX9(m_deviceDX9.GetDevice());
        CHECK_AMF_ERROR_RETURN(res, L"m_pContext->InitDX9() failed");
    }

    if(renderMemoryType == amf::AMF_MEMORY_DX11 || encoderMemoryType == amf::AMF_MEMORY_DX11)
    {
        //in case of opengl usage we need to select only adapter with output (display)
        res = m_deviceDX11.Init(adapterID, renderMemoryType == amf::AMF_MEMORY_OPENGL || secondaryMemoryType == amf::AMF_MEMORY_OPENGL);
        CHECK_AMF_ERROR_RETURN(res, L"m_deviceDX11.Init() failed");
        displayDeviceName = m_deviceDX11.GetDisplayDeviceName();
        res = m_pContext->InitDX11(m_deviceDX11.GetDevice());
        CHECK_AMF_ERROR_RETURN(res, L"m_pContext->InitDX11() failed");

		D3D_FEATURE_LEVEL dxLevel = m_deviceDX11.GetDevice()->GetFeatureLevel();
		// En/Decoder supports starts 11.1
		if (dxLevel != D3D_FEATURE_LEVEL_11_1)
		{
			res = m_deviceDX9.Init(deviceEX, adapterID, bFullScreen, width, height);
			CHECK_AMF_ERROR_RETURN(res, L"m_deviceDX9.Init() failed");
			displayDeviceName = m_deviceDX9.GetDisplayDeviceName();
			res = m_pContext->InitDX9(m_deviceDX9.GetDevice());
			CHECK_AMF_ERROR_RETURN(res, L"m_pContext->InitDX9() failed");
		}
    }

    if(renderMemoryType == amf::AMF_MEMORY_OPENGL || secondaryMemoryType == amf::AMF_MEMORY_OPENGL)
    {
        res = m_deviceOpenGL.Init(hWnd, displayDeviceName.c_str());
        CHECK_AMF_ERROR_RETURN(res, L"m_deviceOpenGL.Init() failed");

        res = m_pContext->InitOpenGL(m_deviceOpenGL.GetContextOpenGL(), m_deviceOpenGL.GetWindowOpenGL(), m_deviceOpenGL.GetDCOpenGL());
        CHECK_AMF_ERROR_RETURN(res, L"m_pContext->InitOpenGL() failed");
    }

    if(renderMemoryType == amf::AMF_MEMORY_OPENCL || renderMemoryType == amf::AMF_MEMORY_OPENGL || secondaryMemoryType == amf::AMF_MEMORY_OPENGL)
    {
        res = m_deviceOpenCL.Init(m_deviceDX9.GetDevice(), m_deviceDX11.GetDevice(), m_deviceOpenGL.GetContextOpenGL(), m_deviceOpenGL.GetDCOpenGL());
        CHECK_AMF_ERROR_RETURN(res, L"m_deviceOpenCL.Init() failed");

        res = m_pContext->InitOpenCL(m_deviceOpenCL.GetCommandQueue());
        CHECK_AMF_ERROR_RETURN(res, L"m_pContext->InitOpenCL() failed");
    }

    if(renderMemoryType == amf::AMF_MEMORY_HOST)
    {
    }

    //---------------------------------------------------------------------------------------------
    // Init Video Render

    m_pVideoRender = VideoRender::Create(width, height, bInterlaced, frames, renderMemoryType, encoderMemoryType, m_pContext);

    res = m_pVideoRender->Init(hWnd, bFullScreen);
    CHECK_AMF_ERROR_RETURN(res, L"m_pVideoRender->Init() failed");

    //---------------------------------------------------------------------------------------------
    // Init Video Converter
    if(bFullScreen)
    {
        res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, AMFVideoConverter, &m_pConverter);
        CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << AMFVideoConverter << L") failed");

        m_pConverter->SetProperty(AMF_VIDEO_CONVERTER_MEMORY_TYPE, encoderMemoryType);
        m_pConverter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_SIZE, AMFConstructSize(width, height));
        m_pConverter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_FORMAT, amf::AMF_SURFACE_NV12);

        res = m_pConverter->Init(m_pVideoRender->GetFormat(), 0, 0);
        CHECK_AMF_ERROR_RETURN(res, L"m_pConverter->Init() failed");
    }


    //---------------------------------------------------------------------------------------------
    // Init Video Encoder

    std::wstring encoderID = AMFVideoEncoderVCE_AVC;
    pParams->GetParamWString(RenderEncodePipeline::PARAM_NAME_CODEC, encoderID);

    if(encoderID == AMFVideoEncoderVCE_AVC)
    { 
        amf_int64 usage = 0;
        if(pParams->GetParam(AMF_VIDEO_ENCODER_USAGE, usage) == AMF_OK)
        {
            if(usage == amf_int64(AMF_VIDEO_ENCODER_USAGE_WEBCAM))
            {
                encoderID = AMFVideoEncoderVCE_SVC;
            }
        }
    }
    res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, encoderID.c_str(), &m_pEncoder);
    CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << encoderID << L") failed");

	// Usage is preset that will set many parameters
	PushParamsToPropertyStorage(pParams, ParamEncoderUsage, m_pEncoder);
	// override some usage parameters
	PushParamsToPropertyStorage(pParams, ParamEncoderStatic, m_pEncoder);

    // Query Number of VCE Independent Instances and selecting proper one.
    amf_bool toQuery;
	amf_int32   numOfInstances = 0;
	amf_int32	 selectedInstance;
    amf::AMFCapsPtr encoderCaps;

    m_pEncoder->GetCaps(&encoderCaps);


	if (pParams->GetParam(PARAM_NAME_QUERY_INST_COUNT, toQuery) != AMF_OK)
		toQuery = false;

	if (pParams->GetParam(PARAM_NAME_SELECT_INSTANCE, selectedInstance) != AMF_OK)
		selectedInstance = -1;
	
	// Query Number of Independent Instances
    if(encoderID == AMFVideoEncoderVCE_AVC || encoderID == AMFVideoEncoderVCE_SVC)
    { 
	    if ( toQuery || (selectedInstance >= 0) )
        {

		    if (encoderCaps)
		    {
                encoderCaps->GetProperty(AMF_VIDEO_ENCODER_CAP_NUM_OF_HW_INSTANCES, &numOfInstances);
			    LOG_SUCCESS(L"Number of VCE Independent Instances are: " << numOfInstances);
		    }
		    else
		    {
			    LOG_ERROR(L"Unable to Query Number of VCE Independent Instances");
			    numOfInstances = 1;
			    selectedInstance = -1;
		    }
	    }
	    // Set the selected instance
	    if (selectedInstance >= 0)
	    {
		    if (selectedInstance >= numOfInstances)
		    {
			    LOG_ERROR(L"Invalid instance number is selected. The instance number can be from 0 to (Number of available instance - 1). Selected instance is: " << selectedInstance);
			    LOG_ERROR(L"Number of available instances are: " << numOfInstances);
		    }
		    else
		    {
			    if (m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_MULTI_INSTANCE_MODE, true) == AMF_OK)
			    {
				    if (m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_CURRENT_QUEUE, selectedInstance) == AMF_OK)
				    {
					    LOG_SUCCESS(L"Selected Instance is: " << selectedInstance);
				    }
				    else
				    {
					    LOG_ERROR(L"Error while selecting instace number " << selectedInstance);
				    }
			    }
			    else
			    {
				    LOG_ERROR(L"Error while enabling Multi Instace Mode");
			    }
		    }
	    }
    }
	// Initialize the Encoder
	res = m_pEncoder->Init(bFullScreen ? amf::AMF_SURFACE_NV12 : m_pVideoRender->GetFormat(), width, height);
	CHECK_AMF_ERROR_RETURN(res, L"m_pEncoder->Init() failed");

	PushParamsToPropertyStorage(pParams, ParamEncoderDynamic, m_pEncoder);
	    
    //---------------------------------------------------------------------------------------------
    // Init Stream Writer

    std::wstring outputPath = L"";
    pParams->GetParamWString(PARAM_NAME_OUTPUT, outputPath);
    if(outputPath.empty())
    {
        res = AMF_FILE_NOT_OPEN;
    }

    if(threadID != -1)
    {
        std::wstring::size_type pos_dot = outputPath.rfind(L'.');
        if(pos_dot == std::wstring::npos)
        {
            LOG_ERROR(L"Bad file name (no extension): " << outputPath);
            return AMF_FAIL;
        }
        std::wstringstream prntstream;
        prntstream << threadID;
        outputPath = outputPath.substr(0, pos_dot) + L"_" + prntstream.str() + outputPath.substr(pos_dot);
    }

    amf::AMFDataStream::OpenDataStream(outputPath.c_str(), amf::AMFSO_WRITE, amf::AMFFS_SHARE_READ, &m_pStreamOut);
    CHECK_RETURN(m_pStreamOut != NULL, AMF_FILE_NOT_OPEN, "Open File" << outputPath);

    m_pStreamWriter = PipelineElementPtr(new StreamWriter(m_pStreamOut));

#define ASYNC_CONNECT 0 // full a-sync
//#define ASYNC_CONNECT 1 // full sync
//#define ASYNC_CONNECT 2 // siltter test
#if ASYNC_CONNECT == 0
    //---------------------------------------------------------------------------------------------
    // Connect pipeline a-sync

    Connect(m_pVideoRender, 4);
    if(m_pConverter != NULL)
    {
        Connect(PipelineElementPtr(new AMFComponentElement(m_pConverter)), 4);
    }
    Connect(PipelineElementPtr(new PipelineElementEncoder(m_pEncoder, pParams, frameParameterFreq, dynamicParameterFreq)), 10);
    Connect(m_pStreamWriter, 5);
//    m_pDummyWriter = DummyWriterPtr(new DummyWriter());
//    Connect(m_pDummyWriter, 5);
#elif ASYNC_CONNECT == 1
    //---------------------------------------------------------------------------------------------
    // Connect pipeline sync - all components run in the same thread - slow - this code is for demo only

    Connect(m_pVideoRender, 4, true);
    if(m_pConverter != NULL)
    {
        Connect(PipelineElementPtr(new AMFComponentElement(m_pConverter)), 4, true);
    }
    Connect(PipelineElementPtr(new PipelineElementEncoder(m_pEncoder, pParams, frameParameterFreq, dynamicParameterFreq)), 10, true);
    Connect(m_pStreamWriter, 5, true);
#else
    // try splitter

    std::wstring outputPath2 = L"";
    std::wstring::size_type pos_dot = outputPath.rfind(L'.');
    outputPath2 = outputPath.substr(0, pos_dot) + L"_split" + outputPath.substr(pos_dot);

    m_pStreamOut2 = AMFDataStream::Create(outputPath2.c_str(), AMF_FileWrite);
    CHECK_RETURN(m_pStreamOut2 != NULL, AMF_FILE_NOT_OPEN, "Open File" << outputPath2);

    m_pStreamWriter2 = PipelineElementPtr(new StreamWriter(m_pStreamOut2));

    m_pSplitter = SplitterPtr(new Splitter(false, 2, 10));

    Connect(m_pVideoRender, 4, true);
    if(m_pConverter != NULL)
    {
        Connect(PipelineElementPtr(new AMFComponentElement(m_pConverter)), 4, true);
    }
    Connect(PipelineElementPtr(new PipelineElementEncoder(m_pEncoder, pParams, frameParameterFreq, dynamicParameterFreq)), 10, false);
    Connect(m_pSplitter, 2, false);
    Connect(m_pStreamWriter, 0, m_pSplitter, 0, 5, false);
    Connect(m_pStreamWriter2, 0, m_pSplitter, 1, 5, false);
#endif
    return res;
}

AMF_RESULT RenderEncodePipeline::Run()
{
    AMF_RESULT res = AMF_OK;
    res= Pipeline::Start();
    CHECK_AMF_ERROR_RETURN(res, L"Pipeline::Start() failed");
    return AMF_OK;
}

void RenderEncodePipeline::ProcessWindowMessages()
{
    m_window.ProcessWindowMessages();
}
