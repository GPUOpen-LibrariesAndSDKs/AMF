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

#pragma once

#include "public/include/core/Context.h"
#include "PipelineElement.h"

enum BitStreamType
{
    BitStreamH264AnnexB,
    BitStreamH264AvcC,
    BitStreamMpeg2,
    BitStreamMpeg4part2,
    BitStreamVC1,
    BitStream265AnnexB,
    BitStreamUnknown
};

BitStreamType GetStreamType(const wchar_t* path);

class BitStreamParser;
typedef std::shared_ptr<BitStreamParser> BitStreamParserPtr;

class BitStreamParser : public PipelineElement
{
public:
    virtual ~BitStreamParser();

    virtual amf_int32               GetInputSlotCount() const {return 0;}
    virtual amf_int32               GetOutputSlotCount() const { return 1; }

    virtual int                     GetOffsetX() const = 0;
    virtual int                     GetOffsetY() const = 0;
    virtual int                     GetPictureWidth() const = 0;
    virtual int                     GetPictureHeight() const = 0;
    virtual int                     GetAlignedWidth() const = 0;
    virtual int                     GetAlignedHeight() const = 0;

    virtual void                    SetMaxFramesNumber(amf_size num) = 0;

    virtual const unsigned char*    GetExtraData() const = 0;
    virtual size_t                  GetExtraDataSize() const = 0;
    virtual void                    SetUseStartCodes(bool bUse) = 0;
    virtual void                    SetFrameRate(double fps) = 0;
    virtual double                  GetFrameRate() const = 0;
    virtual void                    GetFrameRate(AMFRate *frameRate) const = 0;
    virtual const wchar_t*          GetCodecComponent() = 0;
    virtual AMF_RESULT              QueryOutput(amf::AMFData** ppData) = 0;
    virtual AMF_RESULT              ReInit() = 0;

public:
    static BitStreamParserPtr       Create(amf::AMFDataStream* pStream, BitStreamType type, amf::AMFContext* pContext);
};

// helpers
namespace Parser
{
    inline char getLowByte(amf_uint16 data)
    {
        return (data >> 8);
    }

    inline char getHiByte(amf_uint16 data)
    {
        return (data & 0xFF);
    }

    inline bool getBit(const amf_uint8 *data, size_t &bitIdx)
    {
        bool ret = (data[bitIdx / 8] >> (7 - bitIdx % 8) & 1);
        bitIdx++;
        return ret;
    }
    inline amf_uint32 getBitToUint32(const amf_uint8 *data, size_t &bitIdx)
    {
        amf_uint32 ret = (data[bitIdx / 8] >> (7 - bitIdx % 8) & 1);
        bitIdx++;
        return ret;
    }

    inline amf_uint32 readBits(const amf_uint8 *data, size_t &startBitIdx, size_t bitsToRead)
    {
        if (bitsToRead > 32)
        {
            return 0; // assert(0);
        }
        amf_uint32 result = 0;
        for (size_t i = 0; i < bitsToRead; i++)
        {
            result = result << 1;
            result |= getBitToUint32(data, startBitIdx); // startBitIdx incremented inside
        }
        return result;
    }

    inline size_t countContiniusZeroBits(const amf_uint8 *data, size_t &startBitIdx)
    {
        size_t startBitIdxOrg = startBitIdx;
        while (getBit(data, startBitIdx) == false) // startBitIdx incremented inside
        {
        }
        startBitIdx--; // remove non zero
        return startBitIdx - startBitIdxOrg;
    }

    namespace ExpGolomb
    {
        inline amf_uint32 readUe(const amf_uint8 *data, size_t &startBitIdx)
        {
            size_t zeroBitsCount = countContiniusZeroBits(data, startBitIdx); // startBitIdx incremented inside
            if (zeroBitsCount > 30)
            {
                return 0; // assert(0)
            }

            amf_uint32 leftPart = (0x1 << zeroBitsCount) - 1;
            startBitIdx++;
            amf_uint32 rightPart = readBits(data, startBitIdx, zeroBitsCount);
            return leftPart + rightPart;
        }

        inline amf_int32 readSe(const amf_uint8 *data, size_t &startBitIdx)
        {
            amf_uint32 ue = readUe(data, startBitIdx);
            // se From Ue 
            amf_uint32 mod2 = ue % 2;
            amf_uint32 half = ue / 2;
            amf_uint32 r = ue / 2 + mod2;

            if (mod2 == 0)
            {
                return r * -1;
            }
            return r;
        }
    }
}

