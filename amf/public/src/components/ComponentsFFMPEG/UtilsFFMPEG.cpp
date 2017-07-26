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

#include "UtilsFFMPEG.h"

#include "public/common/AMFSTL.h"
#include "public/common/DataStream.h"
#include "public/include/core/Trace.h"
#include "public/common/TraceAdapter.h"


using namespace amf;

// there is no definition in FFMPEG for H264MVC so create an ID
// based on the last element in their enumeration
const amf_int  AV_CODEC_H264MVC = AV_CODEC_ID_WRAPPED_AVFRAME + 0x300000;
AVRational AMF_TIME_BASE_Q    = { 1, AMF_SECOND };
AVRational FFMPEG_TIME_BASE_Q = { 1, AV_TIME_BASE };


//-------------------------------------------------------------------------------------------------
// custom FFMPEG proocol implementation
//-------------------------------------------------------------------------------------------------

static int file_open(URLContext *h, const char *filename, int flags)
{
    filename += strlen("vlfile:");
    amf_wstring wstr = amf_from_utf8_to_unicode(filename);


    AMF_STREAM_OPEN openDisp = AMFSO_READ;
    AMF_FILE_SHARE shareDisp = AMFFS_SHARE_READ;

    const wchar_t *pPpenDisp = NULL;
    if ((flags &  AVIO_FLAG_READ_WRITE) == AVIO_FLAG_READ_WRITE) {
        openDisp = AMFSO_READ_WRITE;
        shareDisp = AMFFS_SHARE_READ;
        pPpenDisp = L"READ_WRITE";
    }
    else if (flags & AVIO_FLAG_WRITE) {
        openDisp = AMFSO_WRITE;
        shareDisp = AMFFS_EXCLUSIVE;
        pPpenDisp = L"WRITE";
    }
    else {
        openDisp = AMFSO_READ;
        shareDisp = AMFFS_SHARE_READ;
        pPpenDisp = L"READ";
    }

    AMFDataStream *ptr = NULL;
    if (amf::AMFDataStream::OpenDataStream(wstr.c_str(), openDisp, shareDisp, &ptr) != AMF_OK){
        AMFTraceError(AMF_FACILITY, L"file_open() - amf::AMFDataStream::OpenDataStream(%s %s) failed to open file", pPpenDisp, wstr.c_str());
        return AVERROR(ENOENT);
    }
    h->priv_data = (void *)ptr;
    return 0;
}
//-------------------------------------------------------------------------------------------------
static int file_read(URLContext *h, unsigned char *buf, int size)
{
    AMFDataStream *ptr = (AMFDataStream *)h->priv_data;
    amf_size ready = 0;
    ptr->Read(buf, size, &ready);
    return (int)ready;
}
//-------------------------------------------------------------------------------------------------
static int file_write(URLContext *h, const unsigned char *buf, int size)
{
    AMFDataStream *ptr = (AMFDataStream *)h->priv_data;
    amf_size written = 0;
    if (ptr->Write(buf, size, &written) != AMF_OK){
        return -1;
    }
    return (int)written;
}
//-------------------------------------------------------------------------------------------------
static int64_t file_seek(URLContext *h, int64_t pos, int whence)
{
    AMFDataStream *ptr = (AMFDataStream *)h->priv_data;
    amf_int64 ret = 0;
    AMF_RESULT err = AMF_OK;
    if (whence == AVSEEK_SIZE) {
        err = ptr->GetSize(&ret);
    }
    else{
        err = ptr->Seek((AMF_SEEK_ORIGIN)whence, pos, &ret);
    }
    if (err != AMF_OK){
        return -1L;
    }
    return ret;
}
//-------------------------------------------------------------------------------------------------
static int file_close(URLContext *h)
{
    AMFDataStream *ptr = (AMFDataStream *)h->priv_data;
    AMF_RESULT err = ptr->Close();
    ptr->Release();
    h->priv_data = NULL;
    return err == AMF_OK ? 0 : -1;
}
//-------------------------------------------------------------------------------------------------
// FFmpeg doesnt support multi-byte file names only so if a file name has Eng + Lang1 + lang2 where one of languages is not default it will fail
// FFmpeg 3.3.1 disabled custom protocols

int ffurl_register_protocol2(URLProtocol *first_protocol, URLProtocol *protocol, int size)
{
    /*
    URLProtocol **p;
    if (static_cast<amf_size>(size) < sizeof(URLProtocol))
    {
        URLProtocol* temp = (URLProtocol*)av_mallocz(sizeof(URLProtocol));
        memcpy(temp, protocol, size);
        protocol = temp;
    }
    p = &first_protocol;
    while (*p != NULL) 
        p = &(*p)->next;
    *p = protocol;
    protocol->next = NULL;
    */
    //MM total hack. FFMPEG 3.3.1 removed a way to register external protocols. So we reuse one
//    *((URLProtocol*)&ff_unix_protocol) = *protocol;
    /*
    const URLProtocol **protocols = ffurl_get_protocols(NULL, NULL);
    if (!protocols)
        return 0;
    for (int i = 0; protocols[i]; i++) 
    {
        const URLProtocol *up = protocols[i];
        if (!strcmp("gopher", up->name)) {
            *((URLProtocol*)up) = *protocol;
            av_freep(&protocols);
            break;
        }
    }
    av_freep(&protocols);
    */
    return 0;
}
//-------------------------------------------------------------------------------------------------

extern "C"
{
    URLProtocol amf_file_protocol = 
    {
        "vlfile",   // name
        file_open,  // open
        NULL,       // open2
        NULL,       // accept
        NULL,       // handshake
        file_read,  // read
        file_write, // write
        file_seek,  // seek
        file_close, // close
//        NULL,       // next
        NULL,       //url_read_pause
        NULL,       //url_read_seek
        NULL,       //file_get_handle
        NULL,       //int (*url_get_multi_file_handle)(URLContext *h, int **handles, int *numhandles);
        NULL,       //int (*url_get_short_seek)(URLContext *h);
        NULL,       //int (*url_shutdown)(URLContext *h, int flags);
        0,          //int priv_data_size;
        NULL,       //const AVClass *priv_data_class;
        0,          //int flags;
        NULL,       //int (*url_check)(URLContext *h, int mask);
        NULL,       //int (*url_open_dir)(URLContext *h);
        NULL,       //int (*url_read_dir)(URLContext *h, AVIODirEntry **next);
        NULL,       //int (*url_close_dir)(URLContext *h);
        NULL,       //int (*url_delete)(URLContext *h);
        NULL,       //int (*url_move)(URLContext *h_src, URLContext *h_dst);
        NULL,       //const char *default_whitelist;
    };
};

#define DETAILED_FFMPEG_LOG 0

extern "C"
{
static void my_log_callback(void*, int, const char*, va_list);
};

void my_log_callback(void* ptr, int level, const char* fmt, va_list vl)
{
//    return;
      int loglevel=av_log_get_level();
    AVClass* avc= ptr ? *(AVClass**)ptr : NULL;

    if(level > loglevel)
    { 
#if !DETAILED_FFMPEG_LOG
        return;
#endif
    }
//#undef fprintf
    amf_string name;
    if(avc!=NULL) {
        name=avc->item_name(ptr);
    }
    if(name.length()==0 || name=="NULL"){
        name="ffmpeg";
    }
#if !DETAILED_FFMPEG_LOG
    
    if(name=="mov,mp4,m4a,3gp,3g2,mj2")
    { 
        return;
    }


    if(name=="libx264"){
        return;
    }
    if(name=="mp3"){
        int a=1;
    }
    if(name=="wmv3"){
        return;
    }

    if(name=="vc1"){
        return;
    }
    if(name=="h264"){
        return;
    }
//    if(name=="rtp"){
//        return;
//    }
    if(name=="mpegts"){
        return;
    }
#endif
    
    amf_string format=fmt;

    amf_string bad="%td";
    while(true){

        amf_string::size_type pos=format.find(bad);
        if(pos==amf_string::npos)
            break;
        format.replace(pos,bad.length(),"%ID");
    }
    amf_string bad1="%s";
    while(true){

        amf_string::size_type pos=format.find(bad1);
        if(pos==amf_string::npos)
            break;
        format.replace(pos,bad.length(),"%S");
    }

    if(format[format.length()-1]=='\n')
    { 
        format=format.substr(0,format.length()-1);
    }
    amf_wstring wname = amf_from_utf8_to_unicode(name);
    amf_wstring wformat = amf_from_utf8_to_unicode(format);
    AMFTrace(AMF_TRACE_ERROR, wname.c_str(),L"%s",amf_string_formatVA(wformat.c_str(),vl).c_str());
    
}


//-------------------------------------------------------------------------------------------------
void AMF_STD_CALL amf::InitFFMPEG()
{
    // setup own reader for FFMPEG
static bool g_bRegistered = false;
    if (!g_bRegistered)
    {
        av_log_set_callback(my_log_callback);

//        int currLevel = av_log_get_level();
        av_log_set_level(AV_LOG_ERROR);
        av_register_all();

        // register protocol
        void*       opaque = NULL;
        const char* name   = avio_enum_protocols(&opaque, 0);
        (void)name;

        ffurl_register_protocol2((URLProtocol*)opaque, &amf_file_protocol, sizeof(amf_file_protocol));
        g_bRegistered = true;
    }
}
//-------------------------------------------------------------------------------------------------
bool AMF_STD_CALL  amf::IsAudioPlanar(AMF_AUDIO_FORMAT inFormat)
{
    switch (inFormat)
    {
    case AMFAF_U8P:
    case AMFAF_S16P:
    case AMFAF_S32P:
    case AMFAF_FLTP:
    case AMFAF_DBLP:
        return true;
    }

    return false;
}
//-------------------------------------------------------------------------------------------------
AMF_AUDIO_FORMAT  AMF_STD_CALL  amf::GetAMFAudioFormat(AVSampleFormat inFormat)
{
    switch (inFormat)
    {
    case AV_SAMPLE_FMT_U8:   return AMFAF_U8;
    case AV_SAMPLE_FMT_S16:  return AMFAF_S16;
    case AV_SAMPLE_FMT_S32:  return AMFAF_S32;
    case AV_SAMPLE_FMT_FLT:  return AMFAF_FLT;
    case AV_SAMPLE_FMT_DBL:  return AMFAF_DBL;

    case AV_SAMPLE_FMT_U8P:  return AMFAF_U8P;
    case AV_SAMPLE_FMT_S16P: return AMFAF_S16P;
    case AV_SAMPLE_FMT_S32P: return AMFAF_S32P;
    case AV_SAMPLE_FMT_FLTP: return AMFAF_FLTP;
    case AV_SAMPLE_FMT_DBLP: return AMFAF_DBLP;

    case AV_SAMPLE_FMT_NONE:
    default:
        return AMFAF_UNKNOWN;
    }
}
//-------------------------------------------------------------------------------------------------
AVSampleFormat AMF_STD_CALL  amf::GetFFMPEGAudioFormat(AMF_AUDIO_FORMAT inFormat)
{
    switch (inFormat)
    {
    case AMFAF_U8:   return AV_SAMPLE_FMT_U8;
    case AMFAF_S16:  return AV_SAMPLE_FMT_S16;
    case AMFAF_S32:  return AV_SAMPLE_FMT_S32;
    case AMFAF_FLT:  return AV_SAMPLE_FMT_FLT;
    case AMFAF_DBL:  return AV_SAMPLE_FMT_DBL;

    case AMFAF_U8P:  return AV_SAMPLE_FMT_U8P;
    case AMFAF_S16P: return AV_SAMPLE_FMT_S16P;
    case AMFAF_S32P: return AV_SAMPLE_FMT_S32P;
    case AMFAF_FLTP: return AV_SAMPLE_FMT_FLTP;
    case AMFAF_DBLP: return AV_SAMPLE_FMT_DBLP;

    case AMFAF_UNKNOWN:
    default:
        return AV_SAMPLE_FMT_NONE;
    }
}
//-------------------------------------------------------------------------------------------------
amf_int32 AMF_STD_CALL  amf::GetAudioSampleSize(AMF_AUDIO_FORMAT inFormat)
{
    amf_int32 sample_size = 2;
    switch (inFormat)
    {
    case AMFAF_UNKNOWN:
        sample_size = 0;
        break;
    case AMFAF_U8P:
    case AMFAF_U8:
        sample_size = 1;
        break;
    case AMFAF_S16P:
    case AMFAF_S16:
        sample_size = 2;
        break;
    case AMFAF_S32P:
    case AMFAF_FLTP:
    case AMFAF_S32:
    case AMFAF_FLT:
        sample_size = 4;
        break;
    case AMFAF_DBLP:
    case AMFAF_DBL:
        sample_size = 8;
        break;
    }
    return sample_size;
}
