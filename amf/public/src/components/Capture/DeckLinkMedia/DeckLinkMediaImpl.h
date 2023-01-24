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
#pragma once

#include "public/include/components/Capture.h"
#include "public/common/PropertyStorageExImpl.h"
#include "public/include/core/AudioBuffer.h"
#include "DeckLinkAPI_h.h" // MM temporary path
#include "public/common/Thread.h"
#include "../VideoTransfer.h"
#include <atlbase.h>

namespace amf
{
    //-------------------------------------------------------------------------------------------------
    class DLCaptureManagerImpl : public AMFInterfaceImpl<AMFCaptureManager>
    {
    public:
        DLCaptureManagerImpl(amf::AMFContext* pContext);
        virtual ~DLCaptureManagerImpl();

        virtual AMF_RESULT          AMF_STD_CALL Update();
        virtual amf_int32           AMF_STD_CALL GetDeviceCount();
        virtual AMF_RESULT          AMF_STD_CALL GetDevice(amf_int32 index,AMFCaptureDevice **pDevice);

    protected:
        amf::AMFContextPtr                              m_pContext;
        amf_vector< CComQIPtr<IDeckLinkInput> >         m_Inputs;
    };
    //-------------------------------------------------------------------------------------------------
    class AMFDeckLinkDeviceImpl;
    class VideoMemoryAllocator : public IDeckLinkMemoryAllocator
    {
    public:
        VideoMemoryAllocator(AMFDeckLinkDeviceImpl* pHost) : m_pHost(pHost){}
        virtual ~VideoMemoryAllocator(){}

	    // IUnknown methods
        virtual HRESULT STDMETHODCALLTYPE	QueryInterface(REFIID /* iid */, LPVOID* /* ppv */) { return E_NOTIMPL; }
        virtual ULONG STDMETHODCALLTYPE		AddRef(void){return 1;}
	    virtual ULONG STDMETHODCALLTYPE		Release(void){return 1;}

	    // IDeckLinkMemoryAllocator methods
	    virtual HRESULT STDMETHODCALLTYPE	AllocateBuffer(unsigned int bufferSize, void* *allocatedBuffer);
	    virtual HRESULT STDMETHODCALLTYPE	ReleaseBuffer(void* buffer);
	    virtual HRESULT STDMETHODCALLTYPE	Commit();
	    virtual HRESULT STDMETHODCALLTYPE	Decommit();

    protected:
        AMFDeckLinkDeviceImpl* m_pHost;
    };
    //-------------------------------------------------------------------------------------------------
    class CaptureCallback : public IDeckLinkInputCallback
    {
    public:
	    CaptureCallback(AMFDeckLinkDeviceImpl* pHost) : m_pHost(pHost){}
        virtual ~CaptureCallback(){}


	    // IUnknown needs only a dummy implementation
        virtual HRESULT STDMETHODCALLTYPE	QueryInterface(REFIID /* iid */, LPVOID* /* ppv */) { return E_NOTIMPL; }
        virtual ULONG STDMETHODCALLTYPE		AddRef(void){return 1;}
	    virtual ULONG STDMETHODCALLTYPE		Release(void){return 1;}

        // IDeckLinkInputCallback methods
	    virtual HRESULT STDMETHODCALLTYPE	VideoInputFrameArrived(IDeckLinkVideoInputFrame *videoFrame, IDeckLinkAudioInputPacket *audioPacket);
	    virtual HRESULT	STDMETHODCALLTYPE	VideoInputFormatChanged(BMDVideoInputFormatChangedEvents notificationEvents, IDeckLinkDisplayMode *newDisplayMode, BMDDetectedVideoInputFormatFlags detectedSignalFlags);
    protected:
        AMFDeckLinkDeviceImpl* m_pHost;
    };
    //-------------------------------------------------------------------------------------------------
    class AMFDeckLinkDeviceImpl :
        public AMFInterfaceBase,
        public AMFPropertyStorageExImpl<AMFCaptureDevice>,
        public AMFSurfaceObserver
    {
        friend class VideoMemoryAllocator;
        friend class CaptureCallback;
        friend class DLCaptureManagerImpl;
    public:
        // interface access
        AMF_BEGIN_INTERFACE_MAP
            AMF_INTERFACE_MULTI_ENTRY(AMFComponent)
            AMF_INTERFACE_MULTI_ENTRY(AMFComponentEx)
            AMF_INTERFACE_MULTI_ENTRY(AMFCaptureDevice)
            AMF_INTERFACE_CHAIN_ENTRY(AMFPropertyStorageExImpl<AMFCaptureDevice>)
        AMF_END_INTERFACE_MAP


        typedef AMFInterfaceImpl < AMFPropertyStorageExImpl <AMFOutput> > baseclassOutput;
    protected:
        struct Mode
        {
            Mode() {memset(this, 0, sizeof(*this));}

            BMDDisplayMode      mode;
            AMFSize             framesize;
            AMFRate             framerate;
            amf_wstring         name;
            BMDFieldDominance   field;
            BMDDisplayModeFlags flags;
        };

        //-------------------------------------------------------------------------------------------------
        class AMFVideoOutput : public baseclassOutput
        {
        public:
            AMFVideoOutput(AMFDeckLinkDeviceImpl* pHost);
            virtual ~AMFVideoOutput(){}
            // AMFOutput interface
            virtual AMF_RESULT AMF_STD_CALL  QueryOutput(AMFData** ppData);

            // AMFPropertyStorageExImpl
            virtual AMF_RESULT  AMF_STD_CALL ValidateProperty(const wchar_t* name, AMFVariantStruct value, AMFVariantStruct* pOutValidated) const;

        protected:

            AMFDeckLinkDeviceImpl*                         m_pHost;
            AMFDeckLinkDeviceImpl::Mode                    m_CurrentMode;
        };
        //-------------------------------------------------------------------------------------------------
        class AMFAudioOutput : public baseclassOutput
        {
        public:
            AMFAudioOutput(AMFDeckLinkDeviceImpl* pHost);
            virtual ~AMFAudioOutput(){}
            // AMFOutput interface
            virtual AMF_RESULT AMF_STD_CALL  QueryOutput(AMFData** ppData);
        protected:
            AMFDeckLinkDeviceImpl* m_pHost;
        };
        //-------------------------------------------------------------------------------------------------
    public:
        AMFDeckLinkDeviceImpl(AMFContext* pContext, IDeckLinkInput *input);
        virtual ~AMFDeckLinkDeviceImpl();

        // AMFComponent interface
        virtual AMF_RESULT  AMF_STD_CALL    Init(AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height);
        virtual AMF_RESULT  AMF_STD_CALL    ReInit(amf_int32 width, amf_int32 height);
        virtual AMF_RESULT  AMF_STD_CALL    Terminate();
        virtual AMF_RESULT  AMF_STD_CALL    Drain();
        virtual AMF_RESULT  AMF_STD_CALL    Flush();

        virtual AMF_RESULT  AMF_STD_CALL    SubmitInput(AMFData* /* pData */)                               { return AMF_NOT_SUPPORTED; };
        virtual AMF_RESULT  AMF_STD_CALL    QueryOutput(AMFData** /* ppData */)                             { return AMF_NOT_SUPPORTED; };
        virtual AMF_RESULT  AMF_STD_CALL    SetOutputDataAllocatorCB(AMFDataAllocatorCB* /* callback */)    { return AMF_OK; };
        virtual AMF_RESULT  AMF_STD_CALL    GetCaps(AMFCaps** /* ppCaps */)                                 { return AMF_NOT_SUPPORTED; };
        virtual AMF_RESULT  AMF_STD_CALL    Optimize(AMFComponentOptimizationCallback* /* pCallback */)     { return AMF_OK; };
        virtual AMFContext* AMF_STD_CALL    GetContext()                                                    { return m_pContext; };

        // AMFComponentEx interface
        virtual amf_int32   AMF_STD_CALL    GetInputCount()                                                 {  return 0;  };
        virtual amf_int32   AMF_STD_CALL    GetOutputCount();

        virtual AMF_RESULT  AMF_STD_CALL    GetInput(amf_int32 /* index */, AMFInput** /* ppInput */)       { return AMF_NOT_SUPPORTED; };
        virtual AMF_RESULT  AMF_STD_CALL    GetOutput(amf_int32 index, AMFOutput** ppOutput);

        // AMFSurfaceObserver interface
        virtual void AMF_STD_CALL           OnSurfaceDataRelease(AMFSurface* pSurface);

        // AMFCaptureDevice
        AMF_RESULT  AMF_STD_CALL            Start();
        AMF_RESULT  AMF_STD_CALL            Stop();

    protected:
        //-------------------------------------------------------------------------------------------------
        struct Surface
        {
            Surface() : trackedSurface(NULL), allocated(false),transferred(false),allocatedSurface(NULL), virtualMemory(NULL), size(0), pts(0), duration(0) {}
            void*         allocatedSurface;
            AMFSurface*   trackedSurface;
            bool          allocated;
            bool          transferred;
            amf_uint8*    virtualMemory;
            amf_size      size;
            amf_pts       pts;
            amf_pts       duration;
        };
        //-------------------------------------------------------------------------------------------------

        AMF_RESULT  AMF_STD_CALL  UpdateFromDeckLink();
        AMF_RESULT  AMF_STD_CALL  InitDeckLink();
//        AMF_RESULT  AMF_STD_CALL  AllocOutputPool();
        AMF_RESULT  AMF_STD_CALL  InitStreams();
        AMFOutputPtr AMF_STD_CALL GetStream(AMF_STREAM_TYPE_ENUM eType);

        AMFContextPtr                   m_pContext;

        amf_vector<AMFOutputPtr>        m_OutputStreams;

        CComQIPtr<IDeckLinkInput>       m_pDLInput;
        VideoMemoryAllocator            m_VideoMemoryAllocator;
        CaptureCallback                 m_CaptureCallback;
        amf_list<Surface>               m_SurfacePool;

        // video
        AMFVideoTransferPtr             m_pVideoTransfer;
        AMF_MEMORY_TYPE                 m_eVideoMemoryType;

        // audio
        amf_list<AMFAudioBufferPtr>     m_AudioBuffers;
        AMF_AUDIO_FORMAT                m_eAudioFormat;
        amf_int32                       m_iSampleRate;
        amf_int32                       m_iChannels;

        amf_pts                         m_ptsStartAudio;
        amf_pts                         m_ptsStartVideo;

        AMFCriticalSection              m_sect;
        bool                            m_bEof;

        amf_vector<Mode>                m_SupportedModes;

    private:
        AMFDeckLinkDeviceImpl(const AMFDeckLinkDeviceImpl&);
        AMFDeckLinkDeviceImpl& operator=(const AMFDeckLinkDeviceImpl&);

    };
    typedef AMFInterfacePtr_T<AMFDeckLinkDeviceImpl> AMFDeckLinkDeviceImplPtr;

} // namespace amf