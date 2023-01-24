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

#include "BitStreamParserIVF.h"

#include <vector>
#include <map>
#include <string>
#include "public/include/components/VideoDecoderUVD.h"
#include "public/include/components/VideoDecoderUVD.h"

//-------------------------------------------------------------------------------------------------
class IVFParser : public BitStreamParser
{
public:
	IVFParser(amf::AMFDataStream* stream, amf::AMFContext* pContext);
	virtual ~IVFParser();

	virtual int                     GetOffsetX() const;
	virtual int                     GetOffsetY() const;
	virtual int                     GetPictureWidth() const;
	virtual int                     GetPictureHeight() const;
	virtual int                     GetAlignedWidth() const;
	virtual int                     GetAlignedHeight() const;

	virtual void                    SetMaxFramesNumber(amf_size /* num */) { return; };

	virtual const unsigned char*    GetExtraData() const;
	virtual size_t                  GetExtraDataSize() const;
	virtual void                    SetUseStartCodes(bool bUse);
	virtual void                    SetFrameRate(double fps);
	virtual double                  GetFrameRate()  const;
	virtual void                    GetFrameRate(AMFRate *frameRate) const;

	virtual const wchar_t*          GetCodecComponent();

	virtual AMF_RESULT              QueryOutput(amf::AMFData** ppData);
	virtual AMF_RESULT              ReInit();

	static const size_t m_ReadSize = 1024 * 4;
	amf_uint16 m_pwidth;
	amf_uint16	m_pheight;
	AMFByteArray   m_ReadData;
	AMFByteArray   m_HeaderData;
	AMFByteArray   m_Extradata;
	amf_size       m_PacketCount;
	bool            m_bEof;
	double          m_fps;
	amf_size        m_maxFramesNumber;
	amf_pts        m_currentFrameTimestamp;
	amf::AMFDataStreamPtr m_pStream;
	amf::AMFContext* m_pContext;
	amf_uint32 m_CurrentFrameSize;
	IVF_CODEC_TYPE m_codec;
};
//-------------------------------------------------------------------------------------------------
BitStreamParser* CreateIVFParser(amf::AMFDataStream* stream, amf::AMFContext* pContext)
{
	return new IVFParser(stream, pContext);
}
//-------------------------------------------------------------------------------------------------
IVFParser::IVFParser(amf::AMFDataStream* stream, amf::AMFContext* pContext) :
	m_pStream(stream),
	m_currentFrameTimestamp(0),
	m_CurrentFrameSize(0),
	m_PacketCount(0),
	m_bEof(false),
	m_fps(0),
	m_maxFramesNumber(0),
	m_pContext(pContext),
	m_codec(IVF_CODEC_UNKNOWN),
	m_pheight(0),
	m_pwidth(0)
{
	stream->Seek(amf::AMF_SEEK_BEGIN, 0, NULL);
	size_t ready = m_HeaderData.GetSize();
	if (ready == 0)
	{
		m_HeaderData.SetSize(m_HeaderData.GetSize() + 32);
		ready = 0;
		m_pStream->Read(m_HeaderData.GetData(), 32, &ready);
		if (ready == 0)
		{
			m_bEof = true;
		}
	}

	amf_uint8* data = m_HeaderData.GetData();

	for (size_t i = 0; i < ready; i++)
	{
		if (i == 0)
		{
			char *signature = (char*)data;
			if (strcmp(signature, "DKIF"))
				return;
		}
		if (i == 6) {
			amf_uint16 *length = (amf_uint16*)data;
			m_pwidth = (amf_uint16)*length;
		}
		if (i == 8) 
		{
			char *s = (char*)data;
			char codec[]="VP80";
			strncpy(codec, s, sizeof(codec)-1);
			if (!strcmp(codec, "VP90"))
			{ 
				m_codec = IVF_CODEC_VP9;
			}
			else if (!strcmp(codec, "AV01"))
			{
				m_codec = IVF_CODEC_AV1;
			}
			else
			{
				m_codec = IVF_CODEC_UNKNOWN;
			}
		}
		if (i == 12) 
		{
			amf_uint16 *width = (amf_uint16*)data;
			m_pwidth = *width;
		}
		if (i == 14) 
		{
			amf_uint16 *height = (amf_uint16*)data;
			m_pheight = *height;
		}
		if (i == 24) 
		{
			amf_uint32 *number = (amf_uint32*)data;
			m_maxFramesNumber = *number;
		}
		data++;
	}

}
//-------------------------------------------------------------------------------------------------
IVFParser::~IVFParser()
{
	//    LOG_DEBUG(L"IVFParser: parsed frames:" << m_PacketCount << L"\n");
}
//-------------------------------------------------------------------------------------------------
int IVFParser::GetOffsetX() const
{
	return 0;
}
//-------------------------------------------------------------------------------------------------
int IVFParser::GetOffsetY() const
{
	return 0;
}
//-------------------------------------------------------------------------------------------------
int IVFParser::GetPictureWidth() const
{
	return m_pwidth;
}
//-------------------------------------------------------------------------------------------------
int IVFParser::GetPictureHeight() const
{
	return m_pheight;
}
//-------------------------------------------------------------------------------------------------
int IVFParser::GetAlignedWidth() const
{
	return m_pwidth;
}
//-------------------------------------------------------------------------------------------------
int IVFParser::GetAlignedHeight() const
{
	return m_pheight;
}
//-------------------------------------------------------------------------------------------------
const unsigned char*    IVFParser::GetExtraData() const
{
	AMFByteArray   readData;
	return readData.GetData();
}
//-------------------------------------------------------------------------------------------------
size_t                  IVFParser::GetExtraDataSize() const
{
	AMFByteArray   readData;
	return readData.GetSize();
}
//-------------------------------------------------------------------------------------------------
void                    IVFParser::SetUseStartCodes(bool /* bUse */)
{
	return;
}
//-------------------------------------------------------------------------------------------------
void                    IVFParser::SetFrameRate(double /* fps */)
{
	return;
}
//-------------------------------------------------------------------------------------------------
double                  IVFParser::GetFrameRate()  const
{
	return 0;
}
//-------------------------------------------------------------------------------------------------
void                    IVFParser::GetFrameRate(AMFRate * /* frameRate */) const
{
	return;
}
//-------------------------------------------------------------------------------------------------
const wchar_t* IVFParser::GetCodecComponent()
{ 
	if (m_codec == IVF_CODEC_VP9)
	{
		return AMFVideoDecoderHW_VP9;
	}
	else if (m_codec == IVF_CODEC_AV1)
	{
		return AMFVideoDecoderHW_AV1;
	}
	return AMFVideoDecoderHW_VP9;
}
AMF_RESULT              IVFParser::ReInit()
{
	m_CurrentFrameSize = 0;
	m_maxFramesNumber = 0;
	m_currentFrameTimestamp = 0;
	m_pStream->Seek(amf::AMF_SEEK_BEGIN, 0, NULL);
	m_PacketCount = 0;
	m_bEof = false;
	m_ReadData.SetSize(0);
	return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
AMF_RESULT IVFParser::QueryOutput(amf::AMFData** ppData)
{
	if (m_bFrozen)
	{
		return AMF_OK;
	}

	if ((m_bEof && m_ReadData.GetSize() == 0) || (m_maxFramesNumber && m_PacketCount >= m_maxFramesNumber))
	{
		return AMF_EOF;
	}

	size_t packetSize = 12;
	size_t currentOutputSize = 0;

	size_t ready = m_ReadData.GetSize();
	if (ready == 0)
	{
		if (m_CurrentFrameSize == 0)// new frame
		{
			m_ReadData.SetSize(m_ReadData.GetSize() + packetSize);
			ready = 0;
			m_pStream->Read(m_ReadData.GetData(), packetSize, &ready);
			if (ready != packetSize)
			{
				m_bEof = true;
				return AMF_EOF;
				// EOF
			}
			else//header of frame
			{
				m_PacketCount++;
				amf_uint8* data = m_ReadData.GetData();
				for (size_t i = 0; i < ready; i++)
				{
					if (i == 0)
					{
						amf_uint32 *framesize = (amf_uint32*)data;
						m_CurrentFrameSize = *framesize;
					}
					if (i == 4)
					{
						amf_pts *timestamp = (amf_pts*)data;
						m_currentFrameTimestamp = *timestamp;
						break;
					}
					data++;
				}
			}
		}

		// frame data
		m_ReadData.SetSize(m_CurrentFrameSize);
		ready = 0;
		m_pStream->Read(m_ReadData.GetData(), m_CurrentFrameSize, &ready);
		if (ready == 0)
		{
			m_bEof = true;
			m_ReadData.SetSize(0);
			m_CurrentFrameSize = 0;
			return AMF_EOF;
			// EOF
		}
		else if (ready < m_CurrentFrameSize)
		{
			currentOutputSize = ready;
		}
		else
		{
			currentOutputSize = m_CurrentFrameSize;
		}

	}


	//copy to decoder
	amf::AMFBufferPtr pictureBuffer;
	AMF_RESULT res = m_pContext->AllocBuffer(amf::AMF_MEMORY_HOST, currentOutputSize, &pictureBuffer);

	if (res != AMF_OK) 
	{
		return res;
	}


	amf_uint8 *data = (amf_uint8*)pictureBuffer->GetNative();

	memcpy(data, m_ReadData.GetData(), m_CurrentFrameSize);

	pictureBuffer->SetPts(m_currentFrameTimestamp);

	*ppData = pictureBuffer.Detach();
	m_CurrentFrameSize = m_CurrentFrameSize - (amf_uint32)currentOutputSize;
	m_ReadData.SetSize(0);

	return AMF_OK;
}
//-------------------------------------------------------------------------------------------------
