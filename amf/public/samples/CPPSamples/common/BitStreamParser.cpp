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

#include <assert.h>
#include <string>
#include <cctype>
#include <algorithm>
#include <functional>
#include <iterator>
#include "BitStreamParser.h"
#include "BitStreamParserH264.h"
#include "BitStreamParserH265.h"

BitStreamParser::~BitStreamParser()
{
}

typedef BitStreamParser* (*BitstreamParserCreateFunction)(const wchar_t* fileName);

/*
const std::array<StreamTypeInfo, 5> StreamTypeAssociations = {
    StreamTypeInfo(L".h264", StreamType::H264AnnexB, H264::CreateAnnexBParser),
    StreamTypeInfo(L".avcc", StreamType::H264AvcC, H264::CreateAvcCParser),
    StreamTypeInfo(L".m2v", StreamType::Mpeg2, Mpeg2::CreateParser),
    StreamTypeInfo(L".m4v", StreamType::Mpeg4part2, Mpeg4Visual::CreateParser),
    StreamTypeInfo(L".vc1", StreamType::VC1, VC1::CreateParser)
};
*/
inline std::wstring toUpper(const std::wstring& str)
{
    std::wstring result;
    std::transform(str.begin(), str.end(), std::back_inserter(result), toupper);
    return result;
}

BitStreamType GetStreamType(const wchar_t* path)
{
    const wchar_t ExtDelimiter = L'.';

    std::wstring name(toUpper(path));
    std::wstring::size_type delimiterPos = name.find_last_of(ExtDelimiter);

    if (std::wstring::npos == delimiterPos)
    {
        return BitStreamUnknown;
    }

    const std::wstring ext = name.substr(delimiterPos);

    if(ext == L".H264" || ext == L".264" || ext == L".SVC")
    {
        return BitStreamH264AnnexB;
    }
    if(ext == L".HEVC" || ext == L".265" || ext == L".H265")
    {
        return BitStream265AnnexB;
    }
    if(ext == L".AVCC")
    {
        //CreateAvcCParser(fileName);
    }

    return BitStreamUnknown;
}

BitStreamParserPtr BitStreamParser::Create(amf::AMFDataStream* pStream, BitStreamType type, amf::AMFContext* pContext)
{
    BitStreamParserPtr pParser;
    switch(type)
    {
    case BitStreamH264AnnexB:
        pParser = BitStreamParserPtr(CreateAnnexBParser(pStream, pContext));
        break;
    case BitStream265AnnexB:
        pParser = BitStreamParserPtr(CreateHEVCParser(pStream, pContext));
        break;
    case BitStreamH264AvcC:
    case BitStreamMpeg2:
    case BitStreamMpeg4part2:
    case BitStreamVC1:
    default:
        break;
    }
    return pParser;
}
