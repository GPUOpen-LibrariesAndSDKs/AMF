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

#include "DeckLinkMediaImpl.h"
#include <atlbase.h>
#include <d3d11.h>

#define AMF_FACILITY L"DeckLinkMediaImpl"


using namespace amf;

extern "C"
{
    AMF_RESULT AMF_CDECL_CALL AMFCreateCaptureManagerDeckLinkMedia(amf::AMFContext* pContext, amf::AMFCaptureManager** ppManager)
    {
        *ppManager = new amf::AMFInterfaceMultiImpl< amf::DLCaptureManagerImpl, amf::AMFCaptureManager, amf::AMFContext*>(pContext);
        (*ppManager)->Acquire();
        return AMF_OK;
    }
}
//-------------------------------------------------------------------------------------------------
DLCaptureManagerImpl::DLCaptureManagerImpl(amf::AMFContext* pContext) :
m_pContext(pContext)
{
}
//-------------------------------------------------------------------------------------------------
DLCaptureManagerImpl::~DLCaptureManagerImpl()
{
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT          AMF_STD_CALL DLCaptureManagerImpl::Update()
{
    HRESULT hr = S_OK;

    m_Inputs.clear();

    CComPtr<IDeckLinkIterator>  pDLIterator;
    hr = CoCreateInstance(CLSID_CDeckLinkIterator, NULL, CLSCTX_ALL, IID_IDeckLinkIterator, (void**)&pDLIterator);
    if(FAILED(hr))
    {
        AMFTraceInfo(AMF_FACILITY, L"CoCreateInstance(CLSID_CDeckLinkIterator) failed", AMFFormatHResult(hr).c_str());
        return AMF_OK;
    }
    while(true)
    {
        CComPtr<IDeckLink> pDL;
        if(pDLIterator->Next(&pDL) != S_OK)
        {
            break;
        }

        CComQIPtr<IDeckLinkAttributes>    deckLinkAttributes(pDL);
        if(deckLinkAttributes == NULL)
        {
            AMFTraceWarning(AMF_FACILITY, L"Could not obtain the IDeckLinkAttributes interface ");
            continue;
        }
        BOOL supportsFullDuplex = FALSE;
        if (deckLinkAttributes->GetFlag(BMDDeckLinkSupportsFullDuplex, &supportsFullDuplex) != S_OK)
        {
            supportsFullDuplex = FALSE;
        }
        // at this point we need just input
        CComQIPtr<IDeckLinkInput> input(pDL);

        if(input != NULL)
        {
            m_Inputs.push_back(input);
        }
    }
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
amf_int32           AMF_STD_CALL DLCaptureManagerImpl::GetDeviceCount()
{
    return (amf_int32)m_Inputs.size();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT          AMF_STD_CALL DLCaptureManagerImpl::GetDevice(amf_int32 index,AMFCaptureDevice **pDevice)
{
    AMF_RETURN_IF_FALSE(index < amf_int32(m_Inputs.size()), AMF_INVALID_ARG, L"Invlid device index %d", (int)index);
    AMFDeckLinkDeviceImplPtr device = new amf::AMFInterfaceMultiImpl< amf::AMFDeckLinkDeviceImpl, amf::AMFCaptureDevice, amf::AMFContext*, IDeckLinkInput*>(m_pContext, m_Inputs[index]);
    AMF_RESULT res = device->UpdateFromDeckLink();
    AMF_RETURN_IF_FAILED(res, L"InitDeckLink() failed");
    *pDevice = device.Detach();
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
/*
static const AMFEnumDescriptionEntry AMF_DECKLINK_DISPLAY_MODE_ENUM_DESC[] =
{
    { AMF_DECKLINK_DISPLAY_MODE_UNKNOWN         , L"bmdModeUnknown      "},
    { AMF_DECKLINK_DISPLAY_MODE_NTSC            , L"bmdModeNTSC         "},
    { AMF_DECKLINK_DISPLAY_MODE_NTSC2398        , L"bmdModeNTSC2398     "},
    { AMF_DECKLINK_DISPLAY_MODE_PAL             , L"bmdModePAL          "},
    { AMF_DECKLINK_DISPLAY_MODE_NTSCp           , L"bmdModeNTSCp        "},
    { AMF_DECKLINK_DISPLAY_MODE_PALp            , L"bmdModePALp         "},
    { AMF_DECKLINK_DISPLAY_MODE_HD1080p2398     , L"bmdModeHD1080p2398  "},
    { AMF_DECKLINK_DISPLAY_MODE_HD1080p24       , L"bmdModeHD1080p24    "},
    { AMF_DECKLINK_DISPLAY_MODE_HD1080p25       , L"bmdModeHD1080p25    "},
    { AMF_DECKLINK_DISPLAY_MODE_HD1080p2997     , L"bmdModeHD1080p2997  "},
    { AMF_DECKLINK_DISPLAY_MODE_HD1080p30       , L"bmdModeHD1080p30    "},
    { AMF_DECKLINK_DISPLAY_MODE_HD1080i50       , L"bmdModeHD1080i50    "},
    { AMF_DECKLINK_DISPLAY_MODE_HD1080i5994     , L"bmdModeHD1080i5994  "},
    { AMF_DECKLINK_DISPLAY_MODE_HD1080i6000     , L"bmdModeHD1080i6000  "},
    { AMF_DECKLINK_DISPLAY_MODE_HD1080p50       , L"bmdModeHD1080p50    "},
    { AMF_DECKLINK_DISPLAY_MODE_HD1080p5994     , L"bmdModeHD1080p5994  "},
    { AMF_DECKLINK_DISPLAY_MODE_HD1080p6000     , L"bmdModeHD1080p6000  "},
    { AMF_DECKLINK_DISPLAY_MODE_HD720p50        , L"bmdModeHD720p50     "},
    { AMF_DECKLINK_DISPLAY_MODE_HD720p5994      , L"bmdModeHD720p5994   "},
    { AMF_DECKLINK_DISPLAY_MODE_HD720p60        , L"bmdModeHD720p60     "},
    { AMF_DECKLINK_DISPLAY_MODE_2k2398          , L"bmdMode2k2398       "},
    { AMF_DECKLINK_DISPLAY_MODE_2k24            , L"bmdMode2k24         "},
    { AMF_DECKLINK_DISPLAY_MODE_2k25            , L"bmdMode2k25         "},
    { AMF_DECKLINK_DISPLAY_MODE_2kDCI2398       , L"bmdMode2kDCI2398    "},
    { AMF_DECKLINK_DISPLAY_MODE_2kDCI24         , L"bmdMode2kDCI24      "},
    { AMF_DECKLINK_DISPLAY_MODE_2kDCI25         , L"bmdMode2kDCI25      "},
    { AMF_DECKLINK_DISPLAY_MODE_4K2160p2398     , L"bmdMode4K2160p2398  "},
    { AMF_DECKLINK_DISPLAY_MODE_4K2160p24       , L"bmdMode4K2160p24    "},
    { AMF_DECKLINK_DISPLAY_MODE_4K2160p25       , L"bmdMode4K2160p25    "},
    { AMF_DECKLINK_DISPLAY_MODE_4K2160p2997     , L"bmdMode4K2160p2997  "},
    { AMF_DECKLINK_DISPLAY_MODE_4K2160p30       , L"bmdMode4K2160p30    "},
    { AMF_DECKLINK_DISPLAY_MODE_4K2160p50       , L"bmdMode4K2160p50    "},
    { AMF_DECKLINK_DISPLAY_MODE_4K2160p5994     , L"bmdMode4K2160p5994  "},
    { AMF_DECKLINK_DISPLAY_MODE_4K2160p60       , L"bmdMode4K2160p60    "},
    { AMF_DECKLINK_DISPLAY_MODE_4kDCI2398       , L"bmdMode4kDCI2398    "},
    { AMF_DECKLINK_DISPLAY_MODE_4kDCI24         , L"bmdMode4kDCI24      "},
    { AMF_DECKLINK_DISPLAY_MODE_4kDCI25         , L"bmdMode4kDCI25      "},
    {0, NULL}

};
struct DisplayModeMap
{
    AMF_DECKLINK_DISPLAY_MODE_ENUM  modeAMF;
    BMDDisplayMode                  modeDL;
    AMFSize                         frameSize;
};
static DisplayModeMap s_displayModeMap[] =
{
    { AMF_DECKLINK_DISPLAY_MODE_UNKNOWN         , bmdModeUnknown      ,{0, 0}},
    { AMF_DECKLINK_DISPLAY_MODE_NTSC            , bmdModeNTSC         ,{ 720,  486}},
    { AMF_DECKLINK_DISPLAY_MODE_NTSC2398        , bmdModeNTSC2398     ,{ 720,  486}},
    { AMF_DECKLINK_DISPLAY_MODE_PAL             , bmdModePAL          ,{ 720,  576}},
    { AMF_DECKLINK_DISPLAY_MODE_NTSCp           , bmdModeNTSCp        ,{ 720,  486}},
    { AMF_DECKLINK_DISPLAY_MODE_PALp            , bmdModePALp         ,{ 720,  486}},
    { AMF_DECKLINK_DISPLAY_MODE_HD1080p2398     , bmdModeHD1080p2398  ,{1920, 1080}},
    { AMF_DECKLINK_DISPLAY_MODE_HD1080p24       , bmdModeHD1080p24    ,{1920, 1080}},
    { AMF_DECKLINK_DISPLAY_MODE_HD1080p25       , bmdModeHD1080p25    ,{1920, 1080}},
    { AMF_DECKLINK_DISPLAY_MODE_HD1080p2997     , bmdModeHD1080p2997  ,{1920, 1080}},
    { AMF_DECKLINK_DISPLAY_MODE_HD1080p30       , bmdModeHD1080p30    ,{1920, 1080}},
    { AMF_DECKLINK_DISPLAY_MODE_HD1080i50       , bmdModeHD1080i50    ,{1920, 1080}},
    { AMF_DECKLINK_DISPLAY_MODE_HD1080i5994     , bmdModeHD1080i5994  ,{1920, 1080}},
    { AMF_DECKLINK_DISPLAY_MODE_HD1080i6000     , bmdModeHD1080i6000  ,{1920, 1080}},
    { AMF_DECKLINK_DISPLAY_MODE_HD1080p50       , bmdModeHD1080p50    ,{1920, 1080}},
    { AMF_DECKLINK_DISPLAY_MODE_HD1080p5994     , bmdModeHD1080p5994  ,{1920, 1080}},
    { AMF_DECKLINK_DISPLAY_MODE_HD1080p6000     , bmdModeHD1080p6000  ,{1920, 1080}},
    { AMF_DECKLINK_DISPLAY_MODE_HD720p50        , bmdModeHD720p50     ,{1280, 720}},
    { AMF_DECKLINK_DISPLAY_MODE_HD720p5994      , bmdModeHD720p5994   ,{1280, 720}},
    { AMF_DECKLINK_DISPLAY_MODE_HD720p60        , bmdModeHD720p60     ,{1280, 720}},
    { AMF_DECKLINK_DISPLAY_MODE_2k2398          , bmdMode2k2398       ,{2048, 1556}},
    { AMF_DECKLINK_DISPLAY_MODE_2k24            , bmdMode2k24         ,{2048, 1556}},
    { AMF_DECKLINK_DISPLAY_MODE_2k25            , bmdMode2k25         ,{2048, 1556}},
    { AMF_DECKLINK_DISPLAY_MODE_2kDCI2398       , bmdMode2kDCI2398    ,{2048, 1080}},
    { AMF_DECKLINK_DISPLAY_MODE_2kDCI24         , bmdMode2kDCI24      ,{2048, 1080}},
    { AMF_DECKLINK_DISPLAY_MODE_2kDCI25         , bmdMode2kDCI25      ,{2048, 1080}},
    { AMF_DECKLINK_DISPLAY_MODE_4K2160p2398     , bmdMode4K2160p2398  ,{3840, 2160}},
    { AMF_DECKLINK_DISPLAY_MODE_4K2160p24       , bmdMode4K2160p24    ,{3840, 2160}},
    { AMF_DECKLINK_DISPLAY_MODE_4K2160p25       , bmdMode4K2160p25    ,{3840, 2160}},
    { AMF_DECKLINK_DISPLAY_MODE_4K2160p2997     , bmdMode4K2160p2997  ,{3840, 2160}},
    { AMF_DECKLINK_DISPLAY_MODE_4K2160p30       , bmdMode4K2160p30    ,{3840, 2160}},
    { AMF_DECKLINK_DISPLAY_MODE_4K2160p50       , bmdMode4K2160p50    ,{3840, 2160}},
    { AMF_DECKLINK_DISPLAY_MODE_4K2160p5994     , bmdMode4K2160p5994  ,{3840, 2160}},
    { AMF_DECKLINK_DISPLAY_MODE_4K2160p60       , bmdMode4K2160p60    ,{3840, 2160}},
    { AMF_DECKLINK_DISPLAY_MODE_4kDCI2398       , bmdMode4kDCI2398    ,{4096, 2160}},
    { AMF_DECKLINK_DISPLAY_MODE_4kDCI24         , bmdMode4kDCI24      ,{4096, 2160}},
    { AMF_DECKLINK_DISPLAY_MODE_4kDCI25         , bmdMode4kDCI25      ,{4096, 2160}},
};
//-------------------------------------------------------------------------------------------------
static BMDDisplayMode   FromAMFToDLDisplayMode(AMF_DECKLINK_DISPLAY_MODE_ENUM displayModeAMF)
{
    for(int i = 0; i < amf_countof(s_displayModeMap); i++)
    {
        if(s_displayModeMap[i].modeAMF == displayModeAMF)
        {
            return s_displayModeMap[i].modeDL;
        }
    }
    return bmdModeUnknown;
}
//-------------------------------------------------------------------------------------------------
static AMFSize FromDisplayModeToDimensions(AMF_DECKLINK_DISPLAY_MODE_ENUM displayModeAMF)
{
    for(int i = 0; i < amf_countof(s_displayModeMap); i++)
    {
        if(s_displayModeMap[i].modeAMF == displayModeAMF)
        {
            return s_displayModeMap[i].frameSize;
        }
    }
    return AMFSize();
}
*/
//-------------------------------------------------------------------------------------------------
static const AMFEnumDescriptionEntry AMF_CAPTURE_VIDEO_FORMAT_DESC[] =
{

    { AMF_SURFACE_Y210,       L"bmdFormat10BitYUV" },
    { AMF_SURFACE_UYVY,       L"bmdFormat8BitYUV"  },
    {AMF_SURFACE_ARGB,        L"bmdFormat8BitARGB" },
    {AMF_SURFACE_BGRA,        L"bmdFormat8BitBGRA" },
                                                    //bmdFormat10BitRGB
                                                    //bmdFormat12BitRGB
                                                    //bmdFormat12BitRGBLE
                                                    //bmdFormat10BitRGBXLE
                                                    //bmdFormat10BitRGBX
                                                    //bmdFormatH265
                                                    //bmdFormatDNxHR
    {0, NULL}
};
struct VideoFormatMap
{
    AMF_SURFACE_FORMAT  formatAMF;
    BMDPixelFormat      formatDL;
};
static VideoFormatMap s_VideoFormatMap[] =
{
    { AMF_SURFACE_Y210,       bmdFormat10BitYUV },
    { AMF_SURFACE_UYVY,       bmdFormat8BitYUV  },
    {AMF_SURFACE_ARGB,        bmdFormat8BitARGB },
    {AMF_SURFACE_BGRA,        bmdFormat8BitBGRA },
                                                    //bmdFormat10BitRGB
                                                    //bmdFormat12BitRGB
                                                    //bmdFormat12BitRGBLE
                                                    //bmdFormat10BitRGBXLE
                                                    //bmdFormat10BitRGBX
                                                    //bmdFormatH265
                                                    //bmdFormatDNxHR
};
//-------------------------------------------------------------------------------------------------
static BMDPixelFormat   FromAMFToDLFormat(AMF_SURFACE_FORMAT  formatAMF)
{
    for(int i = 0; i < amf_countof(s_VideoFormatMap); i++)
    {
        if(s_VideoFormatMap[i].formatAMF == formatAMF)
        {
            return s_VideoFormatMap[i].formatDL;
        }
    }
    return (BMDPixelFormat)0;
}

//-------------------------------------------------------------------------------------------------
static const AMFEnumDescriptionEntry AMF_CAPTURE_VIDEO_MEMORY_TYPE_DESC[] =
{
    {AMF_MEMORY_DX11, L"DX11"},
    {0, NULL}
};
//-------------------------------------------------------------------------------------------------
static const AMFEnumDescriptionEntry AMF_CAPTURE_AUDIO_FORMAT_DESC[] =
{
    {AMFAF_S16 , L"S16" },
    {AMFAF_S32 , L"S32" },
    {0, NULL}
};
BMDAudioSampleType FromAMFToDLAudioType(AMF_AUDIO_FORMAT  typeAMF)
{
    switch(typeAMF)
    {
    case AMFAF_S16: return bmdAudioSampleType16bitInteger;
    case AMFAF_S32: return bmdAudioSampleType32bitInteger;
    }
    return BMDAudioSampleType(0);
}

static const AMFEnumDescriptionEntry  AMF_CAPTURE_CHANNELS_DESC[] =
{
    {2 , L"2" },
    {8 , L"8" },
    {16 , L"16" },
    {0, NULL}
};
//-------------------------------------------------------------------------------------------------
AMFDeckLinkDeviceImpl::AMFDeckLinkDeviceImpl(AMFContext* pContext, IDeckLinkInput *input) :
    m_pContext(pContext),
    m_pDLInput(input),
    m_VideoMemoryAllocator(this),
    m_CaptureCallback(this),
    m_bEof(false),
    m_eAudioFormat(AMFAF_S16),
    m_iSampleRate(48000),
    m_iChannels(2),
    m_ptsStartAudio(-1LL),
    m_ptsStartVideo(-1LL),
    m_eVideoMemoryType(AMF_MEMORY_DX11)
{
    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoWString(AMF_CAPTURE_DEVICE_NAME, AMF_CAPTURE_DEVICE_NAME, L"", false),
        AMFPropertyInfoInt64(AMF_CAPTURE_DEVICE_TYPE, AMF_CAPTURE_DEVICE_TYPE,  AMF_CAPTURE_DEVICE_SDI, AMF_CAPTURE_DEVICE_SDI, AMF_CAPTURE_DEVICE_SDI, false),
    AMFPrimitivePropertyInfoMapEnd

}
//-------------------------------------------------------------------------------------------------
AMFDeckLinkDeviceImpl::~AMFDeckLinkDeviceImpl()
{

}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL  AMFDeckLinkDeviceImpl::Init(AMF_SURFACE_FORMAT /* format */, amf_int32 /* width */, amf_int32 /* height */)
{
    AMF_RESULT   res = AMF_OK;

    Terminate();

    AMFLock lock(&m_sect);

    res = InitDeckLink();
    AMF_RETURN_IF_FAILED(res, L"InitDeckLink() failed");

    m_bEof = false;
    m_ptsStartAudio = -1LL;
    m_ptsStartVideo = -1LL;

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL  AMFDeckLinkDeviceImpl::UpdateFromDeckLink()
{
    AMF_RETURN_IF_FALSE(m_pDLInput != NULL, AMF_NOT_INITIALIZED, L"DeckLink device is not set");

    CComQIPtr<IDeckLinkAttributes>    deckLinkAttributes(m_pDLInput);

    AMF_RETURN_IF_FALSE(deckLinkAttributes != NULL, AMF_FAIL, L"QueryInterface(IDeckLinkAttributes) failed");

    BSTR pName = NULL;
    deckLinkAttributes->GetString(BMDDeckLinkModelName, &pName);
    if(pName != NULL)
    {
        SetProperty(AMF_CAPTURE_DEVICE_NAME, (wchar_t*)pName);
        ::SysFreeString(pName);
    }

    CComPtr<IDeckLinkDisplayModeIterator>  pModeIterator;
    m_pDLInput->GetDisplayModeIterator(&pModeIterator);
    AMF_RETURN_IF_FALSE(pModeIterator != NULL, AMF_FAIL, L"GetDisplayModeIterator() failed");

    m_SupportedModes.clear();

    while(true)
    {
        CComPtr<IDeckLinkDisplayMode>  pMode;
        pModeIterator->Next(&pMode);
        if(pMode == NULL)
        {
            break;
        }
        Mode mode = {};
        mode.mode = pMode->GetDisplayMode();
        mode.framesize.width = (amf_int32)pMode->GetWidth();
        mode.framesize.height = (amf_int32)pMode->GetHeight();

        BMDTimeValue frameDuration = 0;
        BMDTimeScale timeScale = 0;

        pMode->GetFrameRate(&frameDuration, &timeScale);

        mode.framerate.num = (amf_uint32)timeScale;
        mode.framerate.den = (amf_uint32)frameDuration;

        mode.field = pMode->GetFieldDominance();
        mode.flags = pMode->GetFlags();

        m_SupportedModes.push_back(mode);
    }
    AMF_RESULT res = InitStreams();
    AMF_RETURN_IF_FAILED(res, L"InitStreams() failed");

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMFOutputPtr AMF_STD_CALL AMFDeckLinkDeviceImpl::GetStream(AMF_STREAM_TYPE_ENUM eType)
{
    for(amf_vector<AMFOutputPtr>::iterator it = m_OutputStreams.begin(); it != m_OutputStreams.end(); it++)
    {
        amf_int64 currentType = AMF_STREAM_UNKNOWN;
        (*it)->GetProperty(AMF_STREAM_TYPE, &currentType);
        if(currentType == eType)
        {
            return *it;
        }
    }
    return AMFOutputPtr();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL  AMFDeckLinkDeviceImpl::InitDeckLink()
{
    AMF_RESULT res = AMF_OK;
    HRESULT hr = S_OK;

    AMF_RETURN_IF_FALSE(m_pDLInput != NULL, AMF_NOT_FOUND, L"DeckLink input was not found");

    // find best video mode
    AMFOutputPtr pVideoStream = GetStream(AMF_STREAM_VIDEO);
    AMF_RETURN_IF_FALSE(pVideoStream != NULL, AMF_NOT_FOUND, L"Video stream was not found");

    bool bEnableVideo = false;
    pVideoStream->GetProperty(AMF_STREAM_ENABLED, &bEnableVideo);

//    hr = m_pDLInput->SetCallback(&m_CaptureCallback);
//    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"SetCallback() failed");

    if(bEnableVideo)
    {
        res = AMFVideoTransfer::CreateVideoTransfer(m_pContext, m_eVideoMemoryType , &m_pVideoTransfer);
        AMF_RETURN_IF_FAILED(res, L"CreateVideoTransfer() failed");

        AMFSize framesize = {};
        pVideoStream->GetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, &framesize);
        AMFRate framerate = {};
        pVideoStream->GetProperty(AMF_STREAM_VIDEO_FRAME_RATE, &framerate);

        amf_int64 memoryType = AMF_MEMORY_DX11;
        pVideoStream->GetProperty(AMF_STREAM_VIDEO_MEMORY_TYPE, &memoryType);
        m_eVideoMemoryType = AMF_MEMORY_TYPE(memoryType);

        BMDDisplayMode      mode = bmdModeUnknown;
        for(amf_vector<Mode>::iterator it = m_SupportedModes.begin(); it != m_SupportedModes.end(); it++)
        {
            if(it->framesize.width == framesize.width && it->framesize.height == framesize.height &&

                fabs(double(it->framerate.num) / it->framerate.den  - double (framerate.num) / framerate.den) < 0.00001 &&
                it->field == bmdProgressiveFrame // hard - coded progressive for now
                // TODO it->flag ???
                )
            {
                mode = it->mode;
                AMFTraceInfo(AMF_FACILITY, L"InitDeckLink() %dx%d@%5.2f", it->framesize.width, it->framesize.height, (double)it->framerate.num / it->framerate.den );

                break;
            }
        }
        AMF_RETURN_IF_FALSE(mode != bmdModeUnknown, AMF_NOT_FOUND, L"Video stream display mode was not found");

        CComQIPtr<IDeckLinkAttributes>    deckLinkAttributes(m_pDLInput);
        BOOL supportsFormatDetection= FALSE;
        deckLinkAttributes->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &supportsFormatDetection);


        amf_int64 formatAMF = AMF_SURFACE_UNKNOWN;
        pVideoStream->GetProperty(AMF_STREAM_VIDEO_FORMAT, &formatAMF);
        BMDPixelFormat pixelFormat = FromAMFToDLFormat(AMF_SURFACE_FORMAT(formatAMF));

//        pixelFormat = bmdFormat8BitYUV;

        BMDDisplayModeSupport result = bmdDisplayModeNotSupported;

        BMDVideoInputFlags inputFlags = bmdVideoInputFlagDefault;
        if(supportsFormatDetection)
        {
            inputFlags |= bmdVideoInputEnableFormatDetection;
        }

        CComPtr<IDeckLinkDisplayMode>  pMode;
        hr = m_pDLInput->DoesSupportVideoMode(mode, pixelFormat, inputFlags, &result, &pMode);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"DoesSupportVideoMode() failed");

        hr = m_pDLInput->SetVideoInputFrameMemoryAllocator(&m_VideoMemoryAllocator);
        //workaround for the auto replay on some systems
        AMF_ASSERT(hr == AMF_OK, L"SetVideoInputFrameMemoryAllocator() failed");
        //        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"SetVideoInputFrameMemoryAllocator() failed");

        hr = m_pDLInput->SetCallback(&m_CaptureCallback);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"SetCallback() failed");


        hr = m_pDLInput->EnableVideoInput(mode, pixelFormat, inputFlags); // bmdVideoInputEnableFormatDetection ???
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"EnableVideoInput() failed");


    }
    else
    {
        AMFTraceInfo(AMF_FACILITY, L"Video stream is disabled");
    }
    AMFOutputPtr pAudioStream = GetStream(AMF_STREAM_AUDIO);
    AMF_RETURN_IF_FALSE(pAudioStream != NULL, AMF_NOT_FOUND, L"Audio stream was not found");

    bool bEnableAudio = false;
    pAudioStream->GetProperty(AMF_STREAM_ENABLED, &bEnableAudio);

    /*
    if(bEnableAudio)
    {
        amf_int64 audioFormatAMF = AMFAF_S16;
        GetProperty(AMF_STREAM_AUDIO_FORMAT, &audioFormatAMF);

        amf_int64 sampleRateAMF = 48000;
        GetProperty(AMF_STREAM_AUDIO_SAMPLE_RATE, &sampleRateAMF);

        amf_int64 channelCountAMF = 2;
        GetProperty(AMF_STREAM_AUDIO_CHANNELS, &channelCountAMF);

        m_eAudioFormat = AMF_AUDIO_FORMAT(audioFormatAMF);
        m_iSampleRate = amf_int32(sampleRateAMF);
        m_iChannels = amf_int32(channelCountAMF);

        // TODO: use sampleRateAMF DL supports 48000 only;

        BMDAudioSampleRate sampleRate = bmdAudioSampleRate48kHz;
        BMDAudioSampleType sampleType = FromAMFToDLAudioType(AMF_AUDIO_FORMAT(audioFormatAMF));
        unsigned int channelCount = (unsigned int)channelCountAMF;

        hr = m_pDLInput->EnableAudioInput(sampleRate, sampleType, channelCount);
        ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"EnableAudioInput() failed");
    }
    else
    {
        AMFTraceInfo(AMF_FACILITY, L"Audio stream is disabled");
    }
    */
    AMF_RETURN_IF_FALSE(bEnableVideo || bEnableAudio, AMF_NOT_FOUND, L"Audio and Video streams were not found");

	hr = m_pDLInput->StartStreams();
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"StartStreams() failed");


    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
#if 0
AMF_RESULT  AMF_STD_CALL  AMFDeckLinkDeviceImpl::AllocOutputPool()
{
    AMF_RESULT res = AMF_OK;

    if(m_SurfacePool.size() > 0)
    {
        return AMF_OK;
    }

    AMFOutputPtr pVideoStream = GetStream(AMF_STREAM_VIDEO);
    if(pVideoStream != NULL)
    {
        bool bEnableVideo = false;
        pVideoStream->GetProperty(AMF_STREAM_ENABLED, &bEnableVideo);
        if(bEnableVideo)
        {
            res = AMFVideoTransfer::CreateVideoTransfer(m_pContext, m_eVideoMemoryType , &m_pVideoTransfer);
            AMF_RETURN_IF_FAILED(res, L"CreateVideoTransfer() failed");

            amf_int64 poolSize = 5;
            pVideoStream->GetProperty(AMF_STREAM_VIDEO_SURFACE_POOL, &poolSize);
            if(poolSize == 0)
            {
                poolSize = 2;
            }

            amf_int64 formatAMF = AMF_SURFACE_UYVY;
//            pVideoStream->GetProperty(AMF_STREAM_VIDEO_FORMAT, &formatAMF);
            AMFSize framesize = {};
            pVideoStream->GetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, &framesize);

            amf_size surfaceVirtualSize = framesize.width * framesize.height;

            switch(formatAMF)
            {
            case AMF_SURFACE_RGBA:
                surfaceVirtualSize *= 4;
                break;
            case AMF_SURFACE_BGRA:
                surfaceVirtualSize *= 4;
                break;
            case AMF_SURFACE_UYVY:
                surfaceVirtualSize *= 2;
                //MM to test only
//                surfaceVirtualSize *= 2;
                //
                break;
            default:
                AMF_RETURN_IF_FALSE(false, AMF_NOT_SUPPORTED, L"Format is not supported %d", AMFSurfaceGetFormatName(AMF_SURFACE_FORMAT(formatAMF)));
                break;
            }


            for(amf_int64 i = 0; i < poolSize; i++)
            {
                Surface surface;

                surface.virtualMemory = amf_virtual_alloc(surfaceVirtualSize);
                AMF_RETURN_IF_FALSE(surface.virtualMemory != NULL, AMF_OUT_OF_MEMORY, L"Out of memory");
                res = m_pVideoTransfer->AllocateSurface(surface.virtualMemory, AMF_SURFACE_FORMAT(formatAMF), framesize.width, framesize.height, &surface.allocatedSurface);
                AMF_RETURN_IF_FAILED(res, L"AllocSurface() failed");
                m_SurfacePool.push_back(surface);
            }
        }
    }
    return AMF_OK;
}
#endif
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL  AMFDeckLinkDeviceImpl::InitStreams()
{
    AMF_RETURN_IF_FALSE(m_pDLInput != NULL, AMF_NOT_INITIALIZED, L"Not initialized");

    m_OutputStreams.clear();

    m_OutputStreams.push_back(new AMFVideoOutput(this));
    m_OutputStreams.push_back(new AMFAudioOutput(this));

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL  AMFDeckLinkDeviceImpl::Start()
{
    AMF_RETURN_IF_FALSE(m_pDLInput != NULL, AMF_NOT_INITIALIZED, L"Not initialized");

//    AMF_RESULT res = AMF_OK;
//    HRESULT hr = S_OK;

//    res = AllocOutputPool();
//    AMF_RETURN_IF_FAILED(res, L"AllocOutputPool() failed");

//	  hr = m_pDLInput->StartStreams();
//    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"StartStreams() failed");

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL  AMFDeckLinkDeviceImpl::Stop()
{
    AMF_RETURN_IF_FALSE(m_pDLInput != NULL, AMF_NOT_INITIALIZED, L"Not initialized");

    HRESULT hr = S_OK;

	hr = m_pDLInput->StopStreams();
    ASSERT_RETURN_IF_HR_FAILED(hr, AMF_FAIL, L"StopStreams() failed");

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL  AMFDeckLinkDeviceImpl::ReInit(amf_int32 /* width */, amf_int32 /* height */)
{
    Terminate();
    return Init(AMF_SURFACE_UNKNOWN, 0, 0);
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL  AMFDeckLinkDeviceImpl::Terminate()
{
    if(m_pDLInput != NULL)
    {
        m_pDLInput->SetCallback(NULL);
        m_pDLInput->StopStreams();
        m_pDLInput->SetVideoInputFrameMemoryAllocator(NULL);

        m_pDLInput->DisableVideoInput();

//        m_pDLInput.Release();
    }
//    m_OutputStreams.clear();

    AMFLock lock(&m_sect);
    for(amf_list<AMFDeckLinkDeviceImpl::Surface>::iterator it = m_SurfacePool.begin(); it != m_SurfacePool.end(); it++)
    {
        if(it->trackedSurface != NULL)
        {
            it->trackedSurface->RemoveObserver(this);
        }
        m_pVideoTransfer->ReleaseSurface(it->allocatedSurface);
        amf_virtual_free(it->virtualMemory);
        it->allocated = false;
        it->transferred = false;
    }
    m_SurfacePool.clear();
    m_pVideoTransfer.Release();

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL  AMFDeckLinkDeviceImpl::Drain()
{
    AMFLock lock(&m_sect);
    m_bEof = true;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL  AMFDeckLinkDeviceImpl::Flush()
{
    AMFLock lock(&m_sect);
    //MM TODO
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
amf_int32   AMF_STD_CALL  AMFDeckLinkDeviceImpl::GetOutputCount()
{
    AMFLock lock(&m_sect);
    return (amf_int32)m_OutputStreams.size();
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL  AMFDeckLinkDeviceImpl::GetOutput(amf_int32 index, AMFOutput** ppOutput)
{
    AMFLock lock(&m_sect);
    AMF_RETURN_IF_FALSE(index < (amf_int32)m_OutputStreams.size(), AMF_INVALID_ARG, L"index %d out of range", index);
    *ppOutput = m_OutputStreams[index];
    (*ppOutput)->Acquire();
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMFDeckLinkDeviceImpl::AMFVideoOutput::AMFVideoOutput(AMFDeckLinkDeviceImpl* pHost) :
    m_pHost(pHost)
{
    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoEnum( AMF_STREAM_VIDEO_MEMORY_TYPE,  AMF_STREAM_VIDEO_MEMORY_TYPE, AMF_MEMORY_DX11, AMF_CAPTURE_VIDEO_MEMORY_TYPE_DESC, false),
        AMFPropertyInfoEnum( AMF_STREAM_VIDEO_FORMAT,       AMF_STREAM_VIDEO_FORMAT, AMF_SURFACE_UYVY, AMF_CAPTURE_VIDEO_FORMAT_DESC, false),
        AMFPropertyInfoRate( AMF_STREAM_VIDEO_FRAME_RATE,   AMF_STREAM_VIDEO_FRAME_RATE, 30, 1,  false),
        AMFPropertyInfoSize(AMF_STREAM_VIDEO_FRAME_SIZE,    AMF_STREAM_VIDEO_FRAME_SIZE, AMFConstructSize(3840, 2160), AMFConstructSize(320, 240), AMFConstructSize(4096, 4096), false),
        AMFPropertyInfoInt64(AMF_STREAM_VIDEO_SURFACE_POOL, AMF_STREAM_VIDEO_SURFACE_POOL, 10, 0, INT_MAX, false),
        AMFPropertyInfoInt64(AMF_STREAM_TYPE,               AMF_STREAM_TYPE, AMF_STREAM_VIDEO, AMF_STREAM_VIDEO, AMF_STREAM_VIDEO, false),
        AMFPropertyInfoBool( AMF_STREAM_ENABLED,            AMF_STREAM_ENABLED, false, true),
        AMFPropertyInfoInt64(AMF_STREAM_CODEC_ID,           AMF_STREAM_CODEC_ID, 0, 0, 0, false),

    AMFPrimitivePropertyInfoMapEnd
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT  AMF_STD_CALL AMFDeckLinkDeviceImpl::AMFVideoOutput::ValidateProperty(const wchar_t* name, AMFVariantStruct value, AMFVariantStruct* pOutValidated) const
{
    AMF_RESULT res = baseclassOutput::ValidateProperty(name, value, pOutValidated);
    if(res != AMF_OK)
    {
        return res;
    }
    amf_wstring sname(name);
    if(sname == AMF_STREAM_VIDEO_FRAME_SIZE)
    {
        bool bFound = false;
        for(amf_vector<Mode>::iterator it = m_pHost->m_SupportedModes.begin(); it != m_pHost->m_SupportedModes.end(); it++)
        {
            if(it->framesize.width == value.sizeValue.width && it->framesize.height == value.sizeValue.height)
            {
                bFound = true;
                break;
            }
        }
        if(!bFound)
        {
            return AMF_OUT_OF_RANGE;
        }
    }else if(sname == AMF_STREAM_VIDEO_FRAME_RATE)
    {
        bool bFound = false;
        for(amf_vector<Mode>::iterator it = m_pHost->m_SupportedModes.begin(); it != m_pHost->m_SupportedModes.end(); it++)
        {
            if(it->framerate.num == value.rateValue.num && it->framerate.den == value.rateValue.den)
            {
                bFound = true;
                break;
            }
        }
        if(!bFound)
        {
            return AMF_OUT_OF_RANGE;
        }
    }
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFDeckLinkDeviceImpl::AMFVideoOutput::QueryOutput(AMFData** ppData)
{
    AMFLock lock(&m_pHost->m_sect);

    if(m_pHost->m_bEof)
    {
        return AMF_EOF;
    }

    amf_list<Surface>::iterator found = m_pHost->m_SurfacePool.begin();
    for(; found != m_pHost->m_SurfacePool.end(); found++)
    {
        if(found->transferred && found->trackedSurface == NULL)
        {
            break;
        }
    }

    if(found == m_pHost->m_SurfacePool.end())
    {
        return AMF_REPEAT; // not ready
    }

    AMFTraceInfo(AMF_FACILITY, L"QueryOutput() size=%u vm=0x%X", found->size, found->virtualMemory);

    AMFSurfacePtr pOutput;

    switch(m_pHost->m_eVideoMemoryType)
    {
    case AMF_MEMORY_DX11:
        m_pHost->m_pContext->CreateSurfaceFromDX11Native(found->allocatedSurface, &pOutput, m_pHost);
        found->trackedSurface = pOutput;
        break;
    }
//    pOutput->Duplicate(pOutput->GetMemoryType(), ppData);
//    found->transferred = false;

    /*
    amf_int64 formatAMF = AMF_SURFACE_UNKNOWN;
    GetProperty(AMF_STREAM_VIDEO_FORMAT, &formatAMF);
    AMFSize framesize = {};
    GetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, &framesize);

    switch(m_pHost->m_eVideoMemoryType)
    {
    case AMF_MEMORY_DX11:
        m_pHost->m_pContext->AllocSurface(m_pHost->m_eVideoMemoryType, (AMF_SURFACE_FORMAT)formatAMF, framesize.width, framesize.height, &pOutput);
        {
            CComPtr<ID3D11Device> pDeviceD3D = (ID3D11Device*)m_pHost->m_pContext->GetDX11Device();
            CComPtr<ID3D11DeviceContext> pDeviceContextD3D;
            pDeviceD3D->GetImmediateContext(&pDeviceContextD3D);
            pDeviceContextD3D->CopyResource((ID3D11Texture2D*)pOutput->GetPlaneAt(0)->GetNative(), (ID3D11Texture2D*)found->allocatedSurface);
        }
        break;
    }
    found->trackedSurface = NULL;
    found->transferred = false;
    found->allocated = false;
    */
    *ppData = pOutput.Detach();
    (*ppData)->SetPts(found->pts);
    (*ppData)->SetDuration(found->duration);

    m_pHost->m_SurfacePool.push_back(*found);
    m_pHost->m_SurfacePool.erase(found);

    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL AMFDeckLinkDeviceImpl::OnSurfaceDataRelease(AMFSurface* pSurface)
{
    AMFLock lock(&m_sect);
    for(amf_list<Surface>::iterator found = m_SurfacePool.begin(); found != m_SurfacePool.end(); found++)
    {
        if(found->trackedSurface == pSurface)
        {
            found->trackedSurface = NULL;
            found->transferred = false;
            found->allocated = false;

            AMFTraceInfo(AMF_FACILITY, L"OnSurfaceDataRelease() size=%u vm=0x%X", found->size, found->virtualMemory);

            break;
        }
    }
}
//-------------------------------------------------------------------------------------------------
AMFDeckLinkDeviceImpl::AMFAudioOutput::AMFAudioOutput(AMFDeckLinkDeviceImpl* pHost)  :
m_pHost(pHost)
{
    AMFPrimitivePropertyInfoMapBegin
        AMFPropertyInfoEnum( AMF_STREAM_AUDIO_FORMAT,       AMF_STREAM_AUDIO_FORMAT, AMFAF_S16, AMF_CAPTURE_AUDIO_FORMAT_DESC, false),
        AMFPropertyInfoInt64(AMF_STREAM_AUDIO_SAMPLE_RATE,  AMF_STREAM_AUDIO_SAMPLE_RATE,  48000, 48000, 48000, false), // bmdAudioSampleRate48kHz
        AMFPropertyInfoEnum( AMF_STREAM_AUDIO_CHANNELS,     AMF_STREAM_AUDIO_CHANNELS, 2, AMF_CAPTURE_CHANNELS_DESC, false),
        AMFPropertyInfoInt64(AMF_STREAM_TYPE,               AMF_STREAM_TYPE, AMF_STREAM_AUDIO, AMF_STREAM_AUDIO, AMF_STREAM_AUDIO, false),
        AMFPropertyInfoBool( AMF_STREAM_ENABLED,            AMF_STREAM_ENABLED, false, true),
        AMFPropertyInfoInt64(AMF_STREAM_CODEC_ID,           AMF_STREAM_CODEC_ID, 0, 0, 0, false),
    AMFPrimitivePropertyInfoMapEnd
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AMF_STD_CALL  AMFDeckLinkDeviceImpl::AMFAudioOutput::QueryOutput(AMFData** ppData)
{
    AMFLock lock(&m_pHost->m_sect);

    if(m_pHost->m_bEof)
    {
        return AMF_EOF;
    }

    if(m_pHost->m_AudioBuffers.size() == 0)
    {
        return AMF_REPEAT;
    }
    *ppData = m_pHost->m_AudioBuffers.front().Detach();
    m_pHost->m_AudioBuffers.pop_front();
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
// IDeckLinkMemoryAllocator methods
//-------------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE	VideoMemoryAllocator::AllocateBuffer(unsigned int bufferSize, void* *allocatedBuffer)
{
    AMFLock lock(&m_pHost->m_sect);

    amf_list<AMFDeckLinkDeviceImpl::Surface>::iterator found = m_pHost->m_SurfacePool.end();
    int inTransit = 0;
    int transferred = 0;

    for(amf_list<AMFDeckLinkDeviceImpl::Surface>::iterator it = m_pHost->m_SurfacePool.begin(); it != m_pHost->m_SurfacePool.end(); it++)
    {
        if(!(it->allocated) && !(it->transferred) && found == m_pHost->m_SurfacePool.end())
        {
            found = it;
        }
        if(it->trackedSurface != NULL)
        {
            inTransit++;
        }
        else if(it->transferred)
        {
            transferred++;
        }
    }
    if(found == m_pHost->m_SurfacePool.end())
    {
        AMFOutputPtr pVideoStream = m_pHost->GetStream(AMF_STREAM_VIDEO);
        if(pVideoStream != NULL)
        {
            amf_int64 poolSize = 5;
            pVideoStream->GetProperty(AMF_STREAM_VIDEO_SURFACE_POOL, &poolSize);
            if(poolSize == 0)
            {
                poolSize = 2;
            }

//            if(inTransit + transferred > 0)
//            {
//                poolSize += inTransit + transferred;
//            }

            if(m_pHost->m_SurfacePool.size() >= (amf_size)poolSize)
            {
                AMFTraceInfo(AMF_FACILITY, L"AllocateBuffer()  no available frames");
                return E_OUTOFMEMORY;
            }

            amf_int64 formatAMF = AMF_SURFACE_UNKNOWN;
            pVideoStream->GetProperty(AMF_STREAM_VIDEO_FORMAT, &formatAMF);
            AMFSize framesize = {};
            pVideoStream->GetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, &framesize);
           AMFDeckLinkDeviceImpl::Surface surface;
           AMF_RESULT res = AMF_OK;
           res = m_pHost->m_pVideoTransfer->AllocateSurface(bufferSize, AMF_SURFACE_FORMAT(formatAMF), framesize.width, framesize.height, (void**)&surface.virtualMemory, &surface.allocatedSurface);
            if(res != AMF_OK)
            {
                return E_OUTOFMEMORY;
            }
            surface.size = amf_size(bufferSize);
            m_pHost->m_SurfacePool.push_back(surface);
            found = m_pHost->m_SurfacePool.end();
            found--;
        }
        else
        {
            AMFTraceError(AMF_FACILITY, L"AllocateBuffer()  no video stream");
            return E_FAIL;
        }
    }
    AMFTraceInfo(AMF_FACILITY, L"AllocateBuffer() size=%u vm=0x%X", found->size, found->virtualMemory);
    found->allocated = true;
    found->transferred = false;
    *allocatedBuffer = found->virtualMemory;
    return S_OK;
}
//-------------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE	VideoMemoryAllocator::ReleaseBuffer(void* buffer)
{
    AMFLock lock(&m_pHost->m_sect);
    amf_list<AMFDeckLinkDeviceImpl::Surface>::iterator found = m_pHost->m_SurfacePool.begin();
    for(; found != m_pHost->m_SurfacePool.end(); found++)
    {
        if(found->virtualMemory == buffer)
        {
            break;
        }
    }
    if(found == m_pHost->m_SurfacePool.end())
    {
        return S_OK; // already released
    }
    AMFTraceInfo(AMF_FACILITY, L"ReleaseBuffer() size=%u vm=0x%X", found->size, found->virtualMemory);

    found->allocated = false;
    return S_OK;
}
//-------------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE	VideoMemoryAllocator::Commit()
{
    AMFTraceInfo(AMF_FACILITY, L"Commit()");
    return S_OK;
}
//-------------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE	VideoMemoryAllocator::Decommit()
{
    AMFTraceInfo(AMF_FACILITY, L"Decommit()");
    AMFLock lock(&m_pHost->m_sect);
    for(amf_list<AMFDeckLinkDeviceImpl::Surface>::iterator it = m_pHost->m_SurfacePool.begin() ; it != m_pHost->m_SurfacePool.end(); it++)
    {
        it->allocated = false;
    }
    return S_OK;
}
//-------------------------------------------------------------------------------------------------
// IDeckLinkInputCallback methods
//-------------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE	CaptureCallback::VideoInputFrameArrived(IDeckLinkVideoInputFrame *videoFrame, IDeckLinkAudioInputPacket *audioPacket)
{
    AMFLock lock(&m_pHost->m_sect);

    if(audioPacket != NULL)
    {
        AMFTraceInfo(AMF_FACILITY, L"AudioInputFrameArrived()");
        amf_uint8 * data = NULL;
        audioPacket->GetBytes((void**)&data);

        BMDTimeValue packetTime = 0;
        BMDTimeScale packetScale = AMF_SECOND;
        audioPacket->GetPacketTime(&packetTime, packetScale);

        long sampleCount = audioPacket->GetSampleFrameCount();
        AMFAudioBufferPtr pBuffer;
        AMF_RESULT res = m_pHost->m_pContext->AllocAudioBuffer(AMF_MEMORY_HOST, m_pHost->m_eAudioFormat, amf_int32(sampleCount), m_pHost->m_iSampleRate, m_pHost->m_iChannels, &pBuffer);
        if(res != AMF_OK)
        {
            AMFTraceError(AMF_FACILITY, L"VideoInputFrameArrived() AllocAudioBuffer() failed with error %d", res);
            return E_OUTOFMEMORY;
        }
        memcpy(pBuffer->GetNative(), data, pBuffer->GetSize());
        if(m_pHost->m_ptsStartAudio == -1LL)
        {
            m_pHost->m_ptsStartAudio = amf_pts(packetTime);
        }

        pBuffer->SetPts(amf_pts(packetTime) - m_pHost->m_ptsStartAudio);
        pBuffer->SetDuration(sampleCount * AMF_SECOND / m_pHost->m_iSampleRate);

        m_pHost->m_AudioBuffers.push_back(pBuffer);
    }

    if(videoFrame != NULL)
    {
        BMDFrameFlags flags = videoFrame->GetFlags();
        if((flags & bmdFrameHasNoInputSource) == bmdFrameHasNoInputSource)
        {
            AMFTraceWarning(AMF_FACILITY, L"VideoInputFrameArrived() no input source, frame is invalid");
            return S_OK;
        }

        amf_uint8 * data = NULL;
        amf_uint8 * dataAux = NULL;
        videoFrame->GetBytes((void**)&data);

        long rowBytes= videoFrame->GetRowBytes();


        CComPtr<IDeckLinkVideoFrameAncillary> pAuxData;
        videoFrame->GetAncillaryData(&pAuxData);
        if(pAuxData != NULL)
        {
            pAuxData->GetBufferForVerticalBlankingLine(0, (void**)&dataAux);
        }


        BMDTimeValue packetTime = 0;
        BMDTimeValue packetDuration = 0;
        BMDTimeScale packetScale = AMF_SECOND;
        videoFrame->GetStreamTime(&packetTime, &packetDuration, packetScale);
        if(m_pHost->m_ptsStartVideo == -1LL)
        {
            m_pHost->m_ptsStartVideo = amf_pts(packetTime);
        }

       //MM ???
        // videoFrame->GetHardwareReferenceTimestamp()

        amf_list<AMFDeckLinkDeviceImpl::Surface>::iterator found = m_pHost->m_SurfacePool.begin();
        for(; found != m_pHost->m_SurfacePool.end(); found++)
        {
            if(found->virtualMemory == data)
            {
                break;
            }
        }
        if(found == m_pHost->m_SurfacePool.end())
        {
            AMFTraceError(AMF_FACILITY, L"VideoInputFrameArrived() Surface not found() ");
            return E_FAIL;
        }
        AMFTraceInfo(AMF_FACILITY, L"VideoInputFrameArrived() size=%u vm=0x%X", found->size, found->virtualMemory);

        found->pts = amf_pts(packetTime);
        found->duration = amf_pts(packetDuration);

        m_pHost->m_pVideoTransfer->Transfer(found->virtualMemory, (amf_size)rowBytes, found->allocatedSurface);

        found->transferred = true;
        m_pHost->m_SurfacePool.push_front(*found);
        m_pHost->m_SurfacePool.erase(found);
    }
    return S_OK;
}
//-------------------------------------------------------------------------------------------------
HRESULT	STDMETHODCALLTYPE	CaptureCallback::VideoInputFormatChanged(BMDVideoInputFormatChangedEvents /* notificationEvents */, IDeckLinkDisplayMode* newDisplayMode, BMDDetectedVideoInputFormatFlags detectedSignalFlags)
{
    AMFLock lock(&m_pHost->m_sect);

    for(amf_list<AMFDeckLinkDeviceImpl::Surface>::iterator it = m_pHost->m_SurfacePool.begin(); it != m_pHost->m_SurfacePool.end(); it++)
    {
        m_pHost->m_pVideoTransfer->ReleaseSurface(it->allocatedSurface);
        amf_virtual_free(it->virtualMemory);
        it->allocated = false;
        it->transferred = false;
    }
    m_pHost->m_SurfacePool.clear();

	BMDPixelFormat	pixelFormat = bmdFormat10BitYUV;

    if((detectedSignalFlags & bmdDetectedVideoInputYCbCr422) == bmdDetectedVideoInputYCbCr422)
    {
        pixelFormat = bmdFormat8BitYUV;
//        pixelFormat = bmdFormat8BitBGRA;
    }
    else if((detectedSignalFlags & bmdDetectedVideoInputRGB444) == bmdDetectedVideoInputRGB444)
    {
        pixelFormat = bmdFormat10BitRGB;
    }

    HRESULT hr = S_OK;


	m_pHost->m_pDLInput->StopStreams();

     BMDDisplayMode mode = newDisplayMode->GetDisplayMode();

    for(amf_vector<AMFDeckLinkDeviceImpl::Mode>::iterator it = m_pHost->m_SupportedModes.begin(); it != m_pHost->m_SupportedModes.end(); it++)
    {
        if(it->mode == mode)
        {
            AMFOutputPtr pVideoStream = m_pHost->GetStream(AMF_STREAM_VIDEO);
            if(pVideoStream != NULL)
            {
                pVideoStream->SetProperty(AMF_STREAM_VIDEO_FRAME_SIZE, it->framesize);
                pVideoStream->SetProperty(AMF_STREAM_VIDEO_FRAME_RATE, it->framerate);

                //keep the selected pixel format
                amf_int64 formatAMF = AMF_SURFACE_UNKNOWN;
                pVideoStream->GetProperty(AMF_STREAM_VIDEO_FORMAT, &formatAMF);
                pixelFormat = FromAMFToDLFormat(AMF_SURFACE_FORMAT(formatAMF));

                AMFTraceWarning(AMF_FACILITY, L"VideoInputFormatChanged() %dx%d@%5.2f", it->framesize.width, it->framesize.height, (double)it->framerate.num / it->framerate.den );
            }
            break;
        }
    }

    hr = m_pHost->m_pDLInput->PauseStreams();

	// Set the video input mode
	hr = m_pHost->m_pDLInput->EnableVideoInput(mode, pixelFormat, bmdVideoInputEnableFormatDetection);

    hr = m_pHost->m_pDLInput->FlushStreams();
	// Start the capture
	hr = m_pHost->m_pDLInput->StartStreams();

    return S_OK;
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
