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

#include "H264Mp4ToAnnexB.h"
#include "public/common/AMFFactory.h"
#include "public/common/TraceAdapter.h"

using namespace amf;

//-------------------------------------------------------------------------------------------------
// class H264Mp4ToAnnexB
//------------------------------------------------------------------------------------------------
#ifndef AV_RB16
#   define AV_RB16(x)                           \
    ((((const amf_uint8*)(x))[0] << 8) |         \
     ((const amf_uint8*)(x))[1])
#endif
#ifndef AV_RB32
#   define AV_RB32(x)                           \
    ((((const amf_uint8*)(x))[0] << 24) |        \
     (((const amf_uint8*)(x))[1] << 16) |        \
     (((const amf_uint8*)(x))[2] <<  8) |        \
     ((const amf_uint8*)(x))[3])
#endif
#ifndef AV_WB32
#   define AV_WB32(p, d) do {                   \
        ((amf_uint8*)(p))[3] = (d);              \
        ((amf_uint8*)(p))[2] = (d)>>8;           \
        ((amf_uint8*)(p))[1] = (d)>>16;          \
        ((amf_uint8*)(p))[0] = (d)>>24;          \
        } while(0)
#endif

#define AMF_INPUT_BUFFER_PADDING_SIZE 8


#ifdef __USE_H264Mp4ToAnnexB
//------------------------------------------------------------------------------------------------
H264Mp4ToAnnexB::H264Mp4ToAnnexB() :
m_lengthSize(0),
m_firstIDR(0),
m_pExtradata(NULL),
m_ExtradataSize(0),
m_pOutBuf(NULL),
m_outBufSize(0)
{
    g_AMFFactory.Init();
}
//-------------------------------------------------------------------------------------------------
H264Mp4ToAnnexB::~H264Mp4ToAnnexB()
{
    if (m_pExtradata != NULL)
    {
        free(m_pExtradata);
    }

    if (m_pOutBuf != NULL)
    {
        free(m_pOutBuf);
    }

    g_AMFFactory.Terminate();
}
//-------------------------------------------------------------------------------------------------
static const amf_uint8 naluHeader[4] = { 0, 0, 0, 1 };
int H264Mp4ToAnnexB::ProcessExtradata(const amf_uint8* pExtraData, amf_size extraDataSize)
{
    amf_uint16 unitSize;
    amf_size totalSize = 0;
    amf_uint8 *pOutData = NULL;
    amf_uint8 unitNB = 0;
    amf_uint8 spsDone = 0;
    amf_uint8 spsSeen = 0;
    amf_uint8 ppsSeen = 0;

    // check if data already parocessed - if get annexB streams
    if (extraDataSize < 4)
    {
        return 1;
    }
    if (pExtraData[0] != 1 && memcmp(pExtraData, naluHeader, 4) == 0)
    {
        m_pExtradata = (amf_uint8*)malloc(extraDataSize);
        memcpy(m_pExtradata, pExtraData, extraDataSize);
        m_ExtradataSize = extraDataSize;
        m_firstIDR = 1;
        return 0;
    }

    const amf_uint8* extradata = pExtraData + 4;

    // retrieve length coded size for future use - AVCC nal units have size of this length at the beginning
    m_lengthSize = (*extradata++ & 0x3) + 1;
    if (m_lengthSize == 3)
    {
        return 1; // error - wrong value
    }

    // retrieve sps and pps unit(s)
    unitNB = *extradata++ & 0x1f; // number of sps unit(s) 
    if (!unitNB)
    {
        goto pps;
    }
    else
    {
        spsSeen = 1;
    }

    while (unitNB--)
    {
        void *tmp;

        unitSize = AV_RB16(extradata);
        totalSize += unitSize + 4;
        if (totalSize > INT_MAX - AMF_INPUT_BUFFER_PADDING_SIZE ||
            extradata + 2 + unitSize > pExtraData + extraDataSize) {
            free(pOutData);
            return 1;
        }
        tmp = realloc(pOutData, totalSize + AMF_INPUT_BUFFER_PADDING_SIZE);
        if (!tmp) {
            free(pOutData);
            return 1;
        }
        pOutData = (amf_uint8*)tmp;
        memcpy(pOutData + totalSize - unitSize - 4, naluHeader, 4);
        memcpy(pOutData + totalSize - unitSize, extradata + 2, unitSize);
        extradata += 2 + unitSize;
    pps:
        if (!unitNB && !spsDone++) {
            unitNB = *extradata++; // number of pps unit(s) 
            if (unitNB)
                ppsSeen = 1;
        }
    }

    if (pOutData)
        memset(pOutData + totalSize, 0, AMF_INPUT_BUFFER_PADDING_SIZE);

    if (!spsSeen){
        AMFTraceError(AMF_FACILITY, L"ProcessExtradata() - Warning: SPS NALU missing or invalid. The resulting stream may not play. ");
    }
    if (!ppsSeen){
        AMFTraceError(AMF_FACILITY, L"ProcessExtradata() - Warning: PPS NALU missing or invalid. The resulting stream may not play. ");
    }
    m_pExtradata = pOutData;
    m_ExtradataSize = totalSize;
    m_firstIDR = 1;
    return 0;
}
//-------------------------------------------------------------------------------------------------
int H264Mp4ToAnnexB::Filter(amf_uint8** pOutBuf, amf_size* pOutBufSize, amf_uint8* pBuf, amf_size bufSize)
{
    amf_uint8 unitType = 0;
    amf_int32 nalSize = 0;
    amf_uint32 cumulSize = 0;
    const amf_uint8* pBufEnd = pBuf + bufSize;

    // check if data already parocessed - if get annexB streams
    if (bufSize>4 && memcmp(pBuf, naluHeader, 4) == 0)
    {
        *pOutBuf = pBuf;
        *pOutBufSize = bufSize;

        return 0;
    }


    /* nothing to filter */
    if (!m_pExtradata || m_ExtradataSize < 6 || m_lengthSize == 0)
    {
        *pOutBuf = pBuf;
        *pOutBufSize = bufSize;

        return 0;
    }

    *pOutBufSize = 0;
    *pOutBuf = NULL;

    AMF_RESULT res = AMF_OK;

    do
    {
        if (pBuf + m_lengthSize > pBufEnd)
        {
            res = AMF_FAIL;
            break;
        }

        if (m_lengthSize == 1)
        {
            nalSize = pBuf[0];
        }
        else if (m_lengthSize == 2)
        {
            nalSize = AV_RB16(pBuf);
        }
        else
        {
            nalSize = AV_RB32(pBuf);
        }

        pBuf += m_lengthSize;
        unitType = *pBuf & 0x1f;

        if (unitType == 0) //MM added this condition to remove trailing zeros
        {
            break;
        }

        if (pBuf + nalSize > pBufEnd || nalSize < 0)
        {
            res = AMF_FAIL;
        }

        /* prepend only to the first type 5 NAL unit of an IDR picture */
        //MM - 6 comes first in some files - try this
        //        if (first_idr && unit_type == 5 ) {
        if (m_firstIDR && (unitType == 5 || unitType == 6 || unitType == 1))
        {
            if (AllocAndCopy(pOutBufSize, m_pExtradata, m_ExtradataSize, pBuf, nalSize) < 0)
            {
                res = AMF_FAIL;
                break;
            }
            m_firstIDR = 0;
        }
        else
        {
            if (AllocAndCopy(pOutBufSize, NULL, 0, pBuf, nalSize) < 0)
            {
                res = AMF_FAIL;
                break;
            }
        }

        pBuf += nalSize;
        cumulSize += nalSize + m_lengthSize;
    } while (cumulSize < bufSize);

    if (res == AMF_OK)
    {
        *pOutBuf = m_pOutBuf;
        return 1;
    }
    else
    {
        if (pOutBuf != NULL)
        {
            free(pOutBuf);
        }

        *pOutBufSize = 0;

        return 0;
    }


}
//-------------------------------------------------------------------------------------------------
int H264Mp4ToAnnexB::AllocAndCopy(amf_size* pOutBufSize, const amf_uint8* pSpsPps, amf_size spsPpsSize, const amf_uint8* pInData, amf_size inDataSize)
{
    amf_size offset = *pOutBufSize;
    amf_uint8 nal_header_size = offset ? 3 : 4;

    *pOutBufSize += spsPpsSize + inDataSize + nal_header_size;

    if (*pOutBufSize > m_outBufSize) //MM fast realloc
    {
        m_pOutBuf = reinterpret_cast<amf_uint8*>(realloc(m_pOutBuf, *pOutBufSize));
        m_outBufSize = *pOutBufSize;
    }
    if (!m_pOutBuf)
    {
        return -1;
    }

    if (pSpsPps)
    {
        memcpy(m_pOutBuf + offset, pSpsPps, spsPpsSize);
    }

    memcpy(m_pOutBuf + spsPpsSize + nal_header_size + offset, pInData, inDataSize);

    if (!offset)
    {
        AV_WB32(m_pOutBuf + spsPpsSize, 1);
    }
    else
    {
        (m_pOutBuf + offset + spsPpsSize)[0] = 0;
        (m_pOutBuf + offset + spsPpsSize)[1] = 0;
        (m_pOutBuf + offset + spsPpsSize)[2] = 1;
    }

    return 0;
}
//-------------------------------------------------------------------------------------------------
#endif// __USE_H264Mp4ToAnnexB