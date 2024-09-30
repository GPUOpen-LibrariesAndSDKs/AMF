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

#include "AudioPresenter.h"
#include "AudioPresenterWin.h"
#include <public/common/TraceAdapter.h>

using namespace amf;

#define AMF_FACILITY    L"AudioPresenter"
//-------------------------------------------------------------------------------------------------
AudioPresenter::AudioPresenter() :
    m_ptsSeek(-1LL),
    m_bLowLatency(false),
    m_startTime(-1LL),
    m_pAVSync(NULL),
    m_bDoWait(true),
    m_ResetAVSync(true),
    m_SequentiallyDroppedAudioSamplesCnt(0),
    m_SequentiallyLateAudioSamplesCnt(0),
    m_DropCyclesCnt(0),
    m_DesyncToIgnore(0LL)
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
    m_ResetAVSync = true;
    return AMF_OK;
}

AMF_RESULT AudioPresenter::Flush()
{
    amf::AMFLock lock(&m_cs);
    m_ResetAVSync = true;
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

bool AudioPresenter::ShouldPresentAudioOrNot(amf_pts videoPts, amf_pts audioPts)
{
    bool present = true;
    amf_pts now = amf_high_precision_clock();
    static const amf_pts maxLateBy = 80 * AMF_MILLISECOND;

    //  Compensation for audio samples arriving later than real time:
    //  An audio sample can be delayed on the network and arrive late. Since we generally try to 
    //  buffer as little as possible not to increase latency and minimize desync with video, 
    //  this will of course cause audio stutter. However, when the late sample arrives, if we still
    //  play it right after the gap, it would delay the whole audio stream even further. Since we
    //  have already stuttered, skipping the late sample won't make it sound worse, would just make the
    //  gap a little longer, but would allow the rest of the stream to catch up with real time.
    //  We allow for no more than 80ms of delay and no more than 10 consecutive samples to be skipped.
    if (m_ResetAVSync == true)
    {
        m_FirstAudioSamplePts = audioPts;
        m_FirstAudioSampleTime = now;
        m_SequentiallyLateAudioSamplesCnt = 0;
        m_SequentiallyDroppedAudioSamplesCnt = 0;
        m_ResetAVSync = false;
        m_DesyncToIgnore = 0;
    }

    if (videoPts != -1LL)
    {
        //  Here we compensate for audio lagging behind video by more than a certain threshold
        m_AverageAVDesyncQueue.push_back(videoPts - audioPts);
        if (m_AverageAVDesyncQueue.size() > 100)
        {
            amf_pts averageAVDesync = 0;
            m_AverageAVDesyncQueue.pop_front();
            for (auto ptsDiff : m_AverageAVDesyncQueue)
            {
                averageAVDesync += ptsDiff;
            }
            // we need to cast to signed, because otherwise the division gets computed unsigned
            averageAVDesync /= static_cast<amf_pts>(m_AverageAVDesyncQueue.size());
            if (averageAVDesync > maxLateBy + m_DesyncToIgnore)
            {
                if (++m_SequentiallyDroppedAudioSamplesCnt < 50)
                {
                    present = false;
                    AMFTraceWarning(AMF_FACILITY, L"Dropping unsynced audio, desync=%5.2fms", float(averageAVDesync) / AMF_MILLISECOND);
                }
                else
                {
                    AMFTraceWarning(AMF_FACILITY, L"Failed to resync audio and video, accepting desync of %5.2fms", float(averageAVDesync) / AMF_MILLISECOND);
                    m_DesyncToIgnore += averageAVDesync;
                    m_SequentiallyDroppedAudioSamplesCnt = 0;
                }
            }
            else if (averageAVDesync < maxLateBy)
            {
                m_SequentiallyDroppedAudioSamplesCnt = 0;
                m_DesyncToIgnore = 0;
            }
        }
    }
    return present;
}
