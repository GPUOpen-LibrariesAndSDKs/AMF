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

#include "BitStreamParserH264.h"

#include <vector>
#include <map>

#include "public/include/components/VideoDecoderUVD.h"

static const amf_uint16 maxSpsSize = 0xFFFF;
static const amf_uint16 minSpsSize = 5;
static const amf_uint16 maxPpsSize = 0xFFFF;
static const amf_uint8 NalUnitLengthSize = 4U;

class ExtraDataAvccBuilder
{
public:
    ExtraDataAvccBuilder() : m_SPSCount(0), m_PPSCount(0){}

    void AddSPS(amf_uint8 *sps, size_t size);
    void AddPPS(amf_uint8 *pps, size_t size);
    bool SetAnnexB(amf_uint8 *data, size_t size);
    bool GetExtradata(AMFByteArray   &extradata);

private:
    AMFByteArray   m_SPSs;
    AMFByteArray   m_PPSs;
    amf_int32       m_SPSCount;
    amf_int32       m_PPSCount;
};

//-------------------------------------------------------------------------------------------------
class AvcParser : public BitStreamParser
{
public:
    friend class ExtraDataAvccBuilder;

    AvcParser(amf::AMFDataStream* stream, amf::AMFContext* pContext);
    virtual ~AvcParser();

    virtual int                     GetOffsetX() const;
    virtual int                     GetOffsetY() const;
    virtual int                     GetPictureWidth() const;
    virtual int                     GetPictureHeight() const;
    virtual int                     GetAlignedWidth() const;
    virtual int                     GetAlignedHeight() const;

    virtual void                    SetMaxFramesNumber(amf_size num) { m_maxFramesNumber = num; }

    virtual const unsigned char*    GetExtraData() const;
    virtual size_t                  GetExtraDataSize() const;
    virtual void                    SetUseStartCodes(bool bUse);
    virtual void                    SetFrameRate(double fps);
    virtual double                  GetFrameRate()  const;
    virtual void                    GetFrameRate(AMFRate *frameRate) const;

    virtual const wchar_t*          GetCodecComponent() {return AMFVideoDecoderUVD_H264_AVC;}

    virtual AMF_RESULT              QueryOutput(amf::AMFData** ppData);
    virtual AMF_RESULT              ReInit();

protected:

    enum NalUnitType
    {
        NalUnitTypeUnspecified = 0,
        NalUnitTypeSliceOfNonIdrPicture = 1,
        NalUnitTypeSliceDataPartitionA = 2,
        NalUnitTypeSliceDataPartitionB = 3,
        NalUnitTypeSliceDataPartitionC = 4,
        NalUnitTypeSliceIdrPicture = 5,
        NalUnitTypeSupplementalEnhancementInformation = 6,
        NalUnitTypeSequenceParameterSet = 7,
        NalUnitTypePictureParameterSet = 8,
        NalUnitTypeAccessUnitDelimiter = 9,
        NalUnitTypeEndOfSequence = 10,
        NalUnitTypeEndOfStream = 11,
        NalUnitTypeFillerData = 12,
        NalUnitTypeSequenceParameterSetExtension = 13,
        NalUnitTypePrefixNalUnit = 14,
        NalUnitTypeSubsetSequenceParameterSet = 15,
        NalUnitTypeReserved16 = 16,
        NalUnitTypeReserved17 = 17,
        NalUnitTypeReserved18 = 18,
        NalUnitTypeSliceOfAnAuxiliaryCoded = 19,
        NalUnitTypeSliceExtension = 20,
        NalUnitTypeSliceExtensionForDepthView = 21,
        NalUnitTypeReserved22 = 22,
        NalUnitTypeReserved23 = 23,
        NalUnitTypeUnspecified24 = 24,
        NalUnitTypeUnspecified25 = 25,
        NalUnitTypeUnspecified26 = 26,
        NalUnitTypeUnspecified27 = 27,
        NalUnitTypeUnspecified28 = 28,
        NalUnitTypeUnspecified29 = 29,
        NalUnitTypeUnspecified30 = 30,
        NalUnitTypeUnspecified31 = 31
    };

    // ISO-IEC 14496-15-2004.pdf, page 14, table 1 " NAL unit types in elementary streams.
    struct SpsData
    {
        amf_uint8 ProfileIdc;
        //bool ConstraintSet0;
        //bool ConstraintSet1;
        //bool ConstraintSet2;
        //bool ConstraintSet3;
        //bool ConstraintSet4;
        //bool ConstraintSet5;
        amf_uint8 LevelIdc;
        amf_uint32 Id;
        bool SeparateColourPlane;
        amf_uint32 ChromaFormatIdc;
        amf_uint32 Log2MaxFrameNumMinus4;
        amf_uint32 Log2MaxPicOrderCntLsbMinus4;
        amf_uint32 PicOrderCntType;
        amf_uint32 MaxNumRefFrames;
        bool DeltaPicOrderAlwaysZero;
        amf_uint32 PicWidthInMbsMinus1;
        amf_uint32 PicHeightInMapUnitsMinus1;
        bool FrameMbsOnlyFlag;

        bool FrameCroppingFlag;
        amf_uint32 FrameCroppingRectLeftOffset;
        amf_uint32 FrameCroppingRectRightOffset;
        amf_uint32 FrameCroppingRectTopOffset;
        amf_uint32 FrameCroppingRectBottomOffset;
        // VUI 
        bool timing_info_present_flag;
        amf_uint32 num_units_in_tick;
        amf_uint32 time_scale;

        SpsData(void)
            : ProfileIdc(0),
            LevelIdc(0),
            Id(0),
            SeparateColourPlane(0),
            ChromaFormatIdc(1),
            Log2MaxFrameNumMinus4(0),
            Log2MaxPicOrderCntLsbMinus4(0),
            PicOrderCntType(0),
            MaxNumRefFrames(0),
            DeltaPicOrderAlwaysZero(false),
            PicWidthInMbsMinus1(0),
            PicHeightInMapUnitsMinus1(0),
            FrameMbsOnlyFlag(false),
            FrameCroppingFlag(false),
            FrameCroppingRectLeftOffset(0),
            FrameCroppingRectRightOffset(0),
            FrameCroppingRectTopOffset(0),
            FrameCroppingRectBottomOffset(0),
            timing_info_present_flag(false),
            num_units_in_tick(0),
            time_scale(0)
        {
        }
        bool Parse(amf_uint8 *data, size_t size);
    };
    struct PpsData
    {
        amf_uint32 Id;
        amf_uint32 SpsId;
        bool EntropyCodingMode;
        bool BottomFieldPicOrderInFramePresent;

        PpsData(void)
            : Id(0),
            SpsId(0),
            EntropyCodingMode(false),
            BottomFieldPicOrderInFramePresent(false)
        {
        }
        bool Parse(amf_uint8 *data, size_t size);
    };
    // See ITU-T Rec. H.264 (04/2013) Advanced video coding for generic audiovisual services, page 28, 91.
    struct AccessUnitSigns
    {
        amf_uint32 FrameNum;
        amf_uint32 PicParameterSetId;
        bool FieldPicFlag;
        bool BottomFieldFlag;
        amf_uint32 NalRefIdc;
        amf_uint32 PicOrderCntType;
        amf_uint32 PicOrderCntLsb;
        amf_int32 DeltaPicOrderCntBottom;
        amf_int32 DeltaPicOrderCnt0;
        amf_int32 DeltaPicOrderCnt1;
        bool IdrPicFlag;
        amf_uint32 IdrPicId;

        AccessUnitSigns() :
            FrameNum(0),
            PicParameterSetId(-1), // flag that not -init
            FieldPicFlag(0),
            BottomFieldFlag(0),
            NalRefIdc(0),
            PicOrderCntType(0),
            PicOrderCntLsb(0),
            DeltaPicOrderCntBottom(0),
            DeltaPicOrderCnt0(0),
            DeltaPicOrderCnt1(0),
            IdrPicFlag(0),
            IdrPicId(0)
        {}
        bool Parse(amf_uint8 *data, size_t size, std::map<amf_uint32,SpsData> &spsMap, std::map<amf_uint32,PpsData> &ppsMap);
        bool IsNewPicture(const AccessUnitSigns &other);
    };

    friend struct AccessUnitSigns;

    static const amf_uint32 MacroblocSize = 16;
    static const amf_uint8 NalUnitTypeMask = 0x1F; // b00011111
    static const amf_uint8 NalRefIdcMask = 0x60;   // b01100000

    static const size_t m_ReadSize = 1024*4;


    NalUnitType   ReadNextNaluUnit(size_t *offset, size_t *nalu, size_t *size);
    void          FindSPSandPPS();
    static inline NalUnitType GetNaluUnitType(amf_uint8 data)
    {
        return (NalUnitType)(data  & NalUnitTypeMask);
    }
    size_t EBSPtoRBSP(amf_uint8 *streamBuffer,size_t begin_bytepos, size_t end_bytepos);


    AMFByteArray   m_ReadData;
    AMFByteArray   m_Extradata;
    
    AMFByteArray   m_EBSPtoRBSPData;

    bool           m_bUseStartCodes;
    amf_pts        m_currentFrameTimestamp;
    amf::AMFDataStreamPtr m_pStream;
    std::map<amf_uint32,SpsData> m_SpsMap;
    std::map<amf_uint32,PpsData> m_PpsMap;
    amf_size       m_PacketCount;
    AccessUnitSigns m_currentAccessUnitsSigns;
    bool            m_bEof;
    double          m_fps;
    amf_size        m_maxFramesNumber;
    amf::AMFContext* m_pContext;
};
//-------------------------------------------------------------------------------------------------
BitStreamParser* CreateAnnexBParser(amf::AMFDataStream* stream, amf::AMFContext* pContext)
{
    return new AvcParser(stream, pContext);
}
//-------------------------------------------------------------------------------------------------

BitStreamParser* CreateAvcCParser(amf::AMFDataStream* stream, amf::AMFContext* pContext)
{
    return NULL;
}
//-------------------------------------------------------------------------------------------------
AvcParser::AvcParser(amf::AMFDataStream* stream, amf::AMFContext* pContext) :
    m_bUseStartCodes(false),
    m_currentFrameTimestamp(0),
    m_pStream(stream),
    m_PacketCount(0),
    m_bEof(false),
    m_fps(0),
    m_maxFramesNumber(0),
    m_pContext(pContext)
{
    stream->Seek(amf::AMF_SEEK_BEGIN, 0, NULL);
    FindSPSandPPS();
}
//-------------------------------------------------------------------------------------------------
AvcParser::~AvcParser()
{
//    LOG_DEBUG(L"AvcParser: parsed frames:" << m_PacketCount << L"\n");
}
//-------------------------------------------------------------------------------------------------
static const int SubWidthC  [4]= { 1, 2, 2, 1};
static const int SubHeightC [4]= { 1, 2, 1, 1};
static const int MacroblockSize = 16;

int  AvcParser::GetOffsetX() const
{
    int offsetX = 0;
    if(m_SpsMap.size() == 0)
    {
        return offsetX;
    }
    const SpsData &sps = m_SpsMap.cbegin()->second;
    if(sps.FrameCroppingFlag)
    {
        //MM we need not cropped area but -nonaligned allocated area
        offsetX = SubWidthC[sps.ChromaFormatIdc] * sps.FrameCroppingRectLeftOffset;
    }
    return offsetX;
}

int  AvcParser::GetOffsetY() const
{
    int offsetY = 0;
    if(m_SpsMap.size() == 0)
    {
        return 0;
    }
    const SpsData &sps = m_SpsMap.cbegin()->second;
    if(sps.FrameCroppingFlag)
    {
        //MM we need not cropped area but -nonaligned allocated area
        offsetY = SubHeightC[sps.ChromaFormatIdc]*( 2 - sps.FrameMbsOnlyFlag ) * sps.FrameCroppingRectTopOffset;
    }
    return offsetY;
}

int AvcParser::GetPictureWidth() const
{
    if(m_SpsMap.size() == 0)
    {
        return 0;
    }
    const SpsData &sps = m_SpsMap.cbegin()->second;
    int width = (sps.PicWidthInMbsMinus1 + 1) * MacroblocSize;
    if(sps.FrameCroppingFlag)
    {
        width -= SubWidthC[sps.ChromaFormatIdc] * sps.FrameCroppingRectLeftOffset;
        width -= SubWidthC[sps.ChromaFormatIdc] * sps.FrameCroppingRectRightOffset;
    }

    return width;//(sps.PicWidthInMbsMinus1 + 1) * MacroblocSize;
}
//-------------------------------------------------------------------------------------------------
int AvcParser::GetPictureHeight() const
{
    if(m_SpsMap.size() == 0)
    {
        return 0;
    }
    const SpsData &sps = m_SpsMap.cbegin()->second;
    const amf_uint32 picHeightInMapUnits = sps.PicHeightInMapUnitsMinus1 + 1;
    const amf_uint32 frameHeightInMbs = sps.FrameMbsOnlyFlag ? picHeightInMapUnits : picHeightInMapUnits * 2; 

    int height = frameHeightInMbs * MacroblocSize;
    if(sps.FrameCroppingFlag)
    {
        height -= SubHeightC[sps.ChromaFormatIdc]*( 2 - sps.FrameMbsOnlyFlag ) * sps.FrameCroppingRectTopOffset;
        height -= SubHeightC[sps.ChromaFormatIdc] * ( 2 - sps.FrameMbsOnlyFlag) * sps.FrameCroppingRectBottomOffset;
    }

    return height; // return height aligned to microblocks. VideoDecoder will add ROI to AMFSurface if needed.
}
//-------------------------------------------------------------------------------------------------
int AvcParser::GetAlignedWidth() const
{
    if(m_SpsMap.size() == 0)
    {
        return 0;
    }
    const SpsData &sps = m_SpsMap.cbegin()->second;
    int width = (sps.PicWidthInMbsMinus1 + 1) * MacroblocSize;
    return width;//(sps.PicWidthInMbsMinus1 + 1) * MacroblocSize;
}
//-------------------------------------------------------------------------------------------------
int AvcParser::GetAlignedHeight() const
{
    if(m_SpsMap.size() == 0)
    {
        return 0;
    }
    const SpsData &sps = m_SpsMap.cbegin()->second;
    const amf_uint32 picHeightInMapUnits = sps.PicHeightInMapUnitsMinus1 + 1;
    const amf_uint32 frameHeightInMbs = sps.FrameMbsOnlyFlag ? picHeightInMapUnits : picHeightInMapUnits * 2; 

    int height = frameHeightInMbs * MacroblocSize;
    return height; // return height aligned to microblocks. VideoDecoder will add ROI to AMFSurface if needed.
}
//-------------------------------------------------------------------------------------------------
const unsigned char* AvcParser::GetExtraData() const
{
    return m_Extradata.GetData();
}
//-------------------------------------------------------------------------------------------------
size_t AvcParser::GetExtraDataSize() const
{
    return m_Extradata.GetSize();
};
//-------------------------------------------------------------------------------------------------
void AvcParser::SetUseStartCodes(bool bUse)
{
    m_bUseStartCodes = bUse;
}
//-------------------------------------------------------------------------------------------------
void AvcParser::SetFrameRate(double fps)
{
    m_fps = fps;
}
//-------------------------------------------------------------------------------------------------
double AvcParser::GetFrameRate()  const
{
    if(m_fps != 0)
    {
        return m_fps;
    }
    if(m_SpsMap.size() > 0)
    {
        const SpsData &sps = m_SpsMap.cbegin()->second;
        if(sps.timing_info_present_flag && sps.num_units_in_tick != 0)
        {
            // according to the latest h264 standard nuit_field_based_flag is always = 1 and therefore this must be divided by two 
            // some old clips may get wrong FPS. This is just a sample. Use container information
            return (double)sps.time_scale / sps.num_units_in_tick / 2;
        }
    }
    return 25.0;
}
//-------------------------------------------------------------------------------------------------
void     AvcParser::GetFrameRate(AMFRate *frameRate) const
{
    if(m_SpsMap.size() > 0)
    {
        const SpsData &sps = m_SpsMap.cbegin()->second;
        if(sps.timing_info_present_flag && sps.num_units_in_tick != 0)
        {
            // according to the latest h264 standard nuit_field_based_flag is always = 1 and therefore this must be divided by two 
            // some old clips may get wrong FPS. This is just a sample. Use container information
            frameRate->num = sps.time_scale / 2;
            frameRate->den = sps.num_units_in_tick;
            return;
        }
    }
    frameRate->num = 0;
    frameRate->den = 0;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT AvcParser::QueryOutput(amf::AMFData** ppData)
{
    if(m_bFrozen)
    {
        return AMF_OK;
    }

    if((m_bEof && m_ReadData.GetSize() == 0) || (m_maxFramesNumber && m_PacketCount >= m_maxFramesNumber))
    {
        return AMF_EOF;
    }
    bool newPictureDetected = false;
    size_t packetSize = 0;
    size_t readSize = 0;
    std::vector<size_t> naluStarts;
    std::vector<size_t> naluSizes;
    size_t dataOffset = 0;
    bool bSliceFound = false;
    do 
    {
        
        size_t naluSize = 0;
        size_t naluOffset = 0;
        size_t naluAnnexBOffset = dataOffset;
        NalUnitType   naluType = ReadNextNaluUnit(&dataOffset, &naluOffset, &naluSize);

        if (naluType == NalUnitTypeSequenceParameterSet)
        {
            m_EBSPtoRBSPData.SetSize(naluSize);
            memcpy(m_EBSPtoRBSPData.GetData(), m_ReadData.GetData() + naluOffset, naluSize);
            size_t newNaluSize = EBSPtoRBSP(m_EBSPtoRBSPData.GetData(),0, naluSize);

            SpsData sps;
            sps.Parse(m_EBSPtoRBSPData.GetData(), newNaluSize);
            m_SpsMap[sps.Id] = sps;
        }
        else if (naluType == NalUnitTypePictureParameterSet)
        {
            m_EBSPtoRBSPData.SetSize(naluSize);
            memcpy(m_EBSPtoRBSPData.GetData(), m_ReadData.GetData() + naluOffset, naluSize);
            size_t newNaluSize = EBSPtoRBSP(m_EBSPtoRBSPData.GetData(),0, naluSize);

            PpsData pps;
            pps.Parse(m_EBSPtoRBSPData.GetData(), newNaluSize);
            m_PpsMap[pps.Id] = pps;
        }
        else if(NalUnitTypeAccessUnitDelimiter == naluType)
        {
            if(packetSize > 0)
            {
                newPictureDetected = true;
            }
            else
            {
                m_currentAccessUnitsSigns.PicParameterSetId = amf_uint32(-1);
            }
        }
        else if(NalUnitTypePrefixNalUnit == naluType)
        {
            if(bSliceFound)
            {
                newPictureDetected = true;
                m_currentAccessUnitsSigns.PicParameterSetId = amf_uint32(-1);
            }
        }
        else if (NalUnitTypeSliceDataPartitionA == naluType ||
            NalUnitTypeSliceIdrPicture == naluType ||
            NalUnitTypeSliceOfNonIdrPicture == naluType)
        {
            bSliceFound = true;  
            AccessUnitSigns naluAccessUnitsSigns;

            m_EBSPtoRBSPData.SetSize(naluSize);
            memcpy(m_EBSPtoRBSPData.GetData(), m_ReadData.GetData() + naluOffset, naluSize);
            size_t newNaluSize = EBSPtoRBSP(m_EBSPtoRBSPData.GetData(),0, naluSize);

            naluAccessUnitsSigns.Parse(m_EBSPtoRBSPData.GetData(), newNaluSize, m_SpsMap, m_PpsMap);

            if (m_currentAccessUnitsSigns.PicParameterSetId == amf_uint32(-1))
            {
                m_currentAccessUnitsSigns = naluAccessUnitsSigns;
            }
            else
            {
                newPictureDetected = m_currentAccessUnitsSigns.IsNewPicture(naluAccessUnitsSigns);
                if (newPictureDetected)
                {
                    m_currentAccessUnitsSigns = naluAccessUnitsSigns;
                }
            }
        }

        if(naluSize > 0 && !newPictureDetected)
        {
            packetSize += naluSize;
            if(!m_bUseStartCodes)
            {
                packetSize += NalUnitLengthSize;
                naluStarts.push_back(naluOffset);
                naluSizes.push_back(naluSize);
            }
            else
            {
                size_t startCodeSize = naluOffset - naluAnnexBOffset;
                packetSize += startCodeSize;
            }
        }
        if(!newPictureDetected)
        {
            readSize = dataOffset;
        }
        if(naluType == NalUnitTypeUnspecified)
        {
            break;
        }
    } while (!newPictureDetected);


    amf::AMFBufferPtr pictureBuffer;
    AMF_RESULT ar = m_pContext->AllocBuffer(amf::AMF_MEMORY_HOST, packetSize, &pictureBuffer);

    amf_uint8 *data = (amf_uint8*)pictureBuffer->GetNative();
    if(m_bUseStartCodes)
    {
        memcpy(data, m_ReadData.GetData(), packetSize);
    }
    else
    {
        for( size_t i=0; i < naluStarts.size(); i++)
        {
            // copy size
            amf_uint32 naluSize= (amf_uint32)naluSizes[i];
            *data++ = (naluSize >> 24);
            *data++ = ((naluSize & 0x00FF0000) >> 16);
            *data++ = ((naluSize & 0x0000FF00) >> 8);
            *data++ = ((naluSize & 0x000000FF));

            memcpy(data, m_ReadData.GetData() + naluStarts[i], naluSize);
            data += naluSize;
        }
    }

    pictureBuffer->SetPts(m_currentFrameTimestamp);
    amf_pts frameDuration = amf_pts(AMF_SECOND / GetFrameRate()); // In 100 NanoSeconds
    pictureBuffer->SetDuration(frameDuration);
    m_currentFrameTimestamp += frameDuration;

//    if (newPictureDetected)
    {
    // shift remaining data in m_ReadData
        size_t remainingData = m_ReadData.GetSize() - readSize;
        memmove(m_ReadData.GetData(), m_ReadData.GetData()+readSize, remainingData);
        m_ReadData.SetSize(remainingData);
    }
    *ppData = pictureBuffer.Detach();
    m_PacketCount++;
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AvcParser::NalUnitType   AvcParser::ReadNextNaluUnit(size_t *offset, size_t *nalu, size_t *size)
{
    *size = 0;
    size_t startOffset = *offset;

    bool newNalFound = false;
    size_t zerosCount = 0;

    while(!newNalFound)
    {
        // read next portion if needed
        size_t ready = m_ReadData.GetSize() - *offset;
        if(ready == 0)
        {
            m_ReadData.SetSize(m_ReadData.GetSize()+m_ReadSize);
            ready = 0;
            m_pStream->Read(m_ReadData.GetData() + *offset, m_ReadSize, &ready);
            if(ready == 0 )
            {
                m_bEof = true;
                newNalFound = startOffset != *offset; 
                *offset = m_ReadData.GetSize();
                break; // EOF
            }
        }
        amf_uint8* data= m_ReadData.GetData() + *offset;
        for(size_t i = 0; i < ready; i++)
        {
            amf_uint8 ch = *data++;
            if (0 == ch)
            {
                zerosCount++;
            }
            else 
            {
                if (1 == ch && zerosCount > 1) // We found a start code in Annex B stream
                {
                    if(*offset + (i - zerosCount) > startOffset)
                    {
                        ready = i - zerosCount;
                        newNalFound = true; // new NAL
                        break; 
                    }
                    else
                    {
                        *nalu = *offset + zerosCount + 1;
                    }
                }
                zerosCount = 0;
            }
        }
        // if zeros found but not a new NAL - continue with zerosCount on the next iteration
        *offset += ready;
    }
    if(!newNalFound)
    {
        return NalUnitTypeUnspecified; // EOF
    }
    *size = *offset - *nalu;
    // get NAL type
    return GetNaluUnitType(*(m_ReadData.GetData() + *nalu));
}
//-------------------------------------------------------------------------------------------------
void    AvcParser::FindSPSandPPS()
{
    ExtraDataAvccBuilder extraDataBuilder;

    bool bSPSFound = false;
    bool bPPSFound = false;
    size_t dataOffset = 0;
    do 
    {
        
        size_t naluSize = 0;
        size_t naluOffset = 0;
        size_t naluAnnexBOffset = dataOffset;
        NalUnitType   naluType = ReadNextNaluUnit(&dataOffset, &naluOffset, &naluSize);

        if (naluType == NalUnitTypeUnspecified )
        {
            break; // EOF
        }

        if (naluType == NalUnitTypeSequenceParameterSet)
        {
            m_EBSPtoRBSPData.SetSize(naluSize);
            memcpy(m_EBSPtoRBSPData.GetData(), m_ReadData.GetData() + naluOffset, naluSize);
            size_t newNaluSize = EBSPtoRBSP(m_EBSPtoRBSPData.GetData(),0, naluSize);

            SpsData sps;
            sps.Parse(m_EBSPtoRBSPData.GetData(), newNaluSize);
            m_SpsMap[sps.Id] = sps;
            extraDataBuilder.AddSPS(m_ReadData.GetData()+naluOffset, naluSize);
            bSPSFound = true;
        }
        else if (naluType == NalUnitTypePictureParameterSet)
        {
            m_EBSPtoRBSPData.SetSize(naluSize);
            memcpy(m_EBSPtoRBSPData.GetData(), m_ReadData.GetData() + naluOffset, naluSize);
            size_t newNaluSize = EBSPtoRBSP(m_EBSPtoRBSPData.GetData(),0, naluSize);

            PpsData pps;
            pps.Parse(m_EBSPtoRBSPData.GetData(), newNaluSize);
            m_PpsMap[pps.Id] = pps;
            extraDataBuilder.AddPPS(m_ReadData.GetData()+naluOffset, naluSize);
            bPPSFound = true;
        }
        else if (   /*naluType == static_cast<amf_uint8>(NalUnitTypeSliceDataPartitionA) ||
                    naluType == static_cast<amf_uint8>(NalUnitTypeSliceDataPartitionB) ||
                    naluType == static_cast<amf_uint8>(NalUnitTypeSliceDataPartitionC) || */
                    naluType == static_cast<amf_uint8>(NalUnitTypeSliceIdrPicture) ||
                    naluType == static_cast<amf_uint8>(NalUnitTypeSliceOfNonIdrPicture) )
        {
            if(bSPSFound && bPPSFound)
            {
                break; // frame data
            }
        }
    } while (true);

    m_pStream->Seek(amf::AMF_SEEK_BEGIN, 0, NULL);
    m_ReadData.SetSize(0);
    // It will fail if SPS or PPS are absent
    extraDataBuilder.GetExtradata(m_Extradata);
}
//-------------------------------------------------------------------------------------------------
bool AvcParser::SpsData::Parse(amf_uint8 *nalu, size_t size)
{
    ProfileIdc = nalu[1];
    LevelIdc = nalu[3];

    size_t offset = 32; // 4 bytes

    Id = Parser::ExpGolomb::readUe(nalu, offset);

    // See ITU-T Rec. H.264 (04/2013) Advanced video coding for generic audiovisual services, page 64
    if( ProfileIdc == 100 ||
        ProfileIdc == 110 ||
        ProfileIdc == 122 || 
        ProfileIdc == 244 || 
        ProfileIdc == 44 ||
        ProfileIdc == 83 || 
        ProfileIdc == 86 || 
        ProfileIdc == 118 ||
        ProfileIdc == 128 || 
        ProfileIdc == 138 )
    {
        ChromaFormatIdc = Parser::ExpGolomb::readUe(nalu, offset);

        if (3 == ChromaFormatIdc)
        {
            SeparateColourPlane = Parser::getBit(nalu, offset);
        }

        amf_uint32 bitDepthLumaMinus8 = Parser::ExpGolomb::readUe(nalu, offset);
        amf_uint32 bitDepthChromaMinus8 = Parser::ExpGolomb::readUe(nalu, offset);
        bool qpPrimeYZeroTransformBypass = Parser::getBit(nalu, offset);
        bool seqScalingMatrixPresent = Parser::getBit(nalu, offset);

        if (seqScalingMatrixPresent)
        {
            size_t iterationsCount = 3 == ChromaFormatIdc ? 12 : 8;
            for (size_t i = 0; i < iterationsCount; i++)
            {
                bool seqScalingListPresent = Parser::getBit(nalu, offset);
                if (seqScalingListPresent)
                {
                    amf_uint32 lastScale = 8;
                    amf_uint32 nextScale = 8;

                    size_t sizeOfScalingList = i < 6 ? 16 : 64;
                    for (size_t j = 0; j < sizeOfScalingList; j++)
                    {
                        if(nextScale)
                        {
                            nextScale = (lastScale + Parser::ExpGolomb::readSe(nalu, offset)) & 0xFF;
                        }
//                        amf_int32 deltaScale = Parser::ExpGolomb::readSe(nalu, offset);
//                        if (nextScale != 0)
//                        {
//                            nextScale = (lastScale + deltaScale + 256 ) % 256;
//                        }

                        lastScale = (0 == nextScale) ? lastScale : nextScale;
                    }
                }
            }
        }
    }

    Log2MaxFrameNumMinus4 = Parser::ExpGolomb::readUe(nalu, offset);
    PicOrderCntType = Parser::ExpGolomb::readUe(nalu, offset);
    if (0 == PicOrderCntType)
    {
        Log2MaxPicOrderCntLsbMinus4 = Parser::ExpGolomb::readUe(nalu, offset);
    }
    else if (1 == PicOrderCntType)
    {
        DeltaPicOrderAlwaysZero = Parser::getBit(nalu, offset);
        amf_int32 offsetForNonRefPic = Parser::ExpGolomb::readSe(nalu, offset);
        amf_int32 offsetForTopToBottomField = Parser::ExpGolomb::readSe(nalu, offset);
        amf_uint32 numRefFramesInPicOrderCntCycle = Parser::ExpGolomb::readUe(nalu, offset);

        for(size_t i = 0; i < numRefFramesInPicOrderCntCycle; i++)
        {
            amf_int32 offsetForRefFrame = Parser::ExpGolomb::readSe(nalu, offset);
        }
    }

    MaxNumRefFrames = Parser::ExpGolomb::readUe(nalu, offset);
    bool gapsInFrameNumValueAllowedFlag = Parser::getBit(nalu, offset);
    PicWidthInMbsMinus1 = Parser::ExpGolomb::readUe(nalu, offset);
    PicHeightInMapUnitsMinus1 = Parser::ExpGolomb::readUe(nalu, offset);
    FrameMbsOnlyFlag = Parser::getBit(nalu, offset);

    if (!FrameMbsOnlyFlag)
    {
        bool mb_adaptive_frame_field_flag = Parser::getBit(nalu, offset);
    }
    bool direct_8x8_inference_flag = Parser::getBit(nalu, offset);
    FrameCroppingFlag = Parser::getBit(nalu, offset);
    if (FrameCroppingFlag)
    {

        FrameCroppingRectLeftOffset = Parser::ExpGolomb::readUe(nalu, offset);
        FrameCroppingRectRightOffset = Parser::ExpGolomb::readUe(nalu, offset);
        FrameCroppingRectTopOffset = Parser::ExpGolomb::readUe(nalu, offset);
        FrameCroppingRectBottomOffset = Parser::ExpGolomb::readUe(nalu, offset);
    }
    bool vui_parameters_present_flag = Parser::getBit(nalu, offset);
    // partial read of VUI - need frame rate 
    if(vui_parameters_present_flag)
    {
        bool aspect_ratio_info_present_flag = Parser::getBit(nalu, offset);
        if (aspect_ratio_info_present_flag)
        {
            amf_uint8 aspect_ratio_idc             = Parser::readBits(nalu, offset, 8);
            if (255==aspect_ratio_idc)
            {
                amf_uint16 sar_width                  = Parser::readBits(nalu, offset, 16);
                amf_uint16 sar_height                 = Parser::readBits(nalu, offset, 16);
            }
        }
        bool overscan_info_present_flag     = Parser::getBit(nalu, offset);
        if (overscan_info_present_flag)
        {
            bool overscan_appropriate_flag    = Parser::getBit(nalu, offset);
        }
        bool video_signal_type_present_flag = Parser::getBit(nalu, offset);
        if (video_signal_type_present_flag)
        {
            amf_uint8 video_format              = Parser::readBits(nalu, offset, 3);
            bool video_full_range_flag           = Parser::getBit(nalu, offset);
            bool colour_description_present_flag = Parser::getBit(nalu, offset);
            if(colour_description_present_flag)
            {
                amf_uint8 colour_primaries              = Parser::readBits(nalu, offset, 8);
                amf_uint8 transfer_characteristics      = Parser::readBits(nalu, offset, 8);
                amf_uint8 matrix_coefficients           = Parser::readBits(nalu, offset, 8);
            }
        }
        bool chroma_location_info_present_flag = Parser::getBit(nalu, offset);;
        if(chroma_location_info_present_flag)
        {
            amf_uint32 chroma_sample_loc_type_top_field     = Parser::ExpGolomb::readUe(nalu, offset);
            amf_uint32 chroma_sample_loc_type_bottom_field  = Parser::ExpGolomb::readUe(nalu, offset);
        }
        timing_info_present_flag          = Parser::getBit(nalu, offset);
        if (timing_info_present_flag)
        {
            num_units_in_tick               = Parser::readBits(nalu, offset, 32);
            time_scale                      = Parser::readBits(nalu, offset, 32);
            bool fixed_frame_rate_flag      = Parser::getBit(nalu, offset);
        }
        // the rest can be parsed if needed
    }
    return true;
}
//-------------------------------------------------------------------------------------------------
bool AvcParser::PpsData::Parse(amf_uint8 *nalu, size_t size)
{
    size_t offset = 8; // 1 byte

    Id = Parser::ExpGolomb::readUe(nalu, offset);
    SpsId = Parser::ExpGolomb::readUe(nalu, offset);
    EntropyCodingMode = Parser::getBit(nalu, offset);
    BottomFieldPicOrderInFramePresent = Parser::getBit(nalu, offset);
    return true;
}
//-------------------------------------------------------------------------------------------------
bool AvcParser::AccessUnitSigns::Parse(amf_uint8 *nalu, size_t size, std::map<amf_uint32,SpsData> &spsMap, std::map<amf_uint32,PpsData> &ppsMap)
{
    size_t offset = 8;

    amf_uint32 firstMbInSlice = Parser::ExpGolomb::readUe(nalu, offset);
    amf_uint32 sliceType = Parser::ExpGolomb::readUe(nalu, offset);
    PicParameterSetId = Parser::ExpGolomb::readUe(nalu, offset);

    std::map<amf_uint32,PpsData>::iterator ppsIt = ppsMap.find(PicParameterSetId);
    if (ppsIt == ppsMap.end())
    {
       return false;// assert(0);
    }

    std::map<amf_uint32,SpsData>::iterator spsIt = spsMap.find(ppsIt->second.SpsId);
    if (spsIt == spsMap.end())
    {
        return false;// assert(0);
    }

    if (spsIt->second.SeparateColourPlane)
    {
       amf_uint32 colourPlaneId = Parser::readBits(nalu, offset, 2);
    }

    amf_uint32 frameNumBitsCount = spsIt->second.Log2MaxFrameNumMinus4 + 4;

    FrameNum = Parser::readBits(nalu, offset, frameNumBitsCount);

    FieldPicFlag = false;
    BottomFieldFlag = false;

    if (!spsIt->second.FrameMbsOnlyFlag)
    {
        FieldPicFlag = Parser::getBit(nalu, offset);

        if (FieldPicFlag)
        {
            BottomFieldFlag = Parser::getBit(nalu, offset);
        }
    }

    IdrPicFlag = GetNaluUnitType(*nalu) == NalUnitTypeSliceIdrPicture;
    IdrPicId = 0;
    if (IdrPicFlag)
    {
        IdrPicId = Parser::ExpGolomb::readUe(nalu, offset);
    }

    PicOrderCntLsb = 0;
    DeltaPicOrderCntBottom = 0;
    if (0 == spsIt->second.PicOrderCntType) 
    {
        amf_uint32 picOrderCntLsbBitsCount = spsIt->second.Log2MaxPicOrderCntLsbMinus4 + 4;
        PicOrderCntLsb = Parser::readBits(nalu, offset, picOrderCntLsbBitsCount);

        if (ppsIt->second.BottomFieldPicOrderInFramePresent && !FieldPicFlag)
        {
            DeltaPicOrderCntBottom = Parser::ExpGolomb::readSe(nalu, offset);
        }
    }

    DeltaPicOrderCnt0 = 0;
    DeltaPicOrderCnt1 = 0;
    if (1 == spsIt->second.PicOrderCntType && !spsIt->second.DeltaPicOrderAlwaysZero) 
    {
        DeltaPicOrderCnt0 = Parser::ExpGolomb::readSe(nalu, offset);

        if (ppsIt->second.BottomFieldPicOrderInFramePresent && !FieldPicFlag)
        {
            DeltaPicOrderCnt1 = Parser::ExpGolomb::readSe(nalu, offset);
        }
    }

    NalRefIdc = static_cast<amf_uint32>(nalu[0] & NalRefIdcMask);
    PicOrderCntType = spsIt->second.PicOrderCntType;
    return true;
}
//-------------------------------------------------------------------------------------------------
bool AvcParser::AccessUnitSigns::IsNewPicture(const AccessUnitSigns &newSigns)
{
        if (FrameNum != newSigns.FrameNum)
        {
            return true;
        }

        if (PicParameterSetId != newSigns.PicParameterSetId)
        {
            return true;
        }

        if (FieldPicFlag != newSigns.FieldPicFlag)
        {
            return true;
        }

        if (IdrPicFlag != newSigns.IdrPicFlag)
        {
            return true;
        }

        if (FieldPicFlag && newSigns.FieldPicFlag && 
            BottomFieldFlag != newSigns.BottomFieldFlag)
        {
            return true;
        }

        if ((NalRefIdc == 0 || newSigns.NalRefIdc == 0) && 
            NalRefIdc != newSigns.NalRefIdc)
        {
            return true;
        }

        if (IdrPicFlag && newSigns.IdrPicFlag && 
            IdrPicId != newSigns.IdrPicId)
        {
            return true;
        }

        if ((PicOrderCntType == 0 && newSigns.PicOrderCntType == 0) && 
            ((PicOrderCntLsb != newSigns.PicOrderCntLsb) || 
            (DeltaPicOrderCntBottom != newSigns.DeltaPicOrderCntBottom)))
        {
            return true;
        }

        if ((PicOrderCntType == 1 && newSigns.PicOrderCntType == 1) && 
            ((DeltaPicOrderCnt0 != newSigns.DeltaPicOrderCnt0) || 
            (DeltaPicOrderCnt1 != newSigns.DeltaPicOrderCnt1)))
        {
            return true;
        }

        return false;
}
//-------------------------------------------------------------------------------------------------
void ExtraDataAvccBuilder::AddSPS(amf_uint8 *sps, size_t size)
{
    m_SPSCount++;
    size_t pos = m_SPSs.GetSize();
    amf_uint16 spsSize = size & maxSpsSize;
    m_SPSs.SetSize(pos + spsSize +2);
    amf_uint8 *data = m_SPSs.GetData() + pos;
    *data++ = Parser::getLowByte(spsSize);
    *data++ = Parser::getHiByte(spsSize);
    memcpy(data , sps, (size_t)spsSize);
}
//-------------------------------------------------------------------------------------------------
void ExtraDataAvccBuilder::AddPPS(amf_uint8 *pps, size_t size)
{
    m_PPSCount++;
    size_t pos = m_PPSs.GetSize();
    amf_uint16 ppsSize = size & maxPpsSize;
    m_PPSs.SetSize(pos + ppsSize +2);
    amf_uint8 *data = m_PPSs.GetData() + pos;
    *data++ = Parser::getLowByte(ppsSize);
    *data++ = Parser::getHiByte(ppsSize);
    memcpy(data , pps, (size_t)ppsSize);
}
//-------------------------------------------------------------------------------------------------
// ISO-IEC 14496-15-2004.pdf, #5.2.4.1.1
//-------------------------------------------------------------------------------------------------
bool ExtraDataAvccBuilder::GetExtradata(AMFByteArray   &extradata)
{
    if( m_SPSs.GetSize() == 0  || m_PPSs .GetSize() ==0 )
    {
        return false;
    }

    if (m_SPSCount > 0x1F)
    {
        return false;
    }


    if (m_SPSs.GetSize() < minSpsSize)
    {
        return false;
    }

    extradata.SetSize(7 + m_SPSs.GetSize() + m_PPSs .GetSize());

    amf_uint8 *data = extradata.GetData();
    amf_uint8 *sps0 = m_SPSs.GetData();
    // c

    *data++ = 0x01; // configurationVersion
    *data++ = sps0[3]; // AVCProfileIndication
    *data++ = sps0[4]; // profile_compatibility
    *data++ = sps0[5]; // AVCLevelIndication
    *data++ =  (0xFC | (NalUnitLengthSize - 1));   // reserved(11111100) + lengthSizeMinusOne
    *data++ = (0xE0 | static_cast<amf_uint8>(m_SPSCount)); // reserved(11100000) + numOfSequenceParameterSets

    memcpy(data, m_SPSs.GetData(), m_SPSs.GetSize());
    data += m_SPSs.GetSize();


//    if (m_PPSCount > 0xFF)
//    {
//        return false;
//    }

    *data++ = (static_cast<amf_uint8>(m_PPSCount)); // numOfPictureParameterSets
    
    memcpy(data, m_PPSs.GetData(), m_PPSs.GetSize());

    data += m_PPSs.GetSize();
    return true;
}
bool ExtraDataAvccBuilder::SetAnnexB(amf_uint8 *data, size_t size)
{
    int zerosCount = 0;
    bool bFirst = true;
    amf_uint8 *nalu = 0;
    for(size_t i = 0; i < size; i++)
    {
        amf_uint8 ch = *data++;
        if (0 == ch)
        {
            zerosCount++;
        }
        else 
        {
            if (1 == ch && zerosCount > 1) // We found a start code in Annex B stream
            {
                if(!bFirst)
                {
                   amf_uint8 *naluEnd = data - zerosCount - 1;
                   // check type
                   if(AvcParser::GetNaluUnitType(*nalu) == AvcParser::NalUnitTypeSequenceParameterSet)
                   {
                       AddSPS(nalu, naluEnd - nalu);
                   }
                   else  if(AvcParser::GetNaluUnitType(*nalu) == AvcParser::NalUnitTypePictureParameterSet)
                   {
                       AddPPS(nalu, naluEnd - nalu);
                   }
                }
                bFirst = false;
                nalu = data;
            }
            zerosCount = 0;
        }
    }
    if(!bFirst)
    {
        amf_uint8 *naluEnd = data;
        // check type
        if(AvcParser::GetNaluUnitType(*nalu) == AvcParser::NalUnitTypeSequenceParameterSet)
        {
            AddSPS(nalu, naluEnd - nalu);
        }
        else  if(AvcParser::GetNaluUnitType(*nalu) == AvcParser::NalUnitTypePictureParameterSet)
        {
           AddPPS(nalu, naluEnd - nalu);
        }

    }
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
#define ZEROBYTES_SHORTSTARTCODE 2 //indicates the number of zero bytes in the short start-code prefix
//-------------------------------------------------------------------------------------------------
size_t AvcParser::EBSPtoRBSP(amf_uint8 *streamBuffer,size_t begin_bytepos, size_t end_bytepos)
{
    int count = 0;
    if(end_bytepos < begin_bytepos)
    {
        return end_bytepos;
    }
    amf_uint8 *streamBuffer_i=streamBuffer+begin_bytepos;
    amf_uint8 *streamBuffer_end=streamBuffer+end_bytepos;
    int iReduceCount=0;
    for(; streamBuffer_i!=streamBuffer_end; )
    { 
        //starting from begin_bytepos to avoid header information
        //in NAL unit, 0x000000, 0x000001 or 0x000002 shall not occur at any amf_uint8-aligned position
        register amf_uint8 tmp=*streamBuffer_i;
        if(count == ZEROBYTES_SHORTSTARTCODE)
        {
            if(tmp == 0x03)
            {
                //check the 4th amf_uint8 after 0x000003, except when cabac_zero_word is used, in which case the last three bytes of this NAL unit must be 0x000003
                if((streamBuffer_i+1 != streamBuffer_end) && (streamBuffer_i[1] > 0x03))
                {
                    return -1;
                }
                //if cabac_zero_word is used, the final amf_uint8 of this NAL unit(0x03) is discarded, and the last two bytes of RBSP must be 0x0000
                if(streamBuffer_i+1 == streamBuffer_end)
                {
                    break;
                }
                memmove(streamBuffer_i,streamBuffer_i+1,streamBuffer_end-streamBuffer_i-1);
                streamBuffer_end--;
                iReduceCount++;
                count = 0;
                tmp = *streamBuffer_i;
            }
            else if(tmp < 0x03) 
            {
            }
        }
        if(tmp == 0x00)
        {
            count++;
        }
        else
        {
            count = 0;
        }
        streamBuffer_i++;
    }
    return end_bytepos - begin_bytepos + iReduceCount;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT              AvcParser::ReInit()
{
    m_currentFrameTimestamp = 0;
    m_pStream->Seek(amf::AMF_SEEK_BEGIN, 0, NULL);
    m_PacketCount = 0;
    m_bEof = false;
    m_currentAccessUnitsSigns = AccessUnitSigns();
    m_ReadData.SetSize(0);
    return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
