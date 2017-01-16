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

#include <SDKDDKVer.h>
#include <stdio.h>
#include <tchar.h>
#include "SVCSplitter.h"
#include "../common/CmdLogger.h"
#include "public/common/AMFFactory.h"


enum NALU_TYPE
{
    NALU_TYPE_SLICE    = 1,
    NALU_TYPE_DPA      = 2,
    NALU_TYPE_DPB      = 3,
    NALU_TYPE_DPC      = 4,
    NALU_TYPE_IDR      = 5,
    NALU_TYPE_SEI      = 6,
    NALU_TYPE_SPS      = 7,
    NALU_TYPE_PPS      = 8,
    NALU_TYPE_AUD      = 9,
    NALU_TYPE_EOSEQ    = 10,
    NALU_TYPE_EOSTREAM = 11,
    NALU_TYPE_FILL     = 12,
    NALU_TYPE_PREFIX   = 14,
    NALU_TYPE_SUB_SPS  = 15,
    NALU_TYPE_SLC_EXT  = 20,
    NALU_TYPE_VDRD     = 24  // View and Dependency Representation Delimiter NAL Unit
};

#pragma pack( push )
#pragma pack(1)

union NaluHeader
{
    struct
    {
        unsigned char nal_unit_type: 5;
        unsigned char nal_ref_idc: 2;
        unsigned char forbidden_zero: 1;
    };
    unsigned char value;
};
union NaluHeaderExt
{
    struct
    {
        // byte #0
        unsigned char priority_id:              6;
        unsigned char idr_flag:                 1;
        unsigned char svc_extension_flag:       1;
        // byte #1
        unsigned char quality_id:               4;
        unsigned char dependency_id:            3;
        unsigned char no_inter_layer_pred_flag: 1;
        // byte #2
        unsigned char reserved_three_2bits:     2;
        unsigned char output_flag:              1;
        unsigned char discardable_flag:         1;
        unsigned char use_ref_base_pic_flag:    1;
        unsigned char temporal_id:              3;
    };
    unsigned char values[3];
};

#pragma pack( pop )

SVCSplitter::SVCSplitter(void) :
    m_pParser(NULL)
{
}


SVCSplitter::~SVCSplitter(void)
{
}

AMF_RESULT SVCSplitter::Init(amf_int32 layerCount, const wchar_t *fileIn,const wchar_t *fileOut)
{
    if(m_pParser != NULL)
    {
        CHECK_AMF_ERROR_RETURN(AMF_ALREADY_INITIALIZED, L"Already Initialized");
    }
    if(fileIn == NULL)
    {
        CHECK_AMF_ERROR_RETURN(AMF_INVALID_ARG, L"No input file name");
    }
    if(fileOut == NULL)
    {
        CHECK_AMF_ERROR_RETURN(AMF_INVALID_ARG, L"No output file name");
    }
    if(layerCount <= 0)
    {
        CHECK_AMF_ERROR_RETURN(AMF_INVALID_ARG, L"layer count <= 0");
    }

    m_FileNameIn = fileIn;
    m_FileNameOut = fileOut;

    if(m_FileNameIn.length() == 0)
    {
        CHECK_AMF_ERROR_RETURN(AMF_INVALID_ARG, L"Empty input file name");
    }
    if(m_FileNameOut.length() ==0 )
    {
        CHECK_AMF_ERROR_RETURN(AMF_INVALID_ARG, L"Empty output file name");
    }

    AMF_RESULT res = AMF_OK;
    res = g_AMFFactory.Init();
    CHECK_AMF_ERROR_RETURN(res, L"AMF Failed to initialize");

    res = g_AMFFactory.GetFactory()->CreateContext(&m_pContext);
    CHECK_AMF_ERROR_RETURN(res , L"AMFCreateContext failed");

    amf::AMFDataStreamPtr stream;
    amf::AMFDataStream::OpenDataStream(m_FileNameIn.c_str(), amf::AMFSO_READ, amf::AMFFS_SHARE_READ, &stream);

    if(stream == NULL)
    {
        LOG_ERROR(L"Cannot open file " << m_FileNameIn);
        return AMF_FILE_NOT_OPEN;
    }
    m_pParser = BitStreamParser::Create(stream, GetStreamType(m_FileNameIn.c_str()), m_pContext);
    if(m_pParser == NULL)
    {
        return AMF_FAIL;
    }
    m_pParser->SetUseStartCodes(true);

    m_OutputFiles.resize(layerCount);
#if defined(SVC_TRACE_LEYERS)
    m_Indexes.resize(layerCount);
    m_DroppedSize.resize(layerCount,0);
#endif
    return AMF_OK;
}
AMF_RESULT SVCSplitter::Terminate()
{
    if(m_pParser)
    {
        m_pParser = NULL;
    }
    for(OutputFiles::iterator it =m_OutputFiles.begin(); it != m_OutputFiles.end(); it++)
    {
        if(*it != NULL)
        {
            *it = NULL;
        }
    }
    m_OutputFiles.clear();
    m_pContext->Terminate();
    m_pContext = NULL;
    g_AMFFactory.Terminate();

#if defined(SVC_TRACE_LEYERS)
    for( size_t i = 0; i < m_Indexes.size(); i++)
    {
        printf("\nStream # %d: dropped size = %d :", (int)i , (int)m_DroppedSize[i]);
        for( size_t j = 0; j < m_Indexes[i].size(); j++)
        {
            printf("%d ", (int)m_Indexes[i][j]);
        }
    }
#endif
    return AMF_OK;
}
AMF_RESULT SVCSplitter::Run()
{
    if(m_pParser == NULL)
    {
        CHECK_AMF_ERROR_RETURN(AMF_NOT_INITIALIZED, L"Not Initialized");
    }
    AMF_RESULT res = AMF_OK;

    amf_int64 inputFrameCount = 0;

#if defined(SVC_TRACE_LEYERS)
    printf("\nLevels : ");
#endif

    while(true)
    {
        amf::AMFDataPtr pData;
        res = m_pParser->QueryOutput(&pData);
        if(res != AMF_OK)
        {
            if(res != AMF_EOF)
            {
                CHECK_AMF_ERROR_RETURN(res, L"Failed to read or parce input file. Frame# " << inputFrameCount);
            }
            break;
        }
        amf::AMFBufferPtr buffer = amf::AMFBufferPtr(pData);
        amf_int32 indexTemporal = 0;
        res = GetTemporalIndex(buffer, indexTemporal);
        if(res !=AMF_OK )
        {
            break;
        }
#if defined(SVC_TRACE_LEYERS)
        printf("%d ", (int)indexTemporal);
#endif

        for( amf_int32 layerIndex = 0; layerIndex < (amf_int32)m_OutputFiles.size(); layerIndex++)
        {
//            if((amf_int32)m_OutputFiles.size() - layerIndex - 1 >= indexTemporal)
            if(layerIndex > indexTemporal)
            {
                if(m_OutputFiles[layerIndex] == NULL)
                { // create new output_file
                    std::wstring::size_type pos_dot = m_FileNameOut.rfind(L'.');
                    if(pos_dot == std::wstring::npos)
                    {
                        CHECK_AMF_ERROR_RETURN(AMF_FAIL, L"Bad file name (no extension): " << m_FileNameOut);
                    }
                    std::wstring outputPath = m_FileNameOut.substr(0, pos_dot) + L"_" + (wchar_t)(layerIndex+L'0') + m_FileNameOut.substr(pos_dot);

                    amf::AMFDataStreamPtr stream;
                    amf::AMFDataStream::OpenDataStream(outputPath.c_str(), amf::AMFSO_WRITE, amf::AMFFS_SHARE_READ, &stream);
                    if(stream == NULL )
                    {
                        LOG_ERROR(L"Failed to open file: " << outputPath);
                        return AMF_FAIL;
                    }
                    m_OutputFiles[layerIndex] = stream;
                }
                if(m_OutputFiles[layerIndex] == NULL) // shouldn't happen; just in case 
                {
                    CHECK_AMF_ERROR_RETURN(AMF_FAIL, L"Internal error"); 
                }

                amf_size written = 0;
                m_OutputFiles[layerIndex]->Write(buffer->GetNative(), buffer->GetSize(), &written);
                if(written != buffer->GetSize())
                {
                    CHECK_AMF_ERROR_RETURN(AMF_FAIL, L"Failed to write file"); 
                }
#if defined(SVC_TRACE_LEYERS)
                m_Indexes[layerIndex].push_back(inputFrameCount);
#endif
            }
            else
            {
#if defined(SVC_TRACE_LEYERS)
                m_DroppedSize[layerIndex] += buffer->GetSize();
#endif
            }
        }
        inputFrameCount++;
    }
    printf("\n");
    return res;
}
AMF_RESULT SVCSplitter::GetTemporalIndex(amf::AMFBuffer *buffer, amf_int32 &index)
{
    index = 0; // base layer;
    if(buffer == NULL)
    {
        CHECK_AMF_ERROR_RETURN(AMF_INVALID_ARG, L"No buffer");
    }
    amf_uint8 *data = (amf_uint8 *)buffer->GetNative();
    amf_size size = buffer->GetSize();

    for (amf_size i = 0; i < size; )
    {
        if (0 == data[i] &&
            0 == data[i + 1] &&
            0 == data[i + 2] &&
            1 == data[i + 3])
        {
            i += 4;
            // NAL unit - check it
            NaluHeader* pHdr = reinterpret_cast<NaluHeader*>(data+i);
            amf_uint8 naluType = pHdr->nal_unit_type;
            i++;
// this is test code 
//            if(naluType == NALU_TYPE_IDR)
//            {
//                index = 0;
//                return AMF_OK;
//            }

            if(naluType == NALU_TYPE_PREFIX || naluType == NALU_TYPE_SLC_EXT)
            {
                NaluHeaderExt* pHdrExt = reinterpret_cast<NaluHeaderExt*>(data+i);
                if (pHdrExt->reserved_three_2bits != 0x3)
                {
                    CHECK_AMF_ERROR_RETURN(AMF_INVALID_FORMAT, L"Fail: wrong Prefix syntax"); 
                }
                index = pHdrExt->temporal_id;
                return AMF_OK;
            }
   

        }
        else
        {
            i += 1;
        }
    }
    return AMF_OK;
}




int _tmain(int argc, _TCHAR* argv[])
{
    if(argc<5)
    {
        LOG_ERROR(L"Not enough arguments. cmd: SVCSplitter.exe -n <count> <input file> <output file>");
        return 1;
    }
    AMFCustomTraceWriter writer(AMF_TRACE_INFO);
    std::wstring fileIn;
    std::wstring fileOut;

    amf_int32 layerCount = 1;
    for(int i = 1; i < argc ; i++ )
    {
        if(argv[i][0] == L'-')
        {
            if(argv[i][1]== L'n')
            {
                layerCount = _wtoi(argv[i+1]);
            }
            i++;
        }
        else
        {
            if(fileIn.length() == 0)
            {
                fileIn = argv[i];
            }
            else
            {
                fileOut = argv[i];
            }
        }

    }
    if(fileIn.length() == 0 )
    {
        LOG_ERROR(L"input file name is empty");
        return 1;
    }
    if(fileOut.length() == 0 )
    {
        LOG_ERROR(L"output file name is empty");
        return 1;
    }
    if(layerCount <= 0)
    {
        LOG_ERROR(L"Number of layers must be > 0");
        return 1;
    }
    AMF_RESULT res;
    SVCSplitter splitter;
    res = splitter.Init(layerCount, fileIn.c_str(), fileOut.c_str());
    if(res != AMF_OK)
    {
        return 1;
    }
    res = splitter.Run();
    splitter.Terminate();
    if(res != AMF_OK)
    {
        return 1;
    }

    return 0;
}


