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

#include "RawStreamReader.h"
#include "PipelineDefines.h"
#include "CmdLogger.h"
#include <fstream>
#include <wctype.h>


// replacing with std::iswdigit(wchar_t ch)
//inline bool AMFIsDecimal(wchar_t sum)
//{
//    return sum >= L'0' && sum <= L'9';
//}

static bool PicGetStride(amf::AMF_SURFACE_FORMAT eFormat, amf_int32 width, amf_int32& stride)
{
    stride = 0;
    switch(eFormat)
    {
        case amf::AMF_SURFACE_NV12:
            stride =  width%2 ? width+1 : width;
            break;
        case amf::AMF_SURFACE_YUY2:
        case amf::AMF_SURFACE_UYVY:
            stride =  width * 2;
            break;
        case amf::AMF_SURFACE_P010:
        case amf::AMF_SURFACE_P012:
        case amf::AMF_SURFACE_P016:
            stride =  (width%2 ? width+1 : width) * 2;
            break;
        case amf::AMF_SURFACE_YUV420P:
        case amf::AMF_SURFACE_YV12:
            stride = width;
            break;
        case amf::AMF_SURFACE_BGRA:
        case amf::AMF_SURFACE_RGBA:
            stride = width * 4;
            break;

        case amf::AMF_SURFACE_R10G10B10A2:
            stride = width * 4;
            break;
        case amf::AMF_SURFACE_RGBA_F16:
            stride = width * 8;
            break;

        default:
            LOG_ERROR("Stride cannot be calculated, unknown pixel format");
            return false;
    }
    return true;
}

static bool PicGetFrameSize(amf::AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height, int& size)
{
    size = 0;
    amf_int32 stride;
    if( !PicGetStride(format, width, stride) )
    {
        return false;
    }

    switch (format)
    {
        case amf::AMF_SURFACE_YUV420P: 
        case amf::AMF_SURFACE_YV12:
        case amf::AMF_SURFACE_NV12:
        case amf::AMF_SURFACE_P010:
        case amf::AMF_SURFACE_P012:
        case amf::AMF_SURFACE_P016:
            size = stride*height + stride/2*height/2*2; // 2 planes
            break;
        case amf::AMF_SURFACE_YUY2:
        case amf::AMF_SURFACE_UYVY:
        case amf::AMF_SURFACE_BGRA:
        case amf::AMF_SURFACE_RGBA:
        case amf::AMF_SURFACE_RGBA_F16:
        case amf::AMF_SURFACE_R10G10B10A2:
            size = stride*height;
            break;
        default:
            LOG_ERROR("Frame size cannot be calculated, unknown pixel format");
            return false;
    }
    return true;
}

static void PlaneCopy(const amf_uint8 *src, amf_int32 srcStride, amf_int32 srcHeight, amf_uint8 *dst, amf_int32 dstStride, amf_int32 dstHeight)
{
    amf_int32 minHeight = AMF_MIN(srcHeight, dstHeight);
    if (srcStride == dstStride)
    {
        memcpy( dst, src, minHeight * srcStride);
    }
    else
    {
        int minStride = AMF_MIN(srcStride, dstStride);
        for(int y = 0; y < minHeight; y++)
        {
            memcpy(dst + dstStride*y, src + srcStride*y, minStride);
        }
    }
}

static void NV12PicCopy(const amf_uint8 *src, amf_int32 srcStride, amf_int32 srcHeight, amf_uint8 *dst, amf_int32 dstStride, amf_int32 dstHeight)
{
    // Y- plane
    PlaneCopy(src, srcStride, srcHeight, dst, dstStride, dstHeight); 
    // UV - plane
    amf_int32 srcYSize = srcHeight * srcStride;
    amf_int32 dstYSize = dstHeight * dstStride;
    PlaneCopy(src + srcYSize, srcStride, srcHeight / 2, dst + dstYSize, dstStride, dstHeight / 2); 
}

static void YUV420PicCopy(const amf_uint8 *src, amf_int32 srcStride, amf_int32 srcHeight, amf_uint8 *dst, amf_int32 dstStride, amf_int32 dstHeight)
{
    // Y- plane
    PlaneCopy(src, srcStride, srcHeight, dst, dstStride, dstHeight); 
    // U - plane
    amf_int32 srcYSize = srcHeight * srcStride;
    amf_int32 dstYSize = dstHeight * dstStride;

    PlaneCopy(src + srcYSize, srcStride / 2, srcHeight / 2, dst + dstYSize, dstStride / 2, dstHeight / 2); 

    amf_int32 srcUSize = srcHeight/2 * srcStride/2;
    amf_int32 dstUSize = dstHeight/2 * dstStride/2;

    PlaneCopy(src + srcYSize + srcUSize, srcStride / 2, srcHeight / 2, dst + dstYSize + dstUSize, dstStride / 2, dstHeight / 2); 
}


RawStreamReader::RawStreamReader()
    :m_pDataStream(),
    m_format(amf::AMF_SURFACE_UNKNOWN),
    m_memoryType(amf::AMF_MEMORY_UNKNOWN),
    m_width(0), 
    m_height(0), 
    m_stride(0), 
    m_framesCount(0),
    m_framesCountRead(0),
    m_frame()
{
}

RawStreamReader::~RawStreamReader()
{
    Terminate();
}

AMF_RESULT RawStreamReader::Init(ParametersStorage* pParams, amf::AMFContext* pContext)
{
    AMF_RESULT res = AMF_OK;
    m_pContext = pContext;
    std::wstring path;
    res = pParams->GetParamWString(PARAM_NAME_INPUT, path);
	std::wstring search_center_map_path;
	std::wstring codec;
	res = pParams->GetParamWString(PARAM_NAME_SEARCH_CENTER_MAP_INPUT, search_center_map_path);
	if (res != AMF_OK)
	{
		m_pSearchCenterMapEnabled = false;
	}
	else
	{
		m_pSearchCenterMapEnabled = true;
		res = pParams->GetParamWString(PARAM_NAME_CODEC, codec);
	}

    if(amf_path_is_relative(path.c_str()))
    {
        std::wstring dir;
        if(pParams->GetParamWString(PARAM_NAME_INPUT_DIR, dir) == AMF_OK)
        {
            path = dir + PATH_SEPARATOR_WSTR + path;
			if (m_pSearchCenterMapEnabled)
			{
				search_center_map_path = dir + PATH_SEPARATOR_WSTR + search_center_map_path;
			}
        }
    }

    amf_int width = 0;
    amf_int height = 0;

    pParams->GetParam(PARAM_NAME_INPUT_WIDTH, width);
    pParams->GetParam(PARAM_NAME_INPUT_HEIGHT, height);

    amf_int roi_x = 0;
    amf_int roi_y = 0;
    amf_int roi_width = 0;
    amf_int roi_height = 0;
    pParams->GetParam(PARAM_NAME_INPUT_ROI_X, roi_x);
    pParams->GetParam(PARAM_NAME_INPUT_ROI_Y, roi_y);
    pParams->GetParam(PARAM_NAME_INPUT_ROI_WIDTH, roi_width);
    pParams->GetParam(PARAM_NAME_INPUT_ROI_HEIGHT, roi_height);

    amf::AMF_SURFACE_FORMAT inputFormat = amf::AMF_SURFACE_UNKNOWN;
  	amf_int64 formatEnum = 0;
	pParams->GetParam(PARAM_NAME_INPUT_FORMAT, formatEnum);
	inputFormat = static_cast <amf::AMF_SURFACE_FORMAT> (formatEnum);
	
    amf_int frames = 0;
    pParams->GetParam(PARAM_NAME_INPUT_FRAMES, frames);


    m_width = width;
    m_height = height;
    m_format = inputFormat;

	if (m_pSearchCenterMapEnabled)
	{
		if (codec == L"AMFVideoEncoderHW_HEVC")  //(codec == AMFVideoEncoder_HEVC)
		{
			m_searchCenterMapSize = ((m_width + 63) >> 6) * ((m_height + 63) >> 6) * 4;
		}
		else // (codec == L"AMFVideoEncoderVCE_AVC")  //if (codec == AMFVideoEncoderVCE_AVC)
		{
			m_searchCenterMapSize = ((m_width + 15) >> 4) * ((m_height + 15) >> 4) * 4;
		}
		m_searchCenterMapformat = amf::AMF_SURFACE_NV12;	//Fixed format: NV12
		m_searchCenterMapWidth  = 1024;						//Fixed width and stride
		m_searchCenterMapStride = 1024;
		m_searchCenterMapHeight = (m_searchCenterMapSize + 1023) / 1024;
		m_searchCenterMapFrame.SetSize(m_searchCenterMapSize);
		amf::AMFDataStream::OpenDataStream(search_center_map_path.c_str(), amf::AMFSO_READ, amf::AMFFS_SHARE_READ, &m_pSearchCenterMapStream);
		if (!m_pSearchCenterMapStream)
		{
			LOG_ERROR("Cannot open search center map input file: " << search_center_map_path.c_str());
			return AMF_FAIL;
		}
	}

    if(m_width == 0 || m_height == 0 || m_format == amf::AMF_SURFACE_UNKNOWN)
    {
        ParseRawFileFormat(path, m_width, m_height, m_format);
    }

    m_roi_x = roi_x; 
    m_roi_y = roi_y;
    m_roi_width = roi_width;
    m_roi_height = roi_height;

    if(m_roi_width == 0)
    {
        m_roi_width = m_width;
    }
    if(m_roi_height == 0)
    {
        m_roi_height = m_height;
    }

    if(m_format == amf::AMF_SURFACE_UNKNOWN)
    {
        LOG_ERROR(L"Unknown file format: inputFileName - " << path);
        return AMF_FAIL;
    }

    if (m_format != amf::AMF_SURFACE_YUV420P && m_format != amf::AMF_SURFACE_BGRA && m_format != amf::AMF_SURFACE_RGBA &&
        m_format != amf::AMF_SURFACE_NV12 && m_format != amf::AMF_SURFACE_P010 && m_format != amf::AMF_SURFACE_P012 &&
        m_format != amf::AMF_SURFACE_P016 && m_format != amf::AMF_SURFACE_RGBA_F16 && m_format != amf::AMF_SURFACE_R10G10B10A2 &&
        m_format != amf::AMF_SURFACE_YUY2 && m_format != amf::AMF_SURFACE_UYVY
        )
    {
        LOG_ERROR("Only YUV420P or BGRA or NV12 or RGBA picture format supported");
        return AMF_FAIL;
    }

    amf::AMFDataStream::OpenDataStream(path.c_str(), amf::AMFSO_READ, amf::AMFFS_SHARE_READ, &m_pDataStream);
    if (!m_pDataStream)
    {
        LOG_ERROR("Cannot open input file: " << path.c_str() );
        return AMF_FAIL;
    }

    if( !PicGetStride(m_format, m_width, m_stride) )
    {
        LOG_ERROR("Wrong format:" << m_format);
        return AMF_FAIL;
    }
    int frameSize = 0;
    if( !PicGetFrameSize(m_format, m_width, m_height, frameSize) )
    {
        LOG_ERROR("Wrong format:" << m_format);
        return AMF_FAIL;
    }
    m_frame.SetSize(frameSize);

    if (!m_stride || !frameSize)
    {
        LOG_ERROR("Could not define frame size for current frame format");
        return AMF_FAIL;
    }
    amf_int64 size = 0;
    m_pDataStream->GetSize(&size);
    m_framesCount = static_cast<int>(size / frameSize);
    if(frames)
    {
        m_framesCount = AMF_MIN(frames, m_framesCount);
    }
    return AMF_OK;
}

AMF_RESULT RawStreamReader::Terminate()
{
    AMF_RESULT res = AMF_OK;
    m_pDataStream = NULL;
    m_pContext = NULL;
    return res;
}

AMF_RESULT RawStreamReader::SubmitInput(amf::AMFData* /* pData */)
{
    return AMF_NOT_SUPPORTED;
}

AMF_RESULT RawStreamReader::QueryOutput(amf::AMFData** ppData)
{
    AMF_RESULT res = AMF_OK;
    amf::AMFSurfacePtr pSurface;

    res = m_pContext->AllocSurface(amf::AMF_MEMORY_HOST, m_format, m_width , m_height, &pSurface);
    CHECK_AMF_ERROR_RETURN(res, L"AMFContext::AllocSurface(amf::AMF_MEMORY_HOST) failed");
    pSurface->SetCrop(m_roi_x, m_roi_y, m_roi_width, m_roi_height);

    amf::AMFPlanePtr plane = pSurface->GetPlaneAt(0);
    res = ReadNextFrame(plane->GetHPitch(), m_height, plane->GetVPitch(), static_cast<unsigned char*>(plane->GetNative()));
    if(res == AMF_EOF)
    {
        return res;
    }
    CHECK_AMF_ERROR_RETURN(res, L"ReadNextFrame() failed");

    // RawStreamReader doesn't have a frame rate, so let's
    // assume the frame rate is 30 fps, and then set pts and duration
    amf_pts frameDuration = amf_pts(AMF_SECOND / 30.0); // In 100 NanoSeconds
    pSurface->SetPts((m_framesCountRead - 1) * frameDuration);
    pSurface->SetDuration(frameDuration);

    *ppData = pSurface.Detach();

	if (m_pSearchCenterMapEnabled)
	{
		amf::AMFSurfacePtr pSearchCenterMapSurface;

		res = m_pContext->AllocSurface(amf::AMF_MEMORY_HOST, m_searchCenterMapformat, m_searchCenterMapWidth, m_searchCenterMapHeight, &pSearchCenterMapSurface);
		CHECK_AMF_ERROR_RETURN(res, L"AMFContext::AllocSurface(amf::AMF_MEMORY_HOST) for search center map failed");

		amf::AMFPlanePtr searchCenterMapPlane = pSearchCenterMapSurface->GetPlaneAt(0);

		res = ReadNextSearchCenterMap(searchCenterMapPlane->GetHPitch(), m_height, searchCenterMapPlane->GetVPitch(), static_cast<unsigned char*>(searchCenterMapPlane->GetNative()));
		if (res == AMF_EOF)
		{
			return res;
		}
		CHECK_AMF_ERROR_RETURN(res, L"ReadNextSearchCenterMap() failed");

		(*ppData)->SetProperty(L"SearchCenterData", pSearchCenterMapSurface.Detach());
	}

    return AMF_OK;
}

AMF_RESULT RawStreamReader::ReadNextFrame(int dstStride, int /* dstHeight */, int valignment, unsigned char* pDstBits)
{
    if(m_framesCountRead == m_framesCount)
    {
        return AMF_EOF;
    }

    {
        amf_size read = 0;
        m_pDataStream->Read(m_frame.GetData(), m_frame.GetSize(), &read);
        if (read != m_frame.GetSize())
        {
            return AMF_EOF;
        }
        m_framesCountRead++;
    }

    switch(m_format)
    {
    case amf::AMF_SURFACE_YUY2:
    case amf::AMF_SURFACE_UYVY:
    case amf::AMF_SURFACE_BGRA:
    case amf::AMF_SURFACE_RGBA:
    case amf::AMF_SURFACE_RGBA_F16:
    case amf::AMF_SURFACE_R10G10B10A2:
        PlaneCopy(m_frame.GetData(), m_stride, m_height, pDstBits, dstStride, valignment);
        break;
    case amf::AMF_SURFACE_YUV420P:
        YUV420PicCopy(m_frame.GetData(), m_stride, m_height, pDstBits, dstStride, valignment);
        break;
    case amf::AMF_SURFACE_NV12:
    case amf::AMF_SURFACE_P010:
    case amf::AMF_SURFACE_P012:
    case amf::AMF_SURFACE_P016:
        NV12PicCopy(m_frame.GetData(), m_stride, m_height, pDstBits, dstStride, valignment);
        break;
    default:
        LOG_ERROR("Format reading is not supported");
        return AMF_FAIL;
    }
    return AMF_OK;
}

AMF_RESULT RawStreamReader::ReadNextSearchCenterMap(int dstStride, int /* dstHeight */, int valignment, unsigned char* pDstBits)
{
	amf_size read = 0;
	m_pSearchCenterMapStream->Read(m_searchCenterMapFrame.GetData(), m_searchCenterMapFrame.GetSize(), &read);
	if (read != m_searchCenterMapFrame.GetSize())
	{
		return AMF_EOF;
	}

	PlaneCopy(m_searchCenterMapFrame.GetData(), m_searchCenterMapStride, m_searchCenterMapHeight, pDstBits, dstStride, valignment);

	return AMF_OK;
}
//----------------------------------------------------------------------------------------------
void RawStreamReader::ParseRawFileFormat(const std::wstring path, amf_int32 &width, amf_int32 &height, amf::AMF_SURFACE_FORMAT& format)
{
    // we read only undefined values before
    if (width == 0 || height == 0 || format == amf::AMF_SURFACE_UNKNOWN)
    {
        //first try to find ini file
        std::wstring::size_type dot_pos = path.find_last_of(L'.');
        std::wstring ini_file = path.substr(0, dot_pos) + L".ini";

        std::wfstream configStream;
#ifdef _WIN32
        configStream.open(ini_file.c_str(), std::ios_base::in);
#else
        std::string _ini_file(ini_file.begin(), ini_file.end());
        configStream.open(_ini_file, std::ios_base::in);
#endif
        if (configStream.is_open())
        {
            while (configStream.eof() == false)
            {
                std::wstring line;
                std::getline(configStream, line);

                line = line.erase(line.find_last_not_of(L'\n') + 1);
                amf_size pos = line.find_first_of(L'=', 0);
                if (std::wstring::npos != pos && (pos - 1) > 0 && pos + 1 != line.length())
                {
                    std::wstring key = line.substr(0, pos);
                    std::wstring value = line.substr(pos + 1, std::wstring::npos);
                    key.erase(0, key.find_first_not_of(' ')); // trim
                    key.erase(key.find_last_not_of(' ') + 1);
                    key = toUpper(key);
                    if (key == L"WIDTH" && width == 0)
                    {
                        width = amf::AMFVariant(value.c_str());
                    }
                    if (key == L"HEIGHT" && height == 0)
                    {
                        height = amf::AMFVariant(value.c_str());
                    }
                    if (key == L"FORMAT" && format == amf::AMF_SURFACE_UNKNOWN)
                    {
                        format = GetFormatFromString(value.c_str());
                    }
                }
            }
        }
    }

    if (width == 0 || height == 0)
    {
        std::wstring::size_type dot_pos = path.find_last_of(L'.');
        std::wstring::size_type slash_pos = path.find_last_of(L"\\/");
        std::wstring fileName = path.substr(slash_pos + 1, dot_pos - (slash_pos + 1));

        amf_size pos = 0;
        amf_size leftPos = std::wstring::npos;
        amf_size rightPos = std::wstring::npos;
        amf_size len = fileName.length();

        std::wstring tmp;
        while (std::wstring::npos != (pos = fileName.find_first_of('x', pos)))
        {
            if (pos == 0 || pos == len - 1) { ++pos; continue; }
            // find left
            leftPos = pos - 1; rightPos = pos + 1;
            if (iswdigit(fileName[leftPos]) && iswdigit(fileName[rightPos]))
            {
                while (leftPos != 0)
                {
                    if (!iswdigit(fileName[leftPos])) { ++leftPos; break; }
                    --leftPos;
                }
                while (rightPos != len)
                {
                    if (!iswdigit(fileName[rightPos])) { --rightPos; break; }
                    ++rightPos;
                }
                tmp = fileName.substr(leftPos, pos - leftPos);
                swscanf(tmp.c_str(), L"%d", &width);
                tmp = fileName.substr(pos + 1, rightPos - pos);
                swscanf(tmp.c_str(), L"%d", &height);

                break;
            }
            ++pos;
        }
    }
    if (format == amf::AMF_SURFACE_UNKNOWN)
    {
        std::wstring::size_type dot_pos = path.find_last_of(L'.');
        std::wstring ext = path.substr(dot_pos + 1);
        format = GetFormatFromString(ext.c_str());
    }
}

void RawStreamReader::RestartReader()
{
    m_pDataStream->Seek(amf::AMF_SEEK_BEGIN, 0, NULL);
    m_framesCountRead = 0;
}
//----------------------------------------------------------------------------------------------
amf::AMF_SURFACE_FORMAT AMF_STD_CALL GetFormatFromString(const wchar_t* str)
{
    amf::AMF_SURFACE_FORMAT ret = amf::AMF_SURFACE_UNKNOWN;
    std::wstring std_string = str;
    if (std_string == L"420p" || std_string == L"yuv" || std_string == L"I420")
    {
        ret = amf::AMF_SURFACE_YUV420P;
    }
    else if (std_string == L"bgra")
    {
        ret = amf::AMF_SURFACE_BGRA;
    }
    else if (std_string == L"rgba")
    {
        ret = amf::AMF_SURFACE_RGBA;
    }
    else if (std_string == L"nv12")
    {
        ret = amf::AMF_SURFACE_NV12;
    }
    else if (std_string == L"yuy2")
    {
        ret = amf::AMF_SURFACE_YUY2;
    }
    else if (std_string == L"uyvy")
    {
        ret = amf::AMF_SURFACE_UYVY;
    }
    else if (std_string == L"p010")
    {
        ret = amf::AMF_SURFACE_P010;
    }
    else if (std_string == L"p012")
    {
        ret = amf::AMF_SURFACE_P012;
    }
    else if (std_string == L"p016")
    {
        ret = amf::AMF_SURFACE_P016;
    }
    else if (std_string == L"rgbaf16")
    {
        ret = amf::AMF_SURFACE_RGBA_F16;
    }
    else if (std_string == L"r10g10b10a2")
    {
        ret = amf::AMF_SURFACE_R10G10B10A2;
    }
    return ret;
}
//----------------------------------------------------------------------------------------------
bool AMF_STD_CALL amf_path_is_relative(const wchar_t* const path)
{
    if(!path)
    {
        return true;
    }
    std::wstring pathStr(path);
    if(pathStr.empty())
    {
        return true;
    }
    amf_size pos = pathStr.find(L":");
    //if(pos==2 && pathStr[1]==L':')
    if(pos != std::wstring::npos)
    {
        return false;
    }
    pos = pathStr.find(L"\\\\");
    if(!pos)
    {
        return false;
    }
    return true;
}