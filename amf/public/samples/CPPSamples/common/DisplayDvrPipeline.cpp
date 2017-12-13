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
// Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.
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
#include "DisplayDvrPipeline.h"
#include "public/include/components/VideoDecoderUVD.h"
#include "public/include/components/VideoConverter.h"
#include "public/include/components/FFMPEGComponents.h"
#include "public/include/components/FFMPEGAudioDecoder.h"
#include "public/include/components/FFMPEGAudioConverter.h"
#include "public/include/components/FFMPEGAudioEncoder.h"
#include "public/include/components/FFMPEGFileDemuxer.h"
#include "public/include/components/FFMPEGFileMuxer.h"
#include "public/include/components/DisplayCapture.h"
#include "public/include/components/AudioCapture.h"
#include "public/common/PropertyStorageExImpl.h"

#pragma warning(disable:4355)


const wchar_t* DisplayDvrPipeline::PARAM_NAME_CODEC          = L"CODEC";
const wchar_t* DisplayDvrPipeline::PARAM_NAME_OUTPUT         = L"OUTPUT";

const wchar_t* DisplayDvrPipeline::PARAM_NAME_ADAPTERID			= L"ADAPTERID";
const wchar_t* DisplayDvrPipeline::PARAM_NAME_MONITORID			= L"MONITORID";

const wchar_t* DisplayDvrPipeline::PARAM_NAME_VIDEO_HEIGHT		= L"VIDEOHEIGHT";
const wchar_t* DisplayDvrPipeline::PARAM_NAME_VIDEO_WIDTH		= L"VIDEOWIDTH";

const wchar_t* DisplayDvrPipeline::PARAM_NAME_OPENCL_CONVERTER = L"OPENCLCONVERTER";

const unsigned kFFMPEG_AAC_CODEC_ID = 0x15002;

// Definitions from include/libavutil/channel_layout.h
const unsigned kFFMPEG_AUDIO_LAYOUT_STEREO = 0x00000003;

const unsigned kFrameRate = 60;

namespace
{
	// Helper for changing the surface format on the display capture connection
	class AMFComponentElementDisplayCaptureInterceptor : public AMFComponentElement
	{
	public:
		AMFComponentElementDisplayCaptureInterceptor(DisplayDvrPipeline* pDisplayDvrPipeline, amf::AMFComponent *pComponent)
			: AMFComponentElement(pComponent)
			, m_pDisplayDvrPipeline(pDisplayDvrPipeline)
		{
		}

		virtual ~AMFComponentElementDisplayCaptureInterceptor()
		{
			m_pDisplayDvrPipeline = NULL;
		}

		virtual AMF_RESULT QueryOutput(amf::AMFData** ppData)
		{
			AMF_RESULT res = AMFComponentElement::QueryOutput(ppData);
			CHECK_AMF_ERROR_RETURN(res, "AMFComponentElement::QueryOutput() failed");
			// Get the surface format
			amf::AMFSurfacePtr pSurfPtr(*ppData);
			if (pSurfPtr)
			{
				AMF_RESULT res2 = AMF_OK;

				amf::AMF_SURFACE_FORMAT format = pSurfPtr->GetFormat();
				// Was there a format switch?
				if (format != m_pDisplayDvrPipeline->GetConverterFormat())
				{
					res2 = m_pDisplayDvrPipeline->SwitchConverterFormat(format);
					CHECK_AMF_ERROR_RETURN(res2, "m_pDisplayDvrPipeline->SwitchConverterFormat() failed");
				}
			}
			return res;
		}

	private:
		DisplayDvrPipeline*                         m_pDisplayDvrPipeline;
	};

	// Helper for changing the surface format on the display capture connection
	class AMFComponentElementConverterInterceptor : public AMFComponentElement
	{
	public:
		AMFComponentElementConverterInterceptor(DisplayDvrPipeline* pDisplayDvrPipeline, amf::AMFComponent *pComponent)
			: AMFComponentElement(pComponent)
			, m_pDisplayDvrPipeline(pDisplayDvrPipeline)
			, m_bBlock(false)
			, m_blockStartTime(-1)
		{
		}

		virtual ~AMFComponentElementConverterInterceptor()
		{
			m_pDisplayDvrPipeline = NULL;
		}

		virtual AMF_RESULT SubmitInput(amf::AMFData* pData)
		{
			amf::AMFLock lock(&m_sync);

			AMF_RESULT res = AMF_OK;
			amf::AMFDataPtr pDataPtr(pData);
			if (pDataPtr->GetPts() <= m_blockStartTime)
			{
				// Discard any old data
				res = AMF_OK;
			}
			else
			{
				// Process normally
				res = AMFComponentElement::SubmitInput(pData);
			}
			return res;
		}

		virtual AMF_RESULT QueryOutput(amf::AMFData** ppData)
		{
			amf::AMFLock lock(&m_sync);

			AMF_RESULT res = AMF_OK;
			if (m_bBlock)
			{
				return AMF_REPEAT;
			}
			//
			res = AMFComponentElement::QueryOutput(ppData);
			CHECK_AMF_ERROR_RETURN(res, "AMFComponentElement::QueryOutput() failed");
			//
			return res;
		}

		void SetBlock(bool state)
		{
			amf::AMFLock lock(&m_sync);

			m_bBlock = state;
			if (state)
			{
				m_blockStartTime = m_pDisplayDvrPipeline->GetCurrentPts();
			}
		}

	private:
		DisplayDvrPipeline*                m_pDisplayDvrPipeline;
		mutable amf::AMFCriticalSection    m_sync;
		bool                               m_bBlock;
		amf_pts                            m_blockStartTime;
	};

}

// PipelineElementEncoder implementation
//
class DisplayDvrPipeline::PipelineElementEncoder : public AMFComponentElement
{
public:
	//-------------------------------------------------------------------------------------------------
	PipelineElementEncoder(amf::AMFComponentPtr pComponent, ParametersStorage* pParams, amf_int64 frameParameterFreq, amf_int64 dynamicParameterFreq)
		:AMFComponentElement(pComponent),
		m_pParams(pParams),
		m_framesSubmitted(0),
		m_frameParameterFreq(frameParameterFreq),
		m_dynamicParameterFreq(dynamicParameterFreq)
	{
	}

	//-------------------------------------------------------------------------------------------------
	virtual ~PipelineElementEncoder()
	{
	}

	//-------------------------------------------------------------------------------------------------
	AMF_RESULT SubmitInput(amf::AMFData* pData)
	{
		AMF_RESULT res = AMF_OK;
		if(pData == NULL) // EOF
		{
			res = m_pComponent->Drain();
		}
		else
		{
			if(m_frameParameterFreq != 0 && m_framesSubmitted !=0 && (m_framesSubmitted % m_frameParameterFreq) == 0)
			{ // apply frame-specific properties to the current frame
				PushParamsToPropertyStorage(m_pParams, ParamEncoderFrame, pData);
			}
			if(m_dynamicParameterFreq != 0 && m_framesSubmitted !=0 && (m_framesSubmitted % m_dynamicParameterFreq) == 0)
			{ // apply dynamic properties to the encoder
				PushParamsToPropertyStorage(m_pParams, ParamEncoderDynamic, m_pComponent);
			}


			res = m_pComponent->SubmitInput(pData);
			if(res == AMF_DECODER_NO_FREE_SURFACES)
			{
				return AMF_INPUT_FULL;
			}
			m_framesSubmitted++;
		}
		return res; 
	}

protected:
	ParametersStorage*      m_pParams;
	amf_int                 m_framesSubmitted;
	amf_int64               m_frameParameterFreq;
	amf_int64               m_dynamicParameterFreq;
};

// DisplayDvrPipeline implementation
//

//-------------------------------------------------------------------------------------------------
DisplayDvrPipeline::DisplayDvrPipeline()
	: m_pContext()
	, m_pCurrentTime(new amf::AMFCurrentTimeImpl())
	, m_converterSurfaceFormat(amf::AMF_SURFACE_BGRA)
	, m_outVideoStreamMuxerIndex(-1)
	, m_outAudioStreamMuxerIndex(-1)
	, m_converterInterceptor(nullptr)
	, m_useOpenCLConverter(false)
{
	SetParamDescription(PARAM_NAME_CODEC, ParamCommon, L"Codec name (AVC or H264, HEVC or H265)", ParamConverterCodec);
	SetParamDescription(PARAM_NAME_OUTPUT, ParamCommon, L"Output file name", NULL);
	SetParamDescription(PARAM_NAME_ADAPTERID, ParamCommon, L"Index of GPU adapter (number, default = 0)", NULL);
	SetParamDescription(PARAM_NAME_MONITORID, ParamCommon, L"Index of monitor on GPU (number, default = 0)", NULL);
	SetParamDescription(PARAM_NAME_VIDEO_HEIGHT, ParamCommon, L"Video height (number, default = 0)", NULL);
	SetParamDescription(PARAM_NAME_VIDEO_WIDTH, ParamCommon, L"Video width (number, default = 0)", NULL);
	SetParamDescription(PARAM_NAME_OPENCL_CONVERTER, ParamCommon, L"Use OpenCL Converter (bool, default = false)", NULL);
	
	// to demo frame-specific properties - will be applied to each N-th frame (force IDR)
	SetParam(AMF_VIDEO_ENCODER_FORCE_PICTURE_TYPE, amf_int64(AMF_VIDEO_ENCODER_PICTURE_TYPE_IDR));
}

//-------------------------------------------------------------------------------------------------
DisplayDvrPipeline::~DisplayDvrPipeline()
{
	Terminate();
}

//-------------------------------------------------------------------------------------------------
void DisplayDvrPipeline::Terminate()
{
	Stop();
	m_pContext = NULL;
}

//-------------------------------------------------------------------------------------------------
amf_pts DisplayDvrPipeline::GetCurrentPts()
{
	return m_pCurrentTime->Get();
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT DisplayDvrPipeline::InitContext(const std::wstring& engineStr, amf::AMF_MEMORY_TYPE engineMemoryType, amf_uint32 adapterID)
{
	AMF_RESULT res = AMF_OK;

	res = g_AMFFactory.GetFactory()->CreateContext(&m_pContext);
	CHECK_AMF_ERROR_RETURN(res, "Create AMF context");

	// Check to see if we need to initialize OpenCL
	GetParam(PARAM_NAME_OPENCL_CONVERTER, m_useOpenCLConverter);

	switch (engineMemoryType)
	{
#if !defined(METRO_APP)
	case amf::AMF_MEMORY_DX9:
		res = m_deviceDX9.Init(true, adapterID, false, 1, 1);
		CHECK_AMF_ERROR_RETURN(res, L"m_deviceDX9.Init() failed");

		res = m_pContext->InitDX9(m_deviceDX9.GetDevice());
		CHECK_AMF_ERROR_RETURN(res, L"m_pContext->InitDX9() failed");
		break;
#endif//#if !defined(METRO_APP)
	case amf::AMF_MEMORY_DX11:
		res = m_deviceDX11.Init(adapterID);
		CHECK_AMF_ERROR_RETURN(res, L"m_deviceDX11.Init() failed");

		res = m_pContext->InitDX11(m_deviceDX11.GetDevice());
		CHECK_AMF_ERROR_RETURN(res, L"m_pContext->InitDX11() failed");
		break;
	}

	if (m_useOpenCLConverter)
	{
		res = m_deviceOpenCL.Init(m_deviceDX9.GetDevice(), m_deviceDX11.GetDevice(),NULL, NULL);
		CHECK_AMF_ERROR_RETURN(res, L"m_deviceOpenCL.Init() failed");

		res = m_pContext->InitOpenCL(m_deviceOpenCL.GetCommandQueue());
		CHECK_AMF_ERROR_RETURN(res, L"m_pContext->InitOpenCL() failed");
	}

	return res;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT DisplayDvrPipeline::InitVideo(amf::AMF_MEMORY_TYPE engineMemoryType, amf_int scaleWidth, amf_int scaleHeight,
	amf_int32 videoWidth, amf_int32 videoHeight)
{
	AMF_RESULT res = AMF_OK;

	// Get monitor ID
	amf_uint32 monitorID = 0;
	GetParam(PARAM_NAME_MONITORID, monitorID);

	// Init dvr capture component
	res = AMFCreateComponentDisplayCapture(m_pContext, &m_pDisplayCapture);
	res = m_pDisplayCapture->SetProperty(AMF_DISPLAYCAPTURE_MONITOR_INDEX, monitorID);
	CHECK_AMF_ERROR_RETURN(res, L"Failed to set Dvr component monitor ID");
	res = m_pDisplayCapture->SetProperty(AMF_DISPLAYCAPTURE_CURRENT_TIME_INTERFACE, m_pCurrentTime);
	CHECK_AMF_ERROR_RETURN(res, L"Failed to set Dvr component current time interface");
	res = m_pDisplayCapture->SetProperty(AMF_DISPLAYCAPTURE_FRAMERATE, kFrameRate);
	CHECK_AMF_ERROR_RETURN(res, L"Failed to set Dvr component frame rate");
	res = m_pDisplayCapture->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
	CHECK_AMF_ERROR_RETURN(res, L"Failed to make Dvr component");

	// Init converter
	res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, AMFVideoConverter, &m_pConverter);
	CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << AMFVideoConverter << L") failed");

	m_pConverter->SetProperty(AMF_VIDEO_CONVERTER_MEMORY_TYPE, 
		(m_useOpenCLConverter) ? amf::AMF_MEMORY_OPENCL : engineMemoryType);
	m_pConverter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_FORMAT, amf::AMF_SURFACE_NV12);
	m_pConverter->SetProperty(AMF_VIDEO_CONVERTER_OUTPUT_SIZE, AMFConstructSize(scaleWidth, scaleHeight));
	
	m_pConverter->Init(m_converterSurfaceFormat, videoWidth, videoHeight);

	// Init encoder
	m_szEncoderID = AMFVideoEncoderVCE_AVC;
	GetParamWString(DisplayDvrPipeline::PARAM_NAME_CODEC, m_szEncoderID);

	if (m_szEncoderID == AMFVideoEncoderVCE_AVC)
	{
		amf_int64 usage = 0;
		if (GetParam(AMF_VIDEO_ENCODER_USAGE, usage) == AMF_OK)
		{
			if (usage == amf_int64(AMF_VIDEO_ENCODER_USAGE_WEBCAM))
			{
				m_szEncoderID = AMFVideoEncoderVCE_SVC;
			}
		}
	}

	res = g_AMFFactory.GetFactory()->CreateComponent(m_pContext, m_szEncoderID.c_str(), &m_pEncoder);
	CHECK_AMF_ERROR_RETURN(res, L"g_AMFFactory.GetFactory()->CreateComponent(" << m_szEncoderID << L") failed");

	AMFRate frameRate = { kFrameRate, 1 };
	res = m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, frameRate);
	CHECK_AMF_ERROR_RETURN(res, L"Failed to set video encoder frame rate");

	// Usage is preset that will set many parameters
	PushParamsToPropertyStorage(this, ParamEncoderUsage, m_pEncoder);
	// override some usage parameters
	PushParamsToPropertyStorage(this, ParamEncoderStatic, m_pEncoder);

	res = m_pEncoder->Init(amf::AMF_SURFACE_NV12, videoWidth, videoHeight);
	CHECK_AMF_ERROR_RETURN(res, L"m_pEncoder->Init() failed");

	PushParamsToPropertyStorage(this, ParamEncoderDynamic, m_pEncoder);

	return res;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT DisplayDvrPipeline::InitAudio()
{
	AMF_RESULT res = AMF_OK;

	// Audio state
	amf_int64 codecID = 0;
	amf_int64 streamBitRate = 0;
	amf_int64 streamSampleRate = 0;
	amf_int64 streamChannels = 0;
	amf_int64 streamFormat = 0;
	amf_int64 streamLayout = 0;
	amf_int64 streamBlockAlign = 0;
	amf_int64 streamFrameSize = 0;
	amf::AMFInterfacePtr pExtradata;

	// Create the audio capture component
	res = AMFCreateComponentAudioCapture(m_pContext, &m_pAudioCapture);
	CHECK_AMF_ERROR_RETURN(res, L"Audio capture component creation failed");
	// Put the audio session component into loopback render mode
	res = m_pAudioCapture->SetProperty(AUDIOCAPTURE_SOURCE, false);
	CHECK_AMF_ERROR_RETURN(res, L"Audio capture component did not enter loopback render mode");
	// Set the current time interface
	res = m_pAudioCapture->SetProperty(AUDIOCAPTURE_CURRENT_TIME_INTERFACE, m_pCurrentTime);
	CHECK_AMF_ERROR_RETURN(res, L"Audio capture component failed to set current time interface");
	// Initialize the audio session component
	res = m_pAudioCapture->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
	CHECK_AMF_ERROR_RETURN(res, L"Audio capture component initialization failed");

	// Read the setup of the audio capture component so that its state can be
	// passed into the audio decoder
	m_pAudioCapture->GetProperty(AUDIOCAPTURE_CODEC, &codecID);
	m_pAudioCapture->GetProperty(AUDIOCAPTURE_BITRATE, &streamBitRate);
	m_pAudioCapture->GetProperty(AUDIOCAPTURE_SAMPLERATE, &streamSampleRate);
	m_pAudioCapture->GetProperty(AUDIOCAPTURE_CHANNELS, &streamChannels);
	m_pAudioCapture->GetProperty(AUDIOCAPTURE_FORMAT, &streamFormat);
	m_pAudioCapture->GetProperty(AUDIOCAPTURE_BLOCKALIGN, &streamBlockAlign);
	m_pAudioCapture->GetProperty(AUDIOCAPTURE_FRAMESIZE, &streamFrameSize);

	// Audio converter
	res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", FFMPEG_AUDIO_CONVERTER, &m_pAudioConverter);
	CHECK_AMF_ERROR_RETURN(res, L"LoadExternalComponent(" << FFMPEG_AUDIO_CONVERTER << L") failed");

	m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_BIT_RATE, streamBitRate);
	m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_SAMPLE_RATE, streamSampleRate);
	m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_CHANNELS, streamChannels);
	m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_SAMPLE_FORMAT, streamFormat);
	m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_CHANNEL_LAYOUT, kFFMPEG_AUDIO_LAYOUT_STEREO);
	m_pAudioConverter->SetProperty(AUDIO_CONVERTER_IN_AUDIO_BLOCK_ALIGN, streamBlockAlign);

	// Audio encoder
	res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", FFMPEG_AUDIO_ENCODER, &m_pAudioEncoder);
	CHECK_AMF_ERROR_RETURN(res, L"LoadExternalComponent(" << FFMPEG_AUDIO_ENCODER << L") failed");

	m_pAudioEncoder->SetProperty(AUDIO_ENCODER_AUDIO_CODEC_ID, kFFMPEG_AAC_CODEC_ID);
	res = m_pAudioEncoder->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
	CHECK_AMF_ERROR_RETURN(res, L"m_pAudioEncoder->Init() failed");

	m_pAudioEncoder->GetProperty(AUDIO_ENCODER_IN_AUDIO_SAMPLE_RATE, &streamSampleRate);
	m_pAudioEncoder->GetProperty(AUDIO_ENCODER_IN_AUDIO_CHANNELS, &streamChannels);
	m_pAudioEncoder->GetProperty(AUDIO_ENCODER_IN_AUDIO_SAMPLE_FORMAT, &streamFormat);
	m_pAudioEncoder->GetProperty(AUDIO_ENCODER_IN_AUDIO_CHANNEL_LAYOUT, &streamLayout);
	m_pAudioEncoder->GetProperty(AUDIO_ENCODER_IN_AUDIO_BLOCK_ALIGN, &streamBlockAlign);

	m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_BIT_RATE, streamBitRate);
	m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_SAMPLE_RATE, streamSampleRate);
	m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_CHANNELS, streamChannels);
	m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_SAMPLE_FORMAT, streamFormat);
	m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_CHANNEL_LAYOUT, streamLayout);
	m_pAudioConverter->SetProperty(AUDIO_CONVERTER_OUT_AUDIO_BLOCK_ALIGN, streamBlockAlign);

	res = m_pAudioConverter->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
	CHECK_AMF_ERROR_RETURN(res, L"m_pAudioConverter->Init() failed");

	return res;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT DisplayDvrPipeline::InitMuxer(
	amf_bool hasDDVideoStream, amf_bool hasSessionAudioStream,
	amf_int32& outVideoStreamMuxerIndex, amf_int32& outAudioStreamMuxerIndex)
{
	AMF_RESULT res = AMF_OK;

	amf::AMFComponentPtr  pMuxer;
	res = g_AMFFactory.LoadExternalComponent(m_pContext, FFMPEG_DLL_NAME, "AMFCreateComponentInt", FFMPEG_MUXER, &pMuxer);
	CHECK_AMF_ERROR_RETURN(res, L"AMFCreateComponent(" << FFMPEG_DEMUXER << L") failed");
	m_pMuxer = amf::AMFComponentExPtr(pMuxer);

	std::wstring outputPath = L"";
	res = GetParamWString(PARAM_NAME_OUTPUT, outputPath);
	CHECK_AMF_ERROR_RETURN(res, L"Output Path");
	m_pMuxer->SetProperty(FFMPEG_MUXER_PATH, outputPath.c_str());
	if (hasDDVideoStream)
	{
		m_pMuxer->SetProperty(FFMPEG_MUXER_ENABLE_VIDEO, true);
	}
	if (hasSessionAudioStream)
	{
		m_pMuxer->SetProperty(FFMPEG_MUXER_ENABLE_AUDIO, true);
	}

	amf_int32 inputs = m_pMuxer->GetInputCount();
	for (amf_int32 input = 0; input < inputs; input++)
	{
		amf::AMFInputPtr pInput;
		res = m_pMuxer->GetInput(input, &pInput);
		CHECK_AMF_ERROR_RETURN(res, L"m_pMuxer->GetInput() failed");

		amf_int64 eStreamType = MUXER_UNKNOWN;
		pInput->GetProperty(FFMPEG_MUXER_STREAM_TYPE, &eStreamType);

		if (eStreamType == MUXER_VIDEO)
		{
			outVideoStreamMuxerIndex = input;

			pInput->SetProperty(FFMPEG_MUXER_STREAM_ENABLED, true);
			amf_int32 bitrate = 0;
			if (m_szEncoderID == AMFVideoEncoderVCE_AVC || m_szEncoderID == AMFVideoEncoderVCE_SVC)
			{
#define AV_CODEC_ID_H264 28 // works with current FFmpeg only
				pInput->SetProperty(FFMPEG_MUXER_CODEC_ID, AV_CODEC_ID_H264); // default
				m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, &bitrate);
				pInput->SetProperty(FFMPEG_MUXER_BIT_RATE, bitrate);
				amf::AMFInterfacePtr pExtraData;
				m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_EXTRADATA, &pExtraData);
				pInput->SetProperty(FFMPEG_MUXER_EXTRA_DATA, pExtraData);

				AMFSize frameSize;
				m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, &frameSize);
				pInput->SetProperty(FFMPEG_MUXER_VIDEO_FRAMESIZE, frameSize);

				AMFRate frameRate;
				m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_FRAMERATE, &frameRate);
				pInput->SetProperty(FFMPEG_MUXER_VIDEO_FRAME_RATE, frameRate);
			}
			else
			{
#define AV_CODEC_ID_H265 174 // works with current FFmpeg only
				pInput->SetProperty(FFMPEG_MUXER_CODEC_ID, AV_CODEC_ID_H265);
				m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, &bitrate);
				pInput->SetProperty(FFMPEG_MUXER_BIT_RATE, bitrate);
				amf::AMFInterfacePtr pExtraData;
				m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_EXTRADATA, &pExtraData);
				pInput->SetProperty(FFMPEG_MUXER_EXTRA_DATA, pExtraData);

				AMFSize frameSize;
				m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, &frameSize);
				pInput->SetProperty(FFMPEG_MUXER_VIDEO_FRAMESIZE, frameSize);

				AMFRate frameRate;
				m_pEncoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, &frameRate);
				pInput->SetProperty(FFMPEG_MUXER_VIDEO_FRAME_RATE, frameRate);
			}
		}
		else if (eStreamType == MUXER_AUDIO)
		{
			outAudioStreamMuxerIndex = input;
			pInput->SetProperty(FFMPEG_MUXER_STREAM_ENABLED, true);

			amf_int64 codecID = 0;
			amf_int64 streamBitRate = 0;
			amf_int64 streamSampleRate = 0;
			amf_int64 streamChannels = 0;
			amf_int64 streamFormat = 0;
			amf_int64 streamLayout = 0;
			amf_int64 streamBlockAlign = 0;
			amf_int64 streamFrameSize = 0;

			m_pAudioEncoder->GetProperty(AUDIO_ENCODER_AUDIO_CODEC_ID, &codecID);
			m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_BIT_RATE, &streamBitRate);
			m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_SAMPLE_RATE, &streamSampleRate);
			m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_CHANNELS, &streamChannels);
			m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_SAMPLE_FORMAT, &streamFormat);
			m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_CHANNEL_LAYOUT, &streamLayout);
			m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_BLOCK_ALIGN, &streamBlockAlign);
			m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_FRAME_SIZE, &streamFrameSize);

			amf::AMFInterfacePtr pExtraData;
			m_pAudioEncoder->GetProperty(AUDIO_ENCODER_OUT_AUDIO_EXTRA_DATA, &pExtraData);
			pInput->SetProperty(FFMPEG_MUXER_EXTRA_DATA, pExtraData);

			pInput->SetProperty(FFMPEG_MUXER_CODEC_ID, codecID);
			pInput->SetProperty(FFMPEG_MUXER_BIT_RATE, streamBitRate);
			pInput->SetProperty(FFMPEG_MUXER_AUDIO_SAMPLE_RATE, streamSampleRate);
			pInput->SetProperty(FFMPEG_MUXER_AUDIO_CHANNELS, streamChannels);
			pInput->SetProperty(FFMPEG_MUXER_AUDIO_SAMPLE_FORMAT, streamFormat);
			pInput->SetProperty(FFMPEG_MUXER_AUDIO_CHANNEL_LAYOUT, streamLayout);
			pInput->SetProperty(FFMPEG_MUXER_AUDIO_BLOCK_ALIGN, streamBlockAlign);
			pInput->SetProperty(FFMPEG_MUXER_AUDIO_FRAME_SIZE, streamFrameSize);
		}
	}

	res = m_pMuxer->Init(amf::AMF_SURFACE_UNKNOWN, 0, 0);
	CHECK_AMF_ERROR_RETURN(res, L"m_pMuxer->Init() failed");

	return res;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT DisplayDvrPipeline::Init()
{
	// Shut down an running pipeline
	Terminate();

	AMF_RESULT res = AMF_OK;

	//---------------------------------------------------------------------------------------------
	// State setup and declarations
	amf_bool hasDDVideoStream = false;
	amf_bool hasSessionAudioStream = false;

	amf_int32 videoWidth = 1920;
	amf_int32 videoHeight = 1080;
	// We are forcing into a texture width and height that most
	// GPUs can handle efficiently
#ifdef NO_OPTIMIZE
	GetParam(PARAM_NAME_VIDEO_WIDTH, videoWidth);
	GetParam(PARAM_NAME_VIDEO_HEIGHT, videoHeight);
#else
	SetParam(PARAM_NAME_VIDEO_WIDTH, videoWidth);
	SetParam(PARAM_NAME_VIDEO_HEIGHT, videoHeight);
#endif

	amf_int scaleWidth = 0;    // if 0 - no scaling
	amf_int scaleHeight = 0;   // if 0 - no scaling

	amf_uint32 adapterID = 0;
	GetParam(PARAM_NAME_ADAPTERID, adapterID);

	// The duplicate display functionality used by the DisplayDvr
	// component requires DX11
	std::wstring engineStr = L"DX11";
	amf::AMF_MEMORY_TYPE engineMemoryType = amf::AMF_MEMORY_DX11;

	//---------------------------------------------------------------------------------------------
	// Init context and devices
	res = InitContext(engineStr, engineMemoryType, adapterID);
	if (AMF_OK != res)
	{
		return res;
	}

	//---------------------------------------------------------------------------------------------
	// Init video except the muxer
	res = InitVideo(engineMemoryType, scaleWidth, scaleHeight, videoWidth, videoHeight);
	if (AMF_OK == res)
	{
		hasDDVideoStream = true;
	}
	else if (AMF_UNEXPECTED == res)
	{
		SetErrorMessage(L"Unsupported OS.");
		return AMF_FAIL;
	}

	//---------------------------------------------------------------------------------------------
	// Init audio except the muxer. If audio fails to init then we still allow video to 
	// be captured.
	res = InitAudio();
	if (AMF_OK == res)
	{
		hasSessionAudioStream = true;
	}

	// Check that we have at least video
	if (!hasDDVideoStream)
	{
		SetErrorMessage(L"No video stream available.");
		return AMF_FAIL;
	}

	//---------------------------------------------------------------------------------------------
	// Init muxer which brings audio and video together
	res = InitMuxer(hasDDVideoStream, hasSessionAudioStream, m_outVideoStreamMuxerIndex, m_outAudioStreamMuxerIndex);
	if (AMF_OK != res)
	{
		return res;
	}

	return ConnectPipeline();
}

AMF_RESULT DisplayDvrPipeline::ConnectPipeline()
{
	amf_int frameParameterFreq = 0;
	amf_int dynamicParameterFreq = 0;

	// Connect components of the video pipeline together
	PipelineElementPtr pPipelineElementVideoEncoder =
		PipelineElementPtr(new PipelineElementEncoder(m_pEncoder, this, frameParameterFreq, dynamicParameterFreq));
	PipelineElementPtr pPipelineElementMuxer =
		PipelineElementPtr(new AMFComponentExElement(m_pMuxer));

	Connect(PipelineElementPtr(new AMFComponentElementDisplayCaptureInterceptor(this, m_pDisplayCapture)), 1, CT_Direct);
	AMFComponentElementConverterInterceptor* interceptor = new AMFComponentElementConverterInterceptor(this, m_pConverter);
	Connect(PipelineElementPtr(interceptor), 4, CT_ThreadQueue);
	m_converterInterceptor = interceptor;
	Connect(pPipelineElementVideoEncoder, 10, CT_ThreadQueue);
	Connect(pPipelineElementMuxer, m_outVideoStreamMuxerIndex, pPipelineElementVideoEncoder, 0, 10, CT_ThreadQueue);

	if (m_outAudioStreamMuxerIndex >= 0)
	{
		// Connect components of the audio pipeline together
		PipelineElementPtr pPipelineElementAudioEncoder =
			PipelineElementPtr(new AMFComponentElement(m_pAudioEncoder));

		Connect(PipelineElementPtr(new AMFComponentElement(m_pAudioCapture)), 100, CT_ThreadQueue);
		Connect(PipelineElementPtr(new AMFComponentElement(m_pAudioConverter)), 4, CT_ThreadQueue);
		Connect(pPipelineElementAudioEncoder, 10, CT_Direct);
		Connect(pPipelineElementMuxer, m_outAudioStreamMuxerIndex, pPipelineElementAudioEncoder, 0, 10, CT_ThreadQueue);
	}

	return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT DisplayDvrPipeline::Stop()
{
	Pipeline::Stop();

	if (m_pAudioCapture != NULL)
	{
		m_pAudioCapture->Terminate();
		m_pAudioCapture.Release();
	}
	if (m_pAudioDecoder != NULL)
	{
		m_pAudioDecoder->Terminate();
		m_pAudioDecoder.Release();
	}
	if (m_pAudioConverter != NULL)
	{
		m_pAudioConverter->Terminate();
		m_pAudioConverter.Release();
	}
	if (m_pAudioEncoder != NULL)
	{
		m_pAudioEncoder->Terminate();
		m_pAudioEncoder.Release();
	}
	if (m_pConverter != NULL)
	{
		m_pConverter->Terminate();
		m_pConverter.Release();
	}
	if (m_pDisplayCapture != NULL)
	{
		m_pDisplayCapture->Terminate();
		m_pDisplayCapture.Release();
	}
	if (m_pEncoder != NULL)
	{
		m_pEncoder->Terminate();
		m_pEncoder.Release();
	}
	if (m_pMuxer != NULL)
	{
		m_pMuxer->Terminate();
		m_pMuxer.Release();
	}
	if (m_pContext != NULL)
	{
		m_pContext->Terminate();
		m_pContext.Release();
	}
#if !defined(METRO_APP)
	m_deviceDX9.Terminate();
#endif // !defined(METRO_APP)
	m_deviceDX11.Terminate();

	if (m_useOpenCLConverter)
	{
		m_deviceOpenCL.Terminate();
	}

	return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
void DisplayDvrPipeline::OnParamChanged(const wchar_t* name)
{
	if (m_pMuxer == NULL)
	{
		return;
	}
	if (name != NULL)
	{
		if (0 != wcscmp(name, PARAM_NAME_OUTPUT))
		{
			const wchar_t* value;
			GetParamWString(name, value);
			m_pMuxer->SetProperty(FFMPEG_MUXER_PATH, value);
		}
	}
}

//-------------------------------------------------------------------------------------------------
amf::AMF_SURFACE_FORMAT DisplayDvrPipeline::GetConverterFormat() const
{
	amf::AMFLock lock(&m_sync);

	return m_converterSurfaceFormat;
}

//-------------------------------------------------------------------------------------------------
void DisplayDvrPipeline::SetConverterFormat(amf::AMF_SURFACE_FORMAT format)
{
	amf::AMFLock lock(&m_sync);

	m_converterSurfaceFormat = format;
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT DisplayDvrPipeline::SwitchConverterFormat(amf::AMF_SURFACE_FORMAT format)
{
	amf::AMFLock lock(&m_sync);

	AMF_RESULT res = AMF_FAIL;
	if (format != m_converterSurfaceFormat)
	{
		m_converterSurfaceFormat = format;
		// Terminate first
		((AMFComponentElementConverterInterceptor*)m_converterInterceptor)->SetBlock(true);
		m_pConverter->Terminate();
		// Get texture width and height
		amf_int32 videoWidth = 1920;
		amf_int32 videoHeight = 1080;
		SetParam(PARAM_NAME_VIDEO_WIDTH, videoWidth);
		SetParam(PARAM_NAME_VIDEO_HEIGHT, videoHeight);
		// Init the converter
		m_pConverter->Init(m_converterSurfaceFormat, videoWidth, videoHeight);
		((AMFComponentElementConverterInterceptor*)m_converterInterceptor)->SetBlock(false);
		// 
		res = AMF_OK;
	}
	return res;
}

