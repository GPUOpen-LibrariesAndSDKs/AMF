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

#include "AudioPresenter.h"
#include "AudioPresenterWin.h"

using namespace amf;

//-------------------------------------------------------------------------------------------------
AudioPresenter::AudioPresenter()
    : m_ptsSeek(-1LL),
    m_bLowLatency(false),
    m_startTime(-1LL),
    m_pAVSync(NULL)
{}

//-------------------------------------------------------------------------------------------------
AudioPresenter::~AudioPresenter()
{
}

//-------------------------------------------------------------------------------------------------
AMF_RESULT AudioPresenter::Seek(amf_pts pts)
{
    amf::AMFLock lock(&m_cs);
    m_ptsSeek = pts;
    return AMF_OK;
}

//-------------------------------------------------------------------------------------------------
bool AudioPresenter::HandleSeek(const AMFAudioBuffer* pBuffer, bool& bDiscard, amf_size& byteOffset)
{
    bDiscard = false;

    amf::AMFLock lock(&m_cs);

    if (m_ptsSeek == -1LL)
    {
        return false;
    }
    byteOffset = 0;

    const amf_pts
        pts = const_cast<AMFAudioBuffer*>(pBuffer)->GetPts(),
        duration = const_cast<AMFAudioBuffer*>(pBuffer)->GetDuration();

    if (m_ptsSeek >= pts + duration)
    {
        // discard whole buffer - it is too early
        bDiscard = true;
        return true;
    }
    else if (m_ptsSeek <= pts) // seek is done 
    {
        const amf_pts timeForSilence = pts - m_ptsSeek;
        
        // wait
        if (timeForSilence > 2000)
        {
            amf_sleep(amf_long(timeForSilence / 10000)); // in ms
        }
    }
    else  // if(m_ptsSeek >= pts && m_ptsSeek < pts + duration)  need to eat part of the buffer
    {
        const amf_pts timeToEat = m_ptsSeek - pts; // in 100s of nanosecond
        amf_int64 samplesToEat = const_cast<AMFAudioBuffer*>(pBuffer)->GetSampleRate() * timeToEat / AMF_SECOND;
        // bytes to eat
        byteOffset = (amf_size)(samplesToEat * const_cast<AMFAudioBuffer*>(pBuffer)->GetSampleSize() * const_cast<AMFAudioBuffer*>(pBuffer)->GetChannelCount());
    }

    m_ptsSeek = -1LL;
    return true;
}
